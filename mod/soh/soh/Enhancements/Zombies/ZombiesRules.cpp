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
    return currentDogRound + 4 + std::min(static_cast<int>(roll * 3), 2);
}

int DogsForRound(int dogRoundIndex, int players) {
    players = std::clamp(players, 1, kMaxPlayers);
    int perPlayer = std::min(6 + 2 * std::max(dogRoundIndex - 1, 0), 12);
    return perPlayer * players;
}

MoveTier PickMoveTier(int round, float roll) {
    int speed = std::max(round, 1) * 8 + static_cast<int>((roll - 0.5f) * 30.0f);
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
        default:
            return 2.0f;
    }
}

EnemyKind PickEnemy(int round, float roll, bool isDay) {
    // Stalchildren burrow away in daylight, so day runs use Redeads in their place.
    EnemyKind small = isDay ? EnemyKind::Redead : EnemyKind::Stalchild;

    if (round <= 2) {
        return small;
    }
    if (round <= 5) {
        return roll < 0.7f ? small : EnemyKind::Redead;
    }
    if (round <= 9) {
        if (roll < 0.4f) {
            return small;
        }
        return roll < 0.8f ? EnemyKind::Redead : EnemyKind::Gibdo;
    }
    if (roll < 0.25f) {
        return small;
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
    } else if (roll < 0.02f) {
        drop = true;
    }
    if (drop) {
        dropsThisRound++;
    }
    return drop;
}

PowerUp PickPowerUp(float roll, bool boxAvailable) {
    int count = static_cast<int>(PowerUp::Count) - (boxAvailable ? 0 : 1);
    int index = std::min(static_cast<int>(roll * count), count - 1);
    return static_cast<PowerUp>(index);
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
        case PowerUp::FireSale:
            return "FIRE SALE";
        default:
            return "";
    }
}

} // namespace Zombies
