#include "ZombiesRules.h"

#include <algorithm>
#include <cmath>

namespace Zombies {

int ZombiesForRound(int round, int players) {
    if (round < 1) {
        round = 1;
    }
    players = std::clamp(players, 1, kMaxPlayers);

    constexpr int maxAi = 24;
    constexpr int aiPerPlayer = 6;

    float multiplier = round / 5.0f;
    if (multiplier < 1.0f) {
        multiplier = 1.0f;
    }
    if (round >= 10) {
        multiplier *= round * 0.15f;
    }

    int total = maxAi;
    if (players == 1) {
        total += static_cast<int>(0.5f * aiPerPlayer * multiplier);
    } else {
        total += static_cast<int>((players - 1) * aiPerPlayer * multiplier);
    }

    static constexpr float earlyRoundFactor[] = { 0.25f, 0.3f, 0.5f, 0.7f, 0.9f };
    if (round <= 5) {
        total = static_cast<int>(total * earlyRoundFactor[round - 1]);
    }
    return std::max(total, 1);
}

float SpawnDelaySeconds(int round) {
    float delay = 2.0f * std::pow(0.95f, static_cast<float>(std::max(round, 1) - 1));
    return std::max(delay, 0.25f);
}

float HealthMultiplier(int round, int scalingPercent) {
    round = std::max(round, 1);
    float mult;
    if (round <= 10) {
        mult = 1.0f + 0.15f * (round - 1);
    } else {
        mult = (1.0f + 0.15f * 9) * std::pow(1.08f, static_cast<float>(round - 10));
    }
    // The slider scales only the growth, so round 1 stays at the enemy's own HP.
    float scale = std::max(scalingPercent, 0) / 100.0f;
    return 1.0f + (mult - 1.0f) * scale;
}

uint8_t ScaleHealth(int baseHp, int round, int scalingPercent) {
    float hp = std::round(baseHp * HealthMultiplier(round, scalingPercent));
    return static_cast<uint8_t>(std::clamp(static_cast<int>(hp), 1, 255));
}

int PointsForKill(KillType type) {
    switch (type) {
        case KillType::Melee:
            return kPointsMeleeKill;
        case KillType::Ranged:
            return kPointsRangedKill;
        default:
            return kPointsKill;
    }
}

int FirstDogRound(float roll) {
    return 5 + std::min(static_cast<int>(roll * 3), 2);
}

int NextDogRound(int currentDogRound, float roll) {
    return currentDogRound + 4 + std::min(static_cast<int>(roll * 2), 1);
}

int DogsForRound(int dogRoundIndex, int players) {
    players = std::clamp(players, 1, kMaxPlayers);
    int perPlayer = dogRoundIndex <= 2 ? 6 : 8;
    return perPlayer * players;
}

int HitStep(int capacity, bool juggernog) {
    int hits = juggernog ? 5 : 2;
    return std::max(1, (capacity + hits - 1) / hits);
}

int HealthAfterHit(int health, int capacity, bool juggernog) {
    int step = HitStep(capacity, juggernog);
    int left = health - step;
    return left * 4 < step ? 0 : left;
}

uint8_t DogHealth(int dogRoundIndex, int baseHp) {
    // Hellhounds get tougher each dog round, not each round: half again per dog round.
    int hp = baseHp + (baseHp * std::max(dogRoundIndex - 1, 0)) / 2;
    return static_cast<uint8_t>(std::clamp(hp, 1, 255));
}

MoveTier PickMoveTier(int round, float roll) {
    // Black Ops rolls each zombie's speed upward from the round's base, never below it:
    // base + 0..34. Round 1's base is 1, so round 1 is all walkers.
    int base = round <= 1 ? 1 : round * 8;
    int speed = base + std::min(static_cast<int>(roll * 35.0f), 34);
    if (speed <= 35) {
        return MoveTier::Walk;
    }
    if (speed <= 70) {
        return MoveTier::Run;
    }
    return MoveTier::Sprint;
}

float PullSpeed(MoveTier tier) {
    switch (tier) {
        case MoveTier::Sprint:
            return 6.0f;
        case MoveTier::Run:
            return 4.0f;
        case MoveTier::Crawl:
            return 1.0f;
        default:
            return 2.0f;
    }
}

EnemyKind PickEnemy(int round, float roll, bool isDay) {
    // Stalchildren burrow away in daylight, so day runs use Redeads in their place.
    EnemyKind weakest = isDay ? EnemyKind::Redead : EnemyKind::Stalchild;

    if (round <= 2) {
        return weakest;
    }
    if (round <= 5) {
        return roll < 0.7f ? weakest : EnemyKind::Redead;
    }
    if (round <= 9) {
        if (roll < 0.4f) {
            return weakest;
        }
        return roll < 0.8f ? EnemyKind::Redead : EnemyKind::Gibdo;
    }
    if (roll < 0.25f) {
        return weakest;
    }
    if (roll < 0.55f) {
        return EnemyKind::Redead;
    }
    if (roll < 0.8f) {
        return EnemyKind::Gibdo;
    }
    return isDay ? EnemyKind::Gibdo : EnemyKind::BigStalchild;
}

bool DropTracker::OnKill(int totalPointsEarned, float roll) {
    if (dropsThisRound >= kMaxDropsPerRound) {
        return false;
    }
    bool drop = false;
    if (totalPointsEarned >= threshold) {
        increment = static_cast<int>(increment * 1.14f);
        threshold = totalPointsEarned + increment;
        drop = true;
    } else if (roll < 0.03f) {
        drop = true;
    }
    if (drop) {
        dropsThisRound++;
    }
    return drop;
}

PowerUp PowerUpDeck::Draw(float (*rng)(), bool boxAvailable, bool barricadesAvailable) {
    constexpr int kCount = static_cast<int>(PowerUp::Count);
    for (int tries = 0; tries < 2 * kCount; tries++) {
        if (next >= kCount) {
            for (int i = 0; i < kCount; i++) {
                order[i] = static_cast<PowerUp>(i);
            }
            for (int i = kCount - 1; i > 0; i--) {
                int j = std::min(static_cast<int>(rng() * (i + 1)), i);
                std::swap(order[i], order[j]);
            }
            next = 0;
        }
        PowerUp p = order[next++];
        if ((p == PowerUp::FireSale && !boxAvailable) || (p == PowerUp::Carpenter && !barricadesAvailable)) {
            continue;
        }
        return p;
    }
    return PowerUp::MaxAmmo;
}

bool ChestMoves(int uses, bool everMoved, float roll) {
    if (uses <= 4) {
        return false;
    }
    if (uses <= 8) {
        return roll < 0.15f;
    }
    if (!everMoved) {
        return true;
    }
    return roll < (uses <= 13 ? 0.3f : 0.5f);
}

int RepairPoints(int repairPointsThisRound) {
    return std::clamp(kRepairPointsCapPerRound - repairPointsThisRound, 0, kPointsPerBoard);
}

const char* PowerUpName(PowerUp p) {
    switch (p) {
        case PowerUp::MaxAmmo:
            return "MAX AMMO";
        case PowerUp::InstaKill:
            return "INSTA-KILL";
        case PowerUp::DoublePoints:
            return "DOUBLE POINTS";
        case PowerUp::Nuke:
            return "KABOOM";
        case PowerUp::Carpenter:
            return "CARPENTER";
        case PowerUp::FireSale:
            return "FIRE SALE";
        default:
            return "";
    }
}

int BoZombieHealth(int round) {
    double health = 150.0;
    for (int r = 2; r <= round; r++) {
        if (r < 10) {
            health += 100.0;
        } else {
            health += std::floor(health * 0.1);
        }
        if (health > 1e9) {
            return 1000000000;
        }
    }
    return static_cast<int>(health);
}

float BoDamageToHp(float boDamage, int maxHp, int round, int scalingPercent) {
    float scale = std::max(scalingPercent, 0) / 100.0f;
    float boHealth = 150.0f + (static_cast<float>(BoZombieHealth(round)) - 150.0f) * scale;
    return boDamage * static_cast<float>(std::max(maxHp, 1)) / std::max(boHealth, 1.0f);
}

int OotWeaponBoDamage(OotWeapon weapon) {
    // Matched to a gun of the item's tier by damage per second at OoT's own fire rate: the
    // bow looses about an arrow every 0.6 s against the M14's 105 at up to 6.7 shots a second.
    switch (weapon) {
        case OotWeapon::Slingshot:
            return 250; // the child's M14: Armory, 500
        case OotWeapon::Bow:
            return 500; // M14 / MP40 tier
        case OotWeapon::FireArrow:
            return 1000; // and it burns
        case OotWeapon::IceArrow:
            return 800; // and it freezes
        case OotWeapon::LightArrow:
            return 2000; // Ray Gun tier, paid for in magic
        case OotWeapon::Boomerang:
            return 300; // stuns, hits several, comes back: no ammo at all
        case OotWeapon::Hookshot:
            return 600; // stuns and pulls
        case OotWeapon::Longshot:
            return 800;
        case OotWeapon::Hammer:
            return 1500; // slow, but a Bowie knife and a half
        case OotWeapon::DekuStick:
            return 500; // until it breaks
        case OotWeapon::Bomb:
            return 800; // a grenade: one-shots to round 7, as Black Ops frags do
        case OotWeapon::Bombchu:
            return 1000;
        case OotWeapon::DinsFire:
            return 1000000000; // the Thundergun: nothing in the blast survives
        case OotWeapon::NayrusBlade:
            return 1000; // the sword as a Bowie knife while Nayru's Love is up
        default:
            return 0;
    }
}

const char* OotWeaponName(OotWeapon weapon) {
    static const char* names[] = { "",         "SLINGSHOT", "BOW",         "FIRE ARROW", "ICE ARROW",
                                   "LIGHT ARROW", "BOOMERANG", "HOOKSHOT",  "LONGSHOT",   "HAMMER",
                                   "DEKU STICK", "BOMB",      "BOMBCHU",   "DIN'S FIRE", "NAYRU'S BLADE" };
    int i = static_cast<int>(weapon);
    return (i >= 0 && i < static_cast<int>(OotWeapon::Count)) ? names[i] : "";
}

int ScaledItemDamage(int vanillaDamage, OotWeapon weapon, int maxHp, int round, int scalingPercent) {
    int bo = OotWeaponBoDamage(weapon);
    if (bo <= 0) {
        return vanillaDamage;
    }
    float hp = BoDamageToHp(static_cast<float>(bo), maxHp, round, scalingPercent);
    int scaled = hp >= 1e6f ? 1000000 : static_cast<int>(std::lround(hp));
    return std::max(vanillaDamage, scaled);
}

} // namespace Zombies
