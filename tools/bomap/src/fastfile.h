#pragma once

// Reads a Black Ops (PC) fastfile from the player's own install: checks the
// header, inflates the zone, and reads the zone's string table and asset list.
// Nothing here knows the layout of individual assets yet; see BLACK_OPS_MAPS.md.

#include <cstdint>
#include <string>
#include <vector>

namespace bomap {

constexpr uint32_t kFastfileVersion = 473; // Black Ops PC

// Asset types the map converter cares about, by their number in the zone.
enum AssetType : uint32_t {
    kAssetImage = 0x08,
    kAssetClipMap = 0x0B,
    kAssetComWorld = 0x0D,
    kAssetMapEnts = 0x10,
    kAssetGfxWorld = 0x11,
    kAssetTypeCount = 0x2B,
};

const char* AssetTypeName(uint32_t type);

struct ZoneAsset {
    uint32_t type;
    uint32_t pointer; // 0xFFFFFFFF when the asset's data follows in the stream
};

struct Zone {
    std::string magic;
    uint32_t version = 0;
    uint32_t size = 0;         // bytes of zone data after the 36-byte header
    uint32_t externalSize = 0; // bytes streamed from outside the zone
    uint32_t blockSize[7] = {};
    std::vector<std::string> scriptStrings;
    std::vector<ZoneAsset> assets;
    size_t assetDataOffset = 0; // where the first asset's data starts in `data`
    std::vector<uint8_t> data;  // the inflated zone, starting at the XFile header
};

// Throws std::runtime_error with a readable reason on anything it can't read.
Zone ReadFastfile(const std::string& path);
Zone ParseFastfile(const std::vector<uint8_t>& file);

} // namespace bomap
