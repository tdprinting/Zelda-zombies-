#include "zone_walker.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>

// Structure sizes and field offsets below are for the 32-bit PC game. The load
// order of every structure follows the game's own loader, which has to be
// reproduced exactly: data is only found by reading everything before it.

namespace bomap {

namespace {

// Alignment masks the loader uses before each kind of data.
constexpr uint32_t kAlign1 = 0;
constexpr uint32_t kAlign2 = 1;
constexpr uint32_t kAlign4 = 3;
constexpr uint32_t kAlign8 = 7;
constexpr uint32_t kAlign16 = 15;
constexpr uint32_t kAlign128 = 127;
constexpr uint32_t kAlign2048 = 2047;

class Walker {
public:
    Walker(const Zone& zone, WalkResult& out)
        : mZone(zone), mOut(out), mStream(std::make_shared<ZoneStream>(zone.data, 52)), s(*mStream) {
        out.stream = mStream;
    }

    void Run();
    size_t Position() const { return s.FilePos(); }

private:
    const Zone& mZone;
    WalkResult& mOut;
    std::shared_ptr<ZoneStream> mStream;
    ZoneStream& s;
    std::map<uint32_t, int> mAliases; // block-4 slot token -> asset index
    std::map<size_t, SoundAliasRecord> mSoundFiles; // sound file structs already read, by file offset
    int mTop = -1;                    // listed asset being walked

    // ---- primitives ----
    uint32_t U32(size_t at, int off) const { return s.U32(at == kNoData ? kNoData : at + off); }
    uint16_t U16(size_t at, int off) const { return s.U16(at == kNoData ? kNoData : at + off); }
    uint8_t U8(size_t at, int off) const { return at == kNoData ? 0 : s.U8(at + off); }

    // Data behind a pointer field that the loader follows when it is non-null.
    size_t Follow(size_t at, int off, uint32_t align, size_t size) {
        if (U32(at, off) == 0) {
            return kNoData;
        }
        s.Align(align);
        return Remember(at, off, s.Load(size));
    }
    size_t Remember(size_t at, int off, size_t data) {
        if (at != kNoData && data != kNoData) {
            mOut.loadedAt[at + off] = data;
        }
        return data;
    }
    // Data behind a pointer field that is either inline (-1) or points at earlier data.
    size_t FollowOrRef(size_t at, int off, uint32_t align, size_t size) {
        uint32_t v = U32(at, off);
        if (v == 0) {
            return kNoData;
        }
        if (v == kPtrInline) {
            s.Align(align);
            return Remember(at, off, s.Load(size));
        }
        return s.Resolve(v);
    }
    bool IsInline(size_t at, int off) const { return U32(at, off) == kPtrInline; }

    std::string XStr(size_t at, int off) {
        uint32_t v = U32(at, off);
        if (v == 0) {
            return std::string();
        }
        if (v == kPtrInline) {
            size_t at2 = kNoData;
            std::string str = s.LoadString(&at2);
            Remember(at, off, at2);
            return str;
        }
        return s.CString(s.Resolve(v));
    }
    void XStrings(size_t at, int off, int count) {
        for (int i = 0; i < count; i++) {
            XStr(at, off + i * 4);
        }
    }
    // Array of string pointers behind a pointer field.
    void XStrArrayPtr(size_t at, int off, int count) {
        size_t arr = FollowOrRef(at, off, kAlign4, 4u * count);
        if (IsInline(at, off)) {
            XStrings(arr, 0, count);
        }
    }

    // ---- assets ----
    int Record(uint32_t type, const std::string& name, size_t header, size_t start, bool listed) {
        WalkedAsset a;
        a.type = type;
        a.name = name;
        a.header = header;
        a.start = start;
        a.end = s.FilePos();
        a.listed = listed;
        a.parent = listed ? -1 : mTop;
        mOut.assets.push_back(a);
        return static_cast<int>(mOut.assets.size()) - 1;
    }

    static size_t HeaderSize(uint32_t type);
    static uint32_t HeaderAlign(uint32_t type) { return type == 0x16 ? kAlign8 : kAlign4; }

    // An asset pointer field: loads the asset if it is inline, and returns its index
    // (or -1 for null or an asset this walk doesn't know).
    int AssetPtr(size_t at, int off, uint32_t type, bool listed = false);
    std::string WalkAsset(uint32_t type, size_t h, int index);

    std::string AssetName(int index) const {
        return index >= 0 ? mOut.assets[index].name : std::string();
    }
    void ModelPtr(size_t at, int off) { AssetPtr(at, off, 0x05); }
    void MaterialPtr(size_t at, int off) { AssetPtr(at, off, 0x06); }
    void FxPtr(size_t at, int off) { AssetPtr(at, off, 0x1C); }
    void ImagePtr(size_t at, int off) { AssetPtr(at, off, 0x08); }

    std::string PhysPreset(size_t h);
    std::string PhysConstraints(size_t h);
    void PhysConstraint(size_t c);
    std::string DestructibleDef(size_t h);
    std::string XAnimParts(size_t h, AnimRecord& rec);
    void XAnimPartTrans(size_t t, bool bigFrames, AnimRecord* rec);
    void XAnimDeltaQuat(size_t q, bool bigFrames, AnimRecord* rec);
    std::string XModel(size_t h, ModelRecord& rec);
    void XSurfaces(size_t arr, int count, ModelRecord& rec);
    void Collmap(size_t c);
    std::string Material(size_t h, MaterialRecord& rec);
    std::string TechniqueSet(size_t h);
    void Technique(size_t at, int off);
    std::string Image(size_t h, ImageRecord& rec);
    std::string SndBank(size_t h, SoundBankRecord& rec);
    void SoundFile(size_t sf, SoundAliasRecord& rec);
    std::string FxEffectDef(size_t h);
    void FxElemDef(size_t d);
    void FxElemVisuals(size_t v, uint8_t elemType);
    std::string ImpactFx(size_t h);
    std::string WeaponVariant(size_t h);
    void WeaponDef(size_t w);
    void FlameTable(size_t at, int off);
    std::string MenuList(size_t h);
    std::string Menu(size_t h);
    std::string Window(size_t w);
    void ExpressionStatement(size_t e);
    void GenericEventHandler(size_t at, int off);
    void GenericEventScript(size_t at, int off);
    void ItemKeyHandler(size_t at, int off);
    void ScriptCondition(size_t at, int off);
    void ItemDef(size_t it);
    void ItemDefData(size_t at, int off, int type);
    void TextDef(size_t t, int type);
    void FocusItemDef(size_t f, int type);
    void ListBox(size_t l);
    std::string Localize(size_t h);
    std::string RawFile(size_t h);
    std::string StringTable(size_t h);
    std::string MapEnts(size_t h);
    std::string LightDef(size_t h);
    std::string ComWorld(size_t h);
    std::string GameWorld(size_t h);
    std::string ClipMap(size_t h);
    void Brush(size_t b);
    void DynEntityDefs(size_t at, int off, int count);
    std::string GfxWorld(size_t h);
    void GfxCell(size_t c);
    void GfxPortal(size_t p);
};

size_t Walker::HeaderSize(uint32_t type) {
    switch (type) {
    case 0x01: return 84;   // PhysPreset
    case 0x02: return 2696; // PhysConstraints
    case 0x03: return 24;   // DestructibleDef
    case 0x04: return 104;  // XAnimParts
    case 0x05: return 252;  // XModel
    case 0x06: return 192;  // Material
    case 0x07: return 528;  // MaterialTechniqueSet
    case 0x08: return 52;   // GfxImage
    case 0x09: return 40;   // SndBank
    case 0x0B:
    case 0x0C: return 332;  // clipMap_t
    case 0x0D: return 64;   // ComWorld
    case 0x0E:
    case 0x0F: return 44;   // GameWorldSp/Mp
    case 0x10: return 12;   // MapEnts
    case 0x11: return 1084; // GfxWorld
    case 0x12: return 16;   // GfxLightDef
    case 0x15: return 12;   // MenuList
    case 0x16: return 400;  // menuDef_t
    case 0x17: return 8;    // LocalizeEntry
    case 0x18: return 228;  // WeaponVariantDef
    case 0x1C: return 60;   // FxEffectDef
    case 0x1D: return 8;    // FxImpactTable
    case 0x24: return 12;   // RawFile
    case 0x25: return 20;   // StringTable
    default: return 0;
    }
}

void Walker::Run() {
    // The script string table: pointers in block 4, then the strings.
    s.Push(4);
    if (s.U32(36 + 4) != 0) {
        s.Align(kAlign4);
        size_t arr = s.Load(4u * mZone.scriptStrings.size());
        for (size_t i = 0; i < mZone.scriptStrings.size(); i++) {
            XStr(arr, static_cast<int>(i * 4));
        }
    }
    s.Pop();

    s.Push(4);
    size_t count = mZone.assets.size();
    if (s.U32(36 + 12) != 0) {
        s.Align(kAlign4);
        size_t list = s.Load(8 * count);
        for (size_t i = 0; i < count; i++) {
            mOut.stoppedAsset = static_cast<int>(i);
            uint32_t type = s.U32(list + i * 8);
            AssetPtr(list + i * 8, 4, type, true);
        }
    }
    s.Pop();
    mOut.stoppedAsset = -1;
    mOut.complete = true;
}

int Walker::AssetPtr(size_t at, int off, uint32_t type, bool listed) {
    uint32_t v = U32(at, off);
    int index = -1;
    s.Push(0);
    if (v == kPtrInline || v == kPtrInsert) {
        size_t size = HeaderSize(type);
        if (size == 0) {
            throw std::runtime_error(std::string("no reader for asset type ") + AssetTypeName(type) + " (0x" +
                                     [&] { char b[8]; snprintf(b, sizeof(b), "%02X", type); return std::string(b); }() + ")");
        }
        s.Align(HeaderAlign(type));
        uint32_t token = v == kPtrInsert ? s.InsertPointer() : 0;
        size_t start = s.FilePos();
        size_t h = s.Load(size);
        index = Record(type, "", h, start, listed);
        if (at != kNoData) {
            mOut.assetAt[at + off] = index;
        }
        int savedTop = mTop;
        if (listed) {
            mTop = index;
        }
        std::string name = WalkAsset(type, h, index);
        mTop = savedTop;
        mOut.assets[index].name = name;
        mOut.assets[index].end = s.FilePos();
        mOut.assets[index].aliasToken = token;
        if (token != 0) {
            mAliases[token] = index;
        }
        // Later data can also point at the slot this pointer was stored in.
        if (uint32_t slot = s.TokenFor(at == kNoData ? kNoData : at + off)) {
            mAliases[slot] = index;
        }
    } else if (v != 0) {
        auto it = mAliases.find(v);
        index = it != mAliases.end() ? it->second : -1;
        if (at != kNoData && index >= 0) {
            mOut.assetAt[at + off] = index;
        }
        if (listed) {
            // A listed asset that only refers to an earlier one.
            int ref = Record(type, AssetName(index), kNoData, s.FilePos(), true);
            mOut.assets[ref].parent = index;
        }
    }
    s.Pop();
    return index;
}

std::string Walker::WalkAsset(uint32_t type, size_t h, int index) {
    switch (type) {
    case 0x01: return PhysPreset(h);
    case 0x02: return PhysConstraints(h);
    case 0x03: return DestructibleDef(h);
    case 0x04: return XAnimParts(h, mOut.anims[index]);
    case 0x05: return XModel(h, mOut.models[index]);
    case 0x06: return Material(h, mOut.materials[index]);
    case 0x07: return TechniqueSet(h);
    case 0x08: return Image(h, mOut.images[index]);
    case 0x09: return SndBank(h, mOut.sounds[index]);
    case 0x0B:
    case 0x0C: return ClipMap(h);
    case 0x0D: return ComWorld(h);
    case 0x0E:
    case 0x0F: return GameWorld(h);
    case 0x10: return MapEnts(h);
    case 0x11: return GfxWorld(h);
    case 0x12: return LightDef(h);
    case 0x15: return MenuList(h);
    case 0x16: return Menu(h);
    case 0x17: return Localize(h);
    case 0x18: return WeaponVariant(h);
    case 0x1C: return FxEffectDef(h);
    case 0x1D: return ImpactFx(h);
    case 0x24: return RawFile(h);
    case 0x25: return StringTable(h);
    default: throw std::runtime_error("no reader for asset type " + std::to_string(type));
    }
}

// ---------------------------------------------------------------- physics

std::string Walker::PhysPreset(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    XStr(h, 28); // sndAliasPrefix
    s.Pop();
    return name;
}

void Walker::PhysConstraint(size_t c) {
    // 168 bytes: targetname/target_ent script strings need no data.
    XStr(c, 20); // target_bone1
    XStr(c, 36); // target_bone2
    MaterialPtr(c, 140);
}

std::string Walker::PhysConstraints(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    for (int i = 0; i < 16; i++) {
        PhysConstraint(h + 8 + i * 168);
    }
    s.Pop();
    return name;
}

std::string Walker::DestructibleDef(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    ModelPtr(h, 4);
    ModelPtr(h, 8);
    int pieceCount = static_cast<int>(U32(h, 12));
    size_t pieces = Follow(h, 16, kAlign4, 312u * pieceCount);
    for (int p = 0; pieces != kNoData && p < pieceCount; p++) {
        size_t piece = pieces + p * 312;
        for (int st = 0; st < 5; st++) {
            size_t stage = piece + st * 48;
            FxPtr(stage, 16);  // breakEffect
            XStrings(stage, 20, 3); // breakSound, breakNotify, loopSound
            for (int m = 0; m < 3; m++) {
                ModelPtr(stage, 32 + m * 4);
            }
            AssetPtr(stage, 44, 0x01); // physPreset
        }
        AssetPtr(piece, 268, 0x02); // physConstraints
        XStr(piece, 276);           // damageSound
        FxPtr(piece, 280);          // burnEffect
        XStr(piece, 284);           // burnSound
    }
    s.Pop();
    return name;
}

// ---------------------------------------------------------------- animation

void Walker::XAnimPartTrans(size_t t, bool bigFrames, AnimRecord* rec) {
    uint16_t size = U16(t, 0);
    bool smallTrans = U8(t, 2) != 0;
    if (size == 0) {
        size_t v = s.Load(12); // frame0
        if (rec != nullptr) {
            rec->deltaTransData = v;
        }
        return;
    }
    size_t frames = s.Load(28); // mins, size, frames pointer
    size_t indices = s.Load(bigFrames ? 2u * (size + 1) : size + 1u);
    size_t data = kNoData;
    if (U32(frames, 24) != 0) {
        if (smallTrans) {
            s.Align(kAlign1);
            data = s.Load(3u * (size + 1));
        } else {
            s.Align(kAlign4);
            data = s.Load(6u * (size + 1));
        }
    }
    if (rec != nullptr) {
        rec->deltaTransFrames = frames;
        rec->deltaTransIndices = indices;
        rec->deltaTransData = data;
    }
}

void Walker::XAnimDeltaQuat(size_t q, bool bigFrames, AnimRecord* rec) {
    uint16_t size = U16(q, 0);
    if (size == 0) {
        size_t v = s.Load(4); // frame0
        if (rec != nullptr) {
            rec->deltaQuatData = v;
        }
        return;
    }
    size_t framesPtr = s.Load(4);
    size_t indices = s.Load(bigFrames ? 2u * (size + 1) : size + 1u);
    size_t data = Follow(framesPtr, 0, kAlign4, 4u * (size + 1));
    if (rec != nullptr) {
        rec->deltaQuatIndices = indices;
        rec->deltaQuatData = data;
    }
}

std::string Walker::XAnimParts(size_t h, AnimRecord& rec) {
    s.Push(4);
    std::string name = XStr(h, 0);
    uint16_t numFrames = U16(h, 14);
    bool bigFrames = numFrames >= 256;
    rec.names = Follow(h, 64, kAlign2, 2u * U8(h, 24 + 9));
    rec.notify = Follow(h, 96, kAlign4, 8u * U8(h, 34));
    if (U32(h, 100) != 0) {
        s.Align(kAlign4);
        size_t delta = s.Load(8);
        if (U32(delta, 0) != 0) {
            s.Align(kAlign4);
            rec.deltaTrans = s.Load(4);
            XAnimPartTrans(rec.deltaTrans, bigFrames, &rec);
        }
        if (U32(delta, 4) != 0) {
            s.Align(kAlign4);
            rec.deltaQuat = s.Load(4);
            XAnimDeltaQuat(rec.deltaQuat, bigFrames, &rec);
        }
    }
    rec.dataByte = Follow(h, 68, kAlign1, U16(h, 4));
    rec.dataShort = Follow(h, 72, kAlign2, 2u * U16(h, 6));
    rec.dataInt = Follow(h, 76, kAlign4, 4u * U16(h, 8));
    rec.randomDataShort = Follow(h, 80, kAlign2, 2u * U32(h, 40));
    rec.randomDataByte = Follow(h, 84, kAlign1, U16(h, 10));
    rec.randomDataInt = Follow(h, 88, kAlign4, 4u * U16(h, 12));
    uint32_t indexCount = U32(h, 44);
    rec.indices = bigFrames ? Follow(h, 92, kAlign2, 2u * indexCount) : Follow(h, 92, kAlign1, indexCount);
    s.Pop();
    return name;
}

// ---------------------------------------------------------------- models

void Walker::XSurfaces(size_t arr, int count, ModelRecord& rec) {
    for (int i = 0; i < count; i++) {
        size_t surf = arr + i * 68;
        SurfaceRecord sr;
        sr.header = surf;
        uint16_t vertCount = U16(surf, 4);
        uint16_t triCount = U16(surf, 6);
        uint8_t vertListCount = U8(surf, 1);
        // XSurfaceVertexInfo at +16: vertCount[4], vertsBlend, tensionData.
        uint32_t c0 = U16(surf, 16), c1 = U16(surf, 18), c2 = U16(surf, 20), c3 = U16(surf, 22);
        sr.blend = FollowOrRef(surf, 24, kAlign2, 2u * (c0 + 3 * c1 + 5 * c2 + 7 * c3));
        FollowOrRef(surf, 28, kAlign4, 48u * (c0 + c1 + c2 + c3)); // tensionData
        sr.verts = FollowOrRef(surf, 32, kAlign16, 32u * vertCount);
        sr.vertList = FollowOrRef(surf, 40, kAlign4, 12u * vertListCount);
        if (IsInline(surf, 40)) {
            for (int v = 0; v < vertListCount; v++) {
                size_t list = sr.vertList + v * 12;
                size_t tree = FollowOrRef(list, 8, kAlign4, 40);
                if (IsInline(list, 8)) {
                    Follow(tree, 28, kAlign16, 16u * U32(tree, 24)); // nodes
                    Follow(tree, 36, kAlign2, 2u * U32(tree, 32));   // leafs
                }
            }
        }
        sr.triIndices = FollowOrRef(surf, 12, kAlign16, 6u * triCount);
        rec.surfaces.push_back(sr);
    }
}

void Walker::Collmap(size_t c) {
    size_t list = Follow(c, 0, kAlign4, 12); // PhysGeomList
    if (list == kNoData && U32(c, 0) == 0) {
        return;
    }
    uint32_t count = U32(list, 0);
    size_t geoms = Follow(list, 4, kAlign16, 68u * count);
    for (uint32_t g = 0; geoms != kNoData && g < count; g++) {
        size_t info = geoms + g * 68;
        size_t brush = FollowOrRef(info, 0, kAlign16, 96);
        if (!IsInline(info, 0)) {
            continue;
        }
        uint32_t numSides = U32(brush, 28);
        size_t sides = Follow(brush, 32, kAlign4, 12u * numSides);
        for (uint32_t i = 0; sides != kNoData && i < numSides; i++) {
            FollowOrRef(sides + i * 12, 0, kAlign4, 20); // plane
        }
        FollowOrRef(brush, 88, kAlign4, 12u * U32(brush, 84)); // verts
        FollowOrRef(brush, 92, kAlign4, 20u * numSides);       // planes
    }
}

std::string Walker::XModel(size_t h, ModelRecord& rec) {
    s.Push(4);
    std::string name = XStr(h, 0);
    uint32_t numBones = U8(h, 4);
    uint32_t numRoot = U8(h, 5);
    uint32_t numSurfs = U8(h, 6);
    uint32_t nonRoot = numBones - numRoot;
    rec.boneNames = FollowOrRef(h, 8, kAlign2, 2u * numBones);
    rec.parentList = FollowOrRef(h, 12, kAlign1, nonRoot);
    rec.quats = FollowOrRef(h, 16, kAlign2, 8u * nonRoot);
    rec.trans = FollowOrRef(h, 20, kAlign4, 16u * nonRoot);
    FollowOrRef(h, 24, kAlign1, numBones); // partClassification
    rec.baseMat = FollowOrRef(h, 28, kAlign4, 32u * numBones);
    if (U32(h, 32) != 0) {
        s.Align(kAlign4);
        size_t surfs = s.Load(68u * numSurfs);
        XSurfaces(surfs, static_cast<int>(numSurfs), rec);
    }
    if (U32(h, 36) != 0) {
        s.Align(kAlign4);
        size_t handles = s.Load(4u * numSurfs);
        for (uint32_t i = 0; i < numSurfs; i++) {
            rec.materials.push_back(AssetName(AssetPtr(handles, i * 4, 0x06)));
        }
    }
    uint32_t numCollSurfs = U32(h, 176);
    size_t collSurfs = Follow(h, 172, kAlign4, 44u * numCollSurfs);
    for (uint32_t i = 0; collSurfs != kNoData && i < numCollSurfs; i++) {
        size_t cs = collSurfs + i * 44;
        Follow(cs, 0, kAlign4, 48u * U32(cs, 4));
    }
    Follow(h, 184, kAlign4, 44u * numBones); // boneInfo
    // streamInfo at +220: highMipBounds
    Follow(h, 220, kAlign4, 16u * numSurfs);
    AssetPtr(h, 236, 0x01); // physPreset
    uint32_t numCollmaps = U8(h, 240);
    size_t collmaps = Follow(h, 244, kAlign4, 4u * numCollmaps);
    for (uint32_t i = 0; collmaps != kNoData && i < numCollmaps; i++) {
        Collmap(collmaps + i * 4);
    }
    AssetPtr(h, 248, 0x02); // physConstraints
    s.Pop();
    return name;
}

// ---------------------------------------------------------------- materials and images

void Walker::Technique(size_t at, int off) {
    uint32_t v = U32(at, off);
    if (v != kPtrInline) {
        return; // null, or a technique loaded earlier
    }
    s.Align(kAlign4);
    size_t tech = s.Load(8);
    uint16_t passCount = U16(tech, 6);
    size_t passes = s.Load(20u * passCount);
    for (uint16_t p = 0; p < passCount; p++) {
        size_t pass = passes + p * 20;
        FollowOrRef(pass, 0, kAlign4, 108); // vertex declaration
        for (int shaderField = 4; shaderField <= 8; shaderField += 4) {
            if (!IsInline(pass, shaderField)) {
                continue;
            }
            s.Align(kAlign4);
            size_t shader = s.Load(16);
            XStr(shader, 0);
            Follow(shader, 8, kAlign4, 4u * U16(shader, 12)); // program
        }
        uint32_t argCount = U8(pass, 12) + U8(pass, 13) + U8(pass, 14);
        size_t args = Follow(pass, 16, kAlign4, 8u * argCount);
        for (uint32_t a = 0; args != kNoData && a < argCount; a++) {
            size_t arg = args + a * 8;
            uint16_t type = U16(arg, 0);
            if (type == 1 || type == 7) {
                FollowOrRef(arg, 4, kAlign4, 16); // literal constant
            }
        }
    }
    XStr(tech, 0);
}

std::string Walker::TechniqueSet(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    for (int i = 0; i < 130; i++) {
        Technique(h, 8 + i * 4);
    }
    s.Pop();
    return name;
}

std::string Walker::Material(size_t h, MaterialRecord& rec) {
    s.Push(4);
    std::string name = XStr(h, 0);
    rec.techset = AssetName(AssetPtr(h, 176, 0x07));
    uint8_t textureCount = U8(h, 170);
    size_t table = FollowOrRef(h, 180, kAlign4, 16u * textureCount);
    if (IsInline(h, 180)) {
        for (uint8_t i = 0; i < textureCount; i++) {
            size_t def = table + i * 16;
            MaterialTexture tex;
            tex.nameHash = U32(def, 0);
            tex.semantic = U8(def, 7);
            if (tex.semantic == 11) {
                // water: generated at runtime from a few parameters and an image
                if (IsInline(def, 12)) {
                    s.Align(kAlign4);
                    size_t water = s.Load(68);
                    uint32_t cells = U32(water, 12) * U32(water, 16);
                    Follow(water, 4, kAlign4, 8u * cells);
                    Follow(water, 8, kAlign4, 4u * cells);
                    tex.image = AssetName(AssetPtr(water, 64, 0x08));
                }
            } else {
                tex.image = AssetName(AssetPtr(def, 12, 0x08));
            }
            rec.textures.push_back(tex);
        }
    }
    FollowOrRef(h, 184, kAlign16, 32u * U8(h, 171)); // constants
    FollowOrRef(h, 188, kAlign4, 8u * U8(h, 172));   // state bits
    s.Pop();
    return name;
}

std::string Walker::Image(size_t h, ImageRecord& rec) {
    s.Push(4);
    std::string name = XStr(h, 44);
    // texture.loadDef: its own small asset-like pointer, read into the temp block.
    uint32_t v = U32(h, 0);
    s.Push(0);
    if (v == kPtrInline || v == kPtrInsert) {
        s.Align(kAlign4);
        if (v == kPtrInsert) {
            s.InsertPointer();
        }
        rec.loadDef = s.Load(12);
        rec.format = U32(rec.loadDef, 4);
        rec.resourceSize = U32(rec.loadDef, 8);
        s.Load(rec.resourceSize);
    }
    s.Pop();
    s.Pop();
    return name;
}

// ---------------------------------------------------------------- sound

void Walker::SoundFile(size_t sf, SoundAliasRecord& rec) {
    uint8_t type = U8(sf, 4);
    if (!IsInline(sf, 0)) {
        return;
    }
    s.Align(kAlign4);
    if (type == 1) {
        size_t loaded = s.Load(60); // LoadedSound: name, snd_asset
        rec.loaded = true;
        rec.file = XStr(loaded, 0);
        rec.asset = loaded + 4;
        Follow(loaded, 48, kAlign4, 4u * U32(loaded, 44)); // seek table
        s.Push(5);
        s.Push(6);
        rec.dataSize = U32(loaded, 52);
        rec.data = Follow(loaded, 56, kAlign2048, rec.dataSize);
        s.Pop();
        s.Pop();
    } else {
        size_t streamed = s.Load(8); // StreamedSound: filename, primeSnd
        rec.file = XStr(streamed, 0);
        if (IsInline(streamed, 4)) {
            s.Align(kAlign4);
            size_t primed = s.Load(12);
            XStr(primed, 0);
            s.Push(5);
            Follow(primed, 4, kAlign2048, U32(primed, 8));
            s.Pop();
        }
    }
}

std::string Walker::SndBank(size_t h, SoundBankRecord& rec) {
    s.Push(4);
    std::string name = XStr(h, 0);
    uint32_t aliasCount = U32(h, 4);
    size_t lists = Follow(h, 8, kAlign4, 20u * aliasCount);
    for (uint32_t i = 0; lists != kNoData && i < aliasCount; i++) {
        size_t list = lists + i * 20;
        std::string listName = XStr(list, 0);
        int count = static_cast<int>(U32(list, 12));
        size_t head = FollowOrRef(list, 8, kAlign4, 84u * count);
        if (!IsInline(list, 8)) {
            continue;
        }
        for (int a = 0; a < count; a++) {
            size_t alias = head + a * 84;
            SoundAliasRecord ar;
            ar.name = XStr(alias, 0);
            XStr(alias, 8);  // subtitle
            XStr(alias, 12); // secondaryname
            if (IsInline(alias, 16)) {
                s.Align(kAlign4);
                size_t sf = s.Load(8);
                SoundFile(sf, ar);
                mSoundFiles[sf] = ar;
            } else if (U32(alias, 16) != 0) {
                // Shares a sound file loaded by an earlier alias.
                auto it = mSoundFiles.find(s.Resolve(U32(alias, 16)));
                if (it != mSoundFiles.end()) {
                    std::string name = ar.name;
                    ar = it->second;
                    ar.name = name;
                    ar.shared = true;
                }
            }
            rec.aliases.push_back(ar);
        }
    }
    Follow(h, 12, kAlign4, 4u * aliasCount);     // aliasIndex
    Follow(h, 28, kAlign4, 96u * U32(h, 24));    // radverbs
    Follow(h, 36, kAlign4, 0x15Cu * U32(h, 32)); // snapshots
    s.Pop();
    return name;
}

// ---------------------------------------------------------------- effects

void Walker::FxElemVisuals(size_t v, uint8_t elemType) {
    switch (elemType) {
    case 7: ModelPtr(v, 0); break;
    case 12: XStr(v, 0); break; // effect by name
    case 10: XStr(v, 0); break; // sound name
    case 8:
    case 9: break;              // lights
    default: MaterialPtr(v, 0); break;
    }
}

void Walker::FxElemDef(size_t d) {
    uint8_t elemType = U8(d, 184);
    uint8_t visualCount = U8(d, 185);
    Follow(d, 188, kAlign4, 96u * (U8(d, 186) + 1)); // velSamples
    Follow(d, 192, kAlign4, 48u * (U8(d, 187) + 1)); // visSamples
    if (elemType == 11) {
        size_t marks = Follow(d, 196, kAlign4, 8u * visualCount);
        for (uint8_t i = 0; marks != kNoData && i < visualCount; i++) {
            MaterialPtr(marks + i * 8, 0);
            MaterialPtr(marks + i * 8, 4);
        }
    } else if (visualCount > 1) {
        size_t arr = Follow(d, 196, kAlign4, 4u * visualCount);
        for (uint8_t i = 0; arr != kNoData && i < visualCount; i++) {
            FxElemVisuals(arr + i * 4, elemType);
        }
    } else {
        FxElemVisuals(d + 196, elemType);
    }
    XStr(d, 224); // effectOnImpact
    XStr(d, 228); // effectOnDeath
    XStr(d, 232); // effectEmitted
    XStr(d, 252); // effectAttached
    size_t trail = Follow(d, 256, kAlign4, 28);
    if (trail != kNoData) {
        Follow(trail, 16, kAlign4, 20u * U32(trail, 12));
        Follow(trail, 24, kAlign2, 2u * U32(trail, 20));
    }
    XStr(d, 280); // spawnSound
}

std::string Walker::FxEffectDef(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    uint32_t count = U32(h, 16) + U32(h, 20) + U32(h, 24);
    size_t elems = Follow(h, 28, kAlign4, 292u * count);
    for (uint32_t i = 0; elems != kNoData && i < count; i++) {
        FxElemDef(elems + i * 292);
    }
    s.Pop();
    return name;
}

std::string Walker::ImpactFx(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    size_t table = Follow(h, 4, kAlign4, 140u * 21);
    for (int i = 0; table != kNoData && i < 21; i++) {
        for (int e = 0; e < 35; e++) {
            FxPtr(table + i * 140, e * 4);
        }
    }
    s.Pop();
    return name;
}

// ---------------------------------------------------------------- weapons

void Walker::FlameTable(size_t at, int off) {
    size_t t = FollowOrRef(at, off, kAlign4, 476);
    if (!IsInline(at, off)) {
        return;
    }
    XStr(t, 424);
    for (int i = 0; i < 8; i++) {
        MaterialPtr(t, 428 + i * 4);
    }
    XStrings(t, 460, 4);
}

void Walker::WeaponDef(size_t w) {
    XStr(w, 0); // szOverlayName
    size_t gun = FollowOrRef(w, 4, kAlign4, 4 * 16);
    if (IsInline(w, 4)) {
        for (int i = 0; i < 16; i++) {
            ModelPtr(gun, i * 4);
        }
    }
    ModelPtr(w, 8); // handXModel
    XStr(w, 12);    // szModeName
    FollowOrRef(w, 16, kAlign2, 2 * 20); // notetrackSoundMapKeys
    FollowOrRef(w, 20, kAlign2, 2 * 20); // notetrackSoundMapValues
    XStr(w, 60);    // parentWeaponName
    FxPtr(w, 116);  // viewFlashEffect
    FxPtr(w, 120);  // worldFlashEffect
    XStrings(w, 124, 62); // pickupSound .. adsZoomSound
    XStrArrayPtr(w, 372, 31); // bounceSound
    XStrings(w, 376, 3);      // stand/crouch/proneMountedWeapdef
    for (int i = 0; i < 4; i++) {
        FxPtr(w, 400 + i * 4); // shell eject effects
    }
    MaterialPtr(w, 416); // reticleCenter
    MaterialPtr(w, 420); // reticleSide
    size_t world = FollowOrRef(w, 780, kAlign4, 4 * 16);
    if (IsInline(w, 780)) {
        for (int i = 0; i < 16; i++) {
            ModelPtr(world, i * 4);
        }
    }
    for (int i = 0; i < 4; i++) {
        ModelPtr(w, 784 + i * 4); // worldClip, rocket, mounted, additionalMelee
    }
    MaterialPtr(w, 800);  // hudIcon
    MaterialPtr(w, 816);  // ammoCounterIcon
    XStr(w, 844);         // szSharedAmmoCapName
    XStrings(w, 916, 6);  // spin sounds
    XStr(w, 1164);        // stackSound
    MaterialPtr(w, 1400); // killIcon
    MaterialPtr(w, 808);  // indicatorIcon
    XStrings(w, 1420, 2); // szSpawnedGrenadeWeaponName, szDualWieldWeaponName
    ModelPtr(w, 1512);    // projectileModel
    for (int i = 0; i < 6; i++) {
        FxPtr(w, 1520 + i * 8); // projExplosionEffect 1-5, projDudEffect
    }
    XStrings(w, 1564, 4); // projExplosionSound .. tankShellSound
    FollowOrRef(w, 1616, kAlign4, 4 * 31); // parallelBounce
    FollowOrRef(w, 1620, kAlign4, 4 * 31); // perpendicularBounce
    FxPtr(w, 1624);       // projTrailEffect
    FxPtr(w, 1652);       // projIgnitionEffect
    XStr(w, 1656);        // projIgnitionSound
    uint32_t knots0 = U32(w, 1836), knots1 = U32(w, 1840);
    XStr(w, 1812); // accuracyGraphName[0]
    FollowOrRef(w, 1820, kAlign4, 8u * knots0);
    FollowOrRef(w, 1828, kAlign4, 8u * knots0);
    XStr(w, 1816); // accuracyGraphName[1]
    FollowOrRef(w, 1824, kAlign4, 8u * knots1);
    FollowOrRef(w, 1832, kAlign4, 8u * knots1);
    XStrings(w, 1924, 2); // szUseHintString, dropHintString
    XStr(w, 1948);        // szScript
    FollowOrRef(w, 1980, kAlign4, 4 * 19); // locationDamageMultipliers
    XStrings(w, 1984, 3); // rumbles
    XStrings(w, 2024, 2); // flame table names
    FlameTable(w, 2032);
    FlameTable(w, 2036);
    FxPtr(w, 2040); // tagFx_preparationEffect
    FxPtr(w, 2044); // tagFlash_preparationEffect
}

std::string Walker::WeaponVariant(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0); // szInternalName
    size_t def = FollowOrRef(h, 8, kAlign4, 2056);
    if (IsInline(h, 8)) {
        WeaponDef(def);
    }
    XStr(h, 12); // szDisplayName
    XStr(h, 20); // szAltWeaponName
    XStrArrayPtr(h, 16, 66);              // szXAnims
    FollowOrRef(h, 24, kAlign2, 2 * 32); // hideTags
    XStr(h, 64);                          // szAmmoName
    XStr(h, 72);                          // szClipName
    MaterialPtr(h, 140);                  // overlayMaterial
    MaterialPtr(h, 144);                  // overlayMaterialLowRes
    MaterialPtr(h, 148);                  // dpadIcon
    s.Pop();
    return name;
}

// ---------------------------------------------------------------- menus

void Walker::ExpressionStatement(size_t e) {
    XStr(e, 0); // filename
    uint32_t numRpn = U32(e, 8);
    size_t rpn = FollowOrRef(e, 12, kAlign4, 12u * numRpn);
    if (!IsInline(e, 12)) {
        return;
    }
    for (uint32_t i = 0; i < numRpn; i++) {
        size_t r = rpn + i * 12;
        // type 0: a constant operand {dataType, value}; string operands carry a string.
        if (U32(r, 0) == 0 && U32(r, 4) == 2) {
            XStr(r, 8);
        }
    }
}

void Walker::ScriptCondition(size_t at, int off) {
    // A linked list: each node is 16 bytes with `next` at +12.
    while (U32(at, off) != 0) {
        s.Align(kAlign4);
        at = s.Load(16);
        off = 12;
    }
}

void Walker::GenericEventScript(size_t at, int off) {
    while (U32(at, off) != 0) {
        s.Align(kAlign4);
        size_t script = s.Load(44);
        ScriptCondition(script, 0); // prerequisites
        ExpressionStatement(script + 4);
        XStr(script, 28); // action
        at = script;
        off = 40; // next
    }
}

void Walker::GenericEventHandler(size_t at, int off) {
    while (U32(at, off) != 0) {
        s.Align(kAlign4);
        size_t handler = s.Load(12);
        XStr(handler, 0);
        GenericEventScript(handler, 4);
        at = handler;
        off = 8; // next
    }
}

void Walker::ItemKeyHandler(size_t at, int off) {
    while (U32(at, off) != 0) {
        s.Align(kAlign4);
        size_t handler = s.Load(12);
        GenericEventScript(handler, 4);
        at = handler;
        off = 8; // next
    }
}

std::string Walker::Window(size_t w) {
    std::string name = XStr(w, 0);
    XStr(w, 52); // group
    MaterialPtr(w, 160);
    return name;
}

void Walker::ListBox(size_t l) {
    MaterialPtr(l, 640);
    MaterialPtr(l, 644);
    MaterialPtr(l, 648);
    uint32_t maxRows = U32(l, 660);
    uint32_t columns = U32(l, 28);
    size_t rows = Follow(l, 656, kAlign4, 24u * maxRows);
    for (uint32_t r = 0; rows != kNoData && r < maxRows; r++) {
        size_t row = rows + r * 24;
        size_t cells = Follow(row, 0, kAlign4, 12u * columns);
        for (uint32_t c = 0; cells != kNoData && c < columns; c++) {
            Follow(cells + c * 12, 8, kAlign1, U32(cells + c * 12, 4));
        }
        Follow(row, 4, kAlign1, 32);
        Follow(row, 8, kAlign1, 32);
    }
}

static bool ItemHasFocus(int type) {
    switch (type) {
    case 3: case 4: case 5: case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14:
    case 16: case 20: case 21: case 22: case 30:
        return true;
    default:
        return false;
    }
}

void Walker::FocusItemDef(size_t f, int type) {
    XStrings(f, 0, 4); // mouseEnterText, mouseExitText, mouseEnter, mouseExit
    ItemKeyHandler(f, 16);
    // focusTypeData at +20
    switch (type) {
    case 4: {
        size_t l = Follow(f, 20, kAlign4, 668);
        if (l != kNoData) {
            ListBox(l);
        }
        break;
    }
    case 10: {
        size_t m = Follow(f, 20, kAlign4, 396);
        if (m != kNoData) {
            XStrings(m, 0, 32);   // dvarList
            XStrings(m, 128, 32); // dvarStr
        }
        break;
    }
    case 5: case 7: case 8: case 9: case 12: case 13: case 14: case 16: case 22: case 30:
        Follow(f, 20, kAlign4, 36); // editFieldDef
        break;
    case 11: {
        size_t e = Follow(f, 20, kAlign4, 4);
        if (e != kNoData) {
            XStr(e, 0);
        }
        break;
    }
    default:
        break;
    }
}

void Walker::TextDef(size_t t, int type) {
    XStr(t, 56); // text
    size_t exp = Follow(t, 60, kAlign4, 16);
    if (exp != kNoData) {
        ExpressionStatement(exp);
    }
    if (ItemHasFocus(type)) {
        size_t f = Follow(t, 64, kAlign4, 24);
        if (f != kNoData) {
            FocusItemDef(f, type);
        }
    } else if (type == 15) {
        Follow(t, 64, kAlign4, 8); // gameMsgDef
    }
}

void Walker::ItemDefData(size_t at, int off, int type) {
    switch (type) {
    case 1: case 3: case 4: case 5: case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14:
    case 15: case 16: case 18: case 20: case 22: {
        size_t t = Follow(at, off, kAlign4, 68);
        if (t != kNoData) {
            TextDef(t, type);
        }
        break;
    }
    case 2:
    case 6: {
        size_t d = Follow(at, off, kAlign4, 16); // imageDef / ownerDrawDef
        if (d != kNoData) {
            ExpressionStatement(d);
        }
        break;
    }
    case 19:
    case 21: {
        size_t f = Follow(at, off, kAlign4, 24);
        if (f != kNoData) {
            FocusItemDef(f, type);
        }
        break;
    }
    default:
        break;
    }
}

void Walker::ItemDef(size_t it) {
    Window(it);
    XStrings(it, 176, 3); // dvar, dvarTest, enableDvar
    int type = static_cast<int>(U32(it, 164));
    ItemDefData(it, 192, type);
    size_t rect = Follow(it, 200, kAlign4, 64);
    for (int i = 0; rect != kNoData && i < 4; i++) {
        ExpressionStatement(rect + i * 16);
    }
    ExpressionStatement(it + 204); // visibleExp
    ExpressionStatement(it + 240); // forecolorAExp
    GenericEventHandler(it, 260);
    size_t anim = Follow(it, 264, kAlign4, 236);
    if (anim != kNoData) {
        uint32_t stateCount = U32(anim, 0);
        size_t states = Follow(anim, 4, kAlign4, 4u * stateCount);
        for (uint32_t i = 0; states != kNoData && i < stateCount; i++) {
            size_t st = Follow(states, i * 4, kAlign4, 108);
            if (st != kNoData) {
                XStr(st, 0);
                GenericEventHandler(st, 104);
            }
        }
    }
}

std::string Walker::Menu(size_t h) {
    s.Push(4);
    std::string name = Window(h);
    XStr(h, 164); // font
    GenericEventHandler(h, 268);
    ItemKeyHandler(h, 272);
    ExpressionStatement(h + 276); // visibleExp
    XStr(h, 312);                 // allowedBinding
    XStr(h, 316);                 // soundName
    ExpressionStatement(h + 360); // rectXExp
    ExpressionStatement(h + 376); // rectYExp
    uint32_t itemCount = U32(h, 176);
    size_t items = Follow(h, 392, kAlign4, 4u * itemCount);
    for (uint32_t i = 0; items != kNoData && i < itemCount; i++) {
        size_t it = Follow(items, i * 4, kAlign8, 272);
        if (it != kNoData) {
            ItemDef(it);
        }
    }
    s.Pop();
    return name;
}

std::string Walker::MenuList(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    uint32_t count = U32(h, 4);
    size_t menus = Follow(h, 8, kAlign4, 4u * count);
    for (uint32_t i = 0; menus != kNoData && i < count; i++) {
        AssetPtr(menus, i * 4, 0x16);
    }
    s.Pop();
    return name;
}

// ---------------------------------------------------------------- small assets

std::string Walker::Localize(size_t h) {
    s.Push(4);
    XStr(h, 0); // value
    std::string name = XStr(h, 4);
    s.Pop();
    return name;
}

std::string Walker::RawFile(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    Follow(h, 8, kAlign1, U32(h, 4) + 1u);
    s.Pop();
    return name;
}

std::string Walker::StringTable(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    uint32_t cells = U32(h, 4) * U32(h, 8);
    size_t values = Follow(h, 12, kAlign4, 8u * cells);
    for (uint32_t i = 0; values != kNoData && i < cells; i++) {
        XStr(values + i * 8, 0);
    }
    Follow(h, 16, kAlign2, 2u * cells);
    s.Pop();
    return name;
}

std::string Walker::MapEnts(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    Follow(h, 4, kAlign1, U32(h, 8));
    s.Pop();
    return name;
}

std::string Walker::LightDef(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    ImagePtr(h, 4); // attenuation.image
    s.Pop();
    return name;
}

std::string Walker::ComWorld(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    uint32_t lights = U32(h, 8);
    size_t prim = Follow(h, 12, kAlign4, 220u * lights);
    for (uint32_t i = 0; prim != kNoData && i < lights; i++) {
        XStr(prim + i * 220, 216); // defName
    }
    Follow(h, 36, kAlign4, 8u * U32(h, 32)); // water cells
    uint32_t burnables = U32(h, 56);
    size_t cells = Follow(h, 60, kAlign4, 12u * burnables);
    for (uint32_t i = 0; cells != kNoData && i < burnables; i++) {
        Follow(cells + i * 12, 8, kAlign1, 32);
    }
    s.Pop();
    return name;
}

std::string Walker::GameWorld(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    size_t path = h + 4; // PathData, 40 bytes
    uint32_t nodeCount = U32(path, 0);
    size_t nodes = Follow(path, 4, kAlign4, 128u * (nodeCount + 128));
    for (uint32_t i = 0; nodes != kNoData && i < nodeCount + 128; i++) {
        size_t node = nodes + i * 128; // pathnode_constant_t at +0
        Follow(node, 64, kAlign4, 12u * U16(node, 62));
    }
    s.Push(1);
    Follow(path, 8, kAlign16, 16u * (nodeCount + 128)); // basenodes (runtime)
    s.Pop();
    Follow(path, 16, kAlign2, 2u * nodeCount); // chainNodeForNode
    Follow(path, 20, kAlign2, 2u * nodeCount); // nodeForChainNode
    Follow(path, 28, kAlign1, U32(path, 24));  // pathVis
    uint32_t treeCount = U32(path, 32);
    size_t trees = Follow(path, 36, kAlign4, 16u * treeCount);
    std::function<void(size_t)> tree = [&](size_t t) {
        if (static_cast<int32_t>(U32(t, 0)) < 0) {
            Follow(t, 12, kAlign2, 2u * U32(t, 8));
        } else {
            for (int c = 0; c < 2; c++) {
                size_t child = FollowOrRef(t, 8 + c * 4, kAlign4, 16);
                if (IsInline(t, 8 + c * 4)) {
                    tree(child);
                }
            }
        }
    };
    for (uint32_t i = 0; trees != kNoData && i < treeCount; i++) {
        tree(trees + i * 16);
    }
    s.Pop();
    return name;
}

void Walker::Brush(size_t b) {
    size_t side = FollowOrRef(b, 32, kAlign4, 12);
    if (IsInline(b, 32)) {
        FollowOrRef(side, 0, kAlign4, 20);
    }
    FollowOrRef(b, 88, kAlign4, 12);
}

void Walker::DynEntityDefs(size_t at, int off, int count) {
    size_t defs = Follow(at, off, kAlign4, 84u * count);
    for (int i = 0; defs != kNoData && i < count; i++) {
        size_t d = defs + i * 84;
        ModelPtr(d, 32);
        ModelPtr(d, 36);
        FxPtr(d, 44);
        size_t pieces = FollowOrRef(d, 52, kAlign4, 12);
        if (IsInline(d, 52)) {
            XStr(pieces, 0);
            uint32_t n = U32(pieces, 4);
            size_t arr = Follow(pieces, 8, kAlign4, 16u * n);
            for (uint32_t p = 0; arr != kNoData && p < n; p++) {
                ModelPtr(arr + p * 16, 0);
            }
        }
        AssetPtr(d, 56, 0x01);
    }
}

std::string Walker::ClipMap(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    FollowOrRef(h, 12, kAlign4, 20u * U32(h, 8)); // planes
    uint32_t staticModels = U32(h, 16);
    size_t sm = Follow(h, 20, kAlign4, 80u * staticModels);
    for (uint32_t i = 0; sm != kNoData && i < staticModels; i++) {
        ModelPtr(sm + i * 80, 4);
    }
    Follow(h, 28, kAlign4, 72u * U32(h, 24)); // materials
    uint32_t sides = U32(h, 32);
    size_t bs = Follow(h, 36, kAlign4, 12u * sides);
    for (uint32_t i = 0; bs != kNoData && i < sides; i++) {
        FollowOrRef(bs + i * 12, 0, kAlign4, 20);
    }
    uint32_t nodes = U32(h, 40);
    size_t ns = Follow(h, 44, kAlign4, 8u * nodes);
    for (uint32_t i = 0; ns != kNoData && i < nodes; i++) {
        FollowOrRef(ns + i * 8, 0, kAlign4, 20);
    }
    Follow(h, 52, kAlign4, 44u * U32(h, 48));  // leafs
    Follow(h, 68, kAlign2, 2u * U32(h, 64));   // leafbrushes
    uint32_t lbn = U32(h, 56);
    size_t lb = Follow(h, 60, kAlign4, 20u * lbn);
    for (uint32_t i = 0; lb != kNoData && i < lbn; i++) {
        size_t node = lb + i * 20;
        int16_t leafBrushCount = static_cast<int16_t>(U16(node, 2));
        if (leafBrushCount > 0) {
            FollowOrRef(node, 8, kAlign2, 2u * leafBrushCount);
        }
    }
    Follow(h, 76, kAlign4, 4u * U32(h, 72));   // leafsurfaces
    Follow(h, 84, kAlign4, 12u * U32(h, 80));  // verts
    Follow(h, 92, kAlign4, 12u * U32(h, 88));  // brushVerts
    Follow(h, 100, kAlign2, 2u * U32(h, 96));  // uinds
    uint32_t triCount = U32(h, 104);
    Follow(h, 108, kAlign2, 6u * triCount);    // triIndices
    Follow(h, 112, kAlign1, 4u * ((3 * triCount + 31) >> 5)); // triEdgeIsWalkable
    Follow(h, 120, kAlign4, 28u * U32(h, 116)); // borders
    uint32_t partitions = U32(h, 124);
    size_t ps = Follow(h, 128, kAlign4, 20u * partitions);
    for (uint32_t i = 0; ps != kNoData && i < partitions; i++) {
        FollowOrRef(ps + i * 20, 16, kAlign4, 28);
    }
    Follow(h, 136, kAlign16, 32u * U32(h, 132)); // aabbTrees
    Follow(h, 144, kAlign4, 72u * U32(h, 140));  // cmodels
    uint32_t brushes = U16(h, 148);
    size_t br = Follow(h, 152, kAlign16, 96u * brushes);
    for (uint32_t i = 0; br != kNoData && i < brushes; i++) {
        Brush(br + i * 96);
    }
    Follow(h, 164, kAlign1, U32(h, 156) * U32(h, 160)); // visibility
    AssetPtr(h, 172, 0x10);                             // mapEnts
    size_t box = FollowOrRef(h, 176, kAlign16, 96);
    if (IsInline(h, 176)) {
        Brush(box);
    }
    uint32_t dynCount[4] = { U16(h, 254), U16(h, 256), U16(h, 258), U16(h, 260) };
    DynEntityDefs(h, 264, static_cast<int>(dynCount[0]));
    DynEntityDefs(h, 268, static_cast<int>(dynCount[1]));
    // Poses, client and server state and collision live in the runtime block.
    const int runtimeFields[] = { 272, 276, 280, 284, 288, 292, 296, 300, 304, 308 };
    for (int f : runtimeFields) {
        s.Push(1);
        Follow(h, f, kAlign4, 0);
        s.Pop();
    }
    uint32_t constraints = U32(h, 312);
    size_t cs = Follow(h, 316, kAlign4, 168u * constraints);
    for (uint32_t i = 0; cs != kNoData && i < constraints; i++) {
        PhysConstraint(cs + i * 168);
    }
    s.Push(1);
    Follow(h, 324, kAlign4, 3188u * U32(h, 320)); // ropes
    s.Pop();
    s.Pop();
    return name;
}

void Walker::GfxPortal(size_t p) {
    size_t cell = FollowOrRef(p, 32, kAlign4, 56);
    if (IsInline(p, 32)) {
        GfxCell(cell);
    }
    Follow(p, 36, kAlign4, 12u * U8(p, 40));
}

void Walker::GfxCell(size_t c) {
    uint32_t trees = U32(c, 24);
    size_t ts = Follow(c, 28, kAlign4, 40u * trees);
    for (uint32_t i = 0; ts != kNoData && i < trees; i++) {
        size_t t = ts + i * 40;
        FollowOrRef(t, 32, kAlign2, 2u * U16(t, 30));
    }
    uint32_t portals = U32(c, 32);
    size_t ps = Follow(c, 36, kAlign4, 68u * portals);
    for (uint32_t i = 0; ps != kNoData && i < portals; i++) {
        GfxPortal(ps + i * 68);
    }
    Follow(c, 44, kAlign4, 4u * U32(c, 40));
    Follow(c, 52, kAlign1, U8(c, 48));
}

std::string Walker::GfxWorld(size_t h) {
    s.Push(4);
    std::string name = XStr(h, 0);
    XStr(h, 4); // baseName
    int32_t planeCount = static_cast<int32_t>(U32(h, 8));
    int32_t nodeCount = static_cast<int32_t>(U32(h, 12));
    int32_t surfaceCount = static_cast<int32_t>(U32(h, 16));
    // streamInfo at +20
    Follow(h, 24, kAlign4, 32u * U32(h, 20));
    Follow(h, 32, kAlign4, 4u * U32(h, 28));
    Follow(h, 40, kAlign4, 4u * U32(h, 36)); // skyStartSurfs
    ImagePtr(h, 44);
    XStr(h, 52); // skyBoxModel
    size_t sun = FollowOrRef(h, 236, kAlign16, 368);
    if (IsInline(h, 236)) {
        AssetPtr(sun, 352, 0x12);
    }
    Follow(h, 268, kAlign4, 32u * U32(h, 264)); // coronas
    Follow(h, 276, kAlign4, 16u * U32(h, 272)); // shadowMapVolumes
    Follow(h, 284, kAlign4, 16u * U32(h, 280)); // shadowMapVolumePlanes
    Follow(h, 292, kAlign4, 24u * U32(h, 288)); // exposureVolumes
    Follow(h, 300, kAlign4, 16u * U32(h, 296)); // exposureVolumePlanes
    // dpvsPlanes at +320
    size_t planes = h + 320;
    int32_t cellCount = static_cast<int32_t>(U32(planes, 0));
    FollowOrRef(planes, 4, kAlign4, 20u * planeCount);
    Follow(planes, 8, kAlign2, 2u * nodeCount);
    s.Push(1);
    Follow(planes, 12, kAlign4, 4u * (cellCount << 9));
    s.Pop();
    size_t cells = Follow(h, 340, kAlign4, 56u * cellCount);
    for (int32_t i = 0; cells != kNoData && i < cellCount; i++) {
        GfxCell(cells + i * 56);
    }
    // draw at +344
    size_t draw = h + 344;
    uint32_t probes = U32(draw, 0);
    size_t ps = Follow(draw, 4, kAlign4, 24u * probes);
    for (uint32_t i = 0; ps != kNoData && i < probes; i++) {
        ImagePtr(ps + i * 24, 12);
        Follow(ps + i * 24, 16, kAlign4, 96u * U32(ps + i * 24, 20));
    }
    s.Push(1);
    Follow(draw, 8, kAlign4, 4u * probes);
    s.Pop();
    uint32_t lightmaps = U32(draw, 12);
    size_t lm = Follow(draw, 16, kAlign4, 12u * lightmaps);
    for (uint32_t i = 0; lm != kNoData && i < lightmaps; i++) {
        for (int k = 0; k < 3; k++) {
            ImagePtr(lm + i * 12, k * 4);
        }
    }
    for (int f = 20; f <= 28; f += 4) {
        s.Push(1);
        Follow(draw, f, kAlign4, 4u * lightmaps);
        s.Pop();
    }
    for (int i = 0; i < 31; i++) {
        ImagePtr(draw, 32 + i * 4); // terrainScorchImages
    }
    Follow(draw, 160, kAlign4, 44u * U32(draw, 156)); // vertices
    Follow(draw, 172, kAlign1, U32(draw, 168));       // vertex layer data
    Follow(draw, 188, kAlign2, 2u * U32(draw, 184));  // indices
    // lightGrid at +536
    size_t grid = h + 536;
    uint32_t rowAxis = U32(grid, 20);
    if (rowAxis < 3) {
        uint32_t rows = U16(grid, 14 + rowAxis * 2) - U16(grid, 8 + rowAxis * 2) + 1;
        Follow(grid, 28, kAlign2, 2u * rows);
    }
    Follow(grid, 36, kAlign4, U32(grid, 32));
    Follow(grid, 44, kAlign4, 4u * U32(grid, 40));
    Follow(grid, 52, kAlign4, 168u * U32(grid, 48));
    Follow(h, 596, kAlign4, 60u * U32(h, 592)); // models
    uint32_t memCount = U32(h, 628);
    size_t mem = Follow(h, 632, kAlign4, 8u * memCount);
    for (uint32_t i = 0; mem != kNoData && i < memCount; i++) {
        MaterialPtr(mem + i * 8, 0);
    }
    MaterialPtr(h, 636 + 4); // sun.spriteMaterial
    MaterialPtr(h, 636 + 8); // sun.flareMaterial
    ImagePtr(h, 796);        // outdoorImage
    uint32_t primaryLights = U32(h, 256);
    uint32_t sunIndex = U32(h, 252);
    size_t dyn = h + 948;
    const int runtime[] = { 800, 804, 808, 812, 816, 820, 824 };
    for (int f : runtime) {
        s.Push(1);
        Follow(h, f, kAlign4, 0);
        s.Pop();
    }
    uint32_t shadowCount = primaryLights;
    size_t sg = Follow(h, 828, kAlign4, 12u * shadowCount);
    for (uint32_t i = 0; sg != kNoData && i < shadowCount; i++) {
        size_t g = sg + i * 12;
        Follow(g, 4, kAlign2, 2u * U16(g, 0));
        Follow(g, 8, kAlign2, 2u * U16(g, 2));
    }
    size_t lr = Follow(h, 832, kAlign4, 8u * primaryLights);
    for (uint32_t i = 0; lr != kNoData && i < primaryLights; i++) {
        uint32_t hullCount = U32(lr + i * 8, 0);
        size_t hulls = Follow(lr + i * 8, 4, kAlign4, 80u * hullCount);
        for (uint32_t k = 0; hulls != kNoData && k < hullCount; k++) {
            Follow(hulls + k * 80, 76, kAlign4, 20u * U32(hulls + k * 80, 72));
        }
    }
    (void)sunIndex;
    // dpvs (static) at +836
    size_t dpvs = h + 836;
    uint32_t smodelCount = U32(dpvs, 0);
    for (int f : { 44, 48, 52, 56, 60, 64, 68, 72, 76 }) {
        s.Push(1);
        Follow(dpvs, f, kAlign4, 0);
        s.Pop();
    }
    Follow(dpvs, 80, kAlign2, 2u * U32(dpvs, 8)); // sortedSurfIndex
    Follow(dpvs, 84, kAlign4, 40u * smodelCount);  // smodelInsts
    size_t surfaces = Follow(dpvs, 88, kAlign16, 80u * surfaceCount);
    for (int32_t i = 0; surfaces != kNoData && i < surfaceCount; i++) {
        MaterialPtr(surfaces + i * 80, 48);
    }
    Follow(dpvs, 92, kAlign4, 32u * U32(h, 260)); // cullGroups
    size_t draws = Follow(dpvs, 96, kAlign4, 76u * smodelCount);
    for (uint32_t i = 0; draws != kNoData && i < smodelCount; i++) {
        ModelPtr(draws + i * 76, 56);
    }
    for (int f : { 100, 104 }) {
        s.Push(1);
        Follow(dpvs, f, kAlign4, 0);
        s.Pop();
    }
    // dpvsDyn at +948: everything is runtime
    for (int f = 16; f < 48; f += 4) {
        s.Push(1);
        Follow(dyn, f, kAlign4, 0);
        s.Pop();
    }
    Follow(h, 1000, kAlign4, 24u * U32(h, 996)); // worldLodChains
    Follow(h, 1008, kAlign4, 12u * U32(h, 1004)); // worldLodInfos
    Follow(h, 1016, kAlign4, 4u * U32(h, 1012));  // worldLodSurfaces
    for (int i = 0; i < 2; i++) {
        size_t wb = h + 1024 + i * 8;
        Follow(wb, 4, kAlign4, 16u * (U32(wb, 0) >> 4));
    }
    MaterialPtr(h, 1040);
    MaterialPtr(h, 1044);
    MaterialPtr(h, 1048);
    Follow(h, 1056, kAlign4, 68u * U32(h, 1052)); // occluders
    Follow(h, 1064, kAlign4, 24u * U32(h, 1060)); // outdoorBounds
    Follow(h, 1076, kAlign4, 56u * U32(h, 1068)); // heroLights
    Follow(h, 1080, kAlign4, 24u * U32(h, 1072)); // heroLightTree
    s.Pop();
    return name;
}

} // namespace

int WalkResult::Find(uint32_t type, const std::string& name) const {
    for (size_t i = 0; i < assets.size(); i++) {
        if (assets[i].type == type && assets[i].name == name && assets[i].header != kNoData) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

size_t WalkResult::Pointer(size_t field) const {
    auto it = loadedAt.find(field);
    if (it != loadedAt.end()) {
        return it->second;
    }
    if (!stream || field == kNoData) {
        return kNoData;
    }
    uint32_t token = stream->U32(field);
    return token == kPtrInline || token == kPtrInsert ? kNoData : stream->Resolve(token);
}

int WalkResult::AssetAt(size_t field) const {
    auto it = assetAt.find(field);
    return it == assetAt.end() ? -1 : it->second;
}

WalkResult WalkZone(const Zone& zone) {
    WalkResult result;
    Walker walker(zone, result);
    try {
        walker.Run();
    } catch (const std::exception& e) {
        result.error = e.what();
        result.complete = false;
    }
    result.stoppedAt = walker.Position();
    return result;
}

} // namespace bomap
