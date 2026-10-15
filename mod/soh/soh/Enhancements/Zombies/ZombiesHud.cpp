// Zombies of Time: HUD, drawn with the game's debug font on a 40x30 character grid
// so it scales with the N64 frame at every resolution.

#include "ZombiesMode.h"
#include "ZombiesGuns.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>

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

// ---------------------------------------------------------------------------
// The round counter, drawn like Black Ops' red chalk: tally marks up to five, then
// numerals. The chalk textures are generated here (our own art, no game assets), with a
// fixed seed so they look hand drawn but never flicker.

extern "C" Gfx* Gfx_TextureIA8(Gfx* displayListHead, void* texture, s16 textureWidth, s16 textureHeight,
                               s16 rectLeft, s16 rectTop, s16 rectWidth, s16 rectHeight, u16 dsdx, u16 dtdy);

static constexpr int kStrokeW = 8, kStrokeH = 32;
static constexpr int kSlashW = 32, kSlashH = 32;
static constexpr int kDigitW = 16, kDigitH = 24;
static u8 sStrokeTex[kStrokeH][kStrokeW];
static u8 sSlashTex[kSlashH][kSlashW];
static u8 sDigitTex[10][kDigitH][kDigitW];
static bool sChalkReady = false;

static u32 sChalkSeed = 0x2F6E2B1u;
static int ChalkRand(int n) {
    sChalkSeed = sChalkSeed * 1664525u + 1013904223u;
    return static_cast<int>((sChalkSeed >> 16) % static_cast<u32>(n));
}

// IA8: high nibble intensity, low nibble alpha. Chalk is full white with grainy alpha
// that thins towards the stroke's edges.
static u8 ChalkTexel(int coverage) {
    if (coverage <= 0) {
        return 0;
    }
    int alpha = std::clamp(coverage - ChalkRand(5), 0, 15);
    return static_cast<u8>(0xF0 | alpha);
}

// 5x7 numerals, one bit per column (bit 4 is the left column).
static const u8 kDigits5x7[10][7] = {
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
};

static void MakeChalk() {
    // A vertical stroke that wanders a little, wider in the middle.
    float centre = kStrokeW / 2.0f;
    for (int y = 0; y < kStrokeH; y++) {
        centre = std::clamp(centre + (ChalkRand(3) - 1) * 0.35f, 3.0f, kStrokeW - 3.0f);
        float half = (y < 2 || y > kStrokeH - 3) ? 1.2f : 2.2f;
        for (int x = 0; x < kStrokeW; x++) {
            float d = std::fabs(x + 0.5f - centre);
            sStrokeTex[y][x] = ChalkTexel(d < half ? 15 - static_cast<int>(d * 3) : 0);
        }
    }
    // The fifth mark, struck from bottom left to top right.
    for (int y = 0; y < kSlashH; y++) {
        for (int x = 0; x < kSlashW; x++) {
            float d = std::fabs((x + 0.5f) - (kSlashW - 1 - y + 0.5f)) * 0.7071f;
            sSlashTex[y][x] = ChalkTexel(d < 1.8f ? 15 - static_cast<int>(d * 4) : 0);
        }
    }
    // Numerals: each font pixel becomes a grainy 3x3 blob, rows nudged sideways.
    for (int digit = 0; digit < 10; digit++) {
        std::memset(sDigitTex[digit], 0, sizeof(sDigitTex[digit]));
        for (int row = 0; row < 7; row++) {
            int nudge = ChalkRand(2);
            for (int col = 0; col < 5; col++) {
                if (!(kDigits5x7[digit][row] & (0x10 >> col))) {
                    continue;
                }
                for (int py = 0; py < 3; py++) {
                    for (int px = 0; px < 3; px++) {
                        int x = 1 + col * 3 + px - 1 + nudge;
                        int y = 1 + row * 3 + py;
                        if (x >= 0 && x < kDigitW && y < kDigitH) {
                            sDigitTex[digit][y][x] = ChalkTexel(13);
                        }
                    }
                }
            }
        }
    }
    sChalkReady = true;
}

// Draws the round at (x, y), the top left of a counter `height` pixels tall, in N64
// screen space. `flash` (0-1) blends from white back to red, as Black Ops does when the
// round changes.
extern "C" void ZombiesHud_DrawRound(PlayState* play, int round, int x, int y, int height, float flash) {
    if (round <= 0) {
        return;
    }
    if (!sChalkReady) {
        MakeChalk();
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_39Overlay(play->state.gfxCtx);
    gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0,
                      PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    u8 r = static_cast<u8>(150 + 105 * flash);
    u8 g = static_cast<u8>(8 + 200 * flash);
    u8 b = static_cast<u8>(8 + 200 * flash);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, r, g, b, 235);
    gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 0);

    // Texture rectangles scale by the step per pixel, in 5.10 fixed point.
    auto step = [](int texSize, int drawSize) { return static_cast<u16>((texSize << 10) / std::max(drawSize, 1)); };
    if (round <= 5) {
        int strokeW = height / 4;
        int gap = height / 4;
        for (int i = 0; i < std::min(round, 4); i++) {
            int sx = x + i * gap;
            int sy = y + (i % 2); // not quite level, like chalk
            OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, sStrokeTex, kStrokeW, kStrokeH, sx, sy, strokeW, height,
                                          step(kStrokeW, strokeW), step(kStrokeH, height));
        }
        if (round == 5) {
            int w = gap * 4;
            OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, sSlashTex, kSlashW, kSlashH, x - gap / 2, y, w, height,
                                          step(kSlashW, w), step(kSlashH, height));
        }
    } else {
        int digitW = height * kDigitW / kDigitH;
        std::string text = std::to_string(round);
        for (size_t i = 0; i < text.size(); i++) {
            int digit = text[i] - '0';
            OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, sDigitTex[digit], kDigitW, kDigitH,
                                          x + static_cast<int>(i) * (digitW - 1), y, digitW, height,
                                          step(kDigitW, digitW), step(kDigitH, height));
        }
    }
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

// The gun in hand, bottom right under the points as in Black Ops: name, then clip | reserve.
static void DrawGun(GfxPrint* printer, bool split) {
    std::string name, ammo, hint;
    bool crosshair = false;
    if (!Guns_HudInfo(&name, &ammo, &hint, &crosshair)) {
        return;
    }
    if (split) {
        GfxPrint_SetColor(printer, 230, 230, 230, 255);
        PrintRight(printer, 38, 14, name + "  " + ammo);
    } else {
        GfxPrint_SetColor(printer, 200, 200, 200, 255);
        PrintRight(printer, 38, 27, name);
        GfxPrint_SetColor(printer, 255, 255, 255, 255);
        PrintRight(printer, 38, 28, ammo);
    }
    if (!hint.empty()) {
        GfxPrint_SetColor(printer, 255, 230, 120, 255);
        PrintCentered(printer, split ? 11 : 24, hint);
    }
    if (crosshair) {
        GfxPrint_SetColor(printer, 255, 255, 255, 200);
        PrintAt(printer, 19, split ? 7 : 14, "+");
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

        DrawPerks(printer, run.players[0], split ? 12 : 22);
        DrawPoints(printer, 0, split ? 13 : 26, split ? 10 : 22);
        DrawGun(printer, split);

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
            PrintCentered(printer, split ? 15 : 21, timers); // bottom centre, as Black Ops shows them
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

// The round counter flashes white for a moment when the round changes.
static int sShownRound = 0;
static int sFlashFrames = 0;
static constexpr int kFlashFrames = 50;

void Hud_Draw() {
    PlayState* play = gPlayState;
    if (play == nullptr || play->pauseCtx.state != 0) {
        return;
    }
    ZombiesHud_DrawPrinter(play, DrawContents);

    const Run& run = GetRun();
    bool inRun = run.phase == Phase::Active || run.phase == Phase::Intermission || run.phase == Phase::Intro;
    if (!inRun) {
        sShownRound = 0;
        return;
    }
    if (run.round != sShownRound) {
        sFlashFrames = sShownRound == 0 ? 0 : kFlashFrames;
        sShownRound = run.round;
    }
    if (sFlashFrames > 0) {
        sFlashFrames--;
    }
    // Bottom left, between the perks and the rupees. Split, it sits on the line between
    // the halves, shared by both players.
    bool split = Coop_SplitView();
    ZombiesHud_DrawRound(play, run.round, 16, split ? 106 : 185, split ? 16 : 22,
                         static_cast<float>(sFlashFrames) / kFlashFrames);
}

} // namespace Zombies
