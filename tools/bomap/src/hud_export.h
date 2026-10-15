#pragma once

// Black Ops zombies HUD images for the game's HUD (ZombiesHud.cpp): chalk tally marks,
// the points bar, perk and power-up icons, the low health overlay and the d-pad dial.
// Each is written as <outDir>/<material>.png, the name the game looks for in
// zombies/hud/. Images Black Ops streams from main/*.iwd need `mainDir`.

#include "fastfile.h"

#include <string>
#include <vector>

namespace bomap {

struct HudImageResult {
    std::vector<std::string> written; // material names
    std::vector<std::string> missing; // not in the zone, or no pixels found
};

// The materials the game's HUD can use, in the order the game lists them.
const std::vector<std::string>& HudMaterials();

// Writes every HUD material found in `zone` to `outDir`. Materials already written by an
// earlier zone can be skipped by passing them in `skip`.
HudImageResult ExportHudImages(const Zone& zone, const std::string& mainDir, const std::string& outDir,
                               const std::vector<std::string>& skip = {});

// Gun icons for the HUD, beside the ammo count: <outDir>/icon_<weapon>.png for each gun
// the game sells, named after the weapon (m1911_zm, ray_gun_zm, zombie_thompson, ...)
// so the game finds them by the name it imports the gun's models by. Each is the weapon's
// Black Ops HUD icon when the zones and main/*.iwd have it. Black Ops has no icon for
// some zombies guns (the Ray Gun, the Olympia): those are drawn from the gun's world
// model instead, side on, in the same white style.
struct WeaponIcon {
    const char* weapon;   // weapon name, as in the game's gun table (ZombiesGunTable.cpp)
    const char* material; // its Black Ops HUD icon, or "" for none
};
const std::vector<WeaponIcon>& WeaponIcons();

HudImageResult ExportWeaponIcons(const std::vector<const Zone*>& zones, const std::string& mainDir,
                                 const std::string& outDir);

} // namespace bomap
