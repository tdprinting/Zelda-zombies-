#include "texture_source.h"

#include "asset_export.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace bomap {

namespace {

uint16_t Le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | p[1] << 8 | p[2] << 16) | static_cast<uint32_t>(p[3]) << 24;
}

std::string Normalize(std::string path) {
    for (char& c : path) {
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return path;
}

bool ReadAt(std::ifstream& in, uint64_t offset, size_t size, std::vector<uint8_t>& out) {
    out.resize(size);
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    return static_cast<size_t>(in.gcount()) == size;
}

} // namespace

IwdArchives::IwdArchives(const std::string& mainDir) {
    if (mainDir.empty()) {
        return;
    }
    std::error_code ec;
    std::vector<std::string> files;
    for (const auto& e : std::filesystem::directory_iterator(mainDir, ec)) {
        std::string ext = Normalize(e.path().extension().string());
        if (e.is_regular_file(ec) && ext == ".iwd") {
            files.push_back(e.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    for (const std::string& file : files) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            continue;
        }
        in.seekg(0, std::ios::end);
        uint64_t size = static_cast<uint64_t>(in.tellg());
        // The end of central directory record is in the last 64 KB + 22 bytes.
        size_t tail = static_cast<size_t>(std::min<uint64_t>(size, 65557));
        std::vector<uint8_t> buf;
        if (!ReadAt(in, size - tail, tail, buf)) {
            continue;
        }
        size_t eocd = std::string::npos;
        for (size_t i = tail >= 22 ? tail - 22 + 1 : 0; i-- > 0;) {
            if (Le32(&buf[i]) == 0x06054b50) {
                eocd = i;
                break;
            }
        }
        if (eocd == std::string::npos) {
            continue;
        }
        uint32_t dirSize = Le32(&buf[eocd + 12]);
        uint32_t dirOffset = Le32(&buf[eocd + 16]);
        std::vector<uint8_t> dir;
        if (!ReadAt(in, dirOffset, dirSize, dir)) {
            continue;
        }
        size_t archive = mArchives.size();
        mArchives.push_back(file);
        for (size_t p = 0; p + 46 <= dir.size() && Le32(&dir[p]) == 0x02014b50;) {
            uint16_t nameLen = Le16(&dir[p + 28]), extraLen = Le16(&dir[p + 30]), commentLen = Le16(&dir[p + 32]);
            if (p + 46 + nameLen > dir.size()) {
                break;
            }
            Entry entry = { archive, Le32(&dir[p + 42]), Le32(&dir[p + 20]), Le32(&dir[p + 24]), Le16(&dir[p + 10]) };
            std::string name(reinterpret_cast<const char*>(&dir[p + 46]), nameLen);
            mEntries[Normalize(name)] = entry;
            p += 46 + nameLen + extraLen + commentLen;
        }
    }
}

bool IwdArchives::Read(const std::string& path, std::vector<uint8_t>& out) const {
    auto it = mEntries.find(Normalize(path));
    if (it == mEntries.end()) {
        return false;
    }
    const Entry& e = it->second;
    std::ifstream in(mArchives[e.archive], std::ios::binary);
    std::vector<uint8_t> header;
    if (!in || !ReadAt(in, e.localHeader, 30, header) || Le32(header.data()) != 0x04034b50) {
        return false;
    }
    uint64_t dataAt = e.localHeader + 30ull + Le16(&header[26]) + Le16(&header[28]);
    std::vector<uint8_t> packed;
    if (!ReadAt(in, dataAt, e.compressedSize, packed)) {
        return false;
    }
    if (e.method == 0) {
        out = std::move(packed);
        return true;
    }
    if (e.method != 8) {
        return false;
    }
    out.assign(e.size, 0);
    z_stream zs = {};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        return false;
    }
    zs.next_in = packed.data();
    zs.avail_in = static_cast<uInt>(packed.size());
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());
    int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    return rc == Z_STREAM_END;
}

bool DecodeIwi(const std::vector<uint8_t>& file, Texture& out) {
    // "IWi", version, format, usage, width, height, depth; the mip levels follow,
    // smallest first, so the full size image is the last thing in the file.
    if (file.size() < 12 || memcmp(file.data(), "IWi", 3) != 0) {
        return false;
    }
    uint8_t format = file[4];
    int w = Le16(&file[6]), h = Le16(&file[8]);
    if (w == 0 || h == 0) {
        return false;
    }
    size_t blocks = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4);
    size_t pixels = static_cast<size_t>(w) * h;
    uint32_t d3d;
    size_t size;
    switch (format) {
    case 0x01: d3d = 21; size = pixels * 4; break;                      // ARGB32 (BGRA in memory)
    case 0x02: d3d = 0; size = pixels * 3; break;                       // RGB24
    case 0x03: d3d = 51; size = pixels * 2; break;                      // GA16
    case 0x04: d3d = 28; size = pixels; break;                          // A8
    case 0x0B: d3d = 0x31545844; size = blocks * 8; break;              // DXT1
    case 0x0C: d3d = 0x33545844; size = blocks * 16; break;             // DXT3
    case 0x0D: d3d = 0x35545844; size = blocks * 16; break;             // DXT5
    default: return false;
    }
    if (size > file.size() - 12) {
        return false;
    }
    const uint8_t* p = file.data() + file.size() - size;
    out.width = w;
    out.height = h;
    if (d3d == 0) {
        out.rgba.resize(pixels * 4);
        for (size_t i = 0; i < pixels; i++) {
            out.rgba[i * 4] = p[i * 3 + 2];
            out.rgba[i * 4 + 1] = p[i * 3 + 1];
            out.rgba[i * 4 + 2] = p[i * 3];
            out.rgba[i * 4 + 3] = 255;
        }
        return true;
    }
    return DecodePixels(d3d, p, size, w, h, out.rgba);
}

void Downscale(Texture& tex, int maxSide) {
    auto fit = [maxSide](int side) {
        int s = 4;
        while (s * 2 <= side && s * 2 <= maxSide) {
            s *= 2;
        }
        return s;
    };
    int tw = fit(tex.width), th = fit(tex.height);
    if (tw == tex.width && th == tex.height) {
        return;
    }
    std::vector<uint8_t> out(static_cast<size_t>(tw) * th * 4);
    for (int y = 0; y < th; y++) {
        int y0 = y * tex.height / th, y1 = std::max(y0 + 1, (y + 1) * tex.height / th);
        for (int x = 0; x < tw; x++) {
            int x0 = x * tex.width / tw, x1 = std::max(x0 + 1, (x + 1) * tex.width / tw);
            uint32_t sum[4] = {};
            uint32_t n = 0;
            for (int sy = y0; sy < y1; sy++) {
                for (int sx = x0; sx < x1; sx++) {
                    const uint8_t* s = &tex.rgba[(static_cast<size_t>(sy) * tex.width + sx) * 4];
                    for (int c = 0; c < 4; c++) {
                        sum[c] += s[c];
                    }
                    n++;
                }
            }
            for (int c = 0; c < 4; c++) {
                out[(static_cast<size_t>(y) * tw + x) * 4 + c] = static_cast<uint8_t>(sum[c] / n);
            }
        }
    }
    tex.width = tw;
    tex.height = th;
    tex.rgba = std::move(out);
}

TextureSource::TextureSource(const Zone& zone, const WalkResult& walk, const std::string& mainDir)
    : mZone(zone), mWalk(walk), mIwds(mainDir) {}

bool TextureSource::Load(const std::string& imageName, int maxSide, Texture& out) {
    if (imageName.empty()) {
        return false;
    }
    out = Texture();
    out.name = imageName;
    int index = mWalk.Find(0x08, imageName);
    bool found = false;
    try {
        if (index >= 0) {
            found = DecodeZoneImage(mZone, mWalk, index, out.rgba, out.width, out.height);
        }
        if (!found && !mIwds.Empty()) {
            // A leading ',' marks an image that lives in another zone; the file has the bare name.
            std::string file = imageName[0] == ',' ? imageName.substr(1) : imageName;
            std::vector<uint8_t> iwi;
            found = mIwds.Read("images/" + file + ".iwi", iwi) && DecodeIwi(iwi, out);
        }
    } catch (const std::exception&) {
        found = false;
    }
    if (!found || out.rgba.empty()) {
        return false;
    }
    Downscale(out, maxSide);
    return true;
}

std::string MaterialColorMap(const WalkResult& walk, const std::string& material) {
    int m = walk.Find(0x06, material);
    if (m < 0) {
        return std::string();
    }
    const MaterialRecord& rec = walk.materials.at(m);
    for (const MaterialTexture& t : rec.textures) {
        if (t.semantic == 2 && !t.image.empty()) {
            return t.image;
        }
    }
    return std::string();
}

uint8_t MaterialFlags(const WalkResult& walk, const std::string& material) {
    int m = walk.Find(0x06, material);
    std::string techset = m >= 0 ? walk.materials.at(m).techset : std::string();
    if (!techset.empty() && techset[0] == ',') {
        techset = techset.substr(1);
    }
    uint8_t flags = 0;
    // Technique set names spell out the blend: wc_l_sm_t0c0... has an alpha tested
    // colour map (t), b0... blends, a0... adds; unlit decals blend over the world.
    size_t start = 0;
    while (start < techset.size()) {
        size_t end = techset.find('_', start);
        std::string token = techset.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (token.size() >= 2 && std::isdigit(static_cast<unsigned char>(token[1]))) {
            if (token[0] == 't') {
                flags |= 1 | 4;
            } else if (token[0] == 'b' || token[0] == 'a') {
                flags |= 2;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    if (techset.find("decal") != std::string::npos) {
        flags |= 2;
    }
    std::string lower = Normalize(material);
    for (const char* twoSided : { "foliage", "leaf", "leaves", "grass", "fence", "wire", "net", "cloth", "vine" }) {
        if (lower.find(twoSided) != std::string::npos) {
            flags |= 4;
        }
    }
    return flags;
}

} // namespace bomap
