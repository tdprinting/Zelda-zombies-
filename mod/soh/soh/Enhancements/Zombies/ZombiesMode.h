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

// Stations (ZombiesStations.cpp)
void Stations_Place(PlayState* play);
void Stations_Clear();
void Stations_Update(PlayState* play, Player* player);
void Stations_OnActorDestroy(Actor* actor);
int Stations_BoxCost();
bool Stations_PapSword(); // Pack-a-Punched blade: melee hits land twice

// HUD (ZombiesHud.cpp)
void Hud_Draw();

// Inventory helpers (ZombiesStations.cpp)
void RefillAmmo();

} // namespace Zombies
