#pragma once

// Writes the binary files the game loads at run time (docs/ZZMODEL_FORMAT.md and
// docs/ZZMAP_FORMAT.md). Library code: the game links bomap and converts the
// player's own install on their machine.

#include "asset_export.h"
#include "fastfile.h"
#include "texture_source.h"
#include "zone_walker.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bomap {

// Black Ops (Z up) to OoT axes: (x, y, z) -> (x, z, -y). Quaternions get the same
// change of axes, so local bone transforms stay consistent with the vertices.
void ToOotAxes(const float in[3], float out[3]);
void QuatToOotAxes(const float in[4], float out[4]);

struct GameModelInfo {
    size_t bones = 0;
    size_t surfaces = 0;
    size_t verts = 0;
    size_t tris = 0;
    size_t images = 0;
    std::vector<std::string> missingImages; // colour maps whose pixels weren't found
    bool flippedWinding = false;
};

// Merges `part` (a head) onto `base` (a body) the way the game attaches models:
// bones with the same name are shared, the part's other bones join under their
// parents, and the part's vertices move into the base's bind pose. A part bone with
// no parent hangs off `tag` when one is given (a gun's j_gun under the hands'
// tag_weapon), otherwise it becomes another root.
Model AttachModel(const Model& base, const Model& part, const std::string& tag = std::string());

// A map's brush model (the "*N" an entity names: doors, barricade boards) as a model
// with one bone, in its entity's local space.
Model ReadBrushModel(const Zone& zone, const WalkResult& walk, int submodel);

// ZZMODEL1 for a model's LOD 0. `textures` may be null (then every material has image -1).
std::vector<uint8_t> WriteGameModel(const Zone& zone, const WalkResult& walk, int model, TextureSource* textures,
                                    int maxTexture, GameModelInfo* info = nullptr, const Model* attach = nullptr);
// The same for a model already read (and perhaps merged).
// Materials missing from `walk` are looked up in `otherWalk` (a part from another zone).
std::vector<uint8_t> WriteGameModel(const Model& model, const WalkResult& walk, TextureSource* textures,
                                    int maxTexture, GameModelInfo* info = nullptr,
                                    const WalkResult* otherWalk = nullptr, TextureSource* otherTextures = nullptr);

struct GameAnimInfo {
    size_t bones = 0;
    size_t bonesWithPos = 0;
    int frames = 0;
    float seconds = 0;
    float rootDistance = 0; // how far the animation carries the character (Black Ops units)
};

// ZZANIM1. Black Ops stores translations as offsets from the model's bind pose, so
// `bind` (the model the animation plays on) turns them into local positions; without
// it only rotations are written.
std::vector<uint8_t> WriteGameAnim(const Anim& anim, const Model* bind, GameAnimInfo* info = nullptr);

struct MapGeoOptions {
    float scale = 1.0f;     // OoT units per Black Ops unit
    int maxTexture = 64;    // 128 for the large option
    bool staticModels = true;
    int propVertexBudget = 600; // per static model: the first level of detail at or under it
    std::string mainDir;    // the install's main/ folder, for .iwd textures; may be empty
};

struct MapGeoInfo {
    size_t colVerts = 0, colTris = 0, wallOnlyTris = 0;
    size_t worldBrushes = 0, skippedBrushes = 0, brushTris = 0, terrainTris = 0;
    int colWeld = 0; // grid the collision was welded on to fit, 0 when it fit as is
    size_t images = 0, materials = 0, meshes = 0, verts = 0, tris = 0, staticModels = 0;
    size_t missingImages = 0;
    float boundsMin[3] = {}, boundsMax[3] = {}; // OoT coordinates after scaling
    float spawn[3] = {};
};

// ZZBOMAP1 for a map zone (needs its clipMap_t and GfxWorld).
std::vector<uint8_t> WriteMapGeo(const Zone& zone, const WalkResult& walk, const MapGeoOptions& options,
                                 MapGeoInfo* info = nullptr);

} // namespace bomap
