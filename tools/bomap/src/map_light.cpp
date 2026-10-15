#include "map_light.h"

#include "asset_export.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace bomap {

namespace {

constexpr uint32_t kDxt1 = 0x31545844;
constexpr uint32_t kR5G6B5 = 23;
constexpr uint32_t kG16R16 = 34;

// The raw pixels of an in-zone image, or null.
const uint8_t* ImagePixels(const Zone& zone, const WalkResult& walk, int asset, uint32_t& format, size_t& size,
                           int& width, int& height) {
    if (asset < 0 || walk.assets[asset].type != 0x08 || walk.assets[asset].header == kNoData) {
        return nullptr;
    }
    auto it = walk.images.find(asset);
    if (it == walk.images.end() || it->second.loadDef == kNoData || it->second.resourceSize == 0) {
        return nullptr;
    }
    const ZoneStream& s = *walk.stream;
    width = s.U16(walk.assets[asset].header + 20);
    height = s.U16(walk.assets[asset].header + 22);
    format = it->second.format;
    size = it->second.resourceSize;
    if (it->second.loadDef + 12 + size > zone.data.size()) {
        return nullptr;
    }
    return &zone.data[it->second.loadDef + 12];
}

float Dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float Smooth(float e0, float e1, float x) {
    if (e1 <= e0) {
        return x >= e0 ? 1.0f : 0.0f;
    }
    float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

} // namespace

MapLighting::MapLighting(const Zone& zone, const WalkResult& walk, int worldIndex, const LightOptions& options)
    : mOptions(options) {
    const ZoneStream& s = *walk.stream;
    size_t gw = walk.assets[worldIndex].header;
    size_t draw = gw + 344;
    size_t lightmaps = walk.Pointer(draw + 16);
    if (lightmaps != kNoData && s.U32(draw + 12) > 0) {
        uint32_t format;
        size_t size;
        int w, h;
        // Primary: DXT1 whose green channel is how much of the surface's primary light
        // reaches each texel (its shadow).
        if (const uint8_t* p = ImagePixels(zone, walk, walk.AssetAt(lightmaps), format, size, w, h)) {
            std::vector<uint8_t> rgba;
            if (format == kDxt1 && DecodePixels(format, p, size, w, h, rgba)) {
                mShadowW = w;
                mShadowH = h;
                mShadow.resize(static_cast<size_t>(w) * h);
                for (size_t i = 0; i < mShadow.size(); i++) {
                    mShadow[i] = rgba[i * 4 + 1];
                }
            }
        }
        // Secondary: R5G6B5, two layers one above the other, each a chromaticity
        // (r + g + b = 1). SecondaryB: G16R16, the two layers' intensities.
        uint32_t fa, fb;
        size_t sa, sb;
        int wa, ha, wb, hb;
        const uint8_t* a = ImagePixels(zone, walk, walk.AssetAt(lightmaps + 4), fa, sa, wa, ha);
        const uint8_t* b = ImagePixels(zone, walk, walk.AssetAt(lightmaps + 8), fb, sb, wb, hb);
        if (a != nullptr && b != nullptr && fa == kR5G6B5 && fb == kG16R16 && wa == wb && ha == hb * 2 &&
            sa >= static_cast<size_t>(wa) * ha * 2 && sb >= static_cast<size_t>(wb) * hb * 4) {
            mWidth = wb;
            mHeight = hb;
            size_t texels = static_cast<size_t>(wb) * hb;
            mSecondary.assign(texels * 3, 0.0f);
            for (size_t i = 0; i < texels; i++) {
                const uint8_t* q = b + i * 4;
                float intensity[2] = { static_cast<float>(q[0] | q[1] << 8) / 1024.0f,
                                       static_cast<float>(q[2] | q[3] << 8) / 1024.0f };
                for (int layer = 0; layer < 2; layer++) {
                    const uint8_t* c = a + (layer * texels + i) * 2;
                    uint16_t v = static_cast<uint16_t>(c[0] | c[1] << 8);
                    float rgb[3] = { (v >> 11 & 31) / 31.0f, (v >> 5 & 63) / 63.0f, (v & 31) / 31.0f };
                    // The layers store chromaticity weighted by each channel's share of
                    // luminance, so the colour's luminance is the intensity: undo the
                    // weights, then pull the (very blue) result part way back to grey.
                    const float kLuma[3] = { 0.299f, 0.587f, 0.114f };
                    float y = 0;
                    for (int k = 0; k < 3; k++) {
                        rgb[k] /= kLuma[k];
                        y += rgb[k] * kLuma[k];
                    }
                    for (int k = 0; k < 3; k++) {
                        rgb[k] = (y + (rgb[k] - y) * options.indirectSaturation) / 3.0f;
                    }
                    for (int k = 0; k < 3; k++) {
                        mSecondary[i * 3 + k] += rgb[k] * intensity[layer] * options.indirectScale;
                    }
                }
            }
        }
    }
    // Primary lights live in the ComWorld.
    for (size_t i = 0; i < walk.assets.size(); i++) {
        if (walk.assets[i].type != 0x0D || walk.assets[i].header == kNoData) {
            continue;
        }
        size_t cw = walk.assets[i].header;
        uint32_t count = s.U32(cw + 8);
        size_t lights = walk.Pointer(cw + 12);
        for (uint32_t k = 0; lights != kNoData && k < count; k++) {
            size_t l = lights + k * 220;
            Light light;
            light.type = s.U8(l);
            for (int c = 0; c < 3; c++) {
                light.color[c] = s.F32(l + 8 + c * 4);
                light.dir[c] = s.F32(l + 20 + c * 4);
                light.origin[c] = s.F32(l + 32 + c * 4);
            }
            light.radius = s.F32(l + 44);
            light.cosOuter = s.F32(l + 48);
            light.cosInner = s.F32(l + 52);
            if (light.type == 1 && mSun < 0) {
                mSun = static_cast<int>(k);
            }
            mLights.push_back(light);
        }
        break;
    }
}

void MapLighting::Direct(const float pos[3], const float normal[3], int index, float visible, float out[3]) const {
    if (index <= 0 || index >= static_cast<int>(mLights.size()) || visible <= 0) {
        return;
    }
    const Light& l = mLights[index];
    float amount = 0;
    if (l.type == 1) {
        amount = std::max(0.0f, Dot3(normal, l.dir));
    } else if (l.type == 2 || l.type == 3) {
        float d[3] = { l.origin[0] - pos[0], l.origin[1] - pos[1], l.origin[2] - pos[2] };
        float dist = std::sqrt(Dot3(d, d));
        if (dist >= l.radius || dist < 1e-3f) {
            return;
        }
        for (float& c : d) {
            c /= dist;
        }
        float falloff = 1.0f - dist / l.radius;
        amount = falloff * falloff * std::max(0.0f, Dot3(normal, d));
        if (l.type == 2) {
            amount *= Smooth(l.cosOuter, l.cosInner, Dot3(d, l.dir));
        }
    }
    amount *= visible * mOptions.directScale;
    for (int c = 0; c < 3; c++) {
        out[c] += l.color[c] * amount;
    }
}

void MapLighting::World(const float pos[3], const float normal[3], float lu, float lv, int primaryLight,
                        float out[3]) {
    float indirect[3] = { 0, 0, 0 };
    if (!mSecondary.empty()) {
        // Bilinear, with texel centres at half-integers.
        float x = lu * mWidth - 0.5f, y = lv * mHeight - 0.5f;
        int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
        float fx = x - x0, fy = y - y0;
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < 2; i++) {
                int tx = std::clamp(x0 + i, 0, mWidth - 1), ty = std::clamp(y0 + j, 0, mHeight - 1);
                float w = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
                const float* t = &mSecondary[(static_cast<size_t>(ty) * mWidth + tx) * 3];
                for (int c = 0; c < 3; c++) {
                    indirect[c] += w * t[c];
                }
            }
        }
    }
    float visible = 1.0f;
    if (!mShadow.empty()) {
        int x = std::clamp(static_cast<int>(lu * mShadowW), 0, mShadowW - 1);
        int y = std::clamp(static_cast<int>(lv * mShadowH), 0, mShadowH - 1);
        visible = mShadow[static_cast<size_t>(y) * mShadowW + x] / 255.0f;
    }
    for (int c = 0; c < 3; c++) {
        out[c] = indirect[c];
    }
    Direct(pos, normal, primaryLight, visible, out);
    float sunVisible = primaryLight == mSun ? visible : 0.0f;
    mSamples.insert(mSamples.end(), { pos[0], pos[1], pos[2], indirect[0], indirect[1], indirect[2], sunVisible,
                                      primaryLight == mSun ? 1.0f : 0.0f });
}

void MapLighting::FinishGrid() {
    if (mSamples.empty()) {
        return;
    }
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t i = 0; i < mSamples.size(); i += 8) {
        for (int k = 0; k < 3; k++) {
            lo[k] = std::min(lo[k], mSamples[i + k]);
            hi[k] = std::max(hi[k], mSamples[i + k]);
        }
    }
    // Keep the grid to a few million cells on big maps.
    mCell = 64;
    for (;;) {
        size_t cells = 1;
        for (int k = 0; k < 3; k++) {
            mGridSize[k] = static_cast<int>((hi[k] - lo[k]) / mCell) + 1;
            cells *= mGridSize[k];
        }
        if (cells <= 4000000) {
            break;
        }
        mCell *= 1.5f;
    }
    memcpy(mGridMin, lo, sizeof(lo));
    size_t cells = static_cast<size_t>(mGridSize[0]) * mGridSize[1] * mGridSize[2];
    // r g b, sun visibility, sun weight, weight
    std::vector<float> sum(cells * 6, 0.0f);
    auto cellOf = [&](const float* p) {
        int c[3];
        for (int k = 0; k < 3; k++) {
            c[k] = std::clamp(static_cast<int>((p[k] - mGridMin[k]) / mCell), 0, mGridSize[k] - 1);
        }
        return (static_cast<size_t>(c[2]) * mGridSize[1] + c[1]) * mGridSize[0] + c[0];
    };
    for (size_t i = 0; i < mSamples.size(); i += 8) {
        float* g = &sum[cellOf(&mSamples[i]) * 6];
        g[0] += mSamples[i + 3];
        g[1] += mSamples[i + 4];
        g[2] += mSamples[i + 5];
        g[3] += mSamples[i + 6];
        g[4] += mSamples[i + 7];
        g[5] += 1;
    }
    mSamples.clear();
    mSamples.shrink_to_fit();
    // Empty cells (inside props, in the air) take the nearest filled ones, growing
    // outwards a cell at a time.
    mGrid.assign(cells * 5, 0.0f);
    std::vector<uint8_t> filled(cells, 0);
    for (size_t c = 0; c < cells; c++) {
        const float* g = &sum[c * 6];
        if (g[5] > 0) {
            float* o = &mGrid[c * 5];
            o[0] = g[0] / g[5];
            o[1] = g[1] / g[5];
            o[2] = g[2] / g[5];
            o[3] = g[4] > 0 ? g[3] / g[4] : 0.0f;
            o[4] = g[4] / g[5]; // how much of the cell is under the open sky
            filled[c] = 1;
        }
    }
    for (int pass = 0; pass < 6; pass++) {
        std::vector<uint8_t> next = filled;
        std::vector<float> grid = mGrid;
        for (int z = 0; z < mGridSize[2]; z++) {
            for (int y = 0; y < mGridSize[1]; y++) {
                for (int x = 0; x < mGridSize[0]; x++) {
                    size_t c = (static_cast<size_t>(z) * mGridSize[1] + y) * mGridSize[0] + x;
                    if (filled[c]) {
                        continue;
                    }
                    float acc[5] = {};
                    int n = 0;
                    for (int dz = -1; dz <= 1; dz++) {
                        for (int dy = -1; dy <= 1; dy++) {
                            for (int dx = -1; dx <= 1; dx++) {
                                int nx = x + dx, ny = y + dy, nz = z + dz;
                                if (nx < 0 || ny < 0 || nz < 0 || nx >= mGridSize[0] || ny >= mGridSize[1] ||
                                    nz >= mGridSize[2]) {
                                    continue;
                                }
                                size_t nc = (static_cast<size_t>(nz) * mGridSize[1] + ny) * mGridSize[0] + nx;
                                if (!filled[nc]) {
                                    continue;
                                }
                                for (int k = 0; k < 5; k++) {
                                    acc[k] += mGrid[nc * 5 + k];
                                }
                                n++;
                            }
                        }
                    }
                    if (n > 0) {
                        for (int k = 0; k < 5; k++) {
                            grid[c * 5 + k] = acc[k] / n;
                        }
                        next[c] = 1;
                    }
                }
            }
        }
        mGrid.swap(grid);
        filled.swap(next);
    }
}

void MapLighting::Prop(const float pos[3], const float normal[3], int primaryLight, float out[3]) const {
    out[0] = out[1] = out[2] = 0;
    float sunVisible = 0, open = 0;
    if (!mGrid.empty()) {
        int c[3];
        for (int k = 0; k < 3; k++) {
            c[k] = std::clamp(static_cast<int>((pos[k] - mGridMin[k]) / mCell), 0, mGridSize[k] - 1);
        }
        const float* g = &mGrid[((static_cast<size_t>(c[2]) * mGridSize[1] + c[1]) * mGridSize[0] + c[0]) * 5];
        // Faces turned down get a little less of the light around them.
        float facing = 0.8f + 0.2f * std::clamp(normal[2], -1.0f, 1.0f);
        for (int k = 0; k < 3; k++) {
            out[k] = g[k] * facing;
        }
        sunVisible = g[3];
        open = g[4];
    }
    if (mSun > 0 && open > 0) {
        Direct(pos, normal, mSun, sunVisible * open, out);
    }
    if (primaryLight != mSun) {
        Direct(pos, normal, primaryLight, 1.0f, out);
    }
}

void MapLighting::ToBytes(const float linear[3], const float albedo[3], uint8_t out[3]) const {
    // The shoulder works on the brightest channel and scales all three alike, so a
    // fire's orange stays orange instead of washing out to white.
    float rgb[3];
    float peak = 0;
    for (int c = 0; c < 3; c++) {
        rgb[c] = std::max(0.0f, linear[c] * mOptions.exposure);
        peak = std::max(peak, rgb[c]);
    }
    float scale = peak > 1e-6f ? (1.0f - std::exp(-peak)) / peak : 1.0f;
    for (int c = 0; c < 3; c++) {
        float srgb = std::pow(rgb[c] * scale, 1.0f / 2.2f);
        out[c] = static_cast<uint8_t>(std::clamp(srgb * albedo[c] * 255.0f + 0.5f, 0.0f, 255.0f));
    }
}

void MaterialAlbedo(const std::string& material, float rgb[3]) {
    std::string name;
    for (char c : material) {
        name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // Blended terrain lists its layers in brackets: the first is the base.
    size_t open = name.find('(');
    if (open != std::string::npos) {
        size_t end = name.find_first_of(":)", open + 1);
        name = name.substr(open + 1, end == std::string::npos ? std::string::npos : end - open - 1);
    }
    struct Rule {
        const char* word;
        float r, g, b;
    };
    // First match wins, so specific words come before general ones.
    static const Rule kRules[] = {
        { "black", 0.05f, 0.05f, 0.05f },
        { "white", 0.85f, 0.85f, 0.82f },
        { "sandbag", 0.56f, 0.49f, 0.36f },
        { "chalk", 0.80f, 0.78f, 0.72f },
        { "paper", 0.78f, 0.74f, 0.62f },
        { "water", 0.22f, 0.27f, 0.20f },
        { "blood", 0.40f, 0.06f, 0.05f },
        { "rust", 0.46f, 0.30f, 0.19f },
        { "camo", 0.32f, 0.36f, 0.24f },
        { "netting", 0.32f, 0.36f, 0.24f },
        { "cloth", 0.44f, 0.40f, 0.31f },
        { "rope", 0.50f, 0.43f, 0.31f },
        { "canvas", 0.48f, 0.44f, 0.33f },
        { "tarp", 0.36f, 0.38f, 0.28f },
        { "bark", 0.33f, 0.27f, 0.21f },
        { "deadtree", 0.33f, 0.27f, 0.21f },
        { "pine", 0.18f, 0.25f, 0.16f },
        { "foliage", 0.24f, 0.31f, 0.18f },
        { "leaf", 0.24f, 0.31f, 0.18f },
        { "leaves", 0.24f, 0.31f, 0.18f },
        { "bush", 0.24f, 0.31f, 0.18f },
        { "ivy", 0.22f, 0.32f, 0.17f },
        { "vine", 0.22f, 0.32f, 0.17f },
        { "grass", 0.35f, 0.37f, 0.21f },
        { "moss", 0.30f, 0.36f, 0.20f },
        { "brick", 0.50f, 0.31f, 0.24f },
        { "asphalt", 0.30f, 0.30f, 0.29f },
        { "tile", 0.46f, 0.44f, 0.41f },
        { "reddish", 0.52f, 0.39f, 0.31f },
        { "coral", 0.40f, 0.38f, 0.34f },
        { "cliff", 0.45f, 0.42f, 0.37f },
        { "rock", 0.45f, 0.42f, 0.37f },
        { "stone", 0.50f, 0.48f, 0.44f },
        { "rubble", 0.48f, 0.46f, 0.42f },
        { "gravel", 0.45f, 0.43f, 0.39f },
        { "mud", 0.36f, 0.29f, 0.21f },
        { "dirt", 0.42f, 0.34f, 0.25f },
        { "sand", 0.60f, 0.53f, 0.40f },
        { "terrain", 0.40f, 0.34f, 0.26f },
        { "ground", 0.40f, 0.34f, 0.26f },
        { "plaster", 0.62f, 0.60f, 0.55f },
        { "concrete", 0.55f, 0.54f, 0.50f },
        { "conc", 0.55f, 0.54f, 0.50f },
        { "cement", 0.55f, 0.54f, 0.50f },
        { "bunker", 0.55f, 0.54f, 0.50f },
        { "plank", 0.47f, 0.35f, 0.23f },
        { "wood", 0.47f, 0.35f, 0.23f },
        { "crate", 0.50f, 0.39f, 0.26f },
        { "palm", 0.47f, 0.38f, 0.27f },
        { "trench", 0.45f, 0.35f, 0.24f },
        { "couch", 0.40f, 0.28f, 0.22f },
        { "chair", 0.45f, 0.34f, 0.23f },
        { "table", 0.45f, 0.34f, 0.23f },
        { "barrel", 0.36f, 0.33f, 0.27f },
        { "opel", 0.31f, 0.33f, 0.26f },
        { "truck", 0.31f, 0.33f, 0.26f },
        { "vehicle", 0.31f, 0.33f, 0.26f },
        { "generator", 0.31f, 0.33f, 0.26f },
        { "radio", 0.30f, 0.30f, 0.27f },
        { "fence", 0.45f, 0.45f, 0.44f },
        { "wire", 0.40f, 0.40f, 0.39f },
        { "barbed", 0.40f, 0.40f, 0.39f },
        { "chain", 0.45f, 0.45f, 0.44f },
        { "glass", 0.55f, 0.60f, 0.60f },
        { "bottle", 0.30f, 0.38f, 0.25f },
        { "metal", 0.42f, 0.42f, 0.41f },
        { "steel", 0.42f, 0.42f, 0.41f },
        { "iron", 0.38f, 0.37f, 0.36f },
        { "skull", 0.75f, 0.72f, 0.62f },
        // General words last.
        { "wall", 0.55f, 0.53f, 0.49f },
        { "ceiling", 0.60f, 0.58f, 0.54f },
        { "door", 0.45f, 0.35f, 0.25f },
        { "beam", 0.42f, 0.40f, 0.38f },
        { "trim", 0.48f, 0.46f, 0.43f },
        { "floor", 0.46f, 0.43f, 0.39f },
        { "bone", 0.75f, 0.72f, 0.62f },
    };
    for (const Rule& r : kRules) {
        if (name.find(r.word) != std::string::npos) {
            rgb[0] = r.r;
            rgb[1] = r.g;
            rgb[2] = r.b;
            return;
        }
    }
    rgb[0] = rgb[1] = rgb[2] = 0.5f;
}

} // namespace bomap
