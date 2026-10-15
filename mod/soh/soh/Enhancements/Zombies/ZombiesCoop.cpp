// Zombies of Time: split-screen co-op.
//
// Player 2 is a second Link on the second controller. Ocarina of Time only knows one
// Link, so player 2 is an actor of our own that draws Link's skeleton through the
// game's Link drawing code, moves with his own stick and camera, and swings a sword
// with its own hitbox. Player 2 has their own health, points, perks and weapons
// (sword, bombs); player 1 keeps the save's inventory.
//
// The screen is split top and bottom, like Black Ops: the game draws player 1's view
// into the top half, then this file draws the scene again from player 2's camera into
// the bottom half.
//
// Downs work like Black Ops co-op: a player who loses all their health goes down and
// bleeds out after 45 seconds unless the other holds the buy button next to them for
// 3 seconds. A player who bleeds out comes back at the start of the next round. The
// run ends when nobody is left standing.

#include "ZombiesMode.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "soh/ActorDB.h"

extern "C" {
#include "objects/gameplay_keep/gameplay_keep.h"
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "functions.h"
#include "macros.h"
#include "seqcmd.h"
#include "sequence.h"
#include "variables.h"
#include "soh/frame_interpolation.h"
extern PlayState* gPlayState;
void func_800AA550(View* view);
extern u32 D_80116068[ACTORCAT_MAX]; // which of Link's states freeze each actor category
void View_ViewportToVp(Vp* dest, Viewport* src);
}

// Display list macros need C linkage, like the HUD's: these sit outside the namespace.
extern "C" void ZombiesCoop_SetLinkSegments(PlayState* play, void* linkObject) {
    OPEN_DISPS(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x06, (uintptr_t)linkObject);
    gSPSegment(POLY_XLU_DISP++, 0x06, (uintptr_t)linkObject);
    gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    gSPSegment(POLY_XLU_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    CLOSE_DISPS(play->state.gfxCtx);
}

namespace Zombies {

static constexpr int kFps = 20;
static constexpr int kBleedOutFrames = 45 * kFps;
static constexpr int kReviveFrames = 3 * kFps;
static constexpr float kReviveRange = 80.0f;
static constexpr int kBaseHealth = 48;      // three hearts, like Link's starting life
static constexpr int kJuggernogHealth = 80; // five hearts
static constexpr int kSwipeFrames = 24; // an undead swipes at most this often
static constexpr float kSwipeReach = 55.0f;
static constexpr float kWindowSwipeReach = 95.0f; // through the boards
static constexpr int kRegenDelayFrames = 50;      // as Link's: health back after 2.5 seconds
static constexpr int kInvincibleFrames = 30;
static constexpr float kRunSpeed = 6.0f;
static constexpr float kCamDist = 260.0f;
static constexpr float kCamHeight = 110.0f;
static constexpr int kMaxBombs = 20;

static bool BlackOpsHealth() {
    return CVarGetInteger(CVAR_ZOMBIES("BlackOpsHealth"), 1) != 0;
}

bool Coop_Enabled() {
    return CVarGetInteger(CVAR_ZOMBIES("Players"), 1) >= 2;
}

static Partner& P() {
    return GetRun().partner;
}

bool Coop_Active() {
    return IsRunActive() && GetRun().playerCount >= 2;
}

static Player* PartnerActor() {
    return reinterpret_cast<Player*>(P().actor);
}

static bool PartnerStanding() {
    return P().actor != nullptr && !P().downed && !P().bledOut;
}

static bool LinkStanding() {
    return !GetRun().linkDown.downed && !GetRun().linkDown.bledOut;
}

// ---------------------------------------------------------------------------
// Player 2's controller. Read before the game updates, then cleared, so the second
// controller can't drive the game's own debug frame advance (R + D-pad Down).

static u16 sCur = 0;
static u16 sPress = 0;
static s8 sStickX = 0;
static s8 sStickY = 0;
static s8 sRightX = 0;

static bool Pressed(u16 button) {
    return (sPress & button) != 0;
}

static bool Held(u16 button) {
    return (sCur & button) == button;
}

// While Link is dead the game stops updating every other actor (enemies, player 2, bombs),
// as the world waits out his death. A downed Link in co-op is not the end: his dead flag is
// lifted after he updates, so everyone else carries on, and put back before he updates
// again, so he stays down. It stays this way through his getting back up.
static bool sRestoreDead = false;
static bool sSplit = false;         // drawing two views this frame
static u8 sDoubleDefenseBefore = 0; // Link's Double Defense before the run, for downs
static u8 sDefenseHeartsBefore = 0;
static const Viewport kTop = { 0, SCREEN_HEIGHT / 2, 0, SCREEN_WIDTH };
static const Viewport kBottom = { SCREEN_HEIGHT / 2, SCREEN_HEIGHT, 0, SCREEN_WIDTH };
static const Viewport kFull = { 0, SCREEN_HEIGHT, 0, SCREEN_WIDTH };
static bool sLinkRising = false;

static void LiftDeadFlag(Player* player) {
    const LinkDown& down = GetRun().linkDown;
    if (!down.downed && !down.bledOut && !sLinkRising) {
        return;
    }
    if (player->stateFlags1 & PLAYER_STATE1_DEAD) {
        player->stateFlags1 &= ~PLAYER_STATE1_DEAD;
        sRestoreDead = true;
        // The undead can't see he's dead now: keep a Redead from grabbing him out of it.
        if (player->invincibilityTimer >= 0) {
            player->invincibilityTimer = -1;
        }
    } else {
        sLinkRising = false; // up again
    }
}

// Switches and scenery (doors, platforms, gates) update before Link, while his dead flag
// is still up, so they would stay frozen while he is down. Unfreeze them for that.
static void FreezeSceneryOnDeath(bool freeze) {
    for (s32 cat : { ACTORCAT_SWITCH, ACTORCAT_BG }) {
        if (freeze) {
            D_80116068[cat] |= PLAYER_STATE1_DEAD;
        } else {
            D_80116068[cat] &= ~PLAYER_STATE1_DEAD;
        }
    }
}

// Controllers: SoH sends every gamepad to port 1 (Link) and none to port 2 unless the
// player sets that up in the controller settings, which is awkward on a phone. When a
// co-op run is on, two pads are connected and port 2 has none, the newest pad moves to
// port 2 (with the default layout if it has none), and goes back when the run ends.
static int32_t sMovedPad = -1;

static void AssignSecondPad(bool coop) {
    auto deck = Ship::Context::GetRawInstance()->GetControlDeck();
    if (deck == nullptr) {
        return;
    }
    auto devices = deck->GetConnectedPhysicalDeviceManager();
    auto pads = devices->GetConnectedSDLGamepadNames();
    if (!coop) {
        if (sMovedPad >= 0) {
            devices->UnignoreInstanceIdForPort(0, sMovedPad);
            devices->IgnoreInstanceIdForPort(1, sMovedPad);
            sMovedPad = -1;
        }
        return;
    }
    // A pad connecting or dropping out makes the engine shut every pad out of port 2 again.
    // The pad already moved keeps its place if it's still here.
    if (sMovedPad >= 0 && pads.contains(sMovedPad)) {
        devices->IgnoreInstanceIdForPort(0, sMovedPad);
        devices->UnignoreInstanceIdForPort(1, sMovedPad);
        return;
    }
    if (sMovedPad >= 0) {
        devices->UnignoreInstanceIdForPort(0, sMovedPad); // gone; don't leave it shut out if it returns
        sMovedPad = -1;
    }
    if (pads.size() < 2) {
        return;
    }
    int32_t newest = -1;
    for (const auto& [id, name] : pads) {
        if (!devices->PortIsIgnoringInstanceId(1, id)) {
            return; // port 2 already has a pad
        }
        newest = std::max(newest, id);
    }
    devices->IgnoreInstanceIdForPort(0, newest);
    devices->UnignoreInstanceIdForPort(1, newest);
    sMovedPad = newest;
    auto controller = deck->GetControllerByPort(1);
    if (controller != nullptr && !controller->HasMappingsForPhysicalDeviceType(Ship::PhysicalDeviceType::SDLGamepad)) {
        controller->AddDefaultMappings(Ship::PhysicalDeviceType::SDLGamepad);
    }
}

void Coop_OnMainStart(PlayState* play) {
    if (play->gameplayFrames % 10 == 0) {
        AssignSecondPad(Coop_Active());
    }
    if (sRestoreDead) {
        sRestoreDead = false;
        Player* player = GET_PLAYER(play);
        if (player != nullptr && Coop_Active()) {
            player->stateFlags1 |= PLAYER_STATE1_DEAD;
        }
    }
    const LinkDown& down = GetRun().linkDown;
    FreezeSceneryOnDeath(!(Coop_Active() && (down.downed || down.bledOut || sLinkRising)));
    Input& input = play->state.input[1];
    sCur = input.cur.button;
    sPress = input.press.button;
    sStickX = input.cur.stick_x;
    sStickY = input.cur.stick_y;
    sRightX = input.cur.right_stick_x;
    if (!Coop_Active()) {
        return;
    }
    input.cur.button = 0;
    input.press.button = 0;
    input.rel.button = 0;

    // Link lies still while he's down.
    if (GetRun().linkDown.downed || GetRun().linkDown.bledOut) {
        Input& link = play->state.input[0];
        link.cur.button = 0;
        link.press.button = 0;
        link.cur.stick_x = 0;
        link.cur.stick_y = 0;
        link.rel.stick_x = 0;
        link.rel.stick_y = 0;
    }
}

// ---------------------------------------------------------------------------
// The player 2 actor: an instance the size of Player, so Link's drawing code can read
// the fields it expects (models, tunic, joint tables) from it.

static int sActorId = -1;
static ColliderCylinder sSword;
static ColliderCylinder sBody;
static bool sSwordInit = false;
static int sSwingFrames = 0;
static int sThrowFrames = 0;
static s16 sCamYaw = 0;
static Vec3f sEye = {};
static Vec3f sAt = {};
static Vec3f sLastSafe = {};
static std::vector<Actor*> sBombs;

static ColliderCylinderInit sSwordInit_ = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_PLAYER,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { DMG_SLASH_KOKIRI, 0x00, 0x01 },
        { 0x00000000, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NORMAL,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { 32, 50, 10, { 0, 0, 0 } },
};

static ColliderCylinderInit sBodyInit = {
    {
        COLTYPE_HIT5,
        AT_NONE,
        AC_NONE,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_PLAYER,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK1,
        { 0x00000000, 0x00, 0x00 },
        { 0x00000000, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_NONE,
        OCELEM_ON,
    },
    { 12, 60, 0, { 0, 0, 0 } },
};

enum class Anim { None, Wait, Walk, Run, Slash, Throw, Down, Rise };
static Anim sAnim = Anim::None;

static LinkAnimationHeader* AnimFor(Anim anim) {
    switch (anim) {
        case Anim::Walk:
            return (LinkAnimationHeader*)gPlayerAnim_link_normal_walk_free;
        case Anim::Run:
            return (LinkAnimationHeader*)gPlayerAnim_link_normal_run_free;
        case Anim::Slash:
            return (LinkAnimationHeader*)gPlayerAnim_link_fighter_normal_kiru;
        case Anim::Throw:
            return (LinkAnimationHeader*)gPlayerAnim_link_normal_throw_free;
        case Anim::Down:
            return (LinkAnimationHeader*)gPlayerAnim_link_normal_front_downA;
        case Anim::Rise:
            return (LinkAnimationHeader*)gPlayerAnim_link_derth_rebirth;
        case Anim::Wait:
        default:
            return (LinkAnimationHeader*)gPlayerAnim_link_normal_wait_free;
    }
}

static void PlayAnim(PlayState* play, Player* p, Anim anim, float speed = 1.0f) {
    if (sAnim == anim) {
        p->skelAnime.playSpeed = speed;
        return;
    }
    sAnim = anim;
    LinkAnimationHeader* header = AnimFor(anim);
    bool loop = anim == Anim::Wait || anim == Anim::Walk || anim == Anim::Run;
    LinkAnimation_Change(play, &p->skelAnime, header, speed, 0.0f, Animation_GetLastFrame(header),
                         loop ? ANIMMODE_LOOP : ANIMMODE_ONCE, -6.0f);
}

static void PartnerSfx(u16 sfx) {
    if (P().actor != nullptr) {
        Audio_PlayActorSound2(P().actor, sfx);
    }
}

static void PartnerVoice(u16 sfx) {
    // Child Link's voice lines sit 0x20 above adult Link's.
    PartnerSfx(static_cast<u16>(sfx + (LINK_IS_ADULT ? 0 : 0x20)));
}

static void PartnerInit(Actor* thisx, PlayState* play) {
    Player* p = reinterpret_cast<Player*>(thisx);
    sAnim = Anim::None;
    SkelAnime_InitLink(play, &p->skelAnime, gPlayerSkelHeaders[gSaveContext.linkAge], AnimFor(Anim::Wait), 9,
                       p->jointTable, p->morphTable, PLAYER_LIMB_MAX);
    p->skelAnime.baseTransl = { -57, 3377, 0 };
    PlayAnim(play, p, Anim::Wait);

    // A red tunic tells the two Links apart.
    p->currentTunic = PLAYER_TUNIC_GORON;
    p->currentBoots = PLAYER_BOOTS_KOKIRI;
    p->currentShield = PLAYER_SHIELD_NONE;
    p->heldItemAction = p->itemAction = LINK_IS_ADULT ? PLAYER_IA_SWORD_MASTER : PLAYER_IA_SWORD_KOKIRI;
    p->meleeWeaponEffectIndex = TOTAL_EFFECT_COUNT;
    Player_SetModelGroup(p, PLAYER_MODELGROUP_SWORD);

    Actor_SetScale(thisx, 0.01f);
    ActorShape_Init(&thisx->shape, 0.0f, ActorShadow_DrawCircle, 20.0f);
    thisx->gravity = -1.0f;
    thisx->minVelocityY = -20.0f;
    thisx->targetMode = 1;

    Collider_InitCylinder(play, &sSword);
    Collider_SetCylinder(play, &sSword, thisx, &sSwordInit_);
    Collider_InitCylinder(play, &sBody);
    Collider_SetCylinder(play, &sBody, thisx, &sBodyInit);
    sSwordInit = true;
    sSwingFrames = 0;
    sThrowFrames = 0;
    sCamYaw = static_cast<s16>(thisx->shape.rot.y);
    sLastSafe = thisx->world.pos;
}

static void PartnerDestroy(Actor* thisx, PlayState* play) {
    if (sSwordInit) {
        Collider_DestroyCylinder(play, &sSword);
        Collider_DestroyCylinder(play, &sBody);
        sSwordInit = false;
    }
    if (P().actor == thisx) {
        P().actor = nullptr;
    }
}

static int SwordDamageFlags() {
    switch (P().sword) {
        case PARTNER_SWORD_BIGGORON:
            return DMG_SLASH_GIANT;
        case PARTNER_SWORD_MASTER:
            return DMG_SLASH_MASTER;
        default:
            return DMG_SLASH_KOKIRI;
    }
}

static void ThrowBomb(PlayState* play, Player* p) {
    Actor* actor = &p->actor;
    Vec3f pos = actor->world.pos;
    pos.x += Math_SinS(actor->shape.rot.y) * 25.0f;
    pos.z += Math_CosS(actor->shape.rot.y) * 25.0f;
    pos.y += 40.0f;
    Actor* bomb =
        Actor_Spawn(&play->actorCtx, play, ACTOR_EN_BOM, pos.x, pos.y, pos.z, 0, actor->shape.rot.y, 0, BOMB_BODY);
    if (bomb == nullptr) {
        return;
    }
    bomb->speedXZ = 8.0f;
    bomb->velocity.y = 6.0f;
    bomb->world.rot.y = actor->shape.rot.y;
    sBombs.push_back(bomb);
    P().bombs--;
    PartnerVoice(NA_SE_VO_LI_SWORD_N);
    PartnerSfx(NA_SE_PL_THROW);
}

// The camera sits behind player 2 and turns with the right stick (or C-left/C-right).
static void UpdateCamera(PlayState* play, Actor* actor) {
    int turn = sRightX;
    if (Held(BTN_CLEFT)) {
        turn -= 60;
    }
    if (Held(BTN_CRIGHT)) {
        turn += 60;
    }
    if (std::abs(turn) > 10) {
        sCamYaw = static_cast<s16>(sCamYaw - turn * 12);
    }
    if (Pressed(BTN_Z)) {
        sCamYaw = static_cast<s16>(actor->shape.rot.y); // snap behind, like Z-targeting
    }

    sAt = actor->world.pos;
    sAt.y += 50.0f;
    Vec3f eye = { sAt.x - Math_SinS(sCamYaw) * kCamDist, sAt.y + kCamHeight - 50.0f,
                  sAt.z - Math_CosS(sCamYaw) * kCamDist };
    // Don't look through walls: pull the camera in to whatever is between.
    Vec3f hit;
    CollisionPoly* poly;
    s32 bgId;
    if (BgCheck_CameraLineTest1(&play->colCtx, &sAt, &eye, &hit, &poly, true, true, true, true, &bgId)) {
        eye.x = hit.x + (sAt.x - hit.x) * 0.1f;
        eye.y = hit.y + (sAt.y - hit.y) * 0.1f;
        eye.z = hit.z + (sAt.z - hit.z) * 0.1f;
    }
    sEye = eye;
}

// A cutscene, a message or a menu holds player 2 too. Not Player_InCsMode: that is also
// true while Link lies downed, and player 2 has to be free to go and revive him.
static bool GameHeld(PlayState* play) {
    return play->csCtx.state != CS_STATE_IDLE || play->msgCtx.msgMode != MSGMODE_NONE || play->pauseCtx.state != 0 ||
           play->transitionTrigger != TRANS_TRIGGER_OFF || play->transitionMode != TRANS_MODE_OFF ||
           (!GetRun().linkDown.downed && !GetRun().linkDown.bledOut && Player_InCsMode(play));
}

static void PartnerUpdate(Actor* thisx, PlayState* play) {
    Player* p = reinterpret_cast<Player*>(thisx);
    Partner& partner = P();
    Run& run = GetRun();
    bool frozen = run.phase == Phase::GameOver || GameHeld(play);

    if (partner.invincible > 0) {
        partner.invincible--;
    }

    if (partner.bledOut) {
        // Spectate player 1 until the next round.
        Player* link = GET_PLAYER(play);
        thisx->world.pos = link->actor.world.pos;
        UpdateCamera(play, &link->actor);
        return;
    }

    if (partner.downed) {
        PlayAnim(play, p, Anim::Down);
        thisx->speedXZ = 0.0f;
    } else if (!frozen) {
        // Swing the sword (B), throw a bomb (R).
        if (sSwingFrames > 0) {
            sSwingFrames--;
        }
        if (sThrowFrames > 0) {
            sThrowFrames--;
        }
        if (Pressed(BTN_B) && sSwingFrames == 0) {
            sSwingFrames = 12;
            sAnim = Anim::None;
            PlayAnim(play, p, Anim::Slash, 1.5f);
            PartnerVoice(NA_SE_VO_LI_SWORD_N);
            PartnerSfx(LINK_IS_ADULT ? NA_SE_IT_SWORD_SWING_HARD : NA_SE_IT_SWORD_SWING);
        } else if (Pressed(BTN_R) && sThrowFrames == 0 && sSwingFrames == 0) {
            if (partner.bombs > 0) {
                sThrowFrames = 10;
                sAnim = Anim::None;
                PlayAnim(play, p, Anim::Throw, 1.5f);
                ThrowBomb(play, p);
            } else {
                Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
            }
        }

        // Move relative to the camera.
        float mag = sqrtf(static_cast<float>(sStickX * sStickX + sStickY * sStickY));
        mag = std::min(mag, 60.0f);
        float speedScale = run.players[1].perks[PERK_STAMIN_UP] ? 1.25f : 1.0f;
        if (mag > 8.0f && sSwingFrames == 0) {
            s16 stickYaw = Math_Atan2S(static_cast<float>(sStickY), static_cast<float>(-sStickX));
            s16 yaw = static_cast<s16>(sCamYaw + stickYaw);
            Math_ScaledStepToS(&thisx->shape.rot.y, yaw, 0x1800);
            thisx->world.rot.y = thisx->shape.rot.y;
            thisx->speedXZ = (mag / 60.0f) * kRunSpeed * speedScale;
            if (sThrowFrames == 0) {
                PlayAnim(play, p, mag > 35.0f ? Anim::Run : Anim::Walk, mag > 35.0f ? 1.2f * speedScale : 1.0f);
            }
        } else {
            Math_StepToF(&thisx->speedXZ, 0.0f, 1.5f);
            if (sSwingFrames == 0 && sThrowFrames == 0) {
                PlayAnim(play, p, Anim::Wait);
            }
        }
    } else {
        thisx->speedXZ = 0.0f;
    }

    LinkAnimation_Update(play, &p->skelAnime);

    if (!frozen) {
        Actor_MoveXZGravity(thisx);
        Actor_UpdateBgCheckInfo(play, thisx, 26.0f, 14.0f, 60.0f, 0x1D);
        // The same walls as Link: keep player 2 off the scene's exits.
        if (Bounds_NearExit(thisx->world.pos)) {
            thisx->world.pos.x = sLastSafe.x;
            thisx->world.pos.z = sLastSafe.z;
        } else {
            sLastSafe = thisx->world.pos;
        }
        if (thisx->world.pos.y < run.origin.y - 1500.0f) {
            thisx->world.pos = run.origin; // fell out of the map
        }
    }
    Actor_SetFocus(thisx, 40.0f);
    UpdateCamera(play, thisx);

    // Sword hits from the swing landed last frame: credit them to player 2.
    if (sSword.base.atFlags & AT_HIT) {
        sSword.base.atFlags &= ~AT_HIT;
        for (TrackedEnemy& enemy : run.enemies) {
            if (enemy.actor == sSword.base.at) {
                enemy.hitBy = 1;
                enemy.hitFrames = 0;
            }
        }
    }

    // The hitbox is out while the blade sweeps.
    if (!partner.downed && sSwingFrames >= 3 && sSwingFrames <= 9) {
        sSword.info.toucher.dmgFlags = SwordDamageFlags();
        sSword.info.toucher.damage = static_cast<u8>((partner.sword == PARTNER_SWORD_BIGGORON ? 4
                                                      : partner.sword == PARTNER_SWORD_MASTER ? 2
                                                                                              : 1) *
                                                     (partner.papSword ? 2 : 1));
        sSword.dim.pos.x = static_cast<s16>(thisx->world.pos.x + Math_SinS(thisx->shape.rot.y) * 35.0f);
        sSword.dim.pos.y = static_cast<s16>(thisx->world.pos.y);
        sSword.dim.pos.z = static_cast<s16>(thisx->world.pos.z + Math_CosS(thisx->shape.rot.y) * 35.0f);
        CollisionCheck_SetAT(play, &play->colChkCtx, &sSword.base);
    }
    if (!partner.downed) {
        Collider_UpdateCylinder(thisx, &sBody);
        CollisionCheck_SetOC(play, &play->colChkCtx, &sBody.base);
    }
}

static void PartnerDraw(Actor* thisx, PlayState* play) {
    Player* p = reinterpret_cast<Player*>(thisx);
    if (P().bledOut) {
        return;
    }
    // Flicker while invincible after a hit.
    if (P().invincible > 0 && (P().invincible & 2)) {
        return;
    }
    s32 index = Object_GetIndex(&play->objectCtx, gLinkObjectIds[gSaveContext.linkAge]);
    if (index < 0) {
        return;
    }

    ZombiesCoop_SetLinkSegments(play, play->objectCtx.status[index].segment);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    Player_DrawImpl(play, p->skelAnime.skeleton, p->skelAnime.jointTable, p->skelAnime.dListCount, 0, p->currentTunic,
                    p->currentBoots, thisx->shape.face, Player_OverrideLimbDrawGameplayDefault,
                    Player_PostLimbDrawGameplay, p);
}

static void RegisterActor() {
    if (sActorId >= 0) {
        return;
    }
    ActorDBInit init;
    init.name = "Zombies_Partner";
    init.desc = "Zombies of Time: player 2";
    init.category = ACTORCAT_NPC;
    init.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
    init.objectId = OBJECT_GAMEPLAY_KEEP;
    init.instanceSize = sizeof(Player);
    init.init = PartnerInit;
    init.destroy = PartnerDestroy;
    init.update = PartnerUpdate;
    init.draw = PartnerDraw;
    sActorId = ActorDB::Instance->AddEntry(init).entry.id;
}

// ---------------------------------------------------------------------------
// Run hooks

static void SpawnPartner(PlayState* play, Player* link) {
    RegisterActor();
    Vec3f pos = link->actor.world.pos;
    s16 yaw = link->actor.shape.rot.y;
    // Beside Link, if there's floor there.
    float side = 60.0f;
    Vec3f beside = { pos.x + Math_CosS(yaw) * side, pos.y, pos.z - Math_SinS(yaw) * side };
    float y;
    if (FloorAt(play, beside.x, beside.z, pos.y, &y)) {
        pos = beside;
        pos.y = y;
    }
    Actor* actor = Actor_Spawn(&play->actorCtx, play, static_cast<s16>(sActorId), pos.x, pos.y, pos.z, 0, yaw, 0, 0);
    P().actor = actor;
    if (actor != nullptr) {
        sCamYaw = yaw;
        UpdateCamera(play, actor);
    }
}

void Coop_Reset() {
    AssignSecondPad(false);
    FreezeSceneryOnDeath(true);
    sRestoreDead = false;
    sLinkRising = false;
    sBombs.clear();
    if (sSplit && gPlayState != nullptr) {
        View_SetViewport(&gPlayState->view, const_cast<Viewport*>(&kFull));
    }
    sSplit = false;
}

void Coop_StartRun(PlayState* play, Player* player) {
    Coop_Reset();
    sDoubleDefenseBefore = gSaveContext.isDoubleDefenseAcquired;
    sDefenseHeartsBefore = gSaveContext.inventory.defenseHearts;
    Run& run = GetRun();
    run.partner = Partner();
    run.linkDown = LinkDown();
    sBombs.clear();
    if (run.playerCount < 2) {
        return;
    }
    SpawnPartner(play, player);
}

void Coop_EndRun(PlayState* play) {
    AssignSecondPad(false);
    Partner& partner = P();
    if (partner.actor != nullptr) {
        Actor_Kill(partner.actor);
        partner.actor = nullptr;
    }
    sBombs.clear();
    if (sSplit && play != nullptr) {
        View_SetViewport(&play->view, const_cast<Viewport*>(&kFull));
    }
    sSplit = false;
    // Ended with Link down but not a game over (quit from the run menu): he gets up.
    LinkDown& down = GetRun().linkDown;
    if ((down.downed || down.bledOut) && play != nullptr && play->gameOverCtx.state == GAMEOVER_INACTIVE) {
        Player* player = GET_PLAYER(play);
        if (player->stateFlags1 & PLAYER_STATE1_DEAD || sRestoreDead) {
            player->av2.actionVar2 = 1;
            sLinkRising = true;
        }
    }
    down = LinkDown();
}

// Black Ops brings back players who bled out at the start of the next round.
void Coop_OnRoundStart(PlayState* play, Player* player) {
    if (!Coop_Active()) {
        return;
    }
    Partner& partner = P();
    if (partner.actor == nullptr) {
        SpawnPartner(play, player);
    }
    if (partner.bledOut || partner.downed) {
        partner.bledOut = false;
        partner.downed = false;
        if (partner.actor != nullptr) {
            partner.actor->world.pos = player->actor.world.pos;
            partner.actor->world.pos.x += 40.0f;
        }
        sAnim = Anim::None;
    }
    partner.health = partner.healthCapacity;

    LinkDown& down = GetRun().linkDown;
    if (down.downed || down.bledOut) {
        down = LinkDown();
        player->av2.actionVar2 = 1; // get up (the fairy revive's own wake-up)
        sLinkRising = true;
    }
}

void Coop_OnActorDestroy(Actor* actor) {
    if (actor == P().actor) {
        P().actor = nullptr;
    }
    sBombs.erase(std::remove(sBombs.begin(), sBombs.end(), actor), sBombs.end());
}

// Player 2's bombs while they blow up: no friendly fire on Link (Black Ops has none), and
// undead in the blast are player 2's kills.
static void UpdateBombs() {
    for (Actor* actor : sBombs) {
        if (actor->params != BOMB_EXPLOSION) {
            continue;
        }
        EnBom* bomb = reinterpret_cast<EnBom*>(actor);
        bomb->explosionCollider.base.atFlags =
            static_cast<u8>((bomb->explosionCollider.base.atFlags & ~AT_TYPE_ALL) | AT_TYPE_PLAYER);
        // Only a blast that hit something last frame, and only what it could have reached.
        if (!(bomb->explosionCollider.base.atFlags & AT_HIT)) {
            continue;
        }
        float radius = bomb->explosionCollider.elements[0].dim.worldSphere.radius;
        for (TrackedEnemy& enemy : GetRun().enemies) {
            if (enemy.actor != nullptr && (enemy.actor == bomb->explosionCollider.base.at ||
                                           Math_Vec3f_DistXYZ(&enemy.actor->world.pos, &actor->world.pos) < radius)) {
                enemy.hitBy = 2;
                enemy.hitFrames = 0;
            }
        }
    }
}

// A hit shows up in the enemy's health a frame or two after it lands.
int Coop_Credit(const TrackedEnemy& enemy) {
    return enemy.hitBy != 0 && enemy.hitFrames <= 4 ? 1 : 0;
}

bool Coop_Target(const Vec3f& fromPos, Vec3f* out, float* dist) {
    PlayState* play = gPlayState;
    Vec3f from = fromPos;
    bool found = false;
    float best = 1e9f;
    if (play != nullptr && LinkStanding()) {
        Player* link = GET_PLAYER(play);
        best = Math_Vec3f_DistXZ(&from, &link->actor.world.pos);
        *out = link->actor.world.pos;
        found = true;
    }
    if (Coop_Active() && PartnerStanding()) {
        float d = Math_Vec3f_DistXZ(&from, &P().actor->world.pos);
        if (d < best) {
            best = d;
            *out = P().actor->world.pos;
            found = true;
        }
    }
    *dist = best;
    return found;
}

// ---------------------------------------------------------------------------
// Player 2's purchases

void Coop_GiveSword(int sword) {
    P().sword = std::max(P().sword, sword);
}

void Coop_GiveBombs(int count) {
    P().bombs = std::min(P().bombs + count, kMaxBombs);
}

int Coop_MaxBombs() {
    return kMaxBombs;
}

void Coop_Heal() {
    P().health = P().healthCapacity;
}

// ---------------------------------------------------------------------------
// Downs and revives

static int ReviveFramesFor(int reviver) {
    return GetRun().players[reviver].perks[PERK_FAIRY_REVIVE] ? kReviveFrames / 2 : kReviveFrames;
}

void LoseOnDown(int index) {
    Run& run = GetRun();
    PlayerState& ps = run.players[index];
    // Black Ops takes a cut of a downed player's points and all their perks.
    int lost = ps.points / 20;
    ps.points -= lost;
    bool hadJuggernog = ps.perks[PERK_JUGGERNOG];
    for (bool& perk : ps.perks) {
        perk = false;
    }
    if (index == 1) {
        P().healthCapacity = kBaseHealth;
    } else if (hadJuggernog) {
        // Juggernog's Double Defense goes; what the file had before the run stays.
        gSaveContext.isDoubleDefenseAcquired = sDoubleDefenseBefore;
        gSaveContext.inventory.defenseHearts = sDefenseHeartsBefore;
    }
}

static void DownPartner() {
    Partner& partner = P();
    partner.downed = true;
    partner.downFrames = kBleedOutFrames;
    partner.reviveFrames = 0;
    partner.health = 0;
    sAnim = Anim::None;
    PartnerVoice(NA_SE_VO_LI_DOWN);
    Announce("PLAYER 2 IS DOWN", 3);
    LoseOnDown(1);
}

static void RevivePartner() {
    Partner& partner = P();
    partner.downed = false;
    partner.health = partner.healthCapacity;
    partner.invincible = kInvincibleFrames * 2;
    sAnim = Anim::None;
    Sfx_PlaySfxCentered(NA_SE_EV_FIATY_HEAL);
    Announce("PLAYER 2 IS BACK UP", 2);
}

bool Coop_OnLinkDeath(PlayState* play, Player* player) {
    if (!Coop_Active() || !PartnerStanding()) {
        return false;
    }
    // Keep Link where he fell: no game over, no menu.
    play->gameOverCtx.state = GAMEOVER_INACTIVE;
    SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0);
    Audio_SetBgmVolumeOnDuringFanfare();
    Letterbox_SetSizeTarget(0);
    Extras_OnRoundStart(GetRun().dogRound);
    player->av1.actionVar1 = 0;
    player->av2.actionVar2 = 0;

    LinkDown& down = GetRun().linkDown;
    down.downed = true;
    down.downFrames = kBleedOutFrames;
    down.reviveFrames = 0;
    LoseOnDown(0);
    Announce("PLAYER 1 IS DOWN", 3);
    return true;
}

static void ReviveLink(Player* player) {
    GetRun().linkDown = LinkDown();
    player->av2.actionVar2 = 1; // the fairy revive's wake-up: full hearts, then back on his feet
    sLinkRising = true;
    Sfx_PlaySfxCentered(NA_SE_EV_FIATY_HEAL);
    Announce("PLAYER 1 IS BACK UP", 2);
}

// Everyone is down: the run is over. Link plays out his death so the game over
// and results follow as in single player.
static void EveryoneDown(PlayState* play, Player* player) {
    GetRun().linkDown = LinkDown();
    sLinkRising = false;
    // A downed Link's dead flag is lifted for the rest of the frame: he is still dead.
    if ((player->stateFlags1 & PLAYER_STATE1_DEAD) || sRestoreDead) {
        player->stateFlags1 |= PLAYER_STATE1_DEAD;
        sRestoreDead = false;
        play->gameOverCtx.state = GAMEOVER_DEATH_START;
        Audio_StopBgmAndFanfare(0);
        Audio_PlayFanfare(NA_BGM_GAME_OVER);
    } else {
        gSaveContext.health = 0; // Link takes the fall
    }
}

void Coop_Update(PlayState* play, Player* player) {
    if (!Coop_Active()) {
        return;
    }
    LiftDeadFlag(player);
    Run& run = GetRun();
    Partner& partner = P();
    if (partner.actor == nullptr && !partner.bledOut) {
        SpawnPartner(play, player);
        if (partner.actor == nullptr) {
            return;
        }
    }
    partner.prompt.clear();

    for (TrackedEnemy& enemy : run.enemies) {
        if (enemy.hitFrames < 1000) {
            enemy.hitFrames++;
        }
        if (enemy.swipeTimer > 0) {
            enemy.swipeTimer--;
        }
    }
    UpdateBombs();

    // Undead reaching player 2 swipe at them: OoT's undead only attack Link on their own.
    if (PartnerStanding() && partner.invincible == 0 && run.phase == Phase::Active) {
        Actor* me = partner.actor;
        for (TrackedEnemy& enemy : run.enemies) {
            if (enemy.killCounted || enemy.swipeTimer > 0 || enemy.actor == nullptr ||
                enemy.actor->colChkInfo.health == 0) {
                continue;
            }
            float dist = Math_Vec3f_DistXZ(&enemy.actor->world.pos, &me->world.pos);
            float reach = enemy.window >= 0 ? kWindowSwipeReach : kSwipeReach;
            if (dist > reach || fabsf(enemy.actor->world.pos.y - me->world.pos.y) > 60.0f) {
                continue;
            }
            enemy.swipeTimer = kSwipeFrames;
            // Black Ops health: two hits down, five with Juggernog. Off, a heart a hit.
            partner.health = BlackOpsHealth() ? HealthAfterHit(partner.health, partner.healthCapacity,
                                                               run.players[1].perks[PERK_JUGGERNOG])
                                              : partner.health - 16;
            partner.sinceHit = 0;
            partner.invincible = kInvincibleFrames;
            s16 away = Math_Vec3f_Yaw(&enemy.actor->world.pos, &me->world.pos);
            me->world.pos.x += Math_SinS(away) * 20.0f;
            me->world.pos.z += Math_CosS(away) * 20.0f;
            Audio_PlayActorSound2(enemy.actor, NA_SE_EN_STALKID_ATTACK);
            PartnerVoice(NA_SE_VO_LI_DAMAGE_S);
            if (partner.health <= 0) {
                DownPartner();
            }
            break;
        }
    }

    // Player 2's health comes back a few seconds after their last hit.
    if (BlackOpsHealth() && PartnerStanding() && partner.health > 0) {
        if (partner.sinceHit < kRegenDelayFrames) {
            partner.sinceHit++;
        } else if (partner.health < partner.healthCapacity) {
            partner.health =
                std::min(partner.healthCapacity, partner.health + std::max(partner.healthCapacity / 10, 4));
        }
    }

    // Player 2 bleeding out.
    if (partner.downed && --partner.downFrames <= 0) {
        partner.downed = false;
        partner.bledOut = true;
        Announce("PLAYER 2 BLED OUT", 3);
    }

    // Link bleeding out.
    LinkDown& down = run.linkDown;
    if (down.downed && --down.downFrames <= 0) {
        down.downed = false;
        down.bledOut = true;
        Announce("PLAYER 1 BLED OUT", 3);
    }

    // Revives: hold the buy button next to the downed player.
    Buyer linkBuyer;
    // In co-op, Fairy Revive halves the time it takes to pick a teammate up.
    if (partner.downed && LinkStanding()) {
        float dist = Math_Vec3f_DistXZ(&player->actor.world.pos, &partner.actor->world.pos);
        if (dist < kReviveRange) {
            run.prompt = std::string("HOLD ") + BuyButtonName() + ": REVIVE PLAYER 2";
            if (BuyHeld(play)) {
                if (++partner.reviveFrames >= ReviveFramesFor(0)) {
                    RevivePartner();
                }
            } else {
                partner.reviveFrames = 0;
            }
        } else {
            partner.reviveFrames = 0;
        }
    }
    if (down.downed && PartnerStanding()) {
        float dist = Math_Vec3f_DistXZ(&player->actor.world.pos, &partner.actor->world.pos);
        if (dist < kReviveRange) {
            partner.prompt = "HOLD A: REVIVE PLAYER 1";
            if (Held(BTN_A)) {
                if (++down.reviveFrames >= ReviveFramesFor(1)) {
                    ReviveLink(player);
                }
            } else {
                down.reviveFrames = 0;
            }
        } else {
            down.reviveFrames = 0;
        }
    }

    // Nobody left standing.
    if (!LinkStanding() && !PartnerStanding()) {
        EveryoneDown(play, player);
    }
}

bool Coop_Buyer(PlayState* play, Buyer* out) {
    if (!Coop_Active() || !PartnerStanding()) {
        return false;
    }
    out->index = 1;
    out->pos = P().actor->world.pos;
    out->pressed = Pressed(BTN_A);
    out->held = Held(BTN_A);
    out->busy = GameHeld(play);
    out->prompt = &P().prompt;
    return true;
}

// ---------------------------------------------------------------------------
// Split screen

bool Coop_SplitView() {
    return sSplit;
}

void Coop_DrawBegin() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    bool split =
        Coop_Active() && P().actor != nullptr && play->pauseCtx.state == 0 && play->transitionMode == TRANS_MODE_OFF;
    if (split) {
        View_SetViewport(&play->view, const_cast<Viewport*>(&kTop));
    } else if (sSplit) {
        View_SetViewport(&play->view, const_cast<Viewport*>(&kFull));
    }
    sSplit = split;
}

} // namespace Zombies

// Sets up player 2's viewport and camera matrices. This is the game's own view setup
// (func_800AAA9C) minus its camera-cut check: that check compares against the last view
// the game set up, which would be player 1's, so every frame would read as a cut and the
// cameras would swap their interpolation at high frame rates. Player 2's camera is
// recorded under its own key, and only counts as a cut when it really jumps.
static Vec3f sLastEye;
static Vec3f sLastAt;

static bool ZombiesCoop_Mirrored() {
    return R_PAUSE_MENU_MODE <= 1 && CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0);
}
static int sViewEpoch;

extern "C" void ZombiesCoop_ApplySecondView(View* view, const Vec3f& eye, const Vec3f& at) {
    GraphicsContext* gfxCtx = view->gfxCtx;

    if (Math_Vec3f_DistXYZ(&sLastEye, const_cast<Vec3f*>(&eye)) + Math_Vec3f_DistXYZ(&sLastAt, const_cast<Vec3f*>(&at)) >
        300.0f) {
        sViewEpoch++;
    }
    sLastEye = eye;
    sLastAt = at;

    OPEN_DISPS(gfxCtx);

    Vp* vp = (Vp*)Graph_Alloc(gfxCtx, sizeof(Vp));
    View_ViewportToVp(vp, &view->viewport);
    view->vp = *vp;
    func_800AA550(view);
    gSPViewport(POLY_OPA_DISP++, vp);
    gSPViewport(POLY_XLU_DISP++, vp);

    Mtx* projection = (Mtx*)Graph_Alloc(gfxCtx, sizeof(Mtx));
    Mtx* projectionFlipped = (Mtx*)Graph_Alloc(gfxCtx, sizeof(Mtx));
    Mtx* viewing = (Mtx*)Graph_Alloc(gfxCtx, sizeof(Mtx));
    view->projectionPtr = projection;
    view->projectionFlippedPtr = projectionFlipped;
    view->viewingPtr = viewing;

    f32 aspect = (f32)(view->viewport.rightX - view->viewport.leftX) /
                 (f32)(view->viewport.bottomY - view->viewport.topY);
    if (view->eye.x == view->lookAt.x && view->eye.y == view->lookAt.y && view->eye.z == view->lookAt.z) {
        view->eye.x += 1.0f;
    }
    func_800ABE74(view->eye.x, view->eye.y, view->eye.z);
    MtxF viewingF;
    guLookAtF(viewingF.mf, view->eye.x, view->eye.y, view->eye.z, view->lookAt.x, view->lookAt.y, view->lookAt.z,
              view->up.x, view->up.y, view->up.z);

    FrameInterpolation_RecordOpenChild(&sViewEpoch, sViewEpoch);
    guPerspective(projection, &view->normal, view->fovy, aspect, view->zNear, view->zFar, view->scale);
    view->projection = *projection;
    View_StepDistortion(view, projection);
    // Mirrored World: the game has already turned culling inside out for the frame, so
    // this view needs its projection flipped too.
    Mtx* load = projection;
    if (ZombiesCoop_Mirrored()) {
        MtxF flipF;
        SkinMatrix_Clear(&flipF);
        flipF.xx = -1.0f;
        MtxF projectionF;
        Matrix_MtxToMtxF(projection, &projectionF);
        SkinMatrix_MtxFMtxFMult(&projectionF, &flipF, &projectionF);
        Matrix_MtxFToMtx(&projectionF, projectionFlipped);
        load = projectionFlipped;
    }
    gSPPerspNormalize(POLY_OPA_DISP++, view->normal);
    gSPMatrix(POLY_OPA_DISP++, load, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPPerspNormalize(POLY_XLU_DISP++, view->normal);
    gSPMatrix(POLY_XLU_DISP++, load, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    Matrix_MtxFToMtx(&viewingF, viewing);
    view->viewing = *viewing;
    gSPMatrix(POLY_OPA_DISP++, viewing, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    gSPMatrix(POLY_XLU_DISP++, viewing, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    FrameInterpolation_RecordCloseChild();

    CLOSE_DISPS(gfxCtx);
}

// The second view, drawn after the game has drawn the first. Display list macros need
// C linkage, like the HUD's.
extern "C" void ZombiesCoop_DrawSecondView(PlayState* play, const Vec3f& eye, const Vec3f& at) {
    GraphicsContext* gfxCtx = play->state.gfxCtx;

    // Remember which actors were in view for player 1: they keep updating either way.
    std::vector<Actor*> inView;
    for (s32 i = 0; i < ACTORCAT_MAX; i++) {
        for (Actor* actor = play->actorCtx.actorLists[i].head; actor != nullptr; actor = actor->next) {
            if (actor->flags & ACTOR_FLAG_INSIDE_CULLING_VOLUME) {
                inView.push_back(actor);
            }
        }
    }

    View saved = play->view;
    MtxF savedBillboard = play->billboardMtxF;
    MtxF savedViewProjection = play->viewProjectionMtxF;
    Mtx* savedBillboardMtx = play->billboardMtx;

    View* view = &play->view;
    View_SetViewport(view, const_cast<Viewport*>(&Zombies::kBottom));
    Vec3f eyeCopy = eye;
    Vec3f atCopy = at;
    Vec3f up = { 0.0f, 1.0f, 0.0f };
    func_800AA358(view, &eyeCopy, &atCopy, &up);
    func_800AA460(view, view->fovy, view->zNear, play->lightCtx.fogFar);
    ZombiesCoop_ApplySecondView(view, eye, at);

    OPEN_DISPS(gfxCtx);

    // The billboard matrix, as Play_Draw builds it.
    Matrix_MtxToMtxF(&view->viewing, &play->billboardMtxF);
    Matrix_MtxToMtxF(&view->projection, &play->viewProjectionMtxF);
    Matrix_Mult(&play->viewProjectionMtxF, MTXMODE_NEW);
    Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
    Matrix_Get(&play->viewProjectionMtxF);
    play->billboardMtxF.mf[0][3] = play->billboardMtxF.mf[1][3] = play->billboardMtxF.mf[2][3] =
        play->billboardMtxF.mf[3][0] = play->billboardMtxF.mf[3][1] = play->billboardMtxF.mf[3][2] = 0.0f;
    Matrix_Transpose(&play->billboardMtxF);
    play->billboardMtx = Matrix_MtxFToMtx(&play->billboardMtxF, (Mtx*)Graph_Alloc(gfxCtx, sizeof(Mtx)));
    gSPSegment(POLY_OPA_DISP++, 0x01, (uintptr_t)play->billboardMtx);
    gSPSegment(POLY_XLU_DISP++, 0x01, (uintptr_t)play->billboardMtx);

    if (play->skyboxId != SKYBOX_NONE && play->skyboxId != SKYBOX_UNSET_1D && !play->envCtx.skyboxDisabled &&
        (play->skyboxId == SKYBOX_NORMAL_SKY || play->skyboxId == SKYBOX_CUTSCENE_MAP)) {
        SkyboxDraw_Draw(&play->skyboxCtx, gfxCtx, play->skyboxId, play->envCtx.skyboxBlend, eye.x, eye.y, eye.z);
    }

    Lights* lights = LightContext_NewLights(&play->lightCtx, gfxCtx);
    Lights_BindAll(lights, play->lightCtx.listHead, NULL);
    Lights_Draw(lights, gfxCtx);

    Scene_Draw(play);
    Room_Draw(play, &play->roomCtx.curRoom, 3);
    Room_Draw(play, &play->roomCtx.prevRoom, 3);
    Actor_DrawAll(play, &play->actorCtx);
    Zombies::Barricades_Draw();

    CLOSE_DISPS(gfxCtx);

    // Back to player 1's view for whatever the game draws next.
    play->view = saved;
    play->billboardMtxF = savedBillboard;
    play->viewProjectionMtxF = savedViewProjection;
    play->billboardMtx = savedBillboardMtx;
    // Put player 1's viewport and matrices back as the game built them this frame. Not by
    // building them again: that would step screen distortion (underwater, Jabu) twice.
    OPEN_DISPS(gfxCtx);
    Vp* vp = (Vp*)Graph_Alloc(gfxCtx, sizeof(Vp));
    *vp = play->view.vp;
    func_800AA550(&play->view);
    gSPViewport(POLY_OPA_DISP++, vp);
    gSPViewport(POLY_XLU_DISP++, vp);
    Mtx* projection = ZombiesCoop_Mirrored() ? play->view.projectionFlippedPtr : play->view.projectionPtr;
    gSPPerspNormalize(POLY_OPA_DISP++, play->view.normal);
    gSPMatrix(POLY_OPA_DISP++, projection, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(POLY_OPA_DISP++, play->view.viewingPtr, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    gSPPerspNormalize(POLY_XLU_DISP++, play->view.normal);
    gSPMatrix(POLY_XLU_DISP++, projection, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(POLY_XLU_DISP++, play->view.viewingPtr, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    gSPSegment(POLY_OPA_DISP++, 0x01, (uintptr_t)play->billboardMtx);
    gSPSegment(POLY_XLU_DISP++, 0x01, (uintptr_t)play->billboardMtx);
    CLOSE_DISPS(gfxCtx);

    for (s32 i = 0; i < ACTORCAT_MAX; i++) {
        for (Actor* actor = play->actorCtx.actorLists[i].head; actor != nullptr; actor = actor->next) {
            SkinMatrix_Vec3fMtxFMultXYZW(&play->viewProjectionMtxF, &actor->world.pos, &actor->projectedPos,
                                         &actor->projectedW);
        }
    }
    for (Actor* actor : inView) {
        actor->flags |= ACTOR_FLAG_INSIDE_CULLING_VOLUME;
    }
}

namespace Zombies {

void Coop_DrawEnd() {
    PlayState* play = gPlayState;
    if (play == nullptr || !sSplit || P().actor == nullptr) {
        return;
    }
    ZombiesCoop_DrawSecondView(play, sEye, sAt);
}

} // namespace Zombies
