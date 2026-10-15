# Zombies of Time

A Call of Duty: Black Ops style Zombies game for The Legend of Zelda: Ocarina
of Time, built on the engine of [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright)
(SoH), the PC port of Ocarina of Time. It is its own install: it has its own
program, window and data folder, so it sits next to a normal SoH install
without touching SoH's saves or settings.

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
| `mod/patches/` | Engine patches for SoH (the game's name, data folder and program name) |
| `mod/patches-android/` | Extra patches for the Android port (app id, label, data folder) |
| `Shipwright/` | Ship of Harkinian, pinned as a git submodule (left unmodified in git) |
| `tests/` | Unit tests for the round, health and points formulas |
| `scripts/apply.sh` | Applies the engine patches and copies the mode into the Shipwright checkout |
| `docs/DESIGN.md` | Design and roadmap |

No game data ships here. Like SoH itself, the build extracts Ocarina of Time's
assets from your own legally obtained ROM, and Black Ops was used only as a
reference for how its mechanics behave.

## Building

1. Clone with submodules:
   `git clone --recursive https://github.com/tdprinting/Zelda-zombies-.git`
2. Patch SoH's engine and add the mode: `./scripts/apply.sh` (safe to run
   again; patches already applied are skipped).
3. Build as SoH's [build guide](Shipwright/docs/BUILDING.md) describes, from
   the `Shipwright/` folder. The program is `ZombiesOfTime` (`ZombiesOfTime.exe`
   on Windows).
4. Run it, give it your ROM when asked, and load a save. **Zombies Only** is
   on by default, so the file goes straight to the Zombies of Time main menu.

Every pull request also builds a Windows version and an Android APK in CI
(the `zombies-of-time-windows` and `zombies-of-time-android` artifacts). Like
SoH, both ask for your own ROM the first time they run.

## Installing next to Ship of Harkinian

Zombies of Time never shares files with SoH. It reads your ROM the same way
SoH does and makes its own `oot.o2r` from it, in its own folder.

| Platform | Program | Saves, settings (`shipofharkinian.json`), logs, `mods/` |
| --- | --- | --- |
| Windows | `ZombiesOfTime.exe` (window "Zombies of Time") | The folder you unzip it to. Unzip it to its own folder, not into SoH's. |
| Linux | `ZombiesOfTime` | The folder you run it from; as an AppImage, `~/.local/share/zombiesoftime` |
| Android | "Zombies of Time" app (`com.tdprinting.zombiesoftime`) | `/storage/emulated/0/ZombiesOfTime` (or an SD card folder you pick) |

On Android it installs as a separate app beside SoH's. APKs built before this
change installed as `com.linkzenic.soh.localtest` and used the `SOH93-Test`
folder; uninstall that one and copy any saves you want from `SOH93-Test/Save`
into `ZombiesOfTime/Save`.

### Engine patches

`mod/patches/*.patch` are changes to SoH's own code, applied by
`scripts/apply.sh` with `git apply` (the `Shipwright/` submodule itself stays
upstream SoH). Number new patches so they sort after the existing ones; a patch
for a nested submodule such as libultraship goes in `mod/patches/libultraship/`.
`scripts/apply.sh --android soh-android` does the same for the Android port,
using a same-named file in `mod/patches-android/` in place of a shared patch
whose context differs there. The script stops with an error if a patch no
longer applies, for example after the submodule is updated.

Run the formula tests with `./scripts/test.sh`.

## Playing

Load a file and the main menu opens over a map at night. Pick the map
(Kakariko Graveyard, Market Ruins, Lon Lon Ranch or Hyrule Field), Link's age
and the starting round, then **Play**. The menu works with the D-pad or stick
and A, or by tapping on Android.

Each map has a start zone and two more zones behind debris. Torches are
stations: walk up to one and press **L** to buy (the buy button can be moved
to **D-pad Down** in the settings). Hold it at a broken window to board it up. Perk and Pack-a-Punch torches stay dark until you throw
the power switch or play the Sun's Song. **START** opens the run menu
(resume, items, controls, end run); the **Controls** page in either menu
lists everything.

### Two players (split screen)

Set **Players** to **2** in the main menu and connect two gamepads. During a
co-op run the second gamepad plays player 2 automatically (with the default
layout if its port has none) and goes back to Link when the run ends. If port 2
in the controller settings already has a gamepad, that setup is used as is.
The screen splits top and bottom like Black Ops: player 1 is Link on top,
player 2 is a second Link in a red tunic below, with their own camera.

Each player has their own points, perks and weapons. Player 2 starts with the
Kokiri Sword and buys their own upgrades: the Master Sword and bombs from the
Armory, swords and bombs from the Mystery Chest, Pack-a-Punch for a stronger
blade, and every perk. Player 2's controls: stick to move, **B** sword,
**R** throw a bomb, **A** buy, repair and revive, right stick (or C-left and
C-right) to turn the camera, **Z** to put it behind you.

Co-op follows Black Ops' rules. A player who takes two hits (five with Juggernog) goes down
and loses some points and their perks. The other has 45 seconds to hold the
buy button next to them for 3 seconds to revive them (half that with Fairy
Revive); otherwise they bleed out and come back at the start of the next
round. When nobody is left standing, the run is over. Rounds
have more undead with two players, as in Black Ops.

After a run, a results screen shows the rounds survived, kills and points
(for each player in co-op).
In Zombies Only, your real inventory comes back when the run ends (or on the
next load after a crash or a reset), and Link's real age and equipment come
back the next time the file loads with Zombies Only off, so the mode never
changes a normal playthrough. (In the normal game, **Keep Items** lets you
keep what you bought.)

To play the mode inside the normal game instead, turn **Zombies Only** off
and **Enable Zombies of Time** on under **Enhancements > Zombies of Time**,
then press **L** anywhere with open ground to start a run there.
