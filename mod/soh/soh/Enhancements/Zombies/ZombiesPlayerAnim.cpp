// Zombies of Time: Link holding a gun with Black Ops' own player animations.
//
// tools/bomap playeranims converts Black Ops' third-person player animations from the
// player's own install (the body standing, aiming, walking and running with a pistol or a
// rifle, and the torso firing, reloading, raising and putting a gun away) and the player
// skeleton they play on (manifest role player_skeleton, bones only). Nothing from Black
// Ops ships with the mode. This file plays them on that skeleton and retargets the result
// onto Link's, whose 21 limbs are a different shape and size:
//
// - Each Link limb that is driven takes one Black Ops bone. Both skeletons are first put
//   in the same reference pose: Black Ops' bind pose (standing, arms down, forearms
//   forward). For Link that pose is built, not stored: each arm and leg limb's bone axis
//   (towards its child joint) is laid along the matching Black Ops bone and turned so its
//   hinge axis is the body's left-right axis, and the torso, head and feet are upright.
// - A frame of animation is then the Black Ops bone's rotation away from its bind pose,
//   in model space, applied to the Link limb's reference rotation: the same turn from the
//   same starting pose. Link's own limb lengths stay, so only rotations carry over.
// - Those model-space rotations become Link's parent-relative joint rotations (OoT's
//   Z-Y-X angles) and are blended over his own animation by the gun's raise weight.
// - The legs follow Black Ops only on the ground and outside rolls and jumps; then the
//   hips drop as Black Ops' do (scaled to Link's legs). Walks and runs play at the pace
//   Link moves, from how far each animation carries the Black Ops player.
// - The gun goes where Black Ops puts it, at the hand's tag_weapon_right, carried into
//   Link's hand through the same reference.

#include "ZombiesPlayerAnim.h"
#include "ZombiesModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
// z_player.c
void Player_Action_Roll(Player* player, PlayState* play);
}

namespace Zombies {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDt = 1.0f / 20.0f; // OoT's game logic runs at 20 updates a second
constexpr int kMaxWho = 2;
constexpr int kLimbs = PLAYER_LIMB_MAX; // jointTable: [0] root position, [1..21] limb rotations
constexpr float kBodyBlendFrames = 6.0f;

struct Q {
    float x = 0, y = 0, z = 0, w = 1;
};

Q Mul(const Q& a, const Q& b) {
    return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
             a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}

Q Conj(const Q& q) {
    return { -q.x, -q.y, -q.z, q.w };
}

Q Norm(const Q& q) {
    float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len < 1e-6f) {
        return Q();
    }
    return { q.x / len, q.y / len, q.z / len, q.w / len };
}

Q Nlerp(const Q& a, Q b, float t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) {
        b = { -b.x, -b.y, -b.z, -b.w };
    }
    return Norm({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
}

Vec3f Rot(const Q& q, const Vec3f& v) {
    float cx = q.y * v.z - q.z * v.y;
    float cy = q.z * v.x - q.x * v.z;
    float cz = q.x * v.y - q.y * v.x;
    float ccx = q.y * cz - q.z * cy;
    float ccy = q.z * cx - q.x * cz;
    float ccz = q.x * cy - q.y * cx;
    return { v.x + 2.0f * (q.w * cx + ccx), v.y + 2.0f * (q.w * cy + ccy), v.z + 2.0f * (q.w * cz + ccz) };
}

Q AxisAngle(float x, float y, float z, float rad) {
    float s = std::sin(rad * 0.5f);
    return { x * s, y * s, z * s, std::cos(rad * 0.5f) };
}

// Rotation matrix (columns are the rotated axes) to a quaternion.
Q FromAxes(const Vec3f& ax, const Vec3f& ay, const Vec3f& az) {
    float m00 = ax.x, m01 = ay.x, m02 = az.x;
    float m10 = ax.y, m11 = ay.y, m12 = az.y;
    float m20 = ax.z, m21 = ay.z, m22 = az.z;
    float tr = m00 + m11 + m22;
    Q q;
    if (tr > 0.0f) {
        float s = std::sqrt(tr + 1.0f) * 2.0f;
        q = { (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s };
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = { 0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s };
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = { (m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s };
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = { (m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s };
    }
    return Norm(q);
}

float BinangToRad(s16 a) {
    return static_cast<float>(a) * (kPi / 32768.0f);
}

s16 RadToBinang(float r) {
    return static_cast<s16>(static_cast<s32>(std::lround(r * (32768.0f / kPi))));
}

// OoT's limb rotation: Matrix_TranslateRotateZYX, so R = Rz * Ry * Rx.
Q FromJoint(const Vec3s& r) {
    return Mul(Mul(AxisAngle(0, 0, 1, BinangToRad(r.z)), AxisAngle(0, 1, 0, BinangToRad(r.y))),
               AxisAngle(1, 0, 0, BinangToRad(r.x)));
}

Vec3s ToJoint(const Q& q) {
    float m00 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    float m10 = 2.0f * (q.x * q.y + q.w * q.z);
    float m20 = 2.0f * (q.x * q.z - q.w * q.y);
    float m21 = 2.0f * (q.y * q.z + q.w * q.x);
    float m22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    float sy = std::clamp(-m20, -1.0f, 1.0f);
    Vec3s out;
    out.x = RadToBinang(std::atan2(m21, m22));
    out.y = RadToBinang(std::asin(sy));
    out.z = RadToBinang(std::atan2(m10, m00));
    return out;
}

Vec3f Sub(const Vec3f& a, const Vec3f& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

Vec3f Scale(const Vec3f& a, float s) {
    return { a.x * s, a.y * s, a.z * s };
}

float Dot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f Cross(const Vec3f& a, const Vec3f& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

float Len(const Vec3f& a) {
    return std::sqrt(Dot(a, a));
}

Vec3f Unit(const Vec3f& a) {
    float l = Len(a);
    return l > 1e-6f ? Scale(a, 1.0f / l) : Vec3f{ 0.0f, 1.0f, 0.0f };
}

Q BoneQ(const BoneXf& b) {
    return { b.qx, b.qy, b.qz, b.qw };
}

// Black Ops' model space (as imported: +X forward, +Y up, -Z left) to Link's (+Z forward,
// +Y up, +X left): a quarter turn about Y.
const Q kToLink = AxisAngle(0, 1, 0, -kPi * 0.5f);

// How each driven Link limb's reference rotation is made.
enum class RefKind : u8 {
    Root,  // upright: the actor's own axes
    Upper, // upright torso: +X up the spine, +Y forward, +Z left
    Head,  // upright head: +X forward, +Y down, +Z left
    Foot,  // flat foot: +X forward, +Y down, +Z left
    Bone,  // +X along the bone towards `toward`, +Z the body's left
    Hand,  // +Y along the hand towards `toward` (Link's hands lie across their limb), +Z left
};

struct LimbMap {
    u8 limb;
    const char* bone;
    const char* toward;
    RefKind kind;
    bool leg;     // follows Black Ops only when the legs do
    s16 twist;    // a turn about the bone axis, matching how Link's mesh lies on the limb
};

// clang-format off
const LimbMap kMap[] = {
    { PLAYER_LIMB_ROOT,       "j_mainroot",    nullptr,       RefKind::Root,  true,  0 },
    { PLAYER_LIMB_UPPER,      "j_spine4",      nullptr,       RefKind::Upper, false, 0 },
    { PLAYER_LIMB_HEAD,       "j_head",        nullptr,       RefKind::Head,  false, 0 },
    { PLAYER_LIMB_L_SHOULDER, "j_shoulder_le", "j_elbow_le",  RefKind::Bone,  false, 0 },
    { PLAYER_LIMB_L_FOREARM,  "j_elbow_le",    "j_wrist_le",  RefKind::Bone,  false, 0 },
    { PLAYER_LIMB_L_HAND,     "j_wrist_le",    "j_mid_le_1",  RefKind::Hand,  false, 0 },
    { PLAYER_LIMB_R_SHOULDER, "j_shoulder_ri", "j_elbow_ri",  RefKind::Bone,  false, 0 },
    { PLAYER_LIMB_R_FOREARM,  "j_elbow_ri",    "j_wrist_ri",  RefKind::Bone,  false, 0 },
    { PLAYER_LIMB_R_HAND,     "j_wrist_ri",    "j_mid_ri_1",  RefKind::Hand,  false, 0 },
    { PLAYER_LIMB_L_THIGH,    "j_hip_le",      "j_knee_le",   RefKind::Bone,  true,  0 },
    { PLAYER_LIMB_L_SHIN,     "j_knee_le",     "j_ankle_le",  RefKind::Bone,  true,  0 },
    { PLAYER_LIMB_L_FOOT,     "j_ankle_le",    nullptr,       RefKind::Foot,  true,  0 },
    { PLAYER_LIMB_R_THIGH,    "j_hip_ri",      "j_knee_ri",   RefKind::Bone,  true,  0 },
    { PLAYER_LIMB_R_SHIN,     "j_knee_ri",     "j_ankle_ri",  RefKind::Bone,  true,  0 },
    { PLAYER_LIMB_R_FOOT,     "j_ankle_ri",    nullptr,       RefKind::Foot,  true,  0 },
};
// clang-format on
constexpr int kMapCount = static_cast<int>(sizeof(kMap) / sizeof(kMap[0]));

// The Black Ops side, worked out once from the imported skeleton.
struct Reference {
    bool tried = false;
    bool ok = false;
    const ZModel* skel = nullptr;
    int bone[kMapCount] = {};
    Q bindToRef[kMapCount]; // bind^-1 * toLink^-1 * linkRef: a frame's model rotation times this is Link's
    int pelvis = -1, wristR = -1, tagR = -1;
    float pelvisY = 0.0f;   // bind height of the hips
    float armLength = 1.0f; // shoulder to wrist, inches
    float legLength = 1.0f; // hip to ankle
    Q handFrame;            // the right wrist's bind frame as seen from Link's hand reference
};
Reference sRef;

Vec3f BindPos(const ZModel* m, int bone) {
    return Model_BoneBind(m, bone).pos;
}

bool BuildReference() {
    if (sRef.tried) {
        return sRef.ok;
    }
    sRef.tried = true;
    std::vector<std::string> names = Model_Manifest("player_skeleton");
    if (names.empty()) {
        return false;
    }
    const ZModel* m = Model_Get(names[0]);
    if (m == nullptr) {
        return false;
    }
    sRef.skel = m;
    const Vec3f left = { 1.0f, 0.0f, 0.0f };
    for (int i = 0; i < kMapCount; i++) {
        const LimbMap& e = kMap[i];
        int b = Model_BoneIndex(m, e.bone);
        if (b < 0) {
            return false;
        }
        sRef.bone[i] = b;
        Q linkRef;
        switch (e.kind) {
            case RefKind::Root:
                linkRef = Q();
                break;
            case RefKind::Upper:
                linkRef = FromAxes({ 0, 1, 0 }, { 0, 0, 1 }, left);
                break;
            case RefKind::Head:
            case RefKind::Foot:
                linkRef = FromAxes({ 0, 0, 1 }, { 0, -1, 0 }, left);
                break;
            case RefKind::Bone:
            case RefKind::Hand: {
                int t = Model_BoneIndex(m, e.toward);
                if (t < 0) {
                    return false;
                }
                Vec3f along = Unit(Rot(kToLink, Sub(BindPos(m, t), BindPos(m, b))));
                Vec3f side = Sub(left, Scale(along, Dot(left, along)));
                if (Len(side) < 0.2f) {
                    side = Cross(along, { 0.0f, 1.0f, 0.0f });
                }
                side = Unit(side);
                if (e.kind == RefKind::Bone) {
                    linkRef = FromAxes(along, Cross(side, along), side); // X along, Z left, Y = Z x X
                    linkRef = Mul(linkRef, AxisAngle(1, 0, 0, BinangToRad(e.twist)));
                } else {
                    linkRef = FromAxes(Cross(along, side), along, side); // Y along, Z left, X = Y x Z
                    linkRef = Mul(linkRef, AxisAngle(0, 1, 0, BinangToRad(e.twist)));
                }
                break;
            }
        }
        Q bind = BoneQ(Model_BoneBind(m, b));
        sRef.bindToRef[i] = Mul(Mul(Conj(bind), Conj(kToLink)), linkRef);
        if (e.limb == PLAYER_LIMB_R_HAND) {
            // Link's hand = toLink * wrist * handFrame^-1 ... kept as: the wrist's frame in the hand's.
            sRef.handFrame = Mul(Mul(Conj(linkRef), kToLink), bind);
        }
    }
    sRef.pelvis = Model_BoneIndex(m, "j_mainroot");
    sRef.wristR = Model_BoneIndex(m, "j_wrist_ri");
    sRef.tagR = Model_BoneIndex(m, "tag_weapon_right");
    int shoulder = Model_BoneIndex(m, "j_shoulder_ri");
    int elbow = Model_BoneIndex(m, "j_elbow_ri");
    int hip = Model_BoneIndex(m, "j_hip_ri");
    int knee = Model_BoneIndex(m, "j_knee_ri");
    int ankle = Model_BoneIndex(m, "j_ankle_ri");
    if (sRef.pelvis < 0 || sRef.wristR < 0 || sRef.tagR < 0 || shoulder < 0 || elbow < 0 || hip < 0 || knee < 0 ||
        ankle < 0) {
        return false;
    }
    sRef.pelvisY = BindPos(m, sRef.pelvis).y;
    sRef.armLength = Len(Sub(BindPos(m, elbow), BindPos(m, shoulder))) + Len(Sub(BindPos(m, sRef.wristR), BindPos(m, elbow)));
    sRef.legLength = Len(Sub(BindPos(m, knee), BindPos(m, hip))) + Len(Sub(BindPos(m, ankle), BindPos(m, knee)));
    sRef.ok = sRef.armLength > 1.0f && sRef.legLength > 1.0f;
    return sRef.ok;
}

// Link's skeleton (adult or child), read from the limbs themselves.
struct LinkSkel {
    void** skeleton = nullptr;
    s8 parent[kLimbs] = {};
    float armUnits = 1.0f; // limb units (a hundredth of OoT's) per Black Ops inch, arms
    float legUnits = 1.0f; // and legs
    s16 restRootY = 0;
};
LinkSkel sSkels[2];

float JointLen(const LodLimb* limb) {
    return Len({ static_cast<float>(limb->jointPos.x), static_cast<float>(limb->jointPos.y),
                 static_cast<float>(limb->jointPos.z) });
}

const LinkSkel* SkelOf(Player* player) {
    void** skeleton = player->skelAnime.skeleton;
    if (skeleton == nullptr || player->skelAnime.limbCount < kLimbs - 1) {
        return nullptr;
    }
    for (LinkSkel& s : sSkels) {
        if (s.skeleton == skeleton) {
            return &s;
        }
    }
    LinkSkel& s = sSkels[sSkels[0].skeleton == nullptr ? 0 : 1];
    s = LinkSkel();
    s.skeleton = skeleton;
    LodLimb* limbs[kLimbs] = {};
    for (int i = 1; i < kLimbs; i++) {
        limbs[i] = static_cast<LodLimb*>(SEGMENTED_TO_VIRTUAL(skeleton[i - 1]));
        s.parent[i] = 0;
    }
    for (int i = 1; i < kLimbs; i++) {
        u8 c = limbs[i]->child;
        while (c != LIMB_DONE && c + 1 < kLimbs) {
            s.parent[c + 1] = static_cast<s8>(i);
            c = limbs[c + 1]->sibling;
        }
    }
    float arm = JointLen(limbs[PLAYER_LIMB_R_FOREARM]) + JointLen(limbs[PLAYER_LIMB_R_HAND]);
    float leg = JointLen(limbs[PLAYER_LIMB_R_SHIN]) + JointLen(limbs[PLAYER_LIMB_R_FOOT]);
    s.armUnits = arm / sRef.armLength;
    s.legUnits = leg / sRef.legLength;
    s.restRootY = limbs[PLAYER_LIMB_ROOT]->jointPos.y;
    return &s;
}

// Roles, as bomap playeranims names them.
const char* Family(PlayerAnimKind k) {
    return k == PlayerAnimKind::Pistol ? "pistol" : "rifle";
}

const char* KindName(PlayerAnimKind k) {
    switch (k) {
        case PlayerAnimKind::Pistol:
            return "pistol";
        case PlayerAnimKind::Smg:
            return "smg";
        case PlayerAnimKind::Shotgun:
            return "shotgun";
        case PlayerAnimKind::Lmg:
            return "lmg";
        default:
            return "rifle";
    }
}

const ZAnim* RoleAnim(const std::string& role) {
    static std::map<std::string, const ZAnim*> sCache;
    auto it = sCache.find(role);
    if (it != sCache.end()) {
        return it->second;
    }
    const ZAnim* anim = nullptr;
    std::vector<std::string> names = Model_Manifest(role);
    if (!names.empty()) {
        anim = Anim_Get(names[0]);
    }
    sCache.emplace(role, anim);
    return anim;
}

// A body role for the gun's family.
const ZAnim* BodyAnim(PlayerAnimKind k, const char* state) {
    return RoleAnim(std::string("player_") + Family(k) + "_" + state);
}

// A torso role for the gun's kind, else its family's, else the rifle's.
const ZAnim* TorsoAnim(PlayerAnimKind k, const char* state) {
    const ZAnim* a = RoleAnim(std::string("player_") + KindName(k) + "_" + state);
    if (a == nullptr) {
        a = RoleAnim(std::string("player_") + Family(k) + "_" + state);
    }
    if (a == nullptr && k != PlayerAnimKind::Pistol) {
        a = RoleAnim(std::string("player_rifle_") + state);
    }
    return a;
}

struct Layer {
    const ZAnim* anim = nullptr;
    float time = 0.0f;
    float rate = 1.0f;
};

struct PlayerState {
    bool active = false;
    Layer cur, prev;
    float blend = 1.0f;
    const ZAnim* torso = nullptr;
    float torsoTime = 0.0f;
    float torsoWeight = 0.0f;
    const ZAnim* fireAnim = nullptr;
    float fireTime = -1.0f;
    bool legs = false;
    // Set by the pose, for the draw.
    bool posed = false;
    Vec3s saved[kLimbs];
    Vec3s* table = nullptr;
    Q gunRot;
    Vec3f gunPos = {};
    Q limbWorld[kLimbs]; // actor space, as posed
    float armUnits = 1.0f;
};
PlayerState sState[kMaxWho];

// How fast an animation carries the Black Ops player, inches a second.
float AnimSpeed(const ZAnim* anim) {
    float len = Anim_Length(anim);
    return len > 0.0f ? Len(Anim_Travel(anim)) / len : 0.0f;
}

bool LegsFree(Player* player) {
    if (!(player->actor.bgCheckFlags & 1)) {
        return false; // in the air
    }
    if ((player->stateFlags1 & (PLAYER_STATE1_JUMPING | PLAYER_STATE1_FREEFALL | PLAYER_STATE1_DAMAGED)) ||
        (player->stateFlags2 & PLAYER_STATE2_HOPPING) || (player->stateFlags3 & PLAYER_STATE3_MIDAIR)) {
        return false;
    }
    return player->actionFunc != Player_Action_Roll;
}

} // namespace

bool PlayerAnim_Available(PlayerAnimKind kind) {
    return BuildReference() && BodyAnim(kind, "idle") != nullptr;
}

void PlayerAnim_Reset(int who) {
    if (who >= 0 && who < kMaxWho) {
        PlayerState& ps = sState[who];
        ps.active = false;
        ps.cur = Layer();
        ps.prev = Layer();
        ps.torso = nullptr;
        ps.torsoWeight = 0.0f;
        ps.fireTime = -1.0f;
    }
}

void PlayerAnim_Update(int who, Player* player, const PlayerAnimGun& gun) {
    if (who < 0 || who >= kMaxWho || player == nullptr || !PlayerAnim_Available(gun.kind)) {
        return;
    }
    PlayerState& ps = sState[who];
    const LinkSkel* skel = SkelOf(player);
    if (skel == nullptr) {
        return;
    }
    ps.legs = LegsFree(player);

    // The body: standing, aiming, or moving each way at a walk, run or sprint.
    float speed = player->actor.speedXZ; // units a frame
    const ZAnim* body = BodyAnim(gun.kind, gun.aiming ? "ads" : "idle");
    if (body == nullptr) {
        body = BodyAnim(gun.kind, "idle");
    }
    float rate = 1.0f;
    if (ps.legs && speed > 0.5f) {
        s16 rel = static_cast<s16>(player->actor.world.rot.y - player->actor.shape.rot.y);
        const char* dir = "f";
        if (rel > 0x2000 && rel < 0x6000) {
            dir = "l";
        } else if (rel < -0x2000 && rel > -0x6000) {
            dir = "r";
        } else if (rel >= 0x6000 || rel <= -0x6000) {
            dir = "b";
        }
        const ZAnim* walk = BodyAnim(gun.kind, (std::string("walk_") + dir).c_str());
        const ZAnim* run = BodyAnim(gun.kind, (std::string("run_") + dir).c_str());
        const ZAnim* sprint = BodyAnim(gun.kind, "sprint");
        // Black Ops inches a second to Link's units a frame.
        float toLink = skel->legUnits * 0.01f * kDt;
        float walkSpeed = AnimSpeed(walk) * toLink;
        float runSpeed = AnimSpeed(run) * toLink;
        const ZAnim* pick = walk;
        float natural = walkSpeed;
        if (gun.sprint && sprint != nullptr && *dir == 'f') {
            pick = sprint;
            natural = AnimSpeed(sprint) * toLink;
        } else if (run != nullptr && (walk == nullptr || speed > (walkSpeed + runSpeed) * 0.5f)) {
            pick = run;
            natural = runSpeed;
        }
        if (pick != nullptr) {
            body = pick;
            rate = natural > 0.01f ? std::clamp(speed / natural, 0.5f, 2.0f) : 1.0f;
        }
    }
    if (!ps.active) {
        ps.active = true;
        ps.cur = { body, 0.0f, rate };
        ps.prev = Layer();
        ps.blend = 1.0f;
    } else if (body != ps.cur.anim) {
        ps.prev = ps.cur;
        // Keep the step: a new walk or run starts where the last one's feet were.
        float phase = 0.0f;
        float lenOld = Anim_Length(ps.cur.anim);
        if (lenOld > 0.0f && Anim_Looping(ps.cur.anim)) {
            phase = std::fmod(ps.cur.time, lenOld) / lenOld;
        }
        ps.cur = { body, phase * Anim_Length(body), rate };
        ps.blend = 0.0f;
    }
    ps.cur.rate = rate;
    ps.cur.time += kDt * ps.cur.rate;
    ps.prev.time += kDt * ps.prev.rate;
    ps.blend = std::min(1.0f, ps.blend + 1.0f / kBodyBlendFrames);

    // The torso: a swap, a reload or a shot, over the body.
    const ZAnim* torso = nullptr;
    float torsoTime = 0.0f;
    float weight = 0.0f;
    if (gun.fired) {
        ps.fireAnim = TorsoAnim(gun.kind, gun.aiming ? "fire_ads" : "fire");
        if (ps.fireAnim == nullptr) {
            ps.fireAnim = TorsoAnim(gun.kind, "fire");
        }
        ps.fireTime = 0.0f;
    } else if (ps.fireTime >= 0.0f) {
        ps.fireTime += kDt;
        if (ps.fireTime > Anim_Length(ps.fireAnim)) {
            ps.fireTime = -1.0f;
        }
    }
    if (gun.swap >= 0.0f) {
        bool down = gun.swap < 0.5f;
        torso = TorsoAnim(gun.kind, down ? "putaway" : "raise");
        float t = down ? gun.swap * 2.0f : (gun.swap - 0.5f) * 2.0f;
        torsoTime = t * Anim_Length(torso);
        weight = 1.0f;
    } else if (gun.reload >= 0.0f) {
        torso = TorsoAnim(gun.kind, "reload");
        // Played over the gun's own reload time, whatever the animation's length.
        torsoTime = gun.reload * Anim_Length(torso);
        weight = std::min({ 1.0f, gun.reload / 0.08f, (1.0f - gun.reload) / 0.08f });
    } else if (ps.fireTime >= 0.0f && ps.fireAnim != nullptr) {
        torso = ps.fireAnim;
        torsoTime = ps.fireTime;
        float len = Anim_Length(torso);
        weight = len > 0.0f ? std::min(1.0f, (len - ps.fireTime) / (len * 0.3f)) : 0.0f;
    }
    if (torso != nullptr) {
        ps.torso = torso;
        ps.torsoTime = torsoTime;
        ps.torsoWeight = std::clamp(weight, 0.0f, 1.0f);
    } else {
        ps.torsoWeight = std::max(0.0f, ps.torsoWeight - 0.25f);
        if (ps.torsoWeight <= 0.0f) {
            ps.torso = nullptr;
        }
    }
}

bool PlayerAnim_Pose(int who, Player* player, float weight) {
    if (who < 0 || who >= kMaxWho || player == nullptr) {
        return false;
    }
    PlayerState& ps = sState[who];
    ps.posed = false;
    if (!ps.active || ps.cur.anim == nullptr || !BuildReference()) {
        return false;
    }
    const LinkSkel* skel = SkelOf(player);
    Vec3s* table = player->skelAnime.jointTable;
    if (skel == nullptr || table == nullptr) {
        return false;
    }
    PoseLayer layers[3];
    int n = 0;
    if (ps.blend < 1.0f && ps.prev.anim != nullptr) {
        layers[n++] = { ps.prev.anim, ps.prev.time, 1.0f };
        layers[n++] = { ps.cur.anim, ps.cur.time, ps.blend };
    } else {
        layers[n++] = { ps.cur.anim, ps.cur.time, 1.0f };
    }
    if (ps.torso != nullptr && ps.torsoWeight > 0.0f) {
        layers[n++] = { ps.torso, ps.torsoTime, ps.torsoWeight };
    }
    static std::vector<BoneXf> world;
    Model_PoseWorld(sRef.skel, layers, n, &world);
    if (world.empty()) {
        return false;
    }

    std::memcpy(ps.saved, table, sizeof(ps.saved));
    ps.table = table;
    weight = std::clamp(weight, 0.0f, 1.0f);
    const bool legs = ps.legs;
    int mapOf[kLimbs];
    std::fill(std::begin(mapOf), std::end(mapOf), -1);
    for (int i = 0; i < kMapCount; i++) {
        mapOf[kMap[i].limb] = i;
    }
    Q limbWorld[kLimbs];
    for (int limb = 1; limb < kLimbs; limb++) {
        int parent = skel->parent[limb];
        Q parentWorld = parent > 0 ? limbWorld[parent] : Q();
        Q local = FromJoint(table[limb]);
        int m = mapOf[limb];
        if (m >= 0 && (legs || !kMap[m].leg)) {
            Q target = Mul(Mul(kToLink, BoneQ(world[sRef.bone[m]])), sRef.bindToRef[m]);
            Q wanted = Mul(Conj(parentWorld), target);
            local = Nlerp(local, wanted, weight);
            table[limb] = ToJoint(local);
            local = FromJoint(table[limb]);
        }
        limbWorld[limb] = Norm(Mul(parentWorld, local));
    }
    if (legs) {
        // The hips drop as Black Ops' do, scaled to Link's legs. Link's animations carry the
        // adult's root height, which the child's draw scales down: keep to their units.
        float drop = (world[sRef.pelvis].pos.y - sRef.pelvisY) * skel->legUnits;
        float rootY = static_cast<float>(table[0].y);
        float units = skel->restRootY > 0 ? rootY / static_cast<float>(skel->restRootY) : 1.0f;
        table[0].y = static_cast<s16>(std::lround(rootY + drop * units * weight));
    }

    // The gun at the right hand's tag_weapon_right, in the hand's own space.
    Q wrist = BoneQ(world[sRef.wristR]);
    Q tag = BoneQ(world[sRef.tagR]);
    Vec3f offset = Rot(Conj(wrist), Sub(world[sRef.tagR].pos, world[sRef.wristR].pos));
    ps.gunRot = Norm(Mul(sRef.handFrame, Mul(Conj(wrist), tag)));
    ps.gunPos = Scale(Rot(sRef.handFrame, offset), skel->armUnits);
    ps.armUnits = skel->armUnits;
    std::copy(std::begin(limbWorld), std::end(limbWorld), std::begin(ps.limbWorld));
    ps.posed = true;
    return true;
}

void PlayerAnim_Unpose(int who, Player* player) {
    if (who < 0 || who >= kMaxWho) {
        return;
    }
    PlayerState& ps = sState[who];
    if (ps.posed && player != nullptr && ps.table == player->skelAnime.jointTable) {
        std::memcpy(ps.table, ps.saved, sizeof(ps.saved));
    }
    ps.posed = false;
}

bool PlayerAnim_GunInHand(int who, float gunScale, const Vec3f& grip, MtxF* out) {
    if (who < 0 || who >= kMaxWho || !sState[who].posed) {
        return false;
    }
    const PlayerState& ps = sState[who];
    // The gun's display list is in inches times the model vertex scale. Scaled about the
    // grip: hand point = pos + arm * rot * (grip + (v - grip) * gunScale).
    float s = ps.armUnits * gunScale / Model_VertexScale();
    Vec3f shift = Scale(Rot(ps.gunRot, grip), ps.armUnits * (1.0f - gunScale));
    Vec3f ax = Scale(Rot(ps.gunRot, { 1, 0, 0 }), s);
    Vec3f ay = Scale(Rot(ps.gunRot, { 0, 1, 0 }), s);
    Vec3f az = Scale(Rot(ps.gunRot, { 0, 0, 1 }), s);
    out->xx = ax.x;
    out->yx = ax.y;
    out->zx = ax.z;
    out->wx = 0.0f;
    out->xy = ay.x;
    out->yy = ay.y;
    out->zy = ay.z;
    out->wy = 0.0f;
    out->xz = az.x;
    out->yz = az.y;
    out->zz = az.z;
    out->wz = 0.0f;
    out->xw = ps.gunPos.x + shift.x;
    out->yw = ps.gunPos.y + shift.y;
    out->zw = ps.gunPos.z + shift.z;
    out->ww = 1.0f;
    return true;
}

} // namespace Zombies
