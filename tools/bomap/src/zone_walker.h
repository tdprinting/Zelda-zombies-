#pragma once

// Walks every asset in a zone in stream order, the way the game loads it, so each
// asset's name and place in the file is known, and remembers where the parts the
// exporters need (model surfaces, animation data, textures, sounds) ended up.

#include "fastfile.h"
#include "zone_stream.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bomap {

struct WalkedAsset {
    uint32_t type = 0;
    std::string name;
    size_t header = kNoData; // file offset of the asset's main structure
    size_t start = 0;        // file range the asset's data covers
    size_t end = 0;
    bool listed = false;     // in the zone's asset list (not only loaded inside another asset)
    int parent = -1;         // the listed asset it was loaded inside, for nested ones
    uint32_t aliasToken = 0; // block-4 slot later assets use to refer to this one
};

struct SurfaceRecord {
    size_t header = kNoData;     // the 68-byte XSurface
    size_t verts = kNoData;      // GfxPackedVertex[vertCount]
    size_t triIndices = kNoData; // uint16[3 * triCount]
    size_t blend = kNoData;      // uint16 blend info
    size_t vertList = kNoData;   // XRigidVertList[vertListCount]
};

struct ModelRecord {
    size_t boneNames = kNoData;
    size_t parentList = kNoData;
    size_t quats = kNoData;
    size_t trans = kNoData;
    size_t baseMat = kNoData;
    std::vector<SurfaceRecord> surfaces;
    std::vector<std::string> materials; // per surface
};

struct AnimRecord {
    size_t names = kNoData;
    size_t notify = kNoData;
    size_t dataByte = kNoData;
    size_t dataShort = kNoData;
    size_t dataInt = kNoData;
    size_t randomDataShort = kNoData;
    size_t randomDataByte = kNoData;
    size_t randomDataInt = kNoData;
    size_t indices = kNoData;
    size_t deltaTrans = kNoData; // XAnimPartTrans header (4 bytes), data follows it
    size_t deltaTransFrames = kNoData;
    size_t deltaTransIndices = kNoData;
    size_t deltaTransData = kNoData;
    size_t deltaQuat = kNoData;
    size_t deltaQuatIndices = kNoData;
    size_t deltaQuatData = kNoData;
};

struct MaterialTexture {
    uint32_t nameHash = 0;
    uint8_t semantic = 0;
    std::string image;
};

struct MaterialRecord {
    std::string techset;
    std::vector<MaterialTexture> textures;
};

struct ImageRecord {
    size_t loadDef = kNoData; // GfxImageLoadDef header, pixel data follows it
    uint32_t format = 0;
    uint32_t resourceSize = 0;
};

struct SoundAliasRecord {
    std::string name;
    std::string file;   // streamed file name, or the loaded sound's name
    size_t alias = kNoData; // the alias's 84-byte snd_alias_t (volume, pitch, distances, flags)
    bool loaded = false; // the sound's data is inside the zone
    size_t asset = kNoData; // LoadedSound: snd_asset header (56 bytes)
    size_t data = kNoData;  // LoadedSound: the sample data
    uint32_t dataSize = 0;
    bool shared = false; // the same sound file as an earlier alias
};

struct SoundBankRecord {
    std::vector<SoundAliasRecord> aliases;
};

struct WalkResult {
    std::vector<WalkedAsset> assets;
    bool complete = false;
    std::string error;      // why the walk stopped, if it didn't finish
    size_t stoppedAt = 0;   // file offset where it stopped
    int stoppedAsset = -1;  // index in the zone's asset list it was reading
    std::map<int, ModelRecord> models;
    std::map<int, AnimRecord> anims;
    std::map<int, MaterialRecord> materials;
    std::map<int, ImageRecord> images;
    std::map<int, SoundBankRecord> sounds;

    // Index of the asset of this type and name, or -1.
    int Find(uint32_t type, const std::string& name) const;

    // Following pointers after the walk. `field` is the file offset of a pointer
    // field inside loaded data. Pointer() gives the file offset of the data it points
    // at (loaded in place, or earlier data it refers to), or kNoData.
    size_t Pointer(size_t field) const;
    // The asset an asset-pointer field names, or -1.
    int AssetAt(size_t field) const;

    std::unordered_map<size_t, size_t> loadedAt; // pointer field -> where its data was loaded
    std::unordered_map<size_t, int> assetAt;     // asset pointer field -> asset index
    std::shared_ptr<const ZoneStream> stream;    // resolves references; uses the zone's data
};

// Walks all assets. Never throws: on an error it records where it stopped. The
// result refers to `zone`'s data, so the zone must outlive it.
WalkResult WalkZone(const Zone& zone);

} // namespace bomap
