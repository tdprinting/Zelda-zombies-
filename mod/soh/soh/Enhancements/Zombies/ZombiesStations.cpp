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
// switch flag. 0x3F is a temp flag, so lighting one never trips anything in the arena.
static constexpr s16 kTorchGolden = 0x0000 | 0x3F;
static constexpr s16 kTorchStone = 0x1000 | 0x3F;
static constexpr s16 kTorchWooden = 0x2000;
static constexpr s16 kTorchLit = 0x0400;

enum class StationType {
    Armory,
    MysteryChest,
    Juggernog,
    FairyRevive,
    DoubleTap,
    StaminUp,
    Power,
    PackAPunch,
    FairyFountain,
    Count,
};

struct Station {
    StationType type;
    Vec3f pos;
    Actor* torch = nullptr;
    bool placed = false;
};

static Station sStations[static_cast<int>(StationType::Count)];
static int sChestUses = 0;
static bool sPapSword = false;

static bool NeedsPower(StationType type) {
    switch (type) {
        case StationType::Juggernog:
        case StationType::FairyRevive:
        case StationType::DoubleTap:
        case StationType::StaminUp:
        case StationType::PackAPunch:
            return true;
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
            return GetRun().powerOn ? (kTorchStone | kTorchLit) : kTorchStone;
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
            Station& station = sStations[index];
            station.pos = pos;
            station.placed = true;
            station.torch = Actor_Spawn(&play->actorCtx, play, ACTOR_OBJ_SYOKUDAI, pos.x, pos.y, pos.z, 0, 0, 0,
                                        TorchParams(station.type));
            if (station.torch != nullptr) {
                station.torch->room = -1;
            }
            return true;
        }
    }
    return false;
}

void Stations_Clear() {
    for (Station& station : sStations) {
        if (station.torch != nullptr) {
            Actor_Kill(station.torch);
        }
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
        s16 angle = run.originYaw + static_cast<s16>(slot * (0x10000 / count));
        PlaceStation(play, index, angle);
    }
    sChestUses = 0;
    sPapSword = false;
}

static void MoveChest(PlayState* play) {
    int index = static_cast<int>(StationType::MysteryChest);
    Station& chest = sStations[index];
    if (chest.torch != nullptr) {
        Actor_Kill(chest.torch);
    }
    chest.torch = nullptr;
    chest.placed = false;
    PlaceStation(play, index, static_cast<s16>(Rand_ZeroOne() * 0x10000));
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
            Interface_LoadItemIcon1(play, b);
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
    int count = LINK_IS_ADULT ? std::size(kAdultArmory) : std::size(kChildArmory);
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
    int count = LINK_IS_ADULT ? std::size(kAdultChest) : std::size(kChildChest);
    int total = 0;
    for (int i = 0; i < count; i++) {
        total += list[i].weight;
    }
    int roll = std::min(static_cast<int>(Rand_ZeroOne() * total), total - 1);
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

static void GiveFairyBottle() {
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
        target = SLOT_BOTTLE_1; // every bottle is full: the first one's contents make way
    }
    gSaveContext.inventory.items[target] = ITEM_FAIRY;
    for (int b = 1; b <= 3; b++) {
        if (gSaveContext.equips.cButtonSlots[b - 1] == target) {
            gSaveContext.equips.buttonItems[b] = ITEM_FAIRY;
            Interface_LoadItemIcon1(gPlayState, b);
        }
    }
}

static bool HasPerk(Perk perk) {
    if (perk == PERK_FAIRY_REVIVE) {
        return Inventory_HasSpecificBottle(ITEM_FAIRY);
    }
    return GetRun().players[0].perks[perk];
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
            return { PERK_FAIRY_REVIVE, "FAIRY REVIVE", 500 };
        case StationType::DoubleTap:
            return { PERK_DOUBLE_TAP, "DOUBLE TAP", 2000 };
        case StationType::StaminUp:
        default:
            return { PERK_STAMIN_UP, "STAMIN-UP", 2000 };
    }
}

static void ApplyPerk(Perk perk) {
    PlayerState& ps = GetRun().players[0];
    switch (perk) {
        case PERK_JUGGERNOG:
            gSaveContext.isDoubleDefenseAcquired = true;
            gSaveContext.inventory.defenseHearts = 20;
            gSaveContext.health = gSaveContext.healthCapacity;
            break;
        case PERK_FAIRY_REVIVE:
            GiveFairyBottle();
            break;
        default:
            break;
    }
    ps.perks[perk] = true;
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

static Offer GetOffer(const Station& station) {
    Offer offer;
    const Run& run = GetRun();

    if (NeedsPower(station.type) && !run.powerOn) {
        offer.available = false;
        offer.unavailableReason = "NEEDS POWER";
    }

    switch (station.type) {
        case StationType::Armory: {
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
            if (HasPerk(info.perk)) {
                offer.available = false;
                offer.unavailableReason = "OWNED";
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
            offer.rupeeCost = kFountainRupees;
            break;
        default:
            break;
    }
    return offer;
}

static void Fail() {
    Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
}

static void Buy(PlayState* play, Station& station, const Offer& offer) {
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
        Rupees_ChangeBy(-offer.rupeeCost);
    } else if (!SpendPoints(0, offer.cost)) {
        Fail();
        return;
    }

    Run& run = GetRun();
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
            if (sChestUses > 4 && !PowerUpActive(PowerUp::FireSale) && Rand_ZeroOne() < 1.0f / 6.0f) {
                run.players[0].points += offer.cost; // refund, not earnings: no Double Points
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
            ApplyPerk(info.perk);
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

static bool BuyButtonPressed(PlayState* play) {
    u16 button = CVarGetInteger(CVAR_ZOMBIES("BuyButton"), 0) == 1 ? BTN_DDOWN : BTN_L;
    return CHECK_BTN_ALL(play->state.input[0].press.button, button);
}

void Stations_Update(PlayState* play, Player* player) {
    Run& run = GetRun();
    run.prompt.clear();

    // Sun's Song can switch the power on from anywhere; keep the torches in step.
    static bool lastPowerOn = false;
    if (run.powerOn != lastPowerOn) {
        lastPowerOn = run.powerOn;
        RelightTorches();
    }

    Station* nearest = nullptr;
    float nearestDist = kInteractDist;
    for (Station& station : sStations) {
        if (!station.placed) {
            continue;
        }
        float dist = Math_Vec3f_DistXZ(&station.pos, &player->actor.world.pos);
        if (dist < nearestDist && fabsf(station.pos.y - player->actor.world.pos.y) < 80.0f) {
            nearest = &station;
            nearestDist = dist;
        }
    }
    if (nearest == nullptr) {
        return;
    }

    Offer offer = GetOffer(*nearest);
    std::string button = BuyButtonName();
    if (!offer.available) {
        run.prompt = offer.label + " - " + offer.unavailableReason;
    } else if (offer.rupeeCost > 0) {
        run.prompt = button + ": " + offer.label + " [" + std::to_string(offer.rupeeCost) + " RUPEES]";
    } else if (offer.cost > 0) {
        run.prompt = button + ": " + offer.label + " [" + std::to_string(offer.cost) + "]";
    } else {
        run.prompt = button + ": " + offer.label;
    }

    if (BuyButtonPressed(play) && !Player_InCsMode(play)) {
        Buy(play, *nearest, offer);
    }
}

} // namespace Zombies
