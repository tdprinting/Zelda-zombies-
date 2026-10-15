#pragma once

// Link holding a gun the way a Black Ops player does: Black Ops' own third-person player
// animations, imported from the player's install (bomap playeranims), retargeted onto
// Link's skeleton at run time (ZombiesPlayerAnim.cpp). Without the import ZombiesGuns
// keeps its own pose table.

#include "soh/Enhancements/game-interactor/GameInteractor.h"

namespace Zombies {

// Black Ops' weapon kinds for player animations. Pistol covers the Ray Gun; the rest are
// two-handed and fall back to the rifle's animations where Black Ops has none of their own.
enum class PlayerAnimKind : u8 {
    Pistol,
    Rifle,
    Smg,
    Shotgun,
    Lmg,
};

// What the gun is doing this frame, from ZombiesGuns.
struct PlayerAnimGun {
    PlayerAnimKind kind = PlayerAnimKind::Rifle;
    bool aiming = false;  // Z-targeting with the gun: Black Ops' aiming stance
    bool fired = false;   // a shot this frame
    bool sprint = false;  // running with nothing to shoot at
    float reload = -1.0f; // 0..1 through a reload, below 0 when none
    float swap = -1.0f;   // 0..1 through a swap (the gun changes at half), below 0 when none
};

// Whether the import has what this kind of gun needs.
bool PlayerAnim_Available(PlayerAnimKind kind);
// Once per game frame for each player whose gun is up: steps the animations.
void PlayerAnim_Update(int who, Player* player, const PlayerAnimGun& gun);
// The gun is down: the next raise starts fresh.
void PlayerAnim_Reset(int who);
// Just before the draw: lays the retargeted pose over Link's animated joints at `weight`
// (0..1). Returns false when it didn't (then nothing changed).
bool PlayerAnim_Pose(int who, Player* player, float weight);
// Right after the draw: puts Link's joints back.
void PlayerAnim_Unpose(int who, Player* player);
// The held gun's place in Link's right hand, as Black Ops holds it: a matrix from the
// gun model's space (its display list units) to the hand limb's. The gun is sized to
// Link's arms times gunScale, about its grip (gun model inches), so the grip stays in
// the hand. False when not posed.
bool PlayerAnim_GunInHand(int who, float gunScale, const Vec3f& grip, MtxF* out);

} // namespace Zombies
