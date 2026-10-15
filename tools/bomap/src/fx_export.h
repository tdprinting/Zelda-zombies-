#pragma once

// Black Ops particle effects (FxEffectDef assets) read from a zone and written as
// ZZFX1 (docs/ZZFX_FORMAT.md), the small emitter format the game's particle system
// draws. Library code, like game_export: it runs on the player's machine on their
// own install.

#include "fastfile.h"
#include "texture_source.h"
#include "zone_walker.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bomap {

struct FxRange {
    float base = 0, amplitude = 0; // value = base + random(0..1) * amplitude
};

// One sample of an element's look over its life (the game interpolates between them).
struct FxVisSample {
    uint8_t color[4] = { 255, 255, 255, 255 }; // base RGBA
    uint8_t colorAmp[4] = {};                  // random extra (added toward the second colour)
    float rotationDelta = 0, rotationTotal = 0;
    float size[2] = {}, sizeAmp[2] = {};       // half width / half height, Black Ops units
    float scale = 0, scaleAmp = 0;
};

// One sample of an element's movement over its life.
struct FxVelSample {
    float localVel[3] = {}, localVelAmp[3] = {};
    float localDelta[3] = {}, localDeltaAmp[3] = {};
    float worldVel[3] = {}, worldVelAmp[3] = {};
    float worldDelta[3] = {}, worldDeltaAmp[3] = {};
};

struct FxElem {
    enum Category { kLooping, kOneShot, kEmission };
    Category category = kOneShot;
    uint32_t flags = 0;
    uint8_t type = 0;          // Black Ops element type (0..12, see the format doc)
    int loopInterval = 0;      // looping: ms between spawns
    int loopCount = 0;         // looping: particles alive at once at most (0 = no limit)
    int countBase = 1, countAmp = 0; // one-shot: how many
    FxRange spawnRange, fadeIn, fadeOut;
    int delayBase = 0, delayAmp = 0; // ms
    int lifeBase = 0, lifeAmp = 0;   // ms
    FxRange origin[3];
    FxRange offsetRadius, offsetHeight;
    FxRange angles[3], angularVel[3];
    FxRange initialRotation;
    FxRange gravity;            // fraction of world gravity (800 units/s^2 down)
    uint8_t atlasBehavior = 0, atlasIndex = 0, atlasFps = 0, atlasLoops = 0, atlasCols = 0, atlasRows = 0;
    uint16_t atlasEntries = 0;
    std::vector<FxVelSample> vel;
    std::vector<FxVisSample> vis;
    std::vector<std::string> materials; // sprite materials (one is picked per particle)
    std::string effect;         // runner: the effect this element spawns
    std::string model;          // model element
};

struct FxEffect {
    std::string name;
    uint32_t loopingLifeMs = 0;
    std::vector<FxElem> elems;
};

// The effect named `name` (as listed, e.g. "misc/fx_zombie_powerup_on"). False when absent.
bool ReadFx(const Zone& zone, const WalkResult& walk, const std::string& name, FxEffect& out);

// Human-readable dump for looking at what an effect holds.
std::string DescribeFx(const FxEffect& fx);

struct GameFxInfo {
    size_t emitters = 0, images = 0, fallbacks = 0, runnersInlined = 0;
    std::vector<std::string> missingImages;
};

// ZZFX1 bytes. Runner elements naming another effect of the same zone are inlined
// (one level), so the file is self contained. Images not in the zone or the .iwds
// get a procedural soft dot.
std::vector<uint8_t> WriteGameFx(const Zone& zone, const WalkResult& walk, const FxEffect& fx, TextureSource* textures,
                                 int maxTexture, GameFxInfo* info = nullptr);

} // namespace bomap
