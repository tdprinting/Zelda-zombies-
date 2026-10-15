# ZZMODEL1 and ZZANIM1: converted models and animations

`bomap gamemodel <zone> <xmodel> <out.zzmodel>` and
`bomap gameanim <zone> <xanim> <out.zzanim>` write these from a Black Ops zone. The
game reads them at run time (`ZombiesModel.cpp`) from `zombies/models/` next to its
settings. They are made on the player's machine from their own install and are
never committed or shipped.

Little-endian, packed. Positions are in OoT axes (`oot = (x, z, -y)`), still in
Black Ops units (inches): the game scales them. Quaternions are (x, y, z, w) in the
same axes.

## ZZMODEL1

```
char  magic[8]        "ZZMODEL1"
u32   version         1
u32   boneCount
struct {
  char name[32]
  s32  parent         -1 for the root
  f32  pos[3]         bind pose, relative to the parent
  f32  quat[4]        bind pose, relative to the parent
} bones[boneCount]

u32   imageCount      as ZZBOMAP1: { char name[64]; u16 w, h; u8 rgba[w*h*4]; }
u32   materialCount   as ZZBOMAP1: { char name[64]; s32 image; u8 flags; }

u32   surfaceCount    LOD 0 only
struct {
  u32 material
  u32 vertCount
  struct {
    f32 pos[3]        model space, bind pose
    f32 normal[3]
    f32 uv[2]
    u8  rgba[4]
    u16 bone[4]       unused slots repeat bone[0] with weight 0
    f32 weight[4]     sum to 1
  } verts[vertCount]
  u32 indexCount
  u16 indices[indexCount]   triangle list
} surfaces
```

## ZZANIM1

```
char  magic[8]        "ZZANIM1\0"
u32   version         1
f32   framerate
u32   frameCount
u8    looping
u32   boneCount       only the bones the animation moves
struct {
  char name[32]       matched to the model's bones by name
  u8   hasQuat
  u8   hasPos
  f32  quat[frameCount][4]   if hasQuat: local rotation per frame
  f32  pos[frameCount][3]    if hasPos: local translation per frame
} bones
u32   notetrackCount
struct { u32 frame; char name[32]; } notetracks   // e.g. "footstep", "fire", "end"
// optional, readers check for it:
char  tag[4] = "ROOT"
f32   travel[3]       the root's movement over the whole animation, in model axes
```
