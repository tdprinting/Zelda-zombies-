// Zombies of Time: HUD, drawn with the game's debug font on a 40x30 character grid
// so it scales with the N64 frame at every resolution.

#include "ZombiesMode.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
void GfxPrint_SetColor(GfxPrint* printer, u32 r, u32 g, u32 b, u32 a);
void GfxPrint_SetPos(GfxPrint* printer, s32 x, s32 y);
s32 GfxPrint_Printf(GfxPrint* printer, const char* fmt, ...);
}

// OPEN_DISPS forward-declares C functions, so the display list work needs C linkage
// and has to sit outside the Zombies namespace.
extern "C" void ZombiesHud_DrawPrinter(PlayState* play, void (*drawContents)(GfxPrint*)) {
    OPEN_DISPS(play->state.gfxCtx);

    Gfx* polyOpa = POLY_OPA_DISP;
    Gfx* gfx = Graph_GfxPlusOne(polyOpa);
    gSPDisplayList(OVERLAY_DISP++, gfx);

    GfxPrint printer;
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);

    drawContents(&printer);

    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    gSPEndDisplayList(gfx++);
    Graph_BranchDlist(polyOpa, gfx);
    POLY_OPA_DISP = gfx;

    CLOSE_DISPS(play->state.gfxCtx);
}

namespace Zombies {

static constexpr int kCols = 40;

static void PrintAt(GfxPrint* printer, int col, int row, const std::string& text) {
    GfxPrint_SetPos(printer, col, row);
    GfxPrint_Printf(printer, "%s", text.c_str());
}

static void PrintCentered(GfxPrint* printer, int row, const std::string& text) {
    int col = (kCols - static_cast<int>(text.size())) / 2;
    PrintAt(printer, col < 0 ? 0 : col, row, text);
}

static void PrintRight(GfxPrint* printer, int rightCol, int row, const std::string& text) {
    PrintAt(printer, rightCol - static_cast<int>(text.size()), row, text);
}

// Black Ops draws rounds 1-5 as chalk tally marks.
static std::string RoundLabel(int round) {
    if (round <= 0) {
        return "";
    }
    if (round <= 4) {
        return std::string(round, 'I');
    }
    if (round == 5) {
        return "IIII/";
    }
    return std::to_string(round);
}

static void DrawPerks(GfxPrint* printer, const PlayerState& ps, int row) {
    static const char* perkTags[PERK_COUNT] = { "JUG", "REV", "DTP", "STM" };
    std::string perks;
    for (int i = 0; i < PERK_COUNT; i++) {
        if (ps.perks[i]) {
            perks += perkTags[i];
            perks += " ";
        }
    }
    if (!perks.empty()) {
        GfxPrint_SetColor(printer, 120, 220, 120, 255);
        PrintAt(printer, 2, row, perks);
    }
}

// Points at the right edge, with recent changes stacked above.
static void DrawPoints(GfxPrint* printer, int player, int row, int lowestPopupRow) {
    const Run& run = GetRun();
    GfxPrint_SetColor(printer, 255, 255, 255, 255);
    PrintRight(printer, 38, row, std::to_string(run.players[player].points));
    int popupRow = row - 1;
    for (auto it = run.popups.rbegin(); it != run.popups.rend() && popupRow >= lowestPopupRow; ++it) {
        if (it->player != player) {
            continue;
        }
        if (it->amount >= 0) {
            GfxPrint_SetColor(printer, 255, 220, 40, 255);
            PrintRight(printer, 38, popupRow, "+" + std::to_string(it->amount));
        } else {
            GfxPrint_SetColor(printer, 230, 60, 60, 255);
            PrintRight(printer, 38, popupRow, std::to_string(it->amount));
        }
        popupRow--;
    }
}

static std::string Bar(int value, int max, int per) {
    std::string bar;
    for (int i = 0; i < max / per; i++) {
        bar += value > i * per ? '#' : '-';
    }
    return bar;
}

// Player 2's half: health, sword and bombs at the bottom left, points at the bottom right.
static void DrawPartner(GfxPrint* printer) {
    const Run& run = GetRun();
    const Partner& partner = run.partner;
    static const char* swords[] = { "KOKIRI", "MASTER", "BIGGORON" };

    GfxPrint_SetColor(printer, 230, 70, 60, 255);
    PrintAt(printer, 2, 16, "P2 " + Bar(partner.health, partner.healthCapacity, 16));
    GfxPrint_SetColor(printer, 200, 200, 200, 255);
    std::string gear = std::string(swords[std::clamp(partner.sword, 0, 2)]) + (partner.papSword ? "+" : "");
    if (partner.bombBag) {
        gear += "  BOMBS " + std::to_string(partner.bombs);
    }
    PrintAt(printer, 2, 17, gear);
    DrawPerks(printer, run.players[1], 26);
    DrawPoints(printer, 1, 27, 24);

    std::string status;
    if (partner.bledOut) {
        status = "BLED OUT: BACK NEXT ROUND";
    } else if (partner.downed) {
        status = "DOWN! " + std::to_string(partner.downFrames / 20 + 1);
    }
    if (!status.empty()) {
        GfxPrint_SetColor(printer, 230, 60, 60, 255);
        PrintCentered(printer, 21, status);
    } else if (!partner.prompt.empty()) {
        GfxPrint_SetColor(printer, 255, 255, 255, 255);
        PrintCentered(printer, 25, partner.prompt);
    }
}

static void DrawContents(GfxPrint* printer) {
    Run& run = GetRun();
    bool inRun = run.phase == Phase::Active || run.phase == Phase::Intermission || run.phase == Phase::Intro;
    // Split screen: player 1 in the top half (rows 0-14), player 2 in the bottom.
    bool split = Coop_SplitView();

    if (inRun) {
        // Round, bottom left above the rupee counter, blood red. Split, it sits on the line
        // between the halves, shared by both.
        GfxPrint_SetColor(printer, 200, 20, 20, 255);
        PrintAt(printer, 2, split ? 14 : 24, RoundLabel(run.round));

        DrawPerks(printer, run.players[0], split ? 12 : 22);
        DrawPoints(printer, 0, split ? 13 : 26, split ? 10 : 22);

        if (split) {
            DrawPartner(printer);
            const LinkDown& down = run.linkDown;
            if (down.bledOut || down.downed) {
                GfxPrint_SetColor(printer, 230, 60, 60, 255);
                PrintCentered(printer, 6,
                              down.bledOut ? "BLED OUT: BACK NEXT ROUND"
                                           : "DOWN! " + std::to_string(down.downFrames / 20 + 1));
            }
        }

        // Active power-ups with seconds left.
        std::string timers;
        for (int i = 0; i < static_cast<int>(PowerUp::Count); i++) {
            int frames = run.powerUpFrames[i];
            if (frames > 0) {
                if (!timers.empty()) {
                    timers += "  ";
                }
                timers += std::string(PowerUpName(static_cast<PowerUp>(i))) + " " + std::to_string(frames / 20 + 1);
            }
        }
        if (run.stormFrames > 0) {
            if (!timers.empty()) {
                timers += "  ";
            }
            timers += "STORM " + std::to_string(run.stormFrames / 20 + 1);
        }
        if (!timers.empty()) {
            GfxPrint_SetColor(printer, 120, 200, 255, 255);
            PrintCentered(printer, split ? 15 : 3, timers);
        }
    }

    // Layout helper: Link's position, for placing arena spots.
    if (CVarGetInteger(CVAR_ZOMBIES("ShowCoordinates"), 0) && gPlayState != nullptr) {
        Player* player = GET_PLAYER(gPlayState);
        char buf[48];
        snprintf(buf, sizeof(buf), "X %d  Y %d  Z %d", static_cast<int>(player->actor.world.pos.x),
                 static_cast<int>(player->actor.world.pos.y), static_cast<int>(player->actor.world.pos.z));
        GfxPrint_SetColor(printer, 200, 200, 200, 255);
        PrintAt(printer, 2, 1, buf);
    }

    if (run.announcement.framesLeft > 0) {
        GfxPrint_SetColor(printer, 255, 230, 120, 255);
        PrintCentered(printer, 8, run.announcement.text);
        if (split) {
            PrintCentered(printer, 19, run.announcement.text);
        }
    }

    if (!run.prompt.empty()) {
        GfxPrint_SetColor(printer, 255, 255, 255, 255);
        PrintCentered(printer, split ? 10 : 20, run.prompt);
    }
}

void Hud_Draw() {
    PlayState* play = gPlayState;
    if (play == nullptr || play->pauseCtx.state != 0) {
        return;
    }
    ZombiesHud_DrawPrinter(play, DrawContents);
}

} // namespace Zombies
