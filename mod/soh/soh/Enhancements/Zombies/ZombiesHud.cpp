// Zombies of Time: HUD, drawn with the game's debug font on a 40x30 character grid
// so it scales with the N64 frame at every resolution.

#include "ZombiesMode.h"

#include <cstring>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
void GfxPrint_SetColor(GfxPrint* printer, u32 r, u32 g, u32 b, u32 a);
void GfxPrint_SetPos(GfxPrint* printer, s32 x, s32 y);
s32 GfxPrint_Printf(GfxPrint* printer, const char* fmt, ...);
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

static void DrawContents(GfxPrint* printer) {
    Run& run = GetRun();
    bool inRun = run.phase == Phase::Active || run.phase == Phase::Intermission || run.phase == Phase::Intro;

    if (inRun) {
        // Round, bottom left above the rupee counter, blood red.
        GfxPrint_SetColor(printer, 200, 20, 20, 255);
        PrintAt(printer, 2, 24, RoundLabel(run.round));

        // Perks, as short labels under the round.
        static const char* perkTags[PERK_COUNT] = { "JUG", "REV", "DTP", "STM" };
        std::string perks;
        for (int i = 0; i < PERK_COUNT; i++) {
            if (run.players[0].perks[i]) {
                perks += perkTags[i];
                perks += " ";
            }
        }
        if (!perks.empty()) {
            GfxPrint_SetColor(printer, 120, 220, 120, 255);
            PrintAt(printer, 2, 22, perks);
        }

        // Points, bottom right, with recent changes stacked above.
        GfxPrint_SetColor(printer, 255, 255, 255, 255);
        PrintRight(printer, 38, 26, std::to_string(run.players[0].points));
        int row = 25;
        for (auto it = run.popups.rbegin(); it != run.popups.rend() && row >= 22; ++it, row--) {
            if (it->amount >= 0) {
                GfxPrint_SetColor(printer, 255, 220, 40, 255);
                PrintRight(printer, 38, row, "+" + std::to_string(it->amount));
            } else {
                GfxPrint_SetColor(printer, 230, 60, 60, 255);
                PrintRight(printer, 38, row, std::to_string(it->amount));
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
            PrintCentered(printer, 3, timers);
        }
    }

    if (run.announcement.framesLeft > 0) {
        GfxPrint_SetColor(printer, 255, 230, 120, 255);
        PrintCentered(printer, 8, run.announcement.text);
    }

    if (!run.prompt.empty()) {
        GfxPrint_SetColor(printer, 255, 255, 255, 255);
        PrintCentered(printer, 20, run.prompt);
    }
}

void Hud_Draw() {
    PlayState* play = gPlayState;
    if (play == nullptr || play->pauseCtx.state != 0) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx* polyOpa = POLY_OPA_DISP;
    Gfx* gfx = Graph_GfxPlusOne(polyOpa);
    gSPDisplayList(OVERLAY_DISP++, gfx);

    GfxPrint printer;
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);

    DrawContents(&printer);

    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);

    gSPEndDisplayList(gfx++);
    Graph_BranchDlist(polyOpa, gfx);
    POLY_OPA_DISP = gfx;

    CLOSE_DISPS(play->state.gfxCtx);
}

} // namespace Zombies
