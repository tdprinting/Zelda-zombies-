# bomap

Reads a Black Ops map from **your own** Black Ops install for Zombies of Time.
It's the importer for [docs/BLACK_OPS_MAPS.md](../../docs/BLACK_OPS_MAPS.md):
it opens a map's fastfile, prints what's in it and the map's Zombies layout
(windows, wall buys, doors and debris, chest spots, perks) in Ocarina of Time
coordinates, and converts the map's geometry and collision (`mapgeo`), models
and animations (`gamemodel`, `gameanim`), particle effects (`gamefx`), the
zombies HUD art (`hud`) and sounds (`gamesound`) into the files the game reads from its `zombies/`
folder.

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
bomap gamemodel zombie_cod5_prototype.ff zombie_treasure_box box.zzmodel --main <Black Ops>/main   the Mystery Box
bomap fx        common_zombie.ff misc/fx_zombie_powerup_on                what a particle effect holds
bomap gamefx    common_zombie.ff misc/fx_zombie_powerup_on powerup.zzfx --main <Black Ops>/main
```

Link's third-person gun animations come from Black Ops' player animations:

```
bomap playeranims common_zombie.ff --out <models dir> --model zombie_cod5_prototype.ff c_usa_dempsey_body
```

It writes each animation found as `player_<role>.zzanim`, the player skeleton the
animations were made for (`player_skeleton.zzmodel`, bones only) and merges their
`player_` lines into the manifest (other lines are kept). Roles, with the Black Ops
animations tried for each:

| Role | Black Ops animation |
| --- | --- |
| `player_{pistol,rifle}_idle`, `_ads` | `pb_stand_alert[_pistol]`, `pb_stand_ads[_pistol]` |
| `player_pistol_walk_{f,b,l,r}` | `pb_combatwalk_*_loop_pistol` |
| `player_rifle_walk_{f,b,l,r}` | `pb_stand_shoot_walk_*` (or `pb_rifle_stand_shuffle_*`) |
| `player_{pistol,rifle}_run_{f,b,l,r}` | `pb_combatrun_*_loop[_pistol]` (`pb_pistol_run_fast` forward) |
| `player_{pistol,rifle}_sprint` | `pb_sprint[_pistol]` |
| `player_{pistol,rifle,smg}_fire`, `_fire_ads`; `player_shotgun_fire` | `pt_stand_shoot[_ads][_pistol]`, `pt_stand_shoot_auto[_ads]`, `pt_stand_shoot_shotgun` |
| `player_{pistol,rifle,smg,shotgun,lmg}_reload` | `pt_reload_stand_{pistol,rifle,auto}`, `pt_shot_stand_reload`, `pt_lmg_stand_reload` |
| `player_{pistol,rifle}_raise`, `_putaway` | `pt_{pistol,rifle}_stand_raise`, `_putaway` |

`pb_` animations are the whole body, `pt_` the torso played over it. Animations now
end with an optional `ROOT` block, the root's travel over the animation (see
`docs/ZZMODEL_FORMAT.md`), which the game uses to match loop speed to Link's.

Particle effects are in `docs/ZZFX_FORMAT.md`. The game draws each effect role
(`fx_box_beam`, `fx_muzzle_flash`, `fx_explosion`, ...) from the file the manifest
names, and falls back to a look built in code when there is none. Effect sprites
whose images aren't found are drawn as procedural shapes.

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

### Sounds

```
bomap gamesound zombie_cod5_prototype.ff common_zombie.ff --out <SoH folder>/zombies/sound
```

writes the sounds the game plays as WAV files, with `manifest.txt` naming each alias's
files and settings and the alias for each of the game's sound roles: every gun's fire,
dry fire, raise and put away sounds and the reload sounds its view animations name, the
purchase and deny sounds, windows, the Mystery Box, power-ups, perk machines, Pack-a-Punch,
the zombies' and hellhounds' voices, and Nacht der Untoten's ambience. Give Nacht's zone
first so its own sounds win over the shared ones.

Most of Black Ops' sounds are WMA, which is decoded by running [ffmpeg](https://ffmpeg.org);
pass `--ffmpeg <path>` if it isn't on your PATH (without it only the few non-WMA sounds
are written). It takes about a minute and writes about 100 MB; `--weapons m1911_zm,mp40_zm`
keeps only those guns' sounds (and their Pack-a-Punched weapons'). `--roles <file>` adds or
replaces roles, one per line: `<role> <alias>...`. `bomap weapons` lists each weapon's
sound names and notetrack sounds, and `bomap sounds` every alias in a zone.

Music and most voice lines (round stings, perk jingles, the announcer) are streamed from
outside the zones in Black Ops, so they aren't written; the game keeps its own sounds for
those.
