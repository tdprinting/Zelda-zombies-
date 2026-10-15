// Zombies of Time: keeps Link inside the map during a run.
//
// Scene exits are floor or wall polygons that load another area the moment Link touches
// them. A knockback from an undead can throw him onto one, which ends the run. At the start
// of a run, the scene's exit polygons are collected. Every frame, Link is held back from
// them by a margin wider than he can move in one frame.

#include "ZombiesMode.h"

#include <algorithm>
#include <vector>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
}

namespace Zombies {

static constexpr float kMargin = 45.0f; // wider than Link's fastest knockback per frame

struct ExitBox {
    float minX, maxX, minY, maxY, minZ, maxZ;
};

static std::vector<ExitBox> sExits;
static Vec3f sLastSafe = {};
static bool sHaveSafe = false;

void Bounds_Scan(PlayState* play) {
    sExits.clear();
    sHaveSafe = false;
    CollisionHeader* header = play->colCtx.colHeader;
    if (header == nullptr || header->polyList == nullptr || header->vtxList == nullptr) {
        return;
    }
    for (u16 i = 0; i < header->numPolygons; i++) {
        CollisionPoly* poly = &header->polyList[i];
        if (SurfaceType_GetSceneExitIndex(&play->colCtx, poly, BGCHECK_SCENE) == 0) {
            continue;
        }
        const Vec3s& a = header->vtxList[COLPOLY_VTX_A(poly)];
        const Vec3s& b = header->vtxList[COLPOLY_VTX_B(poly)];
        const Vec3s& c = header->vtxList[poly->vIC];
        ExitBox box;
        box.minX = std::min({ a.x, b.x, c.x }) - kMargin;
        box.maxX = std::max({ a.x, b.x, c.x }) + kMargin;
        box.minY = std::min({ a.y, b.y, c.y }) - 80.0f;
        box.maxY = std::max({ a.y, b.y, c.y }) + 120.0f;
        box.minZ = std::min({ a.z, b.z, c.z }) - kMargin;
        box.maxZ = std::max({ a.z, b.z, c.z }) + kMargin;
        sExits.push_back(box);
    }
}

static bool NearExit(const Vec3f& pos) {
    for (const ExitBox& box : sExits) {
        if (pos.x >= box.minX && pos.x <= box.maxX && pos.z >= box.minZ && pos.z <= box.maxZ && pos.y >= box.minY &&
            pos.y <= box.maxY) {
            return true;
        }
    }
    return false;
}

bool Bounds_NearExit(const Vec3f& pos) {
    return NearExit(pos);
}

void Bounds_Update(PlayState* play, Player* player) {
    if (sExits.empty() || play->transitionTrigger != TRANS_TRIGGER_OFF) {
        return;
    }
    Vec3f& pos = player->actor.world.pos;
    if (!NearExit(pos)) {
        sLastSafe = pos;
        sHaveSafe = true;
        return;
    }
    if (!sHaveSafe) {
        return; // the run started on an exit's edge; let him walk off it
    }
    pos.x = sLastSafe.x;
    pos.z = sLastSafe.z;
    player->linearVelocity = 0.0f;
    player->actor.speedXZ = 0.0f;
}

} // namespace Zombies
