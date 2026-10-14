// Zombies of Time: authored arenas.
//
// Anywhere else the mode places its stations and windows around Link. A scene with
// a layout here gets fixed spots instead, split into zones the way a Black Ops map
// is split into rooms: Link starts in one zone and pays to clear the debris into
// the next, which opens its stations and lets undead in through its windows.
//
// Every spot is checked against the scene's collision when the run starts. A spot
// that doesn't land on usable floor is skipped (or falls back to the placement
// around Link), so a layout that is slightly off still plays.

#include "ZombiesMode.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <cmath>

#include "soh/frame_interpolation.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace Zombies {

static constexpr float kResolveRange = 250.0f;
static constexpr float kDebrisReach = 110.0f;

// Kakariko Graveyard, after the flow of Black Ops' first map: a small start area
// with windows and a wall buy, debris into a bigger middle room with the Mystery
// Chest's second spot, and more debris to the far end, where the power and
// Pack-a-Punch are. Spots are rough, from the positions of the graveyard's signs,
// graves and grass in SoH's randomizer tables; they need a walk-through in game.
static const ArenaLayout sGraveyard = {
    SCENE_GRAVEYARD,
    "KAKARIKO GRAVEYARD",
    {
        {
            "THE GATE",
            0,
            { 0.0f, 0.0f },
            { -850.0f, 350.0f },
            { { -1100.0f, 120.0f }, { -1000.0f, 620.0f }, { -700.0f, 50.0f } },
            {
                { StationType::Armory, { -950.0f, 300.0f } },
                { StationType::FairyFountain, { -1000.0f, 470.0f } },
                { StationType::MysteryChest, { -780.0f, 420.0f } },
            },
            { { -780.0f, 420.0f } },
        },
        {
            "THE GRAVES",
            1000,
            { -560.0f, 300.0f },
            { -150.0f, 250.0f },
            { { -300.0f, -150.0f }, { 0.0f, 650.0f }, { 150.0f, -150.0f } },
            {
                { StationType::Juggernog, { -300.0f, 120.0f } },
                { StationType::DoubleTap, { 0.0f, 420.0f } },
            },
            { { -50.0f, 50.0f } },
        },
        {
            "THE ROYAL TOMB",
            1000,
            { 330.0f, 100.0f },
            { 600.0f, 80.0f },
            { { 850.0f, -250.0f }, { 850.0f, 420.0f } },
            {
                { StationType::Power, { 520.0f, -60.0f } },
                { StationType::PackAPunch, { 560.0f, 220.0f } },
                { StationType::StaminUp, { 450.0f, 300.0f } },
                { StationType::FairyRevive, { 450.0f, -150.0f } },
            },
            { { 700.0f, 350.0f } },
        },
    },
};

const ArenaLayout* Arena_ForScene(s16 sceneNum) {
    if (sceneNum == SCENE_GRAVEYARD) {
        return &sGraveyard;
    }
    return nullptr;
}

bool Arena_Resolve(PlayState* play, LayoutSpot spot, float refY, Vec3f* out) {
    Vec3f probe = { spot.x, refY + kResolveRange, spot.z };
    CollisionPoly* poly;
    s32 bgId;
    f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if (floorY <= BGCHECK_Y_MIN + 1.0f || fabsf(floorY - refY) > kResolveRange) {
        return false;
    }
    WaterBox* waterBox;
    f32 waterY;
    if (WaterBox_GetSurface1(play, &play->colCtx, spot.x, spot.z, &waterY, &waterBox) && waterY > floorY + 10.0f) {
        return false;
    }
    *out = { spot.x, floorY, spot.z };
    return true;
}

bool Arena_ZoneOpen(int zone) {
    return zone < 0 || zone >= 32 || (GetRun().openZones & (1u << zone)) != 0;
}

static void OpenZone(PlayState* play, int zone) {
    Run& run = GetRun();
    run.openZones |= 1u << zone;
    for (size_t i = 0; i < run.debris.size(); i++) {
        if (run.debris[i].zone == zone) {
            run.debris.erase(run.debris.begin() + i);
            break;
        }
    }
    Stations_OpenZone(play, zone);
}

void Arena_Start(PlayState* play) {
    Run& run = GetRun();
    run.openZones = 1;
    run.debris.clear();
    run.layout = CVarGetInteger(CVAR_ZOMBIES("Arenas"), 1) ? Arena_ForScene(play->sceneNum) : nullptr;
    if (run.layout == nullptr) {
        return;
    }

    // Everyone starts in the first zone, like Black Ops' spawn room, wherever the map
    // loaded Link (often right on the scene exit he came in by).
    Vec3f start;
    if (!run.layout->zones.empty() && Arena_Resolve(play, run.layout->zones[0].centre, run.origin.y, &start)) {
        Player* player = GET_PLAYER(play);
        player->actor.world.pos = start;
        player->actor.prevPos = start;
        player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = run.originYaw;
        run.origin = start;
    }

    for (size_t z = 1; z < run.layout->zones.size() && z < 32; z++) {
        const LayoutZone& zone = run.layout->zones[z];
        Vec3f pos;
        Vec3f centre;
        if (Arena_Resolve(play, zone.debris, run.origin.y, &pos) &&
            Arena_Resolve(play, zone.centre, run.origin.y, &centre)) {
            run.debris.push_back({ static_cast<int>(z), pos, Math_Vec3f_Yaw(&pos, &centre) });
        } else {
            // Nowhere to put the debris: start with the zone open rather than locked away.
            run.openZones |= 1u << z;
        }
    }
}

void Arena_Update(PlayState* play, Player* player) {
    Run& run = GetRun();
    if (run.layout == nullptr || !run.prompt.empty()) {
        return;
    }

    Debris* nearest = nullptr;
    float nearestDist = kDebrisReach;
    for (Debris& d : run.debris) {
        float dist = Math_Vec3f_DistXZ(&d.pos, &player->actor.world.pos);
        if (dist < nearestDist && fabsf(d.pos.y - player->actor.world.pos.y) < 80.0f) {
            nearest = &d;
            nearestDist = dist;
        }
    }
    if (nearest == nullptr) {
        return;
    }

    const LayoutZone& zone = run.layout->zones[nearest->zone];
    run.prompt = std::string(BuyButtonName()) + ": CLEAR DEBRIS [" + std::to_string(zone.cost) + "]";
    if (!BuyPressed(play) || Player_InCsMode(play)) {
        return;
    }
    if (!SpendPoints(0, zone.cost)) {
        Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
        return;
    }
    Vec3f pos = nearest->pos;
    SoundSource_PlaySfxAtFixedWorldPos(play, &pos, 30, NA_SE_EV_WOODBOX_BREAK);
    Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
    Announce(std::string(zone.name) + " OPEN", 3);
    OpenZone(play, nearest->zone);
}

} // namespace Zombies

// A heap of crates and planks across the way.
extern "C" void Zombies_DrawDebrisPile(PlayState* play, const Vec3f& pos, s16 yaw) {
    // y is the centre of each piece.
    ZombiesDraw_Crate(play, pos, yaw, -40.0f, 15.0f, 0.0f, 0.0f, 32.0f, 30.0f, 32.0f);
    ZombiesDraw_Crate(play, pos, yaw, 0.0f, 17.0f, 8.0f, 0.0f, 36.0f, 34.0f, 34.0f);
    ZombiesDraw_Crate(play, pos, yaw, 42.0f, 14.0f, -4.0f, 0.0f, 30.0f, 28.0f, 30.0f);
    ZombiesDraw_Crate(play, pos, yaw, -20.0f, 46.0f, 4.0f, 0.15f, 30.0f, 28.0f, 30.0f);
    ZombiesDraw_Crate(play, pos, yaw, 24.0f, 44.0f, 0.0f, -0.2f, 28.0f, 26.0f, 28.0f);
    ZombiesDraw_Crate(play, pos, yaw, 0.0f, 60.0f, -10.0f, 0.5f, 110.0f, 7.0f, 4.0f);
    ZombiesDraw_Crate(play, pos, yaw, 0.0f, 48.0f, 18.0f, -0.4f, 100.0f, 7.0f, 4.0f);
}

namespace Zombies {

void Arena_DrawDebris(PlayState* play) {
    for (const Debris& d : GetRun().debris) {
        FrameInterpolation_RecordOpenChild(&d, 0);
        Zombies_DrawDebrisPile(play, d.pos, d.yaw);
        FrameInterpolation_RecordCloseChild();
    }
}

} // namespace Zombies
