// Zombies of Time: chalk outlines of the wall buys.
//
// Black Ops marks each wall buy with a chalk drawing of the gun. Here the drawing is made
// at run time from the model itself: the Zelda item's get-item model from the player's
// own oot.o2r, or the gun's world model imported from their own Black Ops install. The
// model's triangles are read out of its display list, laid flat in a side view, and the
// silhouette's edge (plus the creases where the depth jumps) is drawn as grainy white
// chalk into a texture. Nothing drawn here ships with the mode.
//
// The drawing goes on the wall nearest the Armory, and shows the rack entry Link is
// looking at (R steps through it). Close up, the price is chalked underneath.

#include "ZombiesMode.h"
#include "ZombiesGuns.h"
#include "ZombiesModel.h"

#include <libultraship/bridge/resourcebridge.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "soh/ResourceManagerHelpers.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"

// z_draw.c's get-item table: each entry's draw function and the display lists it draws.
struct ZombiesDrawItemEntry {
    void (*drawFunc)(PlayState*, s16);
    Gfx* dlists[8];
};
extern ZombiesDrawItemEntry sDrawItemTable[];

// ZombiesFx.cpp: a translucent display list at anchor, scaled.
void ZombiesFx_Submit(PlayState* play, Gfx* dl, const Vec3f* anchor, float scale);
}

namespace Zombies {

// ZombiesGuns.cpp: an imported gun's world model display list (vertices in inches times
// Model_VertexScale()), null when it wasn't imported.
Gfx* Guns_WorldDl(GunModel* gm);

// ---------------------------------------------------------------------------
// Reading triangles out of a display list

struct Mesh {
    std::vector<Vec3f> tris; // three corners per triangle
};

class DlReader {
  public:
    explicit DlReader(Mesh* mesh) : mMesh(mesh) {
    }

    void Walk(const Gfx* dl, int depth = 0) {
        if (dl == nullptr || depth > 12) {
            return;
        }
        for (int guard = 0; guard < 65536; guard++, dl++) {
            u32 w0 = static_cast<u32>(dl->words.w0);
            uintptr_t w1 = dl->words.w1;
            u8 op = static_cast<u8>(w0 >> 24);
            switch (op) {
                case G_VTX:
                case G_VTX_WIDE:
                    LoadVerts(w0, reinterpret_cast<const Vtx*>(Pointer(w1)));
                    break;
                case G_VTX_OTR_HASH: {
                    uintptr_t offset = w1;
                    dl++;
                    u64 hash = (static_cast<u64>(static_cast<u32>(dl->words.w0)) << 32) |
                               static_cast<u32>(dl->words.w1);
                    const Vtx* v = nullptr;
                    if (offset > 0xFFFFF) {
                        v = reinterpret_cast<const Vtx*>(offset); // already resolved by a draw
                    } else if (void* base = ResourceGetDataByCrc(hash)) {
                        v = reinterpret_cast<const Vtx*>(static_cast<const char*>(base) + offset);
                    }
                    LoadVerts(w0, v);
                    break;
                }
                case G_VTX_OTR_FILEPATH: {
                    const char* path = reinterpret_cast<const char*>(w1);
                    dl++;
                    size_t count = static_cast<size_t>(dl->words.w0);
                    size_t dest = static_cast<size_t>(dl->words.w1 >> 16);
                    size_t offset = static_cast<size_t>(dl->words.w1 & 0xFFFF);
                    const Vtx* v = path != nullptr ? static_cast<const Vtx*>(ResourceGetDataByName(path)) : nullptr;
                    if (v != nullptr) {
                        Copy(v + offset, dest, count);
                    }
                    break;
                }
                case G_TRI1:
                case G_TRI1_WIDE:
                    Tri((w0 >> 16) & 0xFF, (w0 >> 8) & 0xFF, w0 & 0xFF, true);
                    break;
                case G_TRI2:
                case G_QUAD: {
                    u32 w1lo = static_cast<u32>(w1);
                    Tri((w0 >> 16) & 0xFF, (w0 >> 8) & 0xFF, w0 & 0xFF, true);
                    Tri((w1lo >> 16) & 0xFF, (w1lo >> 8) & 0xFF, w1lo & 0xFF, true);
                    break;
                }
                case G_TRI1_OTR: {
                    u32 w1lo = static_cast<u32>(w1);
                    Tri(w0 & 0xFFFF, w1lo >> 16, w1lo & 0xFFFF, false);
                    break;
                }
                case G_DL: {
                    const Gfx* sub = reinterpret_cast<const Gfx*>(Pointer(w1));
                    if (((w0 >> 16) & 0xFF) == G_DL_PUSH) {
                        Walk(sub, depth + 1);
                        break;
                    }
                    Walk(sub, depth + 1); // a branch: the rest of this list never runs
                    return;
                }
                case G_DL_OTR_HASH: {
                    bool push = ((w0 >> 16) & 0xFF) == G_DL_PUSH;
                    dl++;
                    u64 hash = (static_cast<u64>(static_cast<u32>(dl->words.w0)) << 32) |
                               static_cast<u32>(dl->words.w1);
                    Walk(static_cast<const Gfx*>(ResourceGetDataByCrc(hash)), depth + 1);
                    if (!push) {
                        return;
                    }
                    break;
                }
                case G_DL_OTR_FILEPATH: {
                    const char* path = reinterpret_cast<const char*>(w1);
                    const Gfx* sub = path != nullptr ? static_cast<const Gfx*>(ResourceGetDataByName(path)) : nullptr;
                    Walk(sub, depth + 1);
                    if (((w0 >> 16) & 0x1) != 0) {
                        return;
                    }
                    break;
                }
                case G_SETTIMG_OTR_HASH:
                case G_MARKER:
                case G_MTX_OTR:
                case G_BRANCH_Z_OTR:
                case G_MOVEMEM_OTR:
                    dl++; // two-word commands
                    break;
                case G_SETTILESIZE_INTERP:
                    dl += 2;
                    break;
                case G_ENDDL:
                    return;
                default:
                    break;
            }
        }
    }

  private:
    static constexpr size_t kBuffer = 256;

    // Real pointers only: a segment address (the item's scrolling textures) is skipped.
    static uintptr_t Pointer(uintptr_t w1) {
        return w1 < 0x10000000u ? 0 : w1;
    }

    void LoadVerts(u32 w0, const Vtx* v) {
        size_t n = (w0 >> 12) & 0xFF;
        size_t end = (w0 >> 1) & 0x7F;
        if (v == nullptr || end < n) {
            return;
        }
        Copy(v, end - n, n);
    }

    void Copy(const Vtx* v, size_t dest, size_t count) {
        for (size_t i = 0; i < count && dest + i < kBuffer; i++) {
            const s16* ob = v[i].v.ob;
            mVerts[dest + i] = { static_cast<f32>(ob[0]), static_cast<f32>(ob[1]), static_cast<f32>(ob[2]) };
            mValid[dest + i] = true;
        }
    }

    void Tri(u32 a, u32 b, u32 c, bool halved) {
        if (halved) {
            a /= 2;
            b /= 2;
            c /= 2;
        }
        if (a >= kBuffer || b >= kBuffer || c >= kBuffer || !mValid[a] || !mValid[b] || !mValid[c]) {
            return;
        }
        mMesh->tris.push_back(mVerts[a]);
        mMesh->tris.push_back(mVerts[b]);
        mMesh->tris.push_back(mVerts[c]);
    }

    Mesh* mMesh;
    Vec3f mVerts[kBuffer] = {};
    bool mValid[kBuffer] = {};
};

// ---------------------------------------------------------------------------
// Drawing the chalk

struct ChalkImage {
    int w = 0;
    int h = 0;
    std::vector<u32> px; // RGBA8, big-endian order as the RDP reads it
    float aspect = 1.0f; // the drawing's width over its height, before padding
};

static u32 Hash(u32 x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static float Noise01(int x, int y, u32 seed) {
    return static_cast<float>(Hash(static_cast<u32>(x) * 73856093u ^ static_cast<u32>(y) * 19349663u ^ seed) & 0xFFFF) /
           65535.0f;
}

// Smooth value noise, for the stroke's wander and the chalk wearing thin in patches.
static float SmoothNoise(float x, float y, u32 seed) {
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    float fx = x - static_cast<float>(ix);
    float fy = y - static_cast<float>(iy);
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float a = Noise01(ix, iy, seed);
    float b = Noise01(ix + 1, iy, seed);
    float c = Noise01(ix, iy + 1, seed);
    float d = Noise01(ix + 1, iy + 1, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

static u32 Texel(float alpha) {
    u32 a = static_cast<u32>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    u32 rgba = (0xFFu << 24) | (0xFFu << 16) | (0xFFu << 8) | a; // white, R in the top byte
    // The RDP reads bytes in memory order: R, G, B, A.
    u8 bytes[4] = { static_cast<u8>(rgba >> 24), static_cast<u8>(rgba >> 16), static_cast<u8>(rgba >> 8),
                    static_cast<u8>(rgba) };
    u32 out;
    std::memcpy(&out, bytes, 4);
    return out;
}

// Distance (in pixels, chamfer 3-4) from each pixel where mask == want to the nearest
// pixel where it isn't.
static std::vector<float> Distance(const std::vector<u8>& mask, int w, int h, u8 want) {
    const float big = 1e6f;
    std::vector<float> d(mask.size());
    for (size_t i = 0; i < mask.size(); i++) {
        d[i] = mask[i] == want ? big : 0.0f;
    }
    auto at = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= w || y >= h) {
            return want ? 0.0f : big; // off the image counts as outside
        }
        return d[static_cast<size_t>(y * w + x)];
    };
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float& v = d[static_cast<size_t>(y * w + x)];
            if (v > 0.0f) {
                v = std::min({ v, at(x - 1, y) + 1.0f, at(x, y - 1) + 1.0f, at(x - 1, y - 1) + 1.414f,
                               at(x + 1, y - 1) + 1.414f });
            }
        }
    }
    for (int y = h - 1; y >= 0; y--) {
        for (int x = w - 1; x >= 0; x--) {
            float& v = d[static_cast<size_t>(y * w + x)];
            if (v > 0.0f) {
                v = std::min({ v, at(x + 1, y) + 1.0f, at(x, y + 1) + 1.0f, at(x + 1, y + 1) + 1.414f,
                               at(x - 1, y + 1) + 1.414f });
            }
        }
    }
    return d;
}

// Lays the mesh flat and chalks its outline. axisU runs left to right, axisV bottom to
// top, the third axis is depth. axis -1 picks by the mesh's shape: it is seen from the
// side that shows the most of it, and a slanted item is turned level.
static bool DrawChalk(const Mesh& mesh, int axisU, int axisV, u32 seed, ChalkImage* out) {
    bool straighten = axisU < 0;
    if (mesh.tris.size() < 3) {
        return false;
    }
    Vec3f lo = mesh.tris[0];
    Vec3f hi = mesh.tris[0];
    for (const Vec3f& p : mesh.tris) {
        lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
        hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
    }
    float ext[3] = { hi.x - lo.x, hi.y - lo.y, hi.z - lo.z };
    auto comp = [](const Vec3f& p, int i) { return i == 0 ? p.x : i == 1 ? p.y : p.z; };
    if (axisU < 0) {
        // The side view that shows the most of it: the depth axis whose projection covers
        // the most area (a bow seen face-on, not edge-on).
        int depthAxis = 0;
        float bestArea = -1.0f;
        for (int view = 0; view < 3; view++) {
            int a = (view + 1) % 3;
            int b = (view + 2) % 3;
            float area = 0.0f;
            for (size_t t = 0; t + 2 < mesh.tris.size(); t += 3) {
                const Vec3f& p0 = mesh.tris[t];
                const Vec3f& p1 = mesh.tris[t + 1];
                const Vec3f& p2 = mesh.tris[t + 2];
                float a0 = comp(p1, a) - comp(p0, a);
                float b0 = comp(p1, b) - comp(p0, b);
                float a1 = comp(p2, a) - comp(p0, a);
                float b1 = comp(p2, b) - comp(p0, b);
                area += std::fabs(a0 * b1 - a1 * b0);
            }
            if (area > bestArea) {
                bestArea = area;
                depthAxis = view;
            }
        }
        int a = (depthAxis + 1) % 3;
        int b = (depthAxis + 2) % 3;
        // Upright stays upright: Y is the height whenever it isn't the depth.
        if (a == 1 || b == 1) {
            axisV = 1;
            axisU = a == 1 ? b : a;
        } else {
            axisU = ext[a] >= ext[b] ? a : b;
            axisV = axisU == a ? b : a;
        }
    }
    int axisD = 3 - axisU - axisV;

    // Flat on the wall: (u, v) across and up, d towards the viewer.
    std::vector<Vec3f> flat(mesh.tris.size());
    for (size_t i = 0; i < mesh.tris.size(); i++) {
        flat[i] = { comp(mesh.tris[i], axisU), comp(mesh.tris[i], axisV), comp(mesh.tris[i], axisD) };
    }
    if (straighten) {
        // An item modelled at a slant (the bow) is turned level, as a wall buy hangs.
        float mu = 0.0f, mv = 0.0f;
        for (const Vec3f& f : flat) {
            mu += f.x;
            mv += f.y;
        }
        mu /= static_cast<float>(flat.size());
        mv /= static_cast<float>(flat.size());
        float cuu = 0.0f, cvv = 0.0f, cuv = 0.0f;
        for (const Vec3f& f : flat) {
            cuu += (f.x - mu) * (f.x - mu);
            cvv += (f.y - mv) * (f.y - mv);
            cuv += (f.x - mu) * (f.y - mv);
        }
        float angle = 0.5f * std::atan2(2.0f * cuv, cuu - cvv);
        float mid = 0.5f * (cuu + cvv);
        float spread = sqrtf(0.25f * (cuu - cvv) * (cuu - cvv) + cuv * cuv);
        bool elongated = mid - spread > 0.0f ? (mid + spread) / (mid - spread) > 2.25f : true;
        if (elongated && std::fabs(angle) > 0.05f) {
            float cs = std::cos(-angle);
            float sn = std::sin(-angle);
            for (Vec3f& f : flat) {
                float u = f.x - mu;
                float v = f.y - mv;
                f.x = u * cs - v * sn;
                f.y = u * sn + v * cs;
            }
        }
    }
    Vec3f flo = flat[0];
    Vec3f fhi = flat[0];
    for (const Vec3f& f : flat) {
        flo = { std::min(flo.x, f.x), std::min(flo.y, f.y), std::min(flo.z, f.z) };
        fhi = { std::max(fhi.x, f.x), std::max(fhi.y, f.y), std::max(fhi.z, f.z) };
    }
    float spanU = std::max(fhi.x - flo.x, 1.0f);
    float spanV = std::max(fhi.y - flo.y, 1.0f);
    float spanD = std::max(fhi.z - flo.z, 1.0f);

    // A power-of-two texture, 128 across its long side, the drawing inside a margin.
    float aspect = spanU / spanV;
    int w = 128;
    int h = 128;
    if (aspect >= 1.4f) {
        h = aspect >= 2.8f ? 32 : 64;
    } else if (aspect <= 0.71f) {
        w = aspect <= 0.36f ? 32 : 64;
    }
    const float margin = 4.0f;
    float scale = std::min((static_cast<float>(w) - 2.0f * margin) / spanU, (static_cast<float>(h) - 2.0f * margin) / spanV);
    float offU = (static_cast<float>(w) - spanU * scale) * 0.5f;
    float offV = (static_cast<float>(h) - spanV * scale) * 0.5f;

    // Rasterise: coverage and the nearest depth (the side facing the wall's viewer).
    size_t n = static_cast<size_t>(w * h);
    std::vector<u8> mask(n, 0);
    std::vector<float> depth(n, -1e9f);
    for (size_t t = 0; t + 2 < flat.size(); t += 3) {
        float px[3], py[3], pd[3];
        for (int k = 0; k < 3; k++) {
            const Vec3f& f = flat[t + static_cast<size_t>(k)];
            px[k] = offU + (f.x - flo.x) * scale;
            py[k] = static_cast<float>(h) - (offV + (f.y - flo.y) * scale); // image rows run down
            pd[k] = (f.z - flo.z) / spanD;
        }
        float area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
        if (std::fabs(area) < 1e-4f) {
            continue;
        }
        int x0 = std::max(0, static_cast<int>(std::floor(std::min({ px[0], px[1], px[2] }))));
        int x1 = std::min(w - 1, static_cast<int>(std::ceil(std::max({ px[0], px[1], px[2] }))));
        int y0 = std::max(0, static_cast<int>(std::floor(std::min({ py[0], py[1], py[2] }))));
        int y1 = std::min(h - 1, static_cast<int>(std::ceil(std::max({ py[0], py[1], py[2] }))));
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                float cx = static_cast<float>(x) + 0.5f;
                float cy = static_cast<float>(y) + 0.5f;
                float b0 = ((px[1] - cx) * (py[2] - cy) - (px[2] - cx) * (py[1] - cy)) / area;
                float b1 = ((px[2] - cx) * (py[0] - cy) - (px[0] - cx) * (py[2] - cy)) / area;
                float b2 = 1.0f - b0 - b1;
                const float eps = -0.02f; // thin parts (a barrel seen edge-on) still cover their pixels
                if (b0 < eps || b1 < eps || b2 < eps) {
                    continue;
                }
                size_t i = static_cast<size_t>(y * w + x);
                mask[i] = 1;
                depth[i] = std::max(depth[i], b0 * pd[0] + b1 * pd[1] + b2 * pd[2]);
            }
        }
    }
    // Close pinholes between triangles that don't quite meet.
    std::vector<u8> closed = mask;
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            size_t i = static_cast<size_t>(y * w + x);
            if (!mask[i]) {
                int around = mask[i - 1] + mask[i + 1] + mask[i - static_cast<size_t>(w)] + mask[i + static_cast<size_t>(w)];
                closed[i] = around >= 3 ? 1 : 0;
            }
        }
    }
    mask.swap(closed);

    std::vector<float> inside = Distance(mask, w, h, 1);
    std::vector<float> outside = Distance(mask, w, h, 0);

    out->w = w;
    out->h = h;
    out->aspect = aspect;
    out->px.assign(n, Texel(0.0f));
    bool any = false;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t i = static_cast<size_t>(y * w + x);
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);
            // The edge, a stroke about two pixels wide that wanders and thins in places.
            float edge = mask[i] ? inside[i] - 0.5f : outside[i] - 0.5f;
            float wander = (SmoothNoise(fx * 0.25f, fy * 0.25f, seed) - 0.5f) * 1.2f;
            float width = 1.1f + SmoothNoise(fx * 0.12f, fy * 0.12f, seed ^ 0x5bd1e995u) * 0.9f;
            float stroke = 1.0f - std::fabs(edge + wander * 0.5f - 0.3f) / width;
            float alpha = stroke > 0.0f ? 0.6f + 0.4f * stroke : 0.0f;

            // Creases: where the depth jumps inside the shape (a magazine, a stock).
            if (mask[i] && x > 0 && y > 0 && x < w - 1 && y < h - 1) {
                float jump = 0.0f;
                for (int k = 0; k < 4; k++) {
                    size_t j = k == 0 ? i - 1 : k == 1 ? i + 1 : k == 2 ? i - static_cast<size_t>(w) : i + static_cast<size_t>(w);
                    if (mask[j]) {
                        jump = std::max(jump, std::fabs(depth[i] - depth[j]));
                    }
                }
                if (jump > 0.18f && inside[i] > 2.0f) {
                    alpha = std::max(alpha, 0.55f);
                }
                // A few loose shading strokes across the body, as chalk drawings have.
                int band = (x + 2 * y) % 9;
                if (band == 0 && inside[i] > 2.5f && SmoothNoise(fx * 0.18f, fy * 0.18f, seed ^ 0x2545F491u) > 0.45f) {
                    alpha = std::max(alpha, 0.22f);
                }
            }
            if (alpha <= 0.0f) {
                // Dust: the odd speck just off the line.
                if (outside[i] < 3.0f && Noise01(x, y, seed ^ 0x9E3779B9u) > 0.93f) {
                    alpha = 0.25f;
                } else {
                    continue;
                }
            }
            // Grain: chalk skips over the wall's texture.
            float grain = Noise01(x, y, seed);
            alpha *= 0.55f + 0.45f * grain;
            if (grain < 0.12f) {
                alpha *= 0.3f;
            }
            alpha *= 0.75f + 0.25f * SmoothNoise(fx * 0.08f, fy * 0.08f, seed ^ 0x68E31DA4u); // worn patches
            out->px[i] = Texel(alpha);
            any = true;
        }
    }
    return any;
}

// The price, in grainy chalk numerals (our own 5x7 font, each dot a 3x3 blob).
static const u8 kDigits5x7[10][7] = {
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
};

static void DrawPrice(int cost, ChalkImage* out) {
    out->w = 128;
    out->h = 32;
    out->px.assign(static_cast<size_t>(out->w * out->h), Texel(0.0f));
    std::string text = std::to_string(std::clamp(cost, 0, 99999));
    const int dot = 3;
    const int digitW = 5 * dot + 4;
    int left = (out->w - static_cast<int>(text.size()) * digitW) / 2;
    u32 seed = static_cast<u32>(cost) * 2654435761u;
    for (size_t c = 0; c < text.size(); c++) {
        const u8* rows = kDigits5x7[text[c] - '0'];
        int tilt = static_cast<int>(Hash(seed + static_cast<u32>(c)) % 3u); // each numeral leans its own way
        int drop = static_cast<int>(Hash(seed ^ static_cast<u32>(c * 977u)) % 3u);
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (!(rows[row] & (0x10 >> col))) {
                    continue;
                }
                for (int py = 0; py < dot; py++) {
                    for (int px = 0; px < dot; px++) {
                        int x = left + static_cast<int>(c) * digitW + col * dot + px + (6 - row) * tilt / 6;
                        int y = 3 + drop + row * dot + py;
                        if (x < 0 || x >= out->w || y >= out->h) {
                            continue;
                        }
                        float grain = Noise01(x, y, seed);
                        float alpha = (0.7f + 0.3f * grain) * (grain < 0.1f ? 0.35f : 1.0f);
                        out->px[static_cast<size_t>(y * out->w + x)] = Texel(alpha);
                    }
                }
            }
        }
    }
    out->aspect = 4.0f;
}

// ---------------------------------------------------------------------------
// The models

struct Drawing {
    bool tried = false;
    bool ok = false;
    ChalkImage image;
    float width = 0.0f; // OoT units across the wall
};

static std::map<std::string, Drawing> sDrawings;
static std::map<int, ChalkImage> sPrices;

static u32 Seed(const std::string& key) {
    u32 h = 2166136261u;
    for (char c : key) {
        h = (h ^ static_cast<u8>(c)) * 16777619u;
    }
    return h;
}

// A get-item model, from the player's own oot.o2r.
static const Drawing* ItemDrawing(int gid) {
    std::string key = "gi:" + std::to_string(gid);
    Drawing& d = sDrawings[key];
    if (!d.tried) {
        d.tried = true;
        Mesh mesh;
        DlReader reader(&mesh);
        if (gid >= 0 && gid < GID_MAXIMUM) {
            for (Gfx* entry : sDrawItemTable[gid].dlists) {
                if (entry == nullptr) {
                    continue;
                }
                char* path = reinterpret_cast<char*>(entry);
                const Gfx* dl = ResourceMgr_OTRSigCheck(path) == 1 ? ResourceMgr_LoadGfxByName(path) : entry;
                reader.Walk(dl);
            }
        }
        d.ok = DrawChalk(mesh, -1, -1, Seed(key), &d.image);
        d.width = d.image.aspect >= 1.0f ? 40.0f : 40.0f * d.image.aspect;
    }
    return d.ok ? &d : nullptr;
}

// A Black Ops gun's world model, imported from the player's own install. Its axes as
// imported: +X down the barrel, +Y up.
static const Drawing* GunDrawing(GunId id) {
    const GunStats& gun = GunInfo(id);
    std::string key = std::string("gun:") + gun.boName;
    Drawing& d = sDrawings[key];
    if (!d.tried) {
        d.tried = true;
        Gfx* dl = Guns_WorldDl(Guns_ImportedModel(gun.boName));
        if (dl != nullptr) {
            Mesh mesh;
            DlReader(&mesh).Walk(dl);
            d.ok = DrawChalk(mesh, 0, 1, Seed(key), &d.image);
            float lo = 0.0f;
            float hi = 0.0f;
            for (const Vec3f& v : mesh.tris) {
                lo = std::min(lo, v.x);
                hi = std::max(hi, v.x);
            }
            float inches = (hi - lo) / Model_VertexScale();
            // A little over life size (Black Ops inches to OoT units: a 72-inch zombie
            // stands 64 tall), so a short gun still reads from across the room.
            d.width = std::clamp(inches * (64.0f / 72.0f) * 1.3f, 30.0f, 64.0f);
        }
    }
    return d.ok ? &d : nullptr;
}

static const ChalkImage& PriceImage(int cost) {
    auto it = sPrices.find(cost);
    if (it == sPrices.end()) {
        ChalkImage img;
        DrawPrice(cost, &img);
        it = sPrices.emplace(cost, std::move(img)).first;
    }
    return it->second;
}

static int ItemGid(u8 item, bool ammo) {
    if (ammo) {
        return LINK_IS_ADULT ? GID_ARROWS_MEDIUM : GID_SEEDS;
    }
    switch (item) {
        case ITEM_SLINGSHOT:
            return GID_SLINGSHOT;
        case ITEM_BOOMERANG:
            return GID_BOOMERANG;
        case ITEM_BOMB:
            return GID_BOMB;
        case ITEM_BOW:
            return GID_BOW;
        case ITEM_HOOKSHOT:
            return GID_HOOKSHOT;
        case ITEM_HAMMER:
            return GID_HAMMER;
        default:
            return -1;
    }
}

// ---------------------------------------------------------------------------
// The wall

struct Wall {
    bool found = false;
    Vec3f from = {};   // the Armory spot it was found from
    s16 scene = -1;
    Vec3f centre = {}; // the drawing's middle, just off the surface
    Vec3f normal = {}; // out of the wall
    bool floor = false; // no wall near: the drawing lies on the ground
};

static Wall sWall;
static std::vector<Wall> sWallBuyWalls; // one per wall buy of an imported map

// The wall at (x, z), seen along -n from just in front. off: how far it stands out of
// the plane through onPlane (negative: set back); dot: how nearly it faces along n.
static bool WallAt(PlayState* play, const Vec3f& onPlane, const Vec3f& n, float y, float* off, float* dot) {
    Vec3f a = { onPlane.x + n.x * 30.0f, y, onPlane.z + n.z * 30.0f };
    Vec3f b = { onPlane.x - n.x * 40.0f, y, onPlane.z - n.z * 40.0f };
    Vec3f hit;
    CollisionPoly* poly = nullptr;
    s32 bgId = 0;
    if (!BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, false, false, true, &bgId) ||
        poly == nullptr) {
        return false;
    }
    *dot = COLPOLY_GET_NORMAL(poly->normal.x) * n.x + COLPOLY_GET_NORMAL(poly->normal.z) * n.z;
    *off = (hit.x - onPlane.x) * n.x + (hit.z - onPlane.z) * n.z;
    return true;
}

// Near enough the same plane (a few units out or in, as a panelled wall steps), facing
// much the same way.
static bool SameWall(PlayState* play, const Vec3f& onPlane, const Vec3f& n, float y) {
    float off, dot;
    return WallAt(play, onPlane, n, y, &off, &dot) && dot > 0.95f && std::fabs(off) < 8.0f;
}

// centred: a Black Ops wall buy's own spot on the wall, where the drawing goes as it is
// (no torch to clear), at the spot's height. Otherwise the Armory torch on its floor.
static void FindWall(PlayState* play, const Vec3f& at, Wall& wall, bool centred = false) {
    wall = Wall();
    wall.found = true;
    wall.from = at;
    wall.scene = play->sceneNum;
    float best = 1e9f;
    Vec3f n = {};
    Vec3f wallHit = {};
    float front = 0.0f;
    const float height = centred ? at.y : at.y + 50.0f;
    for (int i = 0; i < 16; i++) {
        s16 yaw = static_cast<s16>(i * 0x1000);
        // From a little behind the spot: a Black Ops wall buy's spot is on the wall itself.
        Vec3f a = { at.x - Math_SinS(yaw) * 25.0f, height, at.z - Math_CosS(yaw) * 25.0f };
        Vec3f b = { at.x + Math_SinS(yaw) * 220.0f, height, at.z + Math_CosS(yaw) * 220.0f };
        Vec3f hit;
        CollisionPoly* poly = nullptr;
        s32 bgId = 0;
        if (!BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, false, false, true, &bgId) ||
            poly == nullptr) {
            continue;
        }
        Vec3f pn = { COLPOLY_GET_NORMAL(poly->normal.x), COLPOLY_GET_NORMAL(poly->normal.y),
                     COLPOLY_GET_NORMAL(poly->normal.z) };
        if (std::fabs(pn.y) > 0.3f) {
            continue; // a slope, not a wall
        }
        float len = sqrtf(pn.x * pn.x + pn.z * pn.z);
        pn = { pn.x / len, 0.0f, pn.z / len };
        // The nearest wall facing the spot (which may sit a little into it).
        float f = (at.x - hit.x) * pn.x + (at.z - hit.z) * pn.z;
        if (f < -10.0f || std::fabs(f) >= best) {
            continue;
        }
        best = std::fabs(f);
        n = pn;
        front = f;
        wallHit = hit;
    }
    if (best > 1e8f) {
        // Open ground: chalk it on the floor in front of the torch, facing the run's start.
        const Run& run = GetRun();
        s16 yaw = Math_Vec3f_Yaw(const_cast<Vec3f*>(&at), const_cast<Vec3f*>(&run.origin));
        wall.floor = true;
        wall.normal = { Math_SinS(yaw), 0.0f, Math_CosS(yaw) };
        wall.centre = { at.x + wall.normal.x * 40.0f, at.y + 1.0f, at.z + wall.normal.z * 40.0f };
        return;
    }
    // The point of the wall's plane straight behind the torch, and the drawing beside it
    // so the torch doesn't stand in front of it, where the wall goes on far enough.
    Vec3f base = { at.x - n.x * front, height, at.z - n.z * front };
    if (centred) {
        float lift = (front > 0.0f && front < 12.0f ? front : 0.0f) + 1.5f;
        wall.normal = n;
        wall.centre = { base.x + n.x * lift, height, base.z + n.z * lift };
        return;
    }
    Vec3f along = { n.z, 0.0f, -n.x };
    Vec3f place = { wallHit.x, height, wallHit.z }; // failing that, where the ray met it
    // The further out the torch stands, the further aside the drawing goes to clear it.
    float aside = 48.0f + std::max(front, 0.0f);
    // First where the wall runs flat under the whole drawing, else where there is wall
    // facing the same way behind its middle (a tree trunk).
    float depth = 0.0f; // a curved wall: the chosen spot's surface, out of the plane through base
    bool curved = false;
    bool placed = false;
    for (float shift : { aside, -aside }) {
        Vec3f p = { base.x + along.x * shift, height, base.z + along.z * shift };
        Vec3f e1 = { p.x + along.x * 22.0f, height, p.z + along.z * 22.0f };
        Vec3f e2 = { p.x - along.x * 22.0f, height, p.z - along.z * 22.0f };
        if (SameWall(play, p, n, height) && SameWall(play, e1, n, height) && SameWall(play, e2, n, height)) {
            place = p;
            placed = true;
            break;
        }
    }
    for (float shift : { aside, -aside, aside * 0.6f, -aside * 0.6f }) {
        Vec3f p = { base.x + along.x * shift, height, base.z + along.z * shift };
        float off, dot;
        if (placed || !WallAt(play, p, n, height, &off, &dot) || dot < 0.7f || std::fabs(off) > 30.0f) {
            continue;
        }
        place = p;
        depth = off;
        curved = true;
        placed = true;
    }
    // A Black Ops wall buy's spot is the face of the wall as drawn, which can stand a
    // little proud of its collision: the chalk goes on the face.
    float lift = (curved ? depth : front > 0.0f && front < 12.0f ? front : 0.0f) + 1.5f;
    wall.normal = n;
    wall.centre = { place.x + n.x * lift, height, place.z + n.z * lift };
}

// One textured quad: centre, half extents along right and up. A texture load holds at
// most 4096 texels, so a bigger image goes up in bands of rows, one quad each.
static void Quad(std::vector<Gfx>& dl, std::vector<Vtx>& verts, const ChalkImage& img, const Vec3f& c,
                 const Vec3f& right, const Vec3f& up, float halfW, float halfH, u8 alpha) {
    if (alpha == 0 || img.w <= 0 || img.h <= 0) {
        return;
    }
    int bandRows = std::max(1, std::min(img.h, 4096 / img.w));
    for (int top = 0; top < img.h; top += bandRows) {
        int rows = std::min(bandRows, img.h - top);
        // This band's share of the quad, top (+1) to bottom (-1).
        float y0 = 1.0f - 2.0f * static_cast<float>(top) / static_cast<float>(img.h);
        float y1 = 1.0f - 2.0f * static_cast<float>(top + rows) / static_cast<float>(img.h);
        size_t base = verts.size();
        const float corners[4][2] = { { -1.0f, y0 }, { 1.0f, y0 }, { 1.0f, y1 }, { -1.0f, y1 } };
        for (int k = 0; k < 4; k++) {
            float sx = corners[k][0];
            float sy = corners[k][1];
            Vtx v = {};
            v.v.ob[0] = static_cast<s16>(std::lround(c.x + right.x * sx * halfW + up.x * sy * halfH));
            v.v.ob[1] = static_cast<s16>(std::lround(c.y + right.y * sx * halfW + up.y * sy * halfH));
            v.v.ob[2] = static_cast<s16>(std::lround(c.z + right.z * sx * halfW + up.z * sy * halfH));
            v.v.tc[0] = static_cast<s16>(sx < 0.0f ? 0 : img.w * 32);
            v.v.tc[1] = static_cast<s16>(k < 2 ? 0 : rows * 32);
            v.v.cn[0] = v.v.cn[1] = v.v.cn[2] = 255;
            v.v.cn[3] = alpha;
            verts.push_back(v);
        }
        Gfx g[16];
        Gfx* p = g;
        gDPPipeSync(p++);
        gDPLoadTextureBlock(p++, &img.px[static_cast<size_t>(top * img.w)], G_IM_FMT_RGBA, G_IM_SIZ_32b, img.w, rows,
                            0, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
        gSPVertex(p++, reinterpret_cast<uintptr_t>(&verts[base]), 4, 0);
        gSP2Triangles(p++, 0, 1, 2, 0, 0, 2, 3, 0);
        dl.insert(dl.end(), g, p);
    }
}

static constexpr float kPriceNear = 150.0f; // the price shows inside this distance
static constexpr float kPriceFade = 40.0f;

static void DrawWares(PlayState* play, const Vec3f& at, const ArmoryWares& wares, const Wall& wall, int slot);

void Chalk_Draw(PlayState* play) {
    if (play == nullptr) {
        return;
    }
    Vec3f at;
    ArmoryWares wares;
    if (Stations_ArmoryWares(&at, &wares)) {
        if (!sWall.found || sWall.scene != play->sceneNum || Math_Vec3f_DistXYZ(&sWall.from, &at) > 1.0f) {
            FindWall(play, at, sWall);
        }
        DrawWares(play, at, wares, sWall, 0);
    }
    // An imported map's own wall buys, each chalked on its spot.
    int count = std::min(Stations_WallBuyCount(), 15);
    sWallBuyWalls.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        if (!Stations_WallBuyWares(i, &at, &wares)) {
            continue;
        }
        Wall& wall = sWallBuyWalls[i];
        if (!wall.found || wall.scene != play->sceneNum || Math_Vec3f_DistXYZ(&wall.from, &at) > 1.0f) {
            FindWall(play, at, wall, true);
        }
        DrawWares(play, at, wares, wall, i + 1);
    }
}

static void DrawWares(PlayState* play, const Vec3f& at, const ArmoryWares& wares, const Wall& wall, int slot) {
    const Drawing* drawing = nullptr;
    if (wares.gun != 0) {
        drawing = GunDrawing(static_cast<GunId>(wares.gun));
        if (drawing == nullptr) {
            // No imported model: the OoT stand-in Link holds for a gun.
            drawing = ItemDrawing(LINK_IS_ADULT ? GID_HOOKSHOT : GID_SLINGSHOT);
        }
    } else {
        int gid = ItemGid(wares.item, wares.ammo);
        drawing = gid >= 0 ? ItemDrawing(gid) : nullptr;
    }

    // The quad's frame: across the wall and up it (or along the ground, away from it).
    // Read facing the wall (or, on the ground, from the start's side).
    const Vec3f& n = wall.normal;
    Vec3f right = { n.z, 0.0f, -n.x };
    Vec3f up = wall.floor ? Vec3f{ -n.x, 0.0f, -n.z } : Vec3f{ 0.0f, 1.0f, 0.0f };

    // Per-frame buffers, kept for the frame the list is drawn in: one set per drawing.
    static std::vector<Gfx> sDl[16][3];
    static std::vector<Vtx> sVerts[16][3];
    static int sFlip[16] = {};
    sFlip[slot] = (sFlip[slot] + 1) % 3;
    std::vector<Gfx>& dl = sDl[slot][sFlip[slot]];
    std::vector<Vtx>& verts = sVerts[slot][sFlip[slot]];
    dl.clear();
    verts.clear();
    dl.reserve(64);
    verts.reserve(32); // never reallocates: the list points into it (at most 5 bands)

    Gfx g[16];
    Gfx* p = g;
    gDPPipeSync(p++);
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetRenderMode(p++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    gSPClearGeometryMode(p++, G_LIGHTING | G_FOG | G_CULL_BOTH | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gDPSetTextureFilter(p++, G_TF_BILERP);
    gDPSetTexturePersp(p++, G_TP_PERSP);
    gDPSetAlphaCompare(p++, G_AC_NONE);
    gDPSetCombineLERP(p++, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, TEXEL0, 0, SHADE, 0);
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    dl.insert(dl.end(), g, p);

    Vec3f origin = wall.centre;
    Vec3f zero = { 0.0f, 0.0f, 0.0f };
    float priceY = -16.0f;
    if (drawing != nullptr) {
        float halfW = drawing->width * 0.5f;
        float halfH = halfW * static_cast<float>(drawing->image.h) / static_cast<float>(drawing->image.w);
        Quad(dl, verts, drawing->image, zero, right, up, halfW, halfH, 235);
        priceY = -halfH - 4.0f;
    }

    Player* player = GET_PLAYER(play);
    Vec3f from = at;
    float dist = player != nullptr ? Math_Vec3f_DistXZ(&player->actor.world.pos, &from) : 1e9f;
    float shown = std::clamp((kPriceNear - dist) / kPriceFade, 0.0f, 1.0f);
    if (shown > 0.0f && wares.cost > 0) {
        const ChalkImage& price = PriceImage(wares.cost);
        Vec3f c = { up.x * priceY, up.y * priceY, up.z * priceY };
        Quad(dl, verts, price, c, right, up, 18.0f, 4.5f, static_cast<u8>(shown * 255.0f));
    }

    Gfx end;
    Gfx* e = &end;
    gSPEndDisplayList(e++);
    dl.push_back(end);
    ZombiesFx_Submit(play, dl.data(), &origin, 1.0f);
}

} // namespace Zombies
