// Zombies of Time: particle effects.
//
// tools/bomap converts Black Ops effects from the player's own install into ZZFX1 files
// (tools/bomap/docs/ZZFX_FORMAT.md) in zombies/models/, named per role in the manifest
// ("fx_box_beam fx_ray_sun_sm_short"). This file plays them: emitters spawn particles,
// each particle follows its emitter's movement and look curves, and every live particle
// is drawn as one quad facing the camera. Roles without an import fall back to looks
// built here, in Ocarina of Time's colours. Nothing from Black Ops ships with the mode.
//
// It is cheap on purpose (phones run this): at most kMaxParticles quads, one texture
// load per run of particles that share an image, and simulation at the game's 20 Hz.

#include "ZombiesFx.h"
#include "ZombiesGuns.h"
#include "ZombiesMode.h"
#include "ZombiesModel.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>

#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// Display list macros need C linkage, like the model renderer's: this sits outside the namespace.
extern "C" void ZombiesFx_Submit(PlayState* play, Gfx* dl, const Vec3f* anchor, float scale) {
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Translate(anchor->x, anchor->y, anchor->z, MTXMODE_NEW);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, dl);
    CLOSE_DISPS(play->state.gfxCtx);
}

namespace Zombies {

// Black Ops units to OoT units: a 72-inch zombie stands 64 units tall.
static constexpr float kBoScale = 64.0f / 72.0f;
static constexpr int kMaxParticles = 320;
static constexpr int kMaxInstances = 64;
static constexpr float kFrameMs = 50.0f;   // the game updates 20 times a second
static constexpr float kGravity = 800.0f;  // Black Ops units/s^2
static constexpr float kPrecision = 4.0f;  // vertex units per OoT unit around the camera
static constexpr float kDrawRange = 3000.0f;
static constexpr int kTexSize = 32;        // procedural shapes

// ---------------------------------------------------------------------------
// Definitions (the ZZFX1 layout, or the built-in looks)

struct VelSample {
    float local[3], localRand[3], world[3], worldRand[3];
};

struct VisSample {
    u8 rgba[4], rgba2[4];
    float size[2], sizeRand[2];
    float roll;
};

enum Shape : u8 { SHAPE_DOT, SHAPE_PUFF, SHAPE_STREAK, SHAPE_RING, SHAPE_GLOW, SHAPE_RAY, SHAPE_COUNT };

struct Emitter {
    u8 category = 1; // 0 looping, 1 one-shot, 2 emitted
    u8 render = 0;   // 0 facing the camera, 1 stretched along its movement, 2 flat facing +X
    u8 blend = 0;    // 0 alpha, 1 additive, 2 multiply
    u8 shape = SHAPE_DOT;
    s32 image = -1;
    u8 cols = 1, rows = 1, start = 0, atlasMode = 0, fps = 0;
    u16 frames = 1;
    u32 spawnA = 1, spawnB = 0;
    u32 delay = 0, delayRand = 0, life = 500, lifeRand = 0;
    float origin[3][2] = {};
    float radius[2] = {}, height[2] = {}, gravity[2] = {}, rotation[2] = {};
    std::vector<VelSample> vel;
    std::vector<VisSample> vis;
};

struct FxImage {
    u16 w = 0, h = 0;
    std::vector<u8> rgba;
};

struct FxDef {
    bool imported = false; // a converted Black Ops effect, not a built-in look
    u32 loopingLife = 0x7FFFFFFF;
    std::vector<FxImage> images;
    std::vector<Emitter> emitters;
};

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

static std::unique_ptr<FxDef> LoadFx(const std::string& path) {
    Reader r(path);
    char magic[8];
    r.Bytes(magic, 8);
    if (!r.Ok() || std::memcmp(magic, "ZZFX1", 5) != 0 || r.Get<u32>() != 1) {
        return nullptr;
    }
    auto def = std::make_unique<FxDef>();
    def->imported = true;
    def->loopingLife = r.Get<u32>();
    u32 ni = r.Count(68);
    def->images.resize(ni);
    for (FxImage& img : def->images) {
        char name[64];
        r.Bytes(name, 64);
        img.w = r.Get<u16>();
        img.h = r.Get<u16>();
        if (img.w == 0 || img.h == 0 || img.w > 128 || img.h > 128 || (img.w & (img.w - 1)) || (img.h & (img.h - 1))) {
            return nullptr;
        }
        img.rgba.resize(static_cast<size_t>(img.w) * img.h * 4);
        r.Bytes(img.rgba.data(), img.rgba.size());
    }
    u32 ne = r.Count(100);
    def->emitters.resize(ne);
    for (Emitter& e : def->emitters) {
        e.category = r.Get<u8>();
        e.render = r.Get<u8>();
        e.blend = r.Get<u8>();
        e.shape = std::min<u8>(r.Get<u8>(), SHAPE_COUNT - 1);
        e.image = r.Get<s32>();
        if (e.image >= static_cast<s32>(ni)) {
            e.image = -1;
        }
        e.cols = std::max<u8>(r.Get<u8>(), 1);
        e.rows = std::max<u8>(r.Get<u8>(), 1);
        e.start = r.Get<u8>();
        e.atlasMode = r.Get<u8>();
        e.fps = r.Get<u8>();
        r.Get<u8>();
        e.frames = std::max<u16>(r.Get<u16>(), 1);
        e.spawnA = r.Get<u32>();
        e.spawnB = r.Get<u32>();
        e.delay = r.Get<u32>();
        e.delayRand = r.Get<u32>();
        e.life = std::max<u32>(r.Get<u32>(), 1);
        e.lifeRand = r.Get<u32>();
        for (auto& o : e.origin) {
            o[0] = r.Get<float>();
            o[1] = r.Get<float>();
        }
        for (float* pair : { e.radius, e.height, e.gravity, e.rotation }) {
            pair[0] = r.Get<float>();
            pair[1] = r.Get<float>();
        }
        u32 nv = r.Count(48);
        e.vel.resize(nv);
        for (VelSample& v : e.vel) {
            r.Bytes(&v, sizeof(VelSample));
        }
        u32 ns = r.Count(28);
        e.vis.resize(ns);
        for (VisSample& v : e.vis) {
            r.Bytes(v.rgba, 4);
            r.Bytes(v.rgba2, 4);
            v.size[0] = r.Get<float>();
            v.size[1] = r.Get<float>();
            v.sizeRand[0] = r.Get<float>();
            v.sizeRand[1] = r.Get<float>();
            v.roll = r.Get<float>();
        }
        if (!r.Ok()) {
            return nullptr;
        }
        // Keep the counts sane: a looping emitter at most every frame, a burst at most 64.
        if (e.category == 0) {
            e.spawnA = std::max<u32>(e.spawnA, 1);
        } else {
            e.spawnA = std::min<u32>(e.spawnA, 64);
            e.spawnB = std::min<u32>(e.spawnB, 64);
        }
    }
    if (!r.Ok()) {
        return nullptr;
    }
    // The renderer only alpha-blends. An image drawn additively (black means nothing) has
    // its brightness turned into coverage; one drawn multiplied (white means nothing) has
    // its darkness turned into coverage. An image shared by both kinds is left alone.
    std::vector<u8> use(def->images.size(), 0);
    for (const Emitter& e : def->emitters) {
        if (e.image >= 0) {
            use[e.image] |= e.blend == 1 ? 1 : e.blend == 2 ? 2 : 4;
        }
    }
    for (size_t i = 0; i < def->images.size(); i++) {
        if (use[i] != 1 && use[i] != 2) {
            continue;
        }
        std::vector<u8>& px = def->images[i].rgba;
        for (size_t k = 0; k + 3 < px.size(); k += 4) {
            int m = std::max({ px[k], px[k + 1], px[k + 2] });
            if (use[i] == 1) {
                px[k + 3] = static_cast<u8>(px[k + 3] * m / 255);
                for (int c = 0; c < 3; c++) {
                    px[k + c] = static_cast<u8>(m > 0 ? px[k + c] * 255 / m : 0);
                }
            } else {
                px[k + 3] = static_cast<u8>(px[k + 3] * (255 - m) / 255);
            }
        }
    }
    return std::move(def);
}

// ---------------------------------------------------------------------------
// The built-in looks: OoT-styled stand-ins for every role.

struct Look {
    Emitter e;
    Look(u8 category, u8 render, u8 blend, u8 shape) {
        e.category = category;
        e.render = render;
        e.blend = blend;
        e.shape = shape;
    }
    Look& Spawn(u32 a, u32 b = 0) {
        e.spawnA = a;
        e.spawnB = b;
        return *this;
    }
    Look& Life(u32 ms, u32 rand = 0) {
        e.life = ms;
        e.lifeRand = rand;
        return *this;
    }
    Look& Delay(u32 ms, u32 rand = 0) {
        e.delay = ms;
        e.delayRand = rand;
        return *this;
    }
    // Where it starts, in the effect's frame (+X forward): base and random extra per axis.
    Look& At(float x, float xr = 0, float y = 0, float yr = 0, float z = 0, float zr = 0) {
        float v[6] = { x, xr, y, yr, z, zr };
        for (int k = 0; k < 3; k++) {
            e.origin[k][0] = v[k * 2];
            e.origin[k][1] = v[k * 2 + 1];
        }
        return *this;
    }
    Look& Ring(float r, float rand) {
        e.radius[0] = r;
        e.radius[1] = rand;
        return *this;
    }
    Look& Fall(float g, float rand = 0) {
        e.gravity[0] = g;
        e.gravity[1] = rand;
        return *this;
    }
    // How far it travels over its life (base life), in the effect's frame, plus a world rise.
    Look& Move(float x, float xr, float y, float yr, float z, float zr, float rise = 0) {
        float life = static_cast<float>(e.life);
        VelSample a = {}, b = {};
        float base[3] = { x, y, z }, rand[3] = { xr, yr, zr };
        for (int k = 0; k < 3; k++) {
            b.local[k] = base[k] / life;
            b.localRand[k] = rand[k] / life;
        }
        b.world[2] = rise / life;
        e.vel = { a, b };
        return *this;
    }
    // A look sample: colour, alpha and half size (width, length); spread evenly over life.
    Look& Vis(u8 r, u8 g, u8 b, u8 a, float w, float h = -1.0f, float sizeRand = 0) {
        VisSample v = {};
        u8 c[4] = { r, g, b, a };
        std::memcpy(v.rgba, c, 4);
        std::memcpy(v.rgba2, c, 4);
        v.size[0] = w;
        v.size[1] = h < 0 ? w : h;
        v.sizeRand[0] = sizeRand;
        v.sizeRand[1] = sizeRand;
        e.vis.push_back(v);
        return *this;
    }
};

static std::unique_ptr<FxDef> Built(std::initializer_list<Look> looks, u32 loopingLife = 0x7FFFFFFF) {
    auto def = std::make_unique<FxDef>();
    def->loopingLife = loopingLife;
    for (const Look& l : looks) {
        def->emitters.push_back(l.e);
        if (def->emitters.back().vel.empty()) {
            def->emitters.back().vel = { VelSample{}, VelSample{} };
        }
    }
    return def;
}

enum { LOOP = 0, ONCE = 1 };
enum { FACE = 0, STRETCH = 1, FLAT = 2 };
enum { ALPHA = 0, ADD = 1 };

static std::unique_ptr<FxDef> BuiltIn(const std::string& role) {
    if (role == "box_beam") {
        // A warm shaft of light above the box (played pointing down: -X is up).
        return Built({
            Look(LOOP, STRETCH, ADD, SHAPE_RAY).Spawn(250, 6).Life(1300).At(-70).Vis(255, 236, 160, 0, 15, 70)
                .Vis(255, 236, 160, 95, 15, 70).Vis(255, 236, 160, 0, 15, 70),
            Look(LOOP, STRETCH, ADD, SHAPE_RAY).Spawn(300, 5).Life(1100).At(-75).Vis(255, 255, 225, 0, 6, 75)
                .Vis(255, 255, 225, 150, 6, 75).Vis(255, 255, 225, 0, 6, 75),
            Look(LOOP, FACE, ADD, SHAPE_DOT).Spawn(110).Life(2600, 900).At(-6).Ring(2, 14)
                .Move(-70, -30, 0, 0, 0, 0).Vis(255, 240, 170, 0, 0.8f).Vis(255, 240, 170, 220, 0.8f)
                .Vis(255, 240, 170, 0, 0.6f),
            Look(LOOP, FACE, ADD, SHAPE_GLOW).Spawn(350, 3).Life(900).At(-10).Vis(255, 230, 150, 0, 34)
                .Vis(255, 230, 150, 55, 34).Vis(255, 230, 150, 0, 34),
        });
    }
    if (role == "muzzle_flash" || role == "pap_flash") {
        bool pap = role == "pap_flash";
        u8 r = pap ? 190 : 255, g = pap ? 130 : 225, b = pap ? 255 : 140;
        return Built({
            Look(ONCE, FACE, ADD, SHAPE_GLOW).Life(70).At(2).Vis(r, g, b, 255, 7).Vis(r, g, b, 0, 9),
            Look(ONCE, STRETCH, ADD, SHAPE_STREAK).Spawn(2, 1).Life(60).At(6).Vis(255, 240, 200, 230, 2.2f, 7)
                .Vis(r, g, b, 0, 1.5f, 9),
            Look(ONCE, FACE, ALPHA, SHAPE_PUFF).Life(650, 200).At(4).Move(10, 6, -3, 6, -3, 6, 10)
                .Vis(190, 190, 190, 70, 2).Vis(160, 160, 160, 0, 7),
        });
    }
    if (role == "impact_flesh") {
        return Built({
            Look(ONCE, FACE, ALPHA, SHAPE_DOT).Spawn(5, 4).Life(450, 200).Move(26, 14, -14, 28, -10, 28).Fall(0.6f)
                .Vis(130, 0, 0, 255, 0.9f).Vis(90, 0, 0, 200, 0.7f),
            Look(ONCE, FACE, ALPHA, SHAPE_PUFF).Life(320).Move(6, 0, 0, 0, 0, 0).Vis(160, 12, 12, 210, 3)
                .Vis(110, 0, 0, 0, 9),
            Look(ONCE, FACE, ALPHA, SHAPE_PUFF).Life(140).Vis(210, 30, 30, 230, 5).Vis(150, 0, 0, 0, 7),
        });
    }
    if (role == "impact_default") {
        return Built({
            Look(ONCE, FACE, ALPHA, SHAPE_PUFF).Spawn(2).Life(700, 250).Move(12, 6, -4, 8, -4, 8, 6)
                .Vis(175, 165, 140, 180, 2).Vis(150, 140, 120, 0, 8),
            Look(ONCE, STRETCH, ADD, SHAPE_STREAK).Spawn(3, 3).Life(160, 100).Move(30, 10, -25, 50, -25, 50)
                .Fall(0.3f).Vis(255, 225, 150, 255, 0.4f, 2.5f).Vis(255, 120, 40, 0, 0.3f, 2),
            Look(ONCE, FACE, ALPHA, SHAPE_DOT).Spawn(3).Life(420).Move(20, 10, -15, 30, -15, 30).Fall(1)
                .Vis(95, 85, 70, 255, 0.6f).Vis(95, 85, 70, 255, 0.6f),
        });
    }
    if (role == "zombie_rise") {
        // Clods of earth thrown up and a slow cloud of dust (played pointing up).
        return Built({
            Look(ONCE, FACE, ALPHA, SHAPE_PUFF).Spawn(10, 5).Life(900, 300).Ring(0, 10)
                .Move(75, 40, -45, 90, -45, 90).Fall(1).Vis(92, 70, 44, 255, 1.6f, -1, 1.4f)
                .Vis(80, 62, 40, 220, 1.4f, -1, 1),
            Look(ONCE, FACE, ALPHA, SHAPE_PUFF).Spawn(4, 2).Delay(0, 200).Life(2300, 800).Ring(0, 16)
                .Move(28, 10, -20, 40, -20, 40).Vis(125, 104, 78, 0, 9).Vis(125, 104, 78, 150, 16)
                .Vis(115, 98, 76, 0, 30),
        });
    }
    if (role == "powerup_on") {
        return Built({
            Look(LOOP, FACE, ADD, SHAPE_GLOW).Spawn(100, 4).Life(320, 100).Vis(110, 255, 110, 0, 18, -1, 6)
                .Vis(110, 255, 110, 190, 18, -1, 6).Vis(60, 255, 60, 0, 16),
            Look(LOOP, FACE, ADD, SHAPE_DOT).Spawn(100, 3).Life(220).Vis(225, 255, 225, 255, 6).Vis(120, 255, 120, 0, 7),
            Look(LOOP, FACE, ADD, SHAPE_DOT).Spawn(130).Life(700, 300).Ring(4, 8).Move(22, 12, 0, 0, 0, 0)
                .Vis(200, 255, 200, 255, 0.8f).Vis(80, 255, 80, 0, 0.6f),
        });
    }
    if (role == "powerup_grab") {
        return Built({
            Look(ONCE, FACE, ADD, SHAPE_GLOW).Life(300).Vis(160, 255, 160, 230, 26).Vis(80, 255, 80, 0, 40),
            Look(ONCE, FACE, ADD, SHAPE_DOT).Spawn(26, 10).Life(520, 200).Move(40, 20, -40, 80, -40, 80).Fall(0.1f)
                .Vis(210, 255, 210, 255, 1.2f).Vis(90, 255, 90, 0, 0.8f),
        });
    }
    if (role == "explosion") {
        return Built({
            Look(ONCE, FACE, ADD, SHAPE_GLOW).Life(140).Vis(255, 235, 170, 255, 50).Vis(255, 130, 40, 0, 70),
            Look(ONCE, FACE, ADD, SHAPE_PUFF).Spawn(6, 3).Life(380, 150).Ring(0, 12).Move(30, 20, -30, 60, -30, 60)
                .Vis(255, 180, 70, 255, 12, -1, 8).Vis(200, 60, 20, 0, 20),
            Look(ONCE, FACE, ALPHA, SHAPE_PUFF).Spawn(6, 3).Delay(100, 150).Life(1900, 700).Ring(0, 16)
                .Move(60, 20, -50, 100, -50, 100).Fall(-0.03f).Vis(60, 55, 50, 210, 14).Vis(85, 80, 74, 0, 40),
            Look(ONCE, STRETCH, ADD, SHAPE_STREAK).Spawn(12, 6).Life(420, 300).Move(120, 60, -120, 240, -120, 240)
                .Fall(0.4f).Vis(255, 225, 130, 255, 0.5f, 4).Vis(255, 100, 20, 0, 0.4f, 3),
            Look(ONCE, FACE, ALPHA, SHAPE_DOT).Spawn(6).Life(900).Move(80, 30, -80, 160, -80, 160).Fall(1)
                .Vis(60, 50, 40, 255, 1.2f).Vis(60, 50, 40, 255, 1.2f),
        });
    }
    bool ug = role.find("_ug_") != std::string::npos;
    u8 r = ug ? 255 : 120, g = ug ? 90 : 255, b = ug ? 90 : 100;
    if (role == "raygun_flash" || role == "raygun_ug_flash") {
        return Built({
            Look(ONCE, FLAT, ADD, SHAPE_RING).Life(260).At(3).Vis(r, g, b, 255, 3).Vis(r, g, b, 0, 8),
            Look(ONCE, FACE, ADD, SHAPE_GLOW).Life(90).At(2).Vis(r, g, b, 255, 6).Vis(r, g, b, 0, 8),
        });
    }
    if (role == "raygun_trail" || role == "raygun_ug_trail") {
        return Built({
            Look(ONCE, FACE, ADD, SHAPE_GLOW).Life(220).Vis(r, g, b, 230, 6).Vis(r, g, b, 0, 2),
            Look(ONCE, FLAT, ADD, SHAPE_RING).Life(320, 150).Vis(r, g, b, 200, 1.5f).Vis(r, g, b, 0, 4),
        });
    }
    if (role == "raygun_impact" || role == "raygun_ug_impact") {
        return Built({
            Look(ONCE, FACE, ADD, SHAPE_GLOW).Life(220).Vis(r, g, b, 255, 28).Vis(r, g, b, 0, 36),
            Look(ONCE, FACE, ADD, SHAPE_DOT).Spawn(12, 8).Life(600, 400).Move(40, 20, -40, 80, -40, 80).Fall(-0.1f)
                .Vis(r, g, b, 255, 1).Vis(r, g, b, 0, 0.7f),
            Look(ONCE, FACE, ALPHA, SHAPE_PUFF).Spawn(2).Life(700).Move(12, 0, -8, 16, -8, 16)
                .Vis(static_cast<u8>(r * 3 / 4), static_cast<u8>(g * 3 / 4), static_cast<u8>(b * 3 / 4), 140, 6)
                .Vis(r, g, b, 0, 18),
        });
    }
    if (role == "barrier_break") {
        return Built({
            Look(ONCE, FACE, ALPHA, SHAPE_DOT).Spawn(8, 4).Life(650, 200).Move(30, 15, -25, 50, -25, 50).Fall(1)
                .Vis(140, 100, 60, 255, 1).Vis(120, 85, 50, 255, 0.8f),
            Look(ONCE, FACE, ALPHA, SHAPE_PUFF).Spawn(2).Life(800).Move(10, 0, -6, 12, -6, 12)
                .Vis(150, 130, 100, 150, 4).Vis(150, 130, 100, 0, 12),
        });
    }
    return nullptr;
}

// Imported files by name, then the role table. Loaded once, kept for the session.
static std::map<std::string, std::unique_ptr<FxDef>> sFiles;
static std::map<std::string, FxDef*> sRoles;

static FxDef* FileFx(const std::string& file) {
    auto it = sFiles.find(file);
    if (it == sFiles.end()) {
        std::string path = Ship::Context::GetPathRelativeToAppDirectory("zombies/models") + "/" + file + ".zzfx";
        it = sFiles.emplace(file, LoadFx(path)).first;
    }
    return it->second.get();
}

// manifestKey names the imported file; role picks the built-in look when there's none.
static FxDef* Resolve(const std::string& manifestKey, const std::string& role) {
    std::string key = manifestKey + "|" + role;
    auto it = sRoles.find(key);
    if (it != sRoles.end()) {
        return it->second;
    }
    FxDef* def = nullptr;
    std::vector<std::string> files = Model_Manifest(manifestKey);
    if (!files.empty()) {
        def = FileFx(files[0]);
    }
    if (def == nullptr) {
        // One built-in look per role, shared by every key that falls back to it (each gun
        // without an imported flash uses the same muzzle flash). It lives in sFiles for the
        // session, so particles can keep pointing at its emitters.
        std::string builtKey = "builtin:" + role;
        auto have = sFiles.find(builtKey);
        if (have == sFiles.end()) {
            have = sFiles.emplace(builtKey, BuiltIn(role)).first;
        }
        def = have->second.get();
    }
    sRoles[key] = def;
    return def;
}

// ---------------------------------------------------------------------------
// Procedural shapes: what a sprite looks like when its image isn't available.

static std::vector<u8> sShapes[SHAPE_COUNT];

static void MakeShapes() {
    if (!sShapes[0].empty()) {
        return;
    }
    u32 seed = 0x2545F491u;
    auto noise = [&seed]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<float>(seed & 0xFFFF) / 65535.0f;
    };
    float grain[kTexSize][kTexSize];
    for (auto& row : grain) {
        for (float& v : row) {
            v = noise();
        }
    }
    for (int s = 0; s < SHAPE_COUNT; s++) {
        std::vector<u8>& img = sShapes[s];
        img.resize(kTexSize * kTexSize * 4);
        for (int y = 0; y < kTexSize; y++) {
            for (int x = 0; x < kTexSize; x++) {
                float fx = (static_cast<float>(x) + 0.5f) / kTexSize * 2.0f - 1.0f;
                float fy = (static_cast<float>(y) + 0.5f) / kTexSize * 2.0f - 1.0f;
                float d = sqrtf(fx * fx + fy * fy);
                float a = 0.0f;
                switch (s) {
                    case SHAPE_DOT:
                        a = std::clamp(1.0f - d, 0.0f, 1.0f);
                        a = a * a * (3.0f - 2.0f * a);
                        break;
                    case SHAPE_PUFF: {
                        // Lumpy: a soft disc broken up by smoothed grain.
                        int x1 = (x + 1) % kTexSize, y1 = (y + 1) % kTexSize;
                        float n = (grain[y][x] + grain[y][x1] + grain[y1][x] + grain[y1][x1]) * 0.25f;
                        a = std::clamp(1.0f - d * (0.8f + 0.5f * n), 0.0f, 1.0f);
                        a = a * a;
                        break;
                    }
                    case SHAPE_STREAK:
                        a = std::clamp(1.0f - fabsf(fx) * 2.2f, 0.0f, 1.0f) * std::clamp(1.0f - fabsf(fy), 0.0f, 1.0f);
                        break;
                    case SHAPE_RING:
                        a = std::clamp(1.0f - fabsf(d - 0.7f) * 5.0f, 0.0f, 1.0f);
                        break;
                    case SHAPE_GLOW:
                        a = std::clamp(1.0f - d, 0.0f, 1.0f);
                        a = a * a;
                        break;
                    case SHAPE_RAY:
                        // Bright along the middle, soft at the sides and both ends.
                        a = std::clamp(1.0f - fabsf(fx), 0.0f, 1.0f) * std::clamp((1.0f - fabsf(fy)) * 2.5f, 0.0f, 1.0f);
                        a = a * a;
                        break;
                    default:
                        break;
                }
                u8* p = &img[(y * kTexSize + x) * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = static_cast<u8>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Live effects

struct Instance {
    int handle = 0;
    const FxDef* def = nullptr;
    Vec3f pos = {};
    Vec3f F = {}, L = {}, U = {}; // the effect's +X, +Y, +Z in OoT axes
    float scale = 1.0f;
    float age = 0.0f; // ms
    bool stopped = false;
    std::vector<float> nextSpawn; // looping emitters: age of the next spawn
};

struct Particle {
    const FxDef* def;
    const Emitter* em;
    float age, life; // ms; below 0 it hasn't started
    Vec3f origin;
    Vec3f F, L, U;
    float scale;
    float rl[3], rw[3];
    float rc, rs, grav, roll;
    u8 frame;
    bool moved;
    Vec3f pos, prev;
};

static std::vector<Instance> sInstances;
static std::vector<Particle> sParticles;
static int sNextHandle = 1;
static u32 sRand = 0x9E3779B9u;

// Our own generator: effects must not move the game's random numbers (the box's rolls).
static float Rnd() {
    sRand ^= sRand << 13;
    sRand ^= sRand >> 17;
    sRand ^= sRand << 5;
    return static_cast<float>(sRand & 0xFFFFFF) / 16777215.0f;
}

static Vec3f Add(const Vec3f& a, const Vec3f& b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
static Vec3f Mul(const Vec3f& a, float s) {
    return { a.x * s, a.y * s, a.z * s };
}
static Vec3f Cross(const Vec3f& a, const Vec3f& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static float Dot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static Vec3f Norm(const Vec3f& v, const Vec3f& fallback = { 0.0f, 1.0f, 0.0f }) {
    float l = sqrtf(Dot(v, v));
    return l > 1e-5f ? Mul(v, 1.0f / l) : fallback;
}

static void Frame(const Vec3f& dir, Vec3f* F, Vec3f* L, Vec3f* U) {
    *F = Norm(dir, { 1.0f, 0.0f, 0.0f });
    Vec3f up = fabsf(F->y) > 0.95f ? Vec3f{ 0.0f, 0.0f, 1.0f } : Vec3f{ 0.0f, 1.0f, 0.0f };
    *L = Norm(Cross(up, *F));
    *U = Cross(*F, *L);
}

static void SpawnParticle(const Instance& inst, const Emitter& em, float delay) {
    if (sParticles.size() >= kMaxParticles) {
        return; // full: the newest give way, nothing already on screen pops
    }
    Particle p = {};
    p.def = inst.def;
    p.em = &em;
    p.age = -delay;
    p.life = static_cast<float>(em.life) + Rnd() * static_cast<float>(em.lifeRand);
    p.F = inst.F;
    p.L = inst.L;
    p.U = inst.U;
    p.scale = inst.scale * kBoScale;
    float o[3];
    for (int k = 0; k < 3; k++) {
        o[k] = em.origin[k][0] + Rnd() * em.origin[k][1];
        p.rl[k] = Rnd();
        p.rw[k] = Rnd();
    }
    float r = em.radius[0] + Rnd() * em.radius[1];
    float h = em.height[0] + Rnd() * em.height[1];
    float ang = Rnd() * 6.2831853f;
    if (r != 0.0f || h != 0.0f) {
        o[0] += h;
        o[1] += cosf(ang) * r;
        o[2] += sinf(ang) * r;
    }
    p.origin = Add(inst.pos, Mul(Add(Add(Mul(p.F, o[0]), Mul(p.L, o[1])), Mul(p.U, o[2])), p.scale));
    p.rc = Rnd();
    p.rs = Rnd();
    p.grav = em.gravity[0] + Rnd() * em.gravity[1];
    p.roll = em.rotation[0] + Rnd() * em.rotation[1];
    p.frame = em.start;
    if (em.atlasMode == 1) {
        p.frame = static_cast<u8>(static_cast<int>(em.start + Rnd() * static_cast<float>(em.frames)) % std::max<int>(em.frames, 1));
    }
    p.pos = p.prev = p.origin;
    sParticles.push_back(p);
}

static void SpawnBurst(const Instance& inst, const Emitter& em) {
    int count = static_cast<int>(em.spawnA) + static_cast<int>(Rnd() * static_cast<float>(em.spawnB + 1));
    count = std::min(count, static_cast<int>(em.spawnB + em.spawnA));
    for (int i = 0; i < count; i++) {
        SpawnParticle(inst, em, static_cast<float>(em.delay) + Rnd() * static_cast<float>(em.delayRand));
    }
}

static float CameraDistSq(const Vec3f& pos) {
    if (gPlayState == nullptr) {
        return 0.0f;
    }
    Vec3f d = { pos.x - gPlayState->view.eye.x, pos.y - gPlayState->view.eye.y, pos.z - gPlayState->view.eye.z };
    return Dot(d, d);
}

static int Start(FxDef* def, const Vec3f& pos, const Vec3f& dir, float scale) {
    if (def == nullptr || !IsRunActive()) {
        return 0;
    }
    Instance inst;
    inst.def = def;
    inst.pos = pos;
    inst.scale = scale;
    Frame(dir, &inst.F, &inst.L, &inst.U);
    bool looping = false;
    for (const Emitter& em : def->emitters) {
        looping |= em.category == 0;
    }
    bool outOfRange = CameraDistSq(pos) > kDrawRange * kDrawRange;
    // One-shot emitters spawn everything now (each particle waits out its own delay).
    if (!outOfRange) {
        for (const Emitter& em : def->emitters) {
            if (em.category != 0) {
                SpawnBurst(inst, em);
            }
        }
    }
    if (!looping) {
        return 0;
    }
    if (sInstances.size() >= kMaxInstances) {
        return 0;
    }
    inst.handle = sNextHandle++;
    if (sNextHandle > 0x3FFFFFFF) {
        sNextHandle = 1;
    }
    for (const Emitter& em : def->emitters) {
        inst.nextSpawn.push_back(static_cast<float>(em.delay));
    }
    sInstances.push_back(inst);
    return inst.handle;
}

static Instance* Find(int handle) {
    for (Instance& inst : sInstances) {
        if (inst.handle == handle && handle != 0) {
            return &inst;
        }
    }
    return nullptr;
}

int Fx_Play(const char* role, const Vec3f& pos, const Vec3f& dir, float scale) {
    return Start(Resolve(std::string("fx_") + role, role), pos, dir, scale);
}

int Fx_PlayUp(const char* role, const Vec3f& pos, float scale) {
    return Fx_Play(role, pos, { 0.0f, 1.0f, 0.0f }, scale);
}

void Fx_Move(int handle, const Vec3f& pos) {
    if (Instance* inst = Find(handle)) {
        inst->pos = pos;
    }
}

void Fx_Stop(int handle) {
    if (Instance* inst = Find(handle)) {
        inst->stopped = true;
    }
}

bool Fx_Alive(int handle) {
    return Find(handle) != nullptr;
}

void Fx_Clear() {
    sInstances.clear();
    sParticles.clear();
}

void Fx_GunFlash(const char* boName, bool pap, const Vec3f& muzzle, const Vec3f& dir) {
    std::string key = std::string("gun_") + (boName != nullptr ? boName : "") + (pap ? "_fx_flash_ug" : "_fx_flash");
    bool ray = boName != nullptr && std::strstr(boName, "ray_gun") != nullptr;
    const char* role = ray ? (pap ? "raygun_ug_flash" : "raygun_flash") : (pap ? "pap_flash" : "muzzle_flash");
    FxDef* def = Resolve(key, role);
    if (def == nullptr || def->emitters.empty()) {
        def = Resolve(std::string("fx_") + role, role);
    }
    Start(def, muzzle, dir, 1.0f);
}

void Fx_Impact(const Vec3f& pos, const Vec3f& normal) {
    Fx_Play("impact_default", pos, normal);
}

void Fx_Blood(const Vec3f& pos, const Vec3f& dir) {
    Fx_Play("impact_flesh", pos, dir);
}

void Fx_Explosion(const Vec3f& pos) {
    Fx_PlayUp("explosion", pos);
}

void Fx_RayGunTrail(const Vec3f& pos, const Vec3f& dir, bool pap) {
    Fx_Play(pap ? "raygun_ug_trail" : "raygun_trail", pos, dir);
}

void Fx_RayGunImpact(const Vec3f& pos, bool pap) {
    Fx_PlayUp(pap ? "raygun_ug_impact" : "raygun_impact", pos);
}

// ---------------------------------------------------------------------------
// Simulation

template <typename T> static void Sample(const std::vector<T>& s, float f, int* i0, int* i1, float* t) {
    int n = static_cast<int>(s.size());
    if (n <= 1) {
        *i0 = *i1 = 0;
        *t = 0.0f;
        return;
    }
    float x = std::clamp(f, 0.0f, 1.0f) * static_cast<float>(n - 1);
    *i0 = std::min(static_cast<int>(x), n - 2);
    *i1 = *i0 + 1;
    *t = x - static_cast<float>(*i0);
}

static Vec3f ParticlePos(const Particle& p) {
    const Emitter& em = *p.em;
    float f = p.age / p.life;
    Vec3f pos = p.origin;
    if (!em.vel.empty()) {
        int a, b;
        float t;
        Sample(em.vel, f, &a, &b, &t);
        const VelSample& va = em.vel[a];
        const VelSample& vb = em.vel[b];
        float l[3], w[3];
        for (int k = 0; k < 3; k++) {
            float la = va.local[k] + p.rl[k] * va.localRand[k];
            float lb = vb.local[k] + p.rl[k] * vb.localRand[k];
            float wa = va.world[k] + p.rw[k] * va.worldRand[k];
            float wb = vb.world[k] + p.rw[k] * vb.worldRand[k];
            l[k] = (la + (lb - la) * t) * p.life;
            w[k] = (wa + (wb - wa) * t) * p.life;
        }
        Vec3f local = Add(Add(Mul(p.F, l[0]), Mul(p.L, l[1])), Mul(p.U, l[2]));
        Vec3f world = { w[0], w[2], -w[1] }; // Black Ops world axes: Z up
        pos = Add(pos, Mul(Add(local, world), p.scale));
    }
    float sec = p.age / 1000.0f;
    pos.y -= 0.5f * p.grav * kGravity * sec * sec * p.scale;
    return pos;
}

static void UpdateFx() {
    // Looping emitters spawn on their beat while their effect runs.
    for (size_t i = 0; i < sInstances.size();) {
        Instance& inst = sInstances[i];
        inst.age += kFrameMs;
        bool done = inst.stopped || inst.age > static_cast<float>(inst.def->loopingLife);
        bool outOfRange = CameraDistSq(inst.pos) > kDrawRange * kDrawRange;
        if (!done) {
            for (size_t e = 0; e < inst.def->emitters.size(); e++) {
                const Emitter& em = inst.def->emitters[e];
                if (em.category != 0) {
                    continue;
                }
                int spawned = 0;
                // At most two a frame from one emitter: a dense Black Ops emitter thins out.
                while (inst.nextSpawn[e] <= inst.age && spawned < 2) {
                    if (!outOfRange) {
                        SpawnParticle(inst, em, 0.0f);
                    }
                    inst.nextSpawn[e] += static_cast<float>(std::max<u32>(em.spawnA, 25));
                    spawned++;
                }
                if (inst.nextSpawn[e] < inst.age) {
                    inst.nextSpawn[e] = inst.age;
                }
            }
        }
        if (done) {
            sInstances.erase(sInstances.begin() + static_cast<long>(i));
        } else {
            i++;
        }
    }
    for (size_t i = 0; i < sParticles.size();) {
        Particle& p = sParticles[i];
        p.age += kFrameMs;
        if (p.age >= p.life) {
            sParticles[i] = sParticles.back();
            sParticles.pop_back();
            continue;
        }
        if (p.age >= 0.0f) {
            p.prev = p.moved ? p.pos : p.origin;
            p.pos = ParticlePos(p);
            p.moved = true;
            const Emitter& em = *p.em;
            if (em.atlasMode >= 2 && em.frames > 1) {
                float f = em.atlasMode == 2 ? p.age / p.life * static_cast<float>(em.frames)
                                            : p.age / 1000.0f * static_cast<float>(std::max<u8>(em.fps, 1));
                p.frame = static_cast<u8>((em.start + static_cast<int>(f)) % em.frames);
            }
        }
        i++;
    }
}

// ---------------------------------------------------------------------------
// Things the effects follow without the rest of the mode calling in: power-ups on the
// ground, undead rising and getting hit, bombs going off.

static std::map<Actor*, int> sDropFx;
static std::map<Actor*, Vec3f> sDropPos;
static std::map<Actor*, s16> sEnemyHealth;

static void Watch(PlayState* play, Player* player) {
    Run& run = GetRun();
    // Power-ups glow while they wait, and burst when picked up.
    for (const ActiveDrop& drop : run.drops) {
        if (drop.actor == nullptr) {
            continue;
        }
        Vec3f at = drop.actor->world.pos;
        at.y += 12.0f;
        auto it = sDropFx.find(drop.actor);
        if (it == sDropFx.end()) {
            sDropFx[drop.actor] = Fx_PlayUp("powerup_on", at);
        } else {
            Fx_Move(it->second, at);
        }
        sDropPos[drop.actor] = at;
    }
    for (auto it = sDropFx.begin(); it != sDropFx.end();) {
        bool live = false;
        for (const ActiveDrop& drop : run.drops) {
            live |= drop.actor == it->first;
        }
        if (live) {
            ++it;
            continue;
        }
        Fx_Stop(it->second);
        Vec3f at = sDropPos[it->first];
        if (player != nullptr && Math_Vec3f_DistXZ(&player->actor.world.pos, &at) < 60.0f) {
            Fx_PlayUp("powerup_grab", at);
        }
        sDropPos.erase(it->first);
        it = sDropFx.erase(it);
    }
    // Undead: dirt as they climb out, blood when their health drops.
    std::map<Actor*, s16> seen;
    for (const TrackedEnemy& enemy : run.enemies) {
        Actor* a = enemy.actor;
        if (a == nullptr || a->update == nullptr) {
            continue;
        }
        s16 hp = a->colChkInfo.health;
        auto it = sEnemyHealth.find(a);
        if (it == sEnemyHealth.end()) {
            if (enemy.kind != EnemyKind::WhiteWolfos) { // hellhounds arrive in lightning, not dirt
                Fx_PlayUp("zombie_rise", a->world.pos);
            }
        } else if (hp < it->second) {
            Vec3f at = a->world.pos;
            at.y += 34.0f;
            Vec3f toward = player != nullptr ? Vec3f{ player->actor.world.pos.x - at.x, 0.0f, player->actor.world.pos.z - at.z }
                                             : Vec3f{ 0.0f, 1.0f, 0.0f };
            Fx_Blood(at, Norm(toward));
        }
        seen[a] = hp;
    }
    sEnemyHealth.swap(seen);
    (void)play;
}

static void OnActorDestroyed(void* ptr) {
    Actor* actor = static_cast<Actor*>(ptr);
    if (!IsRunActive() || actor == nullptr) {
        return;
    }
    if (actor->id == ACTOR_EN_BOM || actor->id == ACTOR_EN_BOM_CHU) {
        Fx_Explosion(actor->world.pos);
    }
    sDropFx.erase(actor);
    sDropPos.erase(actor);
}

static void OnPlayerUpdated() {
    if (gPlayState == nullptr || !IsRunActive()) {
        if (!sParticles.empty() || !sInstances.empty()) {
            Fx_Clear();
        }
        sDropFx.clear();
        sDropPos.clear();
        sEnemyHealth.clear();
        return;
    }
    Watch(gPlayState, GET_PLAYER(gPlayState));
    UpdateFx();
}

// ---------------------------------------------------------------------------
// Drawing

struct DrawBuffer {
    std::vector<Vtx> verts;
    std::vector<Gfx> dl;
};
static DrawBuffer sBuffers[4]; // two views a frame (co-op), two frames in flight
static int sBufferIndex = 0;

static const u8* ImageOf(const Particle& p, int* w, int* h) {
    const Emitter& em = *p.em;
    if (em.image >= 0 && em.image < static_cast<int>(p.def->images.size())) {
        const FxImage& img = p.def->images[em.image];
        *w = img.w;
        *h = img.h;
        return img.rgba.data();
    }
    *w = *h = kTexSize;
    return sShapes[em.shape].data();
}

static int Log2(int n) {
    int l = 0;
    while ((1 << l) < n) {
        l++;
    }
    return l;
}

void Fx_Draw(PlayState* play) {
    if (play == nullptr || sParticles.empty()) {
        return;
    }
    MakeShapes();
    DrawBuffer& buf = sBuffers[sBufferIndex];
    sBufferIndex = (sBufferIndex + 1) % 4;
    buf.verts.clear();
    buf.dl.clear();
    buf.verts.reserve(kMaxParticles * 4);
    buf.dl.reserve(kMaxParticles * 4 + 64);

    Vec3f eye = play->view.eye;
    Vec3f fwd = Norm({ play->view.lookAt.x - eye.x, play->view.lookAt.y - eye.y, play->view.lookAt.z - eye.z }, { 0, 0, 1 });
    Vec3f right = Norm(Cross(fwd, { 0.0f, 1.0f, 0.0f }), { 1, 0, 0 });
    Vec3f up = Cross(right, fwd);

    // Particles sharing an image draw together.
    std::vector<const Particle*> order;
    order.reserve(sParticles.size());
    for (const Particle& p : sParticles) {
        if (p.age < 0.0f) {
            continue;
        }
        Vec3f d = { p.pos.x - eye.x, p.pos.y - eye.y, p.pos.z - eye.z };
        float along = Dot(d, fwd);
        if (along < -20.0f || Dot(d, d) > kDrawRange * kDrawRange) {
            continue;
        }
        order.push_back(&p);
    }
    if (order.empty()) {
        return;
    }
    std::sort(order.begin(), order.end(), [](const Particle* a, const Particle* b) {
        int wa, ha, wb, hb;
        return ImageOf(*a, &wa, &ha) < ImageOf(*b, &wb, &hb);
    });

    auto put = [&buf](auto&& emit) {
        Gfx g[12];
        Gfx* p = g;
        emit(p);
        buf.dl.insert(buf.dl.end(), g, p);
    };
    put([](Gfx*& p) {
        gDPPipeSync(p++);
        gDPSetCycleType(p++, G_CYC_1CYCLE);
        gDPSetRenderMode(p++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
        gSPClearGeometryMode(p++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
        gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
        gDPSetTextureFilter(p++, G_TF_BILERP);
        gDPSetTexturePersp(p++, G_TP_PERSP);
        gDPSetAlphaCompare(p++, G_AC_NONE);
        gDPSetCombineLERP(p++, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0);
        gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    });

    const u8* bound = nullptr;
    int tw = kTexSize, th = kTexSize;
    size_t batchStart = 0;
    auto flush = [&]() {
        size_t n = buf.verts.size() - batchStart;
        if (n == 0) {
            return;
        }
        Gfx vg;
        Gfx* vp = &vg;
        gSPVertex(vp++, (uintptr_t)&buf.verts[batchStart], static_cast<u32>(n), 0);
        buf.dl.push_back(vg);
        for (size_t q = 0; q < n; q += 4) {
            Gfx tg;
            Gfx* tp = &tg;
            gSP2Triangles(tp++, static_cast<u8>(q), static_cast<u8>(q + 1), static_cast<u8>(q + 2), 0,
                          static_cast<u8>(q), static_cast<u8>(q + 2), static_cast<u8>(q + 3), 0);
            buf.dl.push_back(tg);
        }
        batchStart = buf.verts.size();
    };

    for (const Particle* pp : order) {
        const Particle& p = *pp;
        const Emitter& em = *p.em;
        int w, h;
        const u8* img = ImageOf(p, &w, &h);
        if (img != bound || buf.verts.size() - batchStart >= 32) {
            flush();
        }
        if (img != bound) {
            bound = img;
            tw = w;
            th = h;
            put([&](Gfx*& g) {
                gDPPipeSync(g++);
                gDPLoadTextureBlock(g++, img, G_IM_FMT_RGBA, G_IM_SIZ_32b, w, h, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                    G_TX_NOMIRROR | G_TX_CLAMP, Log2(w), Log2(h), G_TX_NOLOD, G_TX_NOLOD);
            });
        }

        // Look at this point of its life.
        float f = p.age / p.life;
        int a, b;
        float t;
        Sample(em.vis, f, &a, &b, &t);
        const VisSample& va = em.vis[a];
        const VisSample& vb = em.vis[b];
        float c[4];
        for (int k = 0; k < 4; k++) {
            float ca = va.rgba[k] + (va.rgba2[k] - va.rgba[k]) * p.rc;
            float cb = vb.rgba[k] + (vb.rgba2[k] - vb.rgba[k]) * p.rc;
            c[k] = ca + (cb - ca) * t;
        }
        float hw = ((va.size[0] + p.rs * va.sizeRand[0]) * (1.0f - t) + (vb.size[0] + p.rs * vb.sizeRand[0]) * t) * p.scale;
        float hh = ((va.size[1] + p.rs * va.sizeRand[1]) * (1.0f - t) + (vb.size[1] + p.rs * vb.sizeRand[1]) * t) * p.scale;
        float roll = p.roll + va.roll + (vb.roll - va.roll) * t;
        if (p.def->imported && em.image < 0) {
            // A Black Ops sprite's own image is mostly see-through; our solid stand-in
            // shapes would cover far more, so they're drawn fainter.
            static const float kShapeAlpha[SHAPE_COUNT] = { 0.8f, 0.4f, 0.9f, 0.8f, 0.55f, 0.7f };
            c[3] *= kShapeAlpha[em.shape];
        }
        if (hw <= 0.01f || hh <= 0.01f || c[3] < 1.0f) {
            continue;
        }
        if (em.blend == 1) {
            // No additive blending in the renderer: brightness becomes coverage.
            float m = std::max({ c[0], c[1], c[2], 1.0f });
            c[3] = c[3] * m / 255.0f;
            for (int k = 0; k < 3; k++) {
                c[k] = c[k] * 255.0f / m;
            }
        }

        Vec3f ax, ay;
        if (em.render == 1) {
            Vec3f motion = { p.pos.x - p.prev.x, p.pos.y - p.prev.y, p.pos.z - p.prev.z };
            ay = Dot(motion, motion) > 0.01f ? Norm(motion) : p.F;
            Vec3f toEye = { eye.x - p.pos.x, eye.y - p.pos.y, eye.z - p.pos.z };
            ax = Norm(Cross(ay, toEye), right);
        } else if (em.render == 2) {
            ax = p.L;
            ay = p.U;
        } else {
            float cr = cosf(roll), sr = sinf(roll);
            ax = Add(Mul(right, cr), Mul(up, sr));
            ay = Add(Mul(up, cr), Mul(right, -sr));
        }

        // Atlas cell.
        int cols = std::max<int>(em.cols, 1), rows = std::max<int>(em.rows, 1);
        int cell = p.frame % (cols * rows);
        float u0 = static_cast<float>(cell % cols) / static_cast<float>(cols);
        float v0 = static_cast<float>(cell / cols) / static_cast<float>(rows);
        float u1 = u0 + 1.0f / static_cast<float>(cols);
        float v1 = v0 + 1.0f / static_cast<float>(rows);

        static const float kCorner[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (int k = 0; k < 4; k++) {
            Vec3f q = Add(p.pos, Add(Mul(ax, hw * kCorner[k][0]), Mul(ay, hh * kCorner[k][1])));
            Vtx v = {};
            v.v.ob[0] = static_cast<s16>(std::clamp((q.x - eye.x) * kPrecision, -32760.0f, 32760.0f));
            v.v.ob[1] = static_cast<s16>(std::clamp((q.y - eye.y) * kPrecision, -32760.0f, 32760.0f));
            v.v.ob[2] = static_cast<s16>(std::clamp((q.z - eye.z) * kPrecision, -32760.0f, 32760.0f));
            float u = kCorner[k][0] < 0 ? u0 : u1;
            float vv = kCorner[k][1] < 0 ? v1 : v0;
            v.v.tc[0] = static_cast<s16>(u * static_cast<float>(tw) * 32.0f);
            v.v.tc[1] = static_cast<s16>(vv * static_cast<float>(th) * 32.0f);
            for (int ch = 0; ch < 4; ch++) {
                v.v.cn[ch] = static_cast<u8>(std::clamp(c[ch], 0.0f, 255.0f));
            }
            buf.verts.push_back(v);
        }
    }
    flush();
    put([](Gfx*& p) {
        gDPPipeSync(p++);
        gSPSetGeometryMode(p++, G_LIGHTING | G_FOG | G_CULL_BACK);
        gSPEndDisplayList(p++);
    });
    ZombiesFx_Submit(play, buf.dl.data(), &eye, 1.0f / kPrecision);
}

static void RegisterFx() {
    bool enabled = CVarGetInteger(CVAR_ZOMBIES("Enabled"), 0) || CVarGetInteger(CVAR_ZOMBIES("ZombiesOnly"), 1);
    COND_HOOK(OnPlayerUpdate, enabled, OnPlayerUpdated);
    COND_HOOK(OnActorDestroy, enabled, OnActorDestroyed);
    COND_HOOK(OnSceneInit, enabled, [](int16_t) {
        Fx_Clear();
        sDropFx.clear();
        sDropPos.clear();
        sEnemyHealth.clear();
    });
}

static RegisterShipInitFunc fxInit(RegisterFx, { CVAR_ZOMBIES("Enabled"), CVAR_ZOMBIES("ZombiesOnly") });

} // namespace Zombies
