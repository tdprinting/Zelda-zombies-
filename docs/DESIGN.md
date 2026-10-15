# Zombies of Time: design

A round-based survival mode for Ocarina of Time, built into Ship of Harkinian
(SoH). The structure follows Call of Duty: Black Ops (2010) Zombies. Out of
the box every model, sound and item comes from Ocarina of Time. Players who
own Black Ops can import its zombie, gun, Mystery Box and board models,
animations, particle effects, zombies HUD art and the Nacht der Untoten map
from their own install with `tools/bomap` (see Phase 5); the game reads the
converted files at run time and falls back to OoT's look for anything not
imported. Nothing from Black Ops or Nintendo ships in this repository: no
Activision code or assets, and no Nintendo data (SoH extracts assets from the
player's own ROM).

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
| Hellhound round | Wolfos round: White Wolfos only | First on round 5 to 7, then every 4 to 5 rounds. Six per player in the first two, eight per player after that, at most two per player on the field at once. Each one arrives in a lightning strike and always sprints. Their health is set by the Wolfos round (half again each time), not the round number. The last Wolfos always drops Max Ammo |
| Round count | `24 + ⌊3 × m⌋`, then `× early-round factor` rounded down, at least 1 | `m = max(1, round/5)`; co-op uses `⌊6 × m × (players − 1)⌋` in place of solo's `⌊3 × m⌋`, and from round 10 `m ×= round × 0.15`. Rounds 1 to 5 use 25%, 30%, 50%, 70%, 90%. Every step truncates toward zero (`ZombiesRules.cpp`) |
| Max zombies alive (24) | Slider, default 24 | Lower it if a phone slows down |
| Player health: two hits down, five with Juggernog, regenerates | **Black Ops Health** (on): two hits put Link or player 2 down, five with Juggernog, however many hearts they have; health comes back in full 2.5 seconds after the last hit | Off: OoT hearts and damage, refilled each round |
| Zombie melee swipe | Undead swipe at arm's length; the Redead/Gibdo grab and scream freeze are off (the scream is a setting). Stalchildren and Wolfos keep their own attacks | Undead held at a window swipe through the boards at anyone standing just inside |
| Last zombie sprints (from round 3) | Same | |
| Crawlers | A bomb blast that doesn't kill an undead takes its legs 30% of the time: it drops low and crawls at half walking speed | |
| Moans | Each undead groans every 3 to 8 seconds and cries out when it swipes, with OoT's own Redead, Stalchild and Wolfos sounds | Black Ops' zombie and hellhound voices when imported ([Sounds](#sounds)) |
| Spawn delay `2.0s × 0.95^(round − 1)` | Same curve, floor 0.25s | |
| Zombie move speed (`round × 8` plus spread; walk ≤ 35, run ≤ 70, sprint above) | Same roll per undead, applied through Horde Pull at 2 / 4 / 6 units per frame | Round 1 is all walkers; from round 9 every undead sprints |
| Zombie health +100 per round, then ×1.1 | Health multiplier on each enemy's own HP: `1 + 0.15 × (round − 1)` to round 10, then ×1.08 per round | Capped at 255 (OoT stores HP in one byte). Slider scales it |
| Points: 10 per hit, 60 kill, 100 headshot, 130 knife | 10 per hit, 60 kill, 100 kill with a gun, arrow, seed or hookshot, 130 kill with a sword | A gun hit too small to show on an undead's health still pays its 10 |
| Start with 500 points | 500 | |
| Starting pistol | **M1911** on the guns' C button | See [Guns](#guns) |
| Wall buys | **Armory** station, a rack: **R** steps through it. First the next Zelda item for Link's age he doesn't have (an ammo refill, 500, once he has them all), then the wall guns at Black Ops prices, and their ammo at half price (4500 once Pack-a-Punched) | Child: Slingshot, Boomerang, Bombs. Adult: Bow, Bombs, Hookshot, Megaton Hammer. Guns: Olympia and M14 (500), MP5K and MP40 (1000) once the second zone is open, AK74u (1200) and Stakeout (1500) once the third is. The rack's current entry is chalked on the wall behind the torch, a white outline drawn at run time from the item's own GetItem model or the gun's imported world model, with its price chalked under it when Link is near |
| Mystery Box (950) | **Mystery Chest** (950): random weapon | Pool: Bombchus (Monkey Bomb), Fire/Ice Arrows, Din's Fire (Thundergun), Light Arrows (rare), Biggoron Sword (rare), Longshot, Hammer, and the box guns (Spectre, Galil, Commando, FAMAS, HK21, RPK, and the Ray Gun, rarest). Child pool swaps in Slingshot, Boomerang, Bombs. Never a gun Link already holds. About half of all prizes are guns (weights: Zelda items 1 to 4 each, box guns 3, Ray Gun 1, so the Ray Gun is about 1 in 38). Skull Kid laughing is the teddy bear: the points come back and the chest moves. As in Black Ops it never happens in the first four uses since the last move, is a 15% chance on uses 5 to 8, is certain from the ninth if the chest has never moved, and otherwise is 30% (50% from the 14th use) |
| Juggernog | **Juggernog Milk** (2500): Double Defense | |
| Quick Revive | **Fairy Revive**. Solo (500, no power needed): a bottled fairy. Co-op (1500): revives take half as long | Solo: a fairy in a bottle is OoT's own self-revive. Fills an empty bottle, else adds a bottle, else replaces the first bottle's contents. Using it counts as a down (5% of points and the perks go), and it sells out after three buys. Co-op has no self-revive |
| Double Tap | **Double Tap** (2000): every third hit lands twice, about the third more damage Black Ops' faster fire gives | |
| Stamin-Up | **Stamin-Up** (2000): Link runs 25% faster | |
| Power switch | **Power** station: free, lights the perk and Pack-a-Punch torches | Playing the Sun's Song anywhere also turns the power on |
| Pack-a-Punch (5000) | **Pack-a-Punch** (5000): upgrades one item | The gun in hand first, if Link fired it more recently than he used a Zelda item: more damage, a bigger clip and reserve, a new name, full ammo. Otherwise the C buttons (and the gun last, when nothing else is left). Bow: bigger quiver, then Light Arrows. Slingshot: bigger seed bag. Bombs: bigger bag. Hookshot: Longshot. Then the sword: Biggoron Sword (adult), then a Punched Blade whose hits land twice. Then the Mirror Shield (adult) |
| Power-ups (3% plus point-based drops on the team's points, 4 per round max) | Same drop logic; each power-up is a floating drop, dealt from a shuffled deck as in Black Ops | Max Ammo = big magic jar, Insta-Kill = red rupee, Double Points = gold rupee, Nuke = bomb drop, Fire Sale = purple rupee |
| Max Ammo | Refills sticks, nuts, seeds, arrows, bombs, bombchus and magic, and every gun's reserve | As in Black Ops the clip in the gun is left alone |
| Carpenter | Deku Stick drop: every window gets all six boards back, +200 points for every player | Only drops while a window is missing boards |
| Insta-Kill (30s) | Every undead dies in one hit | |
| Double Points (30s) | All points ×2 | |
| Nuke | Every undead on the map explodes, +400 points for every player | |
| Fire Sale (30s) | Mystery Chest costs 10 | Only drops once the chest has moved, as in Black Ops |
| Barricades: 6 boards per window, zombies tear them off, players repair for 10 points each | **Barricades**: boarded windows (two posts and six planks), two or three per zone on the hand-made maps and four around the start point anywhere else. Undead spawn outside one and are held there until it is open; each one at the boards tears one off every 1.5 seconds, so two at a window open it twice as fast. Hold the buy button at a window to put a board back every 0.6 seconds for 10 points, up to 500 points of repairs per round | Wolfos rounds ignore the windows, like hellhounds. The planks are the small wooden crate's model squashed flat. The windows are not solid: Link can walk around them, the undead can't |
| Doors and debris | **Debris** piles between the zones of an authored arena (1000 points into the second zone; 1000 or 1250 into the third) | Clearing one opens that zone's stations and windows. See [Kakariko Graveyard](#kakariko-graveyard) |
| Round chime, Samantha's voice | OoT sound effects | Round start: Redead scream. Round end: puzzle-solved chime. Power-up: item get jingle. Black Ops' power-up stings, hellhound round start and the rest when imported ([Sounds](#sounds)) |

## Zelda twists

The mode should feel like both games at once, so some systems come from
Ocarina of Time rather than Black Ops:

- **Songs are abilities.** The Sun's Song turns the power on from anywhere
  (and, as in the base game, freezes nearby Redeads, so it doubles as a panic
  button). The Song of Storms brings the game's own rainstorm, and the horde
  moves at half speed for 20 seconds. Zelda's Lullaby unseals Pack-a-Punch;
  a file that hasn't learned the Lullaby yet only needs the power on.
- **Hearts, Black Ops style.** Hearts are still drawn, but by default
  they behave like Black Ops health (two hits down, regenerating). With
  Black Ops Health off, Link heals the Zelda way: whatever the undead drop,
  the Fairy Fountain, and a full refill at the start of each round.
- **Magic is a resource.** Din's Fire, Nayru's Love and magic arrows draw on
  the magic meter, which only Max Ammo and magic jars refill.
- **Navi is Samantha.** Navi greets each run; round, power-up and power
  callouts use OoT's own sounds (Redead scream, Wolfos howl, puzzle chime).
  Phase 3 gives Navi the full announcer role with text bubbles.
- **Gold Skulltula easter egg.** Shooting three hidden Gold Skulltulas
  during a run plays a song, the way Black Ops maps hide their music easter
  eggs.
- **Rupees still matter.** Kills drop rupees as normal; rupees are spent at
  a Fairy Fountain station to top up hearts between rounds, so OoT's
  currency and Zombies points both have a job.

## Guns

Tee asked for the Call of Duty: Black Ops guns in Link's hands, with Black Ops' ammo
system, in the Armory and the Mystery Chest beside the Zelda items, and for the Zelda
items to stay worth using. The gameplay facts (damage, clips, reserves, fire rates,
reloads, prices, box odds) are written into our own table in `ZombiesGunTable.cpp`. No
Black Ops code or assets are in the repository.

### Controls

Third person stays the main view, on OoT's own camera.

- **The gun button** (C-Left, a setting) is the guns' alone during a run: items can't be
  equipped on it (one put there moves to a free button). Press to fire; hold for the
  automatics. An empty clip reloads by itself, as in Black Ops.
- **Hip-fire** goes where Link faces, with a little aim assist (12 degrees, line of
  sight), like a Black Ops console. **Z-targeting** aims at the locked-on undead.
- **C-Up**, OoT's own first-person look, is aiming down the sights: the imported Black
  Ops viewmodel (or, without it, Link's gloved hand and a stand-in in the lower right)
  with walk bob and idle sway, a crosshair, and a quarter of the hip-fire spread. Link can walk while
  aiming at 60% of his walk: with SoH's right-stick aiming on, the left stick moves and
  the right stick aims; without it, the stick turns the view left and right as usual and
  its forward/back walks. Movement goes through Link's normal movement, so walls stop him.
  A, B or R leave first person as usual. During a run Navi's C-Up hints are muted so
  C-Up always aims (her forced calls still come through).
- **The buy button** (L) reloads when there's nothing to buy, and holding it swaps guns,
  like Black Ops' use/reload button. At the Armory, **R** steps through the rack.
- Pressing the gun button with a Zelda item in hand puts the item away first. While Link
  climbs, swims, carries, swings, shields or aims the bow, the gun stays down.

Two guns, as in Black Ops (the data has room for a third, for a Mule Kick perk later).
Buying or finding a gun with both slots full replaces the one in hand.

### How a bullet hits

A shot is a hitscan ray (shotguns fire eight) from Link's chest, or from his eyes in
first person, to the first wall (`BgCheck_EntityLineTest1`), tested against each tracked
undead's collider cylinder. The top 22% of the cylinder is the head (headshot
multiplier). Rifles pass through two undead and machine guns three, the second and later
taking 75%. Shotgun pellets weaken past a quarter of their range. The Ray Gun fires a
green bolt (45 units a frame, in three steps so it can't skip through an undead) that
bursts on the first undead or wall; Mustang & Sally rounds burst where they hit.

Damage is in Black Ops units and is turned into OoT health so that **shots-to-kill match
Black Ops at every round**, whatever the enemy:
`hp = damage × enemy's full health / Black Ops zombie health on this round`, where Black
Ops zombie health is 150 on round 1, +100 a round to round 9, then ×1.1 a round (the
Health Scaling slider scales that growth as it does ours). Fractions carry to the next
hit. The damage goes onto `colChkInfo.health`; the mode's hit tracking pays the points
next frame and counts gun kills as ranged kills (100). A gun kill ends with OoT's own
enemy death flames and a drop, since zeroing an enemy's health doesn't start its own
death. Insta-Kill and Double Tap work on gun hits as on any other.

### The guns

Damage per bullet (per pellet for shotguns) in Black Ops units; fire rate is frames
between shots at OoT's 20 updates a second; reload is frames from empty (a reload with
rounds left takes 80%). Wall zone is the arena zone that has to be open first on the
hand-made maps.

| Gun | Class | Damage | Head | Clip + reserve | Fire | Reload | Where | Pack-a-Punched |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M1911 | Pistol | 20 | ×2 | 8 + 80 | 3 | 32 | start | Mustang & Sally: 100 + 600 splash, 12 + 50 |
| Olympia | Shotgun | 60 ×8 | ×1.5 | 2 + 38 | 6 | 54 | wall 500 | Hades: 120 ×8, 2 + 60 |
| M14 | Rifle | 105 | ×2.5 | 8 + 92 | 3 | 38 | wall 500 | Mnesia: 250, 15 + 135 |
| MP5K | SMG | 75 | ×2 | 30 + 120 | 1.5 | 48 | wall 1000, zone 2 | MP115 Kollider: 120, 40 + 200 |
| MP40 | SMG | 90 | ×2 | 32 + 192 | 2.3 | 50 | wall 1000, zone 2 | The Afterburner: 150, 64 + 384 |
| AK74u | SMG | 100 | ×2 | 20 + 160 | 1.6 | 46 | wall 1200, zone 3 | AK74fu2: 160, 40 + 280 |
| Stakeout | Shotgun | 80 ×8 | ×1.5 | 6 + 54 | 9 | 60 | wall 1500, zone 3 | Raid: 160 ×8, 10 + 72 |
| Spectre | SMG | 75 | ×2 | 30 + 120 | 1.2 | 44 | chest 3 | Phantasm: 120, 45 + 270 |
| Galil | Rifle | 100 | ×2.5 | 35 + 315 | 1.8 | 56 | chest 3 | Lamentation: 150, 50 + 400 |
| Commando | Rifle | 110 | ×2.5 | 30 + 270 | 1.6 | 42 | chest 3 | Predator: 160, 40 + 400 |
| FAMAS | Rifle | 100 | ×2.5 | 30 + 210 | 1.4 | 52 | chest 3 | G16-GL35: 150, 45 + 360 |
| HK21 | LMG | 130 | ×2 | 125 + 500 | 1.8 | 100 | chest 3 | H115 Oscillator: 200, 150 + 750 |
| RPK | LMG | 130 | ×2 | 100 + 400 | 1.6 | 90 | chest 3 | R115 Resonator: 200, 125 + 500 |
| Ray Gun | Launcher | 1000 + 300 splash | | 20 + 160 | 5 | 60 | chest 1 | Porter's X2 Ray Gun: 1500 + 500 splash, 40 + 200 |
| Kar98k | Rifle | 100 | ×4 | 5 + 50 | 22 | 50 | Nacht wall 200 | Armageddon 98: 600, 8 + 60 |
| M1A1 Carbine | Rifle | 120 | ×2.5 | 15 + 150 | 2.5 | 40 | Nacht wall 600 | Widdershins RC-1: 160, 30 + 180 |
| Thompson | SMG | 120 | ×2 | 20 + 200 | 1.6 | 52 | Nacht wall 1200 | Gibs-O-Matic: 200, 40 + 250 |
| Double-barrel | Shotgun | 80 ×8 | ×1.5 | 2 + 60 | 5 | 60 | Nacht wall 1200 | 24 Bore Long Range: 160 ×8 |
| Sawed-off | Shotgun | 100 ×8 | ×1.5 | 2 + 60 | 5 | 55 | Nacht wall 1200 | Snuff Box: 200 ×8 |
| Trench gun | Shotgun | 100 ×8 | ×1.5 | 6 + 60 | 8 | 70 | Nacht wall 1500 | Gut Shot: 200 ×8, 10 + 60 |
| BAR | LMG | 140 | ×2 | 20 + 180 | 2.6 | 56 | Nacht wall 1800 | The Widow Maker: 260, 30 + 300 |

The Thundergun is Din's Fire (below), so it isn't a gun here. Wall ammo costs half the
gun's price, 4500 once it's Pack-a-Punched; box guns only refill from Max Ammo, as in
Black Ops.

### Zelda items next to the guns

OoT items do fixed damage, so on their own they'd lose to the guns early and beat them
late. Each item has a Black Ops damage equivalent instead, matched to a gun of its tier
by damage per second at OoT's own rate of use (`OotWeaponBoDamage` in
`ZombiesRules.cpp`), turned into health the same way as a bullet, and never below the
item's own damage, which carries it in the late rounds where Black Ops health outgrows
ours. The sword is the knife and keeps its own damage.

| Item | Black Ops equivalent | Tier it matches |
| --- | --- | --- |
| Slingshot | 250 | the child's M14 (Armory, 500) |
| Bow | 500 | M14 / MP40 |
| Fire Arrow | 1000, and it burns | |
| Ice Arrow | 800, and it freezes | |
| Light Arrow | 2000 | Ray Gun, paid in magic |
| Boomerang | 300 | stuns, hits several, no ammo |
| Hookshot / Longshot | 600 / 800 | stuns and pulls |
| Megaton Hammer | 1500 | a Bowie knife and a half |
| Deku Stick | 500 | until it breaks |
| Bombs | 800 | a grenade: one-shots to round 7 |
| Bombchus | 1000 | Monkey Bomb slot |
| Din's Fire | kills anything in the blast | the Thundergun |
| Sword during Nayru's Love | 1000 | Bowie knife |

Hits to kill a Redead (8 HP at round 1), scaled (vanilla in brackets), against M14 body
shots:

| Round | 1 | 5 | 10 | 15 | 20 | 30 |
| --- | --- | --- | --- | --- | --- | --- |
| M14 (body) | 2 | 6 | 10 | 16 | 26 | 67 |
| Slingshot | 1 (8) | 3 (13) | 4 (19) | 7 (28) | 11 (41) | 30 (88) |
| Bow | 1 (4) | 2 (7) | 3 (10) | 4 (14) | 6 (21) | 15 (44) |
| Fire Arrow | 1 (4) | 1 (7) | 2 (10) | 2 (14) | 3 (21) | 7 (44) |
| Light Arrow | 1 (4) | 1 (7) | 1 (10) | 1 (14) | 2 (21) | 4 (44) |
| Hookshot | 1 (8) | 1 (13) | 2 (19) | 3 (28) | 5 (41) | 11 (88) |
| Hammer | 1 (2) | 1 (4) | 1 (5) | 2 (7) | 2 (11) | 5 (22) |
| Bombs | 1 (2) | 1 (4) | 2 (5) | 3 (7) | 4 (11) | 9 (22) |

Which item made a hit is read from the scene when the mode sees the health drop: a Din's
Fire actor, a bomb explosion near the undead, Link's swing and what's in his hand, the
item he's aiming, or the boomerang in flight. Player 2's hits keep their own damage.

### Link with a gun

Link's gun poses are our own keyframes: absolute rotations for his torso and arm joints
per gun class (one hand for pistols and the Ray Gun, two for the rest), with aim, sprint,
reload and lowered poses and a recoil kick (`kAdultPoses` in `ZombiesGuns.cpp`). The aim
keyframe was measured from OoT's own item-aiming pose and the others are built around
it; both ages share the table, as Link's two skeletons have the same joints. Just
before Link is drawn they are blended over his animated arms (sprint while running,
reload and swap timelines, recoil after each shot), and his hand models are swapped for
the gun; right after the draw everything is put back, so his own animations and items
are untouched, and nothing changes when the gun is down (three seconds after its last
use, or as soon as he swings, uses an item, climbs, swims or shields). The poses are a
table.

**Black Ops' player animations.** Tee asked for Link to hold, fire and reload guns the
way a Call of Duty player does. When the player has run `bomap playeranims` (see
`tools/bomap/README.md`), Link's body is posed from Black Ops' own third-person player
animations instead of the table above (`ZombiesPlayerAnim.cpp`). Nothing from those
files is in this repository: they are read at run time from the player's own import,
and the retargeting is our own code.

- *Which animations.* Body loops per weapon family (pistol, which the Ray Gun uses too,
  and rifle for every two-handed gun): standing alert, aiming (Z-targeting), walk and run
  forward, back, left and right, and sprint, chosen by Link's speed and the angle between
  where he moves and where he faces. Black Ops' torso animations play over the body:
  a shot (hip or aimed; per kind for SMGs and shotguns), a reload (per kind: pistol,
  rifle, SMG, shotgun, LMG; stretched to the gun's own reload time) and raise/put-away
  for a swap. Changes crossfade over six frames; a shot restarts the fire overlay; a new
  walk or run loop starts at the old one's phase, and loops play at the rate Link
  actually moves (0.5x to 2x).
- *Retargeting.* The Black Ops skeleton is posed (body layer, then torso layers bone by
  bone over it). Each of Link's limbs is tied to a Black Ops bone (pelvis, upper spine,
  head, shoulders, elbows, wrists, hips, knees, ankles). At start-up a reference frame is
  built per limb from the Black Ops bind pose (the bone's direction to its child, left
  as the second axis) and matched to Link's limb axes; each frame the limb's target
  orientation is the bone's world rotation times that offset, turned into Link's actor
  axes, then made local to Link's own parent limb and written back as OoT's Z-Y-X angles,
  blended with his own animation by the gun's raise weight. Root height follows the
  pelvis, scaled to Link's leg length (so the child stays his size). Rolling, jumping,
  falling and being knocked back keep OoT's own legs.
- *The gun.* It goes where Black Ops' `tag_weapon_right` is relative to the wrist, in
  Link's right hand limb's space, sized to his arms (a little over: 1.35 adult, 1.45
  child) about its grip, so the off hand lands near the foregrip as the animation has it.
- Player 2 uses the same code with their own state; both ages work. Without the import,
  or when a gun kind's animations are missing, the pose table above is used unchanged.

**Imported guns.** `Guns_ImportedModel(boName)` loads each gun's parts from the
importer's manifest (`gun_<boName>_world`, `_view`, `_anim_idle`, `_anim_fire`,
`_anim_reload`, `_anim_draw`; Black Ops' usual names when a role is missing) and keeps
them. In third person the world model goes into Link's closed right fist: the limb draw
(`VB_PLAYER_OVERRIDE_LIMB_DRAW`, right hand) prepends the gun, placed at its grip (a small
per-gun table, from the gun's `tag_weapon`), to the hand's display list, so it follows
the pose table. The two Links' fists take it differently (`kHandFit`): the child's small
bare fist holds the grip at the wrist with the gun shrunk to his size; the adult's
gauntlet is half again as big, so his gun is a little over true size and sits forward of
the knuckles and above the gauntlet, or a pistol would vanish into his fist. Aiming down the sights (C-Up) draws the Black Ops viewmodel, its arms
tinted to Link's tunic and gauntlets, from the eye as Black Ops does (twice as far and
twice the size, which looks the same and keeps clear of OoT's near plane, spread to Black
Ops' 65 degree field of view). It plays the idle, fire, reload and pullout animations off
the gun's state (pullout backwards to put a gun away); while aiming the sights
(`tag_front_sight`, `tag_iron_sight(s)`, `tag_rear_sight`) are brought in under the
crosshair, and reloads and swaps play at the hip. With the viewmodel imported, a reload
lasts as long as its animation (to its `end` notetrack; the shotguns' looped reload once
per shell), and its sound notetracks (`sndnt#<alias>`: magazine out and in, bolt,
rechamber) play Black Ops' own reload sounds when they are imported, or OoT's at its
`reload_clipout`, `reload_clipin` and `reload_rechamber` notetracks when not. Surfaces for attachments the zombies guns don't have (scopes,
silencers, grenade launchers, extended magazines, the clan tag) are left out.

Without the import Link holds an OoT stand-in: the hookshot (adult) or the slingshot
(child), and the first-person viewmodel is his gloved hand with that stand-in. Without
imported sounds the guns use OoT's (a bomb blast pitched up per gun class for shots, the
sword sheath for a reload, the hookshot click when it's done, the magic arrow for the Ray
Gun); with them each gun fires with its Black Ops weapon's own sound (the Pack-a-Punched
weapon's when it has one, another gun of its class's when it has none), and dry fire,
swaps and reloads are Black Ops' too ([Sounds](#sounds)).

### Sounds

`bomap gamesound` ([tools/bomap](../tools/bomap/README.md)) converts the zombies sounds
from the player's own zones into `zombies/sound/`: WAV files (16-bit PCM at the sound's
own rate; Black Ops' WMA is decoded with ffmpeg, its MS ADPCM in the tool) and
`manifest.txt`, which gives each alias its files and settings (volume and pitch ranges,
the distances it is heard over, looping) and names the alias for each of the game's sound
roles. `ZombiesSound.cpp` loads a file on first use and mixes into SoH's audio output
after OoT's synth (engine patch `0004`): each play picks one of the alias's variants and a
volume and pitch in its range, is resampled to the output rate, fades out between the
alias's near and far distance from the nearest player (split screen: either player's) and
is panned by where it is against that player's camera. At most 24 sounds play at once
(the oldest one-shot makes way), and the master and sound-effect volume settings apply.
Every role falls back to the OoT sound it replaces when it isn't imported.

| Role | Black Ops sound |
| --- | --- |
| Guns | Each weapon's first-person fire sound (with its layered action/ring tail), dry fire, raise and put away; reload parts from the view animation's sound notetracks |
| Buying | `cha_ching` on every purchase, `no_cha_ching` when it can't be bought, weapon pickup and ammo |
| Windows | Board snap and the zombie's snarl as it pulls one; repair and slam |
| Mystery Box | Lid open, the music box while it cycles, lid close, the teddy bear's laugh, the box lifting away, the poof and the landing |
| Power-ups | The spawn and its hum while on the floor, the grab, Max Ammo, Insta-Kill and nuke stings, Carpenter, the Insta-Kill and Double Points hums while they last, the end |
| Perks, power, Pack-a-Punch | Machine hum once the power is on, the jingle and drinking, the switch and power on, Pack-a-Punch's hum and upgrade |
| Zombies | Ambient groans (crawler and sprinter ones by their speed), attack and swipe, death, bullet hits and headshots, the rise, footsteps; hellhound voices for the Wolfos and the hellhound round start |
| Players | Hit, down |
| Nacht der Untoten | Its spooky one-shots around the players every 6 to 16 seconds and distant moans |

Black Ops streams its music (round start and end stings, the perk jingles, game over) and
most voice lines (the announcer's power-up calls, the players' quotes) from files outside
the zones, so those aren't imported; those roles keep OoT's sounds.

Every player has their own guns, ammo and gun state. Player 2 starts with the M1911 too:
Z fires (hip-fire where they face, with the same aim assist), a tap of A reloads and a
hold swaps when there's nothing to buy, and L steps through the Armory's guns (R is their
bombs). The Armory, Mystery Chest and Pack-a-Punch give guns to whoever uses them, and
player 2 holds theirs the way Link does, posed in both views. A player 2 gun kill pays
the ranged 100. The first-person viewmodel is off in split screen.

## Controls

In the zombies-only game, runs start from the main menu. With Zombies Only off
and the mode enabled, press **L** anywhere to start a run there. Each station
is a torch; walk up to one and the HUD shows `L: MYSTERY BOX [950]`.
Press **L** to buy, and hold it at a broken window to repair it. During a run
the mode takes the L press, so buying no longer toggles the minimap. The
button can be switched to D-pad Down in the settings.

**START** during a run opens the run menu and freezes the game: Resume,
Items (the Zelda pause screen, for equipping), Controls and End Run.

Torch colours tell stations apart: the golden torch is the Mystery Chest,
wooden torches are the Armory, the Fairy Fountain and the power switch, and
stone torches are the perks and Pack-a-Punch, dark until the power is on.

## HUD

Black Ops' zombies layout, drawn with Ocarina of Time's look (`ZombiesHud.cpp`). It is
anchored to the screen's edges, so it hugs the corners in widescreen too:

- Bottom left: the round in red chalk, as tally marks up to ten (a struck five, then
  the rest) and as chalk numerals after; the round flashes white when it changes. Owned
  perks sit above it as OoT's bottles (red potion Juggernog, bottled fairy Fairy Revive,
  blue potion Double Tap, green potion Stamin-Up) on discs in the perk's colour.
- Bottom right: a dial in place of Black Ops' d-pad ring, holding OoT's B button in the
  middle and C-Left, C-Down and C-Right around it, with OoT's item icons and ammo digits.
  The gun's button shows OoT's empty-button arrow. Up and left of it the points, in OoT's
  rupee-counter digits on a red banner with the rupee icon, `+10` / `+60` flying off its
  left end; under the banner OoT's magic as a thin green bar (when Link has magic). Left
  of the dial the gun's name and `clip / reserve`.
- Bottom centre: the timed power-ups (Insta-Kill as the Skull Mask, Double Points as a
  gold rupee x2, Fire Sale as Din's Fire), blinking in their last seconds.
- Centre: the crosshair while aiming, the reload hint, the prompt for the nearest
  station or window, and DOWN / BLED OUT. Announcements across the top.
- Health: Black Ops' red vignette at the screen's edges, stronger the lower Link's
  health, in place of the hearts.

During a run OoT's own HUD (buttons, hearts, magic, rupees, minimap) is faded out with
its visibility mode; the buttons work as before, only their drawing moved. With Black Ops
health off the hearts stay. Text uses OoT's message font, read from `oot.o2r` like every
OoT graphic.

Black Ops' own HUD images are used when the player has converted them from their
install: `bomap hud common_zombie.ff zombie_cod5_prototype.ff --main <Black Ops>/main --out <SoH>/zombies/hud`
writes the chalk marks, points bar, perk and power-up icons, the low health overlay and
the d-pad ring as PNGs named after the Black Ops material. Any that are missing are drawn
by the HUD's own code instead, so it never needs them.

It also writes each gun's Black Ops HUD icon as `icon_<weapon>.png`, and the HUD draws
the icon of the gun in hand left of its ammo count (the timed power-ups move left to keep
clear of it). Black Ops has no icon for the Ray Gun or the Olympia; `bomap` draws those
from the gun's world model instead. With no icon the HUD shows the name and ammo only.

Split screen: each player gets the whole HUD in their half, scaled down, with their own
points, perks, buttons (player 2's sword and bombs), gun and ammo, vignette and prompts.

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

### Nacht der Untoten

Once the player has imported Nacht der Untoten from their own Black Ops
(`tools/bomap`, see [Phases](#phases)), the menu's Nacht entry plays the
real map: its collision, textured geometry and entities from
`zombies/nacht.zzmap`. Until then, Play shows the import status and won't
start it. No spot is written into the code. Everything below comes from the
map's own entities when the run starts (`BuildLayout` in
`ZombiesBoMap.cpp`):

| Zone (from the map's zone volumes) | Opens with | Windows | Stations |
| --- | --- | --- | --- |
| Start room (the player starts) | | 4 | Armory and Fairy Fountain (500 points here, not rupees) on the two starts farthest apart, Fairy Revive |
| Help Room (upstairs) | Either staircase's debris, 1000; buying one clears both | 4 | Power, Juggernog, Stamin-Up |
| Box Room | The door, 1000 | 4 | Mystery Box, Pack-a-Punch, Double Tap |

- **Windows** sit where the map's barricades are, and face the way the map
  faces them. Undead come up outside, tear the boards (six, as everywhere),
  then climb through the opening over the sill, as in Black Ops. The opening
  has the map's player clip across it, so Link can't leave the building.
- **Debris and the door** are solid until bought. Each one is fitted to the
  narrowest crossing around its spot, so it fills the doorway or stair and
  nothing, Link, player 2 or the undead, gets past it.
- **Wall buys** are the map's own chalk spots, each selling the gun its
  entity names, with the gun's chalk outline drawn on the wall. Prices are
  Black Ops' for Nacht: Kar98k 200, M1A1 Carbine 600, Thompson,
  double-barrel and sawed-off 1200, trench gun 1500, BAR 1800, and four
  grenades (OoT bombs) for 250. Ammo is half the gun's price. On Nacht the
  Armory rack sells only the Zelda items, since the walls sell the guns.
- **The Mystery Box** stands at the map's box spot. Nacht has only one, so
  the box never moves and the bear never comes up. The Ray Gun is in it.
- **Perks and Pack-a-Punch are kept on Nacht**, although Black Ops' Nacht
  had none. They are part of how this mode plays (Fairy Revive, Zelda's
  Lullaby at Pack-a-Punch), and leaving them out would leave Nacht the one
  map without them. They stand on the zone volume farthest from the other
  spots, so they never block a wall buy, a window or the box.
- **No Wolfos rounds**, as Nacht had no hellhounds. Power-ups are Black
  Ops' five (Max Ammo, Insta-Kill, Double Points, Nuke, Carpenter). There is
  no Fire Sale, since the box never moves.
- **Pathing.** A building needs more than "Horde Pull" walking straight at
  Link. On Nacht the undead follow a walk grid made from the map's collision
  (`ZombiesNav.cpp`): 20-unit cells, a node for every floor with room to
  stand, joined where floors are a step apart and no wall stands between.
  Several times a second every node gets its walking distance to the nearest
  player, with closed debris blocking. Each undead heads a few nodes down that
  slope: across rooms, round corners and up the stairs.
- **A partial import** still plays. Missing wall buys fall back to the
  Armory's guns, and with too few usable windows they go around the start
  point. A station with no spot is placed around Link, and a zone with no
  debris spot starts open.

## Your save file

Purchases put real items into the loaded file. The mode snapshots the
inventory, equipment, upgrades, health and magic when a run starts, keeps a
copy in `Save/zombies_snapshot_<slot>.bin`, and restores and saves them when
the run ends (death, quitting, leaving the scene or turning the mode off). If
the game is reset or closed mid-run, the copy is put back the next time that
file loads, over anything an autosave wrote (with Autosave on, the real items
are also saved again when a run ends, after any autosave already queued). Saving from the pause screen is
blocked during a run. **Keep Items After Run** keeps purchases, outside the
zombies-only game only.

## Settings

All under Enhancements > Zombies of Time, stored as
`gEnhancements.Zombies.*` CVars:

| Setting | Default |
| --- | --- |
| Zombies Only | on |
| Enabled (the mode inside the normal game) | off |
| Max Undead Alive | 24 |
| Black Ops Health | on |
| Health Scaling | 100% |
| Starting Points | 500 |
| Starting Round | 1 |
| Wolfos Rounds | on |
| Barricades | on |
| Authored Arenas (the hand-made map layouts) | on |
| Show Coordinates | off |
| Horde Pull (undead always walk toward Link) | on |
| Force Night | on |
| Redead Scream (Redeads and Gibdos freeze Link) | off |
| Buy Button (L / D-pad Down) | L |
| Gun Button (C-Left / C-Down / C-Right) | C-Left |
| Keep Items After Run | off |
| Best Round (read only) | |

## Zombies-only game and main menu

"Zombies Only" (on by default) turns the whole game into the mode:

- Loading any file skips the intro and drops Link into the chosen map at night.
- The main menu opens over the map. Like Black Ops', it has a list on the left: Play, Map (Kakariko Graveyard, Market Ruins, Lon Lon Ranch, Hyrule Field, and Nacht der Untoten once it has been imported from the player's Black Ops; until then Play shows the import status and won't start it), Link's age (locked to adult for the Market Ruins), Starting round, Controls and Quit to title. The map's intel, your best round and a tip are on the right. The menu's choices are saved with SoH's settings. It is drawn in Zelda's colours, with a gold Triforce and a red title. Use the D-pad or stick with A, or tap or click an entry, so it works on Android touch controls.
- Leaving a zombie map warps Link back. After a run, a results screen shows the rounds survived, kills, points and best round, then the menu returns. A death skips the Save/Continue screens.
- START during a run opens the run menu (Resume, Items, Controls, End Run).
- Every run starts with the same kit:
  - the age's sword and shield
  - the M1911 on the gun button (the slingshot or bow is the Armory's first buy)
  - four bombs
  - the Fairy Ocarina, with Zelda's Lullaby, the Sun's Song and the Song of Storms
  - three hearts and 0 rupees
- Each round from 2 onwards adds two bombs, like Black Ops' grenades.
- Your real save inventory comes back when the run ends.
- Music: the Shadow Temple plays during rounds and the mini-boss theme during Wolfos rounds. Kill all three hidden Gold Skulltulas to hear the easter-egg song.

## How it is built

The mode is a SoH enhancement in `mod/soh/soh/Enhancements/Zombies/`. SoH's
CMake picks up every source file under `soh/soh/`, and the mode registers
itself through SoH's existing hooks. It also carries a few small engine
patches in `mod/patches/`, which `scripts/apply.sh` applies to the SoH tree
before copying the mode in:

- `0001-app-name-and-data-folder.patch`: the app's name and its own data folder
- `0002-executable-and-package-name.patch`: the executable and package names
- `0003-collision-vertex-ids-past-8191.patch`: collision polygons keep the top
  bits of their vertex ids in `vtxHi`, so an imported map can use up to 65535
  collision vertices
- `0004-zombies-sound-mix.patch`: the audio thread hands each output frame to the
  mode (`ZombiesSound_Mix`) before playing it, to mix in imported Black Ops sounds
- `mod/patches-android/0001-android-app-id-label-and-data-folder.patch`: the
  Android build's app id, label and data folder

OoT's actor code is untouched. The hooks:

| Hook | Use |
| --- | --- |
| `RegisterShipInitFunc` + `COND_HOOK` | Hooks attach only while the mode is on |
| `RegisterMenuInitFunc` | Adds the settings to the SoH menu |
| `OnPlayerUpdate` | Once per frame: round state machine, spawning, hit and kill tracking, stations, power-ups |
| `OnActorDestroy` | Forget undead and drops that were unloaded |
| `OnGameFrameUpdate` | HUD drawing, and fading OoT's own HUD out for a run |
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
| `ZombiesArena.cpp` | Authored layouts (graveyard, Market Ruins, Lon Lon Ranch, Hyrule Field), zones and debris (solid on Black Ops maps) |
| `ZombiesNav.cpp` | The walk grid the undead follow on a Black Ops map, built from its collision |
| `ZombiesLobby.cpp` | Main menu, run (pause) menu, results screen, map warps |
| `ZombiesExtras.cpp` | Navi callouts, music and the Gold Skulltula easter egg |
| `ZombiesBounds.cpp` | Keeps Link off the scene's exits during a run |
| `ZombiesCoop.cpp` | Split-screen co-op: player 2, second view, downs and revives |
| `ZombiesGuns.h/.cpp` | Black Ops guns: firing, reloads, the Armory rack and chest guns, Pack-a-Punch, Link's gun poses, the first-person viewmodel, OoT item round scaling, the imported-model seam |
| `ZombiesGunTable.cpp` | The gun table and the pure ammo and slot rules, unit tested |
| `ZombiesChalk.cpp` | The Armory's chalk outline, drawn at run time from the item's or gun's own model |
| `ZombiesHud.cpp` | HUD drawing, with Black Ops' HUD art from `zombies/hud/` when imported |
| `ZombiesMenu.cpp` | Menu widgets |
| `ZombiesBoMap.cpp` | Imported Black Ops map (`zombies/nacht.zzmap`): collision, display lists and the layout from its entities, played in the graveyard's slot |
| `ZombiesModel.h/.cpp` | Imported models and animations (`zombies/models/*.zzmodel`, `*.zzanim`, `manifest.txt`): posing, skinning, drawing |
| `ZombiesPlayerAnim.h/.cpp` | Black Ops' third-person player animations retargeted onto Link's skeleton while he holds a gun (both players) |
| `ZombiesZombie.cpp` | The Black Ops zombie actor, drawn with the imported model and driven by the mode's AI |
| `ZombiesBox.h/.cpp` | The imported Mystery Box: chest, lid, cycling guns and the bear |
| `ZombiesFx.h/.cpp` | Particle effects from imported `.zzfx` files, with built-in OoT-coloured fallbacks |
| `ZombiesSound.h/.cpp` | Imported Black Ops sounds (`zombies/sound/`): the roles, and the mixer added to SoH's audio output |

## Phases

1. **Core loop (this PR):** rounds, undead mix and scaling, Wolfos rounds,
   points, stations (Armory, Mystery Chest, four perks, power, Pack-a-Punch,
   Fairy Fountain), six power-ups, song abilities, HUD, save snapshot, menu.
2. **Authored arenas (this PR):** Kakariko Graveyard, Market Ruins, Lon Lon
   Ranch and Hyrule Field, each with three zones. What's left is solid debris
   (collision, so Link can't walk around it).
3. **Polish:** a zombies music track built from OoT sequences, perk jingles,
   and a boss round (Dead Hand or Iron Knuckle).
4. **Split-screen co-op (this PR).** Asked for by Tee. Ocarina of Time
   assumes exactly one Link and one camera, so co-op is built around that
   without patching the engine (`ZombiesCoop.cpp`):
   - **Player 2** is an actor of our own, the size of the game's Player
     struct, drawn with the game's own Link drawing code (red tunic), moved by
     the second controller with its own chase camera. Its sword is its own
     hitbox, its bombs are the game's bombs, and its guns are the mode's (Z to
     fire, as above).
   - **Split screen**: the game draws player 1's view into the top half of the
     screen; after the world draw, the sky, scene, rooms and actors are drawn
     again from player 2's camera into the bottom half.
   - **Own points and items**: points, kills, perks and kill credit are per
     player. Stations, windows and debris take a buyer (Link with L, player 2
     with A); player 2's purchases go to their own guns, sword level, bombs,
     health and perks, never to the save file.
   - **Downs**: a downed player loses 5% of their points and their perks,
     bleeds out after 45 seconds and returns the next round; holding the buy
     button for 3 seconds next to them revives them. When Link goes down his
     game over is held off while player 2 is standing, and the rest of the
     world keeps moving. Both down ends the run.
   - The undead go for whichever standing player is nearest. The game's own
     enemy AI only knows Link, so their swipes at player 2 are the mode's.

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

   As built, the import is a command-line tool, `tools/bomap`
   ([README](../tools/bomap/README.md)), run against the player's own
   Black Ops fastfiles (`zone/.../*.ff`, with `--main <Black Ops>/main` for the
   `.iwd` textures). It writes into the `zombies/` folder next to the game:
   - `bomap mapgeo` writes `zombies/nacht.zzmap`: Nacht der Untoten's
     collision, textured geometry and entities, from which the game places
     windows, wall buys, doors, debris, the box spots and perks
     ([BLACK_OPS_MAPS.md](BLACK_OPS_MAPS.md)).
   - `bomap gamemodel` / `gameanim` write `.zzmodel` / `.zzanim` files into
     `zombies/models/` (zombie, guns and viewmodels, Mystery Box, bear,
     barricade boards), and `bomap gamefx` writes `.zzfx` effects there.
     `zombies/models/manifest.txt` names which file plays each role.
   - `bomap hud` writes the zombies HUD images and gun icons as PNGs into `zombies/hud/`.
   - `bomap gamesound` writes the sounds as WAV files and their manifest into
     `zombies/sound/` ([Sounds](#sounds)).

   The game reads these at run time; anything missing falls back to the OoT
   look and sounds. Nothing converted is uploaded or bundled.
   The zombie AI behaviour (round sizes, move tiers, respawning stuck
   zombies, barricades) is already ported in phase 1 from the
   decompiled scripts, as a reference only.

