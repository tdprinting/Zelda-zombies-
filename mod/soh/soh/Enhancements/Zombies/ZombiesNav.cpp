// Zombies of Time: the undead finding their way through a Black Ops map.
//
// Outside, "Horde Pull" walks each undead straight at the nearest player, which is all an
// open scene needs. A Black Ops map is a building: rooms, doors, two staircases and an
// upper floor, so a straight line runs into walls, and an undead under a player standing
// upstairs would wait there forever. Black Ops zombies follow the map's path nodes. Ours
// follow a walk grid made from the map's own collision when the run starts:
//
// - The map is cut into 20-unit columns. Down each column, every floor with room above it
//   for an undead to stand is a node.
// - Two side-by-side nodes are joined when their floors are within a step of each other
//   (stairs climb) and no wall stands between them. Diagonals are joined only when both
//   sides around the corner are, so nothing cuts a corner.
// - A few times a second the nodes are scored with their walking distance to the nearest
//   player (Dijkstra from every player standing). Closed debris and doors block their nodes.
// - Each undead heads for the node a few steps down that slope from where it stands.
//
// Nothing about the map is written here: the grid comes from whatever collision is loaded.

#include "ZombiesMode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
}

namespace Zombies {

static constexpr float kCell = 20.0f;
static constexpr float kStep = 28.0f;      // floors closer than this in height are one walk apart
static constexpr float kHeadroom = 60.0f;  // floor above a floor: at least this, or nobody fits
static constexpr float kWallProbe = 22.0f; // walls are checked this far above the higher floor
static constexpr int kMaxFloors = 4;       // floors down one column
static constexpr int kRescoreFrames = 6;
static constexpr int kLookAhead = 4; // nodes down the slope an undead aims for
static constexpr float kUnreached = 1e30f;

struct NavNode {
    float y;
    s16 ix;
    s16 iz;
    s32 next[8]; // neighbour node in each direction, -1 for none
};

struct NavGrid {
    bool ready = false;
    const CollisionHeader* builtFor = nullptr;
    float x0 = 0.0f;
    float z0 = 0.0f;
    int nx = 0;
    int nz = 0;
    std::vector<s32> columnStart; // nx * nz + 1 offsets into nodes
    std::vector<NavNode> nodes;
    std::vector<float> dist;
    std::vector<u8> blocked;
    int sinceScore = kRescoreFrames;
};

static NavGrid sNav;

// The eight directions: four sides first, then the diagonals between them.
static constexpr int kDx[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };
static constexpr int kDz[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
static constexpr float kLen[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.41421f, 1.41421f, 1.41421f, 1.41421f };

static int Opposite(int dir) {
    return dir < 4 ? (dir + 2) % 4 : 4 + (dir - 4 + 2) % 4;
}

static float NodeX(const NavNode& n) {
    return sNav.x0 + (static_cast<float>(n.ix) + 0.5f) * kCell;
}

static float NodeZ(const NavNode& n) {
    return sNav.z0 + (static_cast<float>(n.iz) + 0.5f) * kCell;
}

void Nav_Clear() {
    sNav = NavGrid();
}

bool Nav_Ready() {
    const ArenaLayout* layout = GetRun().layout;
    return sNav.ready && layout != nullptr && layout->blackOps;
}

void Nav_Stats(int* nodes, int* links, int* reached) {
    *nodes = static_cast<int>(sNav.nodes.size());
    *links = 0;
    *reached = 0;
    for (const NavNode& node : sNav.nodes) {
        for (s32 m : node.next) {
            *links += m >= 0 ? 1 : 0;
        }
    }
    for (float d : sNav.dist) {
        *reached += d < kUnreached ? 1 : 0;
    }
}

// The box the grid covers: every spot of the layout, with room around for the ground
// outside the windows.
static bool LayoutBounds(const ArenaLayout& layout, float lo[3], float hi[3]) {
    bool any = false;
    auto add = [&](const LayoutSpot& s) {
        float y = s.y > LayoutSpot::kNoHeight ? s.y : GetRun().origin.y;
        float p[3] = { s.x, y, s.z };
        for (int a = 0; a < 3; a++) {
            lo[a] = any ? std::min(lo[a], p[a]) : p[a];
            hi[a] = any ? std::max(hi[a], p[a]) : p[a];
        }
        any = true;
    };
    for (const LayoutZone& zone : layout.zones) {
        add(zone.centre);
        add(zone.debris);
        for (const LayoutSpot& s : zone.windows) {
            add(s);
        }
        for (const LayoutStation& s : zone.stations) {
            add(s.spot);
        }
        for (const LayoutSpot& s : zone.chestSpots) {
            add(s);
        }
        for (const LayoutWallBuy& w : zone.wallBuys) {
            add(w.spot);
        }
        for (const LayoutSpot& s : zone.moreDebris) {
            add(s);
        }
    }
    return any;
}

static bool WallBetween(PlayState* play, const NavNode& a, const NavNode& b) {
    float y = std::max(a.y, b.y) + kWallProbe;
    Vec3f pa = { NodeX(a), y, NodeZ(a) };
    Vec3f pb = { NodeX(b), y, NodeZ(b) };
    Vec3f hit;
    CollisionPoly* poly = nullptr;
    s32 bgId = 0;
    return BgCheck_EntityLineTest1(&play->colCtx, &pa, &pb, &hit, &poly, true, false, false, true, &bgId) != 0;
}

// The node in a column on the floor nearest y, within reach. -1 if none.
static int NodeIn(int ix, int iz, float y, float reach) {
    if (ix < 0 || iz < 0 || ix >= sNav.nx || iz >= sNav.nz) {
        return -1;
    }
    int col = iz * sNav.nx + ix;
    int best = -1;
    float bestDy = reach;
    for (int n = sNav.columnStart[col]; n < sNav.columnStart[col + 1]; n++) {
        float dy = fabsf(sNav.nodes[n].y - y);
        if (dy < bestDy) {
            bestDy = dy;
            best = n;
        }
    }
    return best;
}

void Nav_Build(PlayState* play) {
    const Run& run = GetRun();
    if (play == nullptr || run.layout == nullptr || !run.layout->blackOps) {
        Nav_Clear();
        return;
    }
    if (sNav.ready && sNav.builtFor == play->colCtx.colHeader) {
        std::fill(sNav.dist.begin(), sNav.dist.end(), kUnreached);
        sNav.sinceScore = kRescoreFrames;
        return; // the same map as last run: the grid still fits
    }
    Nav_Clear();
    float lo[3], hi[3];
    if (!LayoutBounds(*run.layout, lo, hi)) {
        return;
    }
    constexpr float kMargin = 220.0f;
    sNav.x0 = lo[0] - kMargin;
    sNav.z0 = lo[2] - kMargin;
    sNav.nx = static_cast<int>((hi[0] - lo[0] + 2.0f * kMargin) / kCell) + 1;
    sNav.nz = static_cast<int>((hi[2] - lo[2] + 2.0f * kMargin) / kCell) + 1;
    if (sNav.nx <= 0 || sNav.nz <= 0 || sNav.nx > 2000 || sNav.nz > 2000) {
        return;
    }
    float top = hi[1] + 200.0f;
    float bottom = lo[1] - 200.0f;

    // Floors down each column.
    sNav.columnStart.assign(static_cast<size_t>(sNav.nx) * sNav.nz + 1, 0);
    for (int iz = 0; iz < sNav.nz; iz++) {
        for (int ix = 0; ix < sNav.nx; ix++) {
            int col = iz * sNav.nx + ix;
            sNav.columnStart[col] = static_cast<s32>(sNav.nodes.size());
            float x = sNav.x0 + (static_cast<float>(ix) + 0.5f) * kCell;
            float z = sNav.z0 + (static_cast<float>(iz) + 0.5f) * kCell;
            float from = top;
            float above = 1e9f; // the floor over this one
            for (int k = 0; k < kMaxFloors; k++) {
                Vec3f probe = { x, from, z };
                CollisionPoly* poly = nullptr;
                s32 bgId = 0;
                float y = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
                if (y <= BGCHECK_Y_MIN + 1.0f || y < bottom) {
                    break;
                }
                if (above - y >= kHeadroom && poly != nullptr && COLPOLY_GET_NORMAL(poly->normal.y) > 0.6f) {
                    NavNode node = {};
                    node.y = y;
                    node.ix = static_cast<s16>(ix);
                    node.iz = static_cast<s16>(iz);
                    std::fill(std::begin(node.next), std::end(node.next), -1);
                    sNav.nodes.push_back(node);
                }
                above = y;
                from = y - 10.0f;
            }
        }
    }
    sNav.columnStart.back() = static_cast<s32>(sNav.nodes.size());

    // Sides: each pair once, from the node with the lower column.
    for (size_t i = 0; i < sNav.nodes.size(); i++) {
        NavNode& a = sNav.nodes[i];
        for (int dir = 0; dir < 2; dir++) {
            int j = NodeIn(a.ix + kDx[dir], a.iz + kDz[dir], a.y, kStep);
            if (j < 0 || WallBetween(play, a, sNav.nodes[j])) {
                continue;
            }
            a.next[dir] = j;
            sNav.nodes[j].next[Opposite(dir)] = static_cast<s32>(i);
        }
    }
    // Diagonals: where both ways round the corner are open.
    for (size_t i = 0; i < sNav.nodes.size(); i++) {
        NavNode& a = sNav.nodes[i];
        for (int dir = 4; dir < 8; dir++) {
            int sideX = kDx[dir] > 0 ? 0 : 2;
            int sideZ = kDz[dir] > 0 ? 1 : 3;
            int bx = a.next[sideX];
            int bz = a.next[sideZ];
            if (bx < 0 || bz < 0) {
                continue;
            }
            int c1 = sNav.nodes[bx].next[sideZ];
            int c2 = sNav.nodes[bz].next[sideX];
            if (c1 >= 0 && c1 == c2) {
                a.next[dir] = c1;
            }
        }
    }
    sNav.dist.assign(sNav.nodes.size(), kUnreached);
    sNav.blocked.assign(sNav.nodes.size(), 0);
    sNav.builtFor = play->colCtx.colHeader;
    sNav.ready = !sNav.nodes.empty();
    sNav.sinceScore = kRescoreFrames;
}

static int NodeAt(const Vec3f& pos, float reach) {
    int ix = static_cast<int>(floorf((pos.x - sNav.x0) / kCell));
    int iz = static_cast<int>(floorf((pos.z - sNav.z0) / kCell));
    int n = NodeIn(ix, iz, pos.y, reach);
    if (n >= 0) {
        return n;
    }
    // Just off the grid's nodes (against a wall, on a window sill): the nearest around.
    float best = 1e9f;
    for (int dz = -2; dz <= 2; dz++) {
        for (int dx = -2; dx <= 2; dx++) {
            int m = NodeIn(ix + dx, iz + dz, pos.y, reach);
            if (m >= 0) {
                float d = static_cast<float>(dx * dx + dz * dz);
                if (d < best) {
                    best = d;
                    n = m;
                }
            }
        }
    }
    return n;
}

// Scores every node with its walking distance to the nearest player.
static void Score(PlayState* play) {
    const Run& run = GetRun();
    std::fill(sNav.blocked.begin(), sNav.blocked.end(), 0);
    for (const Debris& d : run.debris) {
        if (d.radius <= 0.0f) {
            continue;
        }
        int r = static_cast<int>(d.radius / kCell) + 1;
        int cx = static_cast<int>(floorf((d.pos.x - sNav.x0) / kCell));
        int cz = static_cast<int>(floorf((d.pos.z - sNav.z0) / kCell));
        for (int iz = cz - r; iz <= cz + r; iz++) {
            for (int ix = cx - r; ix <= cx + r; ix++) {
                if (ix < 0 || iz < 0 || ix >= sNav.nx || iz >= sNav.nz) {
                    continue;
                }
                int col = iz * sNav.nx + ix;
                for (int n = sNav.columnStart[col]; n < sNav.columnStart[col + 1]; n++) {
                    const NavNode& node = sNav.nodes[n];
                    float dx = NodeX(node) - d.pos.x;
                    float dz = NodeZ(node) - d.pos.z;
                    if (dx * dx + dz * dz < d.radius * d.radius && node.y > d.pos.y - 70.0f &&
                        node.y < d.pos.y + 110.0f) {
                        sNav.blocked[n] = 1;
                    }
                }
            }
        }
    }

    std::fill(sNav.dist.begin(), sNav.dist.end(), kUnreached);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    auto seed = [&](const Vec3f& pos) {
        int n = NodeAt(pos, 60.0f);
        if (n >= 0) {
            sNav.dist[n] = 0.0f;
            open.push({ 0.0f, n });
        }
    };
    Player* player = GET_PLAYER(play);
    if (player != nullptr && !run.linkDown.downed) {
        seed(player->actor.world.pos);
    }
    const Partner& partner = run.partner;
    if (partner.actor != nullptr && !partner.downed && !partner.bledOut) {
        seed(partner.actor->world.pos);
    }
    if (open.empty() && player != nullptr) {
        seed(player->actor.world.pos);
    }
    while (!open.empty()) {
        Item item = open.top();
        open.pop();
        int n = item.second;
        if (item.first > sNav.dist[n]) {
            continue;
        }
        const NavNode& node = sNav.nodes[n];
        for (int dir = 0; dir < 8; dir++) {
            int m = node.next[dir];
            if (m < 0 || sNav.blocked[m]) {
                continue;
            }
            float d = item.first + kLen[dir] + fabsf(sNav.nodes[m].y - node.y) * (1.0f / kCell);
            if (d < sNav.dist[m]) {
                sNav.dist[m] = d;
                open.push({ d, m });
            }
        }
    }
}

void Nav_Update(PlayState* play) {
    if (!Nav_Ready() || play == nullptr) {
        return;
    }
    if (++sNav.sinceScore >= kRescoreFrames) {
        sNav.sinceScore = 0;
        Score(play);
    }
}

bool Nav_Steer(PlayState* play, const Vec3f& from, Vec3f* toward, float* pathDist, int lookAhead) {
    if (!Nav_Ready()) {
        return false;
    }
    int n = NodeAt(from, 45.0f);
    if (n < 0 || sNav.dist[n] >= kUnreached) {
        return false;
    }
    *pathDist = sNav.dist[n] * kCell;
    // A few nodes down the slope, so the walk is smooth rather than cell by cell, but never
    // past one it can't see: a straight line to it would cut a corner into the wall.
    int at = n;
    lookAhead = std::clamp(lookAhead, 1, kLookAhead);
    for (int step = 0; step < lookAhead; step++) {
        int best = -1;
        float bestDist = sNav.dist[at];
        const NavNode& node = sNav.nodes[at];
        for (int dir = 0; dir < 8; dir++) {
            int m = node.next[dir];
            if (m >= 0 && !sNav.blocked[m] && sNav.dist[m] < bestDist) {
                bestDist = sNav.dist[m];
                best = m;
            }
        }
        if (best < 0) {
            break;
        }
        if (step > 0 && play != nullptr) {
            const NavNode& next = sNav.nodes[best];
            Vec3f a = { from.x, std::max(from.y, next.y) + kWallProbe, from.z };
            Vec3f b = { NodeX(next), a.y, NodeZ(next) };
            Vec3f hit;
            CollisionPoly* poly = nullptr;
            s32 bgId = 0;
            if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, false, false, true, &bgId)) {
                break;
            }
        }
        at = best;
    }
    const NavNode& goal = sNav.nodes[at];
    *toward = { NodeX(goal), goal.y, NodeZ(goal) };
    return true;
}

} // namespace Zombies
