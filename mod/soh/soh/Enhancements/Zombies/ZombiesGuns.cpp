// Zombies of Time: Black Ops guns in Link's hands.
//
// Guns live on one C button (C-Left by default, a setting), which the mode keeps empty
// and reads before the game does, so OoT's item system never sees it. Hip-fire goes where
// Link faces (with a little aim assist, as on a Black Ops console) or at whatever Z
// locks onto; C-Up, OoT's own first-person look, is aiming down the sights, with a
// viewmodel and tighter spread, and Link can walk while he aims.
//
// Damage goes straight onto the undead's health, which the mode's hit tracking in
// UpdateEnemies credits next frame (10 points a hit, 100 for a kill as a ranged kill).
// A gun kill ends with OoT's own enemy death flames, since setting an enemy's health to
// zero doesn't start its own death.
//
// Link's gun poses are our own keyframes, written over his animated arms just before he
// is drawn (and put back right after), so nothing changes for his items and animations
// when no gun is up.

#include "ZombiesGuns.h"
#include "ZombiesFx.h"
#include "ZombiesMode.h"
#include "ZombiesModel.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <vector>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "objects/object_link_boy/object_link_boy.h"
#include "objects/object_link_child/object_link_child.h"
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "overlays/effects/ovl_Effect_Ss_HitMark/z_eff_ss_hitmark.h"
extern PlayState* gPlayState;
// z_player.c
void Player_UseItem(PlayState* play, Player* player, s32 item);
void func_8083DF68(Player* player, f32 speedTarget, s16 yawTarget);
void func_8084029C(Player* player, f32 animSpeed);
s32 Player_GetMovementSpeedAndYaw(Player* player, f32* outSpeedTarget, s16* outYawTarget, f32 speedMode,
                                  PlayState* play);
}

namespace Zombies {

// ---------------------------------------------------------------------------
// The imported-model seam

struct GunModel {
    ZModel* world = nullptr;  // in Link's hand
    ZModel* view = nullptr;   // the first-person arms and gun
    ZAnim* idle = nullptr;
    ZAnim* fire = nullptr;
    ZAnim* reload = nullptr;  // a shotgun's is one shell, looped
    ZAnim* draw = nullptr;    // pullout; played backwards to put the gun away
    float reloadEnd = 0.0f;   // seconds: the reload's "end" notetrack, or its length
    Gfx* worldDl = nullptr;   // built on first use
    ModelDrawBuffers worldBuffers;
    ModelDrawBuffers viewBuffers;
};

static std::string RoleName(const std::string& role) {
    std::vector<std::string> names = Model_Manifest(role);
    return names.empty() ? std::string() : names[0];
}

// Where the "end" notetrack falls (Black Ops lets the player act from there), else the
// animation's length.
static float NoteTime(const ZAnim* anim, const char* note) {
    float len = Anim_Length(anim);
    for (float t = 0.0f; t < len; t += 1.0f / 30.0f) {
        if (Anim_NoteBetween(anim, note, t - 1.0f / 30.0f, t)) {
            return t;
        }
    }
    return len;
}

// The models' surfaces for attachments the zombies guns don't have (scopes, silencers,
// grenade launchers, extended magazines, the multiplayer clan tag) are left out.
static bool GunSurface(const std::string& m, bool textured, ModelTint* tint, bool armsGreen) {
    for (const char* extra : { "scope", "reflex", "red_dot", "aimpoint", "suppressor", "silencer", "tishina",
                               "foregrip", "grip", "clan_tag", "player_icon", "rail", "mag_wrap", "acog", "elbit",
                               "lens", "ir_", "flash_hider", "extclip", "dualclip", "magazines2", "grenade_launcher",
                               "_gl", "masterkey", "flamethrower", "flamer", "m203", "gunset_attachments",
                               "weapon_mk", "apature", "aperture", "hkmount", "_mount", "expander", "drum_mag" }) {
        if (m.find(extra) != std::string::npos) {
            return false;
        }
    }
    if (armsGreen && m.find("glovesleeve") != std::string::npos) {
        *tint = { 110, 190, 90 }; // Link's tunic over the sleeve
    } else if (armsGreen && m.find("glove") != std::string::npos) {
        *tint = { 200, 160, 120 }; // his leather gauntlets
    } else if (!textured) {
        bool wood = m.find("wood") != std::string::npos || m.find("stock") != std::string::npos;
        *tint = wood ? ModelTint{ 120, 78, 44 } : ModelTint{ 74, 76, 80 };
    }
    return true;
}

static const ModelStyle& WorldStyle() {
    static const ModelStyle sStyle = [](const std::string& m, bool textured, ModelTint* tint) {
        return GunSurface(m, textured, tint, false);
    };
    return sStyle;
}

static const ModelStyle& ViewStyle() {
    static const ModelStyle sStyle = [](const std::string& m, bool textured, ModelTint* tint) {
        return GunSurface(m, textured, tint, true); // Link's arms, not a marine's
    };
    return sStyle;
}

GunModel* Guns_ImportedModel(const char* boName) {
    static std::map<std::string, std::unique_ptr<GunModel>> sModels;
    if (boName == nullptr || boName[0] == '\0') {
        return nullptr;
    }
    auto it = sModels.find(boName);
    if (it == sModels.end()) {
        // The importer's manifest names each gun's parts; without one, Black Ops' usual names.
        std::string key = std::string("gun_") + boName + "_";
        std::string stem = boName;
        if (stem.size() > 3 && stem.compare(stem.size() - 3, 3, "_zm") == 0) {
            stem.resize(stem.size() - 3);
        }
        auto name = [&](const char* role, const std::string& fallback) {
            std::string n = RoleName(key + role);
            return n.empty() ? fallback : n;
        };
        auto gm = std::make_unique<GunModel>();
        gm->world = Model_Get(name("world", "t5_weapon_" + stem + "_world"));
        gm->view = Model_Get(name("view", "t5_weapon_" + stem + "_viewmodel_arms"));
        gm->idle = Anim_Get(name("anim_idle", "viewmodel_" + stem + "_idle"));
        gm->fire = Anim_Get(name("anim_fire", "viewmodel_" + stem + "_fire"));
        gm->reload = Anim_Get(name("anim_reload", "viewmodel_" + stem + "_reload"));
        gm->draw = Anim_Get(name("anim_draw", "viewmodel_" + stem + "_pullout"));
        if (gm->view != nullptr && gm->idle == nullptr && gm->fire == nullptr) {
            gm->view = nullptr; // no pose to hold it in: the stand-in viewmodel
        }
        gm->reloadEnd = NoteTime(gm->reload, "end");
        bool any = gm->world != nullptr || gm->view != nullptr;
        it = sModels.emplace(boName, any ? std::move(gm) : nullptr).first;
    }
    return it->second.get();
}

// The world model's display list, in model units times Model_VertexScale().
static Gfx* GunWorldDl(GunModel* gm) {
    if (gm == nullptr || gm->world == nullptr) {
        return nullptr;
    }
    if (gm->worldDl == nullptr) {
        ModelPose pose;
        Model_Pose(gm->world, AnimLayer(), AnimLayer(), 0.0f, &pose);
        gm->worldDl = Model_PosedDl(gm->world, pose, &gm->worldBuffers, &WorldStyle());
    }
    return gm->worldDl;
}

// ---------------------------------------------------------------------------
// State

static constexpr int kFps = 20;
static constexpr int kRaiseFrames = 3 * kFps; // a gun stays up this long after it's used
static constexpr int kSwapFrames = 16;
static constexpr int kSwapHoldFrames = 10; // holding the reload button this long swaps instead
static constexpr float kAdsSpeedScale = 0.6f;
static constexpr float kHeadshotShare = 0.22f; // the top of an undead's collider is its head
static constexpr float kAimAssistDeg = 12.0f;
static constexpr float kPi = 3.14159265f;

struct GunState {
    float fireCooldown = 0.0f;
    int reloadFrames = 0; // counting down while a reload is in progress
    int reloadTotal = 0;
    int swapFrames = 0;
    int swapTo = -1;
    int sinceShot = 1000;
    int sinceItem = 1000; // frames since Link last had a Zelda item in hand
    int buyHold = 0;      // frames the reload button has been held
    int lastHitFrame = -100;
    float recoil = 0.0f;  // 1 right after a shot, decays
    float raise = 0.0f;   // pose blend, 0 lowered to 1 up
    bool triggerWasDown = false;
    bool warnedButton = false;
};

static PlayerGuns sGuns[kMaxPlayers];
static GunState sState[kMaxPlayers];
static int sFrame = 0;
static int sRack = 0; // the Armory entry Link is looking at

// The gun button as read this frame, before the game saw it.
static bool sGunPressed = false;
static bool sGunHeld = false;
// ADS walking without a right stick: the stick's forward/back, kept from pitching the view.
static s8 sAdsStickY = 0;
static bool sAdsActive = false;

struct Bolt {
    Vec3f pos;
    Vec3f vel;
    int life;
    GunId gun;
    bool pap;
    int who;
};
static std::vector<Bolt> sBolts;

struct PendingDeath {
    Actor* actor;
    int frames;
};
static std::vector<PendingDeath> sPendingDeaths;

PlayerGuns& Guns_Of(int player) {
    return sGuns[std::clamp(player, 0, kMaxPlayers - 1)];
}

int Guns_Button() {
    return std::clamp(CVarGetInteger(CVAR_ZOMBIES("GunButton"), 1), 1, 3);
}

static u16 GunButtonMask() {
    switch (Guns_Button()) {
        case 2:
            return BTN_CDOWN;
        case 3:
            return BTN_CRIGHT;
        default:
            return BTN_CLEFT;
    }
}

static const char* GunButtonName() {
    switch (Guns_Button()) {
        case 2:
            return "C-DOWN";
        case 3:
            return "C-RIGHT";
        default:
            return "C-LEFT";
    }
}

static bool RunLive() {
    Phase phase = GetRun().phase;
    return phase == Phase::Intro || phase == Phase::Active || phase == Phase::Intermission;
}

void Guns_NewRun(PlayState* play) {
    (void)play;
    for (int i = 0; i < kMaxPlayers; i++) {
        sGuns[i] = PlayerGuns();
        sState[i] = GunState();
        sGuns[i].Give(GunId::M1911); // Black Ops: everyone starts with the M1911
    }
    sBolts.clear();
    sPendingDeaths.clear();
    sRack = 0;
}

void Guns_EndRun() {
    sBolts.clear();
    sPendingDeaths.clear();
    sAdsActive = false;
}

bool Guns_FiredRecently(int player) {
    return sFrame - sState[std::clamp(player, 0, kMaxPlayers - 1)].lastHitFrame <= 1;
}

void Guns_MaxAmmo() {
    for (int i = 0; i < GetRun().playerCount; i++) {
        sGuns[i].MaxAmmo();
    }
}

// ---------------------------------------------------------------------------
// Sounds: OoT stand-ins, pitched per gun class. The imported Black Ops sounds replace
// these when the importer provides them.

static f32 sSfxVolume = 1.0f;
static s8 sSfxReverb = 0;
static f32 sPitch[] = { 2.1f, 2.5f, 1.8f, 1.3f, 1.6f, 1.0f, 1.5f };
enum Pitch { PITCH_PISTOL, PITCH_SMG, PITCH_RIFLE, PITCH_SHOTGUN, PITCH_LMG, PITCH_NORMAL, PITCH_RAY };
static f32 sVolume[] = { 0.45f, 0.35f, 0.5f, 0.7f, 0.45f, 1.0f };
enum Volume { VOL_PISTOL, VOL_SMG, VOL_RIFLE, VOL_SHOTGUN, VOL_LMG, VOL_FULL };

static void PlayGunSfx(Player* player, u16 sfx, int pitch, int volume) {
    Audio_PlaySfxGeneral(sfx, &player->actor.projectedPos, 4, &sPitch[pitch], &sVolume[volume], &sSfxReverb);
}

static void PlayShotSfx(Player* player, GunClass gunClass) {
    switch (gunClass) {
        case GunClass::Pistol:
            PlayGunSfx(player, NA_SE_IT_BOMB_EXPLOSION, PITCH_PISTOL, VOL_PISTOL);
            break;
        case GunClass::Smg:
            PlayGunSfx(player, NA_SE_IT_BOMB_EXPLOSION, PITCH_SMG, VOL_SMG);
            break;
        case GunClass::Rifle:
            PlayGunSfx(player, NA_SE_IT_BOMB_EXPLOSION, PITCH_RIFLE, VOL_RIFLE);
            break;
        case GunClass::Shotgun:
            PlayGunSfx(player, NA_SE_IT_BOMB_EXPLOSION, PITCH_SHOTGUN, VOL_SHOTGUN);
            break;
        case GunClass::Lmg:
            PlayGunSfx(player, NA_SE_IT_BOMB_EXPLOSION, PITCH_LMG, VOL_LMG);
            break;
        case GunClass::Launcher:
        default:
            PlayGunSfx(player, NA_SE_IT_MAGIC_ARROW_SHOT, PITCH_RAY, VOL_FULL);
            break;
    }
}

// ---------------------------------------------------------------------------
// Who's holding what

// Link can't use a gun right now: climbing, swimming, carrying, in a cutscene, mid-swing...
static bool GunBlocked(PlayState* play, Player* player) {
    const u32 blocked1 = PLAYER_STATE1_HOOKSHOT_FALLING | PLAYER_STATE1_TALKING | PLAYER_STATE1_DEAD |
                         PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_CHARGING_SPIN_ATTACK |
                         PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE |
                         PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_SHIELDING | PLAYER_STATE1_ON_HORSE |
                         PLAYER_STATE1_USING_BOOMERANG | PLAYER_STATE1_IN_WATER | PLAYER_STATE1_IN_CUTSCENE;
    if ((player->stateFlags1 & blocked1) || (player->stateFlags2 & (PLAYER_STATE2_OCARINA_PLAYING | PLAYER_STATE2_CRAWLING))) {
        return true;
    }
    if (player->csAction != 0 || Player_InCsMode(play) || player->meleeWeaponState != 0) {
        return true;
    }
    if (player->unk_6AD >= 2) {
        return true; // aiming the bow, slingshot or hookshot
    }
    const Run& run = GetRun();
    return run.linkDown.downed || run.linkDown.bledOut || Pause_IsOpen() || Lobby_IsOpen();
}

static bool HoldsSword(Player* player) {
    switch (player->heldItemAction) {
        case PLAYER_IA_NONE:
        case PLAYER_IA_SWORD_CS:
        case PLAYER_IA_SWORD_MASTER:
        case PLAYER_IA_SWORD_KOKIRI:
        case PLAYER_IA_SWORD_BIGGORON:
            return true;
        default:
            return false;
    }
}

// C-Up first person with a gun: aiming down the sights.
static bool InAds(Player* player) {
    return player->unk_6AD == 1 && (player->stateFlags1 & PLAYER_STATE1_FIRST_PERSON) &&
           sGuns[0].Current().id != GunId::None;
}

// ---------------------------------------------------------------------------
// Damage

static TrackedEnemy* FindTracked(Actor* actor) {
    for (TrackedEnemy& enemy : GetRun().enemies) {
        if (enemy.actor == actor) {
            return &enemy;
        }
    }
    return nullptr;
}

static void QueueDeath(Actor* actor) {
    for (const PendingDeath& pending : sPendingDeaths) {
        if (pending.actor == actor) {
            return;
        }
    }
    sPendingDeaths.push_back({ actor, 0 });
}

static int MaxHealthOf(const TrackedEnemy& enemy) {
    return std::max<int>(enemy.maxHealth, std::max<int>(enemy.lastHealth, 1));
}

// Takes Black Ops damage off one undead. Fractions of a hit point carry over to the next
// hit, so a pistol's tenth of a heart still adds up in the late rounds.
static void DamageEnemy(TrackedEnemy& enemy, float boDamage, int who, const Vec3f& hitPos, bool headshot) {
    Actor* actor = enemy.actor;
    if (enemy.killCounted || actor == nullptr || actor->update == nullptr || actor->colChkInfo.health == 0) {
        return;
    }
    const Run& run = GetRun();
    float hp = BoDamageToHp(boDamage, MaxHealthOf(enemy), run.round, CVarGetInteger(CVAR_ZOMBIES("HealthScaling"), 100)) +
               enemy.gunCarry;
    int whole = static_cast<int>(std::floor(hp));
    enemy.gunCarry = hp - static_cast<float>(whole);
    if (PowerUpActive(PowerUp::InstaKill)) {
        whole = std::max(whole, static_cast<int>(actor->colChkInfo.health));
    }
    sState[who].lastHitFrame = sFrame;
    Actor_SetColorFilter(actor, 0x4000, 255, 0, 4); // the red flash OoT enemies give when hit
    PlayState* play = gPlayState;
    if (play != nullptr) {
        Vec3f mark = hitPos;
        EffectSsHitMark_SpawnFixedScale(play, headshot ? EFFECT_HITMARK_RED : EFFECT_HITMARK_WHITE, &mark);
    }
    if (whole <= 0) {
        // Too little to show on its health this hit: the mode won't see it, so pay the
        // Black Ops 10 points for it here.
        AwardPoints(who, kPointsPerHit);
        return;
    }
    int health = std::max(0, static_cast<int>(actor->colChkInfo.health) - whole);
    actor->colChkInfo.health = static_cast<u8>(health);
    if (health == 0) {
        QueueDeath(actor);
    }
}

// OoT's generic enemy death for undead a gun (or a scaled item hit) dropped: the purple
// flames, a drop and the actor gone. Runs the frame after, once the mode has counted it.
static void UpdatePendingDeaths(PlayState* play) {
    for (size_t i = 0; i < sPendingDeaths.size();) {
        PendingDeath& pending = sPendingDeaths[i];
        TrackedEnemy* enemy = FindTracked(pending.actor);
        if (enemy == nullptr || pending.actor->update == nullptr || pending.actor->colChkInfo.health != 0) {
            sPendingDeaths.erase(sPendingDeaths.begin() + i); // gone, or back on its feet
            continue;
        }
        if (!enemy->killCounted && ++pending.frames < 5) {
            i++;
            continue;
        }
        Actor* actor = pending.actor;
        Vec3f velAndAccel = { 0.0f, 0.5f, 0.0f };
        for (int k = 0; k < 6; k++) {
            Vec3f pos = { actor->world.pos.x + Rand_CenteredFloat(40.0f), actor->world.pos.y + 15.0f + Rand_ZeroFloat(35.0f),
                          actor->world.pos.z + Rand_CenteredFloat(40.0f) };
            EffectSsDeadDb_Spawn(play, &pos, &velAndAccel, &velAndAccel, 100, 0, 255, 255, 255, 255, 0, 0, 255, 1, 9,
                                 k == 0);
        }
        Item_DropCollectibleRandom(play, nullptr, &actor->world.pos, 0x10);
        Actor_Kill(actor);
        sPendingDeaths.erase(sPendingDeaths.begin() + i);
    }
}

// Splash from an explosive round or a Ray Gun bolt: full damage at the centre, half at the edge.
static void Splash(PlayState* play, const Vec3f& center, float damage, float radius, int who, Actor* skip) {
    for (TrackedEnemy& enemy : GetRun().enemies) {
        if (enemy.actor == skip || enemy.killCounted || enemy.actor->update == nullptr) {
            continue;
        }
        Vec3f mid = enemy.actor->world.pos;
        mid.y += 20.0f;
        float dist = Math_Vec3f_DistXYZ(&mid, const_cast<Vec3f*>(&center));
        if (dist < radius) {
            DamageEnemy(enemy, damage * (1.0f - 0.5f * dist / radius), who, mid, false);
        }
    }
    (void)play;
}

// ---------------------------------------------------------------------------
// Firing

struct RayHit {
    TrackedEnemy* enemy;
    float t;
    Vec3f pos;
    bool headshot;
};

static void EnemyCylinder(const Actor* actor, float* radius, float* bottom, float* height) {
    *radius = actor->colChkInfo.cylRadius > 0 ? static_cast<float>(actor->colChkInfo.cylRadius) : 20.0f;
    *height = actor->colChkInfo.cylHeight > 0 ? static_cast<float>(actor->colChkInfo.cylHeight) : 50.0f;
    *bottom = actor->world.pos.y + static_cast<float>(actor->colChkInfo.cylYShift);
    // Undead are hit a little more generously than their pushing collider.
    *radius += 6.0f;
}

// Where a ray first meets an undead's collider, or false.
static bool RayVsEnemy(const Vec3f& origin, const Vec3f& dir, float maxT, const Actor* actor, float* outT) {
    float radius, bottom, height;
    EnemyCylinder(actor, &radius, &bottom, &height);
    float ox = origin.x - actor->world.pos.x;
    float oz = origin.z - actor->world.pos.z;
    float a = dir.x * dir.x + dir.z * dir.z;
    float b = 2.0f * (ox * dir.x + oz * dir.z);
    float c = ox * ox + oz * oz - radius * radius;
    float t;
    if (a < 1e-6f) {
        if (c > 0.0f) {
            return false;
        }
        t = 0.0f;
    } else {
        float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f) {
            return false;
        }
        float root = std::sqrt(disc);
        t = (-b - root) / (2.0f * a);
        if (t < 0.0f) {
            t = (-b + root) / (2.0f * a); // starting inside it
            if (t < 0.0f) {
                return false;
            }
            t = 0.0f;
        }
    }
    if (t > maxT) {
        return false;
    }
    float y = origin.y + dir.y * t;
    if (y < bottom - 4.0f || y > bottom + height + 6.0f) {
        return false;
    }
    *outT = t;
    return true;
}

static Vec3f DirFromAngles(s16 pitch, s16 yaw) {
    // OoT angles: positive pitch looks down.
    return { Math_SinS(yaw) * Math_CosS(pitch), -Math_SinS(pitch), Math_CosS(yaw) * Math_CosS(pitch) };
}

static Vec3f Normalize(Vec3f v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-4f) {
        return { 0.0f, 0.0f, 1.0f };
    }
    return { v.x / len, v.y / len, v.z / len };
}

// Bends a direction by a random amount inside a cone of halfAngle degrees.
static Vec3f Spread(Vec3f dir, float halfAngleDeg) {
    if (halfAngleDeg <= 0.0f) {
        return dir;
    }
    float r = std::tan(halfAngleDeg * kPi / 180.0f) * std::sqrt(Rand_ZeroOne());
    float a = Rand_ZeroOne() * 2.0f * kPi;
    // Two axes across the direction.
    Vec3f side = Normalize({ dir.z, 0.0f, -dir.x });
    Vec3f up = { dir.y * side.z - dir.z * side.y, dir.z * side.x - dir.x * side.z, dir.x * side.y - dir.y * side.x };
    Vec3f out = { dir.x + (side.x * std::cos(a) + up.x * std::sin(a)) * r,
                  dir.y + (side.y * std::cos(a) + up.y * std::sin(a)) * r,
                  dir.z + (side.z * std::cos(a) + up.z * std::sin(a)) * r };
    return Normalize(out);
}

static bool LineOfSight(PlayState* play, const Vec3f& from, const Vec3f& to) {
    Vec3f a = from;
    Vec3f b = to;
    Vec3f hit;
    CollisionPoly* poly;
    s32 bgId;
    return !BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, true, true, true, &bgId);
}

static Vec3f BodyCentre(const Actor* actor) {
    float radius, bottom, height;
    EnemyCylinder(actor, &radius, &bottom, &height);
    return { actor->world.pos.x, bottom + height * 0.6f, actor->world.pos.z };
}

// Where the shot comes from and goes, before spread.
static void AimRay(PlayState* play, Player* player, const GunStats& gun, bool ads, Vec3f* origin, Vec3f* dir,
                   float* spread) {
    float chest = LINK_IS_ADULT ? 42.0f : 28.0f;
    *origin = { player->actor.world.pos.x, player->actor.world.pos.y + chest, player->actor.world.pos.z };
    *spread = gun.spreadDeg;

    if (ads) {
        *origin = player->actor.focus.pos;
        *dir = DirFromAngles(player->actor.focus.rot.x, player->actor.focus.rot.y);
        *spread = gun.spreadDeg * 0.25f;
        return;
    }
    if (player->focusActor != nullptr) {
        // Z-targeting: OoT's own lock-on is the aim.
        Vec3f target = player->focusActor->focus.pos;
        if (FindTracked(player->focusActor) != nullptr) {
            target = BodyCentre(player->focusActor);
        }
        *dir = Normalize({ target.x - origin->x, target.y - origin->y, target.z - origin->z });
        *spread = gun.spreadDeg * 0.6f;
        return;
    }
    // Hip-fire along Link's facing, nudged onto the undead nearest the line.
    s16 yaw = player->actor.shape.rot.y;
    *dir = DirFromAngles(0, yaw);
    float bestAngle = kAimAssistDeg;
    for (TrackedEnemy& enemy : GetRun().enemies) {
        if (enemy.killCounted || enemy.actor->update == nullptr) {
            continue;
        }
        Vec3f centre = BodyCentre(enemy.actor);
        float dist = Math_Vec3f_DistXYZ(origin, &centre);
        if (dist > gun.range || dist < 1.0f) {
            continue;
        }
        s16 toward = Math_Vec3f_Yaw(origin, &centre);
        float angle = std::fabs(static_cast<float>(static_cast<s16>(toward - yaw))) * 360.0f / 65536.0f;
        if (angle < bestAngle && LineOfSight(play, *origin, centre)) {
            bestAngle = angle;
            *dir = Normalize({ centre.x - origin->x, centre.y - origin->y, centre.z - origin->z });
        }
    }
}

static void MuzzleFlash(PlayState* play, const Vec3f& pos, bool ray) {
    static Color_RGBA8 sFlashPrim = { 255, 255, 200, 255 };
    static Color_RGBA8 sFlashEnv = { 255, 160, 0, 255 };
    static Color_RGBA8 sRayPrim = { 200, 255, 200, 255 };
    static Color_RGBA8 sRayEnv = { 0, 255, 60, 255 };
    Vec3f zero = { 0.0f, 0.0f, 0.0f };
    Vec3f p = pos;
    EffectSsKiraKira_SpawnDispersed(play, &p, &zero, &zero, ray ? &sRayPrim : &sFlashPrim, ray ? &sRayEnv : &sFlashEnv,
                                    ray ? 900 : 700, 3);
}

static Vec3f MuzzlePos(Player* player, const Vec3f& origin, const Vec3f& dir, bool ads) {
    if (ads) {
        return { origin.x + dir.x * 30.0f, origin.y + dir.y * 30.0f - 6.0f, origin.z + dir.z * 30.0f };
    }
    const Vec3f& hand = player->bodyPartsPos[PLAYER_BODYPART_R_HAND];
    return { hand.x + dir.x * 14.0f, hand.y + dir.y * 14.0f, hand.z + dir.z * 14.0f };
}

static void FireHitscan(PlayState* play, Player* player, int who, const GunStats& gun, const GunVariant& variant,
                        const Vec3f& origin, const Vec3f& dir) {
    Vec3f end = { origin.x + dir.x * gun.range, origin.y + dir.y * gun.range, origin.z + dir.z * gun.range };
    float maxT = gun.range;
    Vec3f wallHit = end;
    CollisionPoly* poly;
    s32 bgId;
    Vec3f a = origin;
    bool hitWall = BgCheck_EntityLineTest1(&play->colCtx, &a, &end, &wallHit, &poly, true, true, true, true, &bgId);
    if (hitWall) {
        maxT = Math_Vec3f_DistXYZ(&a, &wallHit);
    }

    std::vector<RayHit> hits;
    for (TrackedEnemy& enemy : GetRun().enemies) {
        if (enemy.killCounted || enemy.actor->update == nullptr) {
            continue;
        }
        float t;
        if (RayVsEnemy(origin, dir, maxT, enemy.actor, &t)) {
            float radius, bottom, height;
            EnemyCylinder(enemy.actor, &radius, &bottom, &height);
            Vec3f pos = { origin.x + dir.x * t, origin.y + dir.y * t, origin.z + dir.z * t };
            bool head = pos.y > bottom + height * (1.0f - kHeadshotShare);
            hits.push_back({ &enemy, t, pos, head });
        }
    }
    std::sort(hits.begin(), hits.end(), [](const RayHit& x, const RayHit& y) { return x.t < y.t; });

    Vec3f impact = hitWall ? wallHit : end;
    bool impacted = hitWall;
    int pierced = 0;
    for (RayHit& hit : hits) {
        if (pierced >= std::max(gun.pierce, 1)) {
            break;
        }
        float damage = variant.damage * (hit.headshot ? gun.headshot : 1.0f);
        if (gun.gunClass == GunClass::Shotgun) {
            // Pellets lose their bite with distance.
            damage *= std::clamp(1.25f - hit.t / gun.range, 0.25f, 1.0f);
        }
        damage *= pierced == 0 ? 1.0f : 0.75f;
        DamageEnemy(*hit.enemy, damage, who, hit.pos, hit.headshot);
        if (pierced == 0) {
            impact = hit.pos;
            impacted = true;
        }
        pierced++;
    }
    if (hits.empty() && hitWall) {
        EffectSsHitMark_SpawnFixedScale(play, EFFECT_HITMARK_DUST, &wallHit);
        Fx_Impact(wallHit, { -dir.x, -dir.y, -dir.z });
    }
    if (variant.splash > 0.0f && impacted) {
        // Explosive rounds (Mustang & Sally).
        Vec3f zero = { 0.0f, 0.0f, 0.0f };
        EffectSsBomb2_SpawnLayered(play, &impact, &zero, &zero, 40, 10);
        Fx_Explosion(impact);
        SoundSource_PlaySfxAtFixedWorldPos(play, &impact, 20, NA_SE_IT_BOMB_EXPLOSION);
        Splash(play, impact, variant.splash, variant.splashRadius, who, nullptr);
    }
    (void)player;
}

static void FireBolt(int who, const GunStats& gun, bool pap, const Vec3f& muzzle, const Vec3f& dir) {
    static constexpr float kBoltSpeed = 45.0f;
    Bolt bolt;
    bolt.pos = muzzle;
    bolt.vel = { dir.x * kBoltSpeed, dir.y * kBoltSpeed, dir.z * kBoltSpeed };
    bolt.life = static_cast<int>(gun.range / kBoltSpeed);
    bolt.gun = gun.id;
    bolt.pap = pap;
    bolt.who = who;
    sBolts.push_back(bolt);
}

static void BoltImpact(PlayState* play, const Bolt& bolt, const Vec3f& pos, TrackedEnemy* direct) {
    const GunVariant& variant = GunVariantOf(bolt.gun, bolt.pap);
    if (direct != nullptr) {
        DamageEnemy(*direct, variant.damage, bolt.who, pos, false);
    }
    Splash(play, pos, variant.splash, variant.splashRadius, bolt.who, direct != nullptr ? direct->actor : nullptr);
    // A green burst: the Ray Gun's stand-in impact until imported effects exist.
    Vec3f vel = { 0.0f, 1.0f, 0.0f };
    Vec3f p = pos;
    EffectSsDeadDb_Spawn(play, &p, &vel, &vel, 90, -4, 160, 255, 160, 255, 0, 220, 40, 1, 8, false);
    Fx_RayGunImpact(p, bolt.pap);
    SoundSource_PlaySfxAtFixedWorldPos(play, &p, 20, NA_SE_IT_EXPLOSION_LIGHT);
}

static void UpdateBolts(PlayState* play) {
    static Color_RGBA8 sPrim = { 210, 255, 210, 255 };
    static Color_RGBA8 sEnv = { 0, 255, 40, 255 };
    for (size_t i = 0; i < sBolts.size();) {
        Bolt& bolt = sBolts[i];
        bool done = --bolt.life <= 0;
        // Three short steps a frame, so the bolt can't skip through a thin undead.
        for (int step = 0; step < 3 && !done; step++) {
            Vec3f next = { bolt.pos.x + bolt.vel.x / 3.0f, bolt.pos.y + bolt.vel.y / 3.0f,
                           bolt.pos.z + bolt.vel.z / 3.0f };
            Vec3f wallHit;
            CollisionPoly* poly;
            s32 bgId;
            Vec3f from = bolt.pos;
            if (BgCheck_EntityLineTest1(&play->colCtx, &from, &next, &wallHit, &poly, true, true, true, true, &bgId)) {
                BoltImpact(play, bolt, wallHit, nullptr);
                done = true;
                break;
            }
            Vec3f dir = Normalize(bolt.vel);
            float len = Math_Vec3f_DistXYZ(&bolt.pos, &next);
            for (TrackedEnemy& enemy : GetRun().enemies) {
                float t;
                if (!enemy.killCounted && enemy.actor->update != nullptr &&
                    RayVsEnemy(bolt.pos, dir, len, enemy.actor, &t)) {
                    Vec3f at = { bolt.pos.x + dir.x * t, bolt.pos.y + dir.y * t, bolt.pos.z + dir.z * t };
                    BoltImpact(play, bolt, at, &enemy);
                    done = true;
                    break;
                }
            }
            bolt.pos = next;
            if (step == 0) {
                Fx_RayGunTrail(bolt.pos, dir, bolt.pap);
            }
            Vec3f zero = { 0.0f, 0.0f, 0.0f };
            EffectSsKiraKira_SpawnDispersed(play, &bolt.pos, &zero, &zero, &sPrim, &sEnv, 1100, 4);
        }
        if (done) {
            sBolts.erase(sBolts.begin() + i);
        } else {
            i++;
        }
    }
}

static void Fire(PlayState* play, Player* player, int who) {
    PlayerGuns& guns = sGuns[who];
    GunState& st = sState[who];
    GunSlot& slot = guns.Current();
    const GunStats& gun = GunInfo(slot.id);
    const GunVariant& variant = GunVariantOf(slot.id, slot.pap);
    bool ads = who == 0 && InAds(player);

    Vec3f origin, dir;
    float spread;
    AimRay(play, player, gun, ads, &origin, &dir, &spread);
    Vec3f muzzle = MuzzlePos(player, origin, dir, ads);

    slot.clip--;
    st.sinceShot = 0;
    st.recoil = 1.0f;
    if (gun.projectile) {
        FireBolt(who, gun, slot.pap, ads ? origin : muzzle, Spread(dir, spread));
    } else {
        for (int p = 0; p < gun.pellets; p++) {
            FireHitscan(play, player, who, gun, variant, origin, Spread(dir, spread));
        }
    }
    if (!ads) {
        MuzzleFlash(play, muzzle, gun.projectile);
        Fx_GunFlash(gun.boName, slot.pap, muzzle, dir);
    }
    PlayShotSfx(player, gun.gunClass);
    if (ads) {
        // A little kick up the sights; OoT's look control brings it back.
        player->actor.focus.rot.x = static_cast<s16>(player->actor.focus.rot.x - (gun.gunClass == GunClass::Shotgun ? 500 : 140));
    }
}

static void StartReload(Player* player, int who) {
    GunState& st = sState[who];
    PlayerGuns& guns = sGuns[who];
    if (st.reloadFrames > 0 || st.swapFrames > 0 || !guns.CanReload()) {
        return;
    }
    const GunSlot& slot = guns.Current();
    const GunStats& gun = GunInfo(slot.id);
    int frames = gun.reloadFrames;
    GunModel* gm = Guns_ImportedModel(gun.boName);
    if (gm != nullptr && gm->view != nullptr && gm->reload != nullptr && gm->reloadEnd > 0.0f) {
        // The imported reload sets the time: a shell at a time for the shotguns' loops.
        float sec = gm->reloadEnd;
        if (Anim_Looping(gm->reload)) {
            int shells = std::min(GunVariantOf(slot.id, slot.pap).clip - slot.clip, slot.reserve);
            sec = Anim_Length(gm->reload) * static_cast<float>(std::max(shells, 1));
        }
        frames = std::max(1, static_cast<int>(sec * kFps + 0.5f));
    }
    // A tactical reload (rounds left in the clip) is a little quicker, as in Black Ops.
    bool looped = gm != nullptr && gm->view != nullptr && Anim_Looping(gm->reload);
    st.reloadTotal = slot.clip > 0 && !looped ? frames * 4 / 5 : frames;
    st.reloadFrames = st.reloadTotal;
    if (gm == nullptr || gm->view == nullptr || gm->reload == nullptr) {
        PlayGunSfx(player, NA_SE_IT_SWORD_PUTAWAY, PITCH_NORMAL, VOL_FULL);
    }
}

// Reload sounds on the imported animation's notetracks (OoT sounds: Black Ops' own are
// not imported).
static void ReloadNotes(Player* player, int who) {
    const GunState& st = sState[who];
    const GunStats& gun = GunInfo(sGuns[who].Current().id);
    GunModel* gm = Guns_ImportedModel(gun.boName);
    if (gm == nullptr || gm->view == nullptr || gm->reload == nullptr || st.reloadTotal <= 0) {
        return;
    }
    float len = Anim_Length(gm->reload);
    float rate = Anim_Looping(gm->reload) ? 1.0f : gm->reloadEnd * kFps / static_cast<float>(st.reloadTotal);
    float to = static_cast<float>(st.reloadTotal - st.reloadFrames) / kFps * rate;
    float from = to - rate / kFps;
    if (Anim_Looping(gm->reload) && len > 0.0f) {
        from = std::fmod(from, len);
        to = from + rate / kFps;
    }
    if (Anim_NoteBetween(gm->reload, "rmbnt#reload_clipout", from, to)) {
        PlayGunSfx(player, NA_SE_IT_SWORD_PUTAWAY, PITCH_NORMAL, VOL_FULL);
    }
    if (Anim_NoteBetween(gm->reload, "rmbnt#reload_clipin", from, to)) {
        PlayGunSfx(player, NA_SE_IT_HOOKSHOT_READY, PITCH_NORMAL, VOL_FULL);
    }
    if (Anim_NoteBetween(gm->reload, "rmbnt#reload_rechamber", from, to)) {
        PlayGunSfx(player, NA_SE_IT_SWORD_PICKOUT, PITCH_NORMAL, VOL_FULL);
    }
}

static void StartSwap(Player* player, int who) {
    GunState& st = sState[who];
    PlayerGuns& guns = sGuns[who];
    int next = guns.NextSlot();
    if (next == guns.current || st.swapFrames > 0) {
        return;
    }
    st.reloadFrames = 0; // swapping cancels a reload, as in Black Ops
    st.swapFrames = kSwapFrames;
    st.swapTo = next;
    PlayGunSfx(player, NA_SE_IT_SWORD_PICKOUT, PITCH_NORMAL, VOL_FULL);
}

// The guns' C button stays empty: anything equipped onto it moves to a free button.
static void KeepGunButtonFree(PlayState* play) {
    int b = Guns_Button();
    u8 item = gSaveContext.equips.buttonItems[b];
    if (item == ITEM_NONE) {
        return;
    }
    for (int other = 1; other <= 3; other++) {
        if (other != b && gSaveContext.equips.buttonItems[other] == ITEM_NONE) {
            gSaveContext.equips.buttonItems[other] = item;
            gSaveContext.equips.cButtonSlots[other - 1] = gSaveContext.equips.cButtonSlots[b - 1];
            Interface_LoadItemIcon1(play, static_cast<u16>(other));
            break;
        }
    }
    gSaveContext.equips.buttonItems[b] = ITEM_NONE;
    gSaveContext.equips.cButtonSlots[b - 1] = SLOT_NONE;
    Interface_LoadItemIcon1(play, static_cast<u16>(b));
    if (!sState[0].warnedButton) {
        sState[0].warnedButton = true;
        Announce(std::string(GunButtonName()) + " HOLDS YOUR GUN", 3);
    }
}

void Guns_Update(PlayState* play, Player* player) {
    sFrame++;
    if (!RunLive()) {
        return;
    }
    UpdatePendingDeaths(play);
    UpdateBolts(play);
    KeepGunButtonFree(play);

    PlayerGuns& guns = sGuns[0];
    GunState& st = sState[0];
    Run& run = GetRun();

    // A press that reached the input after the start-of-frame read (another hook's).
    const Input& input = play->state.input[0];
    sGunPressed = sGunPressed || CHECK_BTN_ALL(input.press.button, GunButtonMask());
    sGunHeld = sGunHeld || CHECK_BTN_ALL(input.cur.button, GunButtonMask());

    st.sinceShot = std::min(st.sinceShot + 1, 1000);
    st.sinceItem = HoldsSword(player) ? std::min(st.sinceItem + 1, 1000) : 0;
    st.recoil = std::max(0.0f, st.recoil - 0.2f);
    if (st.fireCooldown > 0.0f) {
        st.fireCooldown -= 1.0f;
    }

    if (st.swapFrames > 0 && --st.swapFrames == kSwapFrames / 2) {
        guns.current = st.swapTo; // the new gun comes up for the second half
    }
    if (st.reloadFrames > 0) {
        --st.reloadFrames;
        ReloadNotes(player, 0);
        if (st.reloadFrames == 0) {
            guns.Reload();
            GunModel* gm = Guns_ImportedModel(GunInfo(guns.Current().id).boName);
            if (gm == nullptr || gm->view == nullptr || gm->reload == nullptr) {
                PlayGunSfx(player, NA_SE_IT_HOOKSHOT_READY, PITCH_NORMAL, VOL_FULL);
            }
        }
    }

    bool blocked = GunBlocked(play, player);
    bool hasGun = guns.Current().id != GunId::None;

    // The buy button reloads (tap) and swaps (hold) when there's nothing to buy, like
    // Black Ops' use/reload button.
    if (run.prompt.empty() && !blocked && hasGun) {
        if (BuyPressed(play)) {
            st.buyHold = 1;
        } else if (st.buyHold > 0 && BuyHeld(play)) {
            if (++st.buyHold == kSwapHoldFrames) {
                StartSwap(player, 0);
            }
        } else if (st.buyHold > 0) {
            if (st.buyHold < kSwapHoldFrames) {
                StartReload(player, 0);
            }
            st.buyHold = 0;
        }
    } else {
        st.buyHold = 0;
    }

    if (!hasGun) {
        return;
    }
    GunSlot& slot = guns.Current();
    const GunStats& gun = GunInfo(slot.id);
    bool wantsFire = sGunPressed || (gun.automatic && sGunHeld && st.triggerWasDown);
    st.triggerWasDown = sGunHeld;
    if (!wantsFire) {
        return;
    }
    if (blocked) {
        return;
    }
    if (!HoldsSword(player)) {
        // A Zelda item in hand goes away first, as pressing another item's button would.
        if (sGunPressed) {
            Player_UseItem(play, player, ITEM_NONE);
        }
        return;
    }
    st.sinceItem = std::max(st.sinceItem, 1);
    if (st.reloadFrames > 0 || st.swapFrames > 0 || st.fireCooldown > 0.0f) {
        return;
    }
    if (slot.clip <= 0) {
        if (slot.reserve > 0) {
            StartReload(player, 0);
        } else if (sGunPressed) {
            PlayGunSfx(player, NA_SE_IT_HOOKSHOT_REFLECT, PITCH_SMG, VOL_SMG); // dry fire
            st.sinceShot = 0;
        }
        return;
    }
    Fire(play, player, 0);
    st.fireCooldown += gun.fireFrames;
    if (slot.clip == 0 && slot.reserve > 0) {
        st.fireCooldown = 0.0f;
        StartReload(player, 0); // Black Ops reloads on its own when the clip runs dry
    }
}

// ---------------------------------------------------------------------------
// OoT items, scaled by round

static bool ActorNear(PlayState* play, u8 category, s16 id, const Vec3f& pos, float dist) {
    for (Actor* a = play->actorCtx.actorLists[category].head; a != nullptr; a = a->next) {
        if (a->id == id && (dist <= 0.0f || Math_Vec3f_DistXYZ(&a->world.pos, const_cast<Vec3f*>(&pos)) < dist)) {
            return true;
        }
    }
    return false;
}

static bool ExplosionNear(PlayState* play, const Vec3f& pos) {
    for (Actor* a = play->actorCtx.actorLists[ACTORCAT_EXPLOSIVE].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_EN_BOM && a->params == BOMB_EXPLOSION &&
            Math_Vec3f_DistXYZ(&a->world.pos, const_cast<Vec3f*>(&pos)) < 200.0f) {
            return true;
        }
    }
    return false;
}

// Which Zelda weapon just hurt this undead.
static OotWeapon HitSource(PlayState* play, Player* player, const TrackedEnemy& enemy) {
    const Vec3f& pos = enemy.actor->world.pos;
    if (ActorNear(play, ACTORCAT_ITEMACTION, ACTOR_MAGIC_FIRE, pos, 0.0f)) {
        return OotWeapon::DinsFire;
    }
    if (ExplosionNear(play, pos)) {
        return OotWeapon::Bomb;
    }
    if (player->meleeWeaponState != 0) {
        switch (player->heldItemAction) {
            case PLAYER_IA_HAMMER:
                return OotWeapon::Hammer;
            case PLAYER_IA_DEKU_STICK:
                return OotWeapon::DekuStick;
            default:
                return ActorNear(play, ACTORCAT_ITEMACTION, ACTOR_MAGIC_DARK, pos, 0.0f) ? OotWeapon::NayrusBlade
                                                                                         : OotWeapon::None;
        }
    }
    switch (player->heldItemAction) {
        case PLAYER_IA_SLINGSHOT:
            return OotWeapon::Slingshot;
        case PLAYER_IA_BOW:
            return OotWeapon::Bow;
        case PLAYER_IA_BOW_FIRE:
            return OotWeapon::FireArrow;
        case PLAYER_IA_BOW_ICE:
            return OotWeapon::IceArrow;
        case PLAYER_IA_BOW_LIGHT:
            return OotWeapon::LightArrow;
        case PLAYER_IA_HOOKSHOT:
            return OotWeapon::Hookshot;
        case PLAYER_IA_LONGSHOT:
            return OotWeapon::Longshot;
        default:
            break;
    }
    if (player->stateFlags1 & PLAYER_STATE1_BOOMERANG_THROWN) {
        return OotWeapon::Boomerang;
    }
    return OotWeapon::None;
}

int Guns_ScaleItemHit(PlayState* play, Player* player, TrackedEnemy& enemy, int health) {
    if (play == nullptr || health <= 0 || health >= enemy.lastHealth) {
        return health;
    }
    if (sFrame - sState[0].lastHitFrame <= 1) {
        return health; // the gun's own hit, already Black Ops damage
    }
    if (Coop_Credit(enemy) != 0) {
        return health; // player 2's sword and bombs keep their own damage
    }
    OotWeapon weapon = HitSource(play, player, enemy);
    if (weapon == OotWeapon::None) {
        return health;
    }
    int vanilla = enemy.lastHealth - health;
    int scaled = ScaledItemDamage(vanilla, weapon, MaxHealthOf(enemy), GetRun().round,
                                  CVarGetInteger(CVAR_ZOMBIES("HealthScaling"), 100));
    int newHealth = std::max(0, static_cast<int>(enemy.lastHealth) - scaled);
    if (newHealth < health) {
        enemy.actor->colChkInfo.health = static_cast<u8>(newHealth);
        if (newHealth == 0) {
            QueueDeath(enemy.actor); // its own death check already passed it by
        }
        return newHealth;
    }
    return health;
}

// ---------------------------------------------------------------------------
// Armory rack and Mystery Chest

static std::vector<GunId> WallGuns() {
    std::vector<GunId> list;
    for (int i = 1; i < static_cast<int>(GunId::Count); i++) {
        const GunStats& gun = GunInfo(static_cast<GunId>(i));
        if (gun.wallCost > 0 && (GetRun().layout == nullptr || Arena_ZoneOpen(gun.wallZone))) {
            list.push_back(gun.id);
        }
    }
    return list;
}

static int SlotOf(const PlayerGuns& guns, GunId id) {
    for (int i = 0; i < guns.slotCount; i++) {
        if (guns.slots[i].id == id) {
            return i;
        }
    }
    return -1;
}

static GunId RackGun() {
    std::vector<GunId> list = WallGuns();
    if (sRack > static_cast<int>(list.size())) {
        sRack = 0;
    }
    return sRack == 0 ? GunId::None : list[sRack - 1];
}

const char* Guns_ArmoryBrowse(PlayState* play, int who) {
    if (who != 0) {
        return "";
    }
    if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_R)) {
        sRack = (sRack + 1) % (static_cast<int>(WallGuns().size()) + 1);
        Sfx_PlaySfxCentered(NA_SE_SY_CURSOR);
    }
    return "  R: NEXT";
}

bool Guns_ArmoryOffer(int who, std::string* label, int* cost, bool* available, std::string* reason) {
    GunId id = who == 0 ? RackGun() : GunId::None;
    if (id == GunId::None) {
        return false;
    }
    const PlayerGuns& guns = sGuns[who];
    int slot = SlotOf(guns, id);
    if (slot < 0) {
        *label = GunName(id, false);
        *cost = GunInfo(id).wallCost;
        return true;
    }
    const GunSlot& s = guns.slots[slot];
    const GunVariant& v = GunVariantOf(id, s.pap);
    *label = std::string(GunName(id, s.pap)) + " AMMO";
    *cost = GunAmmoCost(id, s.pap);
    if (s.clip >= v.clip && s.reserve >= v.reserve) {
        *available = false;
        *reason = "FULL";
    }
    return true;
}

bool Guns_ArmoryBuy(PlayState* play, int who) {
    GunId id = who == 0 ? RackGun() : GunId::None;
    if (id == GunId::None) {
        return false;
    }
    PlayerGuns& guns = sGuns[who];
    int slot = SlotOf(guns, id);
    if (slot < 0) {
        guns.Give(id);
        sState[who].reloadFrames = 0;
        sState[who].swapFrames = kSwapFrames / 2; // bring the new gun up
        sState[who].sinceShot = 0;
        Announce(GunName(id, false), 3);
    } else {
        guns.Refill(slot);
    }
    Sfx_PlaySfxCentered(NA_SE_SY_GET_ITEM);
    (void)play;
    return true;
}

int Guns_ChestWeight(int who) {
    return who == 0 ? GunBoxWeight(sGuns[who]) : 0;
}

const char* Guns_ChestGive(PlayState* play, int who, int roll) {
    GunId id = GunBoxPick(sGuns[who], roll);
    if (id == GunId::None) {
        return nullptr;
    }
    sGuns[who].Give(id);
    sState[who].reloadFrames = 0;
    sState[who].swapFrames = kSwapFrames / 2;
    sState[who].sinceShot = 0;
    (void)play;
    return GunName(id, false);
}

bool Guns_PapWanted(int who, bool anyRecency) {
    if (who != 0) {
        return false;
    }
    const GunSlot& slot = sGuns[who].Current();
    if (slot.id == GunId::None || slot.pap) {
        return false;
    }
    // The gun is "in hand" if Link fired it more recently than he held a Zelda item.
    return anyRecency || sState[who].sinceShot < sState[who].sinceItem;
}

const char* Guns_PapName(int who) {
    const GunSlot& slot = sGuns[who].Current();
    return GunName(slot.id, true);
}

void Guns_PackAPunch(PlayState* play, int who) {
    PlayerGuns& guns = sGuns[who];
    guns.PackAPunch(guns.current);
    sState[who].swapFrames = kSwapFrames / 2;
    sState[who].sinceShot = 0;
    (void)play;
}

// ---------------------------------------------------------------------------
// HUD

bool Guns_HudInfo(std::string* name, std::string* ammo, std::string* hint, bool* crosshair) {
    if (!RunLive()) {
        return false;
    }
    const PlayerGuns& guns = sGuns[0];
    const GunSlot& slot = guns.Current();
    if (slot.id == GunId::None) {
        return false;
    }
    const GunState& st = sState[0];
    *name = std::string(GunName(slot.id, slot.pap)) + " [" + GunButtonName() + "]";
    char buf[32];
    snprintf(buf, sizeof(buf), "%d | %d", slot.clip, slot.reserve);
    *ammo = buf;
    hint->clear();
    int clipSize = GunVariantOf(slot.id, slot.pap).clip;
    if (st.reloadFrames > 0) {
        *hint = "RELOADING";
    } else if (slot.clip == 0 && slot.reserve == 0) {
        *hint = "NO AMMO";
    } else if (slot.reserve > 0 && slot.clip * 4 <= clipSize) {
        *hint = std::string(BuyButtonName()) + ": RELOAD";
    }
    *crosshair = gPlayState != nullptr && InAds(GET_PLAYER(gPlayState));
    return true;
}

} // namespace Zombies

// ---------------------------------------------------------------------------
// Link's gun poses and the first-person viewmodel. Outside the namespace with C linkage
// helpers, as the display list macros want.

namespace Zombies {

// Joints the poses drive, in Link's skeleton (jointTable index = limb).
enum PoseJoint {
    POSE_UPPER,
    POSE_L_SHOULDER,
    POSE_L_FOREARM,
    POSE_L_HAND,
    POSE_R_SHOULDER,
    POSE_R_FOREARM,
    POSE_R_HAND,
    POSE_JOINTS,
};

static const u8 kPoseLimbs[POSE_JOINTS] = {
    PLAYER_LIMB_UPPER,      PLAYER_LIMB_L_SHOULDER, PLAYER_LIMB_L_FOREARM, PLAYER_LIMB_L_HAND,
    PLAYER_LIMB_R_SHOULDER, PLAYER_LIMB_R_FOREARM,  PLAYER_LIMB_R_HAND,
};

// A pose: absolute joint rotations (relative to the parent limb, as OoT stores them).
struct ArmPose {
    Vec3s rot[POSE_JOINTS];
};

// Each gun class's poses. Data, not code: an imported Black Ops viewmodel animation can
// fill the same table later. `twoHanded` false leaves the left arm to OoT's animation.
struct ClassPoses {
    bool twoHanded;
    ArmPose aim;    // gun up, pointing where Link faces
    ArmPose sprint; // gun across the chest while running
    ArmPose reload; // gun tipped down, off hand at the magazine
    ArmPose lower;  // gun down by the hip: swaps, draws, Pack-a-Punch
    Vec3s recoil;   // added to the right forearm and hand at full kick
};

// clang-format off
// The aim keyframe is measured from OoT's own item-aiming pose (slingshot, bow and
// hookshot share it): the right arm out at shoulder height, the left hand up by the face
// as a two-handed grip. The rest are built around it. An all-zero torso keeps the torso
// of whatever animation Link is playing. Both ages share the table.
// Joints: torso, L shoulder, L forearm, L hand, R shoulder, R forearm, R hand.
#define POSE(u, ls, lf, lh, rs, rf, rh) { { u, ls, lf, lh, rs, rf, rh } }
#define V(x, y, z) { x, y, z }
static constexpr Vec3s kKeepTorso = { 0, 0, 0 };
static const ClassPoses kAdultPoses[static_cast<int>(GunClass::Count)] = {
    // Pistol: one arm straight out, the other left to the animation.
    { false,
      POSE(V(10725, -1772, -1637), V(0, 0, 0), V(0, 0, 0), V(0, 0, 0),
           V(-21800, -1887, -29760), V(-11046, -9944, 7461), V(14449, 0, 0)),
      POSE(kKeepTorso, V(0, 0, 0), V(0, 0, 0), V(0, 0, 0),
           V(-16500, -1887, -14000), V(-12000, -6000, 12000), V(14449, 0, 0)),
      POSE(V(9000, -1772, -1637), V(0, 0, 0), V(0, 0, 0), V(0, 0, 0),
           V(-19000, -1887, -21000), V(-11046, -9944, 16000), V(14449, 0, 0)),
      POSE(kKeepTorso, V(0, 0, 0), V(0, 0, 0), V(0, 0, 0),
           V(-15000, -1887, -12000), V(-12000, -9944, 6000), V(14449, 0, 0)),
      V(0, 0, 2400) },
    // SMG: two hands, held at the chest.
    { true,
      POSE(V(10725, -1772, -1637), V(19902, 0, 0), V(0, 17724, -1705), V(10903, 0, 0),
           V(-21800, -1887, -26000), V(-11046, -9944, 11000), V(14449, 0, 0)),
      POSE(kKeepTorso, V(19902, 0, 0), V(0, 12000, -8000), V(10903, 0, 0),
           V(-16500, -1887, -14000), V(-12000, -6000, 14000), V(14449, 0, 0)),
      POSE(V(9000, -1772, -1637), V(19902, 0, 0), V(0, 9000, -9000), V(10903, 0, 0),
           V(-19000, -1887, -21000), V(-11046, -9944, 16000), V(14449, 0, 0)),
      POSE(kKeepTorso, V(19902, 0, 0), V(0, 9000, -6000), V(10903, 0, 0),
           V(-15000, -1887, -12000), V(-12000, -9944, 6000), V(14449, 0, 0)),
      V(0, 0, 1400) },
    // Rifle: two hands, shouldered, the barrel high.
    { true,
      POSE(V(10725, -1772, -1637), V(19902, 0, 0), V(0, 17724, -1705), V(10903, 0, 0),
           V(-21800, -1887, -27500), V(-11046, -9944, 9500), V(14449, 0, 0)),
      POSE(kKeepTorso, V(19902, 0, 0), V(0, 12000, -8000), V(10903, 0, 0),
           V(-16500, -1887, -14000), V(-12000, -6000, 14000), V(14449, 0, 0)),
      POSE(V(9000, -1772, -1637), V(19902, 0, 0), V(0, 9000, -9000), V(10903, 0, 0),
           V(-19000, -1887, -21000), V(-11046, -9944, 16000), V(14449, 0, 0)),
      POSE(kKeepTorso, V(19902, 0, 0), V(0, 9000, -6000), V(10903, 0, 0),
           V(-15000, -1887, -12000), V(-12000, -9944, 6000), V(14449, 0, 0)),
      V(0, 0, 1800) },
    // Shotgun: two hands, a big kick and a pump.
    { true,
      POSE(V(10725, -1772, -1637), V(19902, 0, 0), V(0, 17724, -1705), V(10903, 0, 0),
           V(-21800, -1887, -27000), V(-11046, -9944, 10000), V(14449, 0, 0)),
      POSE(kKeepTorso, V(19902, 0, 0), V(0, 12000, -8000), V(10903, 0, 0),
           V(-16500, -1887, -14000), V(-12000, -6000, 14000), V(14449, 0, 0)),
      POSE(V(9000, -1772, -1637), V(19902, 0, 0), V(0, 9000, -9000), V(10903, 0, 0),
           V(-19000, -1887, -21000), V(-11046, -9944, 16000), V(14449, 0, 0)),
      POSE(kKeepTorso, V(19902, 0, 0), V(0, 9000, -6000), V(10903, 0, 0),
           V(-15000, -1887, -12000), V(-12000, -9944, 6000), V(14449, 0, 0)),
      V(0, 0, 4000) },
    // LMG: two hands, carried low at the hip.
    { true,
      POSE(V(8000, -1772, -1637), V(19902, 0, 0), V(0, 14000, -5000), V(10903, 0, 0),
           V(-20000, -1887, -22000), V(-11046, -9944, 14000), V(14449, 0, 0)),
      POSE(kKeepTorso, V(19902, 0, 0), V(0, 12000, -8000), V(10903, 0, 0),
           V(-16500, -1887, -14000), V(-12000, -6000, 14000), V(14449, 0, 0)),
      POSE(V(8000, -1772, -1637), V(19902, 0, 0), V(0, 9000, -9000), V(10903, 0, 0),
           V(-18000, -1887, -18000), V(-11046, -9944, 16000), V(14449, 0, 0)),
      POSE(kKeepTorso, V(19902, 0, 0), V(0, 9000, -6000), V(10903, 0, 0),
           V(-15000, -1887, -12000), V(-12000, -9944, 6000), V(14449, 0, 0)),
      V(0, 0, 1000) },
    // Ray Gun: one hand, like the pistol, with a softer kick.
    { false,
      POSE(V(10725, -1772, -1637), V(0, 0, 0), V(0, 0, 0), V(0, 0, 0),
           V(-21800, -1887, -29760), V(-11046, -9944, 7461), V(14449, 0, 0)),
      POSE(kKeepTorso, V(0, 0, 0), V(0, 0, 0), V(0, 0, 0),
           V(-16500, -1887, -14000), V(-12000, -6000, 12000), V(14449, 0, 0)),
      POSE(V(9000, -1772, -1637), V(0, 0, 0), V(0, 0, 0), V(0, 0, 0),
           V(-19000, -1887, -21000), V(-11046, -9944, 16000), V(14449, 0, 0)),
      POSE(kKeepTorso, V(0, 0, 0), V(0, 0, 0), V(0, 0, 0),
           V(-15000, -1887, -12000), V(-12000, -9944, 6000), V(14449, 0, 0)),
      V(0, 0, 1600) },
};
#undef POSE
#undef V
// clang-format on

static ClassPoses sPoses[2][static_cast<int>(GunClass::Count)]; // [adult, child]
static bool sPosesReady = false;

static void InitPoses();

static s16 LerpAngle(s16 from, s16 to, float t) {
    return static_cast<s16>(from + static_cast<s16>(to - from) * t);
}

static ArmPose BlendPose(const ArmPose& a, const ArmPose& b, float t) {
    ArmPose out;
    for (int j = 0; j < POSE_JOINTS; j++) {
        out.rot[j].x = LerpAngle(a.rot[j].x, b.rot[j].x, t);
        out.rot[j].y = LerpAngle(a.rot[j].y, b.rot[j].y, t);
        out.rot[j].z = LerpAngle(a.rot[j].z, b.rot[j].z, t);
    }
    // A zero torso means "the animation's": blend to or from it by keeping the other's.
    const Vec3s& ua = a.rot[POSE_UPPER];
    const Vec3s& ub = b.rot[POSE_UPPER];
    if (ub.x == 0 && ub.y == 0 && ub.z == 0) {
        out.rot[POSE_UPPER] = ua;
    } else if (ua.x == 0 && ua.y == 0 && ua.z == 0) {
        out.rot[POSE_UPPER] = ub;
    }
    return out;
}

static float Smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The pose for this frame from the gun's state: aim, sprint, reload and swap keyframes,
// plus recoil.
static ArmPose CurrentPose(Player* player, const GunStats& gun) {
    if (!sPosesReady) {
        InitPoses();
    }
    const ClassPoses& poses = sPoses[LINK_IS_ADULT ? 0 : 1][static_cast<int>(gun.gunClass)];
    const GunState& st = sState[0];
    ArmPose pose = poses.aim;

    bool sprinting = player->actor.speedXZ > 6.0f && st.sinceShot > 10 && player->focusActor == nullptr;
    if (sprinting) {
        pose = poses.sprint;
    }
    if (st.reloadFrames > 0 && st.reloadTotal > 0) {
        // Down to the reload pose, hold, and back up: 25% / 50% / 25% of the reload.
        float t = 1.0f - static_cast<float>(st.reloadFrames) / st.reloadTotal;
        float w = t < 0.25f ? Smooth(t / 0.25f) : t < 0.75f ? 1.0f : Smooth((1.0f - t) / 0.25f);
        pose = BlendPose(pose, poses.reload, w);
    }
    if (st.swapFrames > 0) {
        float t = static_cast<float>(st.swapFrames) / kSwapFrames; // 1 -> 0
        float w = t > 0.5f ? Smooth((1.0f - t) * 2.0f) : Smooth(t * 2.0f);
        pose = BlendPose(pose, poses.lower, w);
    }
    if (st.recoil > 0.0f) {
        float k = st.recoil;
        pose.rot[POSE_R_FOREARM].x = static_cast<s16>(pose.rot[POSE_R_FOREARM].x + poses.recoil.x * k);
        pose.rot[POSE_R_FOREARM].y = static_cast<s16>(pose.rot[POSE_R_FOREARM].y + poses.recoil.y * k);
        pose.rot[POSE_R_FOREARM].z = static_cast<s16>(pose.rot[POSE_R_FOREARM].z + poses.recoil.z * k);
    }
    return pose;
}

// What the draw changed, to put back afterwards.
static Vec3s sSavedJoints[POSE_JOINTS];
static Gfx** sSavedRightHandDLists = nullptr;
static Gfx** sSavedLeftHandDLists = nullptr;
static u8 sSavedRightHandType = 0;
static u8 sSavedLeftHandType = 0;
static bool sPosed = false;

// Stand-in guns: the hookshot (adult) or slingshot (child) in Link's right hand, his left
// fist closed round the foregrip. [adult near, child near, adult far, child far]
static Gfx* sGunHandDLs[4];
static Gfx* sGripHandDLs[4];
static Gfx* sFistHandDLs[4]; // his right fist, round an imported gun's grip
static GunModel* sHeldGun = nullptr;
static GunClass sHeldClass = GunClass::Pistol;
static constexpr float kViewmodelDepth = 2.0f;
static float sHoldTune[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 16384.0f }; // ZZTUNE

static void InitHandDLs() {
    sGunHandDLs[0] = (Gfx*)gLinkAdultRightHandHoldingHookshotNearDL;
    sGunHandDLs[1] = (Gfx*)gLinkChildRightHandHoldingSlingshotNearDL;
    sGunHandDLs[2] = (Gfx*)gLinkAdultRightHandHoldingHookshotNearDL;
    sGunHandDLs[3] = (Gfx*)gLinkChildRightHandHoldingSlingshotNearDL;
    sGripHandDLs[0] = (Gfx*)gLinkAdultLeftHandClosedNearDL;
    sGripHandDLs[1] = (Gfx*)gLinkChildLeftFistNearDL;
    sGripHandDLs[2] = (Gfx*)gLinkAdultLeftHandClosedNearDL;
    sGripHandDLs[3] = (Gfx*)gLinkChildLeftFistNearDL;
    sFistHandDLs[0] = (Gfx*)gLinkAdultRightHandClosedNearDL;
    sFistHandDLs[1] = (Gfx*)gLinkChildRightHandClosedNearDL;
    sFistHandDLs[2] = (Gfx*)gLinkAdultRightHandClosedFarDL;
    sFistHandDLs[3] = (Gfx*)gLinkChildRightHandClosedFarDL;
}

static bool GunRaised(PlayState* play, Player* player) {
    const GunState& st = sState[0];
    if (sGuns[0].Current().id == GunId::None || GunBlocked(play, player) || !HoldsSword(player)) {
        return false;
    }
    return sGunHeld || st.sinceShot < kRaiseFrames || st.reloadFrames > 0 || st.swapFrames > 0;
}

static void PoseLink() {
    { // ZZTUNE
        const char* e = getenv("ZZ_HOLD");
        if (e != nullptr) {
            sscanf(e, "%f,%f,%f,%f,%f,%f", &sHoldTune[0], &sHoldTune[1], &sHoldTune[2], &sHoldTune[3], &sHoldTune[4], &sHoldTune[5]);
        }
    }
    sPosed = false;
    sHeldGun = nullptr;
    PlayState* play = gPlayState;
    if (play == nullptr || !RunLive()) {
        return;
    }
    Player* player = GET_PLAYER(play);
    GunState& st = sState[0];
    bool raised = GunRaised(play, player);
    st.raise = raised ? std::min(1.0f, st.raise + 0.34f) : std::max(0.0f, st.raise - 0.2f);
    if (st.raise <= 0.0f || player->unk_6AD != 0) {
        return;
    }
    const GunSlot& slot = sGuns[0].Current();
    const GunStats& gun = GunInfo(slot.id);
    ArmPose pose = CurrentPose(player, gun);
    const ClassPoses& poses = sPoses[LINK_IS_ADULT ? 0 : 1][static_cast<int>(gun.gunClass)];
    float w = Smooth(st.raise);

    for (int j = 0; j < POSE_JOINTS; j++) {
        Vec3s& joint = player->jointTable[kPoseLimbs[j]];
        sSavedJoints[j] = joint;
        bool leftArm = j == POSE_L_SHOULDER || j == POSE_L_FOREARM || j == POSE_L_HAND;
        if (leftArm && !poses.twoHanded) {
            continue;
        }
        const Vec3s& target = pose.rot[j];
        if (j == POSE_UPPER && target.x == 0 && target.y == 0 && target.z == 0) {
            continue; // keep the animation's torso
        }
        joint.x = LerpAngle(joint.x, target.x, w);
        joint.y = LerpAngle(joint.y, target.y, w);
        joint.z = LerpAngle(joint.z, target.z, w);
    }

    sSavedRightHandDLists = player->rightHandDLists;
    sSavedLeftHandDLists = player->leftHandDLists;
    sSavedRightHandType = player->rightHandType;
    sSavedLeftHandType = player->leftHandType;
    GunModel* gm = Guns_ImportedModel(gun.boName);
    sHeldGun = nullptr;
    if (w > 0.5f) {
        if (sGunHandDLs[0] == nullptr) {
            InitHandDLs();
        }
        if (gm != nullptr && gm->world != nullptr) {
            // The imported gun goes into his closed fist as the hand is drawn (HoldGun).
            player->rightHandDLists = &sFistHandDLs[gSaveContext.linkAge];
            player->rightHandType = PLAYER_MODELTYPE_RH_CLOSED;
            sHeldGun = gm;
            sHeldClass = gun.gunClass;
        } else {
            player->rightHandDLists = &sGunHandDLs[gSaveContext.linkAge];
            player->rightHandType = LINK_IS_ADULT ? PLAYER_MODELTYPE_RH_HOOKSHOT : PLAYER_MODELTYPE_RH_BOW_SLINGSHOT;
        }
        if (poses.twoHanded && HoldsSword(player) && player->heldItemAction == PLAYER_IA_NONE) {
            player->leftHandDLists = &sGripHandDLs[gSaveContext.linkAge];
            player->leftHandType = PLAYER_MODELTYPE_LH_CLOSED;
        }
    }
    sPosed = true;
}

static void UnposeLink() {
    if (!sPosed || gPlayState == nullptr) {
        return;
    }
    sPosed = false;
    sHeldGun = nullptr;
    Player* player = GET_PLAYER(gPlayState);
    for (int j = 0; j < POSE_JOINTS; j++) {
        player->jointTable[kPoseLimbs[j]] = sSavedJoints[j];
    }
    player->rightHandDLists = sSavedRightHandDLists;
    player->leftHandDLists = sSavedLeftHandDLists;
    player->rightHandType = sSavedRightHandType;
    player->leftHandType = sSavedLeftHandType;
}

} // namespace Zombies

// The first-person viewmodel: Link's forearm and gun hand in the lower right of the view,
// swaying while he walks, kicking on each shot and dipping for reloads and swaps.
extern "C" void ZombiesGuns_DrawViewmodel(PlayState* play, Player* player, float bob, float kick, float dip,
                                          float sway) {
    using namespace Zombies;
    const View& view = play->view;
    Vec3f eye = view.eye;
    Vec3f at = view.lookAt;
    s16 yaw = Math_Vec3f_Yaw(&eye, &at);
    s16 pitch = static_cast<s16>(-Math_Vec3f_Pitch(&eye, &at));
    bool adult = LINK_IS_ADULT;

    FrameInterpolation_RecordOpenChild(&sGuns[0], 0);
    // Light it the way Link is lit, as an actor's draw would.
    Lights* lights = LightContext_NewLights(&play->lightCtx, play->state.gfxCtx);
    Lights_BindAll(lights, play->lightCtx.listHead, &player->actor.world.pos);
    Lights_Draw(lights, play->state.gfxCtx);
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x0C, reinterpret_cast<uintptr_t>(gCullBackDList)); // as Link's own draw sets it
    gDPSetEnvColor(POLY_OPA_DISP++, 30, 105, 27, 0); // the tunic's green, for the sleeve

    Matrix_Translate(eye.x, eye.y, eye.z, MTXMODE_NEW);
    Matrix_RotateY(static_cast<f32>(BINANG_TO_RAD(yaw)), MTXMODE_APPLY);
    Matrix_RotateX(static_cast<f32>(BINANG_TO_RAD(pitch)), MTXMODE_APPLY);
    // View space from here: +Z forward, +X left, +Y up.
    // Placement: right of and below the sights, clear of the HUD's lower-right corner.
    float right = adult ? -4.5f : -4.0f;
    float down = (adult ? -3.8f : -3.2f) + bob - dip * 6.0f;
    float forward = (adult ? 20.0f : 18.0f) - kick * 2.5f;
    Matrix_Translate(right + sway, down, forward, MTXMODE_APPLY);
    Matrix_RotateY(static_cast<f32>(BINANG_TO_RAD(0x3E8)), MTXMODE_APPLY); // angled in toward the sights
    Matrix_RotateX(static_cast<f32>(BINANG_TO_RAD(static_cast<s16>(-kick * 1800.0f + dip * 5000.0f))), MTXMODE_APPLY);
    // The hand model's +Y runs down the barrel: turn it to point into the view, then roll
    // it upright.
    Matrix_RotateX(static_cast<f32>(BINANG_TO_RAD(0x4000)), MTXMODE_APPLY);
    Matrix_RotateY(static_cast<f32>(BINANG_TO_RAD(0x4000)), MTXMODE_APPLY);
    float scale = adult ? 0.0055f : 0.0065f;
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    // Link's hands are skinned to his forearm: their wrist vertices load the limb matrices
    // in segment 0x0D. Every limb gets the hand's matrix, so the wrist stays on the hand.
    Mtx* limbMtx = static_cast<Mtx*>(Graph_Alloc(play->state.gfxCtx, PLAYER_LIMB_MAX * sizeof(Mtx)));
    for (int i = 0; i < PLAYER_LIMB_MAX; i++) {
        Matrix_ToMtx(&limbMtx[i], const_cast<char*>(__FILE__), __LINE__);
    }
    gSPSegment(POLY_OPA_DISP++, 0x0D, reinterpret_cast<uintptr_t>(limbMtx));
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, adult ? (Gfx*)gLinkAdultRightHandHoldingHookshotNearDL
                                          : (Gfx*)gLinkChildRightHandHoldingSlingshotNearDL);

    CLOSE_DISPS(play->state.gfxCtx);
    FrameInterpolation_RecordCloseChild();
}

static f32 BinangRad(float binang) {
    return binang * (3.14159265f / 32768.0f);
}

// The imported viewmodel: Black Ops' own arms and gun, posed by its animations and drawn
// from the eye as Black Ops draws it (its model space is the camera's). scale takes its
// inches to OoT units, and further out than OoT's near plane. Returns the muzzle's
// world position in flash.
extern "C" void ZombiesGuns_DrawImportedViewmodel(PlayState* play, Player* player, Gfx* dl, float scale,
                                                  const Vec3f* muzzle, Vec3f* flash, float bob, float sway) {
    using namespace Zombies;
    const View& view = play->view;
    Vec3f eye = view.eye;
    Vec3f at = view.lookAt;
    s16 yaw = Math_Vec3f_Yaw(&eye, &at);
    s16 pitch = static_cast<s16>(-Math_Vec3f_Pitch(&eye, &at));

    FrameInterpolation_RecordOpenChild(&sGuns[0], 1);
    Lights* lights = LightContext_NewLights(&play->lightCtx, play->state.gfxCtx);
    Lights_BindAll(lights, play->lightCtx.listHead, &player->actor.world.pos);
    Lights_Draw(lights, play->state.gfxCtx);
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    Matrix_Translate(eye.x, eye.y, eye.z, MTXMODE_NEW);
    Matrix_RotateY(BinangRad(yaw), MTXMODE_APPLY);
    Matrix_RotateX(BinangRad(pitch), MTXMODE_APPLY);
    // View space: +Z forward, +X left, +Y up. Black Ops' model space has +X forward and
    // +Z right: a quarter turn.
    Matrix_Translate(sway, bob, 0.0f, MTXMODE_APPLY);
    Matrix_RotateY(BinangRad(-0x4000), MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    Matrix_MultVec3f(const_cast<Vec3f*>(muzzle), flash);
    float vs = 1.0f / Model_VertexScale();
    Matrix_Scale(vs, vs, vs, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, dl);

    CLOSE_DISPS(play->state.gfxCtx);
    FrameInterpolation_RecordCloseChild();
}

// Link's right fist with the imported gun in it: the hand's display list, then the gun
// placed in the hand's space (where OoT's limb units are a hundredth of the world's).
extern "C" Gfx* ZombiesGuns_FistWithGun(PlayState* play, Gfx* hand, Gfx* gun, const Vec3f* grip, float scale,
                                        const Vec3f* offset, const Vec3s* rot) {
    Gfx* dl = static_cast<Gfx*>(Graph_Alloc(play->state.gfxCtx, 6 * sizeof(Gfx)));
    Mtx* mtx = static_cast<Mtx*>(Graph_Alloc(play->state.gfxCtx, sizeof(Mtx)));
    Matrix_Push();
    Matrix_Translate(offset->x, offset->y, offset->z, MTXMODE_NEW);
    Matrix_RotateZ(BinangRad(rot->z), MTXMODE_APPLY);
    Matrix_RotateY(BinangRad(rot->y), MTXMODE_APPLY);
    Matrix_RotateX(BinangRad(rot->x), MTXMODE_APPLY);
    float s = scale / Zombies::Model_VertexScale();
    Matrix_Scale(s, s, s, MTXMODE_APPLY);
    float vs = Zombies::Model_VertexScale();
    Matrix_Translate(-grip->x * vs, -grip->y * vs, -grip->z * vs, MTXMODE_APPLY);
    Matrix_ToMtx(mtx, const_cast<char*>(__FILE__), __LINE__);
    Matrix_Pop();
    Gfx* p = dl;
    gSPDisplayList(p++, hand);
    gSPMatrix(p++, mtx, G_MTX_PUSH | G_MTX_MUL | G_MTX_MODELVIEW);
    gSPDisplayList(p++, gun);
    gSPPopMatrix(p++, G_MTX_MODELVIEW);
    gSPEndDisplayList(p++);
    return dl;
}

namespace Zombies {

// Where each imported world gun's grip is, from its own tag_weapon (in inches, Black
// Ops' axes as imported: +X forward, +Y up).
struct GunGrip {
    const char* boName;
    float x, y;
};
static constexpr GunGrip kGrips[] = {
    { "m1911_zm", -11.5f, -2.5f }, { "ray_gun_zm", -10.5f, -2.0f }, { "rottweil72_zm", -12.0f, -2.5f },
    { "ithaca_zm", -10.0f, -2.5f }, { "m14_zm", -8.0f, -2.5f },    { "commando_zm", -7.0f, -2.5f },
    { "famas_zm", -5.5f, -2.5f },   { "hk21_zm", -7.0f, -2.5f },   { "mp40_zm", -4.5f, -2.0f },
    { "spectre_zm", -8.0f, -2.5f },
};

static Vec3f GripOf(const char* boName) {
    for (const GunGrip& g : kGrips) {
        if (std::strcmp(g.boName, boName) == 0) {
            return { g.x, g.y, 0.0f };
        }
    }
    return { -5.5f, -2.5f, 0.0f }; // most rifles and SMGs
}

// The limb draw: the held imported gun goes into Link's right fist.
static void HoldGun(s32 limbIndex, Gfx** dList, Player* player, PlayState* play) {
    if (limbIndex != PLAYER_LIMB_R_HAND || sHeldGun == nullptr || *dList == nullptr || player != GET_PLAYER(play) ||
        player->unk_6AD != 0) {
        return;
    }
    Gfx* gun = GunWorldDl(sHeldGun);
    if (gun == nullptr) {
        return;
    }
    Vec3f grip = GripOf(GunInfo(sGuns[0].Current().id).boName);
    // Black Ops inches to OoT units, times the limb's hundred; Link the child is smaller.
    float scale = (64.0f / 72.0f) * 100.0f * (LINK_IS_ADULT ? 1.0f : 0.8f);
    Vec3f offset = { sHoldTune[0], sHoldTune[1], sHoldTune[2] };
    Vec3s rot = { static_cast<s16>(sHoldTune[3]), static_cast<s16>(sHoldTune[4]), static_cast<s16>(sHoldTune[5]) };
    *dList = ZombiesGuns_FistWithGun(play, *dList, gun, &grip, scale, &offset, &rot);
}

// The imported viewmodel's pose: pullout (backwards to put a gun away), reload, fire or
// idle, timed off the gun's state.
static void PoseViewmodel(GunModel* gm, ModelPose* pose) {
    const GunState& st = sState[0];
    float sec = static_cast<float>(sFrame) / kFps;
    AnimLayer layer;
    if (gm->idle != nullptr) {
        float len = Anim_Length(gm->idle);
        layer = { gm->idle, len > 0.0f ? std::fmod(sec, len) : 0.0f };
    }
    if (st.swapFrames > 0 && gm->draw != nullptr) {
        float t = static_cast<float>(st.swapFrames) / kSwapFrames; // 1 to 0; the gun changes at half
        float len = Anim_Length(gm->draw);
        layer = { gm->draw, len * (t > 0.5f ? (t - 0.5f) * 2.0f : 1.0f - t * 2.0f) };
    } else if (st.reloadFrames > 0 && st.reloadTotal > 0 && gm->reload != nullptr) {
        float done = static_cast<float>(st.reloadTotal - st.reloadFrames) / kFps;
        float len = Anim_Length(gm->reload);
        if (Anim_Looping(gm->reload)) {
            layer = { gm->reload, len > 0.0f ? std::fmod(done, len) : 0.0f };
        } else {
            layer = { gm->reload, done * gm->reloadEnd * kFps / static_cast<float>(st.reloadTotal) };
        }
    } else if (gm->fire != nullptr && static_cast<float>(st.sinceShot) / kFps < Anim_Length(gm->fire)) {
        layer = { gm->fire, static_cast<float>(st.sinceShot) / kFps };
    }
    Model_Pose(gm->view, layer, AnimLayer(), 0.0f, pose);
}

static void DrawImportedViewmodel(PlayState* play, Player* player, GunModel* gm, float bob, float sway) {
    static ModelPose sPose;
    PoseViewmodel(gm, &sPose);
    Gfx* dl = Model_PosedDl(gm->view, sPose, &gm->viewBuffers, &ViewStyle());
    if (dl == nullptr) {
        return;
    }
    Vec3f muzzle = { 20.0f, -6.0f, 0.0f };
    Model_BonePos(gm->view, sPose, "tag_flash", &muzzle);
    Vec3f flash;
    // Twice as far and twice the size looks the same, and keeps clear of the near plane.
    float scale = (64.0f / 72.0f) * kViewmodelDepth;
    ZombiesGuns_DrawImportedViewmodel(play, player, dl, scale, &muzzle, &flash, bob * 0.5f, sway * 0.5f);
    { // ZZTUNE
        Vec3f w = {}, e = play->view.eye;
        Model_BonePos(gm->view, sPose, "tag_weapon", &w);
        printf("ZZVM f=%d muzzle=(%.1f,%.1f,%.1f) weapon=(%.1f,%.1f,%.1f) flash-eye=(%.1f,%.1f,%.1f) skin0=(%.2f %.2f %.2f)\n", sFrame, muzzle.x, muzzle.y, muzzle.z, w.x, w.y, w.z, flash.x - e.x, flash.y - e.y, flash.z - e.z, sPose.skin[0].tx, sPose.skin[0].ty, sPose.skin[0].tz); fflush(stdout);
    }
    if (sState[0].recoil > 0.8f) {
        MuzzleFlash(play, flash, GunInfo(sGuns[0].Current().id).projectile);
    }
}

static void DrawViewmodel() {
    PlayState* play = gPlayState;
    if (play == nullptr || !RunLive() || Coop_SplitView()) {
        return;
    }
    Player* player = GET_PLAYER(play);
    if (!InAds(player)) {
        return;
    }
    const GunState& st = sState[0];
    static float sWalk = 0.0f;
    sWalk += player->actor.speedXZ * 0.35f;
    float bob = std::sin(sWalk) * std::min(player->actor.speedXZ, 4.0f) * 0.25f;
    float sway = std::cos(sWalk * 0.5f) * std::min(player->actor.speedXZ, 4.0f) * 0.15f +
                 std::sin(static_cast<float>(sFrame) * 0.05f) * 0.15f; // idle breathing
    GunModel* gm = Guns_ImportedModel(GunInfo(sGuns[0].Current().id).boName);
    if (gm != nullptr && gm->view != nullptr) {
        DrawImportedViewmodel(play, player, gm, bob, sway);
        return;
    }
    float dip = 0.0f;
    if (st.reloadFrames > 0 && st.reloadTotal > 0) {
        float t = 1.0f - static_cast<float>(st.reloadFrames) / st.reloadTotal;
        dip = t < 0.25f ? Smooth(t / 0.25f) : t < 0.75f ? 1.0f : Smooth((1.0f - t) / 0.25f);
    }
    if (st.swapFrames > 0) {
        float t = static_cast<float>(st.swapFrames) / kSwapFrames;
        dip = std::max(dip, t > 0.5f ? Smooth((1.0f - t) * 2.0f) : Smooth(t * 2.0f));
    }
    ZombiesGuns_DrawViewmodel(play, player, bob, st.recoil, dip, sway);
    if (st.recoil > 0.8f) {
        // Muzzle flash at the end of the viewmodel's barrel, right of the sights.
        s16 yaw = player->actor.focus.rot.y;
        Vec3f dir = DirFromAngles(player->actor.focus.rot.x, yaw);
        Vec3f right = { -Math_CosS(yaw), 0.0f, Math_SinS(yaw) };
        Vec3f eye = play->view.eye;
        Vec3f flash = { eye.x + dir.x * 34.0f + right.x * 5.0f, eye.y + dir.y * 34.0f - 1.5f,
                        eye.z + dir.z * 34.0f + right.z * 5.0f };
        MuzzleFlash(play, flash, GunInfo(sGuns[0].Current().id).projectile);
    }
}

static void InitPoses() {
    for (int c = 0; c < static_cast<int>(GunClass::Count); c++) {
        // One table for both ages for now: Link's child skeleton has the same limbs.
        const ClassPoses& src = kAdultPoses[std::min(c, static_cast<int>(std::size(kAdultPoses)) - 1)];
        sPoses[0][c] = src;
        sPoses[1][c] = src;
    }
    sPosesReady = true;
}

// ---------------------------------------------------------------------------
// Input and first-person movement

static void OnMainStart() {
    PlayState* play = gPlayState;
    sGunPressed = false;
    sGunHeld = false;
    if (play == nullptr || !RunLive() || GET_PLAYER(play) == nullptr) {
        sAdsActive = false;
        return;
    }
    Player* player = GET_PLAYER(play);
    if (player->naviTextId > 0) {
        // C-Up aims during a run. Navi's hints would take it over (her forced calls, the
        // negative ids, still come through).
        player->naviTextId = 0;
    }
    Input& input = play->state.input[0];
    u16 mask = GunButtonMask();
    sGunPressed = CHECK_BTN_ALL(input.press.button, mask);
    sGunHeld = CHECK_BTN_ALL(input.cur.button, mask);
    if (sGuns[0].Current().id != GunId::None) {
        // The gun's button is the mode's alone during a run.
        input.press.button &= static_cast<u16>(~mask);
        input.cur.button &= static_cast<u16>(~mask);
    }
    sAdsActive = InAds(player);
    sAdsStickY = 0;
    if (sAdsActive && !CVarGetInteger(CVAR_SETTING("Controls.RightStickAim"), 0)) {
        // No right stick to look with: the stick turns the view left and right as usual,
        // and its forward/back walks instead of tilting the view.
        sAdsStickY = input.rel.stick_y;
        input.rel.stick_y = 0;
        input.cur.stick_y = 0;
    }
}

static bool SohMovesInFirstPerson() {
    return CVarGetInteger(CVAR_SETTING("MoveInFirstPerson"), 0) && CVarGetInteger(CVAR_SETTING("Controls.RightStickAim"), 0);
}

// Walking while aiming, at about 60% of Link's walk, through his normal movement so walls
// still stop him.
static void AdsMove(Player* player) {
    f32 speedTarget = 0.0f;
    s16 yawTarget = player->actor.shape.rot.y;
    bool moving;
    if (CVarGetInteger(CVAR_SETTING("Controls.RightStickAim"), 0)) {
        u8 mode = player->unk_6AD;
        player->unk_6AD = 0; // normal movement ignores the stick while Link is in first person
        moving = Player_GetMovementSpeedAndYaw(player, &speedTarget, &yawTarget, 0.018f, gPlayState);
        player->unk_6AD = mode;
    } else {
        float stick = std::clamp(static_cast<float>(sAdsStickY) / 60.0f, -1.0f, 1.0f);
        moving = std::fabs(stick) > 0.15f;
        speedTarget = std::fabs(stick) * R_RUN_SPEED_LIMIT / 100.0f;
        yawTarget = static_cast<s16>(player->actor.focus.rot.y + (stick < 0.0f ? 0x8000 : 0));
    }
    speedTarget *= kAdsSpeedScale;
    if (moving) {
        player->yaw = yawTarget;
    }
    func_8083DF68(player, speedTarget, player->yaw);
    if (player->linearVelocity > 0.0f) {
        func_8084029C(player, (REG(30) / 1000.0f) + ((REG(32) / 1000.0f) * player->linearVelocity));
    }
}

static void RegisterGuns() {
    bool enabled = CVarGetInteger(CVAR_ZOMBIES("Enabled"), 0) || CVarGetInteger(CVAR_ZOMBIES("ZombiesOnly"), 1);

    COND_HOOK(OnGameStateMainStart, enabled, OnMainStart);
    COND_HOOK(OnPlayDrawBegin, enabled, PoseLink);
    COND_HOOK(OnPlayDrawEnd, enabled, [] {
        UnposeLink();
        DrawViewmodel();
    });

    COND_VB_SHOULD(VB_PLAYER_OVERRIDE_LIMB_DRAW, enabled, {
        s32 limbIndex = va_arg(args, s32);
        Gfx** dList = va_arg(args, Gfx**);
        Player* player = static_cast<Player*>(va_arg(args, void*));
        PlayState* play = va_arg(args, PlayState*);
        HoldGun(limbIndex, dList, player, play);
    });
    COND_VB_SHOULD(VB_PLAYER_AIM_WITH_LEFT_STICK, enabled, {
        if (sAdsActive && CVarGetInteger(CVAR_SETTING("Controls.RightStickAim"), 0)) {
            *should = false; // left stick walks, right stick aims
        }
    });
    COND_VB_SHOULD(VB_PLAYER_FIRST_PERSON_DECELERATE, enabled, {
        Player* player = va_arg(args, Player*);
        if (sAdsActive && !SohMovesInFirstPerson()) {
            AdsMove(player);
            *should = false;
        }
    });
    COND_VB_SHOULD(VB_PLAYER_MODIFY_FIRST_PERSON_SPEED, enabled, {
        (void)va_arg(args, Player*);
        f32* speed = va_arg(args, f32*);
        if (sAdsActive) {
            *speed *= kAdsSpeedScale; // SoH's own first-person walking, slowed to aiming pace
        }
    });
    COND_VB_SHOULD(VB_PLAYER_FIRST_PERSON_ALIGN_YAW, enabled, {
        Player* player = va_arg(args, Player*);
        if (sAdsActive && player->linearVelocity != 0.0f) {
            *should = false; // keep walking where the stick says, not where Link looks
        }
    });
}

static RegisterShipInitFunc gunsInit(RegisterGuns, { CVAR_ZOMBIES("Enabled"), CVAR_ZOMBIES("ZombiesOnly") });

} // namespace Zombies
