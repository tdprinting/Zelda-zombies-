// Unit tests for the pure Zombies of Time formulas. Build and run with:
//   ./scripts/test.sh

#include "../mod/soh/soh/Enhancements/Zombies/ZombiesRules.h"

#include <cstdio>
#include <cstdlib>

using namespace Zombies;

static int sFailures = 0;

#define EXPECT_EQ(actual, expected)                                                                            \
    do {                                                                                                       \
        auto a_ = (actual);                                                                                    \
        auto e_ = (expected);                                                                                  \
        if (!(a_ == e_)) {                                                                                     \
            std::printf("FAIL %s:%d: %s == %d, expected %d\n", __FILE__, __LINE__, #actual, (int)a_, (int)e_); \
            sFailures++;                                                                                       \
        }                                                                                                      \
    } while (0)

#define EXPECT_TRUE(cond)                                               \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            sFailures++;                                                \
        }                                                               \
    } while (0)

static void TestRoundSizes() {
    // Solo Black Ops values: 24 + 3*multiplier, with the early-round reduction.
    EXPECT_EQ(ZombiesForRound(1, 1), 6);   // 27 * 0.25
    EXPECT_EQ(ZombiesForRound(2, 1), 8);   // 27 * 0.3
    EXPECT_EQ(ZombiesForRound(3, 1), 13);  // 27 * 0.5
    EXPECT_EQ(ZombiesForRound(5, 1), 24);  // 27 * 0.9
    EXPECT_EQ(ZombiesForRound(6, 1), 27);  // multiplier 1.2 -> 24 + 3
    EXPECT_EQ(ZombiesForRound(10, 1), 33); // multiplier 2 * 1.5 = 3 -> 24 + 9
    EXPECT_EQ(ZombiesForRound(20, 1), 60); // multiplier 4 * 3 = 12 -> 24 + 36
    // Two players add 6 per extra player per multiplier.
    EXPECT_EQ(ZombiesForRound(6, 2), 31); // 24 + 7.2
    // Rounds always grow after round 5.
    for (int r = 6; r < 60; r++) {
        EXPECT_TRUE(ZombiesForRound(r + 1, 1) >= ZombiesForRound(r, 1));
    }
}

static void TestSpawnDelay() {
    EXPECT_TRUE(SpawnDelaySeconds(1) > 1.99f && SpawnDelaySeconds(1) < 2.01f);
    EXPECT_TRUE(SpawnDelaySeconds(10) < SpawnDelaySeconds(5));
    EXPECT_TRUE(SpawnDelaySeconds(200) >= 0.25f);
}

static void TestHealth() {
    EXPECT_EQ(ScaleHealth(8, 1, 100), 8);
    EXPECT_EQ(ScaleHealth(8, 5, 100), 13);  // 8 * 1.6 = 12.8
    EXPECT_EQ(ScaleHealth(8, 10, 100), 19); // 8 * 2.35 = 18.8
    EXPECT_EQ(ScaleHealth(8, 10, 0), 8);    // scaling off
    EXPECT_EQ(ScaleHealth(8, 200, 100), 255);
    EXPECT_EQ(ScaleHealth(2, 1, 100), 2);
    for (int r = 1; r < 80; r++) {
        EXPECT_TRUE(ScaleHealth(8, r + 1, 100) >= ScaleHealth(8, r, 100));
    }
}

static void TestDogRounds() {
    EXPECT_EQ(FirstDogRound(0.0f), 5);
    EXPECT_EQ(FirstDogRound(0.99f), 7);
    EXPECT_EQ(NextDogRound(5, 0.0f), 9);
    EXPECT_EQ(NextDogRound(5, 0.99f), 11);
    EXPECT_EQ(DogsForRound(1, 1), 6);
    EXPECT_EQ(DogsForRound(2, 1), 8);
    EXPECT_EQ(DogsForRound(10, 1), 12);
    EXPECT_EQ(DogsForRound(1, 2), 12);
}

static void TestEnemyMix() {
    EXPECT_TRUE(PickEnemy(1, 0.9f, false) == EnemyKind::Stalchild);
    EXPECT_TRUE(PickEnemy(1, 0.9f, true) == EnemyKind::Redead);
    EXPECT_TRUE(PickEnemy(4, 0.9f, false) == EnemyKind::Redead);
    EXPECT_TRUE(PickEnemy(7, 0.9f, false) == EnemyKind::Gibdo);
    EXPECT_TRUE(PickEnemy(12, 0.9f, false) == EnemyKind::BigStalchild);
    EXPECT_TRUE(PickEnemy(12, 0.9f, true) == EnemyKind::Gibdo);
}

static void TestMoveTiers() {
    // Round 1: 8 +/- 15 is always a walk. Round 4 (32) can roll into a run.
    EXPECT_TRUE(PickMoveTier(1, 0.0f) == MoveTier::Walk);
    EXPECT_TRUE(PickMoveTier(1, 0.99f) == MoveTier::Walk);
    EXPECT_TRUE(PickMoveTier(4, 0.0f) == MoveTier::Walk);
    EXPECT_TRUE(PickMoveTier(4, 0.99f) == MoveTier::Run);
    EXPECT_TRUE(PickMoveTier(8, 0.0f) == MoveTier::Run);
    EXPECT_TRUE(PickMoveTier(2, 0.0f) == MoveTier::Walk);
    EXPECT_TRUE(PickMoveTier(2, 0.99f) == MoveTier::Run);
    EXPECT_TRUE(PickMoveTier(8, 0.99f) == MoveTier::Sprint);
    EXPECT_TRUE(PickMoveTier(11, 0.0f) == MoveTier::Sprint);
    EXPECT_TRUE(PullSpeed(MoveTier::Sprint) > PullSpeed(MoveTier::Run));
    EXPECT_TRUE(PullSpeed(MoveTier::Run) > PullSpeed(MoveTier::Walk));
}

static void TestPoints() {
    EXPECT_EQ(PointsForKill(KillType::Melee), 130);
    EXPECT_EQ(PointsForKill(KillType::Ranged), 100);
    EXPECT_EQ(PointsForKill(KillType::Other), 60);
}

static void TestDrops() {
    DropTracker tracker;
    // Below the threshold, only the 2% roll drops.
    EXPECT_TRUE(!tracker.OnKill(500, 0.5f));
    EXPECT_TRUE(tracker.OnKill(500, 0.01f));
    // Crossing 2000 points drops, and the next threshold moves 14% further out.
    EXPECT_TRUE(tracker.OnKill(2010, 0.5f));
    EXPECT_EQ(tracker.threshold, 2010 + 2280);
    EXPECT_TRUE(!tracker.OnKill(3000, 0.5f));
    // Four drops per round at most.
    EXPECT_TRUE(tracker.OnKill(3000, 0.0f));
    EXPECT_TRUE(tracker.OnKill(3000, 0.0f));
    EXPECT_TRUE(!tracker.OnKill(3000, 0.0f));
    tracker.NewRound();
    EXPECT_TRUE(tracker.OnKill(3000, 0.0f));
}

static void TestPowerUpPick() {
    EXPECT_TRUE(PickPowerUp(0.0f, true, true) == PowerUp::MaxAmmo);
    EXPECT_TRUE(PickPowerUp(0.999f, true, true) == PowerUp::FireSale);
    EXPECT_TRUE(PickPowerUp(0.999f, false, true) == PowerUp::Carpenter);
    EXPECT_TRUE(PickPowerUp(0.999f, false, false) == PowerUp::Nuke);
    EXPECT_TRUE(PickPowerUp(0.999f, true, false) == PowerUp::FireSale);
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(PickPowerUp(i / 100.0f, false, false) != PowerUp::Carpenter);
        EXPECT_TRUE(PickPowerUp(i / 100.0f, false, false) != PowerUp::FireSale);
    }
}

static void TestRepairPoints() {
    EXPECT_EQ(RepairPoints(0), 10);
    EXPECT_EQ(RepairPoints(490), 10);
    EXPECT_EQ(RepairPoints(495), 5);
    EXPECT_EQ(RepairPoints(500), 0);
    EXPECT_EQ(RepairPoints(800), 0);
}

int main() {
    TestRoundSizes();
    TestSpawnDelay();
    TestHealth();
    TestDogRounds();
    TestEnemyMix();
    TestMoveTiers();
    TestPoints();
    TestDrops();
    TestPowerUpPick();
    TestRepairPoints();
    if (sFailures == 0) {
        std::printf("All rules tests passed\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d failure(s)\n", sFailures);
    return EXIT_FAILURE;
}
