#include "fastfile.h"

#include <zlib.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace bomap {

static constexpr uint32_t kInline = 0xFFFFFFFF;
static constexpr size_t kXFileSize = 36;
static constexpr size_t kAssetListSize = 16;

const char* AssetTypeName(uint32_t type) {
    static const char* names[] = {
        "xmodelpieces",
        "physpreset",
        "physconstraints",
        "destructibledef",
        "xanim",
        "xmodel",
        "material",
        "techset",
        "image",
        "sound",
        "soundpatch",
        "clipmap",
        "clipmap_pvs",
        "comworld",
        "gameworld_sp",
        "gameworld_mp",
        "map_ents",
        "gfxworld",
        "lightdef",
        "uimap",
        "font",
        "menulist",
        "menu",
        "localize",
        "weapon",
        "weapondef",
        "weaponvariant",
        "snddriverglobals",
        "fx",
        "impactfx",
        "aitype",
        "mptype",
        "mpbody",
        "mphead",
        "character",
        "xmodelalias",
        "rawfile",
        "stringtable",
        "packindex",
        "xglobals",
        "ddl",
        "glasses",
        "emblemset",
    };
    return type < sizeof(names) / sizeof(names[0]) ? names[type] : "unknown";
}

static uint32_t ReadU32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("zone ends early at byte " + std::to_string(offset));
    }
    return static_cast<uint32_t>(data[offset]) | static_cast<uint32_t>(data[offset + 1]) << 8 |
           static_cast<uint32_t>(data[offset + 2]) << 16 | static_cast<uint32_t>(data[offset + 3]) << 24;
}

static std::vector<uint8_t> Inflate(const uint8_t* in, size_t inSize) {
    z_stream stream = {};
    if (inflateInit(&stream) != Z_OK) {
        throw std::runtime_error("zlib could not start");
    }
    std::vector<uint8_t> out;
    std::vector<uint8_t> chunk(1 << 20);
    stream.next_in = const_cast<Bytef*>(in);
    stream.avail_in = static_cast<uInt>(inSize);
    int result;
    do {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&stream);
            throw std::runtime_error(std::string("zone data is not valid zlib (") +
                                     (stream.msg != nullptr ? stream.msg : "no message") + ")");
        }
        out.insert(out.end(), chunk.data(), chunk.data() + (chunk.size() - stream.avail_out));
    } while (result != Z_STREAM_END && stream.avail_in > 0);
    inflateEnd(&stream);
    return out;
}

Zone ParseFastfile(const std::vector<uint8_t>& file) {
    if (file.size() < 12) {
        throw std::runtime_error("file is too small to be a fastfile");
    }
    Zone zone;
    zone.magic.assign(reinterpret_cast<const char*>(file.data()), 8);
    if (zone.magic == "IWff0100") {
        throw std::runtime_error("this fastfile is signed (IWff0100); only unsigned ones (IWffu100) are supported");
    }
    if (zone.magic != "IWffu100") {
        throw std::runtime_error("not a Black Ops fastfile (magic is not IWffu100)");
    }
    zone.version = ReadU32(file, 8);
    if (zone.version != kFastfileVersion) {
        throw std::runtime_error("fastfile version " + std::to_string(zone.version) + ", expected " +
                                 std::to_string(kFastfileVersion) + " (Black Ops PC)");
    }

    zone.data = Inflate(file.data() + 12, file.size() - 12);
    const std::vector<uint8_t>& d = zone.data;
    zone.size = ReadU32(d, 0);
    zone.externalSize = ReadU32(d, 4);
    for (int i = 0; i < 7; i++) {
        zone.blockSize[i] = ReadU32(d, 8 + i * 4);
    }

    // XAssetList: { scriptStringCount, scriptStrings*, assetCount, assets* }, then the
    // data its pointers refer to, in order. The stream has no padding.
    size_t pos = kXFileSize;
    uint32_t stringCount = ReadU32(d, pos);
    uint32_t stringsPtr = ReadU32(d, pos + 4);
    uint32_t assetCount = ReadU32(d, pos + 8);
    uint32_t assetsPtr = ReadU32(d, pos + 12);
    pos += kAssetListSize;

    if (stringCount > 1000000 || assetCount > 1000000) {
        throw std::runtime_error("asset list header looks wrong (" + std::to_string(stringCount) + " strings, " +
                                 std::to_string(assetCount) + " assets)");
    }

    if (stringsPtr != 0) {
        std::vector<uint32_t> ptrs(stringCount);
        for (uint32_t i = 0; i < stringCount; i++) {
            ptrs[i] = ReadU32(d, pos);
            pos += 4;
        }
        for (uint32_t i = 0; i < stringCount; i++) {
            if (ptrs[i] != kInline) {
                zone.scriptStrings.emplace_back();
                continue;
            }
            size_t end = pos;
            while (end < d.size() && d[end] != 0) {
                end++;
            }
            if (end >= d.size()) {
                throw std::runtime_error("script string " + std::to_string(i) + " runs off the end of the zone");
            }
            zone.scriptStrings.emplace_back(reinterpret_cast<const char*>(&d[pos]), end - pos);
            pos = end + 1;
        }
    }

    if (assetsPtr != 0) {
        for (uint32_t i = 0; i < assetCount; i++) {
            zone.assets.push_back({ ReadU32(d, pos), ReadU32(d, pos + 4) });
            pos += 8;
        }
    }
    zone.assetDataOffset = pos;
    return zone;
}

Zone ReadFastfile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("can't open " + path);
    }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ParseFastfile(file);
}

} // namespace bomap
