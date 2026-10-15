// Zombies of Time: the Black Ops zombie.
//
// An actor of our own, drawn with the zombie model and animations imported from the
// player's own Black Ops install (ZombiesModel.cpp). It's a body: where it goes, how fast
// and when it swipes are the mode's Black Ops AI (ZombiesMode.cpp), which drives it like
// the OoT undead. The actor keeps it on the floor, takes hits, and picks the animation
// that matches what it's doing: rising out of the ground, walking, running, sprinting,
// crawling, tearing at boards, swiping and dying.
//
// Which imported models and animations play which part is listed in
// zombies/models/manifest.txt, which the importer writes:
//   zombie_model <name>            the zombie, head merged onto the body
//   anim_<role> <name>             role: rise walk run sprint crawl attack tear death crawl_death
// A role can be listed more than once; each zombie picks one, as Black Ops varies them.

#include "ZombiesMode.h"
#include "ZombiesModel.h"

#include <algorithm>
#include <cmath>

#include "soh/ActorDB.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace Zombies {

static constexpr float kZombieHeight = 64.0f; // a head taller than adult Link
static constexpr float kBlendSeconds = 0.2f;
static constexpr float kDt = 1.0f / 20.0f; // OoT game logic runs at 20 updates per second

enum class Role {
    Rise,
    Walk,
    Run,
    Sprint,
    Crawl,
    Attack,
    Tear,
    Death,
    CrawlDeath,
    Count,
};

static const char* kRoleNames[] = { "rise", "walk", "run", "sprint", "crawl", "attack", "tear", "death", "crawl_death" };

struct Manifest {
    bool read = false;
    std::string model;
    std::vector<std::string> anims[static_cast<int>(Role::Count)];
};

static Manifest sManifest;

static const Manifest& GetManifest() {
    if (!sManifest.read) {
        sManifest.read = true;
        std::vector<std::string> models = Model_Manifest("zombie_model");
        sManifest.model = models.empty() ? "" : models[0];
        for (int r = 0; r < static_cast<int>(Role::Count); r++) {
            sManifest.anims[r] = Model_Manifest(std::string("anim_") + kRoleNames[r]);
        }
    }
    return sManifest;
}

static ZModel* ZombieModel() {
    const Manifest& m = GetManifest();
    return m.model.empty() ? nullptr : Model_Get(m.model);
}

bool ZombieActor_Available() {
    // A model and at least something to walk with.
    const Manifest& m = GetManifest();
    if (ZombieModel() == nullptr) {
        return false;
    }
    for (const std::string& name : m.anims[static_cast<int>(Role::Walk)]) {
        if (Anim_Get(name) != nullptr) {
            return true;
        }
    }
    return false;
}

enum class State {
    Rising,
    Moving,
    Attacking,
    Dying,
};

// What the actor keeps besides the Actor itself: C++ state lives on the heap.
struct ZombieState {
    State state = State::Rising;
    MoveTier tier = MoveTier::Walk;
    bool tearing = false;
    int variant = 0; // which of a role's animations this zombie uses
    Role role = Role::Rise;
    float time = 0.0f;
    Role prevRole = Role::Rise;
    float prevTime = 0.0f;
    float blend = 1.0f; // 1: fully on the current role
    Vec3f lastPos = {};
    ModelPose pose;
    ModelDrawBuffers buffers;
};

struct ZombieActor {
    Actor actor;
    ColliderCylinder collider;
    ZombieState* z;
};

static int sActorId = -1;

static ColliderCylinderInit sCylinderInit = {
    {
        COLTYPE_HIT0,
        AT_NONE,
        AC_ON | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK1,
        { 0x00000000, 0x00, 0x00 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON | BUMP_HOOKABLE,
        OCELEM_ON,
    },
    { 18, 60, 0, { 0, 0, 0 } },
};

// Everything Link has hurts a zombie; the mode scales his items to keep up with the rounds.
static DamageTable sDamageTable = {
    /* Deku nut      */ DMG_ENTRY(0, 0x1),
    /* Deku stick    */ DMG_ENTRY(2, 0x0),
    /* Slingshot     */ DMG_ENTRY(1, 0x0),
    /* Explosive     */ DMG_ENTRY(4, 0x0),
    /* Boomerang     */ DMG_ENTRY(1, 0x1),
    /* Normal arrow  */ DMG_ENTRY(2, 0x0),
    /* Hammer swing  */ DMG_ENTRY(4, 0x0),
    /* Hookshot      */ DMG_ENTRY(2, 0x0),
    /* Kokiri sword  */ DMG_ENTRY(1, 0x0),
    /* Master sword  */ DMG_ENTRY(2, 0x0),
    /* Giant's Knife */ DMG_ENTRY(4, 0x0),
    /* Fire arrow    */ DMG_ENTRY(4, 0x0),
    /* Ice arrow     */ DMG_ENTRY(4, 0x0),
    /* Light arrow   */ DMG_ENTRY(6, 0x0),
    /* Unk arrow 1   */ DMG_ENTRY(2, 0x0),
    /* Unk arrow 2   */ DMG_ENTRY(2, 0x0),
    /* Unk arrow 3   */ DMG_ENTRY(2, 0x0),
    /* Fire magic    */ DMG_ENTRY(6, 0x0),
    /* Ice magic     */ DMG_ENTRY(4, 0x0),
    /* Light magic   */ DMG_ENTRY(6, 0x0),
    /* Shield        */ DMG_ENTRY(0, 0x0),
    /* Mirror Ray    */ DMG_ENTRY(0, 0x0),
    /* Kokiri spin   */ DMG_ENTRY(1, 0x0),
    /* Giant spin    */ DMG_ENTRY(4, 0x0),
    /* Master spin   */ DMG_ENTRY(2, 0x0),
    /* Kokiri jump   */ DMG_ENTRY(2, 0x0),
    /* Giant jump    */ DMG_ENTRY(8, 0x0),
    /* Master jump   */ DMG_ENTRY(4, 0x0),
    /* Unknown 1     */ DMG_ENTRY(0, 0x0),
    /* Unblockable   */ DMG_ENTRY(0, 0x0),
    /* Hammer jump   */ DMG_ENTRY(4, 0x0),
    /* Unknown 2     */ DMG_ENTRY(0, 0x0),
};

static const ZAnim* AnimFor(const ZombieState& z, Role role) {
    const std::vector<std::string>& names = GetManifest().anims[static_cast<int>(role)];
    if (names.empty()) {
        // Stand-ins: anything is better than a T-pose.
        switch (role) {
            case Role::Run:
            case Role::Sprint:
            case Role::Rise:
            case Role::Tear:
                return AnimFor(z, Role::Walk);
            case Role::CrawlDeath:
                return AnimFor(z, Role::Death);
            case Role::Crawl:
                return AnimFor(z, Role::Walk);
            default:
                return nullptr;
        }
    }
    return Anim_Get(names[z.variant % names.size()]);
}

static void Play(ZombieState& z, Role role) {
    if (z.role == role) {
        return;
    }
    z.prevRole = z.role;
    z.prevTime = z.time;
    z.role = role;
    z.time = 0.0f;
    z.blend = 0.0f;
}

static Role MoveRole(const ZombieState& z) {
    if (z.tearing) {
        return Role::Tear;
    }
    switch (z.tier) {
        case MoveTier::Crawl:
            return Role::Crawl;
        case MoveTier::Sprint:
            return Role::Sprint;
        case MoveTier::Run:
            return Role::Run;
        default:
            return Role::Walk;
    }
}

static void ZombieInit(Actor* thisx, PlayState* play) {
    ZombieActor* self = reinterpret_cast<ZombieActor*>(thisx);
    self->z = new ZombieState();
    self->z->variant = static_cast<int>(Rand_ZeroOne() * 1000.0f);
    self->z->lastPos = thisx->world.pos;
    ActorShape_Init(&thisx->shape, 0.0f, ActorShadow_DrawCircle, 22.0f);
    Collider_InitCylinder(play, &self->collider);
    Collider_SetCylinder(play, &self->collider, thisx, &sCylinderInit);
    thisx->colChkInfo.damageTable = &sDamageTable;
    thisx->colChkInfo.mass = 100;
    thisx->colChkInfo.health = 8;
    thisx->gravity = -3.5f;
    thisx->targetMode = 3;
    // Rising out of the ground, unless there's nothing to rise with.
    const ZAnim* rise = GetManifest().anims[static_cast<int>(Role::Rise)].empty() ? nullptr
                                                                                  : AnimFor(*self->z, Role::Rise);
    self->z->state = rise != nullptr ? State::Rising : State::Moving;
    self->z->role = rise != nullptr ? Role::Rise : Role::Walk;
    self->z->blend = 1.0f;
}

static void ZombieDestroy(Actor* thisx, PlayState* play) {
    ZombieActor* self = reinterpret_cast<ZombieActor*>(thisx);
    Collider_DestroyCylinder(play, &self->collider);
    delete self->z;
    self->z = nullptr;
}

static void TakeHits(ZombieActor* self, PlayState* play) {
    Actor* thisx = &self->actor;
    if (!(self->collider.base.acFlags & AC_HIT)) {
        return;
    }
    self->collider.base.acFlags &= ~AC_HIT;
    if (thisx->colChkInfo.damageEffect == 0x1) {
        // Nuts and the boomerang stun, as they do every OoT enemy.
        Actor_SetColorFilter(thisx, 0, 0x78, 0, 40);
        thisx->freezeTimer = 40;
        return;
    }
    if (thisx->colChkInfo.damage > 0) {
        Actor_ApplyDamage(thisx);
        Actor_SetColorFilter(thisx, 0x4000, 0xFF, 0, 8);
        Audio_PlayActorSound2(thisx, thisx->colChkInfo.health > 0 ? NA_SE_EN_STALKID_DAMAGE : NA_SE_EN_STALKID_DEAD);
    }
}

static void ZombieUpdate(Actor* thisx, PlayState* play) {
    ZombieActor* self = reinterpret_cast<ZombieActor*>(thisx);
    ZombieState& z = *self->z;

    if (z.state != State::Dying) {
        TakeHits(self, play);
        if (thisx->colChkInfo.health == 0) {
            z.state = State::Dying;
            Play(z, z.tier == MoveTier::Crawl ? Role::CrawlDeath : Role::Death);
            thisx->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
        }
    }

    float speed = Math_Vec3f_DistXZ(&thisx->world.pos, &z.lastPos);
    z.lastPos = thisx->world.pos;
    const ZAnim* current = AnimFor(z, z.role);
    float length = Anim_Length(current);

    switch (z.state) {
        case State::Rising:
            if (current == nullptr || z.time >= length) {
                z.state = State::Moving;
                Play(z, MoveRole(z));
            }
            break;
        case State::Attacking:
            if (current == nullptr || z.time >= length) {
                z.state = State::Moving;
                Play(z, MoveRole(z));
            }
            break;
        case State::Moving:
            Play(z, MoveRole(z));
            break;
        case State::Dying:
            if (current == nullptr || z.time >= length + 1.0f) {
                Actor_Kill(thisx);
                return;
            }
            break;
    }

    // Walk cycles play at the pace the zombie is actually covering ground: Horde Pull
    // sets the speed, the animation follows.
    float rate = 1.0f;
    if (z.state == State::Moving && !z.tearing) {
        float expected = PullSpeed(z.tier);
        rate = expected > 0.1f ? std::clamp(speed / expected, 0.3f, 1.5f) : 1.0f;
    }
    float before = z.time;
    z.time += kDt * rate;
    z.prevTime += kDt;
    z.blend = std::min(1.0f, z.blend + kDt / kBlendSeconds);
    if (Anim_NoteBetween(current, "footstep", before, z.time) || Anim_NoteBetween(current, "step", before, z.time)) {
        Audio_PlayActorSound2(thisx, NA_SE_EN_STAL_WALK);
    }

    thisx->speedXZ = 0.0f; // the mode moves it
    Actor_MoveXZGravity(thisx);
    // Walls, floor, ceiling and water, as the Redead checks them.
    Actor_UpdateBgCheckInfo(play, thisx, 30.0f, 18.0f, 40.0f, 0x1D);
    thisx->focus.pos = thisx->world.pos;
    thisx->focus.pos.y += z.tier == MoveTier::Crawl ? 15.0f : 50.0f;

    if (z.state != State::Dying) {
        self->collider.dim.height = z.tier == MoveTier::Crawl ? 25 : 60;
        Collider_UpdateCylinder(thisx, &self->collider);
        CollisionCheck_SetOC(play, &play->colChkCtx, &self->collider.base);
        if (z.state != State::Rising) {
            CollisionCheck_SetAC(play, &play->colChkCtx, &self->collider.base);
        }
    }
}

static void ZombieDraw(Actor* thisx, PlayState* play) {
    ZombieActor* self = reinterpret_cast<ZombieActor*>(thisx);
    ZombieState& z = *self->z;
    ZModel* model = ZombieModel();
    if (model == nullptr) {
        return;
    }
    AnimLayer cur = { AnimFor(z, z.role), z.time };
    AnimLayer prev = { AnimFor(z, z.prevRole), z.prevTime };
    // Blend from the last animation into this one, so changes of pace don't pop.
    Model_Pose(model, prev, cur, z.blend, &z.pose);
    Model_Draw(play, model, z.pose, thisx->world.pos, thisx->shape.rot.y, kZombieHeight, &z.buffers);
}

static void RegisterActor() {
    if (sActorId >= 0) {
        return;
    }
    ActorDBInit init;
    init.name = "Zombies_Zombie";
    init.desc = "Zombies of Time: Black Ops zombie";
    init.category = ACTORCAT_ENEMY;
    init.flags = ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE | ACTOR_FLAG_UPDATE_CULLING_DISABLED |
                 ACTOR_FLAG_DRAW_CULLING_DISABLED;
    init.objectId = OBJECT_GAMEPLAY_KEEP;
    init.instanceSize = sizeof(ZombieActor);
    init.init = ZombieInit;
    init.destroy = ZombieDestroy;
    init.update = ZombieUpdate;
    init.draw = ZombieDraw;
    sActorId = ActorDB::Instance->AddEntry(init).entry.id;
}

s16 ZombieActor_Id() {
    RegisterActor();
    return static_cast<s16>(sActorId);
}

static ZombieState* StateOf(Actor* actor) {
    if (actor == nullptr || sActorId < 0 || actor->id != sActorId) {
        return nullptr;
    }
    return reinterpret_cast<ZombieActor*>(actor)->z;
}

bool ZombieActor_Is(Actor* actor) {
    return StateOf(actor) != nullptr;
}

// Rising out of the ground or mid-swipe: the mode leaves it where it is.
bool ZombieActor_Busy(Actor* actor) {
    ZombieState* z = StateOf(actor);
    return z != nullptr && (z->state == State::Rising || z->state == State::Attacking || z->state == State::Dying);
}

void ZombieActor_SetTier(Actor* actor, MoveTier tier) {
    if (ZombieState* z = StateOf(actor)) {
        z->tier = tier;
    }
}

void ZombieActor_SetTearing(Actor* actor, bool tearing) {
    if (ZombieState* z = StateOf(actor)) {
        z->tearing = tearing;
    }
}

void ZombieActor_Attack(Actor* actor) {
    ZombieState* z = StateOf(actor);
    if (z == nullptr || z->state == State::Dying || z->state == State::Rising) {
        return;
    }
    if (AnimFor(*z, Role::Attack) == nullptr) {
        return;
    }
    z->state = State::Attacking;
    z->role = Role::Walk; // restart even if it was already swiping
    Play(*z, Role::Attack);
}

} // namespace Zombies
