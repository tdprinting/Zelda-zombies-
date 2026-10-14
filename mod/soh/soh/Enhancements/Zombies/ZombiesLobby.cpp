// Zombies of Time: zombies-only game and its main menu.
//
// With "Zombies Only" on, the whole game is the mode. After the file select, Link
// lands in a map at night and the main menu opens over it: a Black Ops style list
// down the left (play, map, Link's age, starting round, quit) and the map's details
// on the right, in Zelda's gold with a Triforce in the corner. Between runs, and
// after a game over, the menu comes back.
//
// The menu is driven with the controller (D-pad or stick, A to pick) and also by
// tapping or clicking, so it works with touch controls on Android.

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

#include "soh/ShipInit.hpp"
#include "soh/SohGui/MenuTypes.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace Zombies {

struct LobbyMap {
    const char* name;
    s16 sceneNum;
    s16 entrance;
    const char* intel;
};

static const LobbyMap kMaps[] = {
    { "KAKARIKO GRAVEYARD", SCENE_GRAVEYARD, ENTR_GRAVEYARD_ENTRANCE,
      "Graves, a gate and the Royal Tomb. Start at the gate, clear the debris into the graves, then fight your way "
      "to the tomb for the power and Pack-a-Punch." },
    { "MARKET RUINS", SCENE_MARKET_RUINS, ENTR_MARKET_RUINS_0_2,
      "Castle Town after Ganondorf. Tight streets and Redeads that have been waiting seven years." },
    { "LON LON RANCH", SCENE_LON_LON_RANCH, ENTR_LON_LON_RANCH_ENTRANCE,
      "Wide open paddock. Room to run a train of undead in circles." },
    { "HYRULE FIELD", SCENE_HYRULE_FIELD, ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN,
      "Huge and dark. Turn Max Undead Alive up in the settings for a real horde." },
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
};
static constexpr int kTipCount = sizeof(kTips) / sizeof(kTips[0]);

enum LobbyItem {
    ITEM_PLAY,
    ITEM_MAP,
    ITEM_AGE,
    ITEM_ROUND,
    ITEM_COOP,
    ITEM_QUIT,
    ITEM_COUNT,
};

static bool sOpen = false;
static int sSelected = ITEM_PLAY;
static int sStickCooldown = 0;
static bool sPendingStart = false; // warped to a map from the menu; start the run on arrival
static int sTipIndex = 0;

bool ZombiesOnly() {
    return CVarGetInteger(CVAR_ZOMBIES("ZombiesOnly"), 1) != 0;
}

static int MapIndex() {
    return std::clamp(CVarGetInteger(CVAR_ZOMBIES("LobbyMap"), 0), 0, kMapCount - 1);
}

static int ChosenAge() {
    return CVarGetInteger(CVAR_ZOMBIES("LobbyAge"), LINK_AGE_CHILD) == LINK_AGE_ADULT ? LINK_AGE_ADULT : LINK_AGE_CHILD;
}

static void ChangeMap(int delta) {
    CVarSetInteger(CVAR_ZOMBIES("LobbyMap"), (MapIndex() + delta + kMapCount) % kMapCount);
}

static void ChangeRound(int delta) {
    int round = std::clamp(CVarGetInteger(CVAR_ZOMBIES("StartingRound"), 1) + delta, 1, 50);
    CVarSetInteger(CVAR_ZOMBIES("StartingRound"), round);
}

static void ToggleAge() {
    CVarSetInteger(CVAR_ZOMBIES("LobbyAge"), ChosenAge() == LINK_AGE_CHILD ? LINK_AGE_ADULT : LINK_AGE_CHILD);
}

bool Lobby_IsOpen() {
    return sOpen;
}

static void Close(PlayState* play) {
    sOpen = false;
    if (play != nullptr) {
        GET_PLAYER(play)->stateFlags1 &= ~PLAYER_STATE1_INPUT_DISABLED;
    }
}

static void WarpToMap(PlayState* play, const LobbyMap& map) {
    gSaveContext.linkAge = ChosenAge();
    play->linkAgeOnLoad = ChosenAge();
    play->nextEntranceIndex = map.entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.dayTime = 0;
    gSaveContext.nightFlag = 1;
}

static void Play(PlayState* play) {
    const LobbyMap& map = kMaps[MapIndex()];
    Close(play);
    Sfx_PlaySfxCentered(NA_SE_SY_FSEL_DECIDE_L);
    if (play->sceneNum != map.sceneNum || gSaveContext.linkAge != ChosenAge()) {
        sPendingStart = true;
        WarpToMap(play, map);
        return;
    }
    Lobby_StartRunHere(play);
}

static void Quit() {
    sOpen = false;
    std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))
        ->Dispatch("reset");
}

static void Activate(PlayState* play, int item) {
    switch (item) {
        case ITEM_PLAY:
            Play(play);
            break;
        case ITEM_MAP:
            ChangeMap(1);
            Sfx_PlaySfxCentered(NA_SE_SY_FSEL_CURSOR);
            break;
        case ITEM_AGE:
            ToggleAge();
            Sfx_PlaySfxCentered(NA_SE_SY_FSEL_CURSOR);
            break;
        case ITEM_ROUND:
            ChangeRound(1);
            Sfx_PlaySfxCentered(NA_SE_SY_FSEL_CURSOR);
            break;
        case ITEM_QUIT:
            Quit();
            break;
        default:
            Sfx_PlaySfxCentered(NA_SE_SY_FSEL_ERROR);
            break;
    }
}

static void Adjust(int item, int delta) {
    switch (item) {
        case ITEM_MAP:
            ChangeMap(delta);
            break;
        case ITEM_AGE:
            ToggleAge();
            break;
        case ITEM_ROUND:
            ChangeRound(delta);
            break;
        default:
            return;
    }
    Sfx_PlaySfxCentered(NA_SE_SY_FSEL_CURSOR);
}

// Called from the mode's per-frame update while no run is going.
void Lobby_Update(PlayState* play, Player* player) {
    if (!ZombiesOnly()) {
        sOpen = false;
        return;
    }
    bool ready = play->transitionTrigger == TRANS_TRIGGER_OFF && play->transitionMode == TRANS_MODE_OFF &&
                 play->msgCtx.msgMode == MSGMODE_NONE && (player->actor.bgCheckFlags & 1) &&
                 play->gameOverCtx.state == GAMEOVER_INACTIVE;
    if (!ready) {
        return;
    }
    if (sPendingStart) {
        sPendingStart = false;
        Lobby_StartRunHere(play);
        return;
    }
    if (!sOpen) {
        sOpen = true;
        sSelected = ITEM_PLAY;
        sTipIndex = static_cast<int>(Rand_ZeroOne() * kTipCount) % kTipCount;
        Sfx_PlaySfxCentered(NA_SE_SY_FSEL_DECIDE_S);
    }

    // Link waits while the menu is up.
    player->stateFlags1 |= PLAYER_STATE1_INPUT_DISABLED;

    Input& input = play->state.input[0];
    u16 press = input.press.button;
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
    }
    if (vertical != 0) {
        sSelected = (sSelected + vertical + ITEM_COUNT) % ITEM_COUNT;
        Sfx_PlaySfxCentered(NA_SE_SY_FSEL_CURSOR);
    }
    if (horizontal != 0) {
        Adjust(sSelected, horizontal);
    }
    if (CHECK_BTN_ALL(press, BTN_A)) {
        Activate(play, sSelected);
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
    if (!isMap && gPlayState != nullptr) {
        sPendingStart = false;
        WarpToMap(gPlayState, kMaps[MapIndex()]);
    }
}

// After the file select: skip the story and go straight to the chosen map at night.
void Lobby_OnLoadGame() {
    if (!ZombiesOnly()) {
        return;
    }
    gSaveContext.entranceIndex = kMaps[MapIndex()].entrance;
    gSaveContext.linkAge = ChosenAge();
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.nextCutsceneIndex = 0xFFEF;
    gSaveContext.dayTime = 0;
    gSaveContext.nightFlag = 1;
    sPendingStart = false;
    sOpen = false;
}

// ---------------------------------------------------------------------------
// Drawing

class LobbyWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void Draw() override {
        if (!sOpen || gPlayState == nullptr) {
            return;
        }
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.03f, 0.72f));
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
        const ImVec2 size = ImGui::GetWindowSize();
        const ImVec2 origin = ImGui::GetWindowPos();
        const float scale = std::max(size.y / 720.0f, 0.6f);
        const ImVec4 gold(0.93f, 0.78f, 0.25f, 1.0f);
        const ImVec4 blood(0.78f, 0.06f, 0.06f, 1.0f);
        const ImVec4 grey(0.6f, 0.6f, 0.6f, 1.0f);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Triforce, top right.
        float tri = 46.0f * scale;
        ImVec2 top(origin.x + size.x - 90.0f * scale, origin.y + 40.0f * scale);
        ImU32 goldU32 = ImGui::GetColorU32(gold);
        auto triangle = [&](ImVec2 apex) {
            draw->AddTriangleFilled(apex, ImVec2(apex.x - tri * 0.5f, apex.y + tri * 0.866f),
                                    ImVec2(apex.x + tri * 0.5f, apex.y + tri * 0.866f), goldU32);
        };
        triangle(top);
        triangle(ImVec2(top.x - tri * 0.5f, top.y + tri * 0.866f));
        triangle(ImVec2(top.x + tri * 0.5f, top.y + tri * 0.866f));

        // Title, Black Ops style: big, red, top left.
        ImGui::SetCursorPos(ImVec2(60.0f * scale, 40.0f * scale));
        ImGui::SetWindowFontScale(3.2f * scale);
        ImGui::TextColored(blood, "ZOMBIES OF TIME");
        ImGui::SetWindowFontScale(1.3f * scale);
        ImGui::SetCursorPosX(64.0f * scale);
        ImGui::TextColored(gold, "SURVIVE THE NIGHT IN HYRULE");

        // The menu list, left.
        const LobbyMap& map = kMaps[MapIndex()];
        std::string labels[ITEM_COUNT] = {
            "PLAY",
            std::string("MAP      < ") + map.name + " >",
            std::string("LINK     < ") + (ChosenAge() == LINK_AGE_CHILD ? "CHILD" : "ADULT") + " >",
            "ROUND    < " + std::to_string(CVarGetInteger(CVAR_ZOMBIES("StartingRound"), 1)) + " >",
            "CO-OP    (COMING SOON)",
            "QUIT TO TITLE",
        };
        ImGui::SetWindowFontScale(1.9f * scale);
        float y = 190.0f * scale;
        for (int i = 0; i < ITEM_COUNT; i++) {
            ImGui::SetCursorPos(ImVec2(60.0f * scale, y));
            bool selected = i == sSelected;
            ImVec4 color = i == ITEM_COOP ? grey : (selected ? gold : ImVec4(1, 1, 1, 1));
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.55f, 0.05f, 0.05f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.55f, 0.05f, 0.05f, 0.35f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.7f, 0.08f, 0.08f, 0.6f));
            std::string text = (selected ? "> " : "  ") + labels[i] + "##lobby" + std::to_string(i);
            if (ImGui::Selectable(text.c_str(), selected, 0, ImVec2(size.x * 0.48f, 0))) {
                // Tapping a value row steps it; tapping play or quit does it.
                sSelected = i;
                Activate(gPlayState, i);
            }
            ImGui::PopStyleColor(4);
            y += 52.0f * scale;
        }

        // Map intel, right.
        float right = size.x * 0.58f;
        ImGui::SetWindowFontScale(1.8f * scale);
        ImGui::SetCursorPos(ImVec2(right, 190.0f * scale));
        ImGui::TextColored(gold, "%s", map.name);
        ImGui::SetWindowFontScale(1.15f * scale);
        ImGui::SetCursorPosX(right);
        ImGui::PushTextWrapPos(size.x - 60.0f * scale);
        ImGui::TextWrapped("%s", map.intel);
        ImGui::PopTextWrapPos();
        ImGui::SetCursorPosX(right);
        ImGui::TextColored(blood, "BEST ROUND: %d", CVarGetInteger(CVAR_ZOMBIES("BestRound"), 0));

        // Tip and controls, bottom.
        ImGui::SetWindowFontScale(1.1f * scale);
        ImGui::SetCursorPos(ImVec2(60.0f * scale, size.y - 90.0f * scale));
        ImGui::TextColored(gold, "TIP: %s", kTips[sTipIndex]);
        ImGui::SetCursorPosX(60.0f * scale);
        ImGui::TextColored(grey, "D-PAD / STICK: MOVE    LEFT / RIGHT: CHANGE    A: SELECT");
        ImGui::SetWindowFontScale(1.0f);
    }
};

static void RegisterLobbyWindow() {
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->AddGuiWindow(
        std::make_shared<LobbyWindow>(CVAR_ZOMBIES("LobbyWindow"), true, "Zombies of Time Lobby"));
}

static RegisterMenuInitFunc lobbyWindowInit(RegisterLobbyWindow);

} // namespace Zombies
