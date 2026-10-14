# Zombies of Time

A Call of Duty: Black Ops style Zombies mode for The Legend of Zelda: Ocarina
of Time, built as an enhancement for [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright)
(SoH), the PC port of Ocarina of Time.

Stalchildren, Redeads and Gibdos come at Link in endless rounds, with White
Wolfos packs standing in for hellhounds. Kills earn points, and points buy
weapons from the Armory, a roll of the Mystery Chest, potion perks such as
Juggernog Milk and a bottled-fairy Quick Revive, and Pack-a-Punch upgrades.
Ocarina songs are abilities too: the Sun's Song turns on the power, the Song
of Storms slows the horde and Zelda's Lullaby unseals Pack-a-Punch.

See [docs/DESIGN.md](docs/DESIGN.md) for the full design.

## What's in this repo

| Path | Contents |
| --- | --- |
| `mod/soh/soh/Enhancements/Zombies/` | The mode's source, laid out to drop into SoH's tree |
| `Shipwright/` | Ship of Harkinian, pinned as a git submodule |
| `tests/` | Unit tests for the round, health and points formulas |
| `scripts/apply.sh` | Copies the mode into the Shipwright checkout |
| `docs/DESIGN.md` | Design and roadmap |

No game data ships here. Like SoH itself, the build extracts Ocarina of Time's
assets from your own legally obtained ROM, and Black Ops was used only as a
reference for how its mechanics behave.

## Building

1. Clone with submodules:
   `git clone --recursive https://github.com/tdprinting/Zelda-zombies-.git`
2. Copy the mode into SoH: `./scripts/apply.sh`
3. Build SoH as its [build guide](Shipwright/docs/BUILDING.md) describes, from
   the `Shipwright/` folder.
4. Run SoH, give it your ROM when asked, load a save, and turn on
   **Enhancements > Zombies of Time > Enable Zombies of Time**.

Run the formula tests with `./scripts/test.sh`.

## Playing

Stand somewhere with open ground (Kakariko Graveyard at night is the classic)
and press **L** to start. Torches appear in a ring around you; each is a
station. Walk up to one and press **L** to buy. Perk and Pack-a-Punch torches
stay dark until you throw the power switch or play the Sun's Song.

By default your inventory goes back to how it was when the run ends, so a run
won't change a normal playthrough. Use a spare save file anyway.
