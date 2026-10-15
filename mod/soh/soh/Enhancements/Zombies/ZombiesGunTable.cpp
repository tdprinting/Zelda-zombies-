// Zombies of Time: the Black Ops gun table and the pure ammo rules. No game state here,
// so tests/rules_test.cpp builds it on its own.
//
// Stats are Black Ops 1 Zombies gameplay values (Nacht der Untoten, Verruckt, Kino der
// Toten), written down in our own table. Nacht's World at War wall guns (Kar98k to BAR)
// are our own numbers matched to how they behave there: a bolt-action that one-shots
// early heads, a semi-auto carbine, the Thompson, three shotguns and the BAR. Fire rates
// are frames between shots at OoT's 20 logic updates a second (600 rounds a minute is 2
// frames); reloads are frames too.

#include "ZombiesGuns.h"

#include <algorithm>
#include <cstring>

namespace Zombies {

// clang-format off
static const GunStats kGuns[] = {
    //  id              class                 base: name, damage, clip, reserve, splash, radius        pap: name, damage, clip, reserve, splash, radius               head  pel auto   fire  reload spread range  pierce wall  zone box proj   Black Ops asset
    { GunId::None,     GunClass::Pistol,   { "",          0,    0,   0,   0,   0 },   { "",                     0,   0,   0,   0,   0 }, 1.0f, 1, false, 1.0f,  1,  0.0f,    0, 0,    0, 0, 0, false, "" },
    { GunId::M1911,    GunClass::Pistol,   { "M1911",    20,    8,  80,   0,   0 },   { "MUSTANG & SALLY",    100,  12,  50, 600, 110 }, 2.0f, 1, false, 3.0f, 32,  3.0f, 1500, 1,    0, 0, 0, false, "m1911_zm" },
    { GunId::Olympia,  GunClass::Shotgun,  { "OLYMPIA",  60,    2,  38,   0,   0 },   { "HADES",              120,   2,  60,   0,   0 }, 1.5f, 8, false, 6.0f, 54,  7.0f,  700, 1,  500, 0, 0, false, "rottweil72_zm" },
    { GunId::M14,      GunClass::Rifle,    { "M14",     105,    8,  92,   0,   0 },   { "MNESIA",             250,  15, 135,   0,   0 }, 2.5f, 1, false, 3.0f, 38,  2.5f, 2500, 2,  500, 0, 0, false, "m14_zm" },
    { GunId::MP5K,     GunClass::Smg,      { "MP5K",     75,   30, 120,   0,   0 },   { "MP115 KOLLIDER",     120,  40, 200,   0,   0 }, 2.0f, 1, true,  1.5f, 48,  4.0f, 1500, 1, 1000, 1, 0, false, "mp5k_zm" },
    { GunId::AK74u,    GunClass::Smg,      { "AK74U",   100,   20, 160,   0,   0 },   { "AK74FU2",            160,  40, 280,   0,   0 }, 2.0f, 1, true,  1.6f, 46,  4.0f, 1500, 1, 1200, 2, 0, false, "ak74u_zm" },
    { GunId::MP40,     GunClass::Smg,      { "MP40",     90,   32, 192,   0,   0 },   { "THE AFTERBURNER",    150,  64, 384,   0,   0 }, 2.0f, 1, true,  2.3f, 50,  3.5f, 1600, 1, 1000, 1, 0, false, "mp40_zm" },
    { GunId::Stakeout, GunClass::Shotgun,  { "STAKEOUT", 80,    6,  54,   0,   0 },   { "RAID",               160,  10,  72,   0,   0 }, 1.5f, 8, false, 9.0f, 60,  6.0f,  800, 1, 1500, 2, 0, false, "ithaca_zm" },
    { GunId::Spectre,  GunClass::Smg,      { "SPECTRE",  75,   30, 120,   0,   0 },   { "PHANTASM",           120,  45, 270,   0,   0 }, 2.0f, 1, true,  1.2f, 44,  4.5f, 1400, 1,    0, 0, 3, false, "spectre_zm" },
    { GunId::Galil,    GunClass::Rifle,    { "GALIL",   100,   35, 315,   0,   0 },   { "LAMENTATION",        150,  50, 400,   0,   0 }, 2.5f, 1, true,  1.8f, 56,  3.0f, 2500, 2,    0, 0, 3, false, "galil_zm" },
    { GunId::Commando, GunClass::Rifle,    { "COMMANDO",110,   30, 270,   0,   0 },   { "PREDATOR",           160,  40, 400,   0,   0 }, 2.5f, 1, true,  1.6f, 42,  3.0f, 2500, 2,    0, 0, 3, false, "commando_zm" },
    { GunId::Famas,    GunClass::Rifle,    { "FAMAS",   100,   30, 210,   0,   0 },   { "G16-GL35",           150,  45, 360,   0,   0 }, 2.5f, 1, true,  1.4f, 52,  3.0f, 2500, 2,    0, 0, 3, false, "famas_zm" },
    { GunId::HK21,     GunClass::Lmg,      { "HK21",    130,  125, 500,   0,   0 },   { "H115 OSCILLATOR",    200, 150, 750,   0,   0 }, 2.0f, 1, true,  1.8f, 100, 4.0f, 2500, 3,    0, 0, 3, false, "hk21_zm" },
    { GunId::RPK,      GunClass::Lmg,      { "RPK",     130,  100, 400,   0,   0 },   { "R115 RESONATOR",     200, 125, 500,   0,   0 }, 2.0f, 1, true,  1.6f, 90,  4.0f, 2500, 3,    0, 0, 3, false, "rpk_zm" },
    { GunId::RayGun,   GunClass::Launcher, { "RAY GUN", 1000,  20, 160, 300,  90 },   { "PORTER'S X2 RAY GUN",1500,  40, 200, 500, 110 }, 1.0f, 1, false, 5.0f, 60,  1.0f, 3000, 0,    0, 0, 1, true,  "ray_gun_zm" },
    // Nacht der Untoten's wall guns, World at War's: each on its own spot on the map's walls (wallZone -1).
    { GunId::Kar98k,   GunClass::Rifle,    { "KAR98K",  100,    5,  50,   0,   0 },   { "ARMAGEDDON 98",      600,   8,  60,   0,   0 }, 4.0f, 1, false, 22.0f, 50, 2.0f, 3000, 2,  200,-1, 0, false, "zombie_kar98k" },
    { GunId::M1Carbine,GunClass::Rifle,    { "M1A1 CARBINE",120, 15, 150,   0,   0 },   { "WIDDERSHINS RC-1",   160,  30, 180,   0,   0 }, 2.5f, 1, false, 2.5f, 40,  2.5f, 2500, 2,  600,-1, 0, false, "zombie_m1carbine" },
    { GunId::Thompson, GunClass::Smg,      { "THOMPSON",120,   20, 200,   0,   0 },   { "GIBS-O-MATIC",       200,  40, 250,   0,   0 }, 2.0f, 1, true,  1.6f, 52,  4.0f, 1600, 1, 1200,-1, 0, false, "zombie_thompson" },
    { GunId::DoubleBarrel,GunClass::Shotgun,{ "DOUBLE-BARREL", 80, 2,  60,   0,   0 },   { "24 BORE LONG RANGE", 160,   2,  60,   0,   0 }, 1.5f, 8, false, 5.0f, 60,  6.0f,  800, 1, 1200,-1, 0, false, "zombie_doublebarrel" },
    { GunId::SawedOff, GunClass::Shotgun,  { "SAWED-OFF",100,   2,  60,   0,   0 },   { "SNUFF BOX",          200,   2,  60,   0,   0 }, 1.5f, 8, false, 5.0f, 55,  8.0f,  550, 1, 1200,-1, 0, false, "zombie_doublebarrel_sawed" },
    { GunId::TrenchGun,GunClass::Shotgun,  { "TRENCH GUN",100,  6,  60,   0,   0 },   { "GUT SHOT",           200,  10,  60,   0,   0 }, 1.5f, 8, false, 8.0f, 70,  6.0f,  750, 1, 1500,-1, 0, false, "zombie_shotgun" },
    { GunId::BAR,      GunClass::Lmg,      { "BAR",     140,   20, 180,   0,   0 },   { "THE WIDOW MAKER",    260,  30, 300,   0,   0 }, 2.0f, 1, true,  2.6f, 56,  3.5f, 2500, 3, 1800,-1, 0, false, "zombie_bar" },
};
// clang-format on

static_assert(sizeof(kGuns) / sizeof(kGuns[0]) == static_cast<size_t>(GunId::Count), "one row per gun");

const GunStats& GunInfo(GunId id) {
    int i = static_cast<int>(id);
    if (i <= 0 || i >= static_cast<int>(GunId::Count)) {
        return kGuns[0];
    }
    return kGuns[i];
}

GunId GunByBoName(const char* boName) {
    if (boName == nullptr) {
        return GunId::None;
    }
    for (int i = 1; i < static_cast<int>(GunId::Count); i++) {
        const char* name = kGuns[i].boName;
        // A map's entities name the gun without the _zm suffix the zombies weapons carry.
        size_t n = std::strlen(boName);
        if (std::strcmp(name, boName) == 0 || (std::strncmp(name, boName, n) == 0 && std::strcmp(name + n, "_zm") == 0)) {
            return kGuns[i].id;
        }
    }
    return GunId::None;
}

const GunVariant& GunVariantOf(GunId id, bool pap) {
    const GunStats& stats = GunInfo(id);
    return pap ? stats.pap : stats.base;
}

const char* GunName(GunId id, bool pap) {
    return GunVariantOf(id, pap).name;
}

int GunAmmoCost(GunId id, bool pap) {
    if (pap) {
        return kPapAmmoCost;
    }
    return GunInfo(id).wallCost / 2;
}

bool PlayerGuns::Has(GunId id) const {
    for (int i = 0; i < slotCount; i++) {
        if (slots[i].id == id) {
            return true;
        }
    }
    return false;
}

int PlayerGuns::Count() const {
    int count = 0;
    for (int i = 0; i < slotCount; i++) {
        count += slots[i].id != GunId::None;
    }
    return count;
}

int PlayerGuns::Give(GunId id) {
    int slot = current;
    for (int i = 0; i < slotCount; i++) {
        if (slots[i].id == GunId::None) {
            slot = i;
            break;
        }
    }
    slots[slot] = GunSlot();
    slots[slot].id = id;
    Refill(slot);
    current = slot;
    return slot;
}

bool PlayerGuns::CanReload() const {
    const GunSlot& slot = Current();
    return slot.id != GunId::None && slot.reserve > 0 && slot.clip < GunVariantOf(slot.id, slot.pap).clip;
}

int PlayerGuns::Reload() {
    GunSlot& slot = Current();
    if (slot.id == GunId::None) {
        return 0;
    }
    int room = GunVariantOf(slot.id, slot.pap).clip - slot.clip;
    int moved = std::clamp(room, 0, slot.reserve);
    slot.clip += moved;
    slot.reserve -= moved;
    return moved;
}

void PlayerGuns::MaxAmmo() {
    for (int i = 0; i < slotCount; i++) {
        if (slots[i].id != GunId::None) {
            slots[i].reserve = GunVariantOf(slots[i].id, slots[i].pap).reserve;
        }
    }
}

void PlayerGuns::Refill(int slot) {
    GunSlot& s = slots[slot];
    if (s.id != GunId::None) {
        const GunVariant& v = GunVariantOf(s.id, s.pap);
        s.clip = v.clip;
        s.reserve = v.reserve;
    }
}

void PlayerGuns::PackAPunch(int slot) {
    slots[slot].pap = true;
    Refill(slot);
}

int PlayerGuns::NextSlot() const {
    for (int step = 1; step < slotCount; step++) {
        int i = (current + step) % slotCount;
        if (slots[i].id != GunId::None) {
            return i;
        }
    }
    return current;
}

int GunBoxWeight(const PlayerGuns& guns) {
    int total = 0;
    for (int i = 1; i < static_cast<int>(GunId::Count); i++) {
        GunId id = static_cast<GunId>(i);
        if (!guns.Has(id)) {
            total += GunInfo(id).boxWeight;
        }
    }
    return total;
}

GunId GunBoxPick(const PlayerGuns& guns, int roll) {
    for (int i = 1; i < static_cast<int>(GunId::Count); i++) {
        GunId id = static_cast<GunId>(i);
        int weight = guns.Has(id) ? 0 : GunInfo(id).boxWeight;
        if (roll < weight) {
            return id;
        }
        roll -= weight;
    }
    return GunId::None;
}

} // namespace Zombies
