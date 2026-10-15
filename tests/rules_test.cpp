// Unit tests for the pure Zombies of Time formulas. Build and run with:
//   ./scripts/test.sh

#include "../mod/soh/soh/Enhancements/Zombies/ZombiesRules.h"
#include "../mod/soh/soh/Enhancements/Zombies/ZombiesGuns.h"

#include <cmath>

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
    EXPECT_EQ(NextDogRound(5, 0.99f), 10);
    EXPECT_EQ(DogsForRound(1, 1), 6);
    EXPECT_EQ(DogsForRound(2, 1), 6);
    EXPECT_EQ(DogsForRound(3, 1), 8);
    EXPECT_EQ(DogsForRound(10, 1), 8);
    EXPECT_EQ(DogsForRound(1, 2), 12);
    EXPECT_EQ(DogsForRound(3, 2), 16);
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
    // Below the threshold, only the 3% roll drops.
    EXPECT_TRUE(!tracker.OnKill(500, 0.5f));
    EXPECT_TRUE(!tracker.OnKill(500, 0.035f));
    EXPECT_TRUE(tracker.OnKill(500, 0.025f));
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

static float sRolls[] = { 0.1f, 0.7f, 0.3f, 0.9f, 0.5f, 0.2f, 0.8f, 0.4f, 0.6f, 0.0f };
static int sRollIndex = 0;
static float NextRoll() {
    float r = sRolls[sRollIndex % 10];
    sRollIndex++;
    return r;
}

static void TestPowerUpDeck() {
    // Every full deck holds each power-up once.
    PowerUpDeck deck;
    for (int round = 0; round < 3; round++) {
        int seen[static_cast<int>(PowerUp::Count)] = {};
        for (int i = 0; i < static_cast<int>(PowerUp::Count); i++) {
            seen[static_cast<int>(deck.Draw(NextRoll, true, true))]++;
        }
        for (int count : seen) {
            EXPECT_EQ(count, 1);
        }
    }
    // Fire Sale and Carpenter never come out when they can't drop.
    PowerUpDeck limited;
    for (int i = 0; i < 60; i++) {
        PowerUp p = limited.Draw(NextRoll, false, false);
        EXPECT_TRUE(p != PowerUp::FireSale && p != PowerUp::Carpenter);
    }
}

static void TestChestMoves() {
    for (int uses = 1; uses <= 4; uses++) {
        EXPECT_TRUE(!ChestMoves(uses, false, 0.0f));
    }
    EXPECT_TRUE(ChestMoves(5, true, 0.1f));
    EXPECT_TRUE(!ChestMoves(8, true, 0.2f));
    EXPECT_TRUE(ChestMoves(9, false, 0.99f)); // a chest that never moved always does now
    EXPECT_TRUE(ChestMoves(9, true, 0.25f));
    EXPECT_TRUE(!ChestMoves(13, true, 0.35f));
    EXPECT_TRUE(ChestMoves(14, true, 0.45f));
    EXPECT_TRUE(!ChestMoves(14, true, 0.55f));
}

static void TestRepairPoints() {
    EXPECT_EQ(RepairPoints(0), 10);
    EXPECT_EQ(RepairPoints(490), 10);
    EXPECT_EQ(RepairPoints(495), 5);
    EXPECT_EQ(RepairPoints(500), 0);
    EXPECT_EQ(RepairPoints(800), 0);
}

static void TestBoHealth() {
    // Three hearts: two hits down, five with Juggernog.
    EXPECT_EQ(HealthAfterHit(48, 48, false), 24);
    EXPECT_EQ(HealthAfterHit(24, 48, false), 0);
    int h = 80;
    int hits = 0;
    while (h > 0) {
        h = HealthAfterHit(h, 80, true);
        hits++;
    }
    EXPECT_EQ(hits, 5);
    h = 48;
    hits = 0;
    while (h > 0) {
        h = HealthAfterHit(h, 48, true);
        hits++;
    }
    EXPECT_EQ(hits, 5);
    // Twenty hearts is still two hits.
    EXPECT_EQ(HealthAfterHit(HealthAfterHit(320, 320, false), 320, false), 0);
    // Part-regenerated: still goes down on the second hit.
    EXPECT_EQ(HealthAfterHit(30, 48, false), 6);
    EXPECT_EQ(DogHealth(1, 8), 8);
    EXPECT_EQ(DogHealth(3, 8), 16);
    EXPECT_EQ(DogHealth(500, 8), 255);
    EXPECT_TRUE(PullSpeed(MoveTier::Crawl) < PullSpeed(MoveTier::Walk));
}

static void TestBoZombieHealth() {
    EXPECT_EQ(BoZombieHealth(1), 150);
    EXPECT_EQ(BoZombieHealth(2), 250);
    EXPECT_EQ(BoZombieHealth(9), 950);
    EXPECT_EQ(BoZombieHealth(10), 1045); // x1.1 from round 10
    EXPECT_EQ(BoZombieHealth(11), 1149);
    EXPECT_TRUE(BoZombieHealth(200) > 0); // never overflows
}

// Shots for a gun to drop an enemy with maxHp, as the game counts them (whole HP with
// the remainder carried to the next hit).
static int ShotsToKill(float boDamage, int maxHp, int round) {
    float perShot = BoDamageToHp(boDamage, maxHp, round, 100);
    float dealt = 0.0f;
    int shots = 0;
    while (dealt + 1e-4f < static_cast<float>(maxHp) && shots < 100000) {
        dealt += perShot;
        shots++;
    }
    return shots;
}

static void TestGunDamageMatchesBlackOps() {
    // Shots-to-kill match Black Ops whatever the enemy's own HP is.
    for (int round : { 1, 5, 10, 20, 30 }) {
        for (int maxHp : { 2, 8, 37, 255 }) {
            for (float damage : { 20.0f, 105.0f, 1000.0f }) {
                int bo = static_cast<int>(std::ceil(BoZombieHealth(round) / damage - 1e-4));
                EXPECT_EQ(ShotsToKill(damage, maxHp, round), bo);
            }
        }
    }
    // M1911 on round 1: eight body shots, four to the head.
    EXPECT_EQ(ShotsToKill(20.0f, 8, 1), 8);
    EXPECT_EQ(ShotsToKill(20.0f * GunInfo(GunId::M1911).headshot, 8, 1), 4);
    // Health Scaling at 0% keeps every round at round 1's toughness.
    EXPECT_TRUE(std::fabs(BoDamageToHp(105.0f, 8, 25, 0) - BoDamageToHp(105.0f, 8, 1, 100)) < 1e-4f);
}

static void TestItemScaling() {
    // Never below the item's own damage.
    for (int w = 0; w < static_cast<int>(OotWeapon::Count); w++) {
        for (int round : { 1, 10, 40 }) {
            EXPECT_TRUE(ScaledItemDamage(2, static_cast<OotWeapon>(w), 30, round, 100) >= 2);
        }
    }
    // The sword (knife) keeps OoT damage.
    EXPECT_EQ(ScaledItemDamage(2, OotWeapon::None, 30, 5, 100), 2);
    // Din's Fire is the Thundergun: one cast kills at any round and any health.
    EXPECT_TRUE(ScaledItemDamage(4, OotWeapon::DinsFire, 255, 60, 100) >= 255);
    // An arrow on round 1 kills a Redead (8 HP) in one, as an M14 headshot would.
    EXPECT_TRUE(ScaledItemDamage(2, OotWeapon::Bow, 8, 1, 100) >= 8);
    // Each item outdoes a wall gun of its tier per hit at every round, since it fires
    // slower or costs magic. Round 15 Redead (8 HP base).
    int hp = ScaleHealth(8, 15, 100);
    auto gunHit = [&](GunId id) { return BoDamageToHp(GunInfo(id).base.damage, hp, 15, 100); };
    EXPECT_TRUE(ScaledItemDamage(1, OotWeapon::Slingshot, hp, 15, 100) >= gunHit(GunId::M1911));
    EXPECT_TRUE(ScaledItemDamage(2, OotWeapon::Bow, hp, 15, 100) >= gunHit(GunId::M14));
    EXPECT_TRUE(ScaledItemDamage(2, OotWeapon::LightArrow, hp, 15, 100) >= gunHit(GunId::RayGun));
    EXPECT_TRUE(ScaledItemDamage(4, OotWeapon::Hammer, hp, 15, 100) >= gunHit(GunId::Olympia) * 8);
    // Late, Black Ops health outgrows ours and the item's own damage takes over.
    EXPECT_EQ(ScaledItemDamage(2, OotWeapon::Bow, ScaleHealth(8, 60, 100), 60, 100), 2);
}

static void TestGunTable() {
    for (int i = 1; i < static_cast<int>(GunId::Count); i++) {
        GunId id = static_cast<GunId>(i);
        const GunStats& g = GunInfo(id);
        EXPECT_TRUE(g.id == id);
        EXPECT_TRUE(g.base.clip > 0 && g.base.reserve > 0 && g.fireFrames >= 1.0f && g.reloadFrames > 0);
        // Pack-a-Punch never makes a gun worse.
        EXPECT_TRUE(g.pap.damage + g.pap.splash >= g.base.damage + g.base.splash);
        EXPECT_TRUE(g.pap.clip >= g.base.clip);
        EXPECT_TRUE(g.pap.name[0] != '\0' && g.boName[0] != '\0');
        // Every gun comes from somewhere: start pistol, a wall or the chest.
        EXPECT_TRUE(id == GunId::M1911 || g.wallCost > 0 || g.boxWeight > 0);
    }
    // Ray Gun: box only, rarest, projectile with splash, 20 + 160; Porter's X2 is 40 + 200.
    const GunStats& ray = GunInfo(GunId::RayGun);
    EXPECT_EQ(ray.wallCost, 0);
    EXPECT_TRUE(ray.projectile && ray.base.splash > 0);
    EXPECT_EQ(ray.base.clip, 20);
    EXPECT_EQ(ray.base.reserve, 160);
    EXPECT_EQ(ray.pap.clip, 40);
    for (int i = 1; i < static_cast<int>(GunId::Count); i++) {
        if (GunInfo(static_cast<GunId>(i)).boxWeight > 0) {
            EXPECT_TRUE(ray.boxWeight <= GunInfo(static_cast<GunId>(i)).boxWeight);
        }
    }
    // Black Ops wall prices, and ammo at half price (4500 once punched).
    EXPECT_EQ(GunInfo(GunId::M14).wallCost, 500);
    EXPECT_EQ(GunInfo(GunId::Olympia).wallCost, 500);
    EXPECT_EQ(GunInfo(GunId::MP5K).wallCost, 1000);
    EXPECT_EQ(GunInfo(GunId::AK74u).wallCost, 1200);
    EXPECT_EQ(GunInfo(GunId::Stakeout).wallCost, 1500);
    EXPECT_EQ(GunAmmoCost(GunId::M14, false), 250);
    EXPECT_EQ(GunAmmoCost(GunId::M14, true), kPapAmmoCost);
    // Nacht der Untoten's wall guns at Black Ops' prices, sold from their own wall spots only.
    EXPECT_EQ(GunInfo(GunId::Kar98k).wallCost, 200);
    EXPECT_EQ(GunInfo(GunId::M1Carbine).wallCost, 600);
    EXPECT_EQ(GunInfo(GunId::Thompson).wallCost, 1200);
    EXPECT_EQ(GunInfo(GunId::DoubleBarrel).wallCost, 1200);
    EXPECT_EQ(GunInfo(GunId::SawedOff).wallCost, 1200);
    EXPECT_EQ(GunInfo(GunId::TrenchGun).wallCost, 1500);
    EXPECT_EQ(GunInfo(GunId::BAR).wallCost, 1800);
    for (GunId id : { GunId::Kar98k, GunId::M1Carbine, GunId::Thompson, GunId::DoubleBarrel, GunId::SawedOff,
                      GunId::TrenchGun, GunId::BAR }) {
        EXPECT_EQ(GunInfo(id).wallZone, -1);
        EXPECT_EQ(GunInfo(id).boxWeight, 0);
    }
    // A map's wall buy names its gun with or without the zombies weapons' _zm suffix.
    EXPECT_TRUE(GunByBoName("zombie_kar98k") == GunId::Kar98k);
    EXPECT_TRUE(GunByBoName("zombie_doublebarrel") == GunId::DoubleBarrel);
    EXPECT_TRUE(GunByBoName("zombie_doublebarrel_sawed") == GunId::SawedOff);
    EXPECT_TRUE(GunByBoName("zombie_shotgun") == GunId::TrenchGun);
    EXPECT_TRUE(GunByBoName("m14") == GunId::M14);
    EXPECT_TRUE(GunByBoName("stielhandgranate") == GunId::None);
    EXPECT_TRUE(GunByBoName(nullptr) == GunId::None);
    // Kar98k: one head shot on round 1, as the bolt-action does.
    EXPECT_TRUE(GunInfo(GunId::Kar98k).base.damage * GunInfo(GunId::Kar98k).headshot >= BoZombieHealth(1));
}

// High rounds: nothing overflows or stops growing sensibly however long a run goes.
static void TestHighRounds() {
    int last = 0;
    for (int round = 1; round <= 255; round++) {
        int count = ZombiesForRound(round, 1);
        EXPECT_TRUE(count >= 1 && count < 100000);
        EXPECT_TRUE(round <= 5 || count >= last);
        last = count;
        EXPECT_TRUE(ZombiesForRound(round, kMaxPlayers) >= count);
        EXPECT_TRUE(SpawnDelaySeconds(round) >= 0.25f && SpawnDelaySeconds(round) <= 2.0f);
        uint8_t hp = ScaleHealth(255, round, 200);
        EXPECT_TRUE(hp >= 1);
        EXPECT_TRUE(BoZombieHealth(round) >= 150);
        EXPECT_TRUE(round == 1 || BoZombieHealth(round) >= BoZombieHealth(round - 1));
        // A gun hit stays a finite, positive share of health at any round.
        float share = BoDamageToHp(20.0f, 255, round, 100);
        EXPECT_TRUE(share > 0.0f && share <= 255.0f * 20.0f / 150.0f);
        EXPECT_TRUE(round < 9 || PickMoveTier(round, 0.0f) == MoveTier::Sprint);
    }
    // Black Ops: +100 a round to round 9, then x1.1 (rounded down) from round 10.
    EXPECT_EQ(BoZombieHealth(30), [] {
        int h = 950;
        for (int r = 10; r <= 30; r++) {
            h += h / 10;
        }
        return h;
    }());
    // Round 30's horde, solo: 24 + 3 * (6 * 4.5) = 105.
    EXPECT_EQ(ZombiesForRound(30, 1), 105);
    // The drop threshold grows 14% a drop: a run's whole points never push it past int.
    DropTracker tracker;
    int earned = 0;
    for (int kill = 0; kill < 200000 && earned < 2000000000 - 1000; kill++) {
        earned += 1000;
        tracker.OnKill(earned, 0.5f);
        tracker.NewRound();
        EXPECT_TRUE(tracker.threshold > 0 && tracker.increment > 0);
    }
}

static void TestGunSlots() {
    PlayerGuns guns;
    EXPECT_EQ(guns.Give(GunId::M1911), 0);
    EXPECT_EQ(guns.Current().clip, 8);
    EXPECT_EQ(guns.Current().reserve, 80);
    // A second gun fills the empty slot and is drawn.
    EXPECT_EQ(guns.Give(GunId::M14), 1);
    EXPECT_EQ(guns.current, 1);
    EXPECT_EQ(guns.Count(), 2);
    // A third replaces the one in hand.
    EXPECT_EQ(guns.Give(GunId::MP40), 1);
    EXPECT_TRUE(guns.Has(GunId::M1911) && guns.Has(GunId::MP40) && !guns.Has(GunId::M14));
    // Reload moves only what the clip has room for.
    guns.Current().clip = 5;
    EXPECT_TRUE(guns.CanReload());
    EXPECT_EQ(guns.Reload(), 27);
    EXPECT_EQ(guns.Current().clip, 32);
    EXPECT_EQ(guns.Current().reserve, 192 - 27);
    EXPECT_TRUE(!guns.CanReload());
    // Running the reserve low: the last rounds go in.
    guns.Current().clip = 0;
    guns.Current().reserve = 10;
    EXPECT_EQ(guns.Reload(), 10);
    EXPECT_EQ(guns.Current().reserve, 0);
    EXPECT_TRUE(!guns.CanReload());
    // Max Ammo fills reserves and leaves clips alone.
    guns.MaxAmmo();
    EXPECT_EQ(guns.Current().reserve, 192);
    EXPECT_EQ(guns.Current().clip, 10);
    // Swapping goes to the other gun.
    EXPECT_EQ(guns.NextSlot(), 0);
    // Pack-a-Punch: new name, bigger clip, full.
    guns.PackAPunch(1);
    EXPECT_EQ(guns.slots[1].clip, 64);
    EXPECT_TRUE(std::string(GunName(GunId::MP40, true)) == "THE AFTERBURNER");
    // The chest never offers a gun you hold.
    int total = GunBoxWeight(guns);
    for (int roll = 0; roll < total; roll++) {
        GunId id = GunBoxPick(guns, roll);
        EXPECT_TRUE(id != GunId::None && !guns.Has(id));
    }
    EXPECT_TRUE(GunBoxPick(guns, total) == GunId::None);
}

int main() {
    TestBoZombieHealth();
    TestGunDamageMatchesBlackOps();
    TestItemScaling();
    TestGunTable();
    TestGunSlots();
    TestHighRounds();
    TestBoHealth();
    TestRoundSizes();
    TestSpawnDelay();
    TestHealth();
    TestDogRounds();
    TestEnemyMix();
    TestMoveTiers();
    TestPoints();
    TestDrops();
    TestPowerUpDeck();
    TestChestMoves();
    TestRepairPoints();
    if (sFailures == 0) {
        std::printf("All rules tests passed\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d failure(s)\n", sFailures);
    return EXIT_FAILURE;
}
