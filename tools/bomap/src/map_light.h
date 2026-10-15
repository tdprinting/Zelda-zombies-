#pragma once

// Baked lighting and stand-in colours for a converted map.
//
// A Black Ops world surface is lit by three things: its lightmap's indirect light
// (the "secondary" images: two R5G6B5 layers of luminance-weighted chromaticity,
// each with a 16-bit intensity in a G16R16 image), one
// primary light (the moon, a fire, a lamp: real-time lights in the game) whose
// shadow is in the "primary" lightmap, and the material's colour map. This bakes
// the first two into vertex colours. Static models have no lightmap, so they take
// the light the world around them received, plus their own primary light.
//
// Without the .iwd textures a material has no colour map, so MaterialAlbedo
// guesses its colour from its name (concrete grey, wood brown, rust, dirt, grass...).

#include "fastfile.h"
#include "zone_walker.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bomap {

struct LightOptions {
    float exposure = 1.3f;      // overall brightness of the bake
    float indirectScale = 1.0f; // the lightmap's indirect light
    float directScale = 1.0f;   // the primary lights (moon, fires, lamps)
    float indirectSaturation = 0.5f;  // 1 keeps the lightmap's full colour, 0 makes it grey
};

class MapLighting {
public:
    // Reads the lightmaps of the GfxWorld at `worldIndex` and the primary lights of
    // the zone's ComWorld. A missing piece leaves its part of the light out.
    MapLighting(const Zone& zone, const WalkResult& walk, int worldIndex, const LightOptions& options);

    bool HasLightmap() const { return !mSecondary.empty(); }

    // Linear light at a world vertex: its lightmap coordinate and the surface's
    // primary light (0 none, 1 the sun). Also feeds the grid static models sample.
    void World(const float pos[3], const float normal[3], float lu, float lv, int primaryLight, float out[3]);
    // Call once every world vertex is in, before Prop.
    void FinishGrid();
    // Linear light at a static model's vertex.
    void Prop(const float pos[3], const float normal[3], int primaryLight, float out[3]) const;

    // Linear light times a base colour (sRGB 0..1) to an 8-bit vertex colour:
    // exposure, a soft shoulder that keeps the hue, then gamma.
    void ToBytes(const float linear[3], const float albedo[3], uint8_t out[3]) const;

    struct Light {
        int type = 0; // 1 sun, 2 spot, 3 omni
        float color[3] = {};
        float dir[3] = {};    // towards the light for the sun; a spot shines along -dir
        float origin[3] = {};
        float radius = 0;
        float cosOuter = -1, cosInner = -1;
    };
    const std::vector<Light>& Lights() const { return mLights; }

private:
    void Direct(const float pos[3], const float normal[3], int primaryLight, float visible, float out[3]) const;

    LightOptions mOptions;
    int mWidth = 0, mHeight = 0;     // one secondary layer
    std::vector<float> mSecondary;   // linear RGB, two layers summed, mWidth * mHeight * 3
    int mShadowW = 0, mShadowH = 0;
    std::vector<uint8_t> mShadow;    // primary light visibility, 0..255
    std::vector<Light> mLights;
    int mSun = -1;

    // Indirect light and sun visibility around the world, for static models.
    float mCell = 64;
    float mGridMin[3] = {};
    int mGridSize[3] = {};
    std::vector<float> mSamples; // x y z r g b sun
    std::vector<float> mGrid;    // r g b sun weight
};

// A stand-in base colour (sRGB, 0..1) for a material without its colour map, from
// its name. Blended terrain ("*34n_38n(wc/a:wc/b)") takes its first layer's.
void MaterialAlbedo(const std::string& material, float rgb[3]);

} // namespace bomap
