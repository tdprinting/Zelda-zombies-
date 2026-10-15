// Zombies of Time: Black Ops maps.
//
// Plays a map converted from the player's own Black Ops install (tools/bomap writes it,
// docs/BLACK_OPS_MAPS.md). Nothing from Black Ops ships with the mode: the converted
// file sits in SoH's folder and is read here at run time.
//
// The map is played in Kakariko Graveyard's slot, which already gives it a night sky.
// When the scene loads, its collision is swapped for the map's, its actors are cleared,
// and its room draws the map's geometry instead of the graveyard.
//
// OoT's limits that a Black Ops map runs into, and what happens here:
// - Collision memory is sized per scene, far smaller than a whole map needs. The map's
//   collision gets its own allocation here, sized from the map.
// - Collision vertex ids were 13 bits (8191 vertices); the engine patch
//   mod/patches/0003-collision-vertex-ids-past-8191.patch keeps the top bits in
//   CollisionPoly::vtxHi, so up to 65535. Polygon ids are s16 (32767) and lookup nodes
//   u16. The converter keeps the collision mesh inside those; this file refuses more.
// - Display lists draw from a 32-vertex buffer: meshes are cut into batches.

#include "ZombiesMode.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
void BgCheck_SetSubdivisionDimension(f32 min, s32 subdivAmount, f32* max, f32* subdivLength, f32* subdivLengthInv);
u32 BgCheck_InitializeStaticLookup(CollisionContext* colCtx, PlayState* play, StaticLookup* lookupTbl);
}

namespace Zombies {

static constexpr u32 kVersion = 1;
static constexpr int kMaxColVerts = 0xFFFF; // 13 bits beside the flags, 3 more in vtxHi
static constexpr int kMaxColPolys = 0x7FFF; // polygon ids are s16
static constexpr int kMaxNodes = 0xFFFE;    // lookup node ids are u16, 0xFFFF ends a list
static constexpr float kCellSize = 200.0f;  // collision lookup cell, in OoT units

enum ColFlag : u8 {
    COL_WALL_ONLY = 1,
};

enum MatFlag : u8 {
    MAT_ALPHA_TEST = 1,
    MAT_TRANSLUCENT = 2,
    MAT_TWO_SIDED = 4,
};

struct BoImage {
    std::string name;
    u16 w = 0;
    u16 h = 0;
    std::vector<u8> rgba;
};

struct BoMaterial {
    std::string name;
    int image = -1;
    u8 flags = 0;
};

struct BoMesh {
    u32 material = 0;
    std::vector<Vtx> verts; // three per triangle: each triangle gets its own texture offset
};

struct BoMap {
    bool loaded = false;
    float scale = 1.0f;
    Vec3s spawn = {};
    s16 spawnYaw = 0;

    std::vector<Vec3s> colVerts;
    std::vector<CollisionPoly> colPolys;
    std::vector<SurfaceType> surfaces;
    CamData camData = {};
    CollisionHeader colHeader = {};

    std::vector<BoImage> images;
    std::vector<BoMaterial> materials;
    std::vector<BoMesh> meshes;
    std::vector<Gfx> opaDl;
    std::vector<Gfx> xluDl;
    PolygonDlist dlists[1] = {};
    MeshHeader meshHeader = {};

    ArenaLayout layout;
    std::vector<std::string> names; // backing for layout zone names
};

static BoMap sMap;
static bool sTriedLoad = false;
static bool sSelected = false; // the lobby picked the Black Ops map

// ---------------------------------------------------------------------------
// Reading the converted file

class Reader {
  public:
    explicit Reader(std::vector<u8> data) : mData(std::move(data)) {
    }
    bool Ok() const {
        return mOk;
    }
    template <typename T> T Get() {
        T value{};
        Bytes(&value, sizeof(T));
        return value;
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
    std::string Name() {
        char buf[64];
        Bytes(buf, sizeof(buf));
        buf[63] = '\0';
        return buf;
    }
    // A count that must fit in what's left of the file, each element at least elemSize bytes.
    u32 Count(size_t elemSize) {
        u32 n = Get<u32>();
        if (elemSize > 0 && n > (mData.size() - std::min(mPos, mData.size())) / elemSize) {
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

std::string BoMap_Path() {
    return Ship::Context::GetPathRelativeToAppDirectory("zombies/nacht.zzmap");
}

bool BoMap_Available() {
    std::error_code ec;
    return std::filesystem::exists(BoMap_Path(), ec);
}

static u32 SurfaceSfx(u8 surface) {
    // OoT's footstep sounds: 0 dirt, 2 stone, 4 shallow water, 8 grass, 10 wood.
    switch (surface) {
        case 1:
            return 0;
        case 2:
            return 10;
        case 4:
            return 8;
        case 5:
            return 4;
        default:
            return 2;
    }
}

static bool BuildCollision(BoMap& map, const std::vector<std::array<u16, 3>>& tris, const std::vector<u8>& triSurface,
                           const std::vector<u8>& triFlags) {
    if (map.colVerts.empty() || map.colVerts.size() > kMaxColVerts || tris.empty() || tris.size() > kMaxColPolys) {
        return false;
    }
    // One surface type per (surface, flags) pair. Camera data 0 is the normal camera.
    map.surfaces.clear();
    std::vector<int> surfaceIndex(256 * 2, -1);
    map.colPolys.clear();
    map.colPolys.reserve(tris.size());
    for (size_t i = 0; i < tris.size(); i++) {
        const Vec3s& a = map.colVerts[tris[i][0]];
        const Vec3s& b = map.colVerts[tris[i][1]];
        const Vec3s& c = map.colVerts[tris[i][2]];
        float ux = static_cast<float>(b.x - a.x), uy = static_cast<float>(b.y - a.y), uz = static_cast<float>(b.z - a.z);
        float vx = static_cast<float>(c.x - a.x), vy = static_cast<float>(c.y - a.y), vz = static_cast<float>(c.z - a.z);
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len < 0.5f) {
            continue; // a sliver: OoT's floor and wall checks divide by its normal
        }
        nx /= len;
        ny /= len;
        nz /= len;
        bool wallOnly = (triFlags[i] & COL_WALL_ONLY) != 0;
        int key = triSurface[i] * 2 + (wallOnly ? 1 : 0);
        if (surfaceIndex[key] < 0) {
            surfaceIndex[key] = static_cast<int>(map.surfaces.size());
            SurfaceType type = {};
            type.data[0] = 0; // camera data 0, no exits, no special floor
            type.data[1] = SurfaceSfx(triSurface[i]);
            map.surfaces.push_back(type);
        }
        CollisionPoly poly = {};
        poly.type = static_cast<u16>(surfaceIndex[key]);
        // Clip walls are invisible in Black Ops: the camera and arrows go through them.
        u16 exclude = wallOnly ? 0x2000 | 0x8000 : 0;
        poly.flags_vIA = static_cast<u16>((tris[i][0] & 0x1FFF) | exclude);
        poly.flags_vIB = static_cast<u16>(tris[i][1] & 0x1FFF);
        poly.vtxHi = static_cast<u16>(((tris[i][0] >> 13) & 7) | (((tris[i][1] >> 13) & 7) << 3));
        poly.vIC = tris[i][2];
        poly.normal.x = static_cast<s16>(nx * 0x7FFF);
        poly.normal.y = static_cast<s16>(ny * 0x7FFF);
        poly.normal.z = static_cast<s16>(nz * 0x7FFF);
        poly.dist = static_cast<s16>(-(nx * a.x + ny * a.y + nz * a.z));
        map.colPolys.push_back(poly);
    }
    if (map.colPolys.empty()) {
        return false;
    }

    Vec3s lo = map.colVerts[0];
    Vec3s hi = map.colVerts[0];
    for (const Vec3s& v : map.colVerts) {
        lo.x = std::min(lo.x, v.x);
        lo.y = std::min(lo.y, v.y);
        lo.z = std::min(lo.z, v.z);
        hi.x = std::max(hi.x, v.x);
        hi.y = std::max(hi.y, v.y);
        hi.z = std::max(hi.z, v.z);
    }
    map.camData = { CAM_SET_NORMAL0, 0, nullptr };
    CollisionHeader& h = map.colHeader;
    h = {};
    h.minBounds = { static_cast<s16>(lo.x - 50), static_cast<s16>(lo.y - 50), static_cast<s16>(lo.z - 50) };
    h.maxBounds = { static_cast<s16>(hi.x + 50), static_cast<s16>(hi.y + 50), static_cast<s16>(hi.z + 50) };
    h.numVertices = static_cast<u16>(map.colVerts.size());
    h.vtxList = map.colVerts.data();
    h.numPolygons = static_cast<u16>(map.colPolys.size());
    h.polyList = map.colPolys.data();
    h.surfaceTypeList = map.surfaces.data();
    h.cameraDataList = &map.camData;
    h.cameraDataListLen = 1;
    h.numWaterBoxes = 0;
    h.waterBoxes = nullptr;
    return true;
}

static int Log2(int n) {
    int l = 0;
    while ((1 << l) < n) {
        l++;
    }
    return l;
}

// One material's state, set before its triangles.
static void EmitMaterial(std::vector<Gfx>& dl, const BoMap& map, const BoMaterial& mat) {
    Gfx g[16];
    Gfx* p = g;
    gDPPipeSync(p++);
    gDPSetCycleType(p++, G_CYC_2CYCLE);
    u32 geometry = G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_FOG;
    if (!(mat.flags & MAT_TWO_SIDED)) {
        geometry |= G_CULL_BACK;
    }
    gSPLoadGeometryMode(p++, geometry);
    if (mat.flags & MAT_TRANSLUCENT) {
        gDPSetRenderMode(p++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_XLU_SURF2);
    } else if (mat.flags & MAT_ALPHA_TEST) {
        gDPSetRenderMode(p++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_TEX_EDGE2);
    } else {
        gDPSetRenderMode(p++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
    }
    const BoImage* image = mat.image >= 0 && mat.image < static_cast<int>(map.images.size()) ? &map.images[mat.image]
                                                                                            : nullptr;
    if (image != nullptr) {
        gDPSetCombineMode(p++, G_CC_MODULATEIDECALA, G_CC_PASS2);
        gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        dl.insert(dl.end(), g, p);
        p = g;
        Gfx load[8];
        Gfx* q = load;
        gDPLoadTextureBlock(q++, image->rgba.data(), G_IM_FMT_RGBA, G_IM_SIZ_32b, image->w, image->h, 0,
                            G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, Log2(image->w), Log2(image->h),
                            G_TX_NOLOD, G_TX_NOLOD);
        dl.insert(dl.end(), load, q);
    } else {
        gDPSetCombineMode(p++, G_CC_SHADE, G_CC_PASS2);
        gSPTexture(p++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        dl.insert(dl.end(), g, p);
    }
}

static void EmitMesh(std::vector<Gfx>& dl, const BoMesh& mesh) {
    // 30 vertices a batch: ten triangles that each own their three.
    for (size_t base = 0; base < mesh.verts.size(); base += 30) {
        size_t n = std::min<size_t>(30, mesh.verts.size() - base);
        Gfx g[8];
        Gfx* p = g;
        gSPVertex(p++, (uintptr_t)&mesh.verts[base], static_cast<u32>(n), 0);
        dl.insert(dl.end(), g, p);
        for (size_t t = 0; t + 2 < n; t += 3) {
            Gfx tri;
            Gfx* q = &tri;
            gSP1Triangle(q++, static_cast<u8>(t), static_cast<u8>(t + 1), static_cast<u8>(t + 2), 0);
            dl.push_back(tri);
        }
    }
}

static void BuildDisplayLists(BoMap& map) {
    map.opaDl.clear();
    map.xluDl.clear();
    // Vertex pointers in the lists point into the meshes: nothing may resize them from here.
    for (bool translucent : { false, true }) {
        std::vector<Gfx>& dl = translucent ? map.xluDl : map.opaDl;
        for (u32 m = 0; m < map.materials.size(); m++) {
            const BoMaterial& mat = map.materials[m];
            if (((mat.flags & MAT_TRANSLUCENT) != 0) != translucent) {
                continue;
            }
            bool emitted = false;
            for (const BoMesh& mesh : map.meshes) {
                if (mesh.material != m || mesh.verts.empty()) {
                    continue;
                }
                if (!emitted) {
                    EmitMaterial(dl, map, mat);
                    emitted = true;
                }
                EmitMesh(dl, mesh);
            }
        }
        Gfx end[2];
        Gfx* p = end;
        gDPPipeSync(p++);
        gSPEndDisplayList(p++);
        dl.insert(dl.end(), end, p);
    }
    map.dlists[0].opa = map.opaDl.size() > 2 ? map.opaDl.data() : nullptr;
    map.dlists[0].xlu = map.xluDl.size() > 2 ? map.xluDl.data() : nullptr;
    map.meshHeader.polygon0.base.type = 0;
    map.meshHeader.polygon0.num = 1;
    map.meshHeader.polygon0.start = map.dlists;
    map.meshHeader.polygon0.end = map.dlists + 1;
}

// ---------------------------------------------------------------------------
// The layout, from the map's own entities (the JSON bomap writes)

struct Entity {
    std::string kind;
    std::string detail;
    float pos[3] = {};
    float yaw = 0.0f;
    int cost = 0;
};

// Pulls `"key": value` out of one JSON object line. The converter writes one entity a line.
static bool JsonString(const std::string& line, const char* key, std::string* out) {
    std::string pat = std::string("\"") + key + "\": \"";
    size_t at = line.find(pat);
    if (at == std::string::npos) {
        return false;
    }
    at += pat.size();
    size_t end = line.find('"', at);
    if (end == std::string::npos) {
        return false;
    }
    *out = line.substr(at, end - at);
    return true;
}

static bool JsonNumber(const std::string& line, const char* key, float* out) {
    std::string pat = std::string("\"") + key + "\": ";
    size_t at = line.find(pat);
    if (at == std::string::npos) {
        return false;
    }
    *out = strtof(line.c_str() + at + pat.size(), nullptr);
    return true;
}

static bool JsonVec3(const std::string& line, const char* key, float out[3]) {
    std::string pat = std::string("\"") + key + "\": [";
    size_t at = line.find(pat);
    if (at == std::string::npos) {
        return false;
    }
    const char* s = line.c_str() + at + pat.size();
    char* end;
    for (int i = 0; i < 3; i++) {
        out[i] = strtof(s, &end);
        s = end;
        while (*s == ',' || *s == ' ') {
            s++;
        }
    }
    return true;
}

static std::vector<Entity> ParseEntities(const std::string& json) {
    std::vector<Entity> out;
    size_t start = 0;
    while (start < json.size()) {
        size_t end = json.find('\n', start);
        if (end == std::string::npos) {
            end = json.size();
        }
        std::string line = json.substr(start, end - start);
        start = end + 1;
        Entity e;
        if (!JsonString(line, "kind", &e.kind) || !JsonVec3(line, "pos", e.pos)) {
            continue;
        }
        JsonString(line, "detail", &e.detail);
        JsonNumber(line, "yaw", &e.yaw);
        float cost = 0.0f;
        if (JsonNumber(line, "cost", &cost)) {
            e.cost = static_cast<int>(cost);
        }
        out.push_back(e);
    }
    return out;
}

static LayoutSpot Spot(const Entity& e, float scale) {
    LayoutSpot s;
    s.x = e.pos[0] * scale;
    s.z = e.pos[2] * scale;
    s.y = e.pos[1] * scale;
    return s;
}

// Black Ops zone volumes name the room they cover. Our zones: 0 the start room, then one
// per door or debris pile, in the order the map's doors lead.
static int ZoneForName(const std::string& name) {
    if (name.find("start") == 0) {
        return 0;
    }
    if (name.find("box") == 0) {
        return 2;
    }
    return 1; // upstairs and anything else
}

static void BuildLayout(BoMap& map, const std::vector<Entity>& ents) {
    ArenaLayout& layout = map.layout;
    layout.sceneNum = SCENE_GRAVEYARD;
    layout.name = "NACHT DER UNTOTEN";
    layout.zones.clear();
    layout.zones.resize(3);
    static const char* kZoneNames[] = { "START ROOM", "HELP ROOM", "BOX ROOM" };
    for (int z = 0; z < 3; z++) {
        layout.zones[z].name = kZoneNames[z];
        layout.zones[z].cost = z == 0 ? 0 : 1000;
    }

    struct Volume {
        int zone;
        float x, y, z;
    };
    std::vector<Volume> volumes;
    bool haveStart = false;
    float sx = 0, sy = 0, sz = 0;
    int starts = 0;
    bool haveCentre[3] = {};
    float cx[3] = {}, cz[3] = {}, cy[3] = {};
    int cn[3] = {};
    for (const Entity& e : ents) {
        if (e.kind == "zone") {
            int z = ZoneForName(e.detail);
            volumes.push_back({ z, e.pos[0] * map.scale, e.pos[1] * map.scale, e.pos[2] * map.scale });
            cx[z] += e.pos[0] * map.scale;
            cy[z] += e.pos[1] * map.scale;
            cz[z] += e.pos[2] * map.scale;
            cn[z]++;
        } else if (e.kind == "player_start") {
            sx += e.pos[0] * map.scale;
            sy += e.pos[1] * map.scale;
            sz += e.pos[2] * map.scale;
            starts++;
            haveStart = true;
        }
    }
    // A spot's zone is the nearest zone volume on its floor.
    auto zoneAt = [&](float x, float y, float z) {
        int best = 0;
        float bestD = 1e30f;
        for (const Volume& v : volumes) {
            float dy = fabsf(v.y - y) > 120.0f ? 1000.0f : 0.0f;
            float d = (v.x - x) * (v.x - x) + (v.z - z) * (v.z - z) + dy * dy;
            if (d < bestD) {
                bestD = d;
                best = v.zone;
            }
        }
        return best;
    };
    for (int z = 0; z < 3; z++) {
        if (cn[z] > 0) {
            haveCentre[z] = true;
            layout.zones[z].centre = { cx[z] / cn[z], cz[z] / cn[z] };
            layout.zones[z].centre.y = cy[z] / cn[z];
        }
    }
    if (haveStart) {
        layout.zones[0].centre = { sx / starts, sz / starts };
        layout.zones[0].centre.y = sy / starts;
    }
    for (int z = 1; z < 3; z++) {
        if (!haveCentre[z]) {
            layout.zones[z].centre = layout.zones[0].centre;
        }
    }

    bool armoryPlaced = false;
    bool chestPlaced = false;
    bool debrisPlaced[3] = {};
    for (const Entity& e : ents) {
        LayoutSpot spot = Spot(e, map.scale);
        if (e.kind == "window") {
            // Windows belong to the room they open into.
            layout.zones[zoneAt(spot.x, spot.y, spot.z)].windows.push_back(spot);
        } else if (e.kind == "debris" || e.kind == "door") {
            // A door or debris pile leads into the room named after the arrow: start_2_box opens the box room.
            size_t arrow = e.detail.find("_2_");
            int into = arrow == std::string::npos ? 1 : ZoneForName(e.detail.substr(arrow + 3));
            if (into > 0 && !debrisPlaced[into]) {
                debrisPlaced[into] = true;
                layout.zones[into].debris = spot;
                if (e.cost > 0) {
                    layout.zones[into].cost = e.cost;
                }
            }
        } else if (e.kind == "wall_buy" && !armoryPlaced) {
            // Our Armory stands at the first wall buy in the start room; the guns go on the rest.
            if (zoneAt(spot.x, spot.y, spot.z) == 0) {
                layout.zones[0].stations.push_back({ StationType::Armory, spot });
                armoryPlaced = true;
            }
        } else if (e.kind == "chest") {
            int z = zoneAt(spot.x, spot.y, spot.z);
            if (!chestPlaced) {
                layout.zones[z].stations.push_back({ StationType::MysteryChest, spot });
                chestPlaced = true;
            }
            layout.zones[z].chestSpots.push_back(spot);
        }
    }
    // Nacht has no perks, power or Pack-a-Punch, as in Black Ops. The Fairy Fountain
    // stands by the start so hearts can still be bought.
    LayoutSpot fountain = layout.zones[0].centre;
    fountain.x += 60.0f;
    layout.zones[0].stations.push_back({ StationType::FairyFountain, fountain });
    if (!armoryPlaced) {
        LayoutSpot armory = layout.zones[0].centre;
        armory.x -= 60.0f;
        layout.zones[0].stations.push_back({ StationType::Armory, armory });
    }
    // A zone the map gave no way into would never open: fold it into the start room.
    for (int z = 2; z >= 1; z--) {
        if (!debrisPlaced[z]) {
            for (const LayoutSpot& w : layout.zones[z].windows) {
                layout.zones[0].windows.push_back(w);
            }
            layout.zones.erase(layout.zones.begin() + z);
        }
    }
}

static bool LoadFile(BoMap& map) {
    std::ifstream file(BoMap_Path(), std::ios::binary);
    if (!file) {
        return false;
    }
    std::vector<u8> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Reader r(std::move(data));
    char magic[8];
    r.Bytes(magic, 8);
    if (std::memcmp(magic, "ZZBOMAP1", 8) != 0 || r.Get<u32>() != kVersion) {
        return false;
    }
    map.scale = r.Get<float>();
    map.spawn.x = r.Get<s16>();
    map.spawn.y = r.Get<s16>();
    map.spawn.z = r.Get<s16>();
    map.spawnYaw = r.Get<s16>();

    u32 nv = r.Count(6);
    map.colVerts.resize(nv);
    for (Vec3s& v : map.colVerts) {
        v.x = r.Get<s16>();
        v.y = r.Get<s16>();
        v.z = r.Get<s16>();
    }
    u32 nt = r.Count(8);
    std::vector<std::array<u16, 3>> tris(nt);
    std::vector<u8> surf(nt), flags(nt);
    for (u32 i = 0; i < nt; i++) {
        for (int k = 0; k < 3; k++) {
            tris[i][k] = r.Get<u16>();
            if (tris[i][k] >= nv) {
                return false;
            }
        }
        surf[i] = r.Get<u8>();
        flags[i] = r.Get<u8>();
    }

    u32 ni = r.Count(68);
    map.images.resize(ni);
    for (BoImage& img : map.images) {
        img.name = r.Name();
        img.w = r.Get<u16>();
        img.h = r.Get<u16>();
        if (img.w == 0 || img.h == 0 || img.w > 256 || img.h > 256 || (img.w & (img.w - 1)) || (img.h & (img.h - 1))) {
            return false;
        }
        img.rgba.resize(static_cast<size_t>(img.w) * img.h * 4);
        r.Bytes(img.rgba.data(), img.rgba.size());
    }
    u32 nm = r.Count(69);
    map.materials.resize(nm);
    for (BoMaterial& mat : map.materials) {
        mat.name = r.Name();
        mat.image = r.Get<s32>();
        mat.flags = r.Get<u8>();
    }
    u32 nmesh = r.Count(12);
    map.meshes.resize(nmesh);
    for (BoMesh& mesh : map.meshes) {
        mesh.material = r.Get<u32>();
        if (mesh.material >= nm) {
            return false;
        }
        const BoMaterial& mat = map.materials[mesh.material];
        int tw = 32, th = 32;
        if (mat.image >= 0 && mat.image < static_cast<int>(ni)) {
            tw = map.images[mat.image].w;
            th = map.images[mat.image].h;
        }
        struct SrcVert {
            s16 pos[3];
            float uv[2];
            u8 rgba[4];
        };
        u32 vc = r.Count(18);
        std::vector<SrcVert> src(vc);
        for (SrcVert& v : src) {
            v.pos[0] = r.Get<s16>();
            v.pos[1] = r.Get<s16>();
            v.pos[2] = r.Get<s16>();
            v.uv[0] = r.Get<float>();
            v.uv[1] = r.Get<float>();
            r.Bytes(v.rgba, 4);
        }
        u32 ic = r.Count(2);
        std::vector<u16> idx(ic);
        for (u16& i : idx) {
            i = r.Get<u16>();
            if (i >= vc) {
                return false;
            }
        }
        mesh.verts.reserve(ic);
        for (u32 t = 0; t + 2 < ic; t += 3) {
            // Texture coordinates are 10.5 fixed point: shift each triangle's by whole
            // repeats so tiling far from the origin stays in range.
            float offU = std::floor(std::min({ src[idx[t]].uv[0], src[idx[t + 1]].uv[0], src[idx[t + 2]].uv[0] }));
            float offV = std::floor(std::min({ src[idx[t]].uv[1], src[idx[t + 1]].uv[1], src[idx[t + 2]].uv[1] }));
            for (int k = 0; k < 3; k++) {
                const SrcVert& s = src[idx[t + k]];
                Vtx v = {};
                v.v.ob[0] = s.pos[0];
                v.v.ob[1] = s.pos[1];
                v.v.ob[2] = s.pos[2];
                v.v.tc[0] = static_cast<s16>(std::clamp((s.uv[0] - offU) * tw * 32.0f, -32768.0f, 32767.0f));
                v.v.tc[1] = static_cast<s16>(std::clamp((s.uv[1] - offV) * th * 32.0f, -32768.0f, 32767.0f));
                std::memcpy(v.v.cn, s.rgba, 4);
                mesh.verts.push_back(v);
            }
        }
    }
    u32 entsLen = r.Count(1);
    std::string ents(entsLen, '\0');
    r.Bytes(ents.data(), entsLen);
    if (!r.Ok()) {
        return false;
    }
    if (!BuildCollision(map, tris, surf, flags)) {
        return false;
    }
    BuildDisplayLists(map);
    BuildLayout(map, ParseEntities(ents));
    map.loaded = true;
    return true;
}

static bool EnsureLoaded() {
    if (!sTriedLoad) {
        sTriedLoad = true;
        if (!LoadFile(sMap)) {
            sMap = BoMap();
        }
    }
    return sMap.loaded;
}

// ---------------------------------------------------------------------------
// In the game

void BoMap_Select(bool selected) {
    sSelected = selected;
    if (selected && !sMap.loaded) {
        sTriedLoad = false; // pick up a map converted since the last try
    }
}

bool BoMap_Selected() {
    return sSelected;
}

bool BoMap_Active(PlayState* play) {
    return sSelected && play != nullptr && play->sceneNum == SCENE_GRAVEYARD && sMap.loaded;
}

const ArenaLayout* BoMap_Layout() {
    return sMap.loaded ? &sMap.layout : nullptr;
}

// OoT sizes collision memory per scene; a whole Black Ops map needs much more. This is
// BgCheck_Allocate with the map's own budget: up to 64K lookup nodes and a cell grid
// sized to the map.
// An upper bound on the static lookup nodes BgCheck_InitializeStaticLookup will use.
static size_t LookupNodesNeeded(const CollisionHeader* header, const Vec3i& amount) {
    const s16 lo[3] = { header->minBounds.x, header->minBounds.y, header->minBounds.z };
    const s16 hi[3] = { header->maxBounds.x, header->maxBounds.y, header->maxBounds.z };
    const int n[3] = { amount.x, amount.y, amount.z };
    float len[3];
    for (int a = 0; a < 3; a++) {
        len[a] = std::max(static_cast<float>(hi[a] - lo[a]) / static_cast<float>(n[a]), 1.0f) + 1.0f;
    }
    size_t total = 0;
    for (int i = 0; i < header->numPolygons; i++) {
        const CollisionPoly* poly = &header->polyList[i];
        const Vec3s* v[3] = { &header->vtxList[COLPOLY_VTX_A(poly)], &header->vtxList[COLPOLY_VTX_B(poly)],
                              &header->vtxList[poly->vIC] };
        size_t count = 1;
        for (int a = 0; a < 3; a++) {
            float mn = 32767.0f, mx = -32768.0f;
            for (const Vec3s* p : v) {
                float c = static_cast<float>(a == 0 ? p->x : a == 1 ? p->y : p->z);
                mn = std::min(mn, c);
                mx = std::max(mx, c);
            }
            int c0 = std::clamp(static_cast<int>((mn - BGCHECK_SUBDIV_OVERLAP - lo[a]) / len[a]), 0, n[a] - 1);
            int c1 = std::clamp(static_cast<int>((mx + BGCHECK_SUBDIV_OVERLAP - lo[a]) / len[a]), 0, n[a] - 1);
            count *= static_cast<size_t>(c1 - c0 + 1);
        }
        total += count;
    }
    return total;
}

static void AllocateCollision(PlayState* play, CollisionHeader* header) {
    CollisionContext* colCtx = &play->colCtx;
    colCtx->colHeader = header;
    colCtx->dyna.polyNodesMax = 1000;
    colCtx->dyna.polyListMax = 1024;
    colCtx->dyna.vtxListMax = 1024;
    // Each polygon takes a lookup node in every cell its box (plus OoT's 50 unit overlap)
    // reaches. Cells start at kCellSize and grow until the worst case fits the u16 node ids.
    float cellSize = kCellSize;
    for (int tries = 0; tries < 32; tries++, cellSize *= 1.25f) {
        auto cells = [&](s16 lo, s16 hi, int minCells, int maxCells) {
            return std::clamp(static_cast<int>(static_cast<float>(hi - lo) / cellSize) + 1, minCells, maxCells);
        };
        colCtx->subdivAmount.x = cells(header->minBounds.x, header->maxBounds.x, 4, 64);
        colCtx->subdivAmount.y = cells(header->minBounds.y, header->maxBounds.y, 2, 16);
        colCtx->subdivAmount.z = cells(header->minBounds.z, header->maxBounds.z, 4, 64);
        if (LookupNodesNeeded(header, colCtx->subdivAmount) <= kMaxNodes) {
            break;
        }
    }
    size_t lookupSize =
        colCtx->subdivAmount.x * sizeof(StaticLookup) * colCtx->subdivAmount.y * colCtx->subdivAmount.z;
    colCtx->lookupTbl = static_cast<StaticLookup*>(THA_AllocEndAlign(&play->state.tha, lookupSize, ~1));
    colCtx->minBounds.x = header->minBounds.x;
    colCtx->minBounds.y = header->minBounds.y;
    colCtx->minBounds.z = header->minBounds.z;
    colCtx->maxBounds.x = header->maxBounds.x;
    colCtx->maxBounds.y = header->maxBounds.y;
    colCtx->maxBounds.z = header->maxBounds.z;
    BgCheck_SetSubdivisionDimension(colCtx->minBounds.x, colCtx->subdivAmount.x, &colCtx->maxBounds.x,
                                    &colCtx->subdivLength.x, &colCtx->subdivLengthInv.x);
    BgCheck_SetSubdivisionDimension(colCtx->minBounds.y, colCtx->subdivAmount.y, &colCtx->maxBounds.y,
                                    &colCtx->subdivLength.y, &colCtx->subdivLengthInv.y);
    BgCheck_SetSubdivisionDimension(colCtx->minBounds.z, colCtx->subdivAmount.z, &colCtx->maxBounds.z,
                                    &colCtx->subdivLength.z, &colCtx->subdivLengthInv.z);
    colCtx->memSize = static_cast<u32>(lookupSize + kMaxNodes * sizeof(SSNode));
    SSNodeList_Initialize(&colCtx->polyNodes);
    SSNodeList_Alloc(play, &colCtx->polyNodes, kMaxNodes, header->numPolygons);
    BgCheck_InitializeStaticLookup(colCtx, play, colCtx->lookupTbl);
    DynaPoly_Init(play, &colCtx->dyna);
    DynaPoly_Alloc(play, &colCtx->dyna);
}

void BoMap_OnSceneInit(PlayState* play) {
    if (!sSelected || play->sceneNum != SCENE_GRAVEYARD || !EnsureLoaded()) {
        return;
    }
    AllocateCollision(play, &sMap.colHeader);
    // None of the graveyard's own actors: its graves, signs and doors aren't on the map.
    play->numSetupActors = 0;
    play->transiActorCtx.numActors = 0;
}

// Actors the rooms spawn when they load: everything but Link and Navi goes.
void BoMap_OnSpawnActors(PlayState* play) {
    if (!BoMap_Active(play)) {
        return;
    }
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        if (cat == ACTORCAT_PLAYER || cat == ACTORCAT_ITEMACTION) {
            continue;
        }
        for (Actor* a = play->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            Actor_Kill(a);
        }
    }
}

// Every frame: the loaded room draws the map.
void BoMap_Update(PlayState* play) {
    static bool sHidMinimap = false;
    if (!BoMap_Active(play)) {
        if (sHidMinimap) {
            sHidMinimap = false;
            R_MINIMAP_DISABLED = 0;
        }
        return;
    }
    // The minimap is the graveyard's.
    R_MINIMAP_DISABLED = 1;
    sHidMinimap = true;
    for (Room* room : { &play->roomCtx.curRoom, &play->roomCtx.prevRoom }) {
        if (room->segment != nullptr) {
            room->meshHeader = &sMap.meshHeader;
        }
    }
}

std::string BoMap_Status() {
    if (!BoMap_Available()) {
        return "NOT IMPORTED";
    }
    return EnsureLoaded() ? "READY" : "IMPORT FAILED";
}

static void RegisterBoMap() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneSpawnActors>(
        []() { BoMap_OnSpawnActors(gPlayState); });
}

static RegisterShipInitFunc boMapInit(RegisterBoMap, {});

} // namespace Zombies
