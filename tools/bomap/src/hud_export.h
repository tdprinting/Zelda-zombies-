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

} // namespace bomap
