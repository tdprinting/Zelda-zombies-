#pragma once

// Models and animations imported from the player's own Black Ops install
// (ZombiesModel.cpp, formats in tools/bomap/docs/ZZMODEL_FORMAT.md).

#include <functional>
#include <string>
#include <vector>

// GameInteractor.h pulls in z64.h with the C++ headers it depends on already included.
#include "soh/Enhancements/game-interactor/GameInteractor.h"

namespace Zombies {

struct ZModel;
struct ZAnim;

// zombies/models/<name>.zzmodel and .zzanim. Null when missing or unreadable.
ZModel* Model_Get(const std::string& name);
ZAnim* Anim_Get(const std::string& name);
bool Model_FilesPresent();

float Anim_Length(const ZAnim* anim); // seconds
bool Anim_Looping(const ZAnim* anim);
// Whether a notetrack (e.g. "fire", "footstep") falls in (fromSec, toSec].
bool Anim_NoteBetween(const ZAnim* anim, const char* note, float fromSec, float toSec);
float Model_Height(const ZModel* model); // bind pose top, in model units

struct AnimLayer {
    const ZAnim* anim = nullptr;
    float time = 0.0f; // seconds
};

struct ModelPose {
    struct Skin {
        float qx, qy, qz, qw; // rotation then translation, bind pose to posed, model space
        float tx, ty, tz;
    };
    std::vector<Skin> skin;
};

// Pose from one animation, or two blended (blend 0: all a, 1: all b).
void Model_Pose(const ZModel* model, const AnimLayer& a, const AnimLayer& b, float blend, ModelPose* out);

// Per drawn instance: skinned vertices and the display list, kept between frames.
struct ModelDrawBuffers {
    std::vector<Vtx> verts[2];
    std::vector<Gfx> dl[2];
    int flip = 0;
};

// Draws the posed model standing at pos, turned to yaw and scaled to height (OoT units).
// Call from an actor's draw (or with lights set up), so OoT's lighting applies.
void Model_Draw(PlayState* play, const ZModel* model, const ModelPose& pose, const Vec3f& pos, s16 yaw, float height,
                ModelDrawBuffers* buffers, u8 alpha = 255);

// Model_Draw in two steps, so one skinned result can be drawn in several views (split
// screen) without skinning it again: Model_PosedDl builds the display list, Model_Submit
// draws it standing at pos.
void Model_Submit(PlayState* play, const ZModel* model, Gfx* dl, const Vec3f& pos, s16 yaw, float height,
                  u8 alpha = 255);

// A model in its bind pose, for rigid things (boards, guns): built once, drawn with the
// current matrix. Its vertices are in model units times Model_VertexScale().
Gfx* Model_RigidDl(ZModel* model);
void Model_Bounds(const ZModel* model, Vec3f* lo, Vec3f* hi);

struct ModelTint {
    u8 r, g, b;
};

// Per surface, by its material's name: return false to leave the surface out (a world
// gun's optional scopes and silencers); tint colours it when it has no image (the
// importer found no pixels for it).
using ModelStyle = std::function<bool(const std::string& material, bool textured, ModelTint* tint)>;
// Model_RigidDl with a style, built on first use and kept: one style per model.
Gfx* Model_RigidDlStyled(ZModel* model, const ModelStyle& style);
float Model_VertexScale();

// The posed model's display list for drawing with the caller's own matrix (a viewmodel),
// built into buffers; vertices in model units times Model_VertexScale(). Null when unposed.
Gfx* Model_PosedDl(const ZModel* model, const ModelPose& pose, ModelDrawBuffers* buffers,
                   const ModelStyle* style = nullptr, u8 alpha = 255);
// Where a bone (e.g. "tag_flash") is in the posed model, in model units. False when absent.
bool Model_BonePos(const ZModel* model, const ModelPose& pose, const char* bone, Vec3f* out);

// zombies/models/manifest.txt, written by the importer: which model or animation plays
// each part ("zombie_model", "anim_walk", "board_model", ...). Empty when not listed.
std::vector<std::string> Model_Manifest(const std::string& key);

} // namespace Zombies
