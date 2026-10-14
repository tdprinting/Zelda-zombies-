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
    float closestDist; // nearest it has come to Link since its last progress
    int stuckFrames;   // frames without getting closer
    int window = -1;   // barricade it is tearing through, or -1 once it is inside
};

// A Black Ops window: boards between the outside, where undead spawn, and the arena.
struct Window {
    Vec3f pos;      // centre of the boards, on the floor
    s16 yaw;        // facing into the arena
    Vec3f spawnPos; // outside, where undead climb out of the ground
    int boards = kBoardsPerWindow;
    int tearTimer = 0;
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
    Vec3f lastKillPos = {};

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
// Floor under an authored spot, near refY. Authored spots can be on slightly different
// ground from where Link started, so this allows more height difference than FloorAt.
bool Arena_Resolve(PlayState* play, LayoutSpot spot, float refY, Vec3f* out);
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
void Stations_Forget();
void Stations_NewRun(); // chest uses and the Punched Blade start over // the scene is gone: drop the torches without
                        // killing them
void Stations_Update(PlayState* play, Player* player);
void Stations_OnActorDestroy(Actor* actor);
int Stations_BoxCost();
bool Stations_PapSword(); // Pack-a-Punched blade: melee hits land twice

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
void Lobby_SetResults(const std::string& reason, int rounds, int kills, int points, bool newBest);
void Lobby_Update(PlayState* play, Player* player);
void Lobby_OnSceneInit(s16 sceneNum);
void Lobby_OnLoadGame();
void Lobby_OnFrame(PlayState* play); // every frame, menus and game over included
// Starts a run where Link stands (ZombiesMode.cpp).
void Lobby_StartRunHere(PlayState* play);
// Ends the run from the run menu (ZombiesMode.cpp).
void Run_Quit();

} // namespace Zombies
