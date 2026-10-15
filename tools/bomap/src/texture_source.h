#pragma once

// Finds the pixels of a Black Ops image: in the zone when the zone carries them
// (lightmaps, reflection probes), otherwise in the install's main/*.iwd archives,
// which are zip files holding images/<name>.iwi. Results are scaled down to the
// small power-of-two sizes the game draws with.

#include "fastfile.h"
#include "zone_walker.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace bomap {

struct Texture {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// A read-only view of the .iwd archives in one folder (the install's main/).
class IwdArchives {
public:
    // Indexes every *.iwd in `mainDir`. A later archive (by name) wins, as in the game.
    explicit IwdArchives(const std::string& mainDir);
    bool Empty() const { return mEntries.empty(); }
    size_t ArchiveCount() const { return mArchives.size(); }
    // The uncompressed contents of `path` (case-insensitive, '/' separators), or false.
    bool Read(const std::string& path, std::vector<uint8_t>& out) const;

private:
    struct Entry {
        size_t archive;
        uint32_t localHeader;
        uint32_t compressedSize;
        uint32_t size;
        uint16_t method;
    };
    std::vector<std::string> mArchives;
    std::map<std::string, Entry> mEntries;
};

// Decodes the largest mip level of an .iwi file into RGBA8. False if it can't.
bool DecodeIwi(const std::vector<uint8_t>& file, Texture& out);

// Box-filters `tex` down to power-of-two sides no larger than `maxSide` (and at least 4).
void Downscale(Texture& tex, int maxSide);

class TextureSource {
public:
    // `mainDir` may be empty: then only images carried by the zone are found.
    TextureSource(const Zone& zone, const WalkResult& walk, const std::string& mainDir);
    // The image scaled to fit `maxSide`; false when its pixels can't be found.
    bool Load(const std::string& imageName, int maxSide, Texture& out);
    size_t ArchiveCount() const { return mIwds.ArchiveCount(); }

private:
    const Zone& mZone;
    const WalkResult& mWalk;
    IwdArchives mIwds;
};

// The colour map an artist would call "the texture" of a material, or "".
std::string MaterialColorMap(const WalkResult& walk, const std::string& material);
// ZZ material flags from a material's technique set: 1 alpha test, 2 translucent, 4 two-sided.
uint8_t MaterialFlags(const WalkResult& walk, const std::string& material);

} // namespace bomap
