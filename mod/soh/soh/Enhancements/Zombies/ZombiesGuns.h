#pragma once

// Zombies of Time: Black Ops guns.
//
// The first half of this header is pure data and rules (the gun table, ammo and slots),
// with no game state, so tests/rules_test.cpp can check it. The second half is the game
// side in ZombiesGuns.cpp: firing, reloading, Link's gun poses, the first-person
// viewmodel, the HUD and the hooks the other Zombies files call.
//
// Every number in the table is a Black Ops gameplay fact (damage, clip, reserve, fire
// rate, reload time, wall price, box odds) written down in our own table. No Black Ops
// code or data is in the repository: gun models and viewmodel animations are imported at
// run time from the player's own install by tools/bomap (see Guns_ImportedModel). Without
// them Link holds an Ocarina of Time stand-in (the hookshot or slingshot). The guns use
// OoT sounds.

#include <cstdint>
#include <string>

namespace Zombies {

enum class GunId : uint8_t {
    None,
    M1911,
    Olympia,
    M14,
    MP5K,
    AK74u,
    MP40,
    Stakeout,
    Spectre,
    Galil,
    Commando,
    Famas,
    HK21,
    RPK,
    RayGun,
    Count,
};

// How Link holds it, and which pose set animates him.
enum class GunClass : uint8_t {
    Pistol,   // one hand
    Smg,      // two hands, held low
    Rifle,    // two hands, shouldered
    Shotgun,  // two hands, pump between shots
    Lmg,      // two hands, heavy
    Launcher, // the Ray Gun: one hand, a slow green bolt
    Count,
};

struct GunVariant {
    const char* name;
    float damage;     // Black Ops damage per bullet (per pellet for shotguns)
    int clip;         // rounds per magazine
    int reserve;      // rounds carried beside the magazine, at most
    float splash;     // splash damage at the impact point, 0 for none
    float splashRadius;
};

struct GunStats {
    GunId id;
    GunClass gunClass;
    GunVariant base;
    GunVariant pap;     // Pack-a-Punched
    float headshot;     // damage multiplier for a hit on the head
    int pellets;        // bullets per shot (shotguns fire several)
    bool automatic;     // fires while the button is held
    float fireFrames;   // frames between shots at 20 updates a second
    int reloadFrames;   // frames for a reload from empty
    float spreadDeg;    // hip-fire cone half-angle; aiming in first person tightens it
    float range;        // OoT units: beyond this a bullet does nothing
    int pierce;         // undead one bullet can pass through
    int wallCost;       // 0: not a wall buy
    int wallZone;       // authored arenas: the zone whose debris has to be cleared first
    int boxWeight;      // Mystery Chest weight, 0: not in the chest
    bool projectile;    // fires a travelling bolt instead of a hitscan bullet
    const char* boName; // the Black Ops asset name the importer looks the model up by
};

const GunStats& GunInfo(GunId id);
const GunVariant& GunVariantOf(GunId id, bool pap);
const char* GunName(GunId id, bool pap);

// Black Ops: wall ammo costs half the gun's price; a Pack-a-Punched gun's ammo costs 4500.
constexpr int kPapAmmoCost = 4500;
int GunAmmoCost(GunId id, bool pap);

constexpr int kGunSlots = 2;    // two guns, as in Black Ops (Mule Kick would make three)
constexpr int kMaxGunSlots = 3; // room for a third slot

struct GunSlot {
    GunId id = GunId::None;
    bool pap = false;
    int clip = 0;
    int reserve = 0;
};

// One player's guns. Kept per player from the start: player 1 is Link, player 2 the
// co-op partner (whose guns aren't wired to their controls yet).
struct PlayerGuns {
    GunSlot slots[kMaxGunSlots];
    int slotCount = kGunSlots;
    int current = 0;

    const GunSlot& Current() const {
        return slots[current];
    }
    GunSlot& Current() {
        return slots[current];
    }
    bool Has(GunId id) const;
    int Count() const;
    // Puts a new gun in an empty slot, or in place of the one in hand when both are full
    // (Black Ops). The gun comes full. Returns the slot.
    int Give(GunId id);
    // Fills the clip from the reserve. Returns the rounds moved.
    int Reload();
    bool CanReload() const;
    // Max Ammo: every gun's reserve full (Black Ops leaves the clip alone).
    void MaxAmmo();
    // Wall ammo: clip and reserve full.
    void Refill(int slot);
    void PackAPunch(int slot);
    // The next slot that holds a gun, after the current one.
    int NextSlot() const;
};

// Mystery Chest: total weight of the guns this player could get (never one they hold),
// and the gun at a roll in [0, total).
int GunBoxWeight(const PlayerGuns& guns);
GunId GunBoxPick(const PlayerGuns& guns, int roll);

} // namespace Zombies

// ---------------------------------------------------------------------------
// Game side (ZombiesGuns.cpp)

struct PlayState;
struct Player;

namespace Zombies {

struct TrackedEnemy;

// A gun's imported models (manifest roles gun_<boName>_world, _view, _anim_fire,
// _anim_reload, _anim_draw, _anim_idle), loaded on first use and kept. Null when
// neither model was imported: Link then holds an OoT stand-in.
struct GunModel;
GunModel* Guns_ImportedModel(const char* boName);

PlayerGuns& Guns_Of(int player);

void Guns_NewRun(PlayState* play);   // a run starts: everyone gets the M1911
void Guns_EndRun();                  // the run is over
void Guns_Update(PlayState* play, Player* player);
bool Guns_FiredRecently(int player); // the kill that just happened was a gun's
void Guns_MaxAmmo();
// HUD: the gun's name and button, "clip | reserve" and a reload hint, bottom right as in
// Black Ops; crosshair while aiming in first person. False when there's nothing to show.
bool Guns_HudInfo(std::string* name, std::string* ammo, std::string* hint, bool* crosshair);

// The C button the guns live on: 1 C-Left, 2 C-Down, 3 C-Right. AutoEquip leaves it empty.
int Guns_Button();

// Round scaling for OoT items, applied to a hit the mode just saw on an undead. Returns
// the enemy's new health (lower than `health` when the item's Black Ops tier hits harder).
int Guns_ScaleItemHit(PlayState* play, Player* player, TrackedEnemy& enemy, int health);

// Armory rack: entry 0 is the Zelda item offer the station already had; the rest are wall
// guns. Returns false on entry 0, so the station falls back to its own offer.
bool Guns_ArmoryOffer(int who, std::string* label, int* cost, bool* available, std::string* reason);
bool Guns_ArmoryBuy(PlayState* play, int who);
// R at the Armory: the next rack entry. Returns the prompt suffix to show.
const char* Guns_ArmoryBrowse(PlayState* play, int who);

// Mystery Chest: weight of the guns in the roll for this player, and handing one over.
int Guns_ChestWeight(int who);
const char* Guns_ChestGive(PlayState* play, int who, int roll);

// Pack-a-Punch: whether the gun in hand is what gets punched (it is when Link used it more
// recently than any Zelda item, or with anyRecency when nothing else can be), its new
// name, and doing it.
bool Guns_PapWanted(int who, bool anyRecency = false);
const char* Guns_PapName(int who);
void Guns_PackAPunch(PlayState* play, int who);

} // namespace Zombies
