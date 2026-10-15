#include "hud_export.h"

#include "asset_export.h"
#include "texture_source.h"
#include "zone_walker.h"

#include <algorithm>
#include <fstream>

namespace bomap {

const std::vector<std::string>& HudMaterials() {
    // Kept in step with kBoImageNames in mod/.../Zombies/ZombiesHud.cpp.
    static const std::vector<std::string> kMaterials = {
        "hud_chalk_1",
        "hud_chalk_2",
        "hud_chalk_3",
        "hud_chalk_4",
        "hud_chalk_5",
        "scorebar_zom_1",
        "scorebar_zom_2",
        "specialty_juggernaut_zombies",
        "specialty_quickrevive_zombies",
        "specialty_doubletap_zombies",
        "specialty_longersprint_zombies",
        "specialty_instakill_zombies",
        "specialty_doublepoints_zombies",
        "specialty_firesale_zombies",
        "overlay_low_health",
        "hud_dpad",
    };
    return kMaterials;
}

// A 2D material's image: its colour map, or the only image it has.
static std::string HudImageOf(const WalkResult& walk, const std::string& material) {
    std::string map = MaterialColorMap(walk, material);
    if (!map.empty()) {
        return map;
    }
    int m = walk.Find(0x06, material);
    if (m < 0) {
        return std::string();
    }
    for (const MaterialTexture& t : walk.materials.at(m).textures) {
        if (!t.image.empty()) {
            return t.image;
        }
    }
    return std::string();
}

HudImageResult ExportHudImages(const Zone& zone, const std::string& mainDir, const std::string& outDir,
                               const std::vector<std::string>& skip) {
    HudImageResult result;
    WalkResult walk = WalkZone(zone);
    TextureSource textures(zone, walk, mainDir);
    for (const std::string& material : HudMaterials()) {
        if (std::find(skip.begin(), skip.end(), material) != skip.end()) {
            continue;
        }
        std::string image = HudImageOf(walk, material);
        Texture tex;
        // The game keeps HUD images to 4096 texels; 128 a side leaves it room to halve.
        if (image.empty() || !textures.Load(image, 128, tex)) {
            result.missing.push_back(material);
            continue;
        }
        std::vector<uint8_t> png = EncodePng(tex.rgba, tex.width, tex.height);
        std::ofstream file(outDir + "/" + material + ".png", std::ios::binary);
        file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        if (file) {
            result.written.push_back(material);
        } else {
            result.missing.push_back(material);
        }
    }
    return result;
}

} // namespace bomap
