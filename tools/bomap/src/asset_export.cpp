#include "asset_export.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace bomap {

namespace {

// ---------------------------------------------------------------- reading

struct Reader {
    const std::vector<uint8_t>& d;
    uint8_t U8(size_t at) const { return at < d.size() ? d[at] : 0; }
    uint16_t U16(size_t at) const { return at + 2 <= d.size() ? static_cast<uint16_t>(d[at] | d[at + 1] << 8) : 0; }
    int16_t S16(size_t at) const { return static_cast<int16_t>(U16(at)); }
    uint32_t U32(size_t at) const {
        return at + 4 <= d.size() ? static_cast<uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 |
                                                          static_cast<uint32_t>(d[at + 3]) << 24)
                                  : 0;
    }
    float F32(size_t at) const {
        uint32_t b = U32(at);
        float f;
        memcpy(&f, &b, 4);
        return f;
    }
};

std::string ScriptString(const Zone& zone, uint16_t index) {
    return index < zone.scriptStrings.size() ? zone.scriptStrings[index] : "#" + std::to_string(index);
}

// ---------------------------------------------------------------- JSON

std::string Num(double v) {
    if (!std::isfinite(v)) {
        return "0";
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

template <typename T, typename F>
std::string List(const std::vector<T>& items, F format) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); i++) {
        if (i > 0) {
            out += ",";
        }
        out += format(items[i]);
    }
    return out + "]";
}

std::string Floats(const float* v, int n) {
    std::string out = "[";
    for (int i = 0; i < n; i++) {
        out += (i ? "," : "") + Num(v[i]);
    }
    return out + "]";
}

void Normalize4(float* q) {
    float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 0) {
        for (int i = 0; i < 4; i++) {
            q[i] /= len;
        }
    } else {
        q[0] = q[1] = q[2] = 0;
        q[3] = 1;
    }
}

// ---------------------------------------------------------------- models

void UnpackUnitVec(uint32_t packed, float* out) {
    float scale = ((packed >> 24) + 192.0f) / 32385.0f;
    for (int i = 0; i < 3; i++) {
        out[i] = ((packed >> (i * 8) & 0xFF) - 127.0f) * scale;
    }
}

const WalkedAsset& CheckAsset(const WalkResult& walk, int asset, uint32_t type) {
    if (asset < 0 || asset >= static_cast<int>(walk.assets.size()) || walk.assets[asset].type != type ||
        walk.assets[asset].header == kNoData) {
        throw std::runtime_error("not a loaded asset of the expected type");
    }
    return walk.assets[asset];
}

} // namespace

Model ReadModel(const Zone& zone, const WalkResult& walk, int asset, int lod) {
    const WalkedAsset& a = CheckAsset(walk, asset, 0x05);
    const ModelRecord& rec = walk.models.at(asset);
    Reader r{ zone.data };
    size_t h = a.header;
    Model m;
    m.name = a.name;
    int numBones = r.U8(h + 4);
    int numRoot = r.U8(h + 5);
    m.bones.resize(numBones);
    for (int i = 0; i < numBones; i++) {
        Bone& b = m.bones[i];
        b.name = rec.boneNames != kNoData ? ScriptString(zone, r.U16(rec.boneNames + i * 2)) : "bone" + std::to_string(i);
        if (i >= numRoot) {
            int k = i - numRoot;
            if (rec.parentList != kNoData) {
                b.parent = i - r.U8(rec.parentList + k);
            }
            if (rec.quats != kNoData) {
                for (int c = 0; c < 4; c++) {
                    b.localQuat[c] = r.S16(rec.quats + k * 8 + c * 2) / 32767.0f;
                }
                Normalize4(b.localQuat);
            }
            if (rec.trans != kNoData) {
                for (int c = 0; c < 3; c++) {
                    b.localTrans[c] = r.F32(rec.trans + k * 12 + c * 4); // packed float3s (the block reserves 16 per bone)
                }
            }
        }
        if (rec.baseMat != kNoData) {
            size_t mat = rec.baseMat + i * 32;
            for (int c = 0; c < 4; c++) {
                b.worldQuat[c] = r.F32(mat + c * 4);
            }
            Normalize4(b.worldQuat);
            for (int c = 0; c < 3; c++) {
                b.worldTrans[c] = r.F32(mat + 16 + c * 4);
            }
        }
    }
    // The local transforms are rebuilt from the model space bind pose (local =
    // inverse(parent world) * world): they agree with the stored ones to 1e-4, and
    // some models (the view hands) carry zeroed translations that only the bind
    // pose gets right.
    for (Bone& b : m.bones) {
        memcpy(b.animBase, b.localTrans, sizeof(b.animBase));
    }
    bool zeroTrans = true;
    for (int i = numRoot; i < numBones && zeroTrans; i++) {
        const float* t = m.bones[i].localTrans;
        zeroTrans = t[0] == 0 && t[1] == 0 && t[2] == 0;
    }
    if ((rec.trans == kNoData || rec.quats == kNoData || zeroTrans) && rec.baseMat != kNoData) {
        for (int i = numRoot; i < numBones; i++) {
            Bone& b = m.bones[i];
            if (b.parent < 0 || b.parent >= numBones) {
                continue;
            }
            const Bone& p = m.bones[b.parent];
            float inv[4] = { -p.worldQuat[0], -p.worldQuat[1], -p.worldQuat[2], p.worldQuat[3] };
            float d[3] = { b.worldTrans[0] - p.worldTrans[0], b.worldTrans[1] - p.worldTrans[1],
                           b.worldTrans[2] - p.worldTrans[2] };
            // Rotate d by inv: v' = v + 2w(u x v) + 2u x (u x v), with u the vector part.
            float u[3] = { inv[0], inv[1], inv[2] }, w = inv[3];
            float c1[3] = { u[1] * d[2] - u[2] * d[1], u[2] * d[0] - u[0] * d[2], u[0] * d[1] - u[1] * d[0] };
            float c2[3] = { u[1] * c1[2] - u[2] * c1[1], u[2] * c1[0] - u[0] * c1[2], u[0] * c1[1] - u[1] * c1[0] };
            if (rec.trans == kNoData || zeroTrans) {
                for (int c = 0; c < 3; c++) {
                    b.localTrans[c] = d[c] + 2 * w * c1[c] + 2 * c2[c];
                }
            }
            if (rec.quats == kNoData) {
                const float* q = b.worldQuat;
                float lq[4] = { inv[3] * q[0] + inv[0] * q[3] + inv[1] * q[2] - inv[2] * q[1],
                                inv[3] * q[1] - inv[0] * q[2] + inv[1] * q[3] + inv[2] * q[0],
                                inv[3] * q[2] + inv[0] * q[1] - inv[1] * q[0] + inv[2] * q[3],
                                inv[3] * q[3] - inv[0] * q[0] - inv[1] * q[1] - inv[2] * q[2] };
                Normalize4(lq);
                memcpy(b.localQuat, lq, sizeof(lq));
            }
        }
    }
    int lods = std::clamp(static_cast<int>(static_cast<int16_t>(r.U16(h + 216))), 1, 4);
    lod = std::clamp(lod, 0, lods - 1);
    int lodSurfs = r.U16(h + 40 + lod * 32 + 4);
    int firstSurf = r.U16(h + 40 + lod * 32 + 6);
    for (int si = firstSurf; si < firstSurf + lodSurfs && si < static_cast<int>(rec.surfaces.size()); si++) {
        const SurfaceRecord& sr = rec.surfaces[si];
        Surface s;
        s.material = si < static_cast<int>(rec.materials.size()) ? rec.materials[si] : std::string();
        size_t sh = sr.header;
        int vertCount = r.U16(sh + 4);
        int triCount = r.U16(sh + 6);
        int vertListCount = r.U8(sh + 1);
        s.verts.resize(vertCount);
        for (int v = 0; v < vertCount && sr.verts != kNoData; v++) {
            size_t pv = sr.verts + v * 32;
            Vertex& vx = s.verts[v];
            for (int c = 0; c < 3; c++) {
                vx.pos[c] = r.F32(pv + c * 4);
            }
            UnpackUnitVec(r.U32(pv + 24), vx.normal);
            uint32_t tc = r.U32(pv + 20);
            vx.uv[0] = HalfToFloat(static_cast<uint16_t>(tc >> 16));
            vx.uv[1] = HalfToFloat(static_cast<uint16_t>(tc & 0xFFFF));
        }
        if (vertListCount > 0 && sr.vertList != kNoData) {
            int v = 0;
            for (int l = 0; l < vertListCount; l++) {
                size_t list = sr.vertList + l * 12;
                int bone = r.U16(list) / 64;
                int count = r.U16(list + 2);
                for (int k = 0; k < count && v < vertCount; k++, v++) {
                    s.verts[v].bones[0] = bone;
                }
            }
        } else if (sr.blend != kNoData) {
            size_t p = sr.blend;
            int v = 0;
            for (int section = 0; section < 4; section++) {
                int count = r.U16(sh + 16 + section * 2);
                for (int k = 0; k < count && v < vertCount; k++, v++) {
                    Vertex& vx = s.verts[v];
                    vx.bones[0] = r.U16(p) / 64;
                    p += 2;
                    float rest = 1.0f;
                    for (int extra = 1; extra <= section; extra++) {
                        vx.bones[extra] = r.U16(p) / 64;
                        vx.weights[extra] = r.U16(p + 2) / 65536.0f;
                        rest -= vx.weights[extra];
                        p += 4;
                    }
                    vx.weights[0] = rest;
                }
            }
        }
        if (sr.triIndices != kNoData) {
            s.tris.resize(triCount * 3);
            for (int i = 0; i < triCount * 3; i++) {
                s.tris[i] = r.U16(sr.triIndices + i * 2);
            }
        }
        m.surfaces.push_back(std::move(s));
    }
    return m;
}

// ---------------------------------------------------------------- animations

float HalfToFloat(uint16_t h) {
    if (h == 0) {
        return 0.0f;
    }
    uint32_t x = static_cast<uint32_t>(h) << 14;
    uint32_t bits = ((static_cast<uint32_t>(h) << 16) & 0x80000000u) |
                    ((((x & 0x0FFFC000u) - (~x & 0x10000000u)) ^ 0x80000000u) >> 1);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

std::string ModelToObj(const Zone& zone, const WalkResult& walk, int asset) {
    Model m = ReadModel(zone, walk, asset);
    std::ostringstream out;
    out << "# " << m.name << ", LOD 0, exported by bomap (game units, Z up)\n";
    size_t base = 1;
    for (size_t si = 0; si < m.surfaces.size(); si++) {
        const Surface& s = m.surfaces[si];
        out << "o surf" << si << "\nusemtl " << (s.material.empty() ? "none" : s.material) << "\n";
        for (const Vertex& v : s.verts) {
            out << "v " << Num(v.pos[0]) << " " << Num(v.pos[1]) << " " << Num(v.pos[2]) << "\n";
        }
        for (const Vertex& v : s.verts) {
            out << "vt " << Num(v.uv[0]) << " " << Num(1.0f - v.uv[1]) << "\n";
        }
        for (const Vertex& v : s.verts) {
            out << "vn " << Num(v.normal[0]) << " " << Num(v.normal[1]) << " " << Num(v.normal[2]) << "\n";
        }
        for (size_t t = 0; t + 2 < s.tris.size(); t += 3) {
            out << "f";
            // The game winds triangles the other way round from OBJ's convention.
            for (int k : { 2, 1, 0 }) {
                size_t i = base + s.tris[t + k];
                out << " " << i << "/" << i << "/" << i;
            }
            out << "\n";
        }
        base += s.verts.size();
    }
    return out.str();
}

std::string ModelToJson(const Zone& zone, const WalkResult& walk, int asset) {
    Model m = ReadModel(zone, walk, asset);
    std::ostringstream out;
    out << "{\n\"name\": " << Quote(m.name) << ",\n\"units\": \"game units (inches), Z up\",\n";
    out << "\"bones\": [\n";
    for (size_t i = 0; i < m.bones.size(); i++) {
        const Bone& b = m.bones[i];
        out << " {\"name\": " << Quote(b.name) << ", \"parent\": " << b.parent
            << ", \"localQuat\": " << Floats(b.localQuat, 4) << ", \"localTrans\": " << Floats(b.localTrans, 3)
            << ", \"animBase\": " << Floats(b.animBase, 3)
            << ", \"worldQuat\": " << Floats(b.worldQuat, 4) << ", \"worldTrans\": " << Floats(b.worldTrans, 3) << "}"
            << (i + 1 < m.bones.size() ? ",\n" : "\n");
    }
    out << "],\n\"surfaces\": [\n";
    for (size_t si = 0; si < m.surfaces.size(); si++) {
        const Surface& s = m.surfaces[si];
        out << " {\"material\": " << Quote(s.material) << ",\n";
        out << "  \"positions\": " << List(s.verts, [](const Vertex& v) { return Floats(v.pos, 3); }) << ",\n";
        out << "  \"normals\": " << List(s.verts, [](const Vertex& v) { return Floats(v.normal, 3); }) << ",\n";
        out << "  \"uvs\": " << List(s.verts, [](const Vertex& v) { return Floats(v.uv, 2); }) << ",\n";
        out << "  \"bones\": " << List(s.verts, [](const Vertex& v) {
            return "[" + std::to_string(v.bones[0]) + "," + std::to_string(v.bones[1]) + "," +
                   std::to_string(v.bones[2]) + "," + std::to_string(v.bones[3]) + "]";
        }) << ",\n";
        out << "  \"weights\": " << List(s.verts, [](const Vertex& v) { return Floats(v.weights, 4); }) << ",\n";
        out << "  \"triangles\": " << List(s.tris, [](uint16_t i) { return std::to_string(i); }) << "}"
            << (si + 1 < m.surfaces.size() ? ",\n" : "\n");
    }
    out << "]\n}\n";
    return out.str();
}

Anim ReadAnim(const Zone& zone, const WalkResult& walk, int asset) {
    const WalkedAsset& a = CheckAsset(walk, asset, 0x04);
    const AnimRecord& rec = walk.anims.at(asset);
    Reader r{ zone.data };
    size_t h = a.header;
    int numFrames = r.U16(h + 14);
    bool bigFrames = numFrames >= 256;
    int counts[10];
    for (int i = 0; i < 10; i++) {
        counts[i] = r.U8(h + 24 + i);
    }
    Anim anim;
    anim.name = a.name;
    anim.numFrames = numFrames;
    anim.framerate = r.F32(h + 48);
    anim.looping = r.U8(h + 16) != 0;
    anim.delta = r.U8(h + 17) != 0;
    std::vector<AnimBone>& bones = anim.bones;
    bones.resize(counts[9]);
    for (int i = 0; i < counts[9]; i++) {
        bones[i].name = rec.names != kNoData ? ScriptString(zone, r.U16(rec.names + i * 2)) : "";
    }

    size_t dataByte = rec.dataByte, dataShort = rec.dataShort, dataInt = rec.dataInt;
    size_t randomShort = rec.randomDataShort, randomByte = rec.randomDataByte, indices = rec.indices;
    auto takeByte = [&]() { return r.U8(dataByte++); };
    auto takeShort = [&]() { uint16_t v = r.U16(dataShort); dataShort += 2; return v; };
    // Frame numbers of an animated track's keys, in the order the game reads them.
    auto keyFrames = [&](int tableSize) {
        std::vector<int> frames(tableSize + 1);
        if (!bigFrames) {
            for (int k = 0; k <= tableSize; k++) {
                frames[k] = takeByte();
            }
        } else if (tableSize < 64) {
            for (int k = 0; k <= tableSize; k++) {
                frames[k] = takeShort();
            }
        } else {
            dataShort += 2 * (((tableSize - 1) >> 8) + 2); // coarse lookup table
            for (int k = 0; k <= tableSize; k++) {
                frames[k] = r.U16(indices + k * 2);
            }
            indices += 2 * (tableSize + 1);
        }
        return frames;
    };

    int part = 0;
    for (; part < counts[0]; part++) {
        bones[part].rot.frames = { 0 };
        bones[part].rot.keys = { { 0, 0, 0, 1 } };
    }
    for (int cat = 1; cat <= 4; cat++) {
        for (int n = 0; n < counts[cat] && part < counts[9]; n++, part++) {
            Track& t = bones[part].rot;
            bool full = cat == 2 || cat == 4;
            int comps = full ? 4 : 2;
            if (cat <= 2) {
                int tableSize = takeShort();
                t.frames = keyFrames(tableSize);
                for (int k = 0; k <= tableSize; k++) {
                    float q[4] = { 0, 0, 0, 0 };
                    for (int c = 0; c < comps; c++) {
                        q[4 - comps + c] = r.S16(randomShort + c * 2) / 32767.0f;
                    }
                    randomShort += comps * 2;
                    Normalize4(q);
                    t.keys.push_back({ q[0], q[1], q[2], q[3] });
                }
            } else {
                float q[4] = { 0, 0, 0, 0 };
                for (int c = 0; c < comps; c++) {
                    q[4 - comps + c] = static_cast<int16_t>(takeShort()) / 32767.0f;
                }
                Normalize4(q);
                t.frames = { 0 };
                t.keys = { { q[0], q[1], q[2], q[3] } };
            }
        }
    }
    for (int cat = 5; cat <= 8; cat++) {
        for (int n = 0; n < counts[cat]; n++) {
            int bone = takeByte();
            Track scratch;
            Track& t = bone < static_cast<int>(bones.size()) ? bones[bone].trans : scratch;
            if (cat <= 6) {
                int tableSize = takeShort();
                t.frames = keyFrames(tableSize);
                float mins[3], size[3];
                for (int c = 0; c < 3; c++) {
                    mins[c] = r.F32(dataInt + c * 4);
                    size[c] = r.F32(dataInt + 12 + c * 4);
                }
                dataInt += 24;
                for (int k = 0; k <= tableSize; k++) {
                    std::vector<float> p(3);
                    for (int c = 0; c < 3; c++) {
                        float q = cat == 5 ? r.U8(randomByte++) : r.U16(randomShort + c * 2);
                        p[c] = mins[c] + size[c] * q;
                    }
                    if (cat == 6) {
                        randomShort += 6;
                    }
                    t.keys.push_back(p);
                }
            } else if (cat == 7) {
                t.frames = { 0 };
                t.keys = { { r.F32(dataInt), r.F32(dataInt + 4), r.F32(dataInt + 8) } };
                dataInt += 12;
            } else {
                t.frames = { 0 };
                t.keys = { { 0, 0, 0 } };
            }
        }
    }

    int notifyCount = r.U8(h + 34);
    for (int i = 0; i < notifyCount && rec.notify != kNoData; i++) {
        anim.notetracks.push_back({ ScriptString(zone, r.U16(rec.notify + i * 8)), r.F32(rec.notify + i * 8 + 4) });
    }
    // Root motion: how far the whole character moves and turns over the animation.
    if (rec.deltaTrans != kNoData) {
        uint16_t size = r.U16(rec.deltaTrans);
        Track& t = anim.rootTrans;
        if (size == 0) {
            t.frames = { 0 };
            t.keys = { { r.F32(rec.deltaTransData), r.F32(rec.deltaTransData + 4), r.F32(rec.deltaTransData + 8) } };
        } else {
            bool small = r.U8(rec.deltaTrans + 2) != 0;
            size_t f = rec.deltaTransFrames;
            for (int k = 0; k <= size; k++) {
                t.frames.push_back(bigFrames ? r.U16(rec.deltaTransIndices + k * 2) : r.U8(rec.deltaTransIndices + k));
                std::vector<float> p(3);
                for (int c = 0; c < 3; c++) {
                    float q = small ? r.U8(rec.deltaTransData + k * 3 + c) : r.U16(rec.deltaTransData + k * 6 + c * 2);
                    p[c] = r.F32(f + c * 4) + r.F32(f + 12 + c * 4) * q;
                }
                t.keys.push_back(p);
            }
        }
    }
    if (rec.deltaQuat != kNoData) {
        uint16_t size = r.U16(rec.deltaQuat);
        Track& t = anim.rootQuat;
        for (int k = 0; k <= size; k++) {
            t.frames.push_back(size == 0 ? 0 : bigFrames ? r.U16(rec.deltaQuatIndices + k * 2) : r.U8(rec.deltaQuatIndices + k));
            float q[4] = { 0, 0, r.S16(rec.deltaQuatData + k * 4) / 32767.0f, r.S16(rec.deltaQuatData + k * 4 + 2) / 32767.0f };
            Normalize4(q);
            t.keys.push_back({ q[0], q[1], q[2], q[3] });
        }
    }
    return anim;
}

std::string AnimToJson(const Zone& zone, const WalkResult& walk, int asset) {
    Anim anim = ReadAnim(zone, walk, asset);
    auto track = [](const Track& t) {
        return "{\"frames\": " + List(t.frames, [](int f) { return std::to_string(f); }) +
               ", \"keys\": " + List(t.keys, [](const std::vector<float>& k) { return Floats(k.data(), static_cast<int>(k.size())); }) + "}";
    };
    std::ostringstream out;
    out << "{\n\"name\": " << Quote(anim.name) << ",\n";
    out << "\"frameCount\": " << anim.numFrames << ",\n";
    out << "\"framerate\": " << Num(anim.framerate) << ",\n";
    out << "\"looping\": " << (anim.looping ? "true" : "false") << ",\n";
    out << "\"delta\": " << (anim.delta ? "true" : "false") << ",\n";
    out << "\"note\": \"frames run 0..frameCount at framerate per second. rotation keys are local (parent-space) "
           "quaternions [x,y,z,w] that replace the bind rotation; translation keys are offsets in game units added to "
           "the model's animBase translation (bones without one keep localTrans). rootMotion is the whole character's "
           "movement over the animation\",\n";
    out << "\"notetracks\": [";
    for (size_t i = 0; i < anim.notetracks.size(); i++) {
        out << (i ? ", " : "") << "{\"name\": " << Quote(anim.notetracks[i].first) << ", \"time\": " << Num(anim.notetracks[i].second) << "}";
    }
    out << "],\n\"rootMotion\": {";
    if (!anim.rootTrans.keys.empty()) {
        out << "\"trans\": " << track(anim.rootTrans);
    }
    if (!anim.rootQuat.keys.empty()) {
        out << (!anim.rootTrans.keys.empty() ? ", " : "") << "\"quat\": " << track(anim.rootQuat);
    }
    out << "},\n\"bones\": [\n";
    const std::vector<AnimBone>& bones = anim.bones;
    for (size_t i = 0; i < bones.size(); i++) {
        out << " {\"name\": " << Quote(bones[i].name) << ", \"rotation\": " << track(bones[i].rot);
        if (!bones[i].trans.keys.empty()) {
            out << ", \"translation\": " << track(bones[i].trans);
        }
        out << "}" << (i + 1 < bones.size() ? ",\n" : "\n");
    }
    out << "]\n}\n";
    return out.str();
}

// ---------------------------------------------------------------- images

namespace {

constexpr uint32_t FourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b) << 8 | static_cast<uint32_t>(c) << 16 |
           static_cast<uint32_t>(d) << 24;
}

std::string FormatName(uint32_t f) {
    switch (f) {
    case FourCC('D', 'X', 'T', '1'): return "DXT1";
    case FourCC('D', 'X', 'T', '3'): return "DXT3";
    case FourCC('D', 'X', 'T', '5'): return "DXT5";
    case 21: return "A8R8G8B8";
    case 22: return "X8R8G8B8";
    case 23: return "R5G6B5";
    case 34: return "G16R16";
    case 50: return "L8";
    case 51: return "A8L8";
    case 28: return "A8";
    default: return "format " + std::to_string(f);
    }
}

void Color565(uint16_t c, uint8_t* out) {
    out[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
    out[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
    out[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
    out[3] = 255;
}

uint32_t Crc(const uint8_t* p, size_t n, uint32_t crc = 0) {
    return static_cast<uint32_t>(crc32(crc, p, static_cast<uInt>(n)));
}

void PutBE(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 3; i >= 0; i--) {
        v.push_back(static_cast<uint8_t>(x >> (i * 8)));
    }
}

void Chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& body) {
    PutBE(png, static_cast<uint32_t>(body.size()));
    size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), body.begin(), body.end());
    PutBE(png, Crc(&png[start], png.size() - start));
}

} // namespace

std::vector<uint8_t> DecodeDxt(const uint8_t* data, size_t size, int width, int height, int format) {
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 0);
    int bw = (width + 3) / 4, bh = (height + 3) / 4;
    size_t blockBytes = format == 1 ? 8 : 16;
    if (size < blockBytes * bw * bh) {
        throw std::runtime_error("not enough image data for its size");
    }
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            const uint8_t* b = data + (static_cast<size_t>(by) * bw + bx) * blockBytes;
            const uint8_t* color = format == 1 ? b : b + 8;
            uint16_t c0 = static_cast<uint16_t>(color[0] | color[1] << 8);
            uint16_t c1 = static_cast<uint16_t>(color[2] | color[3] << 8);
            uint8_t pal[4][4];
            Color565(c0, pal[0]);
            Color565(c1, pal[1]);
            for (int c = 0; c < 3; c++) {
                if (c0 > c1 || format != 1) {
                    pal[2][c] = static_cast<uint8_t>((2 * pal[0][c] + pal[1][c]) / 3);
                    pal[3][c] = static_cast<uint8_t>((pal[0][c] + 2 * pal[1][c]) / 3);
                } else {
                    pal[2][c] = static_cast<uint8_t>((pal[0][c] + pal[1][c]) / 2);
                    pal[3][c] = 0;
                }
            }
            pal[2][3] = 255;
            pal[3][3] = (format == 1 && c0 <= c1) ? 0 : 255;
            uint32_t bits = color[4] | color[5] << 8 | color[6] << 16 | static_cast<uint32_t>(color[7]) << 24;
            uint8_t alpha[16];
            if (format == 3) {
                for (int i = 0; i < 16; i++) {
                    alpha[i] = static_cast<uint8_t>(((b[i / 2] >> ((i & 1) * 4)) & 15) * 17);
                }
            } else if (format == 5) {
                uint8_t a[8] = { b[0], b[1] };
                for (int i = 2; i < 8; i++) {
                    a[i] = b[0] > b[1] ? static_cast<uint8_t>(((8 - i) * b[0] + (i - 1) * b[1]) / 7)
                         : i < 6       ? static_cast<uint8_t>(((6 - i) * b[0] + (i - 1) * b[1]) / 5)
                         : i == 6      ? 0
                                       : 255;
                }
                uint64_t abits = 0;
                for (int i = 0; i < 6; i++) {
                    abits |= static_cast<uint64_t>(b[2 + i]) << (8 * i);
                }
                for (int i = 0; i < 16; i++) {
                    alpha[i] = a[(abits >> (3 * i)) & 7];
                }
            }
            for (int i = 0; i < 16; i++) {
                int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= width || y >= height) {
                    continue;
                }
                uint8_t* px = &rgba[(static_cast<size_t>(y) * width + x) * 4];
                memcpy(px, pal[(bits >> (2 * i)) & 3], 4);
                if (format != 1) {
                    px[3] = alpha[i];
                }
            }
        }
    }
    return rgba;
}

std::vector<uint8_t> EncodePng(const std::vector<uint8_t>& rgba, int width, int height) {
    std::vector<uint8_t> raw;
    raw.reserve((static_cast<size_t>(width) * 4 + 1) * height);
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + static_cast<size_t>(y) * width * 4,
                   rgba.begin() + static_cast<size_t>(y + 1) * width * 4);
    }
    uLongf len = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> z(len);
    if (compress2(z.data(), &len, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) {
        throw std::runtime_error("PNG compression failed");
    }
    z.resize(len);
    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    std::vector<uint8_t> ihdr;
    PutBE(ihdr, width);
    PutBE(ihdr, height);
    ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 });
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});
    return png;
}

bool DecodePixels(uint32_t format, const uint8_t* p, size_t size, int width, int height, std::vector<uint8_t>& rgba) {
    size_t pixels = static_cast<size_t>(width) * height;
    switch (format) {
    case FourCC('D', 'X', 'T', '1'): rgba = DecodeDxt(p, size, width, height, 1); return true;
    case FourCC('D', 'X', 'T', '3'): rgba = DecodeDxt(p, size, width, height, 3); return true;
    case FourCC('D', 'X', 'T', '5'): rgba = DecodeDxt(p, size, width, height, 5); return true;
    case 21: case 22: case 23: case 28: case 34: case 50: case 51: break;
    default: return false;
    }
    size_t bpp = (format == 21 || format == 22 || format == 34) ? 4 : (format == 23 || format == 51) ? 2 : 1;
    if (size < pixels * bpp) {
        throw std::runtime_error("not enough image data for its size");
    }
    rgba.resize(pixels * 4);
    for (size_t i = 0; i < pixels; i++) {
        uint8_t* o = &rgba[i * 4];
        const uint8_t* s = p + i * bpp;
        switch (format) {
        case 21: case 22: // BGRA in memory
            o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = format == 21 ? s[3] : 255;
            break;
        case 23: Color565(static_cast<uint16_t>(s[0] | s[1] << 8), o); break;
        case 34: o[0] = s[1]; o[1] = s[3]; o[2] = 0; o[3] = 255; break; // G16R16: high bytes
        case 50: o[0] = o[1] = o[2] = s[0]; o[3] = 255; break;
        case 51: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; break;
        default: o[0] = o[1] = o[2] = 255; o[3] = s[0]; break; // A8
        }
    }
    return true;
}

bool DecodeZoneImage(const Zone& zone, const WalkResult& walk, int asset, std::vector<uint8_t>& rgba, int& width,
                     int& height) {
    const WalkedAsset& a = CheckAsset(walk, asset, 0x08);
    const ImageRecord& img = walk.images.at(asset);
    Reader r{ zone.data };
    width = r.U16(a.header + 20);
    height = r.U16(a.header + 22);
    if (img.loadDef == kNoData || img.resourceSize == 0 || width == 0 || height == 0) {
        return false;
    }
    return DecodePixels(img.format, &zone.data[img.loadDef + 12], img.resourceSize, width, height, rgba);
}

std::vector<uint8_t> ImageToPng(const Zone& zone, const WalkResult& walk, int asset) {
    const WalkedAsset& a = CheckAsset(walk, asset, 0x08);
    const ImageRecord& img = walk.images.at(asset);
    if (img.loadDef == kNoData || img.resourceSize == 0) {
        throw std::runtime_error(a.name + " has no pixels in this zone: the game streams it from an .iwd archive");
    }
    std::vector<uint8_t> rgba;
    int width, height;
    if (!DecodeZoneImage(zone, walk, asset, rgba, width, height)) {
        throw std::runtime_error(a.name + ": can't decode " + FormatName(img.format));
    }
    return EncodePng(rgba, width, height);
}

int ModelLodCount(const Zone& zone, const WalkResult& walk, int asset) {
    const WalkedAsset& a = CheckAsset(walk, asset, 0x05);
    Reader r{ zone.data };
    return std::clamp(static_cast<int>(static_cast<int16_t>(r.U16(a.header + 216))), 1, 4);
}

int ModelLodVerts(const Zone& zone, const WalkResult& walk, int asset, int lod) {
    const WalkedAsset& a = CheckAsset(walk, asset, 0x05);
    const ModelRecord& rec = walk.models.at(asset);
    Reader r{ zone.data };
    int count = r.U16(a.header + 40 + lod * 32 + 4), first = r.U16(a.header + 40 + lod * 32 + 6), verts = 0;
    for (int i = first; i < first + count && i < static_cast<int>(rec.surfaces.size()); i++) {
        verts += r.U16(rec.surfaces[i].header + 4);
    }
    return verts;
}

std::vector<float> SampleTrack(const Track& track, float frame) {
    if (track.keys.empty()) {
        return {};
    }
    if (track.keys.size() == 1 || frame <= track.frames.front()) {
        return track.keys.front();
    }
    if (frame >= track.frames.back()) {
        return track.keys.back();
    }
    size_t i = 0;
    while (i + 2 < track.frames.size() && track.frames[i + 1] <= frame) {
        i++;
    }
    float span = static_cast<float>(track.frames[i + 1] - track.frames[i]);
    float f = span > 0 ? (frame - track.frames[i]) / span : 0.0f;
    std::vector<float> a = track.keys[i], b = track.keys[i + 1];
    if (a.size() == 4) {
        float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
        if (dot < 0) {
            for (float& v : b) {
                v = -v;
            }
        }
    }
    std::vector<float> out(a.size());
    for (size_t k = 0; k < a.size(); k++) {
        out[k] = a[k] + (b[k] - a[k]) * f;
    }
    if (out.size() == 4) {
        Normalize4(out.data());
    }
    return out;
}

// ---------------------------------------------------------------- reports

namespace {

const char* SemanticName(uint8_t s) {
    switch (s) {
    case 0: return "2d";
    case 1: return "function";
    case 2: return "colorMap";
    case 3: return "detailMap";
    case 4: return "unused2";
    case 5: return "normalMap";
    case 6: return "unused3";
    case 7: return "unused4";
    case 8: return "specularMap";
    case 9: return "unused5";
    case 10: return "occlusionMap";
    case 11: return "waterMap";
    default: return "other";
    }
}

void ReportMaterial(std::ostringstream& out, const Zone& zone, const WalkResult& walk, const std::string& name) {
    int m = walk.Find(0x06, name);
    if (m < 0) {
        out << "  " << name << ": not loaded in this zone\n";
        return;
    }
    const MaterialRecord& rec = walk.materials.at(m);
    out << "  " << name << "  (techset " << rec.techset << ")\n";
    Reader r{ zone.data };
    for (const MaterialTexture& t : rec.textures) {
        int img = walk.Find(0x08, t.image);
        std::string where = "?";
        if (img >= 0) {
            const ImageRecord& ir = walk.images.at(img);
            const WalkedAsset& ia = walk.assets[img];
            where = std::to_string(r.U16(ia.header + 20)) + "x" + std::to_string(r.U16(ia.header + 22)) + " " +
                    FormatName(ir.format) + ", " +
                    (ir.resourceSize > 0 ? "in zone, " + std::to_string(ir.resourceSize) + " bytes"
                                         : "streamed: images/" + t.image + ".iwi from an .iwd");
        }
        char hash[16];
        snprintf(hash, sizeof(hash), "%08x", t.nameHash);
        out << "    " << SemanticName(t.semantic) << " [" << hash << "] " << t.image << "  " << where << "\n";
    }
}

} // namespace

std::string MaterialReport(const Zone& zone, const WalkResult& walk, int asset) {
    std::ostringstream out;
    if (asset < 0 || asset >= static_cast<int>(walk.assets.size())) {
        throw std::runtime_error("no such asset");
    }
    const WalkedAsset& a = walk.assets[asset];
    if (a.type == 0x05) {
        out << a.name << "\n";
        std::vector<std::string> seen;
        for (const std::string& m : walk.models.at(asset).materials) {
            bool dup = false;
            for (const std::string& s : seen) {
                dup = dup || s == m;
            }
            if (!dup) {
                seen.push_back(m);
                ReportMaterial(out, zone, walk, m);
            }
        }
    } else if (a.type == 0x06) {
        ReportMaterial(out, zone, walk, a.name);
    } else {
        throw std::runtime_error("not a model or material");
    }
    return out.str();
}

std::vector<WeaponInfo> ReadWeapons(const Zone& zone, const WalkResult& walk) {
    Reader r{ zone.data };
    const ZoneStream& s = *walk.stream;
    auto str = [&](size_t field) { return s.CString(walk.Pointer(field)); };
    auto model = [&](size_t field) {
        int i = walk.AssetAt(field);
        return i >= 0 ? walk.assets[i].name : std::string();
    };
    std::vector<WeaponInfo> out;
    for (size_t i = 0; i < walk.assets.size(); i++) {
        const WalkedAsset& a = walk.assets[i];
        if (a.type != 0x18 || a.header == kNoData) {
            continue;
        }
        WeaponInfo w;
        w.name = a.name;
        w.displayName = str(a.header + 12);
        size_t def = walk.Pointer(a.header + 8);
        if (def != kNoData) {
            size_t gun = walk.Pointer(def + 4), world = walk.Pointer(def + 780);
            for (int k = 0; k < 16; k++) {
                std::string g = gun != kNoData ? model(gun + k * 4) : "";
                std::string wm = world != kNoData ? model(world + k * 4) : "";
                if (!g.empty() && std::find(w.viewModels.begin(), w.viewModels.end(), g) == w.viewModels.end()) {
                    w.viewModels.push_back(g);
                }
                if (!wm.empty() && std::find(w.worldModels.begin(), w.worldModels.end(), wm) == w.worldModels.end()) {
                    w.worldModels.push_back(wm);
                }
            }
            w.handModel = model(def + 8);
            auto fx = [&](size_t field) {
                std::string n = model(field);
                return !n.empty() && n[0] == ',' ? n.substr(1) : n;
            };
            w.viewFlashFx = fx(def + 116);
            w.worldFlashFx = fx(def + 120);
            w.projExplosionFx = fx(def + 1520);
            w.projTrailFx = fx(def + 1624);
            for (int k = 0; k < 62; k++) {
                std::string n = str(def + 124 + k * 4);
                if (!n.empty()) {
                    w.sounds.push_back({ k, n });
                }
            }
            size_t keys = walk.Pointer(def + 16), values = walk.Pointer(def + 20);
            for (int k = 0; k < 20 && keys != kNoData && values != kNoData; k++) {
                uint16_t key = r.U16(keys + k * 2), value = r.U16(values + k * 2);
                if (key != 0 && value != 0) {
                    w.notetrackSounds.push_back({ ScriptString(zone, key), ScriptString(zone, value) });
                }
            }
        }
        size_t anims = walk.Pointer(a.header + 16);
        for (int k = 0; k < 66 && anims != kNoData; k++) {
            std::string n = str(anims + k * 4);
            if (!n.empty()) {
                w.anims.push_back({ k, n });
            }
        }
        out.push_back(std::move(w));
    }
    return out;
}

std::string WeaponReport(const Zone& zone, const WalkResult& walk) {
    std::ostringstream out;
    for (const WeaponInfo& w : ReadWeapons(zone, walk)) {
        out << w.name << "  \"" << w.displayName << "\"\n";
        for (const std::string& m : w.viewModels) {
            out << "  view  " << m << (walk.Find(0x05, m) >= 0 ? "" : "  (not in this zone)") << "\n";
        }
        for (const std::string& m : w.worldModels) {
            out << "  world " << m << (walk.Find(0x05, m) >= 0 ? "" : "  (not in this zone)") << "\n";
        }
        if (!w.handModel.empty()) {
            out << "  hands " << w.handModel << "\n";
        }
        for (const auto& [label, fx] : { std::make_pair("flash", w.worldFlashFx), std::make_pair("viewflash", w.viewFlashFx),
                                         std::make_pair("explode", w.projExplosionFx), std::make_pair("trail", w.projTrailFx) }) {
            if (!fx.empty()) {
                out << "  fx " << label << " " << fx << "\n";
            }
        }
        for (const auto& [slot, name] : w.anims) {
            out << "  anim " << slot << " " << name << (walk.Find(0x04, name) >= 0 ? "" : "  (not in this zone)") << "\n";
        }
        for (const auto& [slot, name] : w.sounds) {
            out << "  sound " << slot << " " << name << "\n";
        }
        for (const auto& [note, alias] : w.notetrackSounds) {
            out << "  notetrack " << note << " " << alias << "\n";
        }
    }
    return out.str();
}

std::string SoundReport(const Zone& zone, const WalkResult& walk) {
    std::ostringstream out;
    Reader r{ zone.data };
    for (const auto& [index, bank] : walk.sounds) {
        const WalkedAsset& a = walk.assets[index];
        size_t h = a.header;
        int loaded = 0;
        for (const SoundAliasRecord& s : bank.aliases) {
            loaded += s.loaded ? 1 : 0;
        }
        out << "sound bank " << a.name << ": " << r.U32(h + 4) << " alias lists, " << bank.aliases.size()
            << " aliases (" << loaded << " loaded in the zone, " << bank.aliases.size() - loaded << " streamed)\n";
        for (const SoundAliasRecord& s : bank.aliases) {
            out << "  " << s.name << "  ";
            if (s.loaded) {
                static const char* formats[] = { "pcm16", "?", "?", "?", "xma", "mp3", "msadpcm", "wma" };
                uint32_t fmt = r.U32(s.asset + 28);
                out << "loaded " << s.file << " " << (fmt < 8 ? formats[fmt] : "?") << " " << r.U32(s.asset + 8)
                    << " Hz " << r.U32(s.asset + 12) << " ch " << r.U32(s.asset + 4) << " frames " << s.dataSize
                    << " bytes\n";
            } else {
                out << "streamed " << (s.file.empty() ? "(no file)" : s.file) << "\n";
            }
        }
    }
    if (walk.sounds.empty()) {
        out << "no sound banks in this zone\n";
    }
    return out.str();
}

} // namespace bomap
