// Zombies of Time: round-based survival for Ship of Harkinian.
//
// This file owns the run: starting and ending it, the round state machine,
// spawning and tracking undead, points, power-ups and the save snapshot.
// Stations, the HUD and the menu live in the other Zombies*.cpp files.

#include "ZombiesMode.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <ship/Context.h>
#include <ship/window/Window.h>

#include <algorithm>
#include <cmath>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace Zombies {

static constexpr int kFps = 20; // OoT game logic runs at 20 updates per second

static constexpr float kSpawnMinDist = 380.0f;
static constexpr float kSpawnMaxDist = 750.0f;
static constexpr float kSpawnMaxHeightDiff = 120.0f;
// Horde Pull stops this close to Link so each enemy's own attack takes over.
static constexpr float kPullStopDistWalk = 140.0f;
static constexpr float kPullStopDistFast = 80.0f;
static constexpr float kLostDist = 2500.0f;
// Black Ops respawns a zombie that can't reach the player. Ours gets 15 seconds to
// close 50 units of distance before it goes back into the round's pool.
static constexpr int kStuckFrames = 15 * 20;
static constexpr float kStuckProgress = 50.0f;
static constexpr float kStuckMinDist = 250.0f;

static Run sRun;

Run& GetRun() {
    return sRun;
}

bool IsRunActive() {
    return sRun.phase != Phase::Inactive;
}

bool PowerUpActive(PowerUp p) {
    return sRun.powerUpFrames[static_cast<int>(p)] > 0;
}

static bool CVarOn(const char* name, int def) {
    return CVarGetInteger(name, def) != 0;
}

void Announce(const std::string& text, int seconds) {
    sRun.announcement.text = text;
    sRun.announcement.framesLeft = seconds * kFps;
}

void AwardPoints(int playerIndex, int amount) {
    if (PowerUpActive(PowerUp::DoublePoints)) {
        amount *= 2;
    }
    PlayerState& ps = sRun.players[playerIndex];
    ps.points += amount;
    ps.totalEarned += amount;
    sRun.popups.push_back({ amount, kFps });
    if (sRun.popups.size() > 4) {
        sRun.popups.erase(sRun.popups.begin());
    }
}

bool SpendPoints(int playerIndex, int amount) {
    PlayerState& ps = sRun.players[playerIndex];
    if (ps.points < amount) {
        return false;
    }
    ps.points -= amount;
    sRun.popups.push_back({ -amount, kFps });
    return true;
}

// ---------------------------------------------------------------------------
// Save snapshot: purchases go into the real file, so by default a run puts the
// inventory back the way it found it.

struct SaveSnapshot {
    bool valid = false;
    ItemEquips equips;
    Inventory inventory;
    s16 healthCapacity;
    s16 health;
    s8 magicLevel;
    s8 magic;
    s16 magicCapacity;
    u8 isMagicAcquired;
    u8 isDoubleMagicAcquired;
    u8 isDoubleDefenseAcquired;
    u8 bgsFlag;
    s16 rupees;
    u16 dayTime;
};

static SaveSnapshot sSnapshot;

static void TakeSnapshot() {
    sSnapshot.valid = true;
    sSnapshot.equips = gSaveContext.equips;
    sSnapshot.inventory = gSaveContext.inventory;
    sSnapshot.healthCapacity = gSaveContext.healthCapacity;
    sSnapshot.health = gSaveContext.health;
    sSnapshot.magicLevel = gSaveContext.magicLevel;
    sSnapshot.magic = gSaveContext.magic;
    sSnapshot.magicCapacity = gSaveContext.magicCapacity;
    sSnapshot.isMagicAcquired = gSaveContext.isMagicAcquired;
    sSnapshot.isDoubleMagicAcquired = gSaveContext.isDoubleMagicAcquired;
    sSnapshot.isDoubleDefenseAcquired = gSaveContext.isDoubleDefenseAcquired;
    sSnapshot.bgsFlag = gSaveContext.bgsFlag;
    sSnapshot.rupees = gSaveContext.rupees;
    sSnapshot.dayTime = gSaveContext.dayTime;
}

static void RestoreSnapshot(bool linkIsDead) {
    if (!sSnapshot.valid) {
        return;
    }
    sSnapshot.valid = false;

    gSaveContext.equips = sSnapshot.equips;
    gSaveContext.inventory = sSnapshot.inventory;
    gSaveContext.healthCapacity = sSnapshot.healthCapacity;
    // During a death the game over sequence owns health; refilling it here would revive Link.
    if (!linkIsDead) {
        gSaveContext.health = std::min(sSnapshot.health, sSnapshot.healthCapacity);
    }
    gSaveContext.magicLevel = sSnapshot.magicLevel;
    gSaveContext.magic = sSnapshot.magic;
    gSaveContext.magicCapacity = sSnapshot.magicCapacity;
    gSaveContext.isMagicAcquired = sSnapshot.isMagicAcquired;
    gSaveContext.isDoubleMagicAcquired = sSnapshot.isDoubleMagicAcquired;
    gSaveContext.isDoubleDefenseAcquired = sSnapshot.isDoubleDefenseAcquired;
    gSaveContext.bgsFlag = sSnapshot.bgsFlag;
    gSaveContext.rupees = sSnapshot.rupees;
    gSaveContext.dayTime = sSnapshot.dayTime;

    if (gPlayState != nullptr) {
        for (u16 button = 0; button < 4; button++) {
            Interface_LoadItemIcon1(gPlayState, button);
        }
        Player_SetEquipmentData(gPlayState, GET_PLAYER(gPlayState));
    }
}

// ---------------------------------------------------------------------------
// Undead

struct EnemySpec {
    s16 actorId;
    s16 params;
    int baseHp;
};

static EnemySpec GetEnemySpec(EnemyKind kind) {
    // Redead/Gibdo high byte is the switch flag set on death; 0x3F is a temp flag
    // so a kill never opens anything in the arena. Bit 0x80 of it disables the scream.
    s16 rdFlags = CVarOn(CVAR_ZOMBIES("RedeadScream"), 1) ? 0x3F : 0xBF;
    switch (kind) {
        case EnemyKind::Stalchild:
            return { ACTOR_EN_SKB, 1, 2 };
        case EnemyKind::BigStalchild:
            return { ACTOR_EN_SKB, 8, 4 };
        case EnemyKind::Redead:
            return { ACTOR_EN_RD, static_cast<s16>((rdFlags << 8) | 0x01), 8 };
        case EnemyKind::Gibdo:
            return { ACTOR_EN_RD, static_cast<s16>((rdFlags << 8) | 0xFE), 8 };
        case EnemyKind::WhiteWolfos:
        default:
            // High byte 0xFF: no switch flag, so the Wolfos never despawns on a flag.
            return { ACTOR_EN_WF, static_cast<s16>(0xFF01), 8 };
    }
}

bool FloorAt(PlayState* play, float x, float z, float refY, float* outY) {
    Vec3f probe = { x, refY + 200.0f, z };
    CollisionPoly* poly;
    s32 bgId;
    f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if (floorY <= BGCHECK_Y_MIN + 1.0f || fabsf(floorY - refY) > kSpawnMaxHeightDiff) {
        return false;
    }
    WaterBox* waterBox;
    f32 waterY;
    if (WaterBox_GetSurface1(play, &play->colCtx, x, z, &waterY, &waterBox) && waterY > floorY + 10.0f) {
        return false;
    }
    *outY = floorY;
    return true;
}

static bool FindFloorNear(PlayState* play, const Vec3f& center, float minDist, float maxDist, float refY, Vec3f* out) {
    for (int attempt = 0; attempt < 16; attempt++) {
        s16 angle = static_cast<s16>(Rand_ZeroOne() * 0x10000);
        float dist = minDist + Rand_ZeroOne() * (maxDist - minDist);
        float x = center.x + Math_SinS(angle) * dist;
        float z = center.z + Math_CosS(angle) * dist;
        float y;
        if (FloorAt(play, x, z, refY, &y)) {
            *out = { x, y, z };
            return true;
        }
    }
    return false;
}

static bool SpawnEnemy(PlayState* play, Player* player, EnemyKind kind) {
    Vec3f pos;
    // Undead come in through the barricades; Wolfos, like hellhounds, appear anywhere.
    int window = kind == EnemyKind::WhiteWolfos ? -1 : Barricades_PickWindow();
    if (window >= 0) {
        const Window& w = sRun.windows[window];
        float side = (Rand_ZeroOne() - 0.5f) * 80.0f;
        pos = { w.spawnPos.x + Math_CosS(w.yaw) * side, w.spawnPos.y, w.spawnPos.z - Math_SinS(w.yaw) * side };
        if (!FloorAt(play, pos.x, pos.z, w.spawnPos.y, &pos.y)) {
            pos = w.spawnPos;
        }
    } else if (!FindFloorNear(play, player->actor.world.pos, kSpawnMinDist, kSpawnMaxDist, player->actor.world.pos.y,
                              &pos)) {
        return false;
    }

    EnemySpec spec = GetEnemySpec(kind);
    s16 yaw = Math_Vec3f_Yaw(&pos, &player->actor.world.pos);

    // Actor_Spawn refuses enemies in a room whose clear flag is set.
    Flags_UnsetClear(play, play->roomCtx.curRoom.num);

    Actor* actor = Actor_Spawn(&play->actorCtx, play, spec.actorId, pos.x, pos.y, pos.z, 0, yaw, 0, spec.params);
    if (actor == nullptr) {
        return false;
    }
    // Room -1 keeps the undead alive when Link walks into another room of the arena.
    actor->room = -1;
    u8 hp = ScaleHealth(spec.baseHp, sRun.round, CVarGetInteger(CVAR_ZOMBIES("HealthScaling"), 100));
    actor->colChkInfo.health = hp;

    // Wolfos are already fast; undead get a Black Ops move tier for the round.
    MoveTier tier = kind == EnemyKind::WhiteWolfos ? MoveTier::Walk : PickMoveTier(sRun.round, Rand_ZeroOne());
    sRun.enemies.push_back({ actor, kind, tier, static_cast<s16>(hp), actor->world.pos, false,
                             Math_Vec3f_DistXZ(&pos, &player->actor.world.pos), 0, window });
    // Dust where the undead claws out of the ground.
    Actor_SpawnFloorDustRing(play, actor, &pos, 30.0f, 7, 4.0f, 60, 6, 0);
    return true;
}

static void ForgetEnemy(size_t index, bool escaped) {
    if (escaped && sRun.phase == Phase::Active) {
        // An undead that despawned or got lost goes back into the round's pool.
        sRun.toSpawn++;
    }
    sRun.enemies.erase(sRun.enemies.begin() + index);
}

static KillType CurrentKillType(Player* player) {
    if (player->meleeWeaponState != 0) {
        return KillType::Melee;
    }
    switch (player->heldItemAction) {
        case PLAYER_IA_BOW:
        case PLAYER_IA_BOW_FIRE:
        case PLAYER_IA_BOW_ICE:
        case PLAYER_IA_BOW_LIGHT:
        case PLAYER_IA_SLINGSHOT:
        case PLAYER_IA_HOOKSHOT:
        case PLAYER_IA_LONGSHOT:
            return KillType::Ranged;
        default:
            return KillType::Other;
    }
}

static void SpawnPowerUp(PlayState* play, const Vec3f& pos, PowerUp type);

static void OnEnemyKilled(PlayState* play, Player* player, TrackedEnemy& enemy) {
    enemy.killCounted = true;
    PlayerState& ps = sRun.players[0];
    ps.kills++;
    AwardPoints(0, PointsForKill(CurrentKillType(player)));
    sRun.lastKillPos = enemy.actor->world.pos;

    if (sRun.dropTracker.OnKill(ps.totalEarned, Rand_ZeroOne())) {
        SpawnPowerUp(play, enemy.actor->world.pos, PickPowerUp(Rand_ZeroOne(), true, Barricades_AnyBroken()));
    }
}

static void UpdateEnemies(PlayState* play, Player* player) {
    bool hordePull = CVarOn(CVAR_ZOMBIES("HordePull"), 1);
    float slow = sRun.stormFrames > 0 ? 0.5f : 1.0f;

    for (size_t i = 0; i < sRun.enemies.size();) {
        TrackedEnemy& enemy = sRun.enemies[i];
        Actor* actor = enemy.actor;

        if (actor->update == nullptr) {
            // Actor_Kill ran: either a finished death animation or a despawn (Stalchild burrowing).
            ForgetEnemy(i, !enemy.killCounted);
            continue;
        }

        if (!enemy.killCounted) {
            if (PowerUpActive(PowerUp::InstaKill) && actor->colChkInfo.health > 1) {
                actor->colChkInfo.health = 1;
                enemy.lastHealth = 1;
            }

            s16 health = actor->colChkInfo.health;
            if (health < enemy.lastHealth) {
                s16 damage = enemy.lastHealth - health;
                // Double Tap and a Pack-a-Punched blade each land the hit again. The extra damage
                // never finishes an enemy off, because its own death check already ran this frame.
                int repeats = (sRun.players[0].perks[PERK_DOUBLE_TAP] ? 1 : 0) +
                              (Stations_PapSword() && player->meleeWeaponState != 0 ? 1 : 0);
                if (repeats > 0 && health > 1) {
                    health = std::max<s16>(1, health - damage * repeats);
                    actor->colChkInfo.health = health;
                }
                AwardPoints(0, kPointsPerHit);
                enemy.lastHealth = health;
            }
            if (health == 0) {
                OnEnemyKilled(play, player, enemy);
            }
        }

        if (!enemy.killCounted) {
            float dist = actor->xzDistToPlayer;

            // Undead outside a barricade stay there, tearing boards, until the window is open.
            if (enemy.window >= 0 && Barricades_HoldEnemy(enemy, PullSpeed(enemy.tier) * slow)) {
                enemy.closestDist = dist;
                enemy.stuckFrames = 0;
                if (dist > kLostDist) {
                    Actor_Kill(actor);
                    ForgetEnemy(i, true);
                    continue;
                }
                actor->home.pos = actor->world.pos;
                enemy.lastPos = actor->world.pos;
                i++;
                continue;
            }

            if (dist < enemy.closestDist - kStuckProgress || dist < kStuckMinDist) {
                enemy.closestDist = std::min(enemy.closestDist, dist);
                enemy.stuckFrames = 0;
            } else if (actor->freezeTimer == 0) {
                enemy.stuckFrames++;
            }
            if (dist > kLostDist || enemy.stuckFrames > kStuckFrames) {
                Actor_Kill(actor);
                ForgetEnemy(i, true);
                continue;
            }

            // Song of Storms: undead move at half speed in the rain.
            if (slow < 1.0f) {
                actor->world.pos.x = enemy.lastPos.x + (actor->world.pos.x - enemy.lastPos.x) * slow;
                actor->world.pos.z = enemy.lastPos.z + (actor->world.pos.z - enemy.lastPos.z) * slow;
            }

            // Horde Pull: OoT enemies idle until Link is close. Zombies always know where he is,
            // and walk, run or sprint at him depending on the round.
            float stopDist = enemy.tier == MoveTier::Walk ? kPullStopDistWalk : kPullStopDistFast;
            if (hordePull && dist > stopDist && actor->freezeTimer == 0) {
                s16 yaw = actor->yawTowardsPlayer;
                float speed = PullSpeed(enemy.tier) * slow;
                actor->world.pos.x += Math_SinS(yaw) * speed;
                actor->world.pos.z += Math_CosS(yaw) * speed;
                actor->shape.rot.y = yaw;
            }

            // Keep home under the enemy so it never walks back or despawns for straying from home.
            actor->home.pos = actor->world.pos;
        }

        enemy.lastPos = actor->world.pos;
        i++;
    }
}

static void KillAllEnemies(PlayState* play, bool explode) {
    for (TrackedEnemy& enemy : sRun.enemies) {
        if (enemy.actor->update == nullptr) {
            continue;
        }
        if (explode) {
            Vec3f zero = { 0, 0, 0 };
            EffectSsBomb2_SpawnLayered(play, &enemy.actor->world.pos, &zero, &zero, 100, 19);
            enemy.killCounted = true;
            sRun.players[0].kills++;
        }
        Actor_Kill(enemy.actor);
    }
    if (!explode) {
        sRun.enemies.clear();
    }
}

// ---------------------------------------------------------------------------
// Power-ups

static s16 PowerUpDropParams(PowerUp type) {
    switch (type) {
        case PowerUp::MaxAmmo:
            return ITEM00_MAGIC_LARGE;
        case PowerUp::InstaKill:
            return ITEM00_RUPEE_RED;
        case PowerUp::DoublePoints:
            return ITEM00_RUPEE_ORANGE;
        case PowerUp::Nuke:
            return ITEM00_BOMBS_A;
        case PowerUp::Carpenter:
            return ITEM00_STICK; // a plank of wood
        case PowerUp::FireSale:
        default:
            return ITEM00_RUPEE_PURPLE;
    }
}

static void SpawnPowerUp(PlayState* play, const Vec3f& pos, PowerUp type) {
    Actor* drop = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_ITEM00, pos.x, pos.y + 30.0f, pos.z, 0, 0, 0,
                              PowerUpDropParams(type));
    if (drop == nullptr) {
        return;
    }
    drop->room = -1;
    sRun.drops.push_back({ drop, drop->draw, type, kPowerUpSeconds * kFps });
    Sfx_PlaySfxCentered(NA_SE_SY_TRE_BOX_APPEAR);
}

void RefillAmmo() {
    if (INV_CONTENT(ITEM_STICK) == ITEM_STICK) {
        AMMO(ITEM_STICK) = CUR_CAPACITY(UPG_STICKS);
    }
    if (INV_CONTENT(ITEM_NUT) == ITEM_NUT) {
        AMMO(ITEM_NUT) = CUR_CAPACITY(UPG_NUTS);
    }
    if (INV_CONTENT(ITEM_SLINGSHOT) == ITEM_SLINGSHOT) {
        AMMO(ITEM_SLINGSHOT) = CUR_CAPACITY(UPG_BULLET_BAG);
    }
    if (INV_CONTENT(ITEM_BOW) == ITEM_BOW) {
        AMMO(ITEM_BOW) = CUR_CAPACITY(UPG_QUIVER);
    }
    if (INV_CONTENT(ITEM_BOMB) == ITEM_BOMB) {
        AMMO(ITEM_BOMB) = CUR_CAPACITY(UPG_BOMB_BAG);
    }
    if (INV_CONTENT(ITEM_BOMBCHU) == ITEM_BOMBCHU) {
        AMMO(ITEM_BOMBCHU) = 50;
    }
    if (gSaveContext.isMagicAcquired && gPlayState != nullptr) {
        Magic_Fill(gPlayState);
    }
}

static void ActivatePowerUp(PlayState* play, PowerUp type) {
    Announce(PowerUpName(type));
    Sfx_PlaySfxCentered(NA_SE_SY_GET_ITEM);

    switch (type) {
        case PowerUp::MaxAmmo:
            RefillAmmo();
            break;
        case PowerUp::Nuke:
            KillAllEnemies(play, true);
            Sfx_PlaySfxCentered(NA_SE_IT_BOMB_EXPLOSION);
            AwardPoints(0, kPointsNuke);
            break;
        case PowerUp::Carpenter:
            Barricades_RepairAll();
            Sfx_PlaySfxCentered(NA_SE_EV_WOOD_HIT);
            AwardPoints(0, kPointsCarpenter);
            break;
        default:
            sRun.powerUpFrames[static_cast<int>(type)] = kPowerUpSeconds * kFps;
            break;
    }
}

static void UpdateDrops(PlayState* play, Player* player) {
    for (size_t i = 0; i < sRun.drops.size();) {
        ActiveDrop& drop = sRun.drops[i];
        if (drop.actor->update == nullptr) {
            // The drop killed itself. If Link is standing on it, he picked it up.
            PowerUp type = drop.type;
            bool pickedUp = Math_Vec3f_DistXZ(&drop.actor->world.pos, &player->actor.world.pos) < 60.0f;
            sRun.drops.erase(sRun.drops.begin() + i);
            if (pickedUp) {
                ActivatePowerUp(play, type);
            }
            continue;
        }
        drop.framesLeft--;
        // Blink for the last five seconds, like the Black Ops drops.
        if (drop.framesLeft < 5 * kFps) {
            drop.actor->draw = ((drop.framesLeft / 2) % 2 == 0) ? drop.draw : nullptr;
        }
        if (drop.framesLeft <= 0) {
            Actor* actor = drop.actor;
            sRun.drops.erase(sRun.drops.begin() + i);
            Actor_Kill(actor);
            continue;
        }
        i++;
    }

    for (int& frames : sRun.powerUpFrames) {
        if (frames > 0) {
            frames--;
        }
    }
}

// ---------------------------------------------------------------------------
// Rounds

static void StartRound(int round) {
    sRun.round = round;
    sRun.dogRound = CVarOn(CVAR_ZOMBIES("WolfosRounds"), 1) && round == sRun.nextDogRound;
    sRun.dropTracker.NewRound();
    sRun.repairPointsThisRound = 0;

    if (sRun.dogRound) {
        sRun.dogRoundIndex++;
        sRun.toSpawn = DogsForRound(sRun.dogRoundIndex, sRun.playerCount);
        sRun.nextDogRound = NextDogRound(round, Rand_ZeroOne());
        Announce("WOLFOS ROUND", 4);
        Sfx_PlaySfxCentered(NA_SE_EN_WOLFOS_CRY);
    } else {
        sRun.toSpawn = ZombiesForRound(round, sRun.playerCount);
        Announce("ROUND " + std::to_string(round), 4);
        Sfx_PlaySfxCentered(NA_SE_EN_REDEAD_CRY);
    }

    // Zelda twist: hearts come back at the start of each round instead of regenerating.
    gSaveContext.health = gSaveContext.healthCapacity;
    // Black Ops gives two grenades a round; here, two bombs.
    if (INV_CONTENT(ITEM_BOMB) == ITEM_BOMB && round > 1) {
        AMMO(ITEM_BOMB) = std::min<int>(AMMO(ITEM_BOMB) + 2, CUR_CAPACITY(UPG_BOMB_BAG));
    }

    Extras_OnRoundStart(sRun.dogRound);

    sRun.spawnTimer = 2 * kFps;
    sRun.phase = Phase::Active;
}

static void EndRound(PlayState* play) {
    if (sRun.dogRound) {
        // The last Wolfos always drops Max Ammo.
        SpawnPowerUp(play, sRun.lastKillPos, PowerUp::MaxAmmo);
    }
    Announce("ROUND " + std::to_string(sRun.round) + " SURVIVED", 3);
    Extras_OnRoundEnd();
    Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
    sRun.phase = Phase::Intermission;
    sRun.phaseTimer = 10 * kFps;
}

static void UpdateActiveRound(PlayState* play, Player* player) {
    int maxAlive = CVarGetInteger(CVAR_ZOMBIES("MaxAlive"), 8);
    int alive = static_cast<int>(sRun.enemies.size());

    if (sRun.toSpawn > 0 && alive < maxAlive) {
        if (--sRun.spawnTimer <= 0) {
            EnemyKind kind = sRun.dogRound ? EnemyKind::WhiteWolfos : PickEnemy(sRun.round, Rand_ZeroOne(), IS_DAY);
            if (SpawnEnemy(play, player, kind)) {
                sRun.toSpawn--;
                sRun.spawnTimer = static_cast<int>(SpawnDelaySeconds(sRun.round) * kFps);
            } else {
                // No floor found this frame; try again shortly.
                sRun.spawnTimer = kFps / 4;
            }
        }
    }

    if (sRun.toSpawn == 0 && sRun.enemies.empty()) {
        EndRound(play);
    }
}

static void ForceNight() {
    if (CVarOn(CVAR_ZOMBIES("ForceNight"), 1)) {
        gSaveContext.dayTime = 0; // midnight
        gSaveContext.nightFlag = 1;
    }
}

static void StartRun(PlayState* play, Player* player) {
    sRun = Run();
    sRun.phase = Phase::Intro;
    sRun.phaseTimer = 4 * kFps;
    sRun.sceneNum = play->sceneNum;
    sRun.origin = player->actor.world.pos;
    sRun.originYaw = player->actor.shape.rot.y;
    sRun.playerCount = 1;
    sRun.players[0].points = CVarGetInteger(CVAR_ZOMBIES("StartingPoints"), kStartingPoints);
    int startRound = std::max(CVarGetInteger(CVAR_ZOMBIES("StartingRound"), 1), 1);
    sRun.nextDogRound = FirstDogRound(Rand_ZeroOne()) + startRound - 1;

    TakeSnapshot();
    if (ZombiesOnly()) {
        GiveLoadout(play);
    }
    ForceNight();
    Arena_Start(play);
    Stations_Place(play);
    Barricades_Place(play);
    Extras_OnRunStart(play);

    Announce(sRun.layout != nullptr ? sRun.layout->name : "ZOMBIES OF TIME", 4);
    Sfx_PlaySfxCentered(NA_SE_VO_NAVY_HELLO);
}

static void EndRun(const std::string& reason, bool linkIsDead) {
    if (!IsRunActive()) {
        return;
    }
    int survived = std::max(sRun.round - (sRun.phase == Phase::Intermission ? 0 : 1), 0);
    if (survived > CVarGetInteger(CVAR_ZOMBIES("BestRound"), 0)) {
        CVarSetInteger(CVAR_ZOMBIES("BestRound"), survived);
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    if (gPlayState != nullptr) {
        KillAllEnemies(gPlayState, false);
        for (ActiveDrop& drop : sRun.drops) {
            Actor_Kill(drop.actor);
        }
    }
    sRun.enemies.clear();
    sRun.drops.clear();
    Stations_Clear();
    Extras_OnRunEnd(gPlayState, linkIsDead);

    if (!CVarOn(CVAR_ZOMBIES("KeepItems"), 0)) {
        RestoreSnapshot(linkIsDead);
    }

    sRun.phase = Phase::GameOver;
    sRun.phaseTimer = 8 * kFps;
    Announce(reason + ": " + std::to_string(survived) + (survived == 1 ? " ROUND" : " ROUNDS"), 8);
}

static bool LinkIsDying(PlayState* play) {
    s16 state = play->gameOverCtx.state;
    return state >= GAMEOVER_DEATH_START && state <= GAMEOVER_DEATH_MENU;
}

static bool CanStartRun(PlayState* play, Player* player) {
    return !Player_InCsMode(play) && play->msgCtx.msgMode == MSGMODE_NONE && play->transitionMode == TRANS_MODE_OFF &&
           (player->actor.bgCheckFlags & 1);
}

static u16 BuyButton() {
    return CVarGetInteger(CVAR_ZOMBIES("BuyButton"), 0) == 1 ? BTN_DDOWN : BTN_L;
}

const char* BuyButtonName() {
    return CVarGetInteger(CVAR_ZOMBIES("BuyButton"), 0) == 1 ? "DPAD DOWN" : "L";
}

bool BuyPressed(PlayState* play) {
    return CHECK_BTN_ALL(play->state.input[0].press.button, BuyButton());
}

bool BuyHeld(PlayState* play) {
    return CHECK_BTN_ALL(play->state.input[0].cur.button, BuyButton());
}

static bool sStationsNeedPlacing = false;

static void OnPlayerUpdate() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(play);

    if (sRun.announcement.framesLeft > 0) {
        sRun.announcement.framesLeft--;
    }
    for (size_t i = 0; i < sRun.popups.size();) {
        if (--sRun.popups[i].framesLeft <= 0) {
            sRun.popups.erase(sRun.popups.begin() + i);
        } else {
            i++;
        }
    }

    switch (sRun.phase) {
        case Phase::Inactive:
            sRun.prompt.clear();
            if (ZombiesOnly()) {
                Lobby_Update(play, player);
                return;
            }
            if (CanStartRun(play, player)) {
                sRun.prompt = std::string(BuyButtonName()) + ": START ZOMBIES OF TIME";
                if (BuyPressed(play)) {
                    StartRun(play, player);
                }
            }
            return;
        case Phase::GameOver:
            sRun.prompt.clear();
            if (--sRun.phaseTimer <= 0) {
                sRun.phase = Phase::Inactive;
            }
            return;
        default:
            break;
    }

    if (LinkIsDying(play)) {
        EndRun("GAME OVER", true);
        return;
    }

    if (sStationsNeedPlacing) {
        sStationsNeedPlacing = false;
        Stations_Place(play);
    }

    if (sRun.stormFrames > 0) {
        sRun.stormFrames--;
    }

    UpdateEnemies(play, player);
    UpdateDrops(play, player);
    Stations_Update(play, player);
    Arena_Update(play, player);
    Barricades_Update(play, player);
    Extras_Update(play, player);

    switch (sRun.phase) {
        case Phase::Intro:
            if (--sRun.phaseTimer <= 0) {
                StartRound(std::max(CVarGetInteger(CVAR_ZOMBIES("StartingRound"), 1), 1));
            }
            break;
        case Phase::Intermission:
            if (--sRun.phaseTimer <= 0) {
                ForceNight();
                StartRound(sRun.round + 1);
            }
            break;
        case Phase::Active:
            UpdateActiveRound(play, player);
            break;
        default:
            break;
    }
}

static void OnActorDestroy(void* refActor) {
    Actor* actor = static_cast<Actor*>(refActor);
    Stations_OnActorDestroy(actor);
    Extras_OnActorDestroy(actor);
    for (size_t i = 0; i < sRun.enemies.size(); i++) {
        if (sRun.enemies[i].actor == actor) {
            ForgetEnemy(i, !sRun.enemies[i].killCounted);
            return;
        }
    }
    for (size_t i = 0; i < sRun.drops.size(); i++) {
        if (sRun.drops[i].actor == actor) {
            sRun.drops.erase(sRun.drops.begin() + i);
            return;
        }
    }
}

static void OnSceneInit(int16_t sceneNum) {
    Lobby_OnSceneInit(sceneNum);
    if (!IsRunActive() || sRun.phase == Phase::GameOver) {
        if (sRun.phase == Phase::GameOver) {
            sRun.phase = Phase::Inactive;
        }
        return;
    }
    if (sceneNum != sRun.sceneNum) {
        EndRun("YOU LEFT THE ARENA", false);
        return;
    }
    // Same scene reloaded (a void out or a room reset): every actor was unloaded.
    sRun.enemies.clear();
    sRun.drops.clear();
    Extras_OnSceneReload();
    sStationsNeedPlacing = true;
}

static void OnOcarinaSong() {
    if (!IsRunActive() || gPlayState == nullptr) {
        return;
    }
    switch (gPlayState->msgCtx.lastPlayedSong) {
        case OCARINA_SONG_SUNS:
            if (!sRun.powerOn) {
                sRun.powerOn = true;
                Announce("THE POWER IS ON");
                Sfx_PlaySfxCentered(NA_SE_EV_DIAMOND_SWITCH);
            }
            break;
        case OCARINA_SONG_STORMS:
            if (sRun.stormFrames == 0) {
                sRun.stormFrames = 20 * kFps;
                Announce("THE STORM SLOWS THE HORDE");
            }
            break;
        case OCARINA_SONG_LULLABY:
            if (!sRun.papUnlocked) {
                sRun.papUnlocked = true;
                Announce("PACK-A-PUNCH UNSEALED");
                Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
            }
            break;
        default:
            break;
    }
}

static void OnGameFrameUpdate() {
    if (gPlayState == nullptr) {
        return;
    }
    if (IsRunActive() || !sRun.prompt.empty() || sRun.announcement.framesLeft > 0 ||
        CVarOn(CVAR_ZOMBIES("ShowCoordinates"), 0)) {
        Hud_Draw();
    }
}

void Lobby_StartRunHere(PlayState* play) {
    StartRun(play, GET_PLAYER(play));
}

static void RegisterZombies() {
    bool enabled = CVarOn(CVAR_ZOMBIES("Enabled"), 0) || ZombiesOnly();

    if (!enabled && IsRunActive()) {
        EndRun("ZOMBIES OFF", false);
        sRun.phase = Phase::Inactive;
    }
    if (!enabled) {
        sRun.prompt.clear();
        sRun.announcement.framesLeft = 0;
    }

    COND_HOOK(OnPlayerUpdate, enabled, OnPlayerUpdate);
    COND_HOOK(OnActorDestroy, enabled, OnActorDestroy);
    COND_HOOK(OnSceneInit, enabled, OnSceneInit);
    COND_HOOK(OnOcarinaSongAction, enabled, OnOcarinaSong);
    COND_HOOK(OnGameFrameUpdate, enabled, OnGameFrameUpdate);
    COND_HOOK(OnPlayDrawEnd, enabled, Barricades_Draw);
    COND_HOOK(OnLoadGame, enabled, [](int32_t) { Lobby_OnLoadGame(); });

    // Stamin-Up: Link runs 25% faster.
    COND_VB_SHOULD(VB_PLAYER_MODIFY_RUN_SPEED, enabled, {
        [[maybe_unused]] Player* player = va_arg(args, Player*);
        f32* speedTarget = va_arg(args, f32*);
        if (IsRunActive() && sRun.players[0].perks[PERK_STAMIN_UP]) {
            *speedTarget *= 1.25f;
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterZombies, { CVAR_ZOMBIES("Enabled"), CVAR_ZOMBIES("ZombiesOnly") });

} // namespace Zombies
