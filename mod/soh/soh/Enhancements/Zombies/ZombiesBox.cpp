// Zombies of Time: the Mystery Box.
//
// The chest, its lid and the teddy bear are Black Ops models the player converted from
// their own install (manifest roles box_model, box_lid_model, teddy_model); the guns
// that cycle out of it are the guns' world models (gun_<name>_world). Black Ops turns
// the lid in script rather than with an animation, so the lid turns here. The timings
// follow the game as played: about four seconds of cycling, about twelve to take the
// prize, and the bear's goodbye before the box moves. The light over the box is the
// box_beam effect (ZombiesFx.cpp). Nothing from Black Ops ships with the mode.

#include "ZombiesBox.h"
#include "ZombiesFx.h"
#include "ZombiesGuns.h"
#include "ZombiesMode.h"
#include "ZombiesModel.h"
#include "ZombiesSound.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// A rigid imported model in the box's frame: at `local` (model units) from the box's
// origin, turned by pitch (about the box's long axis), roll and a spin about up.
// Display list macros need C linkage: this sits outside the namespace.
extern "C" void ZombiesBox_DrawRigid(PlayState* play, Gfx* dl, const Vec3f* pos, s16 yaw, float scale,
                                     const Vec3f* local, float pitch, float roll, float spin) {
    OPEN_DISPS(play->state.gfxCtx);
    Matrix_Translate(pos->x, pos->y, pos->z, MTXMODE_NEW);
    Matrix_RotateY(static_cast<f32>(yaw) * (3.14159265f / 32768.0f), MTXMODE_APPLY);
    Matrix_RotateZ(roll, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    Matrix_Translate(local->x, local->y, local->z, MTXMODE_APPLY);
    Matrix_RotateX(pitch, MTXMODE_APPLY);
    Matrix_RotateY(spin, MTXMODE_APPLY);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    // The scene's lights where the box stands (the chest's torch keeps its light), as an
    // actor gets them; without this the box takes whatever lights were bound last.
    Lights* lights = LightContext_NewLights(&play->lightCtx, play->state.gfxCtx);
    Lights_BindAll(lights, play->lightCtx.listHead, const_cast<Vec3f*>(pos));
    Lights_Draw(lights, play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, dl);
    CLOSE_DISPS(play->state.gfxCtx);
}

namespace Zombies {

static constexpr float kBoScale = 64.0f / 72.0f; // as the zombies: Black Ops inches to OoT units
static constexpr float kPi = 3.14159265f;
static constexpr int kFps = 20;
static constexpr float kCycleSec = 4.0f;    // the guns spin up out of the box
static constexpr float kTakeSec = 12.0f;    // then the prize waits this long
static constexpr float kLidSec = 0.5f;
static constexpr float kLidOpen = 105.0f * kPi / 180.0f;
static constexpr float kBearSec = 2.0f;     // the bear shows before the box lifts
static constexpr float kLiftSec = 2.5f;     // the box rises and shakes
static constexpr float kFlySec = 1.0f;      // then shoots up and away
static constexpr float kArriveSec = 1.0f;   // drops in at its new spot
static constexpr float kPrizeLow = 4.0f;    // prize height in the box (model units)
static constexpr float kPrizeHigh = 30.0f;  // ... and fully risen
// The lid's hinge on the box (model units): the back edge of the top.
static constexpr Vec3f kHinge = { 0.0f, 19.1f, 12.0f };

enum class State { Idle, Cycling, Showing, Closing, Bear, Lifting, Flying, Gone, Arriving };

struct Box {
    State state = State::Idle;
    float t = 0.0f; // seconds in this state
    bool placed = false;
    Vec3f pos = {};
    s16 yaw = 0;
    float lid = 0.0f; // radians open
    int who = 0;
    std::function<bool(BoxPrize*)> roll;
    std::function<void(PlayState*)> move;
    BoxPrize prize;
    int cycleIndex = 0;
    float nextSwap = 0.0f;
    int beam = 0;
    Actor* torch = nullptr;
    ActorFunc torchDraw = nullptr;
};

static Box sBox;

// ---------------------------------------------------------------------------
// Models

// Untextured surfaces (no pixels found) get a colour by material, so the box reads as
// the box: aged wood, dark iron, the glowing question-mark plate.
static ModelStyle BoxStyle() {
    return [](const std::string& m, bool textured, ModelTint* tint) {
        if (textured) {
            return true;
        }
        if (m.find("metal") != std::string::npos) {
            *tint = { 70, 66, 60 };
        } else if (m.find("glo") != std::string::npos || m.find("obj") != std::string::npos) {
            *tint = { 255, 240, 190 };
        } else if (m.find("fill") != std::string::npos) {
            *tint = { 40, 30, 22 };
        } else if (m.find("bear") != std::string::npos) {
            *tint = { 165, 115, 70 };
        } else {
            *tint = { 128, 84, 46 };
        }
        return true;
    };
}

// A world gun shows only its own parts: Black Ops hides the optional sights, grips and
// silencers its world model carries until an attachment asks for them.
static ModelStyle GunStyle() {
    return [](const std::string& m, bool textured, ModelTint* tint) {
        for (const char* extra : { "scope", "reflex", "red_dot", "aimpoint", "suppressor", "silencer", "foregrip",
                                   "grip", "clan_tag", "player_icon", "rail", "mag_wrap", "acog", "elbit", "lens",
                                   "ir_", "flash_hider", "extclip", "dualclip", "magazines2", "grenade_launcher",
                                   "_gl_", "masterkey", "flamethrower" }) {
            if (m.find(extra) != std::string::npos) {
                return false;
            }
        }
        if (m.size() > 3 && m.compare(m.size() - 3, 3, "_gl") == 0) {
            return false; // a grenade launcher ("_glove" is the arms)
        }
        if (!textured) {
            bool wood = m.find("wood") != std::string::npos || m.find("stock") != std::string::npos;
            *tint = wood ? ModelTint{ 120, 78, 44 } : ModelTint{ 74, 76, 80 };
        }
        return true;
    };
}

static ZModel* RoleModel(const char* role) {
    std::vector<std::string> names = Model_Manifest(role);
    return names.empty() ? nullptr : Model_Get(names[0]);
}

static ZModel* GunWorldModel(const char* boName) {
    if (boName == nullptr) {
        return nullptr;
    }
    return RoleModel((std::string("gun_") + boName + "_world").c_str());
}

// The guns the box can give whose models were imported, for the cycle.
static const std::vector<const char*>& CycleGuns() {
    static std::vector<const char*> sGuns;
    static bool sBuilt = false;
    if (!sBuilt) {
        sBuilt = true;
        for (int i = 1; i < static_cast<int>(GunId::Count); i++) {
            const GunStats& gun = GunInfo(static_cast<GunId>(i));
            if (gun.boxWeight > 0 && GunWorldModel(gun.boName) != nullptr) {
                sGuns.push_back(gun.boName);
            }
        }
    }
    return sGuns;
}

bool Box_Active() {
    return RoleModel("box_model") != nullptr;
}

// ---------------------------------------------------------------------------
// Use

void Box_Start(PlayState* play, int who, std::function<bool(BoxPrize*)> roll, std::function<void(PlayState*)> move) {
    sBox.state = State::Cycling;
    sBox.t = 0.0f;
    sBox.who = who;
    sBox.roll = std::move(roll);
    sBox.move = std::move(move);
    sBox.prize = BoxPrize();
    sBox.nextSwap = 0.0f;
    sBox.cycleIndex = static_cast<int>(Rand_ZeroOne() * 8.0f);
    // Black Ops: the lid, then the music box while the guns cycle.
    if (play != nullptr && !Sound_Play("box_open", &sBox.pos)) {
        SoundSource_PlaySfxAtFixedWorldPos(play, &sBox.pos, 20, NA_SE_EV_TBOX_OPEN);
    }
    Sound_Play("box_music", &sBox.pos);
}

bool Box_Busy(int who, const std::string& button, std::string* prompt) {
    if (sBox.state == State::Idle || !Box_Active()) {
        return false;
    }
    if (sBox.state == State::Showing && who == sBox.who) {
        *prompt = button + ": TAKE " + sBox.prize.label;
    } else {
        *prompt = "MYSTERY BOX - IN USE";
    }
    return true;
}

void Box_Take(PlayState* play, int who) {
    if (sBox.state != State::Showing || who != sBox.who) {
        return;
    }
    if (sBox.prize.give) {
        sBox.prize.give(play);
    }
    sBox.prize = BoxPrize();
    sBox.state = State::Closing;
    sBox.t = 0.0f;
    Sound_Play("box_close", &sBox.pos);
}

static void StopBeam() {
    if (sBox.beam != 0) {
        Fx_Stop(sBox.beam);
        sBox.beam = 0;
    }
}

static void Reset() {
    StopBeam();
    if (sBox.torch != nullptr && sBox.torchDraw != nullptr && sBox.torch->update != nullptr) {
        sBox.torch->draw = sBox.torchDraw;
    }
    sBox = Box();
}

static void Update(PlayState* play) {
    Vec3f chestPos;
    Actor* torch = nullptr;
    bool placed = IsRunActive() && Stations_Chest(&chestPos, &torch);
    if (!IsRunActive() || !Box_Active()) {
        if (sBox.placed || sBox.state != State::Idle) {
            Reset();
        }
        return;
    }
    float dt = 1.0f / kFps;

    // The torch stays for its light and its collision; the box is drawn in its place.
    if (torch != sBox.torch) {
        sBox.torch = torch;
        sBox.torchDraw = torch != nullptr ? torch->draw : nullptr;
    }
    if (torch != nullptr) {
        torch->draw = nullptr;
    }

    // The chest moved (the bear, or player 2's roll): drop in at the new spot.
    bool moved = placed && sBox.placed && Math_Vec3f_DistXYZ(&chestPos, &sBox.pos) > 1.0f;
    if (placed && (!sBox.placed || moved)) {
        sBox.pos = chestPos;
        // Front (-Z of the model) toward where the run started, which is the open arena.
        const Run& run = GetRun();
        s16 toward = Math_Vec3f_Yaw(&chestPos, const_cast<Vec3f*>(&run.origin));
        sBox.yaw = static_cast<s16>(toward + 0x8000);
        StopBeam();
        if (sBox.state == State::Gone || moved || !sBox.placed) {
            sBox.state = sBox.placed ? State::Arriving : State::Idle;
            sBox.t = 0.0f;
            sBox.lid = 0.0f;
        }
        sBox.placed = true;
    } else if (!placed && sBox.state != State::Gone) {
        sBox.placed = false;
        StopBeam();
    }
    if (!sBox.placed && sBox.state != State::Gone) {
        return;
    }

    sBox.t += dt;
    switch (sBox.state) {
        case State::Idle:
            sBox.lid = std::max(0.0f, sBox.lid - dt / kLidSec * kLidOpen);
            break;
        case State::Cycling: {
            sBox.lid = std::min(kLidOpen, sBox.t / kLidSec * kLidOpen);
            // Swaps come fast and slow down toward the end.
            if (sBox.t >= sBox.nextSwap) {
                sBox.cycleIndex++;
                sBox.nextSwap = sBox.t + 0.06f + 0.3f * (sBox.t / kCycleSec) * (sBox.t / kCycleSec);
            }
            if (sBox.t >= kCycleSec) {
                BoxPrize prize;
                bool gotPrize = sBox.roll ? sBox.roll(&prize) : false;
                sBox.t = 0.0f;
                if (gotPrize) {
                    sBox.prize = std::move(prize);
                    sBox.state = State::Showing;
                } else {
                    sBox.state = State::Bear;
                }
            }
            break;
        }
        case State::Showing:
            if (sBox.t >= kTakeSec) {
                // Not taken in time: it sinks back in and is gone, as in Black Ops.
                sBox.prize = BoxPrize();
                sBox.state = State::Closing;
                sBox.t = 0.0f;
                Sound_Play("box_close", &sBox.pos);
            }
            break;
        case State::Closing:
            sBox.lid = std::max(0.0f, kLidOpen * (1.0f - sBox.t / kLidSec));
            if (sBox.t >= kLidSec) {
                sBox.state = State::Idle;
                sBox.t = 0.0f;
            }
            break;
        case State::Bear:
            if (sBox.t >= kBearSec) {
                sBox.state = State::Lifting;
                sBox.t = 0.0f;
                StopBeam();
                Sound_Play("box_move", &sBox.pos);
            }
            break;
        case State::Lifting:
            if (sBox.t >= kLiftSec) {
                sBox.state = State::Flying;
                sBox.t = 0.0f;
            }
            break;
        case State::Flying:
            if (sBox.t >= kFlySec) {
                Vec3f top = sBox.pos;
                top.y += 200.0f;
                Fx_PlayUp("powerup_grab", top, 2.0f);
                if (!Sound_Play("box_poof", &top)) {
                    SoundSource_PlaySfxAtFixedWorldPos(play, &top, 20, NA_SE_EN_EXTINCT);
                }
                sBox.state = State::Gone;
                sBox.t = 0.0f;
                if (sBox.move) {
                    auto move = std::move(sBox.move);
                    sBox.move = nullptr;
                    move(play); // the chest station moves on; the box follows it
                }
            }
            break;
        case State::Gone:
            break;
        case State::Arriving:
            if (sBox.t >= kArriveSec) {
                Fx_PlayUp("zombie_rise", sBox.pos, 1.3f);
                Sound_Play("box_land", &sBox.pos);
                sBox.state = State::Idle;
                sBox.t = 0.0f;
            }
            break;
    }

    // The light over the box while it sits there.
    bool beamOn = sBox.placed && (sBox.state == State::Idle || sBox.state == State::Cycling ||
                                  sBox.state == State::Showing || sBox.state == State::Closing ||
                                  sBox.state == State::Bear);
    if (beamOn && !Fx_Alive(sBox.beam)) {
        Vec3f at = sBox.pos;
        at.y += 12.0f;
        sBox.beam = Fx_Play("box_beam", at, { 0.0f, -1.0f, 0.0f });
    } else if (!beamOn) {
        StopBeam();
    }
}

// ---------------------------------------------------------------------------
// Drawing

static s16 ItemDrawId(u8 item) {
    switch (item) {
        case ITEM_BOMBCHU: return GID_BOMBCHU;
        case ITEM_SLINGSHOT: return GID_SLINGSHOT;
        case ITEM_BOOMERANG: return GID_BOOMERANG;
        case ITEM_BOMB: return GID_BOMB;
        case ITEM_DINS_FIRE: return GID_DINS_FIRE;
        case ITEM_NAYRUS_LOVE: return GID_NAYRUS_LOVE;
        case ITEM_STICK: return GID_STICK;
        case ITEM_ARROW_FIRE: return GID_ARROW_FIRE;
        case ITEM_ARROW_ICE: return GID_ARROW_ICE;
        case ITEM_ARROW_LIGHT: return GID_ARROW_LIGHT;
        case ITEM_HAMMER: return GID_HAMMER;
        case ITEM_LONGSHOT: return GID_LONGSHOT;
        case ITEM_SWORD_BGS: return GID_SWORD_BGS;
        default: return -1;
    }
}

// A prize model centred over the box, its lowest point at height y (model units).
static void DrawPrizeModel(PlayState* play, ZModel* model, const ModelStyle& style, const Vec3f& base, float y) {
    Gfx* dl = Model_RigidDlStyled(model, style);
    if (dl == nullptr) {
        return;
    }
    Vec3f lo, hi;
    Model_Bounds(model, &lo, &hi);
    float vs = Model_VertexScale();
    // The matrix scale turns model units * vs into OoT units.
    Vec3f local = { -(lo.x + hi.x) * 0.5f * vs, (y - lo.y) * vs, -(lo.z + hi.z) * 0.5f * vs };
    ZombiesBox_DrawRigid(play, dl, &base, sBox.yaw, kBoScale / vs, &local, 0.0f, 0.0f, 0.0f);
}

static void DrawPrize(PlayState* play, const Vec3f& base, float y) {
    if (sBox.state == State::Cycling) {
        const std::vector<const char*>& guns = CycleGuns();
        if (!guns.empty()) {
            const char* name = guns[static_cast<size_t>(sBox.cycleIndex) % guns.size()];
            DrawPrizeModel(play, GunWorldModel(name), GunStyle(), base, y);
        }
        return;
    }
    if (sBox.prize.gunBoName != nullptr) {
        ZModel* model = GunWorldModel(sBox.prize.gunBoName);
        if (model != nullptr) {
            DrawPrizeModel(play, model, GunStyle(), base, y);
            return;
        }
    }
    s16 gid = ItemDrawId(sBox.prize.item);
    if (gid >= 0) {
        float h = y * kBoScale + 6.0f;
        Matrix_Translate(base.x, base.y + h, base.z, MTXMODE_NEW);
        Matrix_RotateY(static_cast<f32>(sBox.yaw) * (3.14159265f / 32768.0f) + sBox.t * 1.5f, MTXMODE_APPLY);
        Matrix_Scale(0.9f, 0.9f, 0.9f, MTXMODE_APPLY);
        GetItem_Draw(play, gid);
    }
}

void Box_Draw(PlayState* play) {
    if (play == nullptr || !sBox.placed || sBox.state == State::Gone || !Box_Active()) {
        return;
    }
    ZModel* boxModel = RoleModel("box_model");
    ZModel* lidModel = RoleModel("box_lid_model");
    Gfx* boxDl = Model_RigidDlStyled(boxModel, BoxStyle());
    if (boxDl == nullptr) {
        return;
    }
    float vs = Model_VertexScale();
    float s = kBoScale / vs;
    Vec3f base = sBox.pos;
    float roll = 0.0f;
    // Leaving: up a little, shaking, then away fast. Arriving: dropping in from above.
    if (sBox.state == State::Lifting) {
        float f = sBox.t / kLiftSec;
        base.y += 28.0f * f;
        roll = sinf(sBox.t * 40.0f) * 0.06f * f;
    } else if (sBox.state == State::Flying) {
        float f = sBox.t / kFlySec;
        base.y += 28.0f + 260.0f * f * f;
        roll = sinf(sBox.t * 40.0f) * 0.06f;
    } else if (sBox.state == State::Arriving) {
        float f = 1.0f - sBox.t / kArriveSec;
        base.y += 160.0f * f * f;
    }
    Vec3f zero = { 0.0f, 0.0f, 0.0f };
    ZombiesBox_DrawRigid(play, boxDl, &base, sBox.yaw, s, &zero, 0.0f, roll, 0.0f);
    Gfx* lidDl = Model_RigidDlStyled(lidModel, BoxStyle());
    if (lidDl != nullptr) {
        Vec3f hinge = { kHinge.x * vs, kHinge.y * vs, kHinge.z * vs };
        ZombiesBox_DrawRigid(play, lidDl, &base, sBox.yaw, s, &hinge, sBox.lid, roll, 0.0f);
    }

    // What floats over it.
    switch (sBox.state) {
        case State::Cycling: {
            float f = std::min(sBox.t / kCycleSec, 1.0f);
            float y = kPrizeLow + (kPrizeHigh - kPrizeLow) * (1.0f - (1.0f - f) * (1.0f - f));
            DrawPrize(play, base, y);
            break;
        }
        case State::Showing: {
            // Sinks slowly back while it waits.
            float f = std::min(sBox.t / kTakeSec, 1.0f);
            DrawPrize(play, base, kPrizeHigh - (kPrizeHigh - kPrizeLow) * f);
            break;
        }
        case State::Bear:
        case State::Lifting:
        case State::Flying: {
            ZModel* bear = RoleModel("teddy_model");
            Gfx* bearDl = Model_RigidDlStyled(bear, BoxStyle());
            if (bearDl != nullptr) {
                float f = sBox.state == State::Bear ? std::min(sBox.t / 0.8f, 1.0f) : 1.0f;
                Vec3f local = { 0.0f, (kPrizeLow + 14.0f * f) * vs, 0.0f };
                ZombiesBox_DrawRigid(play, bearDl, &base, sBox.yaw, s, &local, 0.0f, roll, 0.0f);
            }
            break;
        }
        default:
            break;
    }
}

static void RegisterBox() {
    bool enabled = CVarGetInteger(CVAR_ZOMBIES("Enabled"), 0) || CVarGetInteger(CVAR_ZOMBIES("ZombiesOnly"), 1);
    COND_HOOK(OnPlayerUpdate, enabled, [] {
        if (gPlayState != nullptr) {
            Update(gPlayState);
        }
    });
    COND_HOOK(OnSceneInit, enabled, [](int16_t) {
        // The scene's actors are gone: forget the torch without touching it.
        sBox.torch = nullptr;
        sBox.torchDraw = nullptr;
        sBox.beam = 0;
        sBox.placed = false;
        sBox.state = State::Idle;
        sBox.lid = 0.0f;
    });
}

static RegisterShipInitFunc boxInit(RegisterBox, { CVAR_ZOMBIES("Enabled"), CVAR_ZOMBIES("ZombiesOnly") });

} // namespace Zombies
