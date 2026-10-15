#pragma once

// Turns walked zone assets into files a modder can use: models as OBJ (a quick
// look) or JSON (skeleton, weights, materials), animations as JSON keyframes,
// in-zone images as PNG, and readable reports of materials and sound banks.

#include "fastfile.h"
#include "zone_walker.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bomap {

// A model's LOD 0 in Black Ops axes and units (Z up, inches).
struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
    int bones[4] = { 0, 0, 0, 0 };
    float weights[4] = { 1, 0, 0, 0 };
};

struct Surface {
    std::string material;
    std::vector<Vertex> verts;
    std::vector<uint16_t> tris;
};

struct Bone {
    std::string name;
    int parent = -1;
    float localQuat[4] = { 0, 0, 0, 1 }; // x, y, z, w, relative to the parent
    float localTrans[3] = { 0, 0, 0 };
    // The translation the model stores, which animation translations are added to.
    // Usually localTrans; the view hands store zeros (their animations carry the
    // whole translation) while localTrans still gives the drawn bind pose.
    float animBase[3] = { 0, 0, 0 };
    float worldQuat[4] = { 0, 0, 0, 1 }; // model space bind pose
    float worldTrans[3] = { 0, 0, 0 };
};

struct Model {
    std::string name;
    std::vector<Bone> bones;
    std::vector<Surface> surfaces;
};

// Keys at frame numbers; a track with one key holds still.
struct Track {
    std::vector<int> frames;
    std::vector<std::vector<float>> keys;
};

struct AnimBone {
    std::string name;
    Track rot;   // local quaternions x, y, z, w (replace the bind rotation)
    Track trans; // offsets added to the bind local translation; empty when none
};

struct Anim {
    std::string name;
    int numFrames = 0; // frames run 0..numFrames
    float framerate = 30;
    bool looping = false;
    bool delta = false;
    std::vector<AnimBone> bones;
    std::vector<std::pair<std::string, float>> notetracks; // name, time 0..1
    Track rootTrans; // the character's movement over the animation
    Track rootQuat;
};

// Each throws std::runtime_error with a readable reason.
// `lod` 0 is the most detailed level.
Model ReadModel(const Zone& zone, const WalkResult& walk, int asset, int lod = 0);
int ModelLodCount(const Zone& zone, const WalkResult& walk, int asset);
int ModelLodVerts(const Zone& zone, const WalkResult& walk, int asset, int lod);
Anim ReadAnim(const Zone& zone, const WalkResult& walk, int asset);
// Linear key interpolation (quaternions normalized) at a frame.
std::vector<float> SampleTrack(const Track& track, float frame);
// Top mip of an in-zone image as RGBA8. False when its pixels aren't in the zone.
bool DecodeZoneImage(const Zone& zone, const WalkResult& walk, int asset, std::vector<uint8_t>& rgba, int& width,
                     int& height);
// Decodes `format` (a D3D format or FourCC) pixels into RGBA8; false if unsupported.
bool DecodePixels(uint32_t format, const uint8_t* data, size_t size, int width, int height, std::vector<uint8_t>& rgba);

std::string ModelToObj(const Zone& zone, const WalkResult& walk, int asset);
std::string ModelToJson(const Zone& zone, const WalkResult& walk, int asset);
std::string AnimToJson(const Zone& zone, const WalkResult& walk, int asset);

// The textures each material of a model (or one material) uses, and whether each
// image's pixels are in this zone or have to come from the game's .iwd archives.
std::string MaterialReport(const Zone& zone, const WalkResult& walk, int asset);
// Each weapon: its view, world and hand models and its animations, by slot.
struct WeaponInfo {
    std::string name;
    std::string displayName;
    std::vector<std::string> viewModels;  // gunXModel (variants for attachments)
    std::vector<std::string> worldModels;
    std::string handModel;
    std::vector<std::pair<int, std::string>> anims; // slot in szXAnims, anim name
};
std::vector<WeaponInfo> ReadWeapons(const Zone& zone, const WalkResult& walk);
std::string WeaponReport(const Zone& zone, const WalkResult& walk);

// Every alias in each sound bank: loaded into the zone or streamed, format, file.
std::string SoundReport(const Zone& zone, const WalkResult& walk);

// Top mip level of an in-zone image as PNG (DXT1/3/5, A8R8G8B8, L8, A8L8).
std::vector<uint8_t> ImageToPng(const Zone& zone, const WalkResult& walk, int asset);

// Block-compressed decode into RGBA8, exposed for tests. `format` is 1, 3 or 5.
std::vector<uint8_t> DecodeDxt(const uint8_t* data, size_t size, int width, int height, int format);
std::vector<uint8_t> EncodePng(const std::vector<uint8_t>& rgba, int width, int height);
// Black Ops' 16-bit texture coordinate format.
float HalfToFloat(uint16_t h);

} // namespace bomap
