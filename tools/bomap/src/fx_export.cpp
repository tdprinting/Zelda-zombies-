#include "fx_export.h"

#include "game_export.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// FxEffectDef (60 bytes) and FxElemDef (292 bytes) of the 32-bit PC game. Field
// offsets were worked out from the data (ranges of plausible millisecond, float and
// colour values) and from the order the zone loader reads the pointers in.

namespace bomap {

namespace {

constexpr uint32_t kTypeFx = 0x1C;
constexpr uint32_t kTypeMaterial = 0x06;
constexpr uint32_t kTypeModel = 0x05;

// Element types, as the zone loader treats their visuals.
constexpr uint8_t kElemModel = 7;
constexpr uint8_t kElemOmniLight = 8;
constexpr uint8_t kElemSpotLight = 9;
constexpr uint8_t kElemSound = 10;
constexpr uint8_t kElemDecal = 11;
constexpr uint8_t kElemRunner = 12;

FxRange Range(const ZoneStream& s, size_t at) {
    return { s.F32(at), s.F32(at + 4) };
}

std::string AssetNameAt(const WalkResult& walk, size_t field) {
    int i = walk.AssetAt(field);
    if (i < 0) {
        return std::string();
    }
    std::string n = walk.assets[i].name;
    return !n.empty() && n[0] == ',' ? n.substr(1) : n;
}

std::string StringAt(const WalkResult& walk, size_t field) {
    size_t p = walk.Pointer(field);
    return p == kNoData ? std::string() : walk.stream->CString(p);
}

void PutU8(std::vector<uint8_t>& o, uint8_t v) {
    o.push_back(v);
}
void PutU16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back(static_cast<uint8_t>(v));
    o.push_back(static_cast<uint8_t>(v >> 8));
}
void PutU32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        o.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
}
void PutF32(std::vector<uint8_t>& o, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    PutU32(o, v);
}
void PutName(std::vector<uint8_t>& o, const std::string& s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        o.push_back(i + 1 < len && i < s.size() ? static_cast<uint8_t>(s[i]) : 0);
    }
}

} // namespace

bool ReadFx(const Zone& zone, const WalkResult& walk, const std::string& name, FxEffect& out) {
    (void)zone;
    int index = walk.Find(kTypeFx, name);
    if (index < 0) {
        index = walk.Find(kTypeFx, "," + name);
    }
    if (index < 0) {
        return false;
    }
    const ZoneStream& s = *walk.stream;
    size_t h = walk.assets[index].header;
    out = FxEffect();
    out.name = name;
    out.loopingLifeMs = s.U32(h + 12);
    uint32_t counts[3] = { s.U32(h + 16), s.U32(h + 20), s.U32(h + 24) };
    size_t elems = walk.Pointer(h + 28);
    if (elems == kNoData) {
        return true;
    }
    uint32_t total = counts[0] + counts[1] + counts[2];
    for (uint32_t i = 0; i < total; i++) {
        size_t d = elems + i * 292;
        FxElem e;
        e.category = i < counts[0] ? FxElem::kLooping : i < counts[0] + counts[1] ? FxElem::kOneShot : FxElem::kEmission;
        e.flags = s.U32(d + 0);
        if (e.category == FxElem::kLooping) {
            e.loopInterval = s.I32(d + 4);
            e.loopCount = s.I32(d + 8);
        } else {
            e.countBase = s.I32(d + 4);
            e.countAmp = s.I32(d + 8);
        }
        e.spawnRange = Range(s, d + 12);
        e.fadeIn = Range(s, d + 20);
        e.fadeOut = Range(s, d + 28);
        // 36: spawn frustum cull radius
        e.delayBase = s.I32(d + 40);
        e.delayAmp = s.I32(d + 44);
        e.lifeBase = s.I32(d + 48);
        e.lifeAmp = s.I32(d + 52);
        for (int k = 0; k < 3; k++) {
            e.origin[k] = Range(s, d + 56 + k * 8);
        }
        e.offsetRadius = Range(s, d + 80);
        e.offsetHeight = Range(s, d + 88);
        for (int k = 0; k < 3; k++) {
            e.angles[k] = Range(s, d + 96 + k * 8);
            e.angularVel[k] = Range(s, d + 120 + k * 8);
        }
        e.initialRotation = Range(s, d + 144);
        // 152: rotation axis
        e.gravity = Range(s, d + 156);
        // 164: reflection factor
        e.atlasBehavior = s.U8(d + 172);
        e.atlasIndex = s.U8(d + 173);
        e.atlasFps = s.U8(d + 174);
        e.atlasLoops = s.U8(d + 175);
        e.atlasCols = s.U8(d + 176);
        e.atlasRows = s.U8(d + 177);
        e.atlasEntries = s.U16(d + 178);
        // 180: wind influence
        e.type = s.U8(d + 184);
        uint8_t visualCount = s.U8(d + 185);
        int velCount = s.U8(d + 186) + 1;
        int visCount = s.U8(d + 187) + 1;
        size_t vel = walk.Pointer(d + 188);
        for (int k = 0; vel != kNoData && k < velCount; k++) {
            size_t v = vel + k * 96;
            FxVelSample vs;
            for (int c = 0; c < 3; c++) {
                vs.localVel[c] = s.F32(v + 0 + c * 4);
                vs.localVelAmp[c] = s.F32(v + 12 + c * 4);
                vs.localDelta[c] = s.F32(v + 24 + c * 4);
                vs.localDeltaAmp[c] = s.F32(v + 36 + c * 4);
                vs.worldVel[c] = s.F32(v + 48 + c * 4);
                vs.worldVelAmp[c] = s.F32(v + 60 + c * 4);
                vs.worldDelta[c] = s.F32(v + 72 + c * 4);
                vs.worldDeltaAmp[c] = s.F32(v + 84 + c * 4);
            }
            e.vel.push_back(vs);
        }
        size_t vis = walk.Pointer(d + 192);
        for (int k = 0; vis != kNoData && k < visCount; k++) {
            size_t v = vis + k * 48;
            FxVisSample vs;
            // Colours are stored B, G, R, A.
            static const int kOrder[4] = { 2, 1, 0, 3 };
            for (int c = 0; c < 4; c++) {
                vs.color[c] = s.U8(v + kOrder[c]);
                vs.colorAmp[c] = s.U8(v + 24 + kOrder[c]);
            }
            vs.rotationDelta = s.F32(v + 4);
            vs.rotationTotal = s.F32(v + 8);
            vs.size[0] = s.F32(v + 12);
            vs.size[1] = s.F32(v + 16);
            vs.scale = s.F32(v + 20);
            vs.sizeAmp[0] = s.F32(v + 24 + 12);
            vs.sizeAmp[1] = s.F32(v + 24 + 16);
            vs.scaleAmp = s.F32(v + 24 + 20);
            e.vis.push_back(vs);
        }
        if (e.type == kElemRunner) {
            e.effect = visualCount > 1 ? std::string() : StringAt(walk, d + 196);
            if (visualCount > 1) {
                size_t arr = walk.Pointer(d + 196);
                if (arr != kNoData) {
                    e.effect = StringAt(walk, arr);
                }
            }
        } else if (e.type == kElemModel) {
            size_t field = visualCount > 1 ? walk.Pointer(d + 196) : d + 196;
            if (field != kNoData) {
                e.model = AssetNameAt(walk, field);
            }
        } else if (e.type != kElemOmniLight && e.type != kElemSpotLight && e.type != kElemSound &&
                   e.type != kElemDecal) {
            if (visualCount > 1) {
                size_t arr = walk.Pointer(d + 196);
                for (uint8_t k = 0; arr != kNoData && k < visualCount; k++) {
                    std::string m = AssetNameAt(walk, arr + k * 4);
                    if (!m.empty()) {
                        e.materials.push_back(m);
                    }
                }
            } else if (visualCount == 1) {
                std::string m = AssetNameAt(walk, d + 196);
                if (!m.empty()) {
                    e.materials.push_back(m);
                }
            }
        }
        if (!e.effect.empty() && e.effect[0] == ',') {
            e.effect = e.effect.substr(1);
        }
        out.elems.push_back(std::move(e));
    }
    return true;
}

std::string DescribeFx(const FxEffect& fx) {
    std::string o;
    char b[512];
    snprintf(b, sizeof(b), "%s: %zu elements, looping life %u ms\n", fx.name.c_str(), fx.elems.size(),
             fx.loopingLifeMs);
    o += b;
    static const char* cats[] = { "looping", "oneshot", "emission" };
    for (size_t i = 0; i < fx.elems.size(); i++) {
        const FxElem& e = fx.elems[i];
        snprintf(b, sizeof(b),
                 " [%zu] %s type %u flags %08x %s %d/%d delay %d+%d life %d+%d grav %.2f+%.2f fade %.2f/%.2f "
                 "atlas b%u i%u fps%u %ux%u n%u\n",
                 i, cats[e.category], e.type, e.flags, e.category == FxElem::kLooping ? "every/max" : "count",
                 e.category == FxElem::kLooping ? e.loopInterval : e.countBase,
                 e.category == FxElem::kLooping ? e.loopCount : e.countAmp, e.delayBase, e.delayAmp, e.lifeBase,
                 e.lifeAmp, e.gravity.base, e.gravity.amplitude, e.fadeIn.base, e.fadeOut.base, e.atlasBehavior,
                 e.atlasIndex, e.atlasFps, 1u << e.atlasCols, 1u << e.atlasRows, e.atlasEntries);
        o += b;
        snprintf(b, sizeof(b), "      origin (%.1f+%.1f %.1f+%.1f %.1f+%.1f) radius %.1f+%.1f height %.1f+%.1f\n",
                 e.origin[0].base, e.origin[0].amplitude, e.origin[1].base, e.origin[1].amplitude, e.origin[2].base,
                 e.origin[2].amplitude, e.offsetRadius.base, e.offsetRadius.amplitude, e.offsetHeight.base,
                 e.offsetHeight.amplitude);
        o += b;
        for (const std::string& m : e.materials) {
            o += "      material " + m + "\n";
        }
        if (!e.effect.empty()) {
            o += "      runs " + e.effect + "\n";
        }
        if (!e.model.empty()) {
            o += "      model " + e.model + "\n";
        }
        for (size_t k = 0; k < e.vel.size(); k++) {
            const FxVelSample& v = e.vel[k];
            snprintf(b, sizeof(b),
                     "      vel[%zu] local (%.2f %.2f %.2f)+(%.2f %.2f %.2f) d(%.2f %.2f %.2f) world (%.2f %.2f %.2f)+(%.2f "
                     "%.2f %.2f) d(%.2f %.2f %.2f)\n",
                     k, v.localVel[0], v.localVel[1], v.localVel[2], v.localVelAmp[0], v.localVelAmp[1],
                     v.localVelAmp[2], v.localDelta[0], v.localDelta[1], v.localDelta[2], v.worldVel[0], v.worldVel[1],
                     v.worldVel[2], v.worldVelAmp[0], v.worldVelAmp[1], v.worldVelAmp[2], v.worldDelta[0],
                     v.worldDelta[1], v.worldDelta[2]);
            o += b;
        }
        for (size_t k = 0; k < e.vis.size(); k++) {
            const FxVisSample& v = e.vis[k];
            snprintf(b, sizeof(b),
                     "      vis[%zu] rgba %u %u %u %u (+%u %u %u %u) size %.2f %.2f (+%.2f %.2f) scale %.2f+%.2f rot "
                     "%.3f/%.3f\n",
                     k, v.color[0], v.color[1], v.color[2], v.color[3], v.colorAmp[0], v.colorAmp[1], v.colorAmp[2],
                     v.colorAmp[3], v.size[0], v.size[1], v.sizeAmp[0], v.sizeAmp[1], v.scale, v.scaleAmp,
                     v.rotationDelta, v.rotationTotal);
            o += b;
        }
    }
    return o;
}

} // namespace bomap

namespace bomap {

namespace {

bool Has(const std::string& s, const char* word) {
    return s.find(word) != std::string::npos;
}

// What to draw when the image isn't available, from the material's name.
uint8_t FallbackShape(const std::string& material) {
    if (Has(material, "spark") || Has(material, "ember") || Has(material, "streak") || Has(material, "tracer") ||
        Has(material, "muz") || Has(material, "drop")) {
        return 2; // streak
    }
    if (Has(material, "ring") || Has(material, "wave")) {
        return 3; // ring
    }
    if (Has(material, "glow") || Has(material, "flare") || Has(material, "light") || Has(material, "ray")) {
        return 4; // wide glow
    }
    if (Has(material, "smk") || Has(material, "smoke") || Has(material, "dust") || Has(material, "dirt") ||
        Has(material, "debris") || Has(material, "fire") || Has(material, "blood") || Has(material, "cloud")) {
        return 1; // puff
    }
    return 0; // soft dot
}

// 0 alpha blend, 1 additive, 2 multiply, from the technique set's name.
uint8_t BlendMode(const WalkResult& walk, const std::string& material) {
    int m = walk.Find(kTypeMaterial, material);
    if (m < 0) {
        m = walk.Find(kTypeMaterial, "," + material);
    }
    std::string techset = m >= 0 ? walk.materials.at(m).techset : std::string();
    if (Has(techset, "_add") || Has(techset, "add_") || Has(techset, "screen")) {
        return 1;
    }
    if (Has(techset, "multiply")) {
        return 2;
    }
    return 0;
}

bool IsDistortion(const WalkResult& walk, const std::string& material) {
    int m = walk.Find(kTypeMaterial, material);
    if (m < 0) {
        m = walk.Find(kTypeMaterial, "," + material);
    }
    std::string techset = m >= 0 ? walk.materials.at(m).techset : std::string();
    return Has(techset, "distortion") || Has(material, "distortion");
}

// 0 camera-facing, 1 stretched along its movement (or the effect's forward axis), 2 flat
// facing the effect's forward axis.
uint8_t RenderMode(uint8_t type) {
    switch (type) {
    case 1: return 2;
    case 3:
    case 4: return 1;
    default: return 0;
    }
}

} // namespace

std::vector<uint8_t> WriteGameFx(const Zone& zone, const WalkResult& walk, const FxEffect& fx, TextureSource* textures,
                                 int maxTexture, GameFxInfo* info) {
    GameFxInfo local;
    GameFxInfo& inf = info != nullptr ? *info : local;
    // Flatten runners one level: their effect's elements play from the runner's place.
    struct Flat {
        FxElem elem;
        float originAdd[3] = {};
        int delayAdd = 0;
    };
    std::vector<Flat> flat;
    for (const FxElem& e : fx.elems) {
        if (e.type == kElemRunner && !e.effect.empty()) {
            FxEffect child;
            if (ReadFx(zone, walk, e.effect, child)) {
                inf.runnersInlined++;
                for (const FxElem& c : child.elems) {
                    if (c.type == kElemRunner) {
                        continue;
                    }
                    Flat f;
                    f.elem = c;
                    for (int k = 0; k < 3; k++) {
                        f.originAdd[k] = e.origin[k].base;
                    }
                    f.delayAdd = e.delayBase;
                    if (e.category == FxElem::kLooping && c.category != FxElem::kLooping) {
                        // A looping runner replays its one-shot effect at the runner's rate.
                        f.elem.category = FxElem::kLooping;
                        f.elem.loopInterval = std::max(e.loopInterval, 1);
                        f.elem.loopCount = std::max(c.countBase + c.countAmp, 1) * 8;
                    }
                    flat.push_back(f);
                }
            }
            continue;
        }
        Flat f;
        f.elem = e;
        flat.push_back(f);
    }

    std::vector<uint8_t> images, emitters;
    std::vector<std::string> imageNames;
    uint32_t imageCount = 0, emitterCount = 0;
    for (const Flat& f : flat) {
        const FxElem& e = f.elem;
        if (e.type == kElemModel || e.type == kElemOmniLight || e.type == kElemSpotLight || e.type == kElemSound ||
            e.type == kElemDecal || e.type == kElemRunner || e.materials.empty() || e.vis.empty()) {
            continue;
        }
        const std::string& material = e.materials[0];
        if (IsDistortion(walk, material)) {
            continue;
        }
        int32_t image = -1;
        std::string imageName = MaterialColorMap(walk, material);
        if (imageName.empty()) {
            imageName = MaterialColorMap(walk, "," + material);
        }
        auto known = std::find(imageNames.begin(), imageNames.end(), imageName);
        if (!imageName.empty() && known != imageNames.end()) {
            image = static_cast<int32_t>(known - imageNames.begin());
        } else if (!imageName.empty()) {
            Texture tex;
            if (textures != nullptr && textures->Load(imageName, maxTexture, tex)) {
                image = static_cast<int32_t>(imageCount++);
                imageNames.push_back(imageName);
                PutName(images, imageName, 64);
                PutU16(images, static_cast<uint16_t>(tex.width));
                PutU16(images, static_cast<uint16_t>(tex.height));
                images.insert(images.end(), tex.rgba.begin(), tex.rgba.end());
                inf.images++;
            } else if (std::find(inf.missingImages.begin(), inf.missingImages.end(), imageName) ==
                       inf.missingImages.end()) {
                inf.missingImages.push_back(imageName);
            }
        }
        if (image < 0) {
            inf.fallbacks++;
        }
        emitterCount++;
        std::vector<uint8_t>& o = emitters;
        PutU8(o, static_cast<uint8_t>(e.category));
        PutU8(o, RenderMode(e.type));
        PutU8(o, BlendMode(walk, material));
        PutU8(o, FallbackShape(material));
        PutU32(o, static_cast<uint32_t>(image));
        // Atlas: a fallback dot has one frame.
        bool atlas = image >= 0;
        PutU8(o, atlas ? static_cast<uint8_t>(1u << std::min<uint8_t>(e.atlasCols, 4)) : 1);
        PutU8(o, atlas ? static_cast<uint8_t>(1u << std::min<uint8_t>(e.atlasRows, 4)) : 1);
        PutU8(o, atlas ? e.atlasIndex : 0);
        PutU8(o, atlas ? std::min<uint8_t>(e.atlasBehavior & 7, 3) : 0);
        PutU8(o, e.atlasFps);
        PutU8(o, 0);
        PutU16(o, atlas ? e.atlasEntries : 1);
        bool looping = e.category == FxElem::kLooping;
        PutU32(o, static_cast<uint32_t>(looping ? std::max(e.loopInterval, 1) : std::max(e.countBase, 0)));
        PutU32(o, static_cast<uint32_t>(looping ? std::max(e.loopCount, 0) : std::max(e.countAmp, 0)));
        PutU32(o, static_cast<uint32_t>(std::max(e.delayBase + f.delayAdd, 0)));
        PutU32(o, static_cast<uint32_t>(std::max(e.delayAmp, 0)));
        PutU32(o, static_cast<uint32_t>(std::max(e.lifeBase, 1)));
        PutU32(o, static_cast<uint32_t>(std::max(e.lifeAmp, 0)));
        for (int k = 0; k < 3; k++) {
            PutF32(o, e.origin[k].base + f.originAdd[k]);
            PutF32(o, e.origin[k].amplitude);
        }
        PutF32(o, e.offsetRadius.base);
        PutF32(o, e.offsetRadius.amplitude);
        PutF32(o, e.offsetHeight.base);
        PutF32(o, e.offsetHeight.amplitude);
        PutF32(o, e.gravity.base);
        PutF32(o, e.gravity.amplitude);
        PutF32(o, e.initialRotation.base);
        PutF32(o, e.initialRotation.amplitude);
        PutU32(o, static_cast<uint32_t>(e.vel.size()));
        for (const FxVelSample& v : e.vel) {
            for (int k = 0; k < 3; k++) PutF32(o, v.localDelta[k]);
            for (int k = 0; k < 3; k++) PutF32(o, v.localDeltaAmp[k]);
            for (int k = 0; k < 3; k++) PutF32(o, v.worldDelta[k]);
            for (int k = 0; k < 3; k++) PutF32(o, v.worldDeltaAmp[k]);
        }
        PutU32(o, static_cast<uint32_t>(e.vis.size()));
        for (const FxVisSample& v : e.vis) {
            for (int k = 0; k < 4; k++) PutU8(o, v.color[k]);
            for (int k = 0; k < 4; k++) PutU8(o, v.colorAmp[k]);
            PutF32(o, v.size[0]);
            PutF32(o, v.size[1]);
            PutF32(o, v.sizeAmp[0]);
            PutF32(o, v.sizeAmp[1]);
            PutF32(o, v.rotationTotal);
        }
    }
    inf.emitters = emitterCount;

    std::vector<uint8_t> out;
    const char magic[8] = { 'Z', 'Z', 'F', 'X', '1', 0, 0, 0 };
    out.insert(out.end(), magic, magic + 8);
    PutU32(out, 1);
    PutU32(out, fx.loopingLifeMs);
    PutU32(out, imageCount);
    out.insert(out.end(), images.begin(), images.end());
    PutU32(out, emitterCount);
    out.insert(out.end(), emitters.begin(), emitters.end());
    return out;
}

} // namespace bomap
