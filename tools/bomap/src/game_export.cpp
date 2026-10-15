#include "game_export.h"

#include "entities.h"
#include "map_light.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace bomap {

namespace {

class Bytes {
public:
    std::vector<uint8_t> b;
    void U8(uint8_t v) { b.push_back(v); }
    void U16(uint16_t v) { Raw(&v, 2); }
    void S16(int16_t v) { Raw(&v, 2); }
    void U32(uint32_t v) { Raw(&v, 4); }
    void S32(int32_t v) { Raw(&v, 4); }
    void F32(float v) { Raw(&v, 4); }
    void Raw(const void* p, size_t n) {
        const uint8_t* s = static_cast<const uint8_t*>(p);
        b.insert(b.end(), s, s + n); // the game is little-endian, like the hosts bomap runs on
    }
    // A fixed-size, NUL-terminated name (cut short to fit).
    void Name(const std::string& s, size_t size) {
        size_t n = std::min(s.size(), size - 1);
        b.insert(b.end(), s.begin(), s.begin() + n);
        b.insert(b.end(), size - n, 0);
    }
};

int16_t ToS16(float v) {
    return static_cast<int16_t>(std::lround(std::clamp(v, -32768.0f, 32767.0f)));
}

void Cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float Dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Black Ops draws triangles wound one way round; the game expects the other. Decide
// from the data: the winding whose face normals agree with the vertex normals.
bool NeedsFlip(const std::vector<Surface>& surfaces) {
    double agree = 0;
    for (const Surface& s : surfaces) {
        for (size_t t = 0; t + 2 < s.tris.size(); t += 3) {
            const Vertex& a = s.verts[s.tris[t]];
            const Vertex& b = s.verts[s.tris[t + 1]];
            const Vertex& c = s.verts[s.tris[t + 2]];
            float u[3], v[3], n[3];
            for (int k = 0; k < 3; k++) {
                u[k] = b.pos[k] - a.pos[k];
                v[k] = c.pos[k] - a.pos[k];
            }
            Cross(u, v, n);
            agree += Dot(n, a.normal) + Dot(n, b.normal) + Dot(n, c.normal) > 0 ? 1 : -1;
        }
    }
    return agree < 0;
}

void WriteImages(Bytes& out, const std::vector<Texture>& images) {
    out.U32(static_cast<uint32_t>(images.size()));
    for (const Texture& t : images) {
        out.Name(t.name, 64);
        out.U16(static_cast<uint16_t>(t.width));
        out.U16(static_cast<uint16_t>(t.height));
        out.Raw(t.rgba.data(), t.rgba.size());
    }
}

struct MaterialTable {
    std::vector<std::string> names;
    std::vector<int32_t> image;
    std::vector<uint8_t> flags;
    std::vector<Texture> images;
    std::map<std::string, uint32_t> index;
    std::map<std::string, int32_t> imageIndex;
    std::vector<std::string> missing;

    uint32_t Add(const WalkResult& walk, const std::string& material, TextureSource* textures, int maxTexture) {
        auto it = index.find(material);
        if (it != index.end()) {
            return it->second;
        }
        int32_t img = -1;
        std::string map = MaterialColorMap(walk, material);
        if (!map.empty()) {
            auto found = imageIndex.find(map);
            if (found != imageIndex.end()) {
                img = found->second;
            } else {
                Texture tex;
                if (textures != nullptr && textures->Load(map, maxTexture, tex)) {
                    img = static_cast<int32_t>(images.size());
                    images.push_back(std::move(tex));
                } else {
                    missing.push_back(map);
                }
                imageIndex[map] = img;
            }
        }
        uint32_t i = static_cast<uint32_t>(names.size());
        names.push_back(material);
        image.push_back(img);
        flags.push_back(MaterialFlags(walk, material));
        index[material] = i;
        return i;
    }

    void Write(Bytes& out) const {
        WriteImages(out, images);
        out.U32(static_cast<uint32_t>(names.size()));
        for (size_t i = 0; i < names.size(); i++) {
            out.Name(names[i], 64);
            out.S32(image[i]);
            out.U8(flags[i]);
        }
    }
};

} // namespace

void ToOotAxes(const float in[3], float out[3]) {
    float x = in[0], y = in[1], z = in[2];
    out[0] = x;
    out[1] = z;
    out[2] = -y;
}

void QuatToOotAxes(const float in[4], float out[4]) {
    float v[3] = { in[0], in[1], in[2] };
    float w = in[3];
    ToOotAxes(v, out);
    out[3] = w;
}

// ---------------------------------------------------------------- attaching models

namespace {

struct Xform {
    float q[4] = { 0, 0, 0, 1 };
    float t[3] = { 0, 0, 0 };
};

void QMul(const float a[4], const float b[4], float out[4]) {
    float r[4] = { a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                   a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2] };
    memcpy(out, r, sizeof(r));
}

void QRot(const float q[4], const float v[3], float out[3]) {
    float p[4] = { v[0], v[1], v[2], 0 }, c[4] = { -q[0], -q[1], -q[2], q[3] }, a[4], b[4];
    QMul(q, p, a);
    QMul(a, c, b);
    out[0] = b[0];
    out[1] = b[1];
    out[2] = b[2];
}

Xform Compose(const Xform& parent, const float q[4], const float t[3]) {
    Xform x;
    QMul(parent.q, q, x.q);
    QRot(parent.q, t, x.t);
    for (int k = 0; k < 3; k++) {
        x.t[k] += parent.t[k];
    }
    return x;
}

Xform World(const Bone& b) {
    Xform x;
    memcpy(x.q, b.worldQuat, sizeof(x.q));
    memcpy(x.t, b.worldTrans, sizeof(x.t));
    return x;
}

// `to` applied after the inverse of `from`, on a point (or a direction when `point` is false).
void Rebind(const Xform& from, const Xform& to, const float in[3], float out[3], bool point) {
    float local[3], inv[4] = { -from.q[0], -from.q[1], -from.q[2], from.q[3] };
    float d[3] = { in[0] - (point ? from.t[0] : 0), in[1] - (point ? from.t[1] : 0), in[2] - (point ? from.t[2] : 0) };
    QRot(inv, d, local);
    QRot(to.q, local, out);
    for (int k = 0; k < 3 && point; k++) {
        out[k] += to.t[k];
    }
}

} // namespace

Model AttachModel(const Model& base, const Model& part, const std::string& tag) {
    int tagBone = -1;
    for (size_t j = 0; j < base.bones.size() && !tag.empty(); j++) {
        if (base.bones[j].name == tag) {
            tagBone = static_cast<int>(j);
        }
    }
    if (!tag.empty() && tagBone < 0) {
        throw std::runtime_error(base.name + " has no bone " + tag + " to attach " + part.name + " to");
    }
    Model merged = base;
    std::vector<int> map(part.bones.size(), -1);
    std::vector<Xform> partWorld(part.bones.size());
    for (size_t i = 0; i < part.bones.size(); i++) {
        const Bone& pb = part.bones[i];
        partWorld[i] = World(pb);
        for (size_t j = 0; j < merged.bones.size(); j++) {
            if (merged.bones[j].name == pb.name) {
                map[i] = static_cast<int>(j);
                break;
            }
        }
        if (map[i] >= 0) {
            continue;
        }
        Bone nb = pb;
        int parent = pb.parent >= 0 ? map[pb.parent] : tagBone;
        nb.parent = parent;
        Xform w = parent >= 0 ? Compose(World(merged.bones[parent]), pb.localQuat, pb.localTrans) : World(pb);
        memcpy(nb.worldQuat, w.q, sizeof(w.q));
        memcpy(nb.worldTrans, w.t, sizeof(w.t));
        map[i] = static_cast<int>(merged.bones.size());
        merged.bones.push_back(nb);
    }
    for (const Surface& s : part.surfaces) {
        Surface ns = s;
        for (Vertex& v : ns.verts) {
            float pos[3] = {}, normal[3] = {};
            for (int k = 0; k < 4; k++) {
                if (v.weights[k] <= 0) {
                    continue;
                }
                int b = v.bones[k];
                Xform to = World(merged.bones[map[b]]);
                float p[3], n[3];
                Rebind(partWorld[b], to, v.pos, p, true);
                Rebind(partWorld[b], to, v.normal, n, false);
                for (int c = 0; c < 3; c++) {
                    pos[c] += v.weights[k] * p[c];
                    normal[c] += v.weights[k] * n[c];
                }
                v.bones[k] = map[b];
            }
            for (int k = 0; k < 4; k++) {
                if (v.weights[k] <= 0) {
                    v.bones[k] = map[v.bones[k]] >= 0 ? map[v.bones[k]] : 0;
                }
            }
            float len = std::sqrt(Dot(normal, normal));
            for (int c = 0; c < 3; c++) {
                v.pos[c] = pos[c];
                v.normal[c] = len > 0 ? normal[c] / len : 0;
            }
        }
        merged.surfaces.push_back(std::move(ns));
    }
    merged.name = base.name + "+" + part.name;
    return merged;
}

// ---------------------------------------------------------------- ZZMODEL1

std::vector<uint8_t> WriteGameModel(const Zone& zone, const WalkResult& walk, int asset, TextureSource* textures,
                                    int maxTexture, GameModelInfo* info, const Model* attach) {
    Model m = ReadModel(zone, walk, asset);
    if (attach != nullptr) {
        m = AttachModel(m, *attach);
    }
    return WriteGameModel(m, walk, textures, maxTexture, info);
}

std::vector<uint8_t> WriteGameModel(const Model& m, const WalkResult& walk, TextureSource* textures, int maxTexture,
                                    GameModelInfo* info, const WalkResult* otherWalk, TextureSource* otherTextures) {
    Bytes out;
    out.Raw("ZZMODEL1", 8);
    out.U32(1);
    out.U32(static_cast<uint32_t>(m.bones.size()));
    for (const Bone& b : m.bones) {
        float pos[3], quat[4];
        ToOotAxes(b.localTrans, pos);
        QuatToOotAxes(b.localQuat, quat);
        out.Name(b.name, 32);
        out.S32(b.parent);
        for (float v : pos) {
            out.F32(v);
        }
        for (float v : quat) {
            out.F32(v);
        }
    }
    MaterialTable mats;
    std::vector<uint32_t> surfMat;
    for (const Surface& s : m.surfaces) {
        // A part attached from another zone has its materials there.
        bool other = otherWalk != nullptr && walk.Find(0x06, s.material) < 0 && otherWalk->Find(0x06, s.material) >= 0;
        surfMat.push_back(other ? mats.Add(*otherWalk, s.material, otherTextures, maxTexture)
                                : mats.Add(walk, s.material, textures, maxTexture));
    }
    mats.Write(out);
    bool flip = NeedsFlip(m.surfaces);
    size_t verts = 0, tris = 0;
    out.U32(static_cast<uint32_t>(m.surfaces.size()));
    for (size_t si = 0; si < m.surfaces.size(); si++) {
        const Surface& s = m.surfaces[si];
        out.U32(surfMat[si]);
        out.U32(static_cast<uint32_t>(s.verts.size()));
        for (const Vertex& v : s.verts) {
            float pos[3], normal[3];
            ToOotAxes(v.pos, pos);
            ToOotAxes(v.normal, normal);
            for (float f : pos) {
                out.F32(f);
            }
            for (float f : normal) {
                out.F32(f);
            }
            out.F32(v.uv[0]);
            out.F32(v.uv[1]);
            for (int k = 0; k < 4; k++) {
                out.U8(255);
            }
            float total = 0;
            for (int k = 0; k < 4; k++) {
                total += v.weights[k] > 0 ? v.weights[k] : 0;
            }
            for (int k = 0; k < 4; k++) {
                bool used = v.weights[k] > 0;
                out.U16(static_cast<uint16_t>(used ? v.bones[k] : v.bones[0]));
            }
            for (int k = 0; k < 4; k++) {
                float w = v.weights[k] > 0 && total > 0 ? v.weights[k] / total : 0;
                out.F32(k == 0 && total <= 0 ? 1.0f : w);
            }
        }
        out.U32(static_cast<uint32_t>(s.tris.size()));
        for (size_t t = 0; t + 2 < s.tris.size(); t += 3) {
            out.U16(s.tris[t]);
            out.U16(s.tris[flip ? t + 2 : t + 1]);
            out.U16(s.tris[flip ? t + 1 : t + 2]);
        }
        verts += s.verts.size();
        tris += s.tris.size() / 3;
    }
    if (info != nullptr) {
        info->bones = m.bones.size();
        info->surfaces = m.surfaces.size();
        info->verts = verts;
        info->tris = tris;
        info->images = mats.images.size();
        info->missingImages = mats.missing;
        info->flippedWinding = flip;
    }
    return out.b;
}

// ---------------------------------------------------------------- ZZANIM1

std::vector<uint8_t> WriteGameAnim(const Anim& anim, const Model* bind, GameAnimInfo* info) {
    int frames = anim.numFrames + 1;
    Bytes out;
    out.Raw("ZZANIM1\0", 8);
    out.U32(1);
    out.F32(anim.framerate);
    out.U32(static_cast<uint32_t>(frames));
    out.U8(anim.looping ? 1 : 0);
    out.U32(static_cast<uint32_t>(anim.bones.size()));
    size_t withPos = 0;
    for (const AnimBone& ab : anim.bones) {
        const Bone* bindBone = nullptr;
        if (bind != nullptr) {
            for (const Bone& b : bind->bones) {
                if (b.name == ab.name) {
                    bindBone = &b;
                }
            }
        }
        bool hasPos = !ab.trans.keys.empty() && bindBone != nullptr;
        withPos += hasPos ? 1 : 0;
        out.Name(ab.name, 32);
        out.U8(1);
        out.U8(hasPos ? 1 : 0);
        for (int f = 0; f < frames; f++) {
            std::vector<float> q = SampleTrack(ab.rot, static_cast<float>(f));
            float conv[4];
            QuatToOotAxes(q.data(), conv);
            for (float v : conv) {
                out.F32(v);
            }
        }
        if (hasPos) {
            for (int f = 0; f < frames; f++) {
                std::vector<float> p = SampleTrack(ab.trans, static_cast<float>(f));
                float local[3], conv[3];
                for (int k = 0; k < 3; k++) {
                    local[k] = bindBone->animBase[k] + p[k];
                }
                ToOotAxes(local, conv);
                for (float v : conv) {
                    out.F32(v);
                }
            }
        }
    }
    out.U32(static_cast<uint32_t>(anim.notetracks.size()));
    for (const auto& [name, time] : anim.notetracks) {
        out.U32(static_cast<uint32_t>(std::lround(time * anim.numFrames)));
        out.Name(name, 32);
    }
    // Optional trailer (readers that stop after the notetracks ignore it): how far the
    // animation carries the character, in OoT axes, so a walk can be played at the pace
    // the character moves.
    float travel[3] = { 0, 0, 0 };
    if (!anim.rootTrans.keys.empty() && anim.rootTrans.keys.back().size() >= 3) {
        ToOotAxes(anim.rootTrans.keys.back().data(), travel);
    }
    out.Raw("ROOT", 4);
    for (float v : travel) {
        out.F32(v);
    }
    if (info != nullptr) {
        info->bones = anim.bones.size();
        info->bonesWithPos = withPos;
        info->frames = frames;
        info->seconds = anim.framerate > 0 ? anim.numFrames / anim.framerate : 0;
        if (!anim.rootTrans.keys.empty()) {
            const std::vector<float>& end = anim.rootTrans.keys.back();
            info->rootDistance = std::sqrt(end[0] * end[0] + end[1] * end[1] + end[2] * end[2]);
        }
    }
    return out.b;
}

// ---------------------------------------------------------------- ZZBOMAP1

namespace {

// Black Ops surface type (bits 20..24 of the surface flags) to the game's six.
uint8_t SurfaceKind(uint32_t surfaceFlags) {
    switch ((surfaceFlags >> 20) & 31) {
    case 6: case 11: case 14: case 18: case 19: return 1;       // dirt, gravel, mud, sand, snow
    case 1: case 3: case 4: case 15: case 21: case 26: return 2; // bark, carpet, cloth, paper, wood, cushion
    case 13: case 28: return 3;                                  // metal, painted metal
    case 8: case 10: case 30: return 4;                          // foliage, grass, tall grass
    case 20: return 5;                                           // water
    default: return 0;                                           // stone and everything else
    }
}

constexpr uint32_t kContentsSolid = 0x1;
constexpr uint32_t kContentsPlayerClip = 0x10000;
constexpr uint32_t kContentsMonsterClip = 0x20000;

struct ColBuilder {
    float scale = 1;
    std::vector<int16_t> verts;
    std::unordered_map<uint64_t, uint16_t> weld;
    struct Tri {
        uint16_t v[3];
        uint8_t surface, flags;
    };
    std::vector<Tri> tris;

    uint16_t Vert(const float cod[3]) {
        float o[3];
        ToOotAxes(cod, o);
        int16_t s[3] = { ToS16(o[0] * scale), ToS16(o[1] * scale), ToS16(o[2] * scale) };
        uint64_t key = static_cast<uint64_t>(static_cast<uint16_t>(s[0])) |
                       static_cast<uint64_t>(static_cast<uint16_t>(s[1])) << 16 |
                       static_cast<uint64_t>(static_cast<uint16_t>(s[2])) << 32;
        auto it = weld.find(key);
        if (it != weld.end()) {
            return it->second;
        }
        if (verts.size() / 3 >= 0xFFFF) {
            throw std::runtime_error("collision needs more than 65535 vertices");
        }
        uint16_t i = static_cast<uint16_t>(verts.size() / 3);
        verts.insert(verts.end(), s, s + 3);
        weld[key] = i;
        return i;
    }

    // A triangle wound so its normal (b - a) x (c - a) faces out.
    void Add(const float a[3], const float b[3], const float c[3], uint8_t surface, uint8_t flags) {
        Tri t = { { Vert(a), Vert(b), Vert(c) }, surface, flags };
        if (t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[0] == t.v[2]) {
            return;
        }
        tris.push_back(t);
    }
};

constexpr size_t kMaxColTris = 0x7FFF;

// Snaps the collision vertices to a grid of `cell` units, merging the ones that land
// together, and drops the triangles that collapse or repeat.
void WeldCollision(ColBuilder& col, int cell) {
    std::unordered_map<uint64_t, uint16_t> merged;
    std::vector<int16_t> verts;
    std::vector<uint16_t> remap(col.verts.size() / 3);
    for (size_t i = 0; i < remap.size(); i++) {
        int16_t snapped[3];
        for (int c = 0; c < 3; c++) {
            float v = std::round(static_cast<float>(col.verts[i * 3 + c]) / cell) * cell;
            snapped[c] = static_cast<int16_t>(std::clamp(v, -32768.0f, 32767.0f));
        }
        uint64_t key = static_cast<uint64_t>(static_cast<uint16_t>(snapped[0])) |
                       static_cast<uint64_t>(static_cast<uint16_t>(snapped[1])) << 16 |
                       static_cast<uint64_t>(static_cast<uint16_t>(snapped[2])) << 32;
        auto it = merged.find(key);
        if (it == merged.end()) {
            it = merged.emplace(key, static_cast<uint16_t>(verts.size() / 3)).first;
            verts.insert(verts.end(), snapped, snapped + 3);
        }
        remap[i] = it->second;
    }
    std::set<std::array<uint16_t, 3>> seen;
    std::vector<ColBuilder::Tri> tris;
    for (ColBuilder::Tri t : col.tris) {
        for (uint16_t& v : t.v) {
            v = remap[v];
        }
        if (t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[0] == t.v[2]) {
            continue;
        }
        std::array<uint16_t, 3> key = { t.v[0], t.v[1], t.v[2] };
        std::sort(key.begin(), key.end());
        if (seen.insert(key).second) {
            tris.push_back(t);
        }
    }
    col.verts = std::move(verts);
    col.tris = std::move(tris);
    col.weld.clear();
}

struct Plane {
    float n[3];
    float d;
    uint32_t sflags;
};

// Each face of a convex brush is the polygon of its vertices lying on that plane.
size_t BrushFaces(ColBuilder& col, const std::vector<Plane>& planes, const std::vector<std::array<float, 3>>& verts,
                  uint8_t flags) {
    size_t added = 0;
    for (const Plane& p : planes) {
        std::vector<std::array<float, 3>> on;
        for (const auto& v : verts) {
            if (std::fabs(Dot(p.n, v.data()) - p.d) < 0.25f) {
                on.push_back(v);
            }
        }
        if (on.size() < 3) {
            continue;
        }
        float c[3] = {};
        for (const auto& v : on) {
            for (int k = 0; k < 3; k++) {
                c[k] += v[k] / on.size();
            }
        }
        float u[3] = { 1, 0, 0 };
        if (std::fabs(p.n[0]) > 0.9f) {
            u[0] = 0;
            u[1] = 1;
        }
        float t[3];
        Cross(p.n, u, t);
        Cross(t, p.n, u);
        Cross(p.n, u, t);
        std::sort(on.begin(), on.end(), [&](const auto& a, const auto& b) {
            float da[3] = { a[0] - c[0], a[1] - c[1], a[2] - c[2] };
            float db[3] = { b[0] - c[0], b[1] - c[1], b[2] - c[2] };
            return std::atan2(Dot(da, t), Dot(da, u)) < std::atan2(Dot(db, t), Dot(db, u));
        });
        uint8_t surface = SurfaceKind(p.sflags);
        for (size_t i = 1; i + 1 < on.size(); i++) {
            size_t before = col.tris.size();
            col.Add(on[0].data(), on[i].data(), on[i + 1].data(), surface, flags);
            added += col.tris.size() - before;
        }
    }
    return added;
}

// Brushes reachable from a leaf brush node tree.
void CollectBrushes(const WalkResult& walk, size_t nodes, uint32_t nodeCount, uint32_t node, std::set<uint32_t>& out,
                    std::set<uint32_t>& seen) {
    const ZoneStream& s = *walk.stream;
    while (node < nodeCount && seen.insert(node).second) {
        size_t n = nodes + node * 20;
        int16_t count = static_cast<int16_t>(s.U16(n + 2));
        if (count > 0) {
            size_t list = walk.Pointer(n + 8);
            for (int16_t k = 0; k < count && list != kNoData; k++) {
                out.insert(s.U16(list + k * 2));
            }
            return;
        }
        if (count < 0) {
            CollectBrushes(walk, nodes, nodeCount, node + 1, out, seen);
        }
        uint16_t c0 = s.U16(n + 16), c1 = s.U16(n + 18);
        if (c0 != 0) {
            CollectBrushes(walk, nodes, nodeCount, node + c0, out, seen);
        }
        if (c1 == 0) {
            return;
        }
        node += c1;
    }
}

bool SkipVisual(const std::string& material, const WalkResult& walk) {
    std::string m = material;
    std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* word : { "sky", "caulk", "nodraw", "shadowcaster", "portal", "clip", "mantle", "lightgrid" }) {
        if (m.find(word) != std::string::npos) {
            return true;
        }
    }
    int index = walk.Find(0x06, material);
    std::string techset = index >= 0 ? walk.materials.at(index).techset : std::string();
    // Unlit decals (chalk, stains) sit exactly on walls and would flicker in the game.
    return techset.find("decal") != std::string::npos;
}

struct MeshBuilder {
    struct V {
        int16_t pos[3];
        float uv[2];
        uint8_t rgba[4];
    };
    struct Mesh {
        uint32_t material;
        std::vector<V> verts;
        std::vector<uint16_t> indices;
    };
    std::vector<Mesh> meshes;
    std::map<uint32_t, size_t> open; // material -> mesh still taking vertices

    Mesh& For(uint32_t material, size_t newVerts) {
        auto it = open.find(material);
        if (it == open.end() || meshes[it->second].verts.size() + newVerts > 0xFFFF) {
            meshes.push_back({ material, {}, {} });
            open[material] = meshes.size() - 1;
            return meshes.back();
        }
        return meshes[it->second];
    }
};


// A world surface on its way to the map file, in Black Ops axes.
struct WorldVert {
    float pos[3];
    float normal[3];
    float uv[2];
    float lm[2];
    float alpha;
};

struct WorldSurface {
    uint32_t material = 0;
    int light = 0; // the surface's primary light
    std::vector<WorldVert> verts;
    std::vector<uint32_t> tris;
};

WorldVert Midpoint(const WorldVert& a, const WorldVert& b) {
    WorldVert m;
    for (int c = 0; c < 3; c++) {
        m.pos[c] = (a.pos[c] + b.pos[c]) * 0.5f;
        m.normal[c] = a.normal[c] + b.normal[c];
    }
    float len = std::sqrt(Dot(m.normal, m.normal));
    for (float& c : m.normal) {
        c = len > 0 ? c / len : 0.0f;
    }
    for (int c = 0; c < 2; c++) {
        m.uv[c] = (a.uv[c] + b.uv[c]) * 0.5f;
        m.lm[c] = (a.lm[c] + b.lm[c]) * 0.5f;
    }
    m.alpha = (a.alpha + b.alpha) * 0.5f;
    return m;
}

float EdgeLength(const WorldVert& a, const WorldVert& b) {
    float d[3] = { a.pos[0] - b.pos[0], a.pos[1] - b.pos[1], a.pos[2] - b.pos[2] };
    return std::sqrt(Dot(d, d));
}

// Splits every edge longer than `maxEdge` at its middle, again and again. Whether an
// edge splits depends only on the edge, so neighbouring triangles split it the same
// way and no cracks open. Returns false (leaving `ws` as it was) past 65535 vertices.
bool SplitLongEdges(WorldSurface& ws, float maxEdge, bool countOnly, size_t* triCount) {
    std::vector<WorldVert> verts = ws.verts;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> mids;
    std::vector<uint32_t> out;
    std::vector<std::array<uint32_t, 3>> work;
    for (size_t t = 0; t + 2 < ws.tris.size(); t += 3) {
        work.push_back({ ws.tris[t], ws.tris[t + 1], ws.tris[t + 2] });
    }
    size_t count = 0;
    auto mid = [&](uint32_t a, uint32_t b) {
        auto key = std::make_pair(std::min(a, b), std::max(a, b));
        auto it = mids.find(key);
        if (it != mids.end()) {
            return it->second;
        }
        uint32_t i = static_cast<uint32_t>(verts.size());
        verts.push_back(Midpoint(verts[a], verts[b]));
        mids.emplace(key, i);
        return i;
    };
    while (!work.empty()) {
        std::array<uint32_t, 3> t = work.back();
        work.pop_back();
        bool split[3];
        int n = 0;
        for (int e = 0; e < 3; e++) {
            split[e] = EdgeLength(verts[t[e]], verts[t[(e + 1) % 3]]) > maxEdge;
            n += split[e];
        }
        if (n == 0) {
            count++;
            if (!countOnly) {
                out.insert(out.end(), t.begin(), t.end());
            }
            continue;
        }
        // Rotate so the first split edge is t0-t1 (and for two, the unsplit one is t2-t0).
        int r = 0;
        if (n == 1) {
            r = split[0] ? 0 : split[1] ? 1 : 2;
        } else if (n == 2) {
            r = !split[2] ? 0 : !split[0] ? 1 : 2;
        }
        uint32_t a = t[r], b = t[(r + 1) % 3], c = t[(r + 2) % 3];
        uint32_t ab = mid(a, b);
        if (n == 1) {
            work.push_back({ a, ab, c });
            work.push_back({ ab, b, c });
        } else if (n == 2) {
            uint32_t bc = mid(b, c);
            work.push_back({ ab, b, bc });
            work.push_back({ a, ab, bc });
            work.push_back({ a, bc, c });
        } else {
            uint32_t bc = mid(b, c), ca = mid(c, a);
            work.push_back({ a, ab, ca });
            work.push_back({ ab, b, bc });
            work.push_back({ ca, bc, c });
            work.push_back({ ab, bc, ca });
        }
        if (verts.size() > 0xFFFF) {
            return false;
        }
    }
    if (triCount != nullptr) {
        *triCount = count;
    }
    if (!countOnly) {
        ws.verts = std::move(verts);
        ws.tris = std::move(out);
    }
    return true;
}

size_t SubdividedTriCount(const WorldSurface& ws, float maxEdge) {
    size_t count = 0;
    WorldSurface copy;
    copy.tris = ws.tris;
    copy.verts = ws.verts;
    if (!SplitLongEdges(copy, maxEdge, true, &count)) {
        return ws.tris.size() / 3;
    }
    return count;
}

void Subdivide(WorldSurface& ws, float maxEdge) {
    SplitLongEdges(ws, maxEdge, false, nullptr);
}

} // namespace

Model ReadBrushModel(const Zone& zone, const WalkResult& walk, int submodel) {
    (void)zone;
    int worldIndex = -1;
    for (size_t i = 0; i < walk.assets.size(); i++) {
        if (walk.assets[i].type == 0x11 && walk.assets[i].header != kNoData) {
            worldIndex = static_cast<int>(i);
        }
    }
    if (worldIndex < 0) {
        throw std::runtime_error("this zone has no GfxWorld, so no brush models");
    }
    const ZoneStream& s = *walk.stream;
    size_t gw = walk.assets[worldIndex].header;
    uint32_t count = s.U32(gw + 592);
    size_t bmodels = walk.Pointer(gw + 596);
    if (submodel <= 0 || static_cast<uint32_t>(submodel) >= count || bmodels == kNoData) {
        throw std::runtime_error("no brush model *" + std::to_string(submodel) + " (the map has " +
                                 std::to_string(count) + ")");
    }
    size_t bm = bmodels + static_cast<size_t>(submodel) * 60;
    uint32_t surfCount = s.U32(bm + 52), start = s.U32(bm + 56);
    size_t draw = gw + 344;
    size_t gverts = walk.Pointer(draw + 160), gindices = walk.Pointer(draw + 188), surfaces = walk.Pointer(gw + 836 + 88);
    uint32_t gvertCount = s.U32(draw + 156), gindexCount = s.U32(draw + 184), allSurfaces = s.U32(gw + 16);
    Model model;
    model.name = "*" + std::to_string(submodel);
    model.bones.push_back(Bone());
    model.bones[0].name = "tag_origin";
    for (uint32_t i = 0; i < surfCount && start + i < allSurfaces && surfaces != kNoData; i++) {
        size_t sf = surfaces + static_cast<size_t>(start + i) * 80;
        int matIndex = walk.AssetAt(sf + 48);
        uint32_t first = s.U32(sf + 28), base = s.U32(sf + 36), triCount = s.U16(sf + 34);
        if (matIndex < 0 || triCount == 0 || base + triCount * 3 > gindexCount) {
            continue;
        }
        Surface out;
        out.material = walk.assets[matIndex].name;
        std::map<uint32_t, uint16_t> remap;
        for (uint32_t k = 0; k < triCount * 3; k++) {
            uint32_t vi = first + s.U16(gindices + (base + k) * 2);
            if (vi >= gvertCount) {
                throw std::runtime_error("brush model vertex out of range");
            }
            auto it = remap.find(vi);
            if (it == remap.end()) {
                size_t v = gverts + static_cast<size_t>(vi) * 44;
                Vertex vert;
                uint32_t packed = s.U32(v + 36);
                float sc = ((packed >> 24) + 192.0f) / 32385.0f;
                for (int c = 0; c < 3; c++) {
                    vert.pos[c] = s.F32(v + c * 4);
                    vert.normal[c] = ((packed >> (c * 8) & 0xFF) - 127.0f) * sc;
                }
                vert.uv[0] = s.F32(v + 20);
                vert.uv[1] = s.F32(v + 24);
                it = remap.emplace(vi, static_cast<uint16_t>(out.verts.size())).first;
                out.verts.push_back(vert);
            }
            out.tris.push_back(it->second);
        }
        model.surfaces.push_back(std::move(out));
    }
    if (model.surfaces.empty()) {
        throw std::runtime_error(model.name + " has no visible surfaces (it is collision only)");
    }
    return model;
}

std::vector<uint8_t> WriteMapGeo(const Zone& zone, const WalkResult& walk, const MapGeoOptions& options,
                                 MapGeoInfo* info) {
    const ZoneStream& s = *walk.stream;
    MapGeoInfo stats;
    int clipIndex = -1, worldIndex = -1;
    for (size_t i = 0; i < walk.assets.size(); i++) {
        if (walk.assets[i].header == kNoData) {
            continue;
        }
        if ((walk.assets[i].type == 0x0B || walk.assets[i].type == 0x0C) && clipIndex < 0) {
            clipIndex = static_cast<int>(i);
        }
        if (walk.assets[i].type == 0x11 && worldIndex < 0) {
            worldIndex = static_cast<int>(i);
        }
    }
    if (clipIndex < 0 || worldIndex < 0) {
        throw std::runtime_error("not a map zone: it has no collision map or world geometry");
    }

    // ---- collision
    ColBuilder col;
    col.scale = options.scale;
    size_t cm = walk.assets[clipIndex].header;
    uint32_t brushCount = s.U16(cm + 148);
    size_t brushes = walk.Pointer(cm + 152);
    uint32_t nodeCount = s.U32(cm + 56);
    size_t nodes = walk.Pointer(cm + 60);
    // Brush models (doors, debris, triggers) move or vanish in game: leave them out.
    std::set<uint32_t> submodelBrushes;
    uint32_t cmodelCount = s.U32(cm + 140);
    size_t cmodels = walk.Pointer(cm + 144);
    for (uint32_t i = 1; i < cmodelCount && cmodels != kNoData && nodes != kNoData; i++) {
        std::set<uint32_t> seen;
        CollectBrushes(walk, nodes, nodeCount, s.U32(cmodels + i * 72 + 64), submodelBrushes, seen);
    }
    for (uint32_t bi = 0; bi < brushCount && brushes != kNoData; bi++) {
        size_t b = brushes + bi * 96;
        uint32_t contents = s.U32(b + 12);
        bool clip = (contents & (kContentsPlayerClip | kContentsMonsterClip)) != 0;
        if (submodelBrushes.count(bi) || (!(contents & kContentsSolid) && !clip)) {
            stats.skippedBrushes++;
            continue;
        }
        stats.worldBrushes++;
        float mins[3] = { s.F32(b), s.F32(b + 4), s.F32(b + 8) };
        float maxs[3] = { s.F32(b + 16), s.F32(b + 20), s.F32(b + 24) };
        std::vector<Plane> planes;
        for (int axis = 0; axis < 3; axis++) {
            Plane lo = { { 0, 0, 0 }, -mins[axis], s.U32(b + 60 + axis * 4) };
            lo.n[axis] = -1;
            Plane hi = { { 0, 0, 0 }, maxs[axis], s.U32(b + 60 + (3 + axis) * 4) };
            hi.n[axis] = 1;
            planes.push_back(lo);
            planes.push_back(hi);
        }
        uint32_t numSides = s.U32(b + 28);
        size_t sides = walk.Pointer(b + 32);
        for (uint32_t k = 0; k < numSides && sides != kNoData; k++) {
            size_t side = sides + k * 12;
            size_t plane = walk.Pointer(side);
            if (plane == kNoData) {
                continue;
            }
            planes.push_back({ { s.F32(plane), s.F32(plane + 4), s.F32(plane + 8) }, s.F32(plane + 12), s.U32(side + 8) });
        }
        std::vector<std::array<float, 3>> verts;
        uint32_t numVerts = s.U32(b + 84);
        size_t vp = walk.Pointer(b + 88);
        for (uint32_t k = 0; k < numVerts && vp != kNoData; k++) {
            verts.push_back({ s.F32(vp + k * 12), s.F32(vp + k * 12 + 4), s.F32(vp + k * 12 + 8) });
        }
        if (verts.empty() && numSides == 0) {
            for (int k = 0; k < 8; k++) {
                verts.push_back({ (k & 1) ? maxs[0] : mins[0], (k & 2) ? maxs[1] : mins[1], (k & 4) ? maxs[2] : mins[2] });
            }
        }
        stats.brushTris += BrushFaces(col, planes, verts, (contents & kContentsSolid) ? 0 : 1);
    }

    // Terrain, patches and models baked into the collision: triangles in partitions,
    // found through the leaves of the collision AABB trees.
    {
        size_t cverts = walk.Pointer(cm + 84);
        size_t ctris = walk.Pointer(cm + 108);
        uint32_t ctriCount = s.U32(cm + 104);
        size_t partitions = walk.Pointer(cm + 128);
        uint32_t partitionCount = s.U32(cm + 124);
        size_t trees = walk.Pointer(cm + 136);
        uint32_t treeCount = s.U32(cm + 132);
        size_t materials = walk.Pointer(cm + 28);
        uint32_t materialCount = s.U32(cm + 24);
        std::set<uint32_t> done;
        struct T {
            float p[3][3];
            uint8_t surface, flags;
        };
        std::vector<T> terrain;
        double up = 0;
        for (uint32_t i = 0; i < treeCount && trees != kNoData; i++) {
            size_t tree = trees + i * 32;
            if (s.U16(tree + 14) != 0) {
                continue; // an inner node
            }
            uint32_t part = s.U32(tree + 28);
            uint16_t mat = s.U16(tree + 12);
            if (part >= partitionCount || !done.insert(part).second || mat >= materialCount) {
                continue;
            }
            uint32_t sflags = s.U32(materials + mat * 72 + 64);
            uint32_t contents = s.U32(materials + mat * 72 + 68);
            bool clip = (contents & (kContentsPlayerClip | kContentsMonsterClip)) != 0;
            if (!(contents & kContentsSolid) && !clip) {
                continue;
            }
            size_t p = partitions + part * 20;
            uint32_t triCount = s.U8(p);
            uint32_t first = s.U32(p + 4);
            for (uint32_t t = first; t < first + triCount && t < ctriCount; t++) {
                T tri;
                for (int k = 0; k < 3; k++) {
                    size_t v = cverts + s.U16(ctris + t * 6 + k * 2) * 12;
                    for (int c = 0; c < 3; c++) {
                        tri.p[k][c] = s.F32(v + c * 4);
                    }
                }
                float u[3], w[3], n[3];
                for (int c = 0; c < 3; c++) {
                    u[c] = tri.p[1][c] - tri.p[0][c];
                    w[c] = tri.p[2][c] - tri.p[0][c];
                }
                Cross(u, w, n);
                float len = std::sqrt(Dot(n, n));
                if (len > 0 && std::fabs(n[2] / len) > 0.7f) {
                    up += n[2] > 0 ? 1 : -1; // terrain is mostly ground, which faces up
                }
                tri.surface = SurfaceKind(sflags);
                tri.flags = (contents & kContentsSolid) ? 0 : 1;
                terrain.push_back(tri);
            }
        }
        bool flip = up < 0;
        for (const T& t : terrain) {
            size_t before = col.tris.size();
            col.Add(t.p[0], t.p[flip ? 2 : 1], t.p[flip ? 1 : 2], t.surface, t.flags);
            stats.terrainTris += col.tris.size() - before;
        }
    }
    // OoT's polygon ids are s16. A mesh with more triangles is welded on a coarser and
    // coarser grid (2, 4, 8 ... units) until it fits: small bevels and rubble flatten out,
    // the floors and walls players touch stay put.
    for (int cell = 2; col.tris.size() > kMaxColTris; cell *= 2) {
        if (cell > 64) {
            throw std::runtime_error("collision needs more than " + std::to_string(kMaxColTris) + " triangles (" +
                                     std::to_string(col.tris.size()) + ")");
        }
        WeldCollision(col, cell);
        stats.colWeld = cell;
    }

    // ---- visible geometry
    TextureSource textures(zone, walk, options.mainDir);
    MaterialTable mats;
    MeshBuilder meshes;
    size_t gw = walk.assets[worldIndex].header;
    size_t draw = gw + 344;
    size_t gverts = walk.Pointer(draw + 160);
    uint32_t gvertCount = s.U32(draw + 156);
    size_t gindices = walk.Pointer(draw + 188);
    uint32_t gindexCount = s.U32(draw + 184);
    auto putPos = [&](const float cod[3], int16_t out[3]) {
        float o[3];
        ToOotAxes(cod, o);
        for (int k = 0; k < 3; k++) {
            out[k] = ToS16(o[k] * options.scale);
        }
    };
    uint32_t surfaceCount = s.U32(gw + 16);
    size_t surfaces = walk.Pointer(gw + 836 + 88);
    // Brush models (*1, *2, ...: doors, barricade boards, wall-buy chalk) keep their
    // surfaces after the world's, in their own local space; the world is model 0.
    if (size_t bmodels = walk.Pointer(gw + 596); bmodels != kNoData && s.U32(gw + 592) > 0) {
        surfaceCount = std::min(surfaceCount, s.U32(bmodels + 52));
    }
    // Decide the winding once for the whole world, from its vertex normals.
    double agree = 0;
    for (uint32_t i = 0; i < surfaceCount && surfaces != kNoData; i++) {
        size_t sf = surfaces + i * 80;
        uint32_t first = s.U32(sf + 28), base = s.U32(sf + 36);
        uint32_t triCount = s.U16(sf + 34);
        for (uint32_t t = 0; t < triCount && t < 8; t++) {
            float p[3][3];
            for (int k = 0; k < 3; k++) {
                size_t v = gverts + (first + s.U16(gindices + (base + t * 3 + k) * 2)) * 44;
                for (int c = 0; c < 3; c++) {
                    p[k][c] = s.F32(v + c * 4);
                }
            }
            float u[3], w[3], n[3], vn[3];
            for (int c = 0; c < 3; c++) {
                u[c] = p[1][c] - p[0][c];
                w[c] = p[2][c] - p[0][c];
            }
            Cross(u, w, n);
            uint32_t packed = s.U32(gverts + (first + s.U16(gindices + (base + t * 3) * 2)) * 44 + 36);
            float sc = ((packed >> 24) + 192.0f) / 32385.0f;
            for (int c = 0; c < 3; c++) {
                vn[c] = ((packed >> (c * 8) & 0xFF) - 127.0f) * sc;
            }
            agree += Dot(n, vn) > 0 ? 1 : -1;
        }
    }
    bool flipWorld = agree < 0;

    // Static models first: their triangle count sets how far the world's triangles
    // can be split for the bake.
    struct PropInst {
        int model;
        float origin[3];
        float axis[3][3];
        float scale;
        int light;
    };
    std::map<int, Model> propModels;
    std::vector<PropInst> props;
    size_t propTris = 0;
    if (options.staticModels) {
        uint32_t smodelCount = s.U32(gw + 836);
        size_t insts = walk.Pointer(gw + 836 + 96);
        for (uint32_t i = 0; i < smodelCount && insts != kNoData; i++) {
            size_t inst = insts + i * 76;
            int mi = walk.AssetAt(inst + 56);
            if (mi < 0 || walk.assets[mi].header == kNoData) {
                continue;
            }
            auto it = propModels.find(mi);
            if (it == propModels.end()) {
                // The first level of detail within the budget keeps grass and trees affordable.
                int lods = ModelLodCount(zone, walk, mi), lod = 0;
                while (lod + 1 < lods && ModelLodVerts(zone, walk, mi, lod) > options.propVertexBudget) {
                    lod++;
                }
                it = propModels.emplace(mi, ReadModel(zone, walk, mi, lod)).first;
            }
            PropInst p;
            p.model = mi;
            for (int c = 0; c < 3; c++) {
                p.origin[c] = s.F32(inst + 4 + c * 4);
            }
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    p.axis[r][c] = s.F32(inst + 16 + (r * 3 + c) * 4);
                }
            }
            p.scale = s.F32(inst + 52);
            p.light = s.U8(inst + 75);
            props.push_back(p);
            for (const Surface& surf : it->second.surfaces) {
                if (!surf.material.empty() && !SkipVisual(surf.material, walk) && surf.verts.size() <= 0xFFFF) {
                    propTris += surf.tris.size() / 3;
                }
            }
        }
    }

    // World surfaces, each with the vertex range its triangles use.
    MapLighting lighting(zone, walk, worldIndex, options.light);
    std::vector<WorldSurface> world;
    for (uint32_t i = 0; i < surfaceCount && surfaces != kNoData; i++) {
        size_t sf = surfaces + i * 80;
        int matIndex = walk.AssetAt(sf + 48);
        std::string material = matIndex >= 0 ? walk.assets[matIndex].name : std::string();
        if (material.empty() || SkipVisual(material, walk)) {
            continue;
        }
        uint32_t first = s.U32(sf + 28), base = s.U32(sf + 36);
        uint32_t vertCount = s.U16(sf + 32), triCount = s.U16(sf + 34);
        if (first + vertCount > gvertCount || base + triCount * 3 > static_cast<uint32_t>(gindexCount)) {
            continue;
        }
        // Surfaces share blocks of vertices (vertexCount is the whole block's): take
        // only the range this surface's triangles use.
        uint32_t lowest = 0xFFFF, highest = 0;
        for (uint32_t k = 0; k < triCount * 3; k++) {
            uint32_t idx = s.U16(gindices + (base + k) * 2);
            lowest = std::min(lowest, idx);
            highest = std::max(highest, idx);
        }
        if (triCount == 0 || highest >= vertCount) {
            continue;
        }
        WorldSurface ws;
        ws.material = mats.Add(walk, material, &textures, options.maxTexture);
        ws.light = s.U8(sf + 54);
        for (uint32_t k = lowest; k <= highest; k++) {
            size_t v = gverts + (first + k) * 44;
            WorldVert out;
            for (int c = 0; c < 3; c++) {
                out.pos[c] = s.F32(v + c * 4);
            }
            uint32_t packed = s.U32(v + 36);
            float sc = ((packed >> 24) + 192.0f) / 32385.0f;
            for (int c = 0; c < 3; c++) {
                out.normal[c] = ((packed >> (c * 8) & 0xFF) - 127.0f) * sc;
            }
            out.uv[0] = s.F32(v + 20);
            out.uv[1] = s.F32(v + 24);
            out.lm[0] = s.F32(v + 28);
            out.lm[1] = s.F32(v + 32);
            // World vertex colours are mostly blend weights between a material's layers;
            // only the alpha (how much a blend layer shows) is kept.
            out.alpha = static_cast<float>(s.U32(v + 16) >> 24);
            ws.verts.push_back(out);
        }
        for (uint32_t t = 0; t < triCount; t++) {
            uint32_t idx[3];
            for (int k = 0; k < 3; k++) {
                idx[k] = s.U16(gindices + (base + t * 3 + k) * 2) - lowest;
            }
            ws.tris.push_back(idx[0]);
            ws.tris.push_back(idx[flipWorld ? 2 : 1]);
            ws.tris.push_back(idx[flipWorld ? 1 : 2]);
        }
        world.push_back(std::move(ws));
    }

    // Light is baked per vertex, so big triangles (floors, walls) are split until their
    // edges are short enough to carry the lightmap's detail, as far as the budget allows.
    size_t worldTris = 0;
    for (const WorldSurface& ws : world) {
        worldTris += ws.tris.size() / 3;
    }
    size_t budget = options.maxVisibleTris > propTris ? options.maxVisibleTris - propTris : 0;
    float edge = 0;
    if (lighting.HasLightmap() && worldTris < budget) {
        for (float candidate : { 1024.0f, 768.0f, 512.0f, 384.0f, 256.0f, 192.0f, 128.0f, 96.0f, 64.0f, 48.0f, 32.0f }) {
            size_t total = 0;
            for (const WorldSurface& ws : world) {
                total += SubdividedTriCount(ws, candidate);
                if (total > budget) {
                    break;
                }
            }
            if (total > budget) {
                break;
            }
            edge = candidate;
        }
    }
    stats.subdivideEdge = edge;
    for (WorldSurface& ws : world) {
        if (edge > 0) {
            Subdivide(ws, edge);
        }
        uint32_t mi = ws.material;
        float albedo[3] = { 1, 1, 1 };
        if (mats.image[mi] < 0) {
            MaterialAlbedo(mats.names[mi], albedo);
        }
        MeshBuilder::Mesh& mesh = meshes.For(mi, ws.verts.size());
        size_t start = mesh.verts.size();
        for (const WorldVert& wv : ws.verts) {
            MeshBuilder::V out;
            putPos(wv.pos, out.pos);
            out.uv[0] = wv.uv[0];
            out.uv[1] = wv.uv[1];
            float light[3];
            lighting.World(wv.pos, wv.normal, wv.lm[0], wv.lm[1], ws.light, light);
            lighting.ToBytes(light, albedo, out.rgba);
            out.rgba[3] = static_cast<uint8_t>(std::clamp(wv.alpha, 0.0f, 255.0f));
            mesh.verts.push_back(out);
        }
        for (uint32_t idx : ws.tris) {
            mesh.indices.push_back(static_cast<uint16_t>(start + idx));
        }
        stats.tris += ws.tris.size() / 3;
    }
    world.clear();
    lighting.FinishGrid();

    // Static models: crates, sandbags, trees, furniture. No lightmap: they take the
    // light the world around them got, and their own primary light.
    for (const PropInst& p : props) {
        const Model& model = propModels.at(p.model);
        bool flip = NeedsFlip(model.surfaces);
        stats.staticModels++;
        for (const Surface& surf : model.surfaces) {
            if (surf.material.empty() || SkipVisual(surf.material, walk) || surf.verts.size() > 0xFFFF) {
                continue;
            }
            uint32_t mat = mats.Add(walk, surf.material, &textures, options.maxTexture);
            float albedo[3] = { 1, 1, 1 };
            if (mats.image[mat] < 0) {
                MaterialAlbedo(surf.material, albedo);
            }
            MeshBuilder::Mesh& mesh = meshes.For(mat, surf.verts.size());
            uint16_t start = static_cast<uint16_t>(mesh.verts.size());
            for (const Vertex& v : surf.verts) {
                float pos[3], n[3];
                for (int c = 0; c < 3; c++) {
                    pos[c] = p.origin[c] + p.scale * (v.pos[0] * p.axis[0][c] + v.pos[1] * p.axis[1][c] + v.pos[2] * p.axis[2][c]);
                    n[c] = v.normal[0] * p.axis[0][c] + v.normal[1] * p.axis[1][c] + v.normal[2] * p.axis[2][c];
                }
                float len = std::sqrt(Dot(n, n));
                for (float& c : n) {
                    c = len > 0 ? c / len : 0.0f;
                }
                MeshBuilder::V out;
                putPos(pos, out.pos);
                out.uv[0] = v.uv[0];
                out.uv[1] = v.uv[1];
                float light[3];
                lighting.Prop(pos, n, p.light, light);
                lighting.ToBytes(light, albedo, out.rgba);
                out.rgba[3] = 255;
                mesh.verts.push_back(out);
            }
            for (size_t t = 0; t + 2 < surf.tris.size(); t += 3) {
                mesh.indices.push_back(static_cast<uint16_t>(start + surf.tris[t]));
                mesh.indices.push_back(static_cast<uint16_t>(start + surf.tris[flip ? t + 2 : t + 1]));
                mesh.indices.push_back(static_cast<uint16_t>(start + surf.tris[flip ? t + 1 : t + 2]));
            }
            stats.tris += surf.tris.size() / 3;
        }
    }

    // ---- entities and the player start
    std::string ents;
    float spawn[3] = {};
    float spawnYaw = 0;
    if (std::optional<std::string> text = FindEntityString(zone.data)) {
        std::vector<Entity> entities = ParseEntities(*text);
        bool found = false;
        for (const Entity& e : entities) {
            auto cls = e.find("classname");
            if (found || cls == e.end() || cls->second != "info_player_start") {
                continue;
            }
            Vec3 o, a;
            auto origin = e.find("origin");
            if (origin != e.end() && sscanf(origin->second.c_str(), "%f %f %f", &o.x, &o.y, &o.z) == 3) {
                spawn[0] = o.x;
                spawn[1] = o.y;
                spawn[2] = o.z;
                found = true;
                auto angles = e.find("angles");
                if (angles != e.end() && sscanf(angles->second.c_str(), "%f %f %f", &a.x, &a.y, &a.z) == 3) {
                    spawnYaw = a.y;
                }
            }
        }
        // Positions in the JSON stay unscaled: the game multiplies them by the file's scale.
        std::string mapName = walk.assets[worldIndex].name;
        size_t slash = mapName.find_last_of('/');
        mapName = mapName.substr(slash == std::string::npos ? 0 : slash + 1);
        mapName = mapName.substr(0, mapName.find('.'));
        ents = LayoutToJson(mapName, ExtractLayout(entities, 1.0f), 1.0f);
    }

    // ---- write
    Bytes out;
    out.Raw("ZZBOMAP1", 8);
    out.U32(1);
    out.F32(options.scale);
    int16_t sp[3];
    putPos(spawn, sp);
    for (int16_t v : sp) {
        out.S16(v);
    }
    out.S16(static_cast<int16_t>(std::lround((spawnYaw + 90.0f) * 65536.0f / 360.0f) & 0xFFFF));
    out.U32(static_cast<uint32_t>(col.verts.size() / 3));
    for (int16_t v : col.verts) {
        out.S16(v);
    }
    out.U32(static_cast<uint32_t>(col.tris.size()));
    for (const ColBuilder::Tri& t : col.tris) {
        for (uint16_t v : t.v) {
            out.U16(v);
        }
        out.U8(t.surface);
        out.U8(t.flags);
        stats.wallOnlyTris += t.flags & 1;
    }
    mats.Write(out);
    out.U32(static_cast<uint32_t>(meshes.meshes.size()));
    float lo[3] = { 32767, 32767, 32767 }, hi[3] = { -32768, -32768, -32768 };
    for (const MeshBuilder::Mesh& m : meshes.meshes) {
        out.U32(m.material);
        out.U32(static_cast<uint32_t>(m.verts.size()));
        for (const MeshBuilder::V& v : m.verts) {
            for (int k = 0; k < 3; k++) {
                out.S16(v.pos[k]);
                lo[k] = std::min(lo[k], static_cast<float>(v.pos[k]));
                hi[k] = std::max(hi[k], static_cast<float>(v.pos[k]));
            }
            out.F32(v.uv[0]);
            out.F32(v.uv[1]);
            out.Raw(v.rgba, 4);
        }
        out.U32(static_cast<uint32_t>(m.indices.size()));
        for (uint16_t i : m.indices) {
            out.U16(i);
        }
        stats.verts += m.verts.size();
    }
    out.U32(static_cast<uint32_t>(ents.size()));
    out.Raw(ents.data(), ents.size());

    stats.colVerts = col.verts.size() / 3;
    stats.colTris = col.tris.size();
    stats.images = mats.images.size();
    stats.materials = mats.names.size();
    stats.meshes = meshes.meshes.size();
    stats.missingImages = mats.missing.size();
    for (int k = 0; k < 3; k++) {
        stats.boundsMin[k] = lo[k];
        stats.boundsMax[k] = hi[k];
        stats.spawn[k] = sp[k];
    }
    if (info != nullptr) {
        *info = stats;
    }
    return out.b;
}

} // namespace bomap
