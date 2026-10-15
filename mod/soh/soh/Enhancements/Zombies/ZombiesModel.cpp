// Zombies of Time: models and animations imported from Black Ops.
//
// tools/bomap converts the player's own Black Ops models and animations into small
// binaries (tools/bomap/docs/ZZMODEL_FORMAT.md) in zombies/models/. This file loads
// them, poses the skeleton from up to two blended animations, skins the mesh on the CPU
// and draws it with OoT's lights and fog. Nothing from Black Ops ships with the mode.

#include "ZombiesModel.h"

#include <ship/Context.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>

#include "soh/frame_interpolation.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
}

// Display list macros need C linkage, like the HUD's: this sits outside the namespace.
extern "C" void ZombiesModel_Submit(PlayState* play, Gfx* dl, const Vec3f* pos, s16 yaw, float scale, bool xlu) {
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Translate(pos->x, pos->y, pos->z, MTXMODE_NEW);
    Matrix_RotateY(static_cast<f32>(yaw) * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    if (xlu) {
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, dl);
    } else {
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, dl);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

namespace Zombies {

// Black Ops models are in inches. Vertices are stored at 16 to the unit so a 72-inch
// zombie keeps sub-inch detail in OoT's 16-bit vertex coordinates.
static constexpr float kVertexScale = 16.0f;

// ---------------------------------------------------------------------------
// Small math

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

static Quat Mul(const Quat& a, const Quat& b) {
    return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
             a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

static Quat Normalize(Quat q) {
    float len = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len < 1e-6f) {
        return Quat();
    }
    return { q.x / len, q.y / len, q.z / len, q.w / len };
}

static Quat Nlerp(const Quat& a, Quat b, float t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) {
        b = { -b.x, -b.y, -b.z, -b.w };
    }
    return Normalize({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
}

static Vec3f Rotate(const Quat& q, const Vec3f& v) {
    // v + 2w(q x v) + 2 q x (q x v)
    float cx = q.y * v.z - q.z * v.y;
    float cy = q.z * v.x - q.x * v.z;
    float cz = q.x * v.y - q.y * v.x;
    float ccx = q.y * cz - q.z * cy;
    float ccy = q.z * cx - q.x * cz;
    float ccz = q.x * cy - q.y * cx;
    return { v.x + 2 * (q.w * cx + ccx), v.y + 2 * (q.w * cy + ccy), v.z + 2 * (q.w * cz + ccz) };
}

static Quat Conjugate(const Quat& q) {
    return { -q.x, -q.y, -q.z, q.w };
}

// A bone's place in model space: rotation then translation.
struct Xform {
    Quat rot;
    Vec3f pos = {};
};

static Xform Compose(const Xform& parent, const Xform& local) {
    Vec3f p = Rotate(parent.rot, local.pos);
    return { Normalize(Mul(parent.rot, local.rot)), { parent.pos.x + p.x, parent.pos.y + p.y, parent.pos.z + p.z } };
}

static Xform Inverse(const Xform& x) {
    Quat inv = Conjugate(x.rot);
    Vec3f p = Rotate(inv, x.pos);
    return { inv, { -p.x, -p.y, -p.z } };
}

// ---------------------------------------------------------------------------
// Files

class Reader {
  public:
    explicit Reader(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            mOk = false;
            return;
        }
        mData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    bool Ok() const {
        return mOk;
    }
    void Bytes(void* out, size_t n) {
        if (!mOk || mPos + n > mData.size()) {
            mOk = false;
            std::memset(out, 0, n);
            return;
        }
        std::memcpy(out, mData.data() + mPos, n);
        mPos += n;
    }
    template <typename T> T Get() {
        T v{};
        Bytes(&v, sizeof(T));
        return v;
    }
    std::string Name(size_t len) {
        std::string s(len, '\0');
        Bytes(s.data(), len);
        s.resize(strnlen(s.c_str(), len));
        return s;
    }
    size_t Remaining() const {
        return mOk && mPos < mData.size() ? mData.size() - mPos : 0;
    }
    u32 Count(size_t elemSize) {
        u32 n = Get<u32>();
        if (n > (mData.size() - std::min(mPos, mData.size())) / std::max<size_t>(elemSize, 1)) {
            mOk = false;
            return 0;
        }
        return n;
    }

  private:
    std::vector<u8> mData;
    size_t mPos = 0;
    bool mOk = true;
};

struct ModelBone {
    std::string name;
    int parent = -1;
    Xform bind;      // relative to the parent
    Xform bindWorld; // model space
};

struct ModelImage {
    u16 w = 0, h = 0;
    std::vector<u8> rgba;
};

struct ModelMaterial {
    std::string name;
    int image = -1;
    u8 flags = 0;
};

struct SkinVert {
    Vec3f pos;
    Vec3f normal;
    float uv[2];
    u8 rgba[4];
    u16 bone[4];
    float weight[4];
};

struct ModelSurface {
    u32 material = 0;
    std::vector<SkinVert> verts;
    std::vector<u16> indices;
};

struct ZModel {
    std::vector<ModelBone> bones;
    std::vector<ModelImage> images;
    std::vector<ModelMaterial> materials;
    std::vector<ModelSurface> surfaces;
    float height = 72.0f; // bind pose top, in model units
    Vec3f lo = {}, hi = {}; // bind pose bounds
    // A rigid model (a board, a gun) is drawn in its bind pose: built once.
    std::vector<Vtx> rigidVerts;
    std::vector<Gfx> rigidDl;
    // The same with a colour per untextured surface and some surfaces left out (Model_RigidDlStyled).
    std::vector<Vtx> styledVerts;
    std::vector<Gfx> styledDl;
};

struct AnimBone {
    std::string name;
    std::vector<Quat> rot;
    std::vector<Vec3f> pos;
};

// Which animation bone drives each of a model's bones (-1: none, the bind pose), matched
// by name once per model and animation pair rather than on every pose.
struct AnimBinding {
    const ZModel* model = nullptr;
    std::vector<int> animBone;
};

struct ZAnim {
    float framerate = 30.0f;
    u32 frameCount = 1;
    bool looping = false;
    std::vector<AnimBone> bones;
    std::vector<std::pair<u32, std::string>> notes;
    std::vector<AnimBinding> bindings; // a few models at most
    Vec3f travel = {};                 // root movement over the animation, when the file has it
};

static std::string ModelDir() {
    return Ship::Context::GetPathRelativeToAppDirectory("zombies/models");
}

static bool ReadImagesAndMaterials(Reader& r, std::vector<ModelImage>& images, std::vector<ModelMaterial>& mats) {
    u32 ni = r.Count(68);
    images.resize(ni);
    for (ModelImage& img : images) {
        r.Name(64);
        img.w = r.Get<u16>();
        img.h = r.Get<u16>();
        if (img.w == 0 || img.h == 0 || img.w > 256 || img.h > 256 || (img.w & (img.w - 1)) || (img.h & (img.h - 1))) {
            return false;
        }
        img.rgba.resize(static_cast<size_t>(img.w) * img.h * 4);
        r.Bytes(img.rgba.data(), img.rgba.size());
    }
    u32 nm = r.Count(69);
    mats.resize(nm);
    for (ModelMaterial& m : mats) {
        m.name = r.Name(64);
        m.image = r.Get<s32>();
        m.flags = r.Get<u8>();
        if (m.image >= static_cast<int>(ni)) {
            m.image = -1;
        }
    }
    return r.Ok();
}

static std::unique_ptr<ZModel> LoadModel(const std::string& path) {
    Reader r(path);
    char magic[8];
    r.Bytes(magic, 8);
    if (!r.Ok() || std::memcmp(magic, "ZZMODEL1", 8) != 0 || r.Get<u32>() != 1) {
        return nullptr;
    }
    auto model = std::make_unique<ZModel>();
    u32 nb = r.Count(64);
    model->bones.resize(nb);
    for (u32 i = 0; i < nb; i++) {
        ModelBone& b = model->bones[i];
        b.name = r.Name(32);
        b.parent = r.Get<s32>();
        b.bind.pos = { r.Get<float>(), r.Get<float>(), r.Get<float>() };
        b.bind.rot.x = r.Get<float>();
        b.bind.rot.y = r.Get<float>();
        b.bind.rot.z = r.Get<float>();
        b.bind.rot.w = r.Get<float>();
        b.bind.rot = Normalize(b.bind.rot);
        if (b.parent >= static_cast<int>(i)) {
            return nullptr; // parents come first
        }
        b.bindWorld = b.parent >= 0 ? Compose(model->bones[b.parent].bindWorld, b.bind) : b.bind;
    }
    if (!ReadImagesAndMaterials(r, model->images, model->materials)) {
        return nullptr;
    }
    u32 ns = r.Count(12);
    model->surfaces.resize(ns);
    float top = 0.0f;
    for (ModelSurface& s : model->surfaces) {
        s.material = r.Get<u32>();
        if (s.material >= model->materials.size()) {
            return nullptr;
        }
        u32 nv = r.Count(60); // pos, normal, uv, rgba, bones, weights
        s.verts.resize(nv);
        for (SkinVert& v : s.verts) {
            v.pos = { r.Get<float>(), r.Get<float>(), r.Get<float>() };
            v.normal = { r.Get<float>(), r.Get<float>(), r.Get<float>() };
            v.uv[0] = r.Get<float>();
            v.uv[1] = r.Get<float>();
            r.Bytes(v.rgba, 4);
            for (u16& b : v.bone) {
                b = r.Get<u16>();
                if (b >= nb && nb > 0) {
                    b = 0;
                }
            }
            for (float& w : v.weight) {
                w = r.Get<float>();
            }
            top = std::max(top, v.pos.y);
            model->lo = { std::min(model->lo.x, v.pos.x), std::min(model->lo.y, v.pos.y), std::min(model->lo.z, v.pos.z) };
            model->hi = { std::max(model->hi.x, v.pos.x), std::max(model->hi.y, v.pos.y), std::max(model->hi.z, v.pos.z) };
        }
        u32 ni = r.Count(2);
        s.indices.resize(ni);
        for (u16& i : s.indices) {
            i = r.Get<u16>();
            if (i >= nv) {
                return nullptr;
            }
        }
    }
    if (!r.Ok() || nb == 0) {
        return nullptr;
    }
    model->height = top > 1.0f ? top : 72.0f;
    return model;
}

static std::unique_ptr<ZAnim> LoadAnim(const std::string& path) {
    Reader r(path);
    char magic[8];
    r.Bytes(magic, 8);
    if (!r.Ok() || std::memcmp(magic, "ZZANIM1", 7) != 0 || r.Get<u32>() != 1) {
        return nullptr;
    }
    auto anim = std::make_unique<ZAnim>();
    anim->framerate = std::max(r.Get<float>(), 1.0f);
    anim->frameCount = std::max<u32>(r.Get<u32>(), 1);
    anim->looping = r.Get<u8>() != 0;
    u32 nb = r.Count(34);
    anim->bones.resize(nb);
    for (AnimBone& b : anim->bones) {
        b.name = r.Name(32);
        bool hasQuat = r.Get<u8>() != 0;
        bool hasPos = r.Get<u8>() != 0;
        if (hasQuat) {
            b.rot.resize(anim->frameCount);
            for (Quat& q : b.rot) {
                q.x = r.Get<float>();
                q.y = r.Get<float>();
                q.z = r.Get<float>();
                q.w = r.Get<float>();
                q = Normalize(q);
            }
        }
        if (hasPos) {
            b.pos.resize(anim->frameCount);
            for (Vec3f& p : b.pos) {
                p = { r.Get<float>(), r.Get<float>(), r.Get<float>() };
            }
        }
        if (!r.Ok()) {
            return nullptr;
        }
    }
    u32 nn = r.Count(36);
    for (u32 i = 0; i < nn; i++) {
        u32 frame = r.Get<u32>();
        anim->notes.emplace_back(frame, r.Name(32));
    }
    if (r.Ok() && r.Remaining() >= 16) {
        char tag[4];
        r.Bytes(tag, 4);
        if (std::memcmp(tag, "ROOT", 4) == 0) {
            anim->travel = { r.Get<float>(), r.Get<float>(), r.Get<float>() };
        }
    }
    return r.Ok() ? std::move(anim) : nullptr;
}

// Loaded once, kept for the session; a name that failed isn't tried again.
static std::map<std::string, std::unique_ptr<ZModel>> sModels;
static std::map<std::string, std::unique_ptr<ZAnim>> sAnims;

ZModel* Model_Get(const std::string& name) {
    auto it = sModels.find(name);
    if (it == sModels.end()) {
        it = sModels.emplace(name, LoadModel(ModelDir() + "/" + name + ".zzmodel")).first;
    }
    return it->second.get();
}

ZAnim* Anim_Get(const std::string& name) {
    auto it = sAnims.find(name);
    if (it == sAnims.end()) {
        it = sAnims.emplace(name, LoadAnim(ModelDir() + "/" + name + ".zzanim")).first;
    }
    return it->second.get();
}

float Anim_Length(const ZAnim* anim) {
    return anim != nullptr ? static_cast<float>(anim->frameCount) / anim->framerate : 0.0f;
}

bool Anim_Looping(const ZAnim* anim) {
    return anim != nullptr && anim->looping;
}

bool Anim_NoteBetween(const ZAnim* anim, const char* note, float fromSec, float toSec) {
    if (anim == nullptr) {
        return false;
    }
    for (const auto& n : anim->notes) {
        float t = static_cast<float>(n.first) / anim->framerate;
        if (t > fromSec && t <= toSec && n.second == note) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> Anim_Notes(const ZAnim* anim) {
    std::vector<std::string> out;
    if (anim != nullptr) {
        for (const auto& n : anim->notes) {
            out.push_back(n.second);
        }
    }
    return out;
}

std::vector<std::string> Anim_NotesBetween(const ZAnim* anim, float fromSec, float toSec) {
    std::vector<std::string> out;
    if (anim != nullptr) {
        for (const auto& n : anim->notes) {
            float t = static_cast<float>(n.first) / anim->framerate;
            if (t > fromSec && t <= toSec) {
                out.push_back(n.second);
            }
        }
    }
    return out;
}

float Model_Height(const ZModel* model) {
    return model != nullptr ? model->height : 0.0f;
}

// ---------------------------------------------------------------------------
// Posing

static const std::vector<int>& BindingFor(const ZAnim* anim, const ZModel* model) {
    ZAnim* a = const_cast<ZAnim*>(anim); // the cache only: the animation itself is unchanged
    for (const AnimBinding& b : a->bindings) {
        if (b.model == model) {
            return b.animBone;
        }
    }
    AnimBinding binding;
    binding.model = model;
    binding.animBone.assign(model->bones.size(), -1);
    std::map<std::string, int> byName;
    for (size_t i = 0; i < anim->bones.size(); i++) {
        byName.emplace(anim->bones[i].name, static_cast<int>(i)); // the first of a name, as before
    }
    for (size_t i = 0; i < model->bones.size(); i++) {
        auto it = byName.find(model->bones[i].name);
        if (it != byName.end()) {
            binding.animBone[i] = it->second;
        }
    }
    a->bindings.push_back(std::move(binding));
    return a->bindings.back().animBone;
}

// Where an animation is at a time: the two frames either side and how far between.
struct AnimFrame {
    u32 f0 = 0, f1 = 0;
    float t = 0.0f;
};

static AnimFrame FrameAt(const ZAnim* anim, float sec) {
    AnimFrame out;
    float frame = sec * anim->framerate;
    float last = static_cast<float>(anim->frameCount - 1);
    if (anim->looping && anim->frameCount > 1) {
        frame = fmodf(frame, static_cast<float>(anim->frameCount));
        if (frame < 0) {
            frame += static_cast<float>(anim->frameCount);
        }
    } else {
        frame = std::clamp(frame, 0.0f, last);
    }
    out.f0 = std::min(static_cast<u32>(frame), anim->frameCount - 1);
    out.f1 = anim->looping ? (out.f0 + 1) % anim->frameCount : std::min(out.f0 + 1, anim->frameCount - 1);
    out.t = frame - static_cast<float>(out.f0);
    return out;
}

// One bone's local transform: the animation's where it has the bone, else the bind pose.
static void SampleLocal(const ZAnim* anim, const ModelBone& bone, int animBone, const AnimFrame& at, Xform* out) {
    *out = bone.bind;
    if (anim == nullptr || animBone < 0) {
        return;
    }
    const AnimBone& b = anim->bones[animBone];
    u32 f0 = at.f0;
    u32 f1 = at.f1;
    float t = at.t;
    if (!b.rot.empty()) {
        out->rot = Nlerp(b.rot[f0], b.rot[f1], t);
    }
    if (!b.pos.empty()) {
        out->pos = { b.pos[f0].x + (b.pos[f1].x - b.pos[f0].x) * t, b.pos[f0].y + (b.pos[f1].y - b.pos[f0].y) * t,
                     b.pos[f0].z + (b.pos[f1].z - b.pos[f0].z) * t };
    }
}

void Model_Pose(const ZModel* model, const AnimLayer& a, const AnimLayer& b, float blend, ModelPose* out) {
    out->skin.clear();
    if (model == nullptr) {
        return;
    }
    static std::vector<Xform> world; // scratch, reused: posing runs on the game thread only
    world.resize(model->bones.size());
    bool useB = b.anim != nullptr && blend > 0.0f;
    static const std::vector<int> kNone;
    const std::vector<int>& bindA = a.anim != nullptr ? BindingFor(a.anim, model) : kNone;
    const std::vector<int>& bindB = useB ? BindingFor(b.anim, model) : kNone;
    AnimFrame atA = a.anim != nullptr ? FrameAt(a.anim, a.time) : AnimFrame();
    AnimFrame atB = useB ? FrameAt(b.anim, b.time) : AnimFrame();
    for (size_t i = 0; i < model->bones.size(); i++) {
        const ModelBone& bone = model->bones[i];
        Xform la, lb;
        SampleLocal(a.anim, bone, a.anim != nullptr ? bindA[i] : -1, atA, &la);
        Xform local = la;
        if (useB) {
            SampleLocal(b.anim, bone, bindB[i], atB, &lb);
            local.rot = Nlerp(la.rot, lb.rot, blend);
            local.pos = { la.pos.x + (lb.pos.x - la.pos.x) * blend, la.pos.y + (lb.pos.y - la.pos.y) * blend,
                          la.pos.z + (lb.pos.z - la.pos.z) * blend };
        }
        world[i] = bone.parent >= 0 ? Compose(world[bone.parent], local) : local;
    }
    out->skin.resize(model->bones.size());
    for (size_t i = 0; i < model->bones.size(); i++) {
        Xform s = Compose(world[i], Inverse(model->bones[i].bindWorld));
        out->skin[i] = { s.rot.x, s.rot.y, s.rot.z, s.rot.w, s.pos.x, s.pos.y, s.pos.z };
    }
}

// ---------------------------------------------------------------------------
// Drawing

static int Log2(int n) {
    int l = 0;
    while ((1 << l) < n) {
        l++;
    }
    return l;
}

// A bone's skinning transform as a 3x4 matrix: cheaper than a quaternion per vertex.
struct SkinMtx {
    float m[3][4];
};

static void SkinMatrices(const ModelPose& pose, std::vector<SkinMtx>& out) {
    out.resize(pose.skin.size());
    for (size_t i = 0; i < pose.skin.size(); i++) {
        const ModelPose::Skin& s = pose.skin[i];
        float x = s.qx, y = s.qy, z = s.qz, w = s.qw;
        float (*m)[4] = out[i].m;
        m[0][0] = 1.0f - 2.0f * (y * y + z * z);
        m[0][1] = 2.0f * (x * y - w * z);
        m[0][2] = 2.0f * (x * z + w * y);
        m[0][3] = s.tx;
        m[1][0] = 2.0f * (x * y + w * z);
        m[1][1] = 1.0f - 2.0f * (x * x + z * z);
        m[1][2] = 2.0f * (y * z - w * x);
        m[1][3] = s.ty;
        m[2][0] = 2.0f * (x * z - w * y);
        m[2][1] = 2.0f * (y * z + w * x);
        m[2][2] = 1.0f - 2.0f * (x * x + y * y);
        m[2][3] = s.tz;
    }
}

static Vtx MakeVtx(const std::vector<SkinMtx>& bones, const SkinVert& v) {
    Vec3f p = {};
    Vec3f n = {};
    for (int k = 0; k < 4; k++) {
        float w = v.weight[k];
        if (w <= 0.0f || v.bone[k] >= bones.size()) {
            continue;
        }
        const float (*m)[4] = bones[v.bone[k]].m;
        p.x += (m[0][0] * v.pos.x + m[0][1] * v.pos.y + m[0][2] * v.pos.z + m[0][3]) * w;
        p.y += (m[1][0] * v.pos.x + m[1][1] * v.pos.y + m[1][2] * v.pos.z + m[1][3]) * w;
        p.z += (m[2][0] * v.pos.x + m[2][1] * v.pos.y + m[2][2] * v.pos.z + m[2][3]) * w;
        n.x += (m[0][0] * v.normal.x + m[0][1] * v.normal.y + m[0][2] * v.normal.z) * w;
        n.y += (m[1][0] * v.normal.x + m[1][1] * v.normal.y + m[1][2] * v.normal.z) * w;
        n.z += (m[2][0] * v.normal.x + m[2][1] * v.normal.y + m[2][2] * v.normal.z) * w;
    }
    float nl = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (nl > 1e-5f) {
        n.x /= nl;
        n.y /= nl;
        n.z /= nl;
    }
    Vtx out = {};
    out.n.ob[0] = static_cast<s16>(std::clamp(p.x * kVertexScale, -32768.0f, 32767.0f));
    out.n.ob[1] = static_cast<s16>(std::clamp(p.y * kVertexScale, -32768.0f, 32767.0f));
    out.n.ob[2] = static_cast<s16>(std::clamp(p.z * kVertexScale, -32768.0f, 32767.0f));
    out.n.n[0] = static_cast<s8>(n.x * 120.0f);
    out.n.n[1] = static_cast<s8>(n.y * 120.0f);
    out.n.n[2] = static_cast<s8>(n.z * 120.0f);
    out.n.a = v.rgba[3];
    return out;
}

// The posed model's display list, its vertices in verts (model units x kVertexScale).
// style (optional): per surface, false leaves it out; tint colours an untextured surface.
static void BuildDl(const ZModel* model, const ModelPose& pose, u8 alpha, std::vector<Vtx>& verts,
                    std::vector<Gfx>& dl, const ModelStyle* style = nullptr) {
    verts.clear();
    dl.clear();
    size_t total = 0;
    for (const ModelSurface& s : model->surfaces) {
        total += s.indices.size();
    }
    verts.reserve(total); // pointers into it go into the display list: no reallocation from here
    // Each vertex is skinned once per surface; the triangles then copy it (they each own
    // their three corners, so a shared vertex would otherwise be skinned for every one).
    static std::vector<SkinMtx> bones;
    static std::vector<Vtx> skinned;
    SkinMatrices(pose, bones);

    for (const ModelSurface& s : model->surfaces) {
        const ModelMaterial& mat = model->materials[s.material];
        const ModelImage* image = mat.image >= 0 ? &model->images[mat.image] : nullptr;
        ModelTint tint = { 255, 255, 255 };
        if (style != nullptr && !(*style)(mat.name, image != nullptr, &tint)) {
            continue;
        }
        Gfx g[16];
        Gfx* p = g;
        gDPPipeSync(p++);
        if (image != nullptr) {
            gDPSetCombineLERP(p++, TEXEL0, 0, SHADE, 0, TEXEL0, 0, PRIMITIVE, 0, COMBINED, 0, PRIMITIVE, 0, COMBINED,
                              0, PRIMITIVE, 0);
            gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        } else {
            gDPSetCombineLERP(p++, SHADE, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, COMBINED, 0, PRIMITIVE, 0, 0, 0, 0,
                              COMBINED);
            gSPTexture(p++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        }
        gDPSetPrimColor(p++, 0, 0, tint.r, tint.g, tint.b, alpha);
        dl.insert(dl.end(), g, p);
        if (image != nullptr) {
            Gfx load[8];
            Gfx* q = load;
            gDPLoadTextureBlock(q++, image->rgba.data(), G_IM_FMT_RGBA, G_IM_SIZ_32b, image->w, image->h, 0,
                                G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, Log2(image->w), Log2(image->h),
                                G_TX_NOLOD, G_TX_NOLOD);
            dl.insert(dl.end(), load, q);
        }
        int tw = image != nullptr ? image->w : 32;
        int th = image != nullptr ? image->h : 32;
        skinned.resize(s.verts.size());
        for (size_t k = 0; k < s.verts.size(); k++) {
            skinned[k] = MakeVtx(bones, s.verts[k]);
        }
        // 30 vertices a load: ten triangles that each own their three.
        for (size_t t = 0; t + 2 < s.indices.size(); t += 30) {
            size_t n = std::min<size_t>(30, s.indices.size() - t) / 3 * 3;
            size_t base = verts.size();
            for (size_t k = 0; k < n; k += 3) {
                const u16 idx[3] = { s.indices[t + k], s.indices[t + k + 1], s.indices[t + k + 2] };
                const SkinVert* tri[3] = { &s.verts[idx[0]], &s.verts[idx[1]], &s.verts[idx[2]] };
                float offU = std::floor(std::min({ tri[0]->uv[0], tri[1]->uv[0], tri[2]->uv[0] }));
                float offV = std::floor(std::min({ tri[0]->uv[1], tri[1]->uv[1], tri[2]->uv[1] }));
                for (int c = 0; c < 3; c++) {
                    const SkinVert* sv = tri[c];
                    Vtx v = skinned[idx[c]];
                    v.n.tc[0] = static_cast<s16>(std::clamp((sv->uv[0] - offU) * static_cast<float>(tw) * 32.0f, -32768.0f, 32767.0f));
                    v.n.tc[1] = static_cast<s16>(std::clamp((sv->uv[1] - offV) * static_cast<float>(th) * 32.0f, -32768.0f, 32767.0f));
                    verts.push_back(v);
                }
            }
            Gfx vg;
            Gfx* vp = &vg;
            gSPVertex(vp++, (uintptr_t)&verts[base], static_cast<u32>(n), 0);
            dl.push_back(vg);
            for (size_t k = 0; k < n; k += 3) {
                Gfx tg;
                Gfx* tp = &tg;
                gSP1Triangle(tp++, static_cast<u8>(k), static_cast<u8>(k + 1), static_cast<u8>(k + 2), 0);
                dl.push_back(tg);
            }
        }
    }
    Gfx end;
    Gfx* ep = &end;
    gSPEndDisplayList(ep++);
    dl.push_back(end);
}

void Model_Draw(PlayState* play, const ZModel* model, const ModelPose& pose, const Vec3f& pos, s16 yaw, float height,
                ModelDrawBuffers* buffers, u8 alpha) {
    if (model == nullptr || pose.skin.size() != model->bones.size() || buffers == nullptr) {
        return;
    }
    // Two sets of vertices so the frame being drawn never reads ones being rewritten.
    std::vector<Vtx>& verts = buffers->verts[buffers->flip];
    std::vector<Gfx>& dl = buffers->dl[buffers->flip];
    buffers->flip ^= 1;
    BuildDl(model, pose, alpha, verts, dl);
    Model_Submit(play, model, dl.data(), pos, yaw, height, alpha);
}

void Model_Submit(PlayState* play, const ZModel* model, Gfx* dl, const Vec3f& pos, s16 yaw, float height, u8 alpha) {
    if (model == nullptr || dl == nullptr) {
        return;
    }
    float scale = height / (model->height * kVertexScale);
    ZombiesModel_Submit(play, dl, &pos, yaw, scale, alpha < 255);
}

Gfx* Model_RigidDl(ZModel* model) {
    if (model == nullptr) {
        return nullptr;
    }
    if (model->rigidDl.empty()) {
        ModelPose pose;
        Model_Pose(model, AnimLayer(), AnimLayer(), 0.0f, &pose);
        BuildDl(model, pose, 255, model->rigidVerts, model->rigidDl);
    }
    return model->rigidDl.data();
}

Gfx* Model_RigidDlStyled(ZModel* model, const ModelStyle& style) {
    if (model == nullptr) {
        return nullptr;
    }
    if (model->styledDl.empty()) {
        ModelPose pose;
        Model_Pose(model, AnimLayer(), AnimLayer(), 0.0f, &pose);
        BuildDl(model, pose, 255, model->styledVerts, model->styledDl, &style);
    }
    return model->styledDl.data();
}

Gfx* Model_PosedDl(const ZModel* model, const ModelPose& pose, ModelDrawBuffers* buffers, const ModelStyle* style,
                   u8 alpha) {
    if (model == nullptr || pose.skin.size() != model->bones.size() || buffers == nullptr) {
        return nullptr;
    }
    std::vector<Vtx>& verts = buffers->verts[buffers->flip];
    std::vector<Gfx>& dl = buffers->dl[buffers->flip];
    buffers->flip ^= 1;
    BuildDl(model, pose, alpha, verts, dl, style);
    return dl.data();
}

int Model_BoneCount(const ZModel* model) {
    return model != nullptr ? static_cast<int>(model->bones.size()) : 0;
}

int Model_BoneIndex(const ZModel* model, const char* name) {
    if (model != nullptr) {
        for (size_t i = 0; i < model->bones.size(); i++) {
            if (model->bones[i].name == name) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

int Model_BoneParent(const ZModel* model, int bone) {
    return model != nullptr && bone >= 0 && bone < static_cast<int>(model->bones.size()) ? model->bones[bone].parent
                                                                                         : -1;
}

static BoneXf ToBoneXf(const Xform& x) {
    BoneXf out;
    out.qx = x.rot.x;
    out.qy = x.rot.y;
    out.qz = x.rot.z;
    out.qw = x.rot.w;
    out.pos = x.pos;
    return out;
}

BoneXf Model_BoneBind(const ZModel* model, int bone) {
    if (model == nullptr || bone < 0 || bone >= static_cast<int>(model->bones.size())) {
        return BoneXf();
    }
    return ToBoneXf(model->bones[bone].bindWorld);
}

void Model_PoseWorld(const ZModel* model, const PoseLayer* layers, int count, std::vector<BoneXf>* world) {
    world->clear();
    if (model == nullptr) {
        return;
    }
    static std::vector<Xform> xf; // scratch: posing runs on the game thread only
    xf.resize(model->bones.size());
    world->resize(model->bones.size());
    for (size_t i = 0; i < model->bones.size(); i++) {
        const ModelBone& bone = model->bones[i];
        Xform local = bone.bind;
        for (int l = 0; l < count; l++) {
            const PoseLayer& layer = layers[l];
            if (layer.anim == nullptr || layer.weight <= 0.0f) {
                continue;
            }
            int ab = BindingFor(layer.anim, model)[i];
            if (ab < 0) {
                continue;
            }
            Xform s;
            SampleLocal(layer.anim, bone, ab, FrameAt(layer.anim, layer.time), &s);
            float w = std::min(layer.weight, 1.0f);
            local.rot = Nlerp(local.rot, s.rot, w);
            local.pos = { local.pos.x + (s.pos.x - local.pos.x) * w, local.pos.y + (s.pos.y - local.pos.y) * w,
                          local.pos.z + (s.pos.z - local.pos.z) * w };
        }
        xf[i] = bone.parent >= 0 ? Compose(xf[bone.parent], local) : local;
        (*world)[i] = ToBoneXf(xf[i]);
    }
}

Vec3f Anim_Travel(const ZAnim* anim) {
    return anim != nullptr ? anim->travel : Vec3f{};
}

bool Model_BonePos(const ZModel* model, const ModelPose& pose, const char* bone, Vec3f* out) {
    if (model == nullptr || pose.skin.size() != model->bones.size()) {
        return false;
    }
    for (size_t i = 0; i < model->bones.size(); i++) {
        if (model->bones[i].name == bone) {
            const ModelPose::Skin& s = pose.skin[i];
            Vec3f p = Rotate(Quat{ s.qx, s.qy, s.qz, s.qw }, model->bones[i].bindWorld.pos);
            *out = { p.x + s.tx, p.y + s.ty, p.z + s.tz };
            return true;
        }
    }
    return false;
}

void Model_Bounds(const ZModel* model, Vec3f* lo, Vec3f* hi) {
    *lo = model != nullptr ? model->lo : Vec3f{};
    *hi = model != nullptr ? model->hi : Vec3f{};
}

float Model_VertexScale() {
    return kVertexScale;
}

std::vector<std::string> Model_Manifest(const std::string& key) {
    static std::multimap<std::string, std::string> sEntries;
    static bool sRead = false;
    if (!sRead) {
        sRead = true;
        std::ifstream file(ModelDir() + "/manifest.txt");
        std::string line;
        while (std::getline(file, line)) {
            size_t space = line.find_first_of(" \t");
            if (space == std::string::npos || line[0] == '#') {
                continue;
            }
            size_t value = line.find_first_not_of(" \t", space);
            size_t end = line.find_last_not_of(" \t\r");
            if (value != std::string::npos && end >= value) {
                sEntries.emplace(line.substr(0, space), line.substr(value, end - value + 1));
            }
        }
    }
    std::vector<std::string> out;
    auto range = sEntries.equal_range(key);
    for (auto it = range.first; it != range.second; ++it) {
        out.push_back(it->second);
    }
    return out;
}

bool Model_FilesPresent() {
    std::error_code ec;
    return std::filesystem::is_directory(ModelDir(), ec);
}

} // namespace Zombies
