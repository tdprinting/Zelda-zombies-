#pragma once

// Pure formulas for Zombies of Time: round sizes, health scaling, spawn pacing,
// points and power-up drops. Nothing here touches game state, so it can be unit
// tested on its own (see tests/rules_test.cpp).

#include <cstdint>

namespace Zombies {

constexpr int kMaxPlayers = 4;

// Points, after the Black Ops values.
constexpr int kStartingPoints = 500;
constexpr int kPointsPerHit = 10;
constexpr int kPointsKill = 60;
constexpr int kPointsRangedKill = 100; // stands in for a headshot
constexpr int kPointsMeleeKill = 130;  // stands in for a knife kill
constexpr int kPointsNuke = 400;
constexpr int kPointsCarpenter = 200;

// Barricades: Black Ops windows hold six boards. Each board Link puts back is worth
// 10 points, up to a per-round cap so repairs can't be farmed forever.
constexpr int kBoardsPerWindow = 6;
constexpr int kPointsPerBoard = 10;
constexpr int kRepairPointsCapPerRound = 500;

constexpr int kPowerUpSeconds = 30;
constexpr int kMaxDropsPerRound = 4;

enum class KillType { Other, Ranged, Melee };

enum class EnemyKind {
    Stalchild,
    BigStalchild,
    Redead,
    Gibdo,
    WhiteWolfos,
};

// Black Ops move speeds: zombies walk early, then a growing share run and sprint.
enum class MoveTier {
    Walk,
    Run,
    Sprint,
    Crawl, // legs blown off by an explosive
};

enum class PowerUp {
    MaxAmmo,
    InstaKill,
    DoublePoints,
    Nuke,
    Carpenter,
    FireSale,
    Count,
};

// Total undead in a round (Black Ops solo/co-op formula, rounds 1-5 reduced).
int ZombiesForRound(int round, int players);

// Seconds between spawns: 2.0 * 0.95^(round-1), floored.
float SpawnDelaySeconds(int round);

// Multiplier on an enemy's own base HP. scalingPercent is the menu slider (100 = default).
float HealthMultiplier(int round, int scalingPercent);

// Base HP scaled for a round, clamped to OoT's one-byte health.
uint8_t ScaleHealth(int baseHp, int round, int scalingPercent);

int PointsForKill(KillType type);

// Wolfos rounds: the first lands on round 5-7, then every 4-5 rounds. Six per player for
// the first two, eight per player after that.
// roll is any value in [0, 1).
int FirstDogRound(float roll);
int NextDogRound(int currentDogRound, float roll);
int DogsForRound(int dogRoundIndex, int players);

// Black Ops health: a fixed number of hits puts a player down whatever hits them, two
// normally and five with Juggernog, and health comes back in full a few seconds after
// the last hit. A hit costs this much of capacity (rounded up).
int HitStep(int capacity, bool juggernog);
// Health left after one hit, from health before it. A sliver under a quarter of a step
// counts as down, so a part-regenerated player still goes down on schedule.
int HealthAfterHit(int health, int capacity, bool juggernog);

// A Wolfos's health on a Wolfos round: fixed for the round, not the round number's scaling,
// as hellhounds have their own health curve.
uint8_t DogHealth(int dogRoundIndex, int baseHp);

// Move tier for one zombie. Black Ops rolls round * 8 plus some random spread
// against walk (<= 35) and run (<= 70) cut-offs. roll is any value in [0, 1).
MoveTier PickMoveTier(int round, float roll);

// How far Horde Pull moves an undead per frame for its tier, in OoT units.
float PullSpeed(MoveTier tier);

// Which undead to spawn this round. roll is any value in [0, 1).
EnemyKind PickEnemy(int round, float roll, bool isDay);

// Power-up drop bookkeeping: every time total points earned crosses the
// threshold the next kill drops a power-up, and the gap to the next threshold
// grows by 14%.
// A 3% random chance also applies. Capped per round. In co-op the points are everyone's
// together.
struct DropTracker {
    int increment = 2000;
    int threshold = 2000;
    int dropsThisRound = 0;

    void NewRound() {
        dropsThisRound = 0;
    }
    // Call once per kill. Returns true if this kill should drop a power-up.
    bool OnKill(int totalPointsEarned, float roll);
};

// Black Ops deals power-ups from a shuffled deck rather than rolling each one, so the same
// one rarely comes twice running. Cards that can't drop right now are skipped (Fire Sale
// without a Mystery Chest that has moved, Carpenter with nothing broken); the deck is
// reshuffled when it runs out. rng returns any value in [0, 1).
struct PowerUpDeck {
    PowerUp order[static_cast<int>(PowerUp::Count)] = {};
    int next = static_cast<int>(PowerUp::Count);

    PowerUp Draw(float (*rng)(), bool boxAvailable, bool barricadesAvailable);
};

// Whether this use of the Mystery Chest is the teddy bear (the chest moves). uses counts
// this one, since the chest last moved. Never in the first four; 15% for uses 5-8; from
// the ninth a chest that has never moved always does, otherwise 30%, then 50% from the 14th.
bool ChestMoves(int uses, bool everMoved, float roll);

// Points for putting one board back, given what repairs already paid this round.
int RepairPoints(int repairPointsThisRound);

const char* PowerUpName(PowerUp p);

// ---------------------------------------------------------------------------
// Weapon damage. Gun damage is written in Black Ops units, against Black Ops zombie health
// (150 on round 1, +100 a round to round 9, then x1.1 a round). A hit takes the same share
// of one of our undead's health as it would of a Black Ops zombie's on that round, so
// shots-to-kill match Black Ops at every round, whatever OoT enemy it is.

int BoZombieHealth(int round);

// HP a hit of boDamage takes off an enemy whose full health is maxHp. scalingPercent is
// the Health Scaling slider, which scales Black Ops' growth the same way it scales ours.
float BoDamageToHp(float boDamage, int maxHp, int round, int scalingPercent);

// Zelda items, each given a Black Ops damage equivalent for its tier, so they keep up
// with the guns as the rounds climb. The sword is the knife and keeps its own damage.
enum class OotWeapon {
    None,
    Slingshot,
    Bow,
    FireArrow,
    IceArrow,
    LightArrow,
    Boomerang,
    Hookshot,
    Longshot,
    Hammer,
    DekuStick,
    Bomb,
    Bombchu,
    DinsFire,
    NayrusBlade, // the sword while Nayru's Love is up
    Count,
};

// Black Ops damage equivalent per hit, 0 for weapons left at their OoT damage.
int OotWeaponBoDamage(OotWeapon weapon);
const char* OotWeaponName(OotWeapon weapon);

// Damage an OoT item's hit deals: its Black Ops equivalent turned into HP as for a gun,
// never less than the item's own damage, which carries it in the late rounds where
// Black Ops health outgrows ours.
int ScaledItemDamage(int vanillaDamage, OotWeapon weapon, int maxHp, int round, int scalingPercent);

} // namespace Zombies
