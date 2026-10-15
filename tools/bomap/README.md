# bomap

Reads a Black Ops map from **your own** Black Ops install for Zombies of Time.
It's stage 1 of the map loader in [docs/BLACK_OPS_MAPS.md](../../docs/BLACK_OPS_MAPS.md):
it opens a map's fastfile and prints what's in it, and the map's Zombies
layout (windows, wall buys, doors and debris, chest spots, perks) in Ocarina
of Time coordinates. It doesn't convert geometry yet.

Nothing it reads is uploaded or shared. Please don't share its output either:
it comes from Activision's map.

## Build

```
cmake -S tools/bomap -B build-bomap
cmake --build build-bomap --config Release
```

Or download the `bomap-windows` / `bomap-linux` build from the latest CI run
on the pull request.

## Run

Nacht der Untoten is `zombie_cod5_prototype.ff`, in your Black Ops folder
under `zone/Common/` (or `zone/<language>/`).

```
bomap info   zombie_cod5_prototype.ff
bomap layout zombie_cod5_prototype.ff nacht.json
```

`info` is the quick check: it prints the file's version, how many assets of
each kind it holds, and whether it found the map's entities. If it fails, the
message says where reading stopped; paste it back in the project thread.

### Models, animations and the map for the game

Zombies and guns are in `zone/Common/common_zombie.ff` and the map's own zone.

```
bomap list      common_zombie.ff                     every asset, in load order
bomap weapons   zombie_cod5_prototype.ff             each weapon's models and animations
bomap gamemodel zombie_cod5_prototype.ff c_ger_honorguard_body1 zombie.zzmodel --attach c_ger_zombie_head1
bomap gameanim  zombie_cod5_prototype.ff ai_zombie_walk_v1 walk.zzanim --model zombie_cod5_prototype.ff c_ger_honorguard_body1
bomap gamemodel common_zombie.ff viewhands_usmc kar98k_view.zzmodel --attach viewmodel_zombie_kar98k_rifle \
                --attach-zone zombie_cod5_prototype.ff --attach-tag tag_weapon
bomap gameanim  zombie_cod5_prototype.ff viewmodel_kar98_fire kar98k_fire.zzanim --model common_zombie.ff viewhands_usmc \
                --model zombie_cod5_prototype.ff viewmodel_zombie_kar98k_rifle --attach-tag tag_weapon
bomap gamemodel zombie_cod5_prototype.ff '*63' board.zzmodel        a brush model of the map (a barricade board)
bomap mapgeo    zombie_cod5_prototype.ff nacht.zzmap --main <Black Ops>/main
```

The formats are in `docs/ZZMODEL_FORMAT.md` and `docs/ZZMAP_FORMAT.md`. Black Ops
streams character, weapon and world textures from the `.iwd` archives in `main/`,
so pass `--main` to get them; without it materials have no image. `model`, `anim`,
`image`, `materials` and `sounds` write readable files (OBJ, JSON, PNG, reports)
for looking at things.

### The zombies HUD

```
bomap hud common_zombie.ff --main <Black Ops>/main --out <SoH folder>/zombies/hud
```

writes Black Ops' zombies HUD images as PNGs named after their material: the chalk
tally marks (`hud_chalk_1`-`5`), the points bars (`scorebar_zom_1`, `_2`), the perk and
power-up icons (`specialty_*_zombies`), the low health overlay (`overlay_low_health`)
and the d-pad ring (`hud_dpad`). They're streamed from `main/*.iwd`, so `--main` is
needed. More than one zone can be given; the first with an image wins. The game uses
whichever it finds and draws its own for the rest.
