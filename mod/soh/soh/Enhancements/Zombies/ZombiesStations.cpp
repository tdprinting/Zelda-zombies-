// Zombies of Time: buy stations.
//
// Each station is a torch placed on open floor in a ring around where the run
// started. Walk up to one and press the buy button. Perk and Pack-a-Punch
// torches stay dark until the power is on.

#include "ZombiesMode.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>
#include <cmath>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace Zombies {

static constexpr float kStationRadius = 320.0f;
static constexpr float kInteractDist = 95.0f;
static constexpr int kChestCost = 950;
static constexpr int kFireSaleCost = 10;
static constexpr int kPackAPunchCost = 5000;
static constexpr int kFountainRupees = 50;

// Torch params: high nibble is the torch type, 0x400 keeps it lit, low bits are a
// switch flag. 0x3E is a temp flag (cleared on every scene load), so lighting one never
// trips anything in the arena; Redead kills use 0x3F, so they never light the stone torches.
static constexpr s16 kTorchFlag = 0x3E;
static constexpr s16 kTorchGolden = 0x0000 | kTorchFlag;
static constexpr s16 kTorchStone = 0x1000 | kTorchFlag;
static constexpr s16 kTorchWooden = 0x2000 | kTorchFlag; // a Deku Stick lights it and sets the flag
static constexpr s16 kTorchLit = 0x0400;

struct Station {
    StationType type;
    Vec3f pos;
    Actor* torch = nullptr;
    bool placed = false;
    int zone = -1; // arena zone, or -1 when placed around the start point
};

static Station sStations[static_cast<int>(StationType::Count)];
static int sChestUses = 0; // prizes since the chest last moved
static bool sChestMoved = false;
static bool sPapSword = false;
static int sFairyBuys = 0; // solo Fairy Revive sells out after three, as Quick Revive does
static constexpr int kSoloFairyBuys = 3;

static bool Solo() {
    return GetRun().playerCount < 2;
}

static bool NeedsPower(StationType type) {
    switch (type) {
        case StationType::Juggernog:
        case StationType::DoubleTap:
        case StationType::StaminUp:
        case StationType::PackAPunch:
            return true;
        case StationType::FairyRevive:
            return !Solo(); // Quick Revive sells without power in solo

        default:
            return false;
    }
}

static s16 TorchParams(StationType type) {
    switch (type) {
        case StationType::MysteryChest:
            return kTorchGolden | kTorchLit;
        case StationType::Armory:
        case StationType::FairyFountain:
            return kTorchWooden | kTorchLit;
        case StationType::Power:
            return GetRun().powerOn ? (kTorchWooden | kTorchLit) : kTorchWooden;
        default:
            return (GetRun().powerOn || !NeedsPower(type)) ? (kTorchStone | kTorchLit) : kTorchStone;
    }
}

static bool FloorAt(PlayState* play, float x, float z, float refY, Vec3f* out) {
    Vec3f probe = { x, refY + 150.0f, z };
    CollisionPoly* poly;
    s32 bgId;
    f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if (floorY <= BGCHECK_Y_MIN + 1.0f || fabsf(floorY - refY) > 80.0f) {
        return false;
    }
    *out = { x, floorY, z };
    return true;
}

static bool TooCloseToOtherStations(const Vec3f& pos, int self) {
    for (int i = 0; i < static_cast<int>(StationType::Count); i++) {
        if (i != self && sStations[i].placed &&
            Math_Vec3f_DistXZ(&sStations[i].pos, const_cast<Vec3f*>(&pos)) < 140.0f) {
            return true;
        }
    }
    return false;
}

static void SpawnTorch(PlayState* play, int index, const Vec3f& pos) {
    Station& station = sStations[index];
    station.pos = pos;
    station.placed = true;
    if (!GetRun().powerOn) {
        Flags_UnsetSwitch(play, kTorchFlag); // a stone torch lit by hand stays dark
    }
    station.torch =
        Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_SYOKUDAI, pos.x, pos.y, pos.z, 0, 0, 0, TorchParams(station.type));
    if (station.torch != nullptr) {
        station.torch->room = -1;
    }
}

static bool PlaceStation(PlayState* play, int index, s16 baseAngle) {
    const Run& run = GetRun();
    static constexpr float radii[] = { 1.0f, 0.75f, 1.3f, 0.5f, 1.6f };
    static constexpr s16 angleOffsets[] = { 0, 0x600, -0x600, 0xC00, -0xC00 };

    for (float radiusScale : radii) {
        for (s16 offset : angleOffsets) {
            s16 angle = baseAngle + offset;
            float radius = kStationRadius * radiusScale;
            Vec3f pos;
            if (!FloorAt(play, run.origin.x + Math_SinS(angle) * radius, run.origin.z + Math_CosS(angle) * radius,
                         run.origin.y, &pos)) {
                continue;
            }
            if (TooCloseToOtherStations(pos, index)) {
                continue;
            }
            SpawnTorch(play, index, pos);
            return true;
        }
    }
    return false;
}

// The authored spot for a station in the arena layout, if it has one.
static const LayoutSpot* LayoutSpotFor(int index, int* zone) {
    const ArenaLayout* layout = GetRun().layout;
    if (layout == nullptr) {
        return nullptr;
    }
    for (size_t z = 0; z < layout->zones.size(); z++) {
        for (const LayoutStation& st : layout->zones[z].stations) {
            if (static_cast<int>(st.type) == index) {
                *zone = static_cast<int>(z);
                return &st.spot;
            }
        }
    }
    return nullptr;
}

static bool PlaceAtSpot(PlayState* play, int index, const LayoutSpot& spot) {
    Vec3f pos;
    if (!Arena_Resolve(play, spot, GetRun().origin.y, &pos)) {
        return false;
    }
    SpawnTorch(play, index, pos);
    return true;
}

void Stations_Clear() {
    for (Station& station : sStations) {
        if (station.torch != nullptr) {
            Actor_Kill(station.torch);
        }
        station = Station();
    }
}

// The scene is gone, and the torches with it: drop them without touching the actors.
void Stations_Forget() {
    for (Station& station : sStations) {
        station = Station();
    }
}

void Stations_OnActorDestroy(Actor* actor) {
    for (Station& station : sStations) {
        if (station.torch == actor) {
            station.torch = nullptr;
            station.placed = false;
        }
    }
}

void Stations_Place(PlayState* play) {
    Stations_Clear();
    const Run& run = GetRun();
    constexpr int count = static_cast<int>(StationType::Count);

    // The Mystery Chest gets the spot straight ahead of Link; the rest fan out around him.
    for (int i = 0; i < count; i++) {
        sStations[i].type = static_cast<StationType>(i);
    }
    static constexpr StationType order[] = {
        StationType::MysteryChest, StationType::Armory,    StationType::Juggernog,
        StationType::Power,        StationType::DoubleTap, StationType::FairyRevive,
        StationType::PackAPunch,   StationType::StaminUp,  StationType::FairyFountain,
    };
    for (int slot = 0; slot < count; slot++) {
        int index = static_cast<int>(order[slot]);
        // In an authored arena, stations go on their spots, and wait for their zone to open.
        int zone;
        const LayoutSpot* spot = LayoutSpotFor(index, &zone);
        if (spot != nullptr) {
            sStations[index].zone = zone;
            if (!Arena_ZoneOpen(zone) || PlaceAtSpot(play, index, *spot)) {
                continue;
            }
        }
        s16 angle = run.originYaw + static_cast<s16>(slot * (0x10000 / count));
        PlaceStation(play, index, angle);
    }
}

// A new run: the chest's use count and the Punched Blade start over. A void-out
// re-places the stations but keeps both.
void Stations_NewRun() {
    sChestUses = 0;
    sChestMoved = false;
    sPapSword = false;
    sFairyBuys = 0;
}

static void MoveChest(PlayState* play) {
    sChestMoved = true;
    int index = static_cast<int>(StationType::MysteryChest);
    Station& chest = sStations[index];
    if (chest.torch != nullptr) {
        Actor_Kill(chest.torch);
    }
    chest.torch = nullptr;
    chest.placed = false;

    // In an authored arena the chest moves between its spots in the open zones.
    const ArenaLayout* layout = GetRun().layout;
    if (layout != nullptr) {
        Vec3f oldPos = chest.pos;
        std::vector<Vec3f> options;
        for (size_t z = 0; z < layout->zones.size(); z++) {
            if (!Arena_ZoneOpen(static_cast<int>(z))) {
                continue;
            }
            for (const LayoutSpot& spot : layout->zones[z].chestSpots) {
                Vec3f pos;
                if (Arena_Resolve(play, spot, GetRun().origin.y, &pos) && Math_Vec3f_DistXZ(&pos, &oldPos) > 100.0f) {
                    options.push_back(pos);
                }
            }
        }
        if (!options.empty()) {
            size_t pick =
                std::min(static_cast<size_t>(Rand_ZeroOne() * static_cast<float>(options.size())), options.size() - 1);
            SpawnTorch(play, index, options[pick]);
            return;
        }
    }
    PlaceStation(play, index, static_cast<s16>(Rand_ZeroOne() * 0x10000));
}

void Stations_OpenZone(PlayState* play, int zone) {
    for (int i = 0; i < static_cast<int>(StationType::Count); i++) {
        int stationZone;
        const LayoutSpot* spot = LayoutSpotFor(i, &stationZone);
        if (spot != nullptr && stationZone == zone && !sStations[i].placed) {
            if (!PlaceAtSpot(play, i, *spot)) {
                PlaceStation(play, i, static_cast<s16>(Rand_ZeroOne() * 0x10000));
            }
        }
    }
}

static void RelightTorches() {
    for (Station& station : sStations) {
        if (station.torch != nullptr) {
            station.torch->params = TorchParams(station.type);
        }
    }
}

int Stations_BoxCost() {
    return PowerUpActive(PowerUp::FireSale) ? kFireSaleCost : kChestCost;
}

bool Stations_PapSword() {
    return sPapSword;
}

// ---------------------------------------------------------------------------
// Giving items

static bool Has(u8 item) {
    return INV_CONTENT(item) == item;
}

static void AutoEquip(PlayState* play, u8 item) {
    for (int b = 1; b <= 3; b++) {
        if (gSaveContext.equips.buttonItems[b] == item) {
            return;
        }
    }
    for (int b = 1; b <= 3; b++) {
        if (gSaveContext.equips.buttonItems[b] == ITEM_NONE) {
            gSaveContext.equips.buttonItems[b] = item;
            gSaveContext.equips.cButtonSlots[b - 1] = SLOT(item);
            Interface_LoadItemIcon1(play, static_cast<u16>(b));
            return;
        }
    }
}

static void GiveMagic() {
    if (!gSaveContext.isMagicAcquired) {
        gSaveContext.isMagicAcquired = true;
        gSaveContext.magicFillTarget = MAGIC_NORMAL_METER;
    }
}

static void GiveWeapon(PlayState* play, u8 item) {
    // Giving the slingshot or bow again resets an upgraded seed bag or quiver: refill instead.
    if ((item == ITEM_SLINGSHOT || item == ITEM_BOW) && Has(item)) {
        RefillAmmo();
        AutoEquip(play, item);
        return;
    }
    switch (item) {
        case ITEM_BOMB:
            Item_Give(play, CUR_UPG_VALUE(UPG_BOMB_BAG) == 0 ? ITEM_BOMB_BAG_20 : ITEM_BOMBS_20);
            break;
        case ITEM_BOMBCHU:
            Item_Give(play, ITEM_BOMBCHUS_20);
            break;
        case ITEM_STICK:
            Item_Give(play, ITEM_STICKS_10);
            break;
        case ITEM_DINS_FIRE:
        case ITEM_NAYRUS_LOVE:
            GiveMagic();
            Item_Give(play, item);
            break;
        case ITEM_ARROW_FIRE:
        case ITEM_ARROW_ICE:
        case ITEM_ARROW_LIGHT:
            if (!Has(ITEM_BOW)) {
                Item_Give(play, ITEM_BOW);
            }
            GiveMagic();
            Item_Give(play, item);
            break;
        case ITEM_SWORD_BGS:
            Item_Give(play, ITEM_SWORD_BGS);
            gSaveContext.bgsFlag = 1;
            gSaveContext.equips.buttonItems[0] = ITEM_SWORD_BGS;
            Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_BIGGORON);
            Interface_LoadItemIcon1(play, 0);
            Player_SetEquipmentData(play, GET_PLAYER(play));
            return;
        default:
            Item_Give(play, item);
            break;
    }
    RefillAmmo();
    AutoEquip(play, item);
}

// Zombies-only starting kit, Black Ops style: a sword for the knife, a slingshot or bow
// for the pistol, four bombs for the grenades, three hearts, and an ocarina with the
// three songs the mode uses. The run's save snapshot puts the real inventory back after.
void GiveLoadout(PlayState* play) {
    bool adult = LINK_IS_ADULT;
    for (int i = 0; i < static_cast<int>(ARRAY_COUNT(gSaveContext.inventory.items)); i++) {
        gSaveContext.inventory.items[i] = ITEM_NONE;
    }
    for (int i = 0; i < static_cast<int>(ARRAY_COUNT(gSaveContext.inventory.ammo)); i++) {
        gSaveContext.inventory.ammo[i] = 0;
    }
    // C buttons and SoH's D-pad item buttons.
    for (int b = 1; b < static_cast<int>(ARRAY_COUNT(gSaveContext.equips.buttonItems)); b++) {
        gSaveContext.equips.buttonItems[b] = ITEM_NONE;
        gSaveContext.equips.cButtonSlots[b - 1] = SLOT_NONE;
    }
    gSaveContext.inventory.equipment &= ~0x00FF; // swords and shields
    gSaveContext.inventory.upgrades = 0;
    gSaveContext.isDoubleDefenseAcquired = 0;
    gSaveContext.isMagicAcquired = 0;
    gSaveContext.isDoubleMagicAcquired = 0;
    gSaveContext.magicLevel = 0;
    gSaveContext.magic = 0;
    gSaveContext.magicCapacity = 0;
    gSaveContext.healthCapacity = 0x30;
    gSaveContext.health = 0x30;
    gSaveContext.rupees = 0;

    Item_Give(play, adult ? ITEM_SWORD_MASTER : ITEM_SWORD_KOKIRI);
    Item_Give(play, adult ? ITEM_SHIELD_HYLIAN : ITEM_SHIELD_DEKU);
    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, adult ? EQUIP_VALUE_SWORD_MASTER : EQUIP_VALUE_SWORD_KOKIRI);
    Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, adult ? EQUIP_VALUE_SHIELD_HYLIAN : EQUIP_VALUE_SHIELD_DEKU);
    gSaveContext.equips.buttonItems[0] = adult ? ITEM_SWORD_MASTER : ITEM_SWORD_KOKIRI;
    Interface_LoadItemIcon1(play, 0);

    GiveWeapon(play, adult ? ITEM_BOW : ITEM_SLINGSHOT);
    AutoEquip(play, adult ? ITEM_BOW : ITEM_SLINGSHOT);
    Item_Give(play, ITEM_BOMB_BAG_20);
    AMMO(ITEM_BOMB) = 4;
    AutoEquip(play, ITEM_BOMB);
    Item_Give(play, ITEM_OCARINA_FAIRY);
    AutoEquip(play, ITEM_OCARINA_FAIRY);
    // Only the mode's three songs: a warp song would carry Link out of the map.
    for (int song = QUEST_SONG_MINUET; song <= QUEST_SONG_STORMS; song++) {
        gSaveContext.inventory.questItems &= ~(1u << song);
    }
    gSaveContext.inventory.questItems |= (1 << QUEST_SONG_LULLABY) | (1 << QUEST_SONG_SUN) | (1 << QUEST_SONG_STORMS);
}

struct WeaponOffer {
    u8 item;
    const char* name;
    int cost;
};

// Wall buys, best-first per age. The Armory sells the first one Link lacks.
static const WeaponOffer kChildArmory[] = {
    { ITEM_SLINGSHOT, "FAIRY SLINGSHOT", 500 },
    { ITEM_BOOMERANG, "BOOMERANG", 750 },
    { ITEM_BOMB, "BOMBS", 1000 },
};
static const WeaponOffer kAdultArmory[] = {
    { ITEM_BOW, "FAIRY BOW", 1000 },
    { ITEM_BOMB, "BOMBS", 1000 },
    { ITEM_HOOKSHOT, "HOOKSHOT", 1200 },
    { ITEM_HAMMER, "MEGATON HAMMER", 1500 },
};
static constexpr int kAmmoCost = 500;

static const WeaponOffer* ArmoryOffer() {
    const WeaponOffer* list = LINK_IS_ADULT ? kAdultArmory : kChildArmory;
    int count = static_cast<int>(LINK_IS_ADULT ? std::size(kAdultArmory) : std::size(kChildArmory));
    for (int i = 0; i < count; i++) {
        if (!Has(list[i].item)) {
            return &list[i];
        }
    }
    return nullptr;
}

struct ChestPrize {
    u8 item;
    const char* name;
    int weight;
};

// Black Ops box analogues: Bombchus are the Monkey Bomb, Din's Fire the Thundergun,
// Light Arrows the Ray Gun.
static const ChestPrize kChildChest[] = {
    { ITEM_BOMBCHU, "BOMBCHUS", 4 },  { ITEM_SLINGSHOT, "FAIRY SLINGSHOT", 3 }, { ITEM_BOOMERANG, "BOOMERANG", 3 },
    { ITEM_BOMB, "BOMBS", 3 },        { ITEM_DINS_FIRE, "DIN'S FIRE", 2 },      { ITEM_NAYRUS_LOVE, "NAYRU'S LOVE", 1 },
    { ITEM_STICK, "DEKU STICKS", 2 },
};
static const ChestPrize kAdultChest[] = {
    { ITEM_BOMBCHU, "BOMBCHUS", 4 },         { ITEM_ARROW_FIRE, "FIRE ARROWS", 3 },
    { ITEM_ARROW_ICE, "ICE ARROWS", 3 },     { ITEM_HAMMER, "MEGATON HAMMER", 2 },
    { ITEM_LONGSHOT, "LONGSHOT", 2 },        { ITEM_DINS_FIRE, "DIN'S FIRE", 2 },
    { ITEM_NAYRUS_LOVE, "NAYRU'S LOVE", 1 }, { ITEM_ARROW_LIGHT, "LIGHT ARROWS", 1 },
    { ITEM_SWORD_BGS, "BIGGORON SWORD", 1 },
};

static const ChestPrize& RollChest() {
    const ChestPrize* list = LINK_IS_ADULT ? kAdultChest : kChildChest;
    int count = static_cast<int>(LINK_IS_ADULT ? std::size(kAdultChest) : std::size(kChildChest));
    int total = 0;
    for (int i = 0; i < count; i++) {
        total += list[i].weight;
    }
    int roll = std::min(static_cast<int>(Rand_ZeroOne() * static_cast<float>(total)), total - 1);
    for (int i = 0; i < count; i++) {
        if (roll < list[i].weight) {
            return list[i];
        }
        roll -= list[i].weight;
    }
    return list[0];
}

// ---------------------------------------------------------------------------
// Pack-a-Punch: upgrades the first upgradable thing on Link's C buttons, then his
// sword, then his shield.

struct PapUpgrade {
    const char* name = nullptr;
    void (*apply)(PlayState*) = nullptr;
};

static PapUpgrade PapForItem(u8 item) {
    switch (item) {
        case ITEM_BOW:
        case ITEM_ARROW_FIRE:
        case ITEM_ARROW_ICE:
        case ITEM_ARROW_LIGHT:
            if (CUR_UPG_VALUE(UPG_QUIVER) < 3) {
                return { "BIGGER QUIVER", [](PlayState* play) {
                            Item_Give(play, CUR_UPG_VALUE(UPG_QUIVER) == 1 ? ITEM_QUIVER_40 : ITEM_QUIVER_50);
                        } };
            }
            if (!Has(ITEM_ARROW_LIGHT)) {
                return { "LIGHT ARROWS", [](PlayState* play) { GiveWeapon(play, ITEM_ARROW_LIGHT); } };
            }
            break;
        case ITEM_SLINGSHOT:
            if (CUR_UPG_VALUE(UPG_BULLET_BAG) < 3) {
                return { "BIGGER SEED BAG", [](PlayState* play) {
                            Item_Give(play,
                                      CUR_UPG_VALUE(UPG_BULLET_BAG) == 1 ? ITEM_BULLET_BAG_40 : ITEM_BULLET_BAG_50);
                        } };
            }
            break;
        case ITEM_BOMB:
            if (CUR_UPG_VALUE(UPG_BOMB_BAG) < 3) {
                return { "BIGGER BOMB BAG", [](PlayState* play) {
                            Item_Give(play, CUR_UPG_VALUE(UPG_BOMB_BAG) == 1 ? ITEM_BOMB_BAG_30 : ITEM_BOMB_BAG_40);
                        } };
            }
            break;
        case ITEM_HOOKSHOT:
            return { "LONGSHOT", [](PlayState* play) { Item_Give(play, ITEM_LONGSHOT); } };
        default:
            break;
    }
    return {};
}

static PapUpgrade FindPapUpgrade() {
    for (int b = 1; b <= 3; b++) {
        PapUpgrade upgrade = PapForItem(gSaveContext.equips.buttonItems[b]);
        if (upgrade.apply != nullptr) {
            return upgrade;
        }
    }
    if (LINK_IS_ADULT && !CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_BIGGORON)) {
        return { "BIGGORON SWORD", [](PlayState* play) { GiveWeapon(play, ITEM_SWORD_BGS); } };
    }
    if (!sPapSword) {
        return { "PUNCHED BLADE", [](PlayState*) { sPapSword = true; } };
    }
    if (LINK_IS_ADULT && !CHECK_OWNED_EQUIP(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_MIRROR)) {
        return { "MIRROR SHIELD", [](PlayState* play) {
                    Item_Give(play, ITEM_SHIELD_MIRROR);
                    Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_MIRROR);
                    Player_SetEquipmentData(play, GET_PLAYER(play));
                } };
    }
    return {};
}

static bool PapNeedsLullaby() {
    return CHECK_QUEST_ITEM(QUEST_SONG_LULLABY) && !GetRun().papUnlocked;
}

// ---------------------------------------------------------------------------
// Perks

// The bottle slot Fairy Revive fills, or -1 if every bottle holds a quest item.
static int FairyBottleSlot() {
    static constexpr int bottleSlots[] = { SLOT_BOTTLE_1, SLOT_BOTTLE_2, SLOT_BOTTLE_3, SLOT_BOTTLE_4 };
    int target = -1;
    for (int slot : bottleSlots) {
        if (gSaveContext.inventory.items[slot] == ITEM_BOTTLE) {
            target = slot;
            break;
        }
    }
    if (target < 0) {
        for (int slot : bottleSlots) {
            if (gSaveContext.inventory.items[slot] == ITEM_NONE) {
                target = slot;
                break;
            }
        }
    }
    if (target < 0) {
        // Every bottle is full: one's contents make way, but never a quest item.
        for (int slot : bottleSlots) {
            u8 item = gSaveContext.inventory.items[slot];
            if (item != ITEM_LETTER_RUTO && item != ITEM_BIG_POE) {
                target = slot;
                break;
            }
        }
    }
    return target;
}

static void GiveFairyBottle() {
    int target = FairyBottleSlot();
    if (target < 0) {
        return;
    }
    gSaveContext.inventory.items[target] = ITEM_FAIRY;
    for (int b = 1; b <= 3; b++) {
        if (gSaveContext.equips.cButtonSlots[b - 1] == target) {
            gSaveContext.equips.buttonItems[b] = ITEM_FAIRY;
            Interface_LoadItemIcon1(gPlayState, static_cast<u16>(b));
        }
    }
}

bool Stations_ChestMoved() {
    return sChestMoved;
}

bool Stations_ChestPlaced() {
    return sStations[static_cast<int>(StationType::MysteryChest)].placed;
}

static bool HasPerk(int who, Perk perk) {
    if (perk == PERK_FAIRY_REVIVE && who == 0) {
        return Inventory_HasSpecificBottle(ITEM_FAIRY);
    }
    return GetRun().players[who].perks[perk];
}

struct PerkInfo {
    Perk perk;
    const char* name;
    int cost;
};

static PerkInfo PerkFor(StationType type) {
    switch (type) {
        case StationType::Juggernog:
            return { PERK_JUGGERNOG, "JUGGERNOG MILK", 2500 };
        case StationType::FairyRevive:
            // Solo it's a self-revive for 500; in co-op it revives others faster, for 1500.
            return { PERK_FAIRY_REVIVE, "FAIRY REVIVE", Solo() ? 500 : 1500 };
        case StationType::DoubleTap:
            return { PERK_DOUBLE_TAP, "DOUBLE TAP", 2000 };
        case StationType::StaminUp:
        default:
            return { PERK_STAMIN_UP, "STAMIN-UP", 2000 };
    }
}

static constexpr int kPartnerJuggernogHealth = 80; // five hearts

static void ApplyPerk(int who, Perk perk) {
    PlayerState& ps = GetRun().players[who];
    ps.perks[perk] = true;
    if (who == 1) {
        // Player 2's perks live in their own state: Juggernog adds two hearts, the rest are
        // read where they act.
        Partner& partner = GetRun().partner;
        if (perk == PERK_JUGGERNOG) {
            partner.healthCapacity = kPartnerJuggernogHealth;
            partner.health = partner.healthCapacity;
        }
        return;
    }
    switch (perk) {
        case PERK_JUGGERNOG:
            gSaveContext.isDoubleDefenseAcquired = true;
            gSaveContext.inventory.defenseHearts = 20;
            gSaveContext.health = gSaveContext.healthCapacity;
            break;
        case PERK_FAIRY_REVIVE:
            if (Solo()) {
                GiveFairyBottle(); // the fairy is the self-revive
                sFairyBuys++;
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Prompts and purchases

struct Offer {
    std::string label;
    int cost = 0;      // points
    int rupeeCost = 0; // Fairy Fountain charges rupees instead
    bool available = true;
    std::string unavailableReason;
};

// Player 2's wall buys and chest: swords and bombs, their own.
static constexpr int kPartnerMasterSwordCost = 1500;
static constexpr int kPartnerBombsCost = 1000;
static constexpr int kPartnerFountainCost = 500;
static constexpr int kPartnerBombsPerBuy = 10;

struct PartnerChestPrize {
    const char* name;
    int weight;
    void (*give)();
};

static const PartnerChestPrize kPartnerChest[] = {
    { "MASTER SWORD", 3, [] { Coop_GiveSword(PARTNER_SWORD_MASTER); } },
    { "BIGGORON SWORD", 1, [] { Coop_GiveSword(PARTNER_SWORD_BIGGORON); } },
    { "BOMBS", 4,
      [] {
          GetRun().partner.bombBag = true;
          Coop_GiveBombs(Coop_MaxBombs());
      } },
    { "FAIRY", 2, [] { Coop_Heal(); } },
};

static const PartnerChestPrize& RollPartnerChest() {
    int total = 0;
    for (const PartnerChestPrize& prize : kPartnerChest) {
        total += prize.weight;
    }
    int roll = std::min(static_cast<int>(Rand_ZeroOne() * static_cast<float>(total)), total - 1);
    for (const PartnerChestPrize& prize : kPartnerChest) {
        if (roll < prize.weight) {
            return prize;
        }
        roll -= prize.weight;
    }
    return kPartnerChest[0];
}

static Offer GetOffer(const Station& station, int who) {
    Offer offer;
    const Run& run = GetRun();
    const Partner& partner = run.partner;

    if (NeedsPower(station.type) && !run.powerOn) {
        offer.available = false;
        offer.unavailableReason = "NEEDS POWER";
    }

    switch (station.type) {
        case StationType::Armory: {
            if (who == 1) {
                if (partner.sword < PARTNER_SWORD_MASTER) {
                    offer.label = "MASTER SWORD";
                    offer.cost = kPartnerMasterSwordCost;
                } else if (!partner.bombBag) {
                    offer.label = "BOMBS";
                    offer.cost = kPartnerBombsCost;
                } else {
                    offer.label = "BOMBS";
                    offer.cost = kAmmoCost;
                    if (partner.bombs >= Coop_MaxBombs()) {
                        offer.available = false;
                        offer.unavailableReason = "FULL";
                    }
                }
                break;
            }
            const WeaponOffer* weapon = ArmoryOffer();
            if (weapon != nullptr) {
                offer.label = weapon->name;
                offer.cost = weapon->cost;
            } else {
                offer.label = "AMMO";
                offer.cost = kAmmoCost;
            }
            break;
        }
        case StationType::MysteryChest:
            offer.label = PowerUpActive(PowerUp::FireSale) ? "MYSTERY CHEST (FIRE SALE)" : "MYSTERY CHEST";
            offer.cost = Stations_BoxCost();
            break;
        case StationType::Juggernog:
        case StationType::FairyRevive:
        case StationType::DoubleTap:
        case StationType::StaminUp: {
            PerkInfo info = PerkFor(station.type);
            offer.label = info.name;
            offer.cost = info.cost;
            if (HasPerk(who, info.perk)) {
                offer.available = false;
                offer.unavailableReason = "OWNED";
            } else if (info.perk == PERK_FAIRY_REVIVE && Solo() && sFairyBuys >= kSoloFairyBuys) {
                offer.available = false;
                offer.unavailableReason = "SOLD OUT";
            } else if (info.perk == PERK_FAIRY_REVIVE && Solo() && FairyBottleSlot() < 0) {
                offer.available = false;
                offer.unavailableReason = "NO BOTTLE FREE";
            }
            break;
        }
        case StationType::Power:
            offer.label = "POWER SWITCH";
            if (run.powerOn) {
                offer.available = false;
                offer.unavailableReason = "ON";
            } else {
                offer.available = true;
            }
            break;
        case StationType::PackAPunch: {
            offer.label = "PACK-A-PUNCH";
            offer.cost = kPackAPunchCost;
            if (offer.available && PapNeedsLullaby()) {
                offer.available = false;
                offer.unavailableReason = "PLAY ZELDA'S LULLABY";
            } else if (offer.available && who == 1) {
                if (partner.papSword) {
                    offer.available = false;
                    offer.unavailableReason = "NOTHING TO UPGRADE";
                } else {
                    offer.label += ": PUNCHED BLADE";
                }
            } else if (offer.available) {
                PapUpgrade upgrade = FindPapUpgrade();
                if (upgrade.apply == nullptr) {
                    offer.available = false;
                    offer.unavailableReason = "NOTHING TO UPGRADE";
                } else {
                    offer.label += std::string(": ") + upgrade.name;
                }
            }
            break;
        }
        case StationType::FairyFountain:
            offer.label = "FAIRY FOUNTAIN";
            if (who == 1) {
                offer.cost = kPartnerFountainCost; // player 2 has no rupees
            } else {
                offer.rupeeCost = kFountainRupees;
            }
            break;
        default:
            break;
    }
    return offer;
}

static void Fail() {
    Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
}

// Player 2's side of the stations that give them something of their own. Returns false
// for the ones that work the same for both (power, perks, the chest moving).
static bool BuyForPartner(PlayState* play, Station& station, const Offer& offer) {
    Partner& partner = GetRun().partner;
    switch (station.type) {
        case StationType::Armory:
            if (partner.sword < PARTNER_SWORD_MASTER) {
                Coop_GiveSword(PARTNER_SWORD_MASTER);
                Announce("PLAYER 2: MASTER SWORD", 3);
            } else {
                partner.bombBag = true;
                Coop_GiveBombs(kPartnerBombsPerBuy);
            }
            Sfx_PlaySfxCentered(NA_SE_SY_GET_ITEM);
            return true;
        case StationType::MysteryChest: {
            sChestUses++;
            if (!PowerUpActive(PowerUp::FireSale) && ChestMoves(sChestUses, sChestMoved, Rand_ZeroOne())) {
                GetRun().players[1].points += offer.cost; // refund, not earnings
                Announce("THE CHEST MOVED", 3);
                Sfx_PlaySfxCentered(NA_SE_VO_SK_LAUGH);
                MoveChest(play);
                sChestUses = 0;
                return true;
            }
            const PartnerChestPrize& prize = RollPartnerChest();
            prize.give();
            Announce(std::string("PLAYER 2: ") + prize.name, 3);
            Sfx_PlaySfxCentered(NA_SE_SY_GET_BOXITEM);
            return true;
        }
        case StationType::PackAPunch:
            partner.papSword = true;
            Announce("PLAYER 2: PUNCHED BLADE", 3);
            Sfx_PlaySfxCentered(NA_SE_SY_GET_ITEM);
            return true;
        case StationType::FairyFountain:
            Coop_Heal();
            Sfx_PlaySfxCentered(NA_SE_EV_FIATY_HEAL);
            return true;
        default:
            return false;
    }
}

static void Buy(PlayState* play, Station& station, const Offer& offer, int who) {
    if (!offer.available) {
        Fail();
        return;
    }
    if (offer.rupeeCost > 0) {
        if (gSaveContext.rupees < offer.rupeeCost) {
            Announce("NOT ENOUGH RUPEES", 2);
            Fail();
            return;
        }
        Rupees_ChangeBy(static_cast<s16>(-offer.rupeeCost));
    } else if (!SpendPoints(who, offer.cost)) {
        Fail();
        return;
    }

    Run& run = GetRun();
    if (who == 1 && BuyForPartner(play, station, offer)) {
        return;
    }
    switch (station.type) {
        case StationType::Armory: {
            const WeaponOffer* weapon = ArmoryOffer();
            if (weapon != nullptr) {
                GiveWeapon(play, weapon->item);
            } else {
                RefillAmmo();
            }
            Sfx_PlaySfxCentered(NA_SE_SY_GET_ITEM);
            break;
        }
        case StationType::MysteryChest: {
            sChestUses++;
            // The Keaton laugh is our teddy bear: after a few uses the chest may move on.
            if (!PowerUpActive(PowerUp::FireSale) && ChestMoves(sChestUses, sChestMoved, Rand_ZeroOne())) {
                run.players[who].points += offer.cost; // refund, not earnings: no Double Points
                Announce("THE CHEST MOVED", 3);
                Sfx_PlaySfxCentered(NA_SE_VO_SK_LAUGH);
                MoveChest(play);
                sChestUses = 0;
                return;
            }
            const ChestPrize& prize = RollChest();
            GiveWeapon(play, prize.item);
            Announce(prize.name, 3);
            Sfx_PlaySfxCentered(NA_SE_SY_GET_BOXITEM);
            break;
        }
        case StationType::Juggernog:
        case StationType::FairyRevive:
        case StationType::DoubleTap:
        case StationType::StaminUp: {
            PerkInfo info = PerkFor(station.type);
            ApplyPerk(who, info.perk);
            Announce(info.name, 3);
            Sfx_PlaySfxCentered(NA_SE_SY_GET_ITEM);
            break;
        }
        case StationType::Power:
            run.powerOn = true;
            RelightTorches();
            Announce("THE POWER IS ON", 3);
            Sfx_PlaySfxCentered(NA_SE_EV_DIAMOND_SWITCH);
            break;
        case StationType::PackAPunch: {
            PapUpgrade upgrade = FindPapUpgrade();
            if (upgrade.apply != nullptr) {
                upgrade.apply(play);
                Announce(upgrade.name, 3);
            }
            Sfx_PlaySfxCentered(NA_SE_SY_GET_ITEM);
            break;
        }
        case StationType::FairyFountain:
            gSaveContext.health = gSaveContext.healthCapacity;
            if (gSaveContext.isMagicAcquired) {
                Magic_Fill(play);
            }
            Sfx_PlaySfxCentered(NA_SE_EV_FIATY_HEAL);
            break;
        default:
            break;
    }
}

static void UpdateBuyer(PlayState* play, const Buyer& buyer) {
    if (!buyer.prompt->empty()) {
        return; // a revive prompt comes first
    }
    Station* nearest = nullptr;
    float nearestDist = kInteractDist;
    Vec3f buyerPos = buyer.pos;
    for (Station& station : sStations) {
        if (!station.placed) {
            continue;
        }
        float dist = Math_Vec3f_DistXZ(&station.pos, &buyerPos);
        if (dist < nearestDist && fabsf(station.pos.y - buyer.pos.y) < 80.0f) {
            nearest = &station;
            nearestDist = dist;
        }
    }
    if (nearest == nullptr) {
        return;
    }

    Offer offer = GetOffer(*nearest, buyer.index);
    std::string button = buyer.index == 0 ? BuyButtonName() : "A";
    std::string& prompt = *buyer.prompt;
    if (!offer.available) {
        prompt = offer.label + " - " + offer.unavailableReason;
    } else if (offer.rupeeCost > 0) {
        prompt = button + ": " + offer.label + " [" + std::to_string(offer.rupeeCost) + " RUPEES]";
    } else if (offer.cost > 0) {
        prompt = button + ": " + offer.label + " [" + std::to_string(offer.cost) + "]";
    } else {
        prompt = button + ": " + offer.label;
    }

    if (buyer.pressed && !buyer.busy) {
        Buy(play, *nearest, offer, buyer.index);
    }
}

void Stations_Update(PlayState* play, Player* player) {
    Run& run = GetRun();

    // Sun's Song can switch the power on from anywhere; keep the torches in step.
    static bool lastPowerOn = false;
    if (run.powerOn != lastPowerOn) {
        lastPowerOn = run.powerOn;
        RelightTorches();
    }

    Buyer buyers[2];
    int count = GetBuyers(play, player, buyers);
    for (int i = 0; i < count; i++) {
        UpdateBuyer(play, buyers[i]);
    }
}

} // namespace Zombies
