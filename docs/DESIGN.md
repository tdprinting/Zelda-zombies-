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
   (Enhancements > Extra Modes > Zombies of Time).
2. Night falls, a Redead scream plays and round 1 begins.
3. Undead crawl in from the edges of the area and walk toward Link. Every hit
   and kill earns points.
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
| Insta-Kill (30s) | Every undead dies in one hit | |
| Double Points (30s) | All points ×2 | |
| Nuke | Every undead on the map explodes, +400 points | |
| Fire Sale (30s) | Mystery Chest costs 10 | |
| Carpenter, barricades, doors | Phase 2 | They need an authored arena, see below |
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

With the mode on, press **L** anywhere to start a run there. Each station
is a torch; walk up to one and the HUD shows `L: MYSTERY CHEST [950]`.
Press **L** to buy. L normally toggles the minimap, which is harmless here.
The button can be switched to D-pad Down in the settings.

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
- Lower middle: the prompt for the nearest station.

## Arenas

The mode runs in any scene. At the start it samples the floor around Link with
the game's collision raycasts to find spawn points and station spots, so it
needs no per-scene data. Recommended arenas, all reachable in a normal file:

- **Kakariko Graveyard** (child or adult): the classic. Night, graves, tight
  paths. Our Nacht der Untoten.
- **Market ruins** (adult): Redeads already live here. Our Kino der Toten.
- **Lon Lon Ranch field** (either age): wide open, good for training hordes.
- **Hyrule Field** (either age): huge; turn Max Alive up.

Leaving the scene ends the run.

## Your save file

Purchases put real items into the loaded file. By default the mode snapshots
the inventory, equipment, upgrades, health and magic when a run starts and
restores them when the run ends (death, leaving the scene or turning the mode
off), so a run does not change a normal playthrough. Turn **Keep Items After
Run** on to keep what you bought.

## Settings

All under Enhancements > Extra Modes > Zombies of Time, stored as
`gEnhancements.Zombies.*` CVars:

| Setting | Default |
| --- | --- |
| Enabled | off |
| Max Undead Alive | 8 |
| Health Scaling | 100% |
| Starting Points | 500 |
| Starting Round | 1 |
| Wolfos Rounds | on |
| Horde Pull (undead always walk toward Link) | on |
| Force Night | on |
| Redead Scream (Redeads and Gibdos freeze Link) | on |
| Buy Button (L / D-pad Down) | L |
| Keep Items After Run | off |
| Best Round (read only) | |

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
| `ZombiesHud.cpp` | HUD drawing |
| `ZombiesMenu.cpp` | Menu widgets |

## Phases

1. **Core loop (this PR):** rounds, undead mix and scaling, Wolfos rounds,
   points, stations (Armory, Mystery Chest, four perks, power, Pack-a-Punch,
   Fairy Fountain), five power-ups, song abilities, HUD, save snapshot, menu.
2. **Authored arena:** a hand-placed layout for Kakariko Graveyard with
   barricades (wooden planks Link repairs for 10 points each, Carpenter
   power-up), paid gates between areas, and fixed station spots marked with
   in-world props.
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
