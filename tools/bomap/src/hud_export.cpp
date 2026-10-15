#include "hud_export.h"

#include "asset_export.h"
#include "texture_source.h"
#include "zone_walker.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>

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

static bool WritePng(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h) {
    std::vector<uint8_t> png = EncodePng(rgba, w, h);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(file);
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
        if (WritePng(outDir + "/" + material + ".png", tex.rgba, tex.width, tex.height)) {
            result.written.push_back(material);
        } else {
            result.missing.push_back(material);
        }
    }
    return result;
}

const std::vector<WeaponIcon>& WeaponIcons() {
    // Kept in step with the boName column of mod/.../Zombies/ZombiesGunTable.cpp.
    static const std::vector<WeaponIcon> kIcons = {
        { "m1911_zm", "hud_icon_colt" },
        { "rottweil72_zm", "" },
        { "m14_zm", "hud_icon_m14" },
        { "mp5k_zm", "hud_icon_mp5" },
        { "ak74u_zm", "hud_icon_ak74u" },
        { "mp40_zm", "hud_mp40" },
        { "ithaca_zm", "hud_ithaca" },
        { "spectre_zm", "hud_icon_spectre" },
        { "galil_zm", "hud_icon_galil" },
        { "commando_zm", "hud_commando" },
        { "famas_zm", "hud_icon_famas" },
        { "hk21_zm", "hud_hk21" },
        { "rpk_zm", "hud_rpk" },
        { "ray_gun_zm", "" },
        { "zombie_kar98k", "hud_icon_kar98k" },
        { "zombie_m1carbine", "hud_icon_m1carbine" },
        { "zombie_thompson", "hud_icon_thompson" },
        { "zombie_doublebarrel", "hud_icon_double_barrel" },
        { "zombie_doublebarrel_sawed", "hud_icon_double_barrel" },
        { "zombie_shotgun", "hud_icon_shotgun" },
        { "zombie_bar", "hud_icon_bar" },
    };
    return kIcons;
}

namespace {

constexpr int kIconW = 128;
constexpr int kIconH = 64;
constexpr int kSuper = 4; // samples a side per icon pixel

// The model side on, muzzle to the left as Black Ops draws its icons, in the look of Black Ops' icons: light grey,
// lit from above, with a dark rim so it reads on any background.
std::vector<uint8_t> RenderIcon(const Model& model) {
    struct P {
        float x, y, depth, shade;
    };
    std::vector<P> pts;
    std::vector<uint32_t> tris;
    for (const Surface& s : model.surfaces) {
        uint32_t base = static_cast<uint32_t>(pts.size());
        for (const Vertex& v : s.verts) {
            // Black Ops axes: X forward, Y left, Z up. Seen from the left side (+Y).
            float light = 0.8f * v.normal[2] + 0.35f * v.normal[1] - 0.2f * v.normal[0];
            pts.push_back({ -v.pos[0], -v.pos[2], -v.pos[1], std::clamp(0.5f + 0.5f * light, 0.0f, 1.0f) });
        }
        for (uint16_t t : s.tris) {
            tris.push_back(base + t);
        }
    }
    std::vector<uint8_t> rgba(static_cast<size_t>(kIconW) * kIconH * 4, 0);
    if (pts.empty() || tris.size() < 3) {
        return rgba;
    }
    float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
    for (const P& p : pts) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    const int sw = kIconW * kSuper;
    const int sh = kIconH * kSuper;
    const float margin = 3.0f * kSuper;
    float scale = std::min((static_cast<float>(sw) - 2.0f * margin) / std::max(maxX - minX, 1e-3f),
                           (static_cast<float>(sh) - 2.0f * margin) / std::max(maxY - minY, 1e-3f));
    float ox = (static_cast<float>(sw) - (maxX - minX) * scale) / 2.0f - minX * scale;
    float oy = (static_cast<float>(sh) - (maxY - minY) * scale) / 2.0f - minY * scale;
    for (P& p : pts) {
        p.x = p.x * scale + ox;
        p.y = p.y * scale + oy;
    }
    std::vector<float> depth(static_cast<size_t>(sw) * sh, 1e30f);
    std::vector<float> shade(static_cast<size_t>(sw) * sh, -1.0f);
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        if (tris[t] >= pts.size() || tris[t + 1] >= pts.size() || tris[t + 2] >= pts.size()) {
            continue;
        }
        const P& a = pts[tris[t]];
        const P& b = pts[tris[t + 1]];
        const P& c = pts[tris[t + 2]];
        float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-6f) {
            continue;
        }
        int x0 = std::max(0, static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))));
        int x1 = std::min(sw - 1, static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))));
        int y0 = std::max(0, static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))));
        int y1 = std::min(sh - 1, static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))));
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                float px = static_cast<float>(x) + 0.5f;
                float py = static_cast<float>(y) + 0.5f;
                float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                    continue;
                }
                size_t i = static_cast<size_t>(y) * sw + x;
                float d = w0 * a.depth + w1 * b.depth + w2 * c.depth;
                if (d < depth[i]) { // nearer the viewer
                    depth[i] = d;
                    shade[i] = w0 * a.shade + w1 * b.shade + w2 * c.shade;
                }
            }
        }
    }
    // Down to icon pixels: coverage is alpha, the mean shade a light grey.
    std::vector<float> alpha(static_cast<size_t>(kIconW) * kIconH, 0.0f);
    for (int y = 0; y < kIconH; y++) {
        for (int x = 0; x < kIconW; x++) {
            int covered = 0;
            float sum = 0.0f;
            for (int sy = 0; sy < kSuper; sy++) {
                for (int sx = 0; sx < kSuper; sx++) {
                    float v = shade[static_cast<size_t>(y * kSuper + sy) * sw + (x * kSuper + sx)];
                    if (v >= 0.0f) {
                        covered++;
                        sum += v;
                    }
                }
            }
            size_t i = static_cast<size_t>(y) * kIconW + x;
            if (covered > 0) {
                float a = static_cast<float>(covered) / static_cast<float>(kSuper * kSuper);
                auto grey = static_cast<uint8_t>(std::lround(90.0f + 160.0f * sum / static_cast<float>(covered)));
                rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = grey;
                rgba[i * 4 + 3] = static_cast<uint8_t>(std::lround(a * 255.0f));
                alpha[i] = a;
            }
        }
    }
    // The dark rim: a pixel next to the gun but not on it.
    for (int y = 0; y < kIconH; y++) {
        for (int x = 0; x < kIconW; x++) {
            size_t i = static_cast<size_t>(y) * kIconW + x;
            if (alpha[i] > 0.5f) {
                continue;
            }
            float edge = 0.0f;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < kIconW && ny < kIconH) {
                        edge = std::max(edge, alpha[static_cast<size_t>(ny) * kIconW + nx]);
                    }
                }
            }
            float rim = edge * 0.7f;
            if (rim > alpha[i]) {
                float a = alpha[i];
                // The gun's own edge pixel over the rim behind it.
                float out = a + rim * (1.0f - a);
                for (int c = 0; c < 3; c++) {
                    rgba[i * 4 + c] = static_cast<uint8_t>(std::lround(rgba[i * 4 + c] * a / std::max(out, 1e-3f)));
                }
                rgba[i * 4 + 3] = static_cast<uint8_t>(std::lround(out * 255.0f));
            }
        }
    }
    return rgba;
}

} // namespace

HudImageResult ExportWeaponIcons(const std::vector<const Zone*>& zones, const std::string& mainDir,
                                 const std::string& outDir) {
    HudImageResult result;
    std::vector<WalkResult> walks;
    std::vector<std::unique_ptr<TextureSource>> textures;
    std::vector<std::vector<WeaponInfo>> weapons;
    walks.reserve(zones.size());
    for (const Zone* zone : zones) {
        walks.push_back(WalkZone(*zone));
    }
    for (size_t z = 0; z < zones.size(); z++) {
        textures.push_back(std::make_unique<TextureSource>(*zones[z], walks[z], mainDir));
        weapons.push_back(ReadWeapons(*zones[z], walks[z]));
    }
    for (const WeaponIcon& icon : WeaponIcons()) {
        std::string path = outDir + "/icon_" + icon.weapon + ".png";
        bool done = false;
        // Black Ops' own icon.
        for (size_t z = 0; z < zones.size() && !done && icon.material[0] != '\0'; z++) {
            std::string image = HudImageOf(walks[z], icon.material);
            Texture tex;
            if (!image.empty() && textures[z]->Load(image, 128, tex)) {
                done = WritePng(path, tex.rgba, tex.width, tex.height);
            }
        }
        // Otherwise the gun's world model, drawn side on.
        std::vector<std::string> models;
        for (const std::vector<WeaponInfo>& list : weapons) {
            for (const WeaponInfo& w : list) {
                if (w.name == icon.weapon) {
                    models.insert(models.end(), w.worldModels.begin(), w.worldModels.end());
                    models.insert(models.end(), w.viewModels.begin(), w.viewModels.end());
                }
            }
        }
        for (size_t m = 0; m < models.size() && !done; m++) {
            for (size_t z = 0; z < zones.size() && !done; z++) {
                int asset = walks[z].Find(0x05, models[m]);
                if (asset < 0) {
                    continue;
                }
                Model model;
                try {
                    model = ReadModel(*zones[z], walks[z], asset);
                } catch (const std::exception&) {
                    continue;
                }
                if (model.surfaces.empty()) {
                    continue;
                }
                done = WritePng(path, RenderIcon(model), kIconW, kIconH);
            }
        }
        (done ? result.written : result.missing).push_back(std::string("icon_") + icon.weapon);
    }
    return result;
}

} // namespace bomap
