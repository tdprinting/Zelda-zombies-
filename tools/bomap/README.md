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
