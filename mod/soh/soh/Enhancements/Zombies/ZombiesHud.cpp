// Zombies of Time: the HUD. Black Ops' zombies layout, drawn with Ocarina of Time's look.
//
//   bottom left    perk bottles in a row, the round in red chalk under them
//   bottom right   points on a red banner, the gun's name and "clip / reserve", and a dial
//                  holding the B and C buttons (OoT's item icons and ammo digits)
//   bottom centre  the timed power-ups
//   centre         crosshair while aiming, reload hint, station prompts
//   screen edges   Black Ops' red damage vignette in place of the hearts
//
// OoT's own button cluster, hearts, magic, rupees and minimap are faded out for the run
// (the buttons still work: only their drawing moves here). Text and digits use OoT's
// font and counter digits, icons OoT's item icons, all read from the player's oot.o2r.
//
// Black Ops HUD images are used when the player has converted them from their own
// install with `bomap hud` (tools/bomap): PNGs in zombies/hud/, named after the Black Ops
// material (hud_chalk_1, scorebar_zom_1, specialty_juggernaut_zombies, overlay_low_health,
// ...). Each one that's missing is drawn by the code below instead, so the HUD works
// without them. Nothing from either game is in this file.
//
// Split screen: each player gets the same HUD in their half, scaled down.

#include "ZombiesMode.h"
#include "ZombiesGuns.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <stb_image.h>

#include "soh/ShipUtils.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "textures/parameter_static/parameter_static.h"
extern PlayState* gPlayState;
void GfxPrint_SetColor(GfxPrint* printer, u32 r, u32 g, u32 b, u32 a);
void GfxPrint_SetPos(GfxPrint* printer, s32 x, s32 y);
s32 GfxPrint_Printf(GfxPrint* printer, const char* fmt, ...);
float OTRGetDimensionFromLeftEdge(float v);
float OTRGetDimensionFromRightEdge(float v);
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

// Hands the overlay display list, set up for HUD texture rectangles, to `draw`.
extern "C" void ZombiesHud_DrawOverlay(PlayState* play, void (*draw)(Gfx**)) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_39Overlay(play->state.gfxCtx);
    Gfx* gfx = OVERLAY_DISP;
    draw(&gfx);
    OVERLAY_DISP = gfx;
    CLOSE_DISPS(play->state.gfxCtx);
}

namespace Zombies {

namespace {

// ---------------------------------------------------------------------------
// Drawing primitives. Everything is a texture rectangle tinted by the primitive colour,
// in N64 screen units (320x240), with x free to reach past the 4:3 frame in widescreen.

struct Color {
    u8 r, g, b, a;
};

Color WithAlpha(Color c, float alpha) {
    c.a = static_cast<u8>(std::clamp(static_cast<float>(c.a) * alpha, 0.0f, 255.0f));
    return c;
}

enum class Fmt { Rgba32, Ia8, I8, I4 };

struct Tex {
    int w = 0;
    int h = 0;
    std::vector<u8> px; // RGBA8
    bool Ok() const {
        return w > 0 && h > 0;
    }
};

void Load(Gfx*& g, const void* tex, Fmt fmt, int w, int h) {
    switch (fmt) {
        case Fmt::Rgba32:
            gDPLoadTextureBlock(g++, tex, G_IM_FMT_RGBA, G_IM_SIZ_32b, w, h, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
        case Fmt::Ia8:
            gDPLoadTextureBlock(g++, tex, G_IM_FMT_IA, G_IM_SIZ_8b, w, h, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
        case Fmt::I8:
            gDPLoadTextureBlock(g++, tex, G_IM_FMT_I, G_IM_SIZ_8b, w, h, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
        case Fmt::I4:
            gDPLoadTextureBlock_4b(g++, tex, G_IM_FMT_I, w, h, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                   G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
    }
}

// The texture loaded last, stretched over (x, y, w, h).
void Quad(Gfx*& g, float x, float y, float w, float h, int tw, int th, Color c) {
    if (w < 0.5f || h < 0.5f || c.a == 0) {
        return;
    }
    gDPPipeSync(g++);
    gDPSetPrimColor(g++, 0, 0, c.r, c.g, c.b, c.a);
    int x0 = static_cast<int>(std::lround(x * 4.0f));
    int y0 = static_cast<int>(std::lround(y * 4.0f));
    int x1 = static_cast<int>(std::lround((x + w) * 4.0f));
    int y1 = static_cast<int>(std::lround((y + h) * 4.0f));
    int dsdx = std::clamp(static_cast<int>(static_cast<float>(tw) * 1024.0f / w), 1, 0x7FFF);
    int dtdy = std::clamp(static_cast<int>(static_cast<float>(th) * 1024.0f / h), 1, 0x7FFF);
    gSPWideTextureRectangle(g++, x0, y0, x1, y1, G_TX_RENDERTILE, 0, 0, dsdx, dtdy);
}

void Image(Gfx*& g, const Tex& t, float x, float y, float w, float h, Color c) {
    Load(g, t.px.data(), Fmt::Rgba32, t.w, t.h);
    Quad(g, x, y, w, h, t.w, t.h, c);
}

// ---------------------------------------------------------------------------
// Textures drawn by code: our own simple shapes, made once.

Tex MakeTex(int w, int h, float (*alphaAt)(float u, float v, int x, int y), u8 intensity = 255) {
    Tex t;
    t.w = w;
    t.h = h;
    t.px.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w) * 2.0f - 1.0f;
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h) * 2.0f - 1.0f;
            float a = std::clamp(alphaAt(u, v, x, y), 0.0f, 1.0f);
            u8* p = &t.px[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = p[1] = p[2] = intensity;
            p[3] = static_cast<u8>(a * 255.0f);
        }
    }
    return t;
}

u32 sNoise = 0x9E3779B9u;
float Noise() {
    sNoise = sNoise * 1664525u + 1013904223u;
    return static_cast<float>((sNoise >> 8) & 0xFFFF) / 65535.0f;
}

struct Shapes {
    Tex white, disc, ring, vignette, banner;
    bool ready = false;
};
Shapes sShapes;

void MakeShapes() {
    sShapes.white = MakeTex(4, 4, [](float, float, int, int) { return 1.0f; });
    // A filled circle, edge smoothed over one texel.
    sShapes.disc = MakeTex(32, 32, [](float u, float v, int, int) {
        float d = std::sqrt(u * u + v * v) * 16.0f;
        return 15.5f - d;
    });
    // The dial's rim: a ring with a soft inner edge.
    sShapes.ring = MakeTex(64, 64, [](float u, float v, int, int) {
        float d = std::sqrt(u * u + v * v) * 32.0f;
        float outer = 31.0f - d;
        float inner = (d - 26.5f) * 0.6f;
        return std::min(outer, inner);
    });
    // Black Ops' hurt overlay: clear in the middle, thick at the edges and corners.
    sShapes.vignette = MakeTex(64, 64, [](float u, float v, int, int) {
        float d = std::pow(std::pow(std::fabs(u), 3.0f) + std::pow(std::fabs(v), 3.0f), 1.0f / 3.0f);
        float t = std::clamp((d - 0.62f) / 0.4f, 0.0f, 1.0f);
        return t * t * (0.85f + 0.15f * Noise());
    });
    // The points banner: a brush stroke with ragged ends and streaks along it.
    sShapes.banner = MakeTex(64, 16, [](float u, float v, int x, int y) {
        (void)x;
        static float rowStart[16], rowEnd[16], rowShade[16];
        static bool rows = false;
        if (!rows) {
            for (int i = 0; i < 16; i++) {
                rowStart[i] = -0.97f + Noise() * 0.1f;
                rowEnd[i] = 0.9f + Noise() * 0.1f;
                rowShade[i] = 0.75f + Noise() * 0.25f;
            }
            rows = true;
        }
        float edge = std::min(u - rowStart[y], rowEnd[y] - u) * 30.0f;
        float vert = (1.0f - std::fabs(v)) * 8.0f;
        return std::min({ edge, vert, 1.0f }) * rowShade[y] * (0.9f + 0.1f * Noise());
    });
    sShapes.ready = true;
}

// ---------------------------------------------------------------------------
// Black Ops HUD images, converted from the player's own install by `bomap hud` into
// zombies/hud/<material>.png. Read once; a missing one is looked for again each run.

enum BoImage {
    BO_CHALK_1,
    BO_CHALK_2,
    BO_CHALK_3,
    BO_CHALK_4,
    BO_CHALK_5,
    BO_SCORE_P1,
    BO_SCORE_P2,
    BO_PERK_JUGGERNOG,
    BO_PERK_REVIVE,
    BO_PERK_DOUBLE_TAP,
    BO_PERK_STAMIN_UP,
    BO_POWER_INSTAKILL,
    BO_POWER_DOUBLE_POINTS,
    BO_POWER_FIRE_SALE,
    BO_LOW_HEALTH,
    BO_DPAD,
    BO_IMAGE_COUNT,
};

const char* const kBoImageNames[BO_IMAGE_COUNT] = {
    "hud_chalk_1",
    "hud_chalk_2",
    "hud_chalk_3",
    "hud_chalk_4",
    "hud_chalk_5",
    "scorebar_zom_1",
    "scorebar_zom_2",
    "specialty_juggernaut_zombies",
    "specialty_quickrevive_zombies",
    "specialty_doubletap_zombies",
    "specialty_longersprint_zombies",
    "specialty_instakill_zombies",
    "specialty_doublepoints_zombies",
    "specialty_firesale_zombies",
    "overlay_low_health",
    "hud_dpad",
};

Tex sBoImages[BO_IMAGE_COUNT];

// The renderer loads a texture in one block: keep it to 4096 texels by halving.
void Shrink(Tex& t) {
    while (t.w * t.h > 4096 && t.w > 1 && t.h > 1) {
        int w = t.w / 2;
        int h = t.h / 2;
        std::vector<u8> px(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                for (int c = 0; c < 4; c++) {
                    int sum = 0;
                    for (int k = 0; k < 4; k++) {
                        int sx = x * 2 + (k & 1);
                        int sy = y * 2 + (k >> 1);
                        sum += t.px[(static_cast<size_t>(sy) * t.w + sx) * 4 + c];
                    }
                    px[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<u8>(sum / 4);
                }
            }
        }
        t.w = w;
        t.h = h;
        t.px.swap(px);
    }
}

void LoadBoImages() {
    std::string dir = Ship::Context::GetPathRelativeToAppDirectory("zombies/hud");
    for (int i = 0; i < BO_IMAGE_COUNT; i++) {
        if (sBoImages[i].Ok()) {
            continue; // already on the GPU under this address: never replaced
        }
        std::string path = dir + "/" + kBoImageNames[i] + ".png";
        int w = 0, h = 0, n = 0;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (data == nullptr) {
            continue;
        }
        if (w > 0 && h > 0 && w <= 2048 && h <= 2048) {
            Tex t;
            t.w = w;
            t.h = h;
            t.px.assign(data, data + static_cast<size_t>(w) * h * 4);
            Shrink(t);
            sBoImages[i] = std::move(t);
        }
        stbi_image_free(data);
    }
}

const Tex* Bo(int image) {
    return sBoImages[image].Ok() ? &sBoImages[image] : nullptr;
}

// ---------------------------------------------------------------------------
// OoT's text: the message font (16x16 I4 glyphs) and the counter digits (8x16 I8).

const char* const kCounterDigits[10] = {
    gCounterDigit0Tex, gCounterDigit1Tex, gCounterDigit2Tex, gCounterDigit3Tex, gCounterDigit4Tex,
    gCounterDigit5Tex, gCounterDigit6Tex, gCounterDigit7Tex, gCounterDigit8Tex, gCounterDigit9Tex,
};
const char* const kAmmoDigits[10] = {
    gAmmoDigit0Tex, gAmmoDigit1Tex, gAmmoDigit2Tex, gAmmoDigit3Tex, gAmmoDigit4Tex,
    gAmmoDigit5Tex, gAmmoDigit6Tex, gAmmoDigit7Tex, gAmmoDigit8Tex, gAmmoDigit9Tex,
};

float GlyphAdvance(char c, float size) {
    return Ship_GetCharFontWidth(static_cast<u8>(c)) * size / 16.0f;
}

float TextWidth(const std::string& text, float size) {
    float w = 0.0f;
    for (char c : text) {
        w += GlyphAdvance(c, size);
    }
    return w;
}

void Glyph(Gfx*& g, char c, float x, float y, float size, Color color, bool shadow) {
    if (c == ' ') {
        return;
    }
    Load(g, Ship_GetCharFontTexture(static_cast<u8>(c)), Fmt::I4, 16, 16);
    if (shadow) {
        Quad(g, x + 1.0f, y + 1.0f, size, size, 16, 16, Color{ 0, 0, 0, static_cast<u8>(color.a * 3 / 4) });
    }
    Quad(g, x, y, size, size, 16, 16, color);
}

// `size` is the glyph cell's height in pixels; letters fill roughly its top two thirds.
void Text(Gfx*& g, const std::string& text, float x, float y, float size, Color color, bool shadow = true) {
    for (char c : text) {
        Glyph(g, c, x, y, size, color, shadow);
        x += GlyphAdvance(c, size);
    }
}

enum class Align { Left, Centre, Right };

void TextAt(Gfx*& g, const std::string& text, float x, float y, float size, Color color, Align align,
            bool shadow = true) {
    float w = TextWidth(text, size);
    float left = align == Align::Left ? x : align == Align::Centre ? x - w / 2.0f : x - w;
    Text(g, text, left, y, size, color, shadow);
}

// Numbers in OoT's rupee-counter digits; other characters ('/', '+', '-') in the font.
float NumberWidth(const std::string& text, float h) {
    float w = 0.0f;
    for (char c : text) {
        w += (c >= '0' && c <= '9') ? h * 0.5f : GlyphAdvance(c, h * 1.1f);
    }
    return w;
}

void Number(Gfx*& g, const std::string& text, float x, float y, float h, Color color, bool shadow = true) {
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            Load(g, kCounterDigits[c - '0'], Fmt::I8, 8, 16);
            if (shadow) {
                Quad(g, x + 1.0f, y + 1.0f, h * 0.5f, h, 8, 16, Color{ 0, 0, 0, static_cast<u8>(color.a * 3 / 4) });
            }
            Quad(g, x, y, h * 0.5f, h, 8, 16, color);
            x += h * 0.5f;
        } else {
            float size = h * 1.1f;
            Glyph(g, c, x, y - h * 0.08f, size, color, shadow);
            x += GlyphAdvance(c, size);
        }
    }
}

// The small ammo digits OoT draws on its item buttons.
void AmmoDigits(Gfx*& g, int value, float x, float y, float size, Color color) {
    std::string text = std::to_string(std::clamp(value, 0, 99));
    for (char c : text) {
        Load(g, kAmmoDigits[c - '0'], Fmt::Ia8, 8, 8);
        Quad(g, x, y, size, size, 8, 8, color);
        x += size * 0.75f;
    }
}

bool ItemIcon(Gfx*& g, int item, float x, float y, float size, u8 alpha) {
    if (item < 0 || item >= 158) {
        return false;
    }
    const char* tex = static_cast<const char*>(gItemIcons[item]);
    if (tex == nullptr || tex[0] == '\0') {
        return false;
    }
    Load(g, tex, Fmt::Rgba32, 32, 32);
    Quad(g, x, y, size, size, 32, 32, Color{ 255, 255, 255, alpha });
    return true;
}

// ---------------------------------------------------------------------------
// The round counter in Black Ops' red chalk: tally marks up to ten (a struck group of
// five, then the rest), numerals after. The fallback chalk is generated here with a fixed
// seed, so it looks hand drawn but never flickers.

constexpr int kStrokeW = 8, kStrokeH = 32;
constexpr int kSlashW = 32, kSlashH = 32;
constexpr int kDigitW = 16, kDigitH = 24;
u8 sStrokeTex[kStrokeH][kStrokeW];
u8 sSlashTex[kSlashH][kSlashW];
u8 sDigitTex[10][kDigitH][kDigitW];
bool sChalkReady = false;

u32 sChalkSeed = 0x2F6E2B1u;
int ChalkRand(int n) {
    sChalkSeed = sChalkSeed * 1664525u + 1013904223u;
    return static_cast<int>((sChalkSeed >> 16) % static_cast<u32>(n));
}

// IA8: high nibble intensity, low nibble alpha. Chalk is full white with grainy alpha
// that thins towards the stroke's edges.
u8 ChalkTexel(int coverage) {
    if (coverage <= 0) {
        return 0;
    }
    int alpha = std::clamp(coverage - ChalkRand(5), 0, 15);
    return static_cast<u8>(0xF0 | alpha);
}

// 5x7 numerals, one bit per column (bit 4 is the left column).
const u8 kDigits5x7[10][7] = {
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
};

void MakeChalk() {
    // A vertical stroke that wanders a little, wider in the middle.
    float centre = kStrokeW / 2.0f;
    for (int y = 0; y < kStrokeH; y++) {
        centre = std::clamp(centre + static_cast<float>(ChalkRand(3) - 1) * 0.35f, 3.0f, kStrokeW - 3.0f);
        float half = (y < 2 || y > kStrokeH - 3) ? 1.2f : 2.2f;
        for (int x = 0; x < kStrokeW; x++) {
            float d = std::fabs(static_cast<float>(x) + 0.5f - centre);
            sStrokeTex[y][x] = ChalkTexel(d < half ? 15 - static_cast<int>(d * 3) : 0);
        }
    }
    // The fifth mark, struck from bottom left to top right.
    for (int y = 0; y < kSlashH; y++) {
        for (int x = 0; x < kSlashW; x++) {
            float d = std::fabs(static_cast<float>(x - (kSlashW - 1 - y))) * 0.7071f;
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
                        int x = col * 3 + px + nudge;
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

// One group of 1-5 tally marks with its top left at (x, y). Returns its width.
float TallyGroup(Gfx*& g, int marks, float x, float y, float h, Color c) {
    if (const Tex* bo = Bo(BO_CHALK_1 + marks - 1)) {
        Image(g, *bo, x, y, h, h, c); // Black Ops: each image holds that many marks
        return h * 0.9f;
    }
    float strokeW = h / 4.0f;
    float gap = h / 4.0f;
    Load(g, sStrokeTex, Fmt::Ia8, kStrokeW, kStrokeH);
    for (int i = 0; i < std::min(marks, 4); i++) {
        Quad(g, x + static_cast<float>(i) * gap, y + static_cast<float>(i % 2), strokeW, h, kStrokeW, kStrokeH, c);
    }
    if (marks == 5) {
        Load(g, sSlashTex, Fmt::Ia8, kSlashW, kSlashH);
        Quad(g, x - gap / 2.0f, y, gap * 4.0f, h, kSlashW, kSlashH, c);
    }
    return gap * 5.5f;
}

// The round, bottom left at (x, y + h). `flash` (0-1) blends from white back to red, as
// Black Ops does when the round changes.
void Round(Gfx*& g, int round, float x, float y, float h, float flash) {
    if (round <= 0) {
        return;
    }
    Color c = { static_cast<u8>(150.0f + 105.0f * flash), static_cast<u8>(8.0f + 200.0f * flash),
                static_cast<u8>(8.0f + 200.0f * flash), 235 };
    if (round <= 10) {
        float w = TallyGroup(g, std::min(round, 5), x, y, h, c);
        if (round > 5) {
            TallyGroup(g, round - 5, x + w, y, h, c);
        }
        return;
    }
    float digitW = h * static_cast<float>(kDigitW) / static_cast<float>(kDigitH);
    std::string text = std::to_string(round);
    for (size_t i = 0; i < text.size(); i++) {
        Load(g, sDigitTex[text[i] - '0'], Fmt::Ia8, kDigitW, kDigitH);
        Quad(g, x + static_cast<float>(i) * (digitW - 1.0f), y, digitW, h, kDigitW, kDigitH, c);
    }
}

// ---------------------------------------------------------------------------
// One player's HUD inside a region of the screen (all of it, or a split-screen half).

struct Region {
    float top;
    float bottom;
    float scale;

    // Distances from the screen's edges, scaled; widescreen pushes them out.
    float Left(float d) const {
        return OTRGetDimensionFromLeftEdge(d * scale);
    }
    float Right(float d) const {
        return OTRGetDimensionFromRightEdge(static_cast<float>(SCREEN_WIDTH) - d * scale);
    }
    float Up(float d) const {
        return bottom - d * scale;
    }
    float Down(float d) const {
        return top + d * scale;
    }
    float MidY() const {
        return (top + bottom) / 2.0f;
    }
    float S(float v) const {
        return v * scale;
    }
};

constexpr float kCentreX = SCREEN_WIDTH / 2.0f;

struct Button {
    int item = ITEM_NONE;
    bool enabled = true;
    int ammo = -1; // -1: no count
    const char* emptyArrow = nullptr; // OoT's empty-button arrow when there's no item
};

struct PlayerView {
    int index = 0;
    Button b;
    Button c[3]; // left, down, right
    float healthFrac = 1.0f;
    bool down = false;
    std::string status;  // DOWN / BLED OUT
    std::string prompt;
    bool gun = false;
    std::string gunName, gunAmmo, gunHint;
    bool crosshair = false;
    float magic = -1.0f; // 0-1, or -1 with no magic meter
};

int ItemAmmo(int item) {
    if (item >= ITEM_BOW_ARROW_FIRE && item <= ITEM_BOW_ARROW_LIGHT) {
        item = ITEM_BOW;
    }
    switch (item) {
        case ITEM_STICK:
        case ITEM_NUT:
        case ITEM_BOMB:
        case ITEM_BOW:
        case ITEM_SLINGSHOT:
        case ITEM_BOMBCHU:
        case ITEM_BEAN:
            return AMMO(item);
        default:
            return -1;
    }
}

// The perk bottles, left to right in the order they were bought would need the run to
// remember it; Black Ops' fixed order reads the same at a glance.
void Perks(Gfx*& g, const Region& r, const PlayerState& ps) {
    static const int kBottles[PERK_COUNT] = { ITEM_POTION_RED, ITEM_FAIRY, ITEM_POTION_BLUE, ITEM_POTION_GREEN };
    static const Color kRims[PERK_COUNT] = {
        { 210, 40, 30, 255 }, { 80, 170, 255, 255 }, { 240, 170, 40, 255 }, { 230, 220, 70, 255 }
    };
    float size = r.S(19.0f);
    float x = r.Left(12.0f);
    float y = r.Up(68.0f);
    for (int i = 0; i < PERK_COUNT; i++) {
        if (!ps.perks[i]) {
            continue;
        }
        if (const Tex* bo = Bo(BO_PERK_JUGGERNOG + i)) {
            Image(g, *bo, x, y, size, size, Color{ 255, 255, 255, 255 });
        } else {
            // OoT's bottles as the colas, on a disc in the perk's Black Ops colour.
            Image(g, sShapes.disc, x, y, size, size, Color{ 0, 0, 0, 150 });
            Image(g, sShapes.ring, x - r.S(1.0f), y - r.S(1.0f), size + r.S(2.0f), size + r.S(2.0f), kRims[i]);
            ItemIcon(g, kBottles[i], x + r.S(1.5f), y + r.S(1.5f), size - r.S(3.0f), 255);
        }
        x += size + r.S(3.0f);
    }
}

// Black Ops' red hurt overlay over the region, stronger the closer to going down.
void Vignette(Gfx*& g, const Region& r, float strength) {
    if (strength <= 0.01f) {
        return;
    }
    float x0 = r.Left(0.0f) - 1.0f;
    float x1 = r.Right(0.0f) + 1.0f;
    if (const Tex* bo = Bo(BO_LOW_HEALTH)) {
        Image(g, *bo, x0, r.top, x1 - x0, r.bottom - r.top, Color{ 255, 255, 255, static_cast<u8>(255.0f * strength) });
    } else {
        Image(g, sShapes.vignette, x0, r.top, x1 - x0, r.bottom - r.top,
              Color{ 160, 0, 0, static_cast<u8>(225.0f * strength) });
    }
}

// The points, on a red banner up and left of the dial; changes fly off its left end.
void Points(Gfx*& g, const Region& r, int player, int points) {
    const Run& run = GetRun();
    std::string text = std::to_string(points);
    float h = r.S(14.0f);
    float iconSize = r.S(12.0f);
    float pad = r.S(5.0f);
    float w = std::max(r.S(58.0f), NumberWidth(text, h) + iconSize + pad * 3.0f);
    float right = r.Right(70.0f);
    float top = r.Up(70.0f);
    float left = right - w;
    float bannerH = h + r.S(5.0f);

    if (const Tex* bo = Bo(player == 0 ? BO_SCORE_P1 : BO_SCORE_P2)) {
        Image(g, *bo, left, top, w, bannerH, Color{ 255, 255, 255, 235 });
    } else {
        Image(g, sShapes.banner, left - r.S(3.0f), top, w + r.S(6.0f), bannerH, Color{ 165, 12, 12, 230 });
    }
    // OoT's rupee as the points icon, gold as Black Ops' points are.
    Load(g, gRupeeCounterIconTex, Fmt::Ia8, 16, 16);
    Quad(g, left + pad, top + (bannerH - iconSize) / 2.0f, iconSize, iconSize, 16, 16, Color{ 255, 215, 70, 255 });
    Number(g, text, right - pad - NumberWidth(text, h), top + r.S(2.5f), h, Color{ 255, 255, 255, 255 });

    int slot = 0;
    for (auto it = run.popups.rbegin(); it != run.popups.rend() && slot < 4; ++it) {
        if (it->player != player) {
            continue;
        }
        float age = 1.0f - static_cast<float>(it->framesLeft) / 20.0f;
        std::string pop = (it->amount >= 0 ? "+" : "") + std::to_string(it->amount);
        float ph = r.S(10.0f);
        float px = left - r.S(6.0f) - NumberWidth(pop, ph) - age * r.S(26.0f);
        float py = top + r.S(3.0f) - age * r.S(10.0f) - static_cast<float>(slot) * r.S(9.0f);
        Color pc = it->amount >= 0 ? Color{ 255, 220, 50, 255 } : Color{ 235, 60, 60, 255 };
        Number(g, pop, px, py, ph, WithAlpha(pc, 1.0f - age * 0.8f));
        slot++;
    }
}

// The dial at the bottom right: Black Ops' d-pad ring, holding OoT's B button in the
// middle and the C buttons around it where Black Ops keeps its equipment.
void Dial(Gfx*& g, const Region& r, const PlayerView& v) {
    float cx = r.Right(40.0f);
    float cy = r.Up(42.0f);
    float ringR = r.S(26.0f);
    if (const Tex* bo = Bo(BO_DPAD)) {
        Image(g, *bo, cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f, Color{ 255, 255, 255, 220 });
    } else {
        Image(g, sShapes.disc, cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f, Color{ 0, 0, 0, 110 });
        Image(g, sShapes.ring, cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f, Color{ 150, 20, 20, 230 });
    }

    // B in the middle, green as in OoT.
    float bR = r.S(13.0f);
    u8 bAlpha = v.b.enabled ? 255 : 90;
    Load(g, gButtonBackgroundTex, Fmt::Ia8, 32, 32);
    Quad(g, cx - bR, cy - bR, bR * 2.0f, bR * 2.0f, 32, 32, Color{ 100, 255, 120, static_cast<u8>(bAlpha * 2 / 3) });
    float bIcon = r.S(20.0f);
    ItemIcon(g, v.b.item, cx - bIcon / 2.0f, cy - bIcon / 2.0f, bIcon, bAlpha);
    if (v.b.ammo >= 0) {
        AmmoDigits(g, v.b.ammo, cx - r.S(2.0f), cy + r.S(3.0f), r.S(8.0f),
                   v.b.ammo == 0 ? Color{ 130, 130, 130, bAlpha } : Color{ 255, 255, 255, bAlpha });
    }

    // C-Left, C-Down, C-Right around the ring, yellow as in OoT.
    static const float kAngles[3][2] = { { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f } };
    float cR = r.S(10.0f);
    for (int i = 0; i < 3; i++) {
        const Button& b = v.c[i];
        if (b.item == ITEM_NONE && b.emptyArrow == nullptr) {
            continue;
        }
        float bx = cx + kAngles[i][0] * ringR;
        float by = cy + kAngles[i][1] * ringR;
        u8 alpha = b.enabled ? 255 : 90;
        Load(g, gButtonBackgroundTex, Fmt::Ia8, 32, 32);
        Quad(g, bx - cR, by - cR, cR * 2.0f, cR * 2.0f, 32, 32, Color{ 255, 160, 0, static_cast<u8>(alpha * 3 / 4) });
        if (b.item != ITEM_NONE) {
            float icon = r.S(16.0f);
            ItemIcon(g, b.item, bx - icon / 2.0f, by - icon / 2.0f, icon, alpha);
        } else {
            Load(g, b.emptyArrow, Fmt::Ia8, 32, 32);
            Quad(g, bx - cR, by - cR, cR * 2.0f, cR * 2.0f, 32, 32, Color{ 255, 240, 0, alpha });
        }
        if (b.ammo >= 0) {
            AmmoDigits(g, b.ammo, bx - r.S(1.0f), by + r.S(2.0f), r.S(7.0f),
                       b.ammo == 0 ? Color{ 130, 130, 130, alpha } : Color{ 255, 255, 255, alpha });
        }
    }
}

// Guns_HudInfo's "clip | reserve".
bool ParseAmmo(const std::string& text, int* clip, int* reserve) {
    size_t bar = text.find('|');
    if (bar == std::string::npos) {
        return false;
    }
    char* end = nullptr;
    long c = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str()) {
        return false;
    }
    const char* rest = text.c_str() + bar + 1;
    long r = std::strtol(rest, &end, 10);
    if (end == rest) {
        return false;
    }
    *clip = static_cast<int>(c);
    *reserve = static_cast<int>(r);
    return true;
}

// The gun's name and "clip / reserve", left of the dial as in Black Ops.
void Gun(Gfx*& g, const Region& r, const PlayerView& v) {
    float right = r.Right(80.0f);
    float y = r.Up(45.0f);
    if (v.gun) {
        TextAt(g, v.gunName, right, y, r.S(13.0f), Color{ 235, 235, 235, 255 }, Align::Right);
        int clip = 0, reserve = 0;
        float h = r.S(14.0f);
        if (ParseAmmo(v.gunAmmo, &clip, &reserve)) {
            std::string reserveText = " / " + std::to_string(reserve);
            float rh = r.S(11.0f);
            float rw = NumberWidth(reserveText, rh);
            std::string clipText = std::to_string(clip);
            Color clipColor = clip == 0 ? Color{ 235, 60, 50, 255 } : Color{ 255, 255, 255, 255 };
            Number(g, clipText, right - rw - NumberWidth(clipText, h), r.Up(34.0f), h, clipColor);
            Number(g, reserveText, right - rw, r.Up(34.0f) + (h - rh), rh, Color{ 210, 210, 210, 255 });
        } else {
            TextAt(g, v.gunAmmo, right, r.Up(34.0f), r.S(14.0f), Color{ 255, 255, 255, 255 }, Align::Right);
        }
    }
}

// OoT's magic as a thin green bar under the points banner, when Link has magic.
void Magic(Gfx*& g, const Region& r, const PlayerView& v) {
    if (v.magic < 0.0f) {
        return;
    }
    float right = r.Right(72.0f);
    float w = r.S(52.0f);
    float bh = std::max(1.0f, r.S(2.5f));
    float top = r.Up(50.0f);
    Image(g, sShapes.white, right - w, top, w, bh, Color{ 0, 0, 0, 170 });
    Image(g, sShapes.white, right - w, top, w * v.magic, bh, Color{ 0, 200, 0, 235 });
}

void Crosshair(Gfx*& g, const Region& r) {
    float cy = r.MidY();
    float len = r.S(5.0f);
    float gap = r.S(4.0f);
    float t = std::max(1.0f, r.S(1.5f));
    Color c = { 255, 255, 255, 210 };
    Image(g, sShapes.white, kCentreX - t / 2.0f, cy - gap - len, t, len, c);
    Image(g, sShapes.white, kCentreX - t / 2.0f, cy + gap, t, len, c);
    Image(g, sShapes.white, kCentreX - gap - len, cy - t / 2.0f, len, t, c);
    Image(g, sShapes.white, kCentreX + gap, cy - t / 2.0f, len, t, c);
}

// Timed power-ups along the bottom centre, blinking in their last seconds.
void PowerUps(Gfx*& g, const Region& r, int frame) {
    const Run& run = GetRun();
    struct Shown {
        int bo;
        int item;
        int frames;
        const char* label;
    };
    std::vector<Shown> shown;
    for (int i = 0; i < static_cast<int>(PowerUp::Count); i++) {
        int frames = run.powerUpFrames[i];
        if (frames <= 0) {
            continue;
        }
        switch (static_cast<PowerUp>(i)) {
            case PowerUp::InstaKill:
                shown.push_back({ BO_POWER_INSTAKILL, ITEM_MASK_SKULL, frames, nullptr });
                break;
            case PowerUp::DoublePoints:
                shown.push_back({ BO_POWER_DOUBLE_POINTS, -1, frames, "x2" });
                break;
            case PowerUp::FireSale:
                shown.push_back({ BO_POWER_FIRE_SALE, ITEM_DINS_FIRE, frames, nullptr });
                break;
            default:
                shown.push_back({ -1, -1, frames, PowerUpName(static_cast<PowerUp>(i)) });
                break;
        }
    }
    if (run.stormFrames > 0) {
        shown.push_back({ -1, -1, run.stormFrames, "STORM" });
    }
    if (shown.empty()) {
        return;
    }
    float size = r.S(21.0f);
    float spacing = r.S(26.0f);
    float x = kCentreX - (static_cast<float>(shown.size()) * spacing - (spacing - size)) / 2.0f;
    float y = r.Up(29.0f);
    for (const Shown& s : shown) {
        bool blinkOff = s.frames < 100 && (frame / (s.frames < 40 ? 2 : 4)) % 2 == 0;
        if (!blinkOff) {
            const Tex* bo = s.bo >= 0 ? Bo(s.bo) : nullptr;
            if (bo != nullptr) {
                Image(g, *bo, x, y, size, size, Color{ 255, 255, 255, 255 });
            } else if (s.item >= 0) {
                Image(g, sShapes.disc, x, y, size, size, Color{ 0, 0, 0, 120 });
                ItemIcon(g, s.item, x + r.S(2.0f), y + r.S(2.0f), size - r.S(4.0f), 255);
            } else if (s.bo == BO_POWER_DOUBLE_POINTS) {
                Image(g, sShapes.disc, x, y, size, size, Color{ 0, 0, 0, 120 });
                Load(g, gRupeeCounterIconTex, Fmt::Ia8, 16, 16);
                Quad(g, x + r.S(3.0f), y + r.S(3.0f), size - r.S(6.0f), size - r.S(6.0f), 16, 16,
                     Color{ 255, 215, 70, 255 });
                TextAt(g, s.label, x + size, y + size - r.S(11.0f), r.S(12.0f), Color{ 255, 255, 255, 255 },
                       Align::Right);
            } else {
                TextAt(g, s.label, x + size / 2.0f, y + r.S(6.0f), r.S(11.0f), Color{ 120, 200, 255, 255 },
                       Align::Centre);
            }
        }
        x += spacing;
    }
}

void PlayerHud(Gfx*& g, const Region& r, const PlayerView& v, int round, float flash, int frame) {
    const Run& run = GetRun();
    float pulse = 0.85f + 0.15f * std::sin(static_cast<float>(frame) * 0.35f);
    Vignette(g, r, v.down ? 1.0f : std::clamp((1.0f - v.healthFrac) * 1.4f, 0.0f, 1.0f) * pulse);

    Perks(g, r, run.players[v.index]);
    float roundH = r.S(30.0f);
    Round(g, round, r.Left(14.0f), r.Up(12.0f) - roundH, roundH, flash);

    Points(g, r, v.index, run.players[v.index].points);
    Magic(g, r, v);
    Gun(g, r, v);
    Dial(g, r, v);
    PowerUps(g, r, frame);

    if (v.crosshair) {
        Crosshair(g, r);
    }
    float textSize = std::max(9.0f, r.S(14.0f));
    if (!v.status.empty()) {
        TextAt(g, v.status, kCentreX, r.MidY() - r.S(34.0f), textSize * 1.2f, Color{ 235, 50, 40, 255 }, Align::Centre);
    }
    if (!v.gunHint.empty()) {
        TextAt(g, v.gunHint, kCentreX, r.MidY() + r.S(18.0f), textSize, Color{ 255, 230, 120, 255 }, Align::Centre);
    }
    if (!v.prompt.empty()) {
        TextAt(g, v.prompt, kCentreX, r.Up(92.0f), textSize, Color{ 255, 255, 255, 255 }, Align::Centre);
    }
}

PlayerView LinkView() {
    const Run& run = GetRun();
    PlayerView v;
    v.index = 0;
    v.b.item = gSaveContext.equips.buttonItems[0];
    v.b.enabled = gSaveContext.buttonStatus[0] != BTN_DISABLED;
    v.b.ammo = ItemAmmo(v.b.item);
    static const char* const kArrows[3] = { gEmptyCLeftArrowTex, gEmptyCDownArrowTex, gEmptyCRightArrowTex };
    for (int i = 0; i < 3; i++) {
        Button& b = v.c[i];
        b.item = gSaveContext.equips.buttonItems[i + 1];
        b.enabled = gSaveContext.buttonStatus[i + 1] != BTN_DISABLED;
        b.ammo = ItemAmmo(b.item);
        b.emptyArrow = kArrows[i];
    }
    int capacity = gSaveContext.healthCapacity;
    v.healthFrac = capacity > 0 ? static_cast<float>(gSaveContext.health) / static_cast<float>(capacity) : 1.0f;
    if (!CVarGetInteger(CVAR_ZOMBIES("BlackOpsHealth"), 1)) {
        v.healthFrac = 1.0f; // OoT's hearts are back on screen instead
    }
    v.down = gSaveContext.health <= 0 || run.linkDown.downed;
    if (run.linkDown.bledOut) {
        v.status = "BLED OUT: BACK NEXT ROUND";
    } else if (run.linkDown.downed) {
        v.status = "DOWN! " + std::to_string(run.linkDown.downFrames / 20 + 1);
    }
    v.prompt = run.prompt;
    std::string name, ammo, hint;
    bool crosshair = false;
    if (Guns_HudInfo(&name, &ammo, &hint, &crosshair)) {
        size_t bracket = name.find(" [");
        v.gun = true;
        v.gunName = bracket == std::string::npos ? name : name.substr(0, bracket);
        v.gunAmmo = ammo;
        v.gunHint = hint;
        v.crosshair = crosshair;
    }
    if (gSaveContext.magicCapacity > 0) {
        v.magic = std::clamp(static_cast<float>(gSaveContext.magic) / static_cast<float>(gSaveContext.magicCapacity),
                             0.0f, 1.0f);
    }
    return v;
}

PlayerView PartnerView() {
    const Run& run = GetRun();
    const Partner& p = run.partner;
    static const int kSwords[3] = { ITEM_SWORD_KOKIRI, ITEM_SWORD_MASTER, ITEM_SWORD_BGS };
    PlayerView v;
    v.index = 1;
    v.b.item = kSwords[std::clamp(p.sword, 0, 2)];
    if (p.bombBag) {
        v.c[0].item = ITEM_BOMB;
        v.c[0].ammo = p.bombs;
    }
    v.healthFrac = p.healthCapacity > 0 ? static_cast<float>(p.health) / static_cast<float>(p.healthCapacity) : 1.0f;
    v.down = p.downed || p.bledOut;
    if (p.bledOut) {
        v.status = "BLED OUT: BACK NEXT ROUND";
    } else if (p.downed) {
        v.status = "DOWN! " + std::to_string(p.downFrames / 20 + 1);
    }
    v.prompt = p.prompt;
    return v;
}

// ---------------------------------------------------------------------------

int sShownRound = 0;
int sFlashFrames = 0;
constexpr int kFlashFrames = 50;
int sFrame = 0;
bool sImagesLooked = false;

bool InRun() {
    Phase phase = GetRun().phase;
    return phase == Phase::Active || phase == Phase::Intermission || phase == Phase::Intro;
}

void DrawHud(Gfx** gfx) {
    Gfx*& g = *gfx;
    const Run& run = GetRun();
    // Modulate: texture times the primitive colour, alpha too.
    gDPSetCombineLERP(g++, TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0,
                      PRIMITIVE, 0);
    bool split = Coop_SplitView();
    float flash = static_cast<float>(sFlashFrames) / static_cast<float>(kFlashFrames);

    if (InRun()) {
        if (split) {
            const float half = SCREEN_HEIGHT / 2.0f;
            PlayerHud(g, Region{ 0.0f, half, 0.7f }, LinkView(), run.round, flash, sFrame);
            PlayerHud(g, Region{ half, static_cast<float>(SCREEN_HEIGHT), 0.7f }, PartnerView(), run.round, flash,
                      sFrame);
            // The line between the halves.
            Image(g, sShapes.white, OTRGetDimensionFromLeftEdge(0.0f) - 1.0f, half - 0.5f,
                  OTRGetDimensionFromRightEdge(static_cast<float>(SCREEN_WIDTH)) -
                      OTRGetDimensionFromLeftEdge(0.0f) + 2.0f,
                  1.0f, Color{ 0, 0, 0, 200 });
        } else {
            PlayerHud(g, Region{ 0.0f, static_cast<float>(SCREEN_HEIGHT), 1.0f }, LinkView(), run.round, flash,
                      sFrame);
        }
    } else if (!run.prompt.empty()) {
        TextAt(g, run.prompt, kCentreX, 150.0f, 14.0f, Color{ 255, 255, 255, 255 }, Align::Centre);
    }

    if (run.announcement.framesLeft > 0) {
        float alpha = std::min(1.0f, static_cast<float>(run.announcement.framesLeft) / 10.0f);
        Color c = WithAlpha(Color{ 255, 230, 120, 255 }, alpha);
        if (split) {
            TextAt(g, run.announcement.text, kCentreX, 22.0f, 11.0f, c, Align::Centre);
            TextAt(g, run.announcement.text, kCentreX, 142.0f, 11.0f, c, Align::Centre);
        } else {
            TextAt(g, run.announcement.text, kCentreX, 52.0f, 15.0f, c, Align::Centre);
        }
    }
}

// Layout helper: Link's position, for placing arena spots.
void DrawCoordinates(GfxPrint* printer) {
    if (gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    char buf[48];
    snprintf(buf, sizeof(buf), "X %d  Y %d  Z %d", static_cast<int>(player->actor.world.pos.x),
             static_cast<int>(player->actor.world.pos.y), static_cast<int>(player->actor.world.pos.z));
    GfxPrint_SetColor(printer, 200, 200, 200, 255);
    GfxPrint_SetPos(printer, 2, 1);
    GfxPrint_Printf(printer, "%s", buf);
}

// OoT's own HUD (buttons, hearts, magic, rupees, minimap) fades out for the run: this
// HUD draws the buttons in the dial and Black Ops' vignette stands in for the hearts.
// With Black Ops health off, OoT's hearts stay. The buttons keep working either way.
bool sHidOotHud = false;

void ManageOotHud() {
    if (InRun()) {
        u16 want = static_cast<u16>(CVarGetInteger(CVAR_ZOMBIES("BlackOpsHealth"), 1) ? HUD_VISIBILITY_NOTHING
                                                                                       : HUD_VISIBILITY_HEARTS);
        if (gSaveContext.hudVisibilityMode != want) {
            Interface_ChangeHudVisibilityMode(want);
        }
        sHidOotHud = true;
    } else if (sHidOotHud) {
        sHidOotHud = false;
        Interface_ChangeHudVisibilityMode(HUD_VISIBILITY_ALL);
    }
}

} // namespace

// Every game frame while the mode is on.
void Hud_Draw() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    if (play->pauseCtx.state != 0) {
        return;
    }
    ManageOotHud();

    const Run& run = GetRun();
    bool inRun = InRun();
    bool coords = CVarGetInteger(CVAR_ZOMBIES("ShowCoordinates"), 0) != 0;
    if (!inRun) {
        sShownRound = 0;
        sImagesLooked = false;
    }
    if (!inRun && run.prompt.empty() && run.announcement.framesLeft <= 0 && !coords) {
        return;
    }
    if (!sShapes.ready) {
        MakeShapes();
    }
    if (!sChalkReady) {
        MakeChalk();
    }
    if (inRun) {
        if (!sImagesLooked) {
            sImagesLooked = true;
            LoadBoImages(); // a new run: pick up images converted since the last one
        }
        if (run.round != sShownRound) {
            sFlashFrames = sShownRound == 0 ? 0 : kFlashFrames;
            sShownRound = run.round;
        }
        if (sFlashFrames > 0) {
            sFlashFrames--;
        }
    }
    sFrame++;
    ZombiesHud_DrawOverlay(play, DrawHud);
    if (coords) {
        ZombiesHud_DrawPrinter(play, DrawCoordinates);
    }
}

} // namespace Zombies
