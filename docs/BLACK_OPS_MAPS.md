# Black Ops maps in Zombies of Time: design

Tee asked for the Black Ops Zombies maps themselves to be playable in Zombies
of Time, starting with Nacht der Untoten. This note describes how the mode
gets them without any Black Ops data in this repository.

## The rule

Activision owns the maps: their geometry, collision, textures and sounds. None
of it is committed here, attached to a release, or downloaded by the mode.
What ships is a **converter**. It reads the map from the player's own Black
Ops install, on their own machine, and writes a SoH mod archive next to their
game. SoH already works this way for Ocarina of Time: it ships no Nintendo
data and extracts everything from the player's ROM.

The decompiled Black Ops source in MisaDev4/bo1-zombies-decompiled is a
reference for how the files are laid out. The converter is written from
scratch, and no code from that repository is copied in.

## What the player does

1. Open **Enhancements > Zombies of Time > Black Ops Maps** and pick their
   Black Ops folder (the one with `zone/` in it).
2. Press **Convert Nacht der Untoten**. The converter writes
   `mods/zombies-bo-nacht.o2r` into SoH's folder. It takes a few seconds and
   only needs doing once.
3. Press **Play Nacht der Untoten**. Link warps into the map and the run
   starts at the map's spawn point.

There's also a command-line tool for the same conversion, so it can be run
and checked without the game.

## Pipeline

```
zone/<lang>/zombie_cod5_prototype.ff   (the player's install)
        │  1. fastfile reader: header, zlib, asset list
        ▼
clipMap_t (collision)   GfxWorld (visible geometry)   MapEnts (entities)   images
        │  2. converters
        ▼
OoT collision header    room display lists            arena layout          textures
        │  3. archive writer
        ▼
mods/zombies-bo-nacht.o2r  → replaces an unused SoH debug scene slot
        │  4. the mode
        ▼
Zombies of Time arena: windows, wall buys, debris, chest spots, perks
```

### 1. Reading the fastfile

Black Ops PC fastfiles start with an 8-byte magic (`IWffu100`, or `IWff0100`
for a signed file), then a 4-byte version (473, `0x1D9`). The rest is one zlib
stream. Inflated, it is the zone: a block-size header, a script string table,
then the asset list. Each asset is written in place, and pointers are tokens
that say whether the data follows inline or refers to something already read.
The reader walks that stream for the four asset types it needs and skips the
others by their known sizes:

| Asset | Type | Used for |
| --- | --- | --- |
| `clipMap_t` | `0x0B` | Collision: triangle soup in the leaf partitions, plus brushes (convex solids given as planes) |
| `GfxWorld` | `0x11` | What you see: vertex buffers, triangle surfaces, material per surface, baked vertex light |
| `MapEnts` | (map entities) | The Zombies layout: the classic `{ "key" "value" }` entity text |
| `GfxImage` | images | Textures for the materials the surfaces use |

Skipping assets is the hard part. Every asset type a map zone contains has to
be parsed far enough to know where it ends, so the reader grows one asset type
at a time. The first milestone is a reader that lists every asset name in a
zone, which Tee can run on their install to check it against the real files.

### 2. Converting

**Coordinates.** Black Ops is Z-up and measured in inches. OoT is Y-up and
measured in its own units, roughly a centimetre: adult Link is about 60 units
tall and a Black Ops player about 70. The conversion is
`oot = (cod.x, cod.z, -cod.y) × scale`, with the scale starting at 1.0 and set
so doorways fit adult Link. OoT stores collision and vertices as 16-bit
integers (±32767), which Nacht fits easily. The larger maps (Kino, Five) are
re-centred on their spawn point first.

**Collision.** OoT collision is a list of triangles with a surface type each.
`clipMap_t` triangles convert directly. Brushes are turned into triangles by
clipping each brush plane against the others. Then:
- Player-clip and monster-clip become walls. The invisible walls around the
  map's edges and windows are what make it play like Black Ops.
- Surface types map across: wood to OoT's wood floor sound, dirt to dirt,
  metal to metal, everything else to stone.
- Triangles closer together than a unit are welded, and tiny slivers are
  dropped, to keep OoT's floor and wall lookups fast.

**Visible geometry.** Each `GfxWorld` surface becomes a display list that
draws its triangles with its texture. Baked lighting goes into vertex colours,
so the map keeps its Black Ops lighting without OoT's light system. Rooms are
split by Black Ops' own cells, so OoT only draws what the camera can see.

**Textures.** Black Ops images are DXT-compressed. They are decoded to RGBA,
scaled down to OoT-sized textures (64×64 to start), and written as RGBA16.
A full-resolution option comes later, through SoH's support for large
textures in mods.

**The Zombies layout** comes from the map's own entities, so every window,
wall buy and door sits where Black Ops puts it:

| Black Ops entity | Zombies of Time |
| --- | --- |
| `info_player_start`, `initial_spawn_points` | Where Link starts |
| `exterior_goal` (each window, six boards) | A barricade window, at the same spot and facing the same way |
| Zombie spawners (`actor_zombie_*`), and the spawn structs behind each window | Where undead claw out of the ground |
| `weapon_upgrade` trigger with `zombie_weapon_upgrade` (wall buys) | An Armory station, priced like the Black Ops wall buy |
| `zombie_door` and `zombie_debris` with `zombie_cost` | Debris or a door in our zone system, same cost; the zone opens and its brush model disappears |
| `info_volume` zones | Our arena zones: which windows and stations open with which door |
| `treasure_chest_use` | Mystery Chest spots |
| `zombie_vending` with `script_noteworthy` `specialty_armorvest` / `specialty_quickrevive` / `specialty_rof` / `specialty_longersprint` | Juggernog Milk / Fairy Revive / Double Tap / Stamin-Up |
| Power switch, Pack-a-Punch (on maps that have them) | Our power switch and Pack-a-Punch |

Nacht has no perks, power or Pack-a-Punch, so on Nacht those stations aren't
placed, the same as the original map. The Fairy Fountain goes next to the
start.

### 3. Writing the mod archive

SoH loads `.o2r` archives from its `mods/` folder, and a file in a mod
replaces the game file at the same path. The converter writes:

- A scene, its rooms, their display lists, vertices and textures, and a
  collision header, in the same resource formats SoH's own importers read
  (`soh/resource/importer/SceneFactory.cpp`, the `scenecommand/` factories,
  `CollisionHeaderFactory.cpp`).
- The arena layout as a small JSON file the mode reads.

The scene goes into the slot of one of OoT's unused debug scenes
(`testroom_scene`, `SCENE_TESTROOM`). No Nintendo scene is touched, and
removing the mod puts everything back as it was.

### 4. Playing it

The mode gets a second source of layouts: besides the ones written in
`ZombiesArena.cpp`, it reads a converted map's JSON layout when that scene
loads. Everything else is the existing mode: the same undead, rounds, points,
barricades, debris and stations, just in the Black Ops geometry. The map has
no OoT enemies, chests or exits of its own, and leaving it is through the
pause menu or by dying.

## Stages

Each stage is its own PR and is usable before the next starts.

1. **Fastfile reader and asset list.** A command-line tool,
   `tools/bomap`, that opens a `.ff` file and prints its assets. Tee runs it
   on `zombie_cod5_prototype.ff` to check it reads the real file.
2. **Entities.** Read `MapEnts` and print the Zombies layout as JSON (windows,
   wall buys, doors, chest spots). This is the first thing that can be
   checked against the map from memory.
3. **Collision.** Convert `clipMap_t`, write the archive, and warp into a
   scene where Link can walk the map's floors and walls, with the layout's
   stations, windows and debris drawn as they are today. The geometry is
   invisible at this stage, but the map already plays.
4. **Visible geometry and textures.** Convert `GfxWorld` and the images.
5. **In-game converter.** The menu buttons above, running the same code
   inside SoH.
6. **More maps.** Verrückt, Shi No Numa, Der Riese, then the Black Ops maps.
   Each one gets a check that its entities map onto our stations.
7. **Zombie models and sounds** from the same install, as in phase 5 of
   `DESIGN.md`.

## What can and can't be checked here

The converter is developed without a Black Ops install, so:

- The reader is written against the layouts in the decompiled engine's
  loading code. Whether it reads real retail files correctly can only be
  confirmed by Tee running it. The asset-list tool in stage 1 exists to make
  that check quick, and to send back exactly where reading stopped if it
  fails.
- SoH resource writing is tested by round-tripping through SoH's own
  importers in a unit test, which doesn't need the ROM.
- How the map feels, its scale, lighting and performance, needs Tee in game.

## Risks

- **Retail fastfiles may differ from the decompiled build.** The version
  number and asset layouts are what the decompiled loader expects (version
  473). If retail differs, stage 1 shows it straight away.
- **Some textures and sounds are streamed** from separate files
  (`.iwd` archives and sound banks) rather than living in the zone. Stage 4
  reads those too, from the same install.
- **Performance.** Black Ops maps have far more triangles than OoT scenes.
  The converter merges and simplifies; the floor of what's acceptable gets
  measured in stage 3.
- **Licence.** Converting files a player owns for their own use is the model
  SoH itself relies on. Converted output is never shared by the mode, and the
  README will say not to share it.
