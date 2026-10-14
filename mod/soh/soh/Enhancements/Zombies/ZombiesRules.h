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
};

enum class PowerUp {
    MaxAmmo,
    InstaKill,
    DoublePoints,
    Nuke,
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

// Wolfos rounds: the first lands on round 5-7, then every 4-6 rounds.
// roll is any value in [0, 1).
int FirstDogRound(float roll);
int NextDogRound(int currentDogRound, float roll);
int DogsForRound(int dogRoundIndex, int players);

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
// A 2% random chance also applies. Capped per round.
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

PowerUp PickPowerUp(float roll, bool boxAvailable);

const char* PowerUpName(PowerUp p);

} // namespace Zombies
