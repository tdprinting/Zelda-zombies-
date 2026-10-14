# Zombies of Time: design

A round-based survival mode for Ocarina of Time, built into Ship of Harkinian
(SoH). The structure follows Call of Duty: Black Ops (2010) Zombies; every
model, sound and item comes from Ocarina of Time. Black Ops is used only as a
reference for how the mechanics behave. No Activision code or assets are in
this repository, and no Nintendo data either: SoH extracts assets from the
player's own ROM.

## The loop

1. Link stands somewhere with open ground (any scene works; see
   [Arenas](#arenas)) and turns the mode on from the SoH menu
   (Enhancements > Zombies of Time).
2. Night falls, a Redead scream plays and round 1 begins.
3. Undead claw out of the ground behind four boarded windows around Link,
   tear the boards off and come for him. Every hit and kill earns points, and
   so does nailing boards back up.
4. Points buy weapons, perks and upgrades at stations placed around the
   starting point.
5. When the last undead of a round falls, a short break begins, then the next
   round starts with more enemies that have more health.
6. Death ends the run. The HUD shows how many rounds Link survived and the
   best run is remembered.

## Mechanic map

| Black Ops Zombies | Zombies of Time | Notes |
| --- | --- | --- |
| Zombie | Stalchild (rounds 1+), Redead (3+), Gibdo (6+), big Stalchild (10+) | Mix shifts toward tougher undead each round |
| Hellhound round | Wolfos round: White Wolfos only | First on round 5 to 7, then every 4 to 6 rounds. The last Wolfos always drops Max Ammo |
| Round count | `ceil(24 + 3 × m) × early-round factor`, solo formula | `m = max(1, round/5)`, and from round 10 `m ×= round × 0.15`. Rounds 1 to 5 use 25%, 30%, 50%, 70%, 90% |
| Max zombies alive (24) | Slider, default 8 | OoT's actor budget and arena sizes are smaller |
| Spawn delay `2.0s × 0.95^round` | Same curve, floor 0.25s | |
| Zombie move speed (`round × 8` plus spread; walk ≤ 35, run ≤ 70, sprint above) | Same roll per undead, applied through Horde Pull at 2 / 4 / 6 units per frame | Round 1 is all walkers; by round 11 every undead sprints |
| Zombie health +100 per round, then ×1.1 | Health multiplier on each enemy's own HP: `1 + 0.15 × (round − 1)` to round 10, then ×1.08 per round | Capped at 255 (OoT stores HP in one byte). Slider scales it |
| Points: 10 per hit, 60 kill, 100 headshot, 130 knife | 10 per hit, 60 kill, 100 kill with an arrow, seed or hookshot, 130 kill with a sword | |
| Start with 500 points | 500 | |
| Wall buys | **Armory** station: an item for Link's age, or a refill at half price if he has it | Child: Deku Sticks, Slingshot, Boomerang, Bombs. Adult: Bow, Bombs, Hookshot, Megaton Hammer |
| Mystery Box (950) | **Mystery Chest** (950): random weapon | Pool: Bombchus (Monkey Bomb), Fire/Ice Arrows, Din's Fire (Thundergun), Light Arrows (Ray Gun, rare), Biggoron Sword (rare), Longshot, Hammer. Child pool swaps in Slingshot, Boomerang, Bombs. After four uses, each roll has a 1 in 6 chance of Skull Kid laughing (the teddy bear): the points come back and the chest moves |
| Juggernog | **Juggernog Milk** (2500): Double Defense | |
| Quick Revive | **Fairy Revive** (500 solo): a bottled fairy | A fairy in a bottle is OoT's own self-revive. Fills an empty bottle, else adds a bottle, else replaces the first bottle's contents. Can be bought again once the fairy is used |
| Double Tap | **Double Tap** (2000): undead lose twice the HP per hit | |
| Stamin-Up | **Stamin-Up** (2000): Link runs 25% faster | |
| Power switch | **Power** station: free, lights the perk and Pack-a-Punch torches | Playing the Sun's Song anywhere also turns the power on |
| Pack-a-Punch (5000) | **Pack-a-Punch** (5000): upgrades one item | Checks the C buttons first. Bow: bigger quiver, then Light Arrows. Slingshot: bigger seed bag. Bombs: bigger bag. Hookshot: Longshot. Then the sword: Biggoron Sword (adult), then a Punched Blade whose hits land twice. Then the Mirror Shield (adult) |
| Power-ups (2% plus point-based drops, 4 per round max) | Same drop logic; each power-up is a floating drop | Max Ammo = big magic jar, Insta-Kill = red rupee, Double Points = gold rupee, Nuke = bomb drop, Fire Sale = purple rupee |
| Max Ammo | Refills sticks, nuts, seeds, arrows, bombs, bombchus and magic | |
| Carpenter | Deku Stick drop: every window gets all six boards back, +200 points | Only drops while a window is missing boards |
| Insta-Kill (30s) | Every undead dies in one hit | |
| Double Points (30s) | All points ×2 | |
| Nuke | Every undead on the map explodes, +400 points | |
| Fire Sale (30s) | Mystery Chest costs 10 | |
| Barricades: 6 boards per window, zombies tear them off, players repair for 10 points each | **Barricades**: boarded windows (two posts and six planks), two or three per zone on the hand-made maps and four around the start point anywhere else. Undead spawn outside one and are held there, tearing a board every 1.5 seconds, until it is open. Hold the buy button at a window to put a board back every 0.6 seconds for 10 points, up to 500 points of repairs per round | Wolfos rounds ignore the windows, like hellhounds. The planks are the small wooden crate's model squashed flat. The windows are not solid: Link can walk around them, the undead can't |
| Doors and debris | **Debris** piles between the zones of an authored arena (1000 points into the second zone; 1000 or 1250 into the third) | Clearing one opens that zone's stations and windows. See [Kakariko Graveyard](#kakariko-graveyard) |
| Round chime, Samantha's voice | OoT sound effects | Round start: Redead scream. Round end: puzzle-solved chime. Power-up: item get jingle |

## Zelda twists

The mode should feel like both games at once, so some systems come from
Ocarina of Time rather than Black Ops:

- **Songs are abilities.** The Sun's Song turns the power on from anywhere
  (and, as in the base game, freezes nearby Redeads, so it doubles as a panic
  button). The Song of Storms brings the game's own rainstorm, and the horde
  moves at half speed for 20 seconds. Zelda's Lullaby unseals Pack-a-Punch;
  a file that hasn't learned the Lullaby yet only needs the power on.
- **Hearts, not regen.** Black Ops heals on its own; here Link heals the
  Zelda way: whatever the undead drop, the Fairy Fountain, and a full heart
  refill at the start of each round.
- **Magic is a resource.** Din's Fire, Nayru's Love and magic arrows draw on
  the magic meter, which only Max Ammo and magic jars refill.
- **Navi is Samantha.** Navi greets each run; round, power-up and power
  callouts use OoT's own sounds (Redead scream, Wolfos howl, puzzle chime).
  Phase 3 gives Navi the full announcer role with text bubbles.
- **Gold Skulltula easter egg (phase 3).** Shooting three hidden Gold
  Skulltulas during a run plays a song and unlocks a hidden station, the way
  Black Ops maps hide their music easter eggs.
- **Rupees still matter.** Kills drop rupees as normal; rupees are spent at
  a Fairy Fountain station to top up hearts between rounds, so OoT's
  currency and Zombies points both have a job.

## Controls

In the zombies-only game, runs start from the main menu. With Zombies Only off
and the mode enabled, press **L** anywhere to start a run there. Each station
is a torch; walk up to one and the HUD shows `L: MYSTERY CHEST [950]`.
Press **L** to buy, and hold it at a broken window to repair it. During a run
the mode takes the L press, so buying no longer toggles the minimap. The
button can be switched to D-pad Down in the settings.

**START** during a run opens the run menu and freezes the game: Resume,
Items (the Zelda pause screen, for equipping), Controls and End Run.

Torch colours tell stations apart: the golden torch is the Mystery Chest,
wooden torches are the Armory, the Fairy Fountain and the power switch, and
stone torches are the perks and Pack-a-Punch, dark until the power is on.

## HUD

Drawn with the game's own debug font in the N64 frame, so it scales with every
SoH resolution:

- Bottom left, above the rupees: the round number in red, as tally marks for
  rounds 1 to 5 and as a number after that, with owned perks above it.
- Bottom right: points, with `+10` / `+60` stacking above them.
- Upper middle: round announcements and power-up names.
- Top: active power-up and storm timers.
- Lower middle: the prompt for the nearest station or damaged window.

## Arenas

The mode runs in any scene. At the start it samples the floor around Link with
the game's collision raycasts to find spawn points and station spots, so it
needs no per-scene data. Recommended arenas, all reachable in a normal file:

- **Kakariko Graveyard** (child or adult): the classic (below).
- **Market Ruins** (adult only): the walled square. South Gate, then The
  Square, then The Castle Road with the power and Pack-a-Punch.
- **Lon Lon Ranch** (either age): Front Gate, The Paddock, then The Barn.
- **Hyrule Field** (either age): The Bridge, then The West Field and The East
  Field. Huge; turn Max Undead Alive up.

All four have hand-made layouts. The spots for the Market, Ranch and Field
were picked from floor scans of each scene and walked with the playtest
harness. Any other scene still works with the automatic placement. During a
run the scene's exits are walled off, so a knockback can't throw Link out;
leaving the scene any other way ends the run.

### Kakariko Graveyard

Tee asked for one of the Black Ops maps to be ported. Its geometry and
assets can't be copied, so the graveyard gets the *flow* of Nacht der
Untoten, the first Black Ops map, built from the graveyard's own ground:

| Zone | Opens with | Windows | Stations |
| --- | --- | --- | --- |
| The Gate (start, by the Kakariko entrance) | | 3 | Armory, Fairy Fountain, Mystery Chest |
| The Graves (the rows in the middle) | Debris, 1000 | 3 | Juggernog, Double Tap, second chest spot |
| The Royal Tomb (the far end) | Debris, 1000 | 2 | Power, Pack-a-Punch, Stamin-Up, Fairy Revive, third chest spot |

As in Nacht, the start area is small and survivable for the first rounds,
opening each zone gives Link more to buy and the horde more ways in, and the
chest moves between its spots in the open zones. Perks and Pack-a-Punch sit
at the far end, so getting to them is the mid-game goal.

The spots are written in `ZombiesArena.cpp` as (x, z) positions, picked
from a floor scan of the graveyard (it climbs in terraces from the gate to the
far end) and walked with the playtest harness; heights come from the floor at
run time. Every spot is
checked against the scene's collision when the run starts: a station that
doesn't land on floor is placed around Link instead, a window without room
behind it for undead is left out, and debris with no floor opens its zone
from the start. The **Show Coordinates** setting prints Link's position on
screen, so the spots can be tuned in game. The debris piles are crates, not
walls: they mark the way and hold the zone's stations and windows back, but
Link can walk around them.

## Your save file

Purchases put real items into the loaded file. The mode snapshots the
inventory, equipment, upgrades, health and magic when a run starts, keeps a
copy in `Save/zombies_snapshot_<slot>.bin`, and restores and saves them when
the run ends (death, quitting, leaving the scene or turning the mode off). If
the game is reset or closed mid-run, the copy is put back the next time that
file loads, over anything an autosave wrote. Saving from the pause screen is
blocked during a run. **Keep Items After Run** keeps purchases, outside the
zombies-only game only.

## Settings

All under Enhancements > Zombies of Time, stored as
`gEnhancements.Zombies.*` CVars:

| Setting | Default |
| --- | --- |
| Zombies Only | on |
| Enabled (the mode inside the normal game) | off |
| Max Undead Alive | 8 |
| Health Scaling | 100% |
| Starting Points | 500 |
| Starting Round | 1 |
| Wolfos Rounds | on |
| Barricades | on |
| Authored Arenas (the hand-made map layouts) | on |
| Show Coordinates | off |
| Horde Pull (undead always walk toward Link) | on |
| Force Night | on |
| Redead Scream (Redeads and Gibdos freeze Link) | on |
| Buy Button (L / D-pad Down) | L |
| Keep Items After Run | off |
| Best Round (read only) | |

## Zombies-only game and main menu

"Zombies Only" (on by default) turns the whole game into the mode:

- Loading any file skips the intro and drops Link into the chosen map at night.
- The main menu opens over the map. Like Black Ops', it has a list on the left: Play, Map, Link's age (locked to adult for the Market Ruins), Starting round, Controls and Quit to title. The map's intel, your best round and a tip are on the right. The menu's choices are saved with SoH's settings. It is drawn in Zelda's colours, with a gold Triforce and a red title. Use the D-pad or stick with A, or tap or click an entry, so it works on Android touch controls.
- Leaving a zombie map warps Link back. After a run, a results screen shows the rounds survived, kills, points and best round, then the menu returns. A death skips the Save/Continue screens.
- START during a run opens the run menu (Resume, Items, Controls, End Run).
- Every run starts with the same kit:
  - the age's sword and shield
  - a slingshot or a bow
  - four bombs
  - the Fairy Ocarina, with Zelda's Lullaby, the Sun's Song and the Song of Storms
  - three hearts and 0 rupees
- Each round from 2 onwards adds two bombs, like Black Ops' grenades.
- Your real save inventory comes back when the run ends.
- Music: the Shadow Temple plays during rounds and the mini-boss theme during Wolfos rounds. Navi calls out low health. Kill all three hidden Gold Skulltulas to hear the easter-egg song.

## How it is built

The mode is a self-contained SoH enhancement in
`mod/soh/soh/Enhancements/Zombies/`. SoH's CMake picks up every source file
under `soh/soh/`, and the mode registers itself through SoH's existing hooks,
so it needs no edits to SoH or to OoT's actor code:

| Hook | Use |
| --- | --- |
| `RegisterShipInitFunc` + `COND_HOOK` | Hooks attach only while the mode is on |
| `RegisterMenuInitFunc` | Adds the settings to the SoH menu |
| `OnPlayerUpdate` | Once per frame: round state machine, spawning, hit and kill tracking, stations, power-ups |
| `OnActorDestroy` | Forget undead and drops that were unloaded |
| `OnGameFrameUpdate` | HUD drawing with `GfxPrint` |
| `OnPlayDrawEnd` | Drawing the barricade planks in the world |
| `OnSceneInit` | End the run when Link leaves the arena |
| `OnOcarinaSongAction` | Sun's Song, Song of Storms and Zelda's Lullaby |
| `VB_PLAYER_MODIFY_RUN_SPEED` | Stamin-Up |

Spawning uses `Actor_Spawn`; SoH loads actor assets by path, so Redeads and
Wolfos can appear in scenes that never had them. Hits and kills are read from
each tracked enemy's `colChkInfo.health`, which works the same for every enemy
type. "Horde Pull" walks idle undead toward Link a few units per frame and
lets their own collision update keep them out of walls.

Code layout:

| File | Contents |
| --- | --- |
| `ZombiesRules.h/.cpp` | Pure round, health, spawn delay, points and drop formulas. No game state, unit tested |
| `ZombiesMode.h/.cpp` | Run state, round state machine, spawning, tracking, power-ups, save snapshot |
| `ZombiesStations.cpp` | Station placement, prompts and purchases |
| `ZombiesBarricades.cpp` | Windows: placement, holding undead outside, tearing, repairs, drawing |
| `ZombiesArena.cpp` | Authored layouts (graveyard, Market Ruins, Lon Lon Ranch, Hyrule Field), zones and debris |
| `ZombiesLobby.cpp` | Main menu, run (pause) menu, results screen, map warps |
| `ZombiesExtras.cpp` | Navi callouts, low-health warning and other touches |
| `ZombiesBounds.cpp` | Keeps Link off the scene's exits during a run |
| `ZombiesHud.cpp` | HUD drawing |
| `ZombiesMenu.cpp` | Menu widgets |

## Phases

1. **Core loop (this PR):** rounds, undead mix and scaling, Wolfos rounds,
   points, stations (Armory, Mystery Chest, four perks, power, Pack-a-Punch,
   Fairy Fountain), six power-ups, song abilities, HUD, save snapshot, menu.
2. **Authored arenas (this PR):** Kakariko Graveyard, Market Ruins, Lon Lon
   Ranch and Hyrule Field, each with three zones. What's left is solid debris
   (collision, so Link can't walk around it).
3. **Polish:** a zombies music track built from OoT sequences, perk jingles,
   crawlers (undead that lose their legs to bombs), and a boss round (Dead
   Hand or Iron Knuckle).
4. **Local co-op, then split screen.** Asked for by Tee. Ocarina of Time
   assumes exactly one Link and one camera everywhere in its code, so this
   comes in two steps after the single-player loop works:
   - **4a, shared screen:** player 2 joins on a second controller, building on
     SoH's existing Ivan co-op (a second player who flies as a fairy and can
     use C-button items). Player 2 gets their own points, perks and revives;
     when Link goes down, player 2 has a few seconds to revive him, Black Ops
     style.
   - **4b, split screen:** a second Link actor with its own camera, with the
     frame drawn twice into two viewports. This touches the player actor,
     camera and renderer, so it is a fork-level change to SoH and gets its own
     design note before any code.

   The core loop is already shaped for this: points, perks and kill credit
   live in a per-player array (index 0 today), and the round formula takes
   the player count.

5. **Bring your own Black Ops (asset loader).** Asked for by Tee: the real
   Black Ops zombie models and sounds. They can't ship with this repository,
   so this phase adds an optional loader that reads them from the player's
   own copy of Black Ops at runtime, the same way SoH reads Ocarina of Time
   from the player's own ROM:
   - A menu setting points at the player's Black Ops install folder.
   - On first run the loader converts the zombie character model, its
     animations and the round, power-up and perk sounds into a local SoH
     resource archive on the player's machine. Nothing converted is uploaded
     or bundled.
   - A custom zombie actor draws that model with the converted animations,
     and uses the same AI as the OoT undead (Horde Pull, move tiers,
     barricades), so gameplay is identical with or without the loader.
   - Without the loader, everything falls back to the OoT undead and sounds.

   Maps come first: [BLACK_OPS_MAPS.md](BLACK_OPS_MAPS.md) covers converting
   Nacht der Untoten and the other maps from the player's install, with their
   windows, wall buys and doors placed from the map's own entities. Zombie
   models follow, with their own note on how converted models map onto OoT's
   skeleton system.
   The zombie AI behaviour (round sizes, move tiers, respawning stuck
   zombies, barricades) is already ported in phase 1 from the
   decompiled scripts, as a reference only.

