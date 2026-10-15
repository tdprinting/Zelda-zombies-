#pragma once

// The map's entities (its MapEnts asset): the `{ "key" "value" }` text that
// places spawn points, windows, wall buys, doors, the chest and perk machines.
// Turned into a Zombies of Time layout in OoT coordinates.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bomap {

using Entity = std::map<std::string, std::string>;

// Finds the entity text in an inflated zone (it starts with the worldspawn entity)
// and returns it, or nothing if the zone has none.
std::optional<std::string> FindEntityString(const std::vector<uint8_t>& zone);

std::vector<Entity> ParseEntities(const std::string& text);

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// Black Ops is Z-up, OoT is Y-up: (x, y, z) -> (x, z, -y), times scale.
Vec3 ToOot(const Vec3& cod, float scale);

enum class LayoutKind {
    PlayerStart,
    Window,
    WallBuy,
    Door,
    Debris,
    Chest,
    Perk,
    PackAPunch,
    Power,
    Spawner,
    Zone,
};

const char* LayoutKindName(LayoutKind kind);

struct LayoutItem {
    LayoutKind kind;
    Vec3 cod;           // Black Ops position
    Vec3 oot;           // OoT position
    float yaw;          // degrees, Black Ops yaw
    std::string detail; // weapon, perk, zone name or flags
    int cost;           // doors and debris; 0 otherwise
};

std::vector<LayoutItem> ExtractLayout(const std::vector<Entity>& entities, float scale);

std::string LayoutToJson(const std::string& mapName, const std::vector<LayoutItem>& items, float scale);

} // namespace bomap
