// Zombies of Time: zombies-only game and its menus.
//
// With "Zombies Only" on, the whole game is the mode. After the file select, Link
// lands in a map at night and the main menu opens over it: a Black Ops style list
// down the left (play, map, Link's age, starting round, controls, quit) and the map's
// details on the right, in Zelda's gold with a Triforce in the corner.
//
// - After a run, a results screen (rounds, kills, points) comes first, then the menu.
// - During a run, START opens the run menu: resume, items (the Zelda pause screen),
//   controls, end run. The game is frozen while it is up.
//
// The menus are driven with the controller (D-pad or stick, A to pick, B to go back)
// and also by tapping or clicking, so they work with touch controls on Android.

#include "ZombiesMode.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/GuiWindow.h>
#include <ship/window/gui/ConsoleWindow.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "soh/ShipInit.hpp"
#include "soh/SohGui/MenuTypes.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
extern s16 gGameOverTimer;
}

namespace Zombies {

struct LobbyMap {
    const char* name;
    const char* shortName; // fits the menu list
    s16 sceneNum;
    s16 entrance;
    int requiredAge; // -1: either
    const char* intel;
};

static const LobbyMap kMaps[] = {
    { "KAKARIKO GRAVEYARD", "GRAVEYARD", SCENE_GRAVEYARD, ENTR_GRAVEYARD_ENTRANCE, -1,
      "Graves, a gate and the Royal Tomb. Start at the gate, clear the debris into the graves, then fight your way "
      "to the tomb for the power and Pack-a-Punch." },
    { "MARKET RUINS", "MARKET", SCENE_MARKET_RUINS, ENTR_MARKET_SOUTH_EXIT, LINK_AGE_ADULT, // adult at night: the ruins
      "Castle Town after Ganondorf. Tight streets and Redeads that have been waiting seven years. Adult Link only." },
    { "LON LON RANCH", "RANCH", SCENE_LON_LON_RANCH, ENTR_LON_LON_RANCH_ENTRANCE, -1,
      "Wide open paddock. Room to run a train of undead in circles around the fence." },
    { "HYRULE FIELD", "FIELD", SCENE_HYRULE_FIELD, ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN, -1,
      "Huge and dark, with nowhere to hide. Turn Max Undead Alive up in the settings for a real horde." },
};
static constexpr int kMapCount = sizeof(kMaps) / sizeof(kMaps[0]);

static const char* kTips[] = {
    "Play the Sun's Song to turn the power on from anywhere.",
    "Zelda's Lullaby unseals Pack-a-Punch.",
    "The Song of Storms slows the horde for 20 seconds.",
    "Hold the buy button at a window to nail the boards back up.",
    "Three Gold Skulltulas hide on every map. Find them all.",
    "Sword kills are worth the most points.",
    "A bottled fairy is your Quick Revive.",
    "Press START during a run for the run menu and your items.",
};
static constexpr int kTipCount = sizeof(kTips) / sizeof(kTips[0]);

enum class Screen {
    None,
    Main,
    Results,
    Pause,
    Controls,
};

enum MainItem { MAIN_PLAY, MAIN_MAP, MAIN_AGE, MAIN_ROUND, MAIN_CONTROLS, MAIN_QUIT, MAIN_COUNT };
enum PauseItem { PAUSE_RESUME, PAUSE_ITEMS, PAUSE_CONTROLS, PAUSE_END, PAUSE_COUNT };

struct Results {
    bool pending = false;
    std::string reason;
    int rounds = 0;
    int kills = 0;
    int points = 0;
    bool newBest = false;
};

static Screen sScreen = Screen::None;
static Screen sControlsReturn = Screen::Main; // where B goes from the controls page
static int sSelected = 0;
static int sStickCooldown = 0;
static bool sPendingStart = false; // warped to a map from the menu; start the run on arrival
static bool sOpenItemsNextFrame = false;
static bool sWarpPending = false; // Link wandered off the zombie maps; send him back
static bool sSwallowInput = false;
static bool sLobbyReady = false; // the game can take a menu choice now (taps check this) // a menu closed this frame:
                                 // its button press stays with it
static int sTipIndex = 0;
static Results sResults;

bool ZombiesOnly() {
    return CVarGetInteger(CVAR_ZOMBIES("ZombiesOnly"), 1) != 0;
}

static void SaveSettings() {
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

static int MapIndex() {
    return std::clamp(CVarGetInteger(CVAR_ZOMBIES("LobbyMap"), 0), 0, kMapCount - 1);
}

static int PreferredAge() {
    return CVarGetInteger(CVAR_ZOMBIES("LobbyAge"), LINK_AGE_CHILD) == LINK_AGE_ADULT ? LINK_AGE_ADULT : LINK_AGE_CHILD;
}

// The age Link will be on the chosen map: some maps only exist for one age.
static int ChosenAge() {
    const LobbyMap& map = kMaps[MapIndex()];
    return map.requiredAge >= 0 ? map.requiredAge : PreferredAge();
}

static void ChangeMap(int delta) {
    CVarSetInteger(CVAR_ZOMBIES("LobbyMap"), (MapIndex() + delta + kMapCount) % kMapCount);
    SaveSettings();
}

static void ChangeRound(int delta) {
    int round = std::clamp(CVarGetInteger(CVAR_ZOMBIES("StartingRound"), 1) + delta, 1, 50);
    CVarSetInteger(CVAR_ZOMBIES("StartingRound"), round);
    SaveSettings();
}

static bool ToggleAge() {
    if (kMaps[MapIndex()].requiredAge >= 0) {
        return false;
    }
    CVarSetInteger(CVAR_ZOMBIES("LobbyAge"), PreferredAge() == LINK_AGE_CHILD ? LINK_AGE_ADULT : LINK_AGE_CHILD);
    SaveSettings();
    return true;
}

bool Lobby_IsOpen() {
    return sScreen == Screen::Main || sScreen == Screen::Results ||
           (sScreen == Screen::Controls && sControlsReturn == Screen::Main);
}

bool Pause_IsOpen() {
    return sScreen == Screen::Pause || (sScreen == Screen::Controls && sControlsReturn == Screen::Pause);
}

void Lobby_SetResults(const std::string& reason, int rounds, int kills, int points, bool newBest) {
    sResults = { true, reason, rounds, kills, points, newBest };
}

// Taps arrive from the draw, outside the game's update: take them only when the game
// could take the same choice from the controller.
static bool TapAllowed() {
    return Pause_IsOpen() || sLobbyReady;
}

static void Open(Screen screen) {
    sScreen = screen;
    sSelected = 0;
}

static void CloseMenus(PlayState* play) {
    sScreen = Screen::None;
    sLobbyReady = false;
    sSwallowInput = true;
    if (play != nullptr) {
        GET_PLAYER(play)->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
        Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_ALL);
        play->frameAdvCtx.enabled = false;
    }
}

// Zombies Only was switched off: let go of Link and the HUD.
void Lobby_Close(PlayState* play) {
    if (sScreen != Screen::None) {
        CloseMenus(play);
    }
    sPendingStart = false;
    sWarpPending = false;
}

// Changing age goes through linkAgeOnLoad: leaving the scene then swaps the child and
// adult equipment the way the Master Sword pedestal does.
static void WarpToMap(PlayState* play, const LobbyMap& map) {
    play->linkAgeOnLoad = static_cast<u8>(ChosenAge());
    play->nextEntranceIndex = map.entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.dayTime = 0;
    gSaveContext.nightFlag = 1;
    gSaveContext.showTitleCard = false; // the menu names the map
}

static void StartPlaying(PlayState* play) {
    const LobbyMap& map = kMaps[MapIndex()];
    CloseMenus(play);
    Sfx_PlaySfxCentered(NA_SE_SY_FSEL_DECIDE_L);
    if (play->sceneNum != map.sceneNum || gSaveContext.linkAge != ChosenAge()) {
        sPendingStart = true;
        WarpToMap(play, map);
        return;
    }
    Lobby_StartRunHere(play);
}

static void QuitToTitle() {
    sScreen = Screen::None;
    std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))
        ->Dispatch("reset");
}

static void ShowControls(Screen from) {
    sControlsReturn = from;
    sScreen = Screen::Controls;
    Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
}

static int ItemCount() {
    switch (sScreen) {
        case Screen::Main:
            return MAIN_COUNT;
        case Screen::Pause:
            return PAUSE_COUNT;
        default:
            return 1;
    }
}

static void Back(PlayState* play) {
    switch (sScreen) {
        case Screen::Controls:
            sScreen = sControlsReturn;
            Sfx_PlaySfxCentered(NA_SE_SY_CANCEL);
            break;
        case Screen::Pause:
            CloseMenus(play);
            Sfx_PlaySfxCentered(NA_SE_SY_CANCEL);
            break;
        default:
            break;
    }
}

static void Activate(PlayState* play, int item) {
    switch (sScreen) {
        case Screen::Results:
            Open(Screen::Main);
            Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
            return;
        case Screen::Controls:
            Back(play);
            return;
        case Screen::Main:
            switch (item) {
                case MAIN_PLAY:
                    StartPlaying(play);
                    return;
                case MAIN_MAP:
                    ChangeMap(1);
                    break;
                case MAIN_AGE:
                    if (!ToggleAge()) {
                        Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
                        return;
                    }
                    break;
                case MAIN_ROUND:
                    ChangeRound(1);
                    break;
                case MAIN_CONTROLS:
                    ShowControls(Screen::Main);
                    return;
                case MAIN_QUIT:
                    QuitToTitle();
                    return;
            }
            Sfx_PlaySfxCentered(NA_SE_SY_FSEL_CURSOR);
            return;
        case Screen::Pause:
            switch (item) {
                case PAUSE_RESUME:
                    Back(play);
                    return;
                case PAUSE_ITEMS:
                    // The Zelda pause screen opens on the next frame, once the game runs again.
                    CloseMenus(play);
                    sOpenItemsNextFrame = true;
                    return;
                case PAUSE_CONTROLS:
                    ShowControls(Screen::Pause);
                    return;
                case PAUSE_END:
                    CloseMenus(play);
                    Run_Quit();
                    return;
            }
            return;
        default:
            return;
    }
}

static void Adjust(int item, int delta) {
    if (sScreen != Screen::Main) {
        return;
    }
    switch (item) {
        case MAIN_MAP:
            ChangeMap(delta);
            break;
        case MAIN_AGE:
            if (!ToggleAge()) {
                Sfx_PlaySfxCentered(NA_SE_SY_ERROR);
                return;
            }
            break;
        case MAIN_ROUND:
            ChangeRound(delta);
            break;
        default:
            return;
    }
    Sfx_PlaySfxCentered(NA_SE_SY_FSEL_CURSOR);
}

// D-pad or stick to move, left/right to change a value, A to pick, B to go back.
static void HandleMenuInput(PlayState* play, const Input& input) {
    u16 press = static_cast<u16>(input.press.button);
    s8 stickY = input.rel.stick_y;
    s8 stickX = input.rel.stick_x;
    if (sStickCooldown > 0) {
        sStickCooldown--;
    }
    int vertical = 0;
    int horizontal = 0;
    if (press & BTN_DUP) {
        vertical = -1;
    } else if (press & BTN_DDOWN) {
        vertical = 1;
    } else if (press & BTN_DLEFT) {
        horizontal = -1;
    } else if (press & BTN_DRIGHT) {
        horizontal = 1;
    } else if (sStickCooldown == 0) {
        if (stickY > 40) {
            vertical = -1;
        } else if (stickY < -40) {
            vertical = 1;
        } else if (stickX < -40) {
            horizontal = -1;
        } else if (stickX > 40) {
            horizontal = 1;
        }
        if (vertical != 0 || horizontal != 0) {
            sStickCooldown = 5;
        }
    } else if (stickX > -20 && stickX < 20 && stickY > -20 && stickY < 20) {
        sStickCooldown = 0; // stick back in the middle: the next push moves at once
    }
    int count = ItemCount();
    if (vertical != 0 && count > 1) {
        sSelected = (sSelected + vertical + count) % count;
        Sfx_PlaySfxCentered(NA_SE_SY_FSEL_CURSOR);
    }
    if (horizontal != 0) {
        Adjust(sSelected, horizontal);
    }
    if (CHECK_BTN_ALL(press, BTN_A)) {
        Activate(play, sSelected);
    } else if (CHECK_BTN_ALL(press, BTN_B) || (sScreen == Screen::Pause && CHECK_BTN_ALL(press, BTN_START))) {
        Back(play);
    }
}

// Called from the mode's per-frame update while no run is going.
void Lobby_Update(PlayState* play, Player* player) {
    if (!ZombiesOnly()) {
        if (Lobby_IsOpen()) {
            CloseMenus(play);
        }
        return;
    }
    bool ready = play->transitionTrigger == TRANS_TRIGGER_OFF && play->transitionMode == TRANS_MODE_OFF &&
                 play->msgCtx.msgMode == MSGMODE_NONE && (player->actor.bgCheckFlags & 1) &&
                 play->gameOverCtx.state == GAMEOVER_INACTIVE && play->pauseCtx.state == 0 &&
                 !Player_InCsMode(play); // e.g. walking in through the ranch gate
    sLobbyReady = ready;
    if (!ready) {
        return;
    }
    if (sWarpPending) {
        sWarpPending = false;
        WarpToMap(play, kMaps[MapIndex()]);
        return;
    }
    if (sPendingStart) {
        sPendingStart = false;
        Lobby_StartRunHere(play);
        return;
    }
    if (!Lobby_IsOpen()) {
        Open(sResults.pending ? Screen::Results : Screen::Main);
        sResults.pending = false;
        sTipIndex = static_cast<int>(Rand_ZeroOne() * kTipCount) % kTipCount;
        Sfx_PlaySfxCentered(NA_SE_SY_FSEL_DECIDE_S);
    }

    // The menu sits over the map at midnight, whatever the clock does in the field.
    gSaveContext.dayTime = 0;
    gSaveContext.nightFlag = 1;

    // Link waits while the menu is up, and the game's HUD stays out of the way.
    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;
    if (gSaveContext.hudVisibilityMode != HUD_VISIBILITY_NOTHING) {
        Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_NOTHING);
    }
    HandleMenuInput(play, play->state.input[0]);
}

// Runs before every game frame, paused or not (OnGameStateMainStart): the run menu
// freezes the game with the frame-advance switch, so it can't live in a player update.
void Pause_OnMainStart(PlayState* play) {
    if (play == nullptr) {
        return;
    }
    Input& input = play->state.input[0];
    if (Lobby_IsOpen()) {
        input.press.button &= static_cast<u16>(~BTN_START); // no Zelda pause screen behind the menu
    }
    if (sSwallowInput) {
        sSwallowInput = false;
        input.press.button = 0;
        input.cur.button = 0;
    }
    if (sOpenItemsNextFrame) {
        sOpenItemsNextFrame = false;
        input.press.button |= BTN_START; // let the Zelda pause screen open
        return;
    }
    bool inRun = ZombiesOnly() && IsRunActive() && GetRun().phase != Phase::GameOver;
    if (!inRun) {
        if (Pause_IsOpen()) {
            CloseMenus(play);
        }
        return;
    }
    if (!Pause_IsOpen()) {
        bool canPause = play->pauseCtx.state == 0 && play->pauseCtx.debugState == 0 &&
                        play->gameOverCtx.state == GAMEOVER_INACTIVE && play->transitionTrigger == TRANS_TRIGGER_OFF &&
                        play->transitionMode == TRANS_MODE_OFF && play->msgCtx.msgMode == MSGMODE_NONE &&
                        !Player_InCsMode(play);
        if (canPause && CHECK_BTN_ALL(input.press.button, BTN_START)) {
            input.press.button &= ~BTN_START; // the Zelda pause screen stays shut
            Open(Screen::Pause);
            play->frameAdvCtx.enabled = true;
            Sfx_PlaySfxCentered(NA_SE_SY_FSEL_DECIDE_S);
        }
        return;
    }
    play->frameAdvCtx.enabled = true;
    HandleMenuInput(play, input);
    // Nothing reaches the game while the menu is up, or on the frame it closes.
    if (Pause_IsOpen() || sSwallowInput) {
        sSwallowInput = false;
        input.press.button = 0;
        input.cur.button = 0;
    }
}

void Lobby_OnSceneInit(s16 sceneNum) {
    if (!ZombiesOnly()) {
        return;
    }
    // Keep the game on zombie maps: anything else sends Link back to the chosen map.
    bool isMap = false;
    for (const LobbyMap& map : kMaps) {
        isMap = isMap || map.sceneNum == sceneNum;
    }
    if (!isMap) {
        // The scene is still loading and would overwrite a transition set now.
        sPendingStart = false;
        sWarpPending = true;
    }
}

// Zombies only: no "Save? Continue?" screens after a death. Link lies there for a moment
// under the game over banner, then the map reloads and the results come up.
void Lobby_OnFrame(PlayState* play) {
    static int sDeadFrames = 0;
    if (!ZombiesOnly() || play == nullptr || play->gameOverCtx.state != GAMEOVER_DEATH_DELAY_MENU) {
        sDeadFrames = 0;
        return;
    }
    if (++sDeadFrames < 3 * 20) {
        gGameOverTimer = 2; // hold off the vanilla menu
        return;
    }
    sDeadFrames = 0;
    play->gameOverCtx.state = GAMEOVER_INACTIVE;
    gSaveContext.health = gSaveContext.healthCapacity;
    gSaveContext.healthAccumulator = 0;
    sPendingStart = false;
    sScreen = Screen::None;
    WarpToMap(play, kMaps[MapIndex()]);
}

// After the file select: skip the story and go straight to the chosen map at night.
void Lobby_OnLoadGame() {
    sScreen = Screen::None;
    sPendingStart = false;
    sOpenItemsNextFrame = false;
    sWarpPending = false;
    sResults.pending = false;
    if (!ZombiesOnly()) {
        return;
    }
    gSaveContext.entranceIndex = kMaps[MapIndex()].entrance;
    if (gSaveContext.linkAge != ChosenAge()) {
        Inventory_SwapAgeEquipment(); // uses the age Link has now
        gSaveContext.linkAge = ChosenAge();
    }
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.nextCutsceneIndex = 0xFFEF;
    gSaveContext.dayTime = 0;
    gSaveContext.nightFlag = 1;
    gSaveContext.showTitleCard = false; // the menu names the map
}

// ---------------------------------------------------------------------------
// Drawing

static const ImVec4 kGold(0.93f, 0.78f, 0.25f, 1.0f);
static const ImVec4 kBlood(0.78f, 0.06f, 0.06f, 1.0f);
static const ImVec4 kGrey(0.6f, 0.6f, 0.6f, 1.0f);
static const ImVec4 kWhite(1.0f, 1.0f, 1.0f, 1.0f);

class LobbyWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void Draw() override {
        if (sScreen == Screen::None || gPlayState == nullptr || !ZombiesOnly()) {
            return;
        }
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.03f, 0.82f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("Zombies of Time##Lobby", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        DrawElement();
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

  protected:
    void InitElement() override {
    }
    void UpdateElement() override {
    }

    void DrawElement() override {
        size = ImGui::GetWindowSize();
        origin = ImGui::GetWindowPos();
        scale = std::max(size.y / 720.0f, 0.6f);

        DrawTriforce();
        switch (sScreen) {
            case Screen::Main:
                DrawTitle("ZOMBIES OF TIME", "SURVIVE THE NIGHT IN HYRULE");
                DrawMain();
                break;
            case Screen::Results:
                DrawTitle("GAME OVER", sResults.reason.c_str());
                DrawResults();
                break;
            case Screen::Pause:
                DrawTitle("PAUSED", GetRun().layout != nullptr ? GetRun().layout->name : kMaps[MapIndex()].name);
                DrawPause();
                break;
            case Screen::Controls:
                DrawTitle("CONTROLS", "HOW TO SURVIVE");
                DrawControls();
                break;
            default:
                break;
        }
        ImGui::SetWindowFontScale(1.0f);
    }

  private:
    ImVec2 size;
    ImVec2 origin;
    float scale = 1.0f;

    void DrawTriforce() {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float tri = 46.0f * scale;
        ImVec2 top(origin.x + size.x - 90.0f * scale, origin.y + 40.0f * scale);
        ImU32 gold = ImGui::GetColorU32(kGold);
        auto triangle = [&](ImVec2 apex) {
            draw->AddTriangleFilled(apex, ImVec2(apex.x - tri * 0.5f, apex.y + tri * 0.866f),
                                    ImVec2(apex.x + tri * 0.5f, apex.y + tri * 0.866f), gold);
        };
        triangle(top);
        triangle(ImVec2(top.x - tri * 0.5f, top.y + tri * 0.866f));
        triangle(ImVec2(top.x + tri * 0.5f, top.y + tri * 0.866f));
    }

    // Title, Black Ops style: big, red, top left.
    void DrawTitle(const char* title, const char* subtitle) {
        ImGui::SetCursorPos(ImVec2(60.0f * scale, 40.0f * scale));
        ImGui::SetWindowFontScale(3.2f * scale);
        ImGui::TextColored(kBlood, "%s", title);
        ImGui::SetWindowFontScale(1.3f * scale);
        ImGui::SetCursorPosX(64.0f * scale);
        ImGui::TextColored(kGold, "%s", subtitle);
    }

    void DrawList(const std::vector<std::string>& labels, const std::vector<bool>& dim) {
        ImGui::SetWindowFontScale(1.9f * scale);
        float y = 190.0f * scale;
        for (int i = 0; i < static_cast<int>(labels.size()); i++) {
            ImGui::SetCursorPos(ImVec2(60.0f * scale, y));
            bool selected = i == sSelected;
            ImVec4 color = selected ? kGold : (dim[i] ? kGrey : kWhite);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.55f, 0.05f, 0.05f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.55f, 0.05f, 0.05f, 0.35f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.7f, 0.08f, 0.08f, 0.6f));
            std::string text = (selected ? "> " : "  ") + labels[i] + "##zmenu" + std::to_string(i);
            if (ImGui::Selectable(text.c_str(), selected, 0, ImVec2(size.x * 0.48f, 0))) {
                // Tapping a value row steps it; tapping anything else does it.
                sSelected = i;
                if (TapAllowed()) {
                    Activate(gPlayState, i);
                }
            }
            ImGui::PopStyleColor(4);
            y += 52.0f * scale;
        }
    }

    void DrawFooter(const char* tip, const char* hints) {
        ImGui::SetWindowFontScale(1.1f * scale);
        ImGui::SetCursorPos(ImVec2(60.0f * scale, size.y - 90.0f * scale));
        if (tip != nullptr) {
            ImGui::TextColored(kGold, "TIP: %s", tip);
        }
        ImGui::SetCursorPos(ImVec2(60.0f * scale, size.y - 60.0f * scale));
        ImGui::TextColored(kGrey, "%s", hints);
    }

    void DrawMain() {
        const LobbyMap& map = kMaps[MapIndex()];
        bool ageLocked = map.requiredAge >= 0;
        std::vector<std::string> labels = {
            "PLAY",
            std::string("MAP      < ") + map.shortName + " >",
            std::string("LINK     ") + (ageLocked ? "  " : "< ") + (ChosenAge() == LINK_AGE_CHILD ? "CHILD" : "ADULT") +
                (ageLocked ? "" : " >"),
            "ROUND    < " + std::to_string(CVarGetInteger(CVAR_ZOMBIES("StartingRound"), 1)) + " >",
            "CONTROLS",
            "QUIT TO TITLE",
        };
        DrawList(labels, { false, false, ageLocked, false, false, false });

        // Map intel, right.
        float right = size.x * 0.58f;
        ImGui::SetWindowFontScale(1.8f * scale);
        ImGui::SetCursorPos(ImVec2(right, 190.0f * scale));
        ImGui::TextColored(kGold, "%s", map.name);
        ImGui::SetWindowFontScale(1.15f * scale);
        ImGui::SetCursorPosX(right);
        ImGui::PushTextWrapPos(size.x - 60.0f * scale);
        ImGui::TextWrapped("%s", map.intel);
        ImGui::PopTextWrapPos();
        ImGui::SetCursorPosX(right);
        ImGui::TextColored(kBlood, "BEST ROUND: %d", CVarGetInteger(CVAR_ZOMBIES("BestRound"), 0));

        DrawFooter(kTips[sTipIndex], "D-PAD / STICK: MOVE    LEFT / RIGHT: CHANGE    A: SELECT");
    }

    void DrawResults() {
        ImGui::SetWindowFontScale(2.4f * scale);
        ImGui::SetCursorPos(ImVec2(60.0f * scale, 200.0f * scale));
        ImGui::TextColored(kWhite, "YOU SURVIVED %d %s", sResults.rounds, sResults.rounds == 1 ? "ROUND" : "ROUNDS");
        ImGui::SetWindowFontScale(1.7f * scale);
        float y = 280.0f * scale;
        auto row = [&](const char* label, int value) {
            ImGui::SetCursorPos(ImVec2(80.0f * scale, y));
            ImGui::TextColored(kGold, "%s", label);
            ImGui::SetCursorPos(ImVec2(360.0f * scale, y));
            ImGui::TextColored(kWhite, "%d", value);
            y += 44.0f * scale;
        };
        row("KILLS", sResults.kills);
        row("POINTS EARNED", sResults.points);
        row("BEST ROUND", CVarGetInteger(CVAR_ZOMBIES("BestRound"), 0));
        if (sResults.newBest) {
            ImGui::SetCursorPos(ImVec2(80.0f * scale, y + 10.0f * scale));
            ImGui::TextColored(kBlood, "NEW BEST!");
        }
        ImGui::SetCursorPos(ImVec2(60.0f * scale, size.y - 140.0f * scale));
        ImGui::SetWindowFontScale(1.9f * scale);
        ImGui::PushStyleColor(ImGuiCol_Text, kGold);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.55f, 0.05f, 0.05f, 0.55f));
        if (ImGui::Selectable("> CONTINUE##zresults", true, 0, ImVec2(size.x * 0.4f, 0))) {
            if (TapAllowed()) {
                Activate(gPlayState, 0);
            }
        }
        ImGui::PopStyleColor(2);
        DrawFooter(nullptr, "A: CONTINUE");
    }

    void DrawPause() {
        const Run& run = GetRun();
        DrawList({ "RESUME", "ITEMS", "CONTROLS", "END RUN" }, { false, false, false, false });

        float right = size.x * 0.58f;
        ImGui::SetWindowFontScale(1.8f * scale);
        ImGui::SetCursorPos(ImVec2(right, 190.0f * scale));
        ImGui::TextColored(kGold, "ROUND %d", std::max(run.round, 1));
        ImGui::SetWindowFontScale(1.3f * scale);
        ImGui::SetCursorPosX(right);
        ImGui::TextColored(kWhite, "POINTS  %d", run.players[0].points);
        ImGui::SetCursorPosX(right);
        ImGui::TextColored(kWhite, "KILLS   %d", run.players[0].kills);
        ImGui::SetCursorPosX(right);
        ImGui::PushTextWrapPos(size.x - 60.0f * scale);
        ImGui::TextColored(kGrey, "Ending the run counts it as a game over. Your real save is put back either way.");
        ImGui::PopTextWrapPos();

        DrawFooter(nullptr, "A: SELECT    B / START: RESUME");
    }

    void DrawControls() {
        static const char* kLines[][2] = {
            { "B", "Sword. Sword kills pay the most." },
            { "C BUTTONS", "Items: slingshot or bow, bombs, the ocarina and what you buy." },
            { "A", "Roll, climb, pick up." },
            { "Z", "Target the nearest undead." },
            { "BUY BUTTON", "Buy at a torch, clear debris, hold at a window to repair it." },
            { "START", "Run menu: resume, items, controls, end run." },
            { "SUN'S SONG", "Turns the power on from anywhere." },
            { "ZELDA'S LULLABY", "Unseals Pack-a-Punch (power needed)." },
            { "SONG OF STORMS", "Slows the horde for 20 seconds." },
            { "GOLD TORCH", "Mystery Chest. Wooden torches: Armory, Fairy Fountain, power." },
            { "STONE TORCHES", "Perks and Pack-a-Punch. Dark until the power is on." },
        };
        ImGui::SetWindowFontScale(1.15f * scale);
        float y = 170.0f * scale;
        for (const auto& line : kLines) {
            ImGui::SetCursorPos(ImVec2(60.0f * scale, y));
            if (strcmp(line[0], "BUY BUTTON") == 0) {
                ImGui::TextColored(kGold, "BUY BUTTON (%s)", BuyButtonName());
            } else {
                ImGui::TextColored(kGold, "%s", line[0]);
            }
            ImGui::SetCursorPos(ImVec2(300.0f * scale, y));
            ImGui::TextColored(kWhite, "%s", line[1]);
            y += 34.0f * scale;
        }
        ImGui::SetCursorPos(ImVec2(60.0f * scale, y + 6.0f * scale));
        ImGui::SetWindowFontScale(1.6f * scale);
        ImGui::PushStyleColor(ImGuiCol_Text, kGold);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.55f, 0.05f, 0.05f, 0.55f));
        if (ImGui::Selectable("> BACK##zcontrols", true, 0, ImVec2(size.x * 0.3f, 0))) {
            if (TapAllowed()) {
                Activate(gPlayState, 0);
            }
        }
        ImGui::PopStyleColor(2);
        DrawFooter(nullptr, "A / B: BACK");
    }
};

static void RegisterLobbyWindow() {
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->AddGuiWindow(
        std::make_shared<LobbyWindow>(CVAR_ZOMBIES("LobbyWindow"), true, "Zombies of Time Lobby"));
}

static RegisterMenuInitFunc lobbyWindowInit(RegisterLobbyWindow);

} // namespace Zombies
