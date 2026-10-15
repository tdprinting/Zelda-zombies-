#pragma once

// Zombies of Time: particle effects (ZombiesFx.cpp).
//
// Black Ops effects converted from the player's own install by tools/bomap
// (tools/bomap/docs/ZZFX_FORMAT.md) play through here; each role falls back to a look
// built in code, in Ocarina of Time's colours, when there is no import. Particles are
// camera-facing quads, capped in number so the cost stays small on phones.

// GameInteractor.h pulls in z64.h with the C++ headers it depends on already included.
#include "soh/Enhancements/game-interactor/GameInteractor.h"

namespace Zombies {

// Roles (manifest "fx_<role>"): box_beam, muzzle_flash, pap_flash, impact_flesh,
// impact_default, zombie_rise, powerup_on, powerup_grab, explosion, raygun_flash,
// raygun_trail, raygun_impact (and raygun_ug_*), barrier_break.
//
// Plays an effect at pos. dir is the way it points (a surface normal, the barrel; unit
// length), scale multiplies its size. Returns a handle for a looping effect, 0 if nothing
// plays.
int Fx_Play(const char* role, const Vec3f& pos, const Vec3f& dir, float scale = 1.0f);
// Pointing up.
int Fx_PlayUp(const char* role, const Vec3f& pos, float scale = 1.0f);
void Fx_Move(int handle, const Vec3f& pos); // a looping effect follows something
void Fx_Stop(int handle);                   // stops spawning; what's out finishes
bool Fx_Alive(int handle);
void Fx_Clear(); // everything gone at once (scene change, run end)

// The hooks the gun code calls.
void Fx_GunFlash(const char* boName, bool pap, const Vec3f& muzzle, const Vec3f& dir);
void Fx_Impact(const Vec3f& pos, const Vec3f& normal); // a bullet on a wall
void Fx_Blood(const Vec3f& pos, const Vec3f& dir);     // a hit on an undead
void Fx_Explosion(const Vec3f& pos);
void Fx_RayGunTrail(const Vec3f& pos, const Vec3f& dir, bool pap);
void Fx_RayGunImpact(const Vec3f& pos, bool pap);

// Draws the particles for the view being drawn (called with the world, once per view).
void Fx_Draw(PlayState* play);

} // namespace Zombies
