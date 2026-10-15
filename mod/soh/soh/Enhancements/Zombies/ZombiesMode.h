#pragma once

// Zombies of Time: run state shared by the mode, stations, HUD and menu.

#include "ZombiesRules.h"

#include <string>
#include <vector>

// GameInteractor.h pulls in z64.h with the C++ headers it depends on already included.
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#define CVAR_ZOMBIES(name) CVAR_ENHANCEMENT("Zombies." name)

namespace Zombies {

enum class Phase {
    Inactive,
    Intro,
    Active,
    Intermission,
    GameOver,
};

enum Perk {
    PERK_JUGGERNOG,
    PERK_FAIRY_REVIVE,
    PERK_DOUBLE_TAP,
    PERK_STAMIN_UP,
    PERK_COUNT,
};

enum class StationType {
    Armory,
    MysteryChest,
    Juggernog,
    FairyRevive,
    DoubleTap,
    StaminUp,
    Power,
    PackAPunch,
    FairyFountain,
    Count,
};

// An authored arena: fixed spots for windows, stations and debris in one scene,
// split into zones that open when Link pays to clear the debris between them.
// Spots are (x, z); heights come from the floor at run time.
struct LayoutSpot {
    float x;
    float z;
    float y = kNoHeight; // set on maps with floors above each other: the floor nearest this height

    static constexpr float kNoHeight = -32000.0f;
};

struct LayoutStation {
    StationType type;
    LayoutSpot spot;
};

struct LayoutZone {
    const char* name;
    int cost;          // points to clear the debris into this zone (0 for the start zone)
    LayoutSpot debris; // where the debris pile sits
    LayoutSpot centre; // windows face this way
    std::vector<LayoutSpot> windows;
    std::vector<LayoutStation> stations;
    std::vector<LayoutSpot> chestSpots; // where the Mystery Chest can move to
};

struct ArenaLayout {
    s16 sceneNum;
    const char* name;
    std::vector<LayoutZone> zones;
};

struct PlayerState {
    int points = 0;
    int totalEarned = 0;
    int kills = 0;
    bool perks[PERK_COUNT] = {};
};

struct TrackedEnemy {
    Actor* actor;
    EnemyKind kind;
    MoveTier tier;
    s16 lastHealth;
    Vec3f lastPos;
    bool killCounted;
    float closestDist;  // nearest it has come to Link since its last progress
    int stuckFrames;    // frames without getting closer
    int window = -1;    // barricade it is tearing through, or -1 once it is inside
    int hitBy = 0;      // player 2's last hit on it: 0 none, 1 sword, 2 bomb, 3 gun
    int hitFrames = 0;  // frames since player 2 last hit it
    int swipeTimer = 0; // frames until it can swipe at anyone again
    int tearTimer = 0;  // frames spent on the board it is pulling off
    int vocalTimer = 0; // frames until its next moan
    // Guns (ZombiesGuns.cpp): full health when it spawned, and the fraction of a hit point
    // the last bullet left over.
    s16 maxHealth = 0;
    float gunCarry = 0.0f;
};

// Player 2 in split-screen co-op: a second Link with their own health, points and
// weapons. Player 1 is the real Link and keeps the save's inventory.
enum PartnerSword {
    PARTNER_SWORD_KOKIRI,
    PARTNER_SWORD_MASTER,
    PARTNER_SWORD_BIGGORON,
};

struct Partner {
    Actor* actor = nullptr;
    int health = 48;
    int healthCapacity = 48;
    int sword = PARTNER_SWORD_KOKIRI;
    bool papSword = false;
    int bombs = 0;
    bool bombBag = false; // bought bombs: the Armory refills them from then on
    bool downed = false;
    bool bledOut = false; // waits for the next round
    int downFrames = 0;   // frames left before bleeding out
    int reviveFrames = 0; // progress of player 1's revive
    int invincible = 0;
    int sinceHit = 0; // frames since last hit: health comes back after a few seconds
    std::string prompt;
};

// Player 1 downed in co-op (the real Link lies where he fell until revived).
struct LinkDown {
    bool downed = false;
    bool bledOut = false;
    int downFrames = 0;
    int reviveFrames = 0;
};

// Someone at a station, window or debris pile this frame.
struct Buyer {
    int index = 0; // 0: Link, 1: player 2
    Vec3f pos = {};
    bool pressed = false;
    bool held = false;
    bool busy = false; // in a cutscene or a menu: can't buy
    std::string* prompt = nullptr;
};

// A Black Ops window: boards between the outside, where undead spawn, and the arena.
struct Window {
    Vec3f pos;      // centre of the boards, on the floor
    s16 yaw;        // facing into the arena
    Vec3f spawnPos; // outside, where undead climb out of the ground
    int boards = kBoardsPerWindow;
    bool beingTorn = false; // an undead is at the boards this frame
    int repairTimer = 0;
    int fallingBoard = 0; // frames left on the last torn board's fall
    int zone = 0;         // arena zone it belongs to; undead only use windows in open zones
};

struct Debris {
    int zone;
    Vec3f pos;
    s16 yaw;
};

struct ActiveDrop {
    Actor* actor;
    ActorFunc draw; // kept so the drop can blink before it expires
    PowerUp type;
    int framesLeft;
};

struct Announcement {
    std::string text;
    int framesLeft = 0;
};

struct PointPopup {
    int amount;
    int framesLeft;
    int player = 0;
};

struct Run {
    Phase phase = Phase::Inactive;
    s16 sceneNum = -1;
    Vec3f origin = {};
    s16 originYaw = 0;

    int round = 0;
    int bestRoundAtStart = 0;
    bool dogRound = false;
    int nextDogRound = 0;
    int dogRoundIndex = 0;

    int toSpawn = 0;    // undead left to spawn this round
    int spawnTimer = 0; // frames until next spawn attempt
    int phaseTimer = 0; // frames left in intro/intermission/game over

    std::vector<TrackedEnemy> enemies;
    std::vector<ActiveDrop> drops;
    DropTracker dropTracker;
    PowerUpDeck powerUpDeck;
    Vec3f lastKillPos = {};

    bool zombiesOnly = false; // Zombies Only when the run started; decides what happens to the items

    PlayerState players[kMaxPlayers];
    int playerCount = 1;

    int powerUpFrames[static_cast<int>(PowerUp::Count)] = {};
    bool powerOn = false;
    bool papUnlocked = false;
    int stormFrames = 0;

    std::vector<Window> windows;
    int repairPointsThisRound = 0;

    const ArenaLayout* layout = nullptr; // null: stations and windows are placed around the start point
    uint32_t openZones = 1;              // bit per layout zone
    std::vector<Debris> debris;

    Announcement announcement;
    std::vector<PointPopup> popups;
    std::string prompt;

    Partner partner;
    LinkDown linkDown;
};

Run& GetRun();

bool IsRunActive();
bool PowerUpActive(PowerUp p);

// Points
void AwardPoints(int playerIndex, int amount);
bool SpendPoints(int playerIndex, int amount);

void Announce(const std::string& text, int seconds = 3);

// Buy button: L, or D-pad Down if the setting says so.
const char* BuyButtonName();
bool BuyPressed(PlayState* play);
bool BuyHeld(PlayState* play);
// Who can buy this frame: Link unless he's down, then player 2 in co-op. Returns how many (at most 2).
int GetBuyers(PlayState* play, Player* player, Buyer* out);

// Floor height under (x, z) within reach of refY, avoiding deep water.
bool FloorAt(PlayState* play, float x, float z, float refY, float* outY);

// Barricades (ZombiesBarricades.cpp)
void Barricades_Place(PlayState* play);
bool Barricades_Enabled();
// Picks a window for a new undead. Returns -1 if there are no windows.
int Barricades_PickWindow();
// Keeps an undead outside its window until the boards are gone. Returns true while it is held.
bool Barricades_HoldEnemy(TrackedEnemy& enemy, float speed);
void Barricades_Update(PlayState* play, Player* player);
void Barricades_RepairAll();
bool Barricades_AnyBroken();
void Barricades_Draw();

// Music, Navi and the Gold Skulltula easter egg (ZombiesExtras.cpp)
void Navi(const std::string& text, u16 voice);
void Extras_OnRunStart(PlayState* play);
void Extras_OnRunEnd(PlayState* play, bool linkIsDead);
void Extras_OnRoundStart(bool dogRound);
void Extras_OnRoundEnd();
void Extras_OnSceneReload();
void Extras_OnActorDestroy(Actor* actor);
void Extras_Update(PlayState* play, Player* player);

// Arenas (ZombiesArena.cpp)
const ArenaLayout* Arena_ForScene(s16 sceneNum);

// The Black Ops zombie drawn with the imported model (ZombiesZombie.cpp). The mode moves
// it like the OoT undead; these tell it what it's doing so it can animate.
bool ZombieActor_Available();
s16 ZombieActor_Id();
bool ZombieActor_Is(Actor* actor);
bool ZombieActor_Busy(Actor* actor);
void ZombieActor_SetTier(Actor* actor, MoveTier tier);
void ZombieActor_SetTearing(Actor* actor, bool tearing);
void ZombieActor_Attack(Actor* actor);

// A Black Ops map converted from the player's own install (ZombiesBoMap.cpp).
std::string BoMap_Path();
bool BoMap_Available();
std::string BoMap_Status();
void BoMap_Select(bool selected);
bool BoMap_Selected();
bool BoMap_Active(PlayState* play);
const ArenaLayout* BoMap_Layout();
void BoMap_OnSceneInit(PlayState* play);
void BoMap_Update(PlayState* play);
// Floor under an authored spot, near refY. Authored spots can be on slightly different
// ground from where Link started, so this allows more height difference than FloorAt.
// justAbove: the spot has its own height a little above its floor (a Black Ops wall buy
// hangs at chest height), so the floor right under it wins over one higher up.
bool Arena_Resolve(PlayState* play, LayoutSpot spot, float refY, Vec3f* out, bool justAbove = false);
bool Arena_ZoneOpen(int zone);
void Arena_Start(PlayState* play);
void Arena_Update(PlayState* play, Player* player);
void Arena_DrawDebris(PlayState* play);
// Stations for a zone that just opened (ZombiesStations.cpp)
void Stations_OpenZone(PlayState* play, int zone);

} // namespace Zombies

// Draws the small wooden crate's model stretched to width x height x depth, at an offset
// (x, y, z) from base in the frame turned by yaw, rolled by roll radians. Used for planks,
// posts and debris (ZombiesBarricades.cpp).
extern "C" void ZombiesDraw_Crate(PlayState* play, const Vec3f& base, s16 yaw, float x, float y, float z, float roll,
                                  float width, float height, float depth);

namespace Zombies {

// Stations (ZombiesStations.cpp)
void Stations_Place(PlayState* play);
void Stations_Clear();
bool Stations_ChestPlaced();
bool Stations_ChestMoved(); // Black Ops only drops a Fire Sale once the box has moved
void Stations_Forget();
void Stations_NewRun(); // chest uses and the Punched Blade start over // the scene is gone: drop the torches without
                        // killing them
void Stations_Update(PlayState* play, Player* player);
void Stations_OnActorDestroy(Actor* actor);
int Stations_BoxCost();
bool Stations_PapSword(); // Pack-a-Punched blade: melee hits land twice
// What the Armory's wall shows Link right now: the rack entry he's looking at (R steps
// through it). gun is a GunId (0 for the Zelda item); ammo when he owns every item.
struct ArmoryWares {
    int gun = 0;
    u8 item = 0;
    bool ammo = false;
    int cost = 0;
};
bool Stations_ArmoryWares(Vec3f* pos, ArmoryWares* wares);
// Chalk outlines of the wall buys (ZombiesChalk.cpp), drawn with the world.
void Chalk_Draw(PlayState* play);

// Split-screen co-op (ZombiesCoop.cpp)
bool Coop_Enabled();   // the Players setting is 2
bool Coop_Active();    // a co-op run is going
bool Coop_SplitView(); // drawing two views this frame
void Coop_StartRun(PlayState* play, Player* player);
void Coop_EndRun(PlayState* play);
void Coop_Reset(); // the run is gone without ending (reset, another file, mode off)
void Coop_OnRoundStart(PlayState* play, Player* player);
void Coop_Update(PlayState* play, Player* player);
bool Coop_Buyer(PlayState* play, Buyer* out);
// Where the horde should go: the nearest player still standing. Returns false if none.
bool Coop_Target(const Vec3f& from, Vec3f* out, float* dist);
// Player 2's camera, for hearing sounds from their side of the split screen.
bool Coop_Listener(Vec3f* eye, Vec3f* at);
// Player 1 just started dying: keep him down for a revive instead. Returns true if handled.
bool Coop_OnLinkDeath(PlayState* play, Player* player);
void Coop_OnMainStart(PlayState* play);
void Coop_DrawBegin();
void Coop_DrawEnd();
void Coop_OnActorDestroy(Actor* actor);
// The player whose hit just landed on this enemy: 1 if player 2's sword or bomb, else 0.
int Coop_Credit(const TrackedEnemy& enemy);
// Player 2's purchases (their own weapons, perks and health)
void Coop_GiveSword(int sword);
void Coop_GiveBombs(int count);
void Coop_Heal();
// A down costs a cut of the player's points and all their perks (Juggernog's defense goes back).
void LoseOnDown(int index);
int Coop_MaxBombs();
// Player 2's controller this frame, for their guns: false when they can't act (no co-op
// run, down, or a cutscene or menu holds them). busy: mid-swing or mid-throw.
struct CoopPad {
    CONTROLLERBUTTONS_T cur = 0;
    CONTROLLERBUTTONS_T press = 0;
    bool busy = false;
};
bool Coop_Pad(PlayState* play, CoopPad* out);

// HUD (ZombiesHud.cpp)
void Hud_Draw();

// Keeps Link off the scene exits during a run (ZombiesBounds.cpp)
void Bounds_Scan(PlayState* play);
void Bounds_Update(PlayState* play, Player* player);
bool Bounds_NearExit(const Vec3f& pos);

// Inventory helpers (ZombiesStations.cpp)
void RefillAmmo();
void GiveLoadout(PlayState* play);

// Zombies-only game and its main menu (ZombiesLobby.cpp)
bool ZombiesOnly();
bool Lobby_IsOpen();
bool Pause_IsOpen();
void Lobby_Close(PlayState* play);       // Zombies Only switched off
void Pause_OnMainStart(PlayState* play); // every frame, before the game updates
// p2Kills < 0: a one player run.
void Lobby_SetResults(const std::string& reason, int rounds, int kills, int points, int p2Kills, int p2Points,
                      bool newBest);
void Lobby_Update(PlayState* play, Player* player);
void Lobby_OnSceneInit(s16 sceneNum);
void Lobby_OnLoadGame();
void Lobby_OnFrame(PlayState* play); // every frame, menus and game over included
// Starts a run where Link stands (ZombiesMode.cpp).
void Lobby_StartRunHere(PlayState* play);
// Ends the run from the run menu (ZombiesMode.cpp).
void Run_Quit();

} // namespace Zombies
