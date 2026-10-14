// Zombies of Time: barricades.
//
// Black Ops undead come in through boarded windows: they stop at the boards, tear
// them off one by one and only then go for the player, who can hold the buy button
// to nail boards back for points. Here each window is a pair of posts with six
// planks across them, placed around the start point. Undead spawn outside a window
// and are held on the far side of it until its boards are gone.

#include "ZombiesMode.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>
#include <cmath>

#include "soh/frame_interpolation.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "objects/gameplay_dangeon_keep/gameplay_dangeon_keep.h"
extern PlayState* gPlayState;
}

namespace Zombies {

static constexpr int kWindowCount = 4;
static constexpr float kWindowDists[] = { 520.0f, 460.0f, 600.0f, 400.0f };
static constexpr float kSpawnBehind = 130.0f; // how far outside the boards undead appear
static constexpr float kHoldDist = 35.0f;     // where undead stand while tearing
static constexpr float kTearReach = 30.0f;    // close enough to the hold point to tear
static constexpr int kTearFrames = 30;        // one board every 1.5 seconds
static constexpr int kRepairFrames = 12;      // holding the button puts a board back every 0.6 seconds
static constexpr float kRepairRange = 100.0f;
static constexpr int kFallFrames = 10;

// Boards are the small wooden crate's model squashed into planks. The crate is about
// 30 units across at the default actor scale of 0.01.
static constexpr float kCrateSize = 30.0f;
static constexpr float kCrateScale = 0.01f;
static constexpr float kWindowWidth = 80.0f;
static constexpr float kPostHeight = 85.0f;

static bool LineIsClear(PlayState* play, Vec3f a, Vec3f b) {
    a.y += 30.0f;
    b.y += 30.0f;
    Vec3f hit;
    CollisionPoly* poly;
    s32 bgId;
    return !BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, false, false, true, &bgId);
}

bool Barricades_Enabled() {
    return CVarGetInteger(CVAR_ZOMBIES("Barricades"), 1) != 0 && !GetRun().windows.empty();
}

void Barricades_Place(PlayState* play) {
    Run& run = GetRun();
    run.windows.clear();
    if (CVarGetInteger(CVAR_ZOMBIES("Barricades"), 1) == 0) {
        return;
    }

    for (int i = 0; i < kWindowCount; i++) {
        // Windows go on the diagonals between the station fan, and try a few distances
        // and angles until one has floor and a clear line to the start point.
        for (int attempt = 0; attempt < 12; attempt++) {
            s16 jitter = static_cast<s16>(((attempt / 4) - 1) * 0x0C00);
            s16 angle = static_cast<s16>(run.originYaw + 0x2000 + i * 0x4000 + jitter);
            float dist = kWindowDists[attempt % 4];
            float x = run.origin.x + Math_SinS(angle) * dist;
            float z = run.origin.z + Math_CosS(angle) * dist;
            float y;
            if (!FloorAt(play, x, z, run.origin.y, &y)) {
                continue;
            }
            Vec3f pos = { x, y, z };
            if (!LineIsClear(play, pos, run.origin)) {
                continue;
            }

            s16 yaw = Math_Vec3f_Yaw(&pos, &run.origin);
            Vec3f spawn = { x - Math_SinS(yaw) * kSpawnBehind, y, z - Math_CosS(yaw) * kSpawnBehind };
            if (!FloorAt(play, spawn.x, spawn.z, y, &spawn.y) || !LineIsClear(play, pos, spawn)) {
                continue;
            }

            Window window;
            window.pos = pos;
            window.yaw = yaw;
            window.spawnPos = spawn;
            run.windows.push_back(window);
            break;
        }
    }

    // One window on its own just funnels the horde; spawn freely instead.
    if (run.windows.size() < 2) {
        run.windows.clear();
    }
}

int Barricades_PickWindow() {
    Run& run = GetRun();
    if (!Barricades_Enabled()) {
        return -1;
    }
    return std::min(static_cast<int>(Rand_ZeroOne() * run.windows.size()), static_cast<int>(run.windows.size()) - 1);
}

bool Barricades_HoldEnemy(TrackedEnemy& enemy, float speed) {
    Run& run = GetRun();
    if (enemy.window < 0 || enemy.window >= static_cast<int>(run.windows.size())) {
        enemy.window = -1;
        return false;
    }
    Window& w = run.windows[enemy.window];
    if (w.boards == 0) {
        // The way in is open.
        enemy.window = -1;
        return false;
    }

    Actor* actor = enemy.actor;
    float inX = Math_SinS(w.yaw);
    float inZ = Math_CosS(w.yaw);

    // Walk up to the boards from outside.
    float targetX = w.pos.x - inX * kHoldDist;
    float targetZ = w.pos.z - inZ * kHoldDist;
    float dx = targetX - actor->world.pos.x;
    float dz = targetZ - actor->world.pos.z;
    float len = sqrtf(dx * dx + dz * dz);
    if (len > 4.0f && actor->freezeTimer == 0) {
        float step = std::min(speed, len);
        actor->world.pos.x += dx / len * step;
        actor->world.pos.z += dz / len * step;
    }

    // Never past the boards, whatever the enemy's own AI wants.
    float depth = (actor->world.pos.x - w.pos.x) * inX + (actor->world.pos.z - w.pos.z) * inZ;
    float limit = -kHoldDist * 0.5f;
    if (depth > limit) {
        actor->world.pos.x -= inX * (depth - limit);
        actor->world.pos.z -= inZ * (depth - limit);
    }
    actor->shape.rot.y = w.yaw;

    if (len < kTearReach) {
        w.beingTorn = true;
    }
    return true;
}

void Barricades_RepairAll() {
    for (Window& w : GetRun().windows) {
        w.boards = kBoardsPerWindow;
        w.repairTimer = 0;
    }
}

bool Barricades_AnyBroken() {
    for (const Window& w : GetRun().windows) {
        if (w.boards < kBoardsPerWindow) {
            return true;
        }
    }
    return false;
}

void Barricades_Update(PlayState* play, Player* player) {
    Run& run = GetRun();

    for (Window& w : run.windows) {
        if (w.fallingBoard > 0) {
            w.fallingBoard--;
        }
        if (w.beingTorn && w.boards > 0) {
            if (--w.tearTimer <= 0) {
                w.boards--;
                w.fallingBoard = kFallFrames;
                w.tearTimer = kTearFrames;
                SoundSource_PlaySfxAtFixedWorldPos(play, &w.pos, 20, NA_SE_EV_WOODBOX_BREAK);
            }
        } else {
            w.tearTimer = kTearFrames;
        }
        w.beingTorn = false;
    }

    // Repairs: the nearest damaged window in reach, unless a station prompt is already up.
    Window* nearest = nullptr;
    float nearestDist = kRepairRange;
    for (Window& w : run.windows) {
        float dist = Math_Vec3f_DistXZ(&w.pos, &player->actor.world.pos);
        if (w.boards < kBoardsPerWindow && dist < nearestDist && fabsf(w.pos.y - player->actor.world.pos.y) < 60.0f) {
            nearest = &w;
            nearestDist = dist;
        }
    }
    for (Window& w : run.windows) {
        if (&w != nearest) {
            w.repairTimer = 0;
        }
    }
    if (nearest == nullptr || !run.prompt.empty()) {
        return;
    }

    run.prompt = std::string("HOLD ") + BuyButtonName() + ": REPAIR BARRIER";
    if (!BuyHeld(play) || Player_InCsMode(play)) {
        nearest->repairTimer = 0;
        return;
    }
    if (++nearest->repairTimer >= kRepairFrames) {
        nearest->repairTimer = 0;
        nearest->boards++;
        int points = RepairPoints(run.repairPointsThisRound);
        if (points > 0) {
            run.repairPointsThisRound += points;
            AwardPoints(0, points);
        }
        SoundSource_PlaySfxAtFixedWorldPos(play, &nearest->pos, 20, NA_SE_EV_WOOD_HIT);
    }
}

} // namespace Zombies

// ---------------------------------------------------------------------------
// Drawing, from SoH's world draw hook. Kept at file scope with C linkage for the
// same reason as the HUD helper: the display list macros expect it.

static void DrawCrate(PlayState* play, const Vec3f& base, s16 yaw, float x, float y, float z, float roll, float width,
                      float height, float depth) {
    Matrix_Translate(base.x, base.y, base.z, MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(yaw), MTXMODE_APPLY);
    Matrix_Translate(x, y, z, MTXMODE_APPLY);
    Matrix_RotateZ(roll, MTXMODE_APPLY);
    // The crate sits on its base, so drop half the plank height to centre it on y.
    Matrix_Translate(0.0f, -height * 0.5f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(width / Zombies::kCrateSize * Zombies::kCrateScale,
                 height / Zombies::kCrateSize * Zombies::kCrateScale,
                 depth / Zombies::kCrateSize * Zombies::kCrateScale, MTXMODE_APPLY);
    Gfx_DrawDListOpa(play, (Gfx*)gSmallWoodenBoxDL);
}

extern "C" void ZombiesBarricades_Draw(PlayState* play) {
    using namespace Zombies;
    const Run& run = GetRun();

    for (size_t i = 0; i < run.windows.size(); i++) {
        const Window& w = run.windows[i];
        FrameInterpolation_RecordOpenChild(&w, 0);

        // Posts either side, so an empty window still reads as a way in.
        float half = kWindowWidth * 0.5f + 4.0f;
        DrawCrate(play, w.pos, w.yaw, -half, kPostHeight * 0.5f, 0.0f, 0.0f, 7.0f, kPostHeight, 7.0f);
        DrawCrate(play, w.pos, w.yaw, half, kPostHeight * 0.5f, 0.0f, 0.0f, 7.0f, kPostHeight, 7.0f);

        for (int b = 0; b < kBoardsPerWindow; b++) {
            float y = 16.0f + b * 11.0f;
            float roll = (b % 2 == 0) ? 0.12f : -0.1f;
            if (b < w.boards) {
                DrawCrate(play, w.pos, w.yaw, 0.0f, y, -4.0f, roll, kWindowWidth + 6.0f, 8.0f, 3.0f);
            } else if (b == w.boards && w.fallingBoard > 0) {
                // The board just torn off tumbles outward.
                float t = 1.0f - static_cast<float>(w.fallingBoard) / kFallFrames;
                DrawCrate(play, w.pos, w.yaw, 0.0f, y - t * y, -4.0f - t * 30.0f, roll + t * 1.4f, kWindowWidth + 6.0f,
                          8.0f, 3.0f);
            }
        }

        FrameInterpolation_RecordCloseChild();
    }
}

namespace Zombies {

void Barricades_Draw() {
    if (gPlayState == nullptr || !IsRunActive() || GetRun().windows.empty()) {
        return;
    }
    ZombiesBarricades_Draw(gPlayState);
}

} // namespace Zombies
