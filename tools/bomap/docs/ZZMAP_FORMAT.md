# ZZBOMAP1: converted map file

`bomap mapgeo <zone> <out.zzmap>` writes this from a Black Ops map zone. The game
reads it at run time (`ZombiesBoMap.cpp`). It is made on the player's machine from
their own install and is never committed or shipped.

Little-endian, packed, no padding. Coordinates are already OoT's: `oot = (x, z, -y) * scale`.

```
char  magic[8]        "ZZBOMAP1"
u32   version         1
f32   scale           applied to every position below
s16   spawn[3]        player start
s16   spawnYaw        OoT binary angle

u32   colVertCount
s16   colVerts[colVertCount][3]
u32   colTriCount
struct { u16 v[3]; u8 surface; u8 flags; } colTris[colTriCount]
      surface: 0 stone, 1 dirt, 2 wood, 3 metal, 4 grass, 5 water (shallow)
      flags:   1 wall only (player/monster clip: blocks, never a floor)

u32   imageCount
struct { char name[64]; u16 w, h; u8 rgba[w * h * 4]; } images
      w, h are powers of two, 4..64 (128 with the large option)

u32   materialCount
struct { char name[64]; s32 image; u8 flags; } materials
      image: index into images, -1 untextured
      flags: 1 alpha test (cutout), 2 translucent, 4 two-sided

u32   meshCount
struct {
  u32 material
  u32 vertCount
  struct { s16 pos[3]; f32 uv[2]; u8 rgba[4]; } verts[vertCount]   // rgba: baked light
  u32 indexCount
  u16 indices[indexCount]                                           // triangle list
} meshes

u32   entitiesLength
char  entities[entitiesLength]   // the same JSON `bomap layout` prints
```
