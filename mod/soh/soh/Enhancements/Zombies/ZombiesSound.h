#pragma once

// Zombies of Time: Black Ops sounds (ZombiesSound.cpp).
//
// `bomap gamesound` (tools/bomap) converts the zombies sounds from the player's own Black
// Ops install into WAV files in zombies/sound/, with a manifest naming which alias plays
// each role. This small mixer loads them on first use and mixes them into SoH's audio
// output, after Ocarina of Time's own synth, with each alias's volume, pitch range and
// distances, panned and faded against the nearest player.
//
// Every call returns false (or 0) when its role or alias wasn't imported: the caller
// then plays its Ocarina of Time sound, so nothing changes without the import.

// GameInteractor.h pulls in z64.h with the C++ headers it depends on already included.
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <cstdint>
#include <string>

namespace Zombies {

// Roles (manifest "role <role> <alias>..."), named in tools/bomap/src/sound_export.cpp:
// purchase, deny, wall_buy, ammo_buy, door_open, debris_clear, board_break, board_tear,
// board_repair, board_slam, box_open, box_close, box_music, box_teddy, box_move, box_poof,
// box_land, box_laugh, powerup_spawn, powerup_loop, powerup_grab, powerup_max_ammo,
// powerup_insta_kill, powerup_double_loop, powerup_nuke, powerup_carpenter, powerup_end,
// perk_loop, perk_jingle, perk_dispense, perk_open, perk_drink, perk_burp, perk_deny,
// power_switch, power_on, pap_upgrade, pap_ready, pap_deny, pap_knuckle, zombie_amb,
// zombie_attack, zombie_sprint, zombie_death, zombie_behind, zombie_board, zombie_crawler,
// zombie_swipe, zombie_hit, zombie_headshot, zombie_gib, zombie_spawn, dog_round_start,
// player_hit, player_heartbeat, player_down, raygun_impact, explosion, amb_spooky,
// round_start, round_end, game_over; and per gun (Black Ops weapon name w):
// gun_<w>_fire, gun_<w>_fire_last, gun_<w>_dry, gun_<w>_raise, gun_<w>_putaway.

bool Sound_Available();             // zombies/sound/manifest.txt was read
bool Sound_Has(const char* role);   // the role was imported
bool Sound_HasAlias(const std::string& alias);

// A one-shot. pos null: heard as is, wherever the players are (music, announcer, menus).
// volume multiplies the alias's own.
bool Sound_Play(const char* role, const Vec3f* pos = nullptr, float volume = 1.0f);
bool Sound_PlayAlias(const std::string& alias, const Vec3f* pos = nullptr, float volume = 1.0f);

// A looping sound (the role's alias loops or not). Returns a handle, 0 if none.
int Sound_Loop(const char* role, const Vec3f* pos = nullptr, float volume = 1.0f);
void Sound_Move(int handle, const Vec3f& pos);
void Sound_Stop(int handle);
void Sound_StopAll(); // run over, scene change
// A loop that plays while it's kept: call once a frame with the same key (the thing that
// makes the sound: a power-up drop, a perk machine); it stops when the calls stop.
bool Sound_Keep(const void* key, const char* role, const Vec3f* pos, float volume = 1.0f);

// Gun hooks (ZombiesGuns.cpp). boName is the gun's Black Ops weapon (m1911_zm);
// fallbackBo another weapon of its class to borrow from. Pack-a-Punched guns use the
// upgraded weapon's sound when there is one.
bool Sound_GunFire(const char* boName, bool pap, const char* fallbackBo, const Vec3f* pos);
bool Sound_GunRole(const char* boName, const char* what, const Vec3f* pos); // "dry", "raise", "putaway"
// Plays every "sndnt#<alias>" notetrack of a view animation that falls in (from, to]
// (reload clip out / in, bolt, rechamber). Returns whether the animation had any.
struct ZAnim;
bool Sound_AnimNotes(const ZAnim* anim, float fromSec, float toSec, const Vec3f* pos);

// Once a frame from the mode's update: the listeners (each player and the way their
// camera faces), Nacht der Untoten's ambience, and the volume settings.
void Sound_Update(PlayState* play, bool ambience);

// Test seams for the harness: every sound started is reported to the log, and every mixed
// output buffer (interleaved stereo at the output rate) to the capture.
void Sound_SetLog(void (*log)(const char* what, const char* file));
void Sound_SetCapture(void (*capture)(const int16_t* samples, uint32_t frames, uint32_t rate));

} // namespace Zombies

// Called by the audio thread (mod/patches/0004-zombies-sound-mix.patch) with each frame of
// SoH's output: interleaved stereo, 16-bit.
extern "C" void ZombiesSound_Mix(int16_t* samples, uint32_t frames, uint32_t rate);
