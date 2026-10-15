# ZZFX1: converted particle effects

`bomap gamefx <zone> <effect> <out.zzfx> [--main <dir>]` writes one Black Ops
effect (an FxEffectDef asset such as `misc/fx_zombie_powerup_on`) in this format.
The game's particle system (`ZombiesFx.cpp`) reads it at run time from
`zombies/models/`. Like the models, these files are made on the player's machine
from their own install and are never committed or shipped.

`bomap fx <zone> <effect>` prints what an effect holds, for looking at it.

## What is kept

Black Ops effects are lists of elements. Each sprite-like element becomes one
emitter here. Runner elements (an element that plays another effect) are inlined
one level deep, so a file stands alone. Model, light, sound and decal elements and
screen-distortion sprites are left out: the game draws only camera-facing quads.

Images: an effect's sprites name materials whose colour maps usually stream from
the install's `.iwd` archives. With `--main <install>/main` they are read and
scaled down to 64 pixels at most; otherwise the emitter's `image` is -1 and the
game draws a procedural shape picked by `shape`.

## Coordinates and units

Positions, offsets and movement are in the effect's own frame and Black Ops units
(inches): +X is the direction the effect is played along (a surface normal, the
barrel), +Y left, +Z up. The game turns the frame to the direction it plays the
effect in and scales to OoT units. Times are milliseconds.

## Layout

Little-endian, packed.

```
char  magic[8]        "ZZFX1\0\0\0"
u32   version         1
u32   loopingLifeMs   how long looping emitters run; 0x7FFFFFFF = until stopped
u32   imageCount      { char name[64]; u16 w, h; u8 rgba[w*h*4]; }  as ZZMODEL1
u32   emitterCount
struct {
  u8  category        0 looping (spawns over time), 1 one-shot, 2 emitted (by moving particles; drawn as one-shot)
  u8  render          0 camera-facing quad, 1 stretched along its movement (or +X when still), 2 flat, facing +X
  u8  blend           0 alpha blend, 1 additive, 2 multiply
  u8  shape           fallback when image is -1: 0 soft dot, 1 puff, 2 streak, 3 ring, 4 wide glow, 5 ray
  s32 image           index into images, or -1
  u8  atlasCols       sprite sheet columns (1, 2, 4, 8, 16)
  u8  atlasRows
  u8  atlasStart      first frame
  u8  atlasMode       0 fixed frame, 1 random frame per particle, 2 play over life, 3 loop at atlasFps
  u8  atlasFps
  u8  pad
  u16 atlasFrames     frames in the sheet
  u32 spawnA          looping: ms between spawns;      one-shot: count
  u32 spawnB          looping: most alive at once (0 = no limit); one-shot: extra random count
  u32 delayMs, delayRandMs
  u32 lifeMs, lifeRandMs
  f32 origin[3][2]    spawn offset per axis: base, random extra (base + rand * extra)
  f32 radius[2]       extra random offset in the YZ disc around +X: base, random extra
  f32 height[2]       ... and along +X
  f32 gravity[2]      fraction of world gravity (800 units/s^2, falling): base, random extra
  f32 rotation[2]     initial roll in radians: base, random extra
  u32 velSampleCount  samples spread evenly over the particle's life
  struct {
    f32 local[3]      displacement so far, per ms of the particle's life, effect frame
    f32 localRand[3]  random extra per axis
    f32 world[3]      the same in world axes (Black Ops: Z up)
    f32 worldRand[3]
  } vel[velSampleCount]
  u32 visSampleCount  samples spread evenly over the particle's life
  struct {
    u8  rgba[4]       colour and alpha
    u8  rgba2[4]      second colour: each particle picks a fixed point between the two
    f32 size[2]       half width, half height (units)
    f32 sizeRand[2]   random extra
    f32 rollTotal     roll reached by this sample (radians)
  } vis[visSampleCount]
} emitters[emitterCount]
```

A particle at life fraction `f` (0..1) sits at
`spawn + (local(f) + r * localRand(f)) * lifeMs` (turned into the play
direction) `+ (world(f) + r' * worldRand(f)) * lifeMs + 0.5 * gravity * 800 * t^2`
downwards, with `local(f)` interpolated between samples and `r`, `r'` fixed random
numbers per particle and axis in 0..1.
