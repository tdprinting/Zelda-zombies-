#include "entities.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace bomap {

std::optional<std::string> FindEntityString(const std::vector<uint8_t>& zone) {
    // Every map's entity text opens with the worldspawn entity.
    static const char kNeedle[] = "\"classname\" \"worldspawn\"";
    const size_t needleLen = sizeof(kNeedle) - 1;
    for (size_t i = 0; i + needleLen <= zone.size(); i++) {
        if (memcmp(&zone[i], kNeedle, needleLen) != 0) {
            continue;
        }
        size_t start = i;
        while (start > 0 && zone[start] != '{') {
            start--;
        }
        size_t end = i;
        while (end < zone.size() && zone[end] != 0) {
            end++;
        }
        return std::string(reinterpret_cast<const char*>(&zone[start]), end - start);
    }
    return std::nullopt;
}

std::vector<Entity> ParseEntities(const std::string& text) {
    std::vector<Entity> entities;
    size_t pos = 0;
    auto readQuoted = [&](std::string* out) {
        size_t open = text.find('"', pos);
        if (open == std::string::npos) {
            return false;
        }
        size_t close = text.find('"', open + 1);
        if (close == std::string::npos) {
            return false;
        }
        *out = text.substr(open + 1, close - open - 1);
        pos = close + 1;
        return true;
    };
    while (true) {
        size_t open = text.find('{', pos);
        if (open == std::string::npos) {
            break;
        }
        size_t close = text.find('}', open);
        if (close == std::string::npos) {
            break;
        }
        Entity entity;
        pos = open + 1;
        while (true) {
            size_t nextQuote = text.find('"', pos);
            if (nextQuote == std::string::npos || nextQuote > close) {
                break;
            }
            std::string key, value;
            if (!readQuoted(&key) || !readQuoted(&value)) {
                break;
            }
            entity[key] = value;
        }
        entities.push_back(std::move(entity));
        pos = close + 1;
    }
    return entities;
}

Vec3 ToOot(const Vec3& cod, float scale) {
    return { cod.x * scale, cod.z * scale, -cod.y * scale };
}

const char* LayoutKindName(LayoutKind kind) {
    switch (kind) {
        case LayoutKind::PlayerStart:
            return "player_start";
        case LayoutKind::Window:
            return "window";
        case LayoutKind::WallBuy:
            return "wall_buy";
        case LayoutKind::Door:
            return "door";
        case LayoutKind::Debris:
            return "debris";
        case LayoutKind::Chest:
            return "chest";
        case LayoutKind::Perk:
            return "perk";
        case LayoutKind::PackAPunch:
            return "pack_a_punch";
        case LayoutKind::Power:
            return "power";
        case LayoutKind::Spawner:
            return "spawner";
        case LayoutKind::Zone:
            return "zone";
    }
    return "unknown";
}

static std::string Get(const Entity& e, const char* key) {
    auto it = e.find(key);
    return it == e.end() ? std::string() : it->second;
}

static bool ReadVec(const std::string& s, Vec3* out) {
    return !s.empty() && sscanf(s.c_str(), "%f %f %f", &out->x, &out->y, &out->z) == 3;
}

static std::optional<LayoutKind> Classify(const Entity& e) {
    std::string classname = Get(e, "classname");
    std::string targetname = Get(e, "targetname");
    std::string noteworthy = Get(e, "script_noteworthy");

    if (classname == "info_player_start" || targetname == "initial_spawn_points") {
        return LayoutKind::PlayerStart;
    }
    if (targetname == "exterior_goal") {
        return LayoutKind::Window;
    }
    if (targetname == "weapon_upgrade") {
        return LayoutKind::WallBuy;
    }
    if (targetname == "zombie_door") {
        return LayoutKind::Door;
    }
    if (targetname == "zombie_debris") {
        return LayoutKind::Debris;
    }
    if (targetname == "treasure_chest_use") {
        return LayoutKind::Chest;
    }
    if (targetname == "zombie_vending_upgrade" || noteworthy == "specialty_weapupgrade") {
        return LayoutKind::PackAPunch;
    }
    if (targetname == "zombie_vending") {
        return LayoutKind::Perk;
    }
    if (targetname == "use_power_switch" || targetname == "use_elec_switch" || targetname == "use_master_switch") {
        return LayoutKind::Power;
    }
    if (classname.rfind("actor_zombie", 0) == 0) {
        return LayoutKind::Spawner;
    }
    if (classname == "info_volume" && targetname.find("zone") != std::string::npos) {
        return LayoutKind::Zone;
    }
    return std::nullopt;
}

std::vector<LayoutItem> ExtractLayout(const std::vector<Entity>& entities, float scale) {
    std::vector<LayoutItem> items;
    for (const Entity& e : entities) {
        std::optional<LayoutKind> kind = Classify(e);
        if (!kind) {
            continue;
        }
        LayoutItem item = { *kind, {}, {}, 0.0f, "", 0 };
        Vec3 angles;
        if (ReadVec(Get(e, "angles"), &angles)) {
            item.yaw = angles.y;
        }
        switch (*kind) {
            case LayoutKind::WallBuy:
                item.detail = Get(e, "zombie_weapon_upgrade");
                break;
            case LayoutKind::Door:
            case LayoutKind::Debris:
                item.cost = atoi(Get(e, "zombie_cost").c_str());
                item.detail = Get(e, "script_flag");
                break;
            case LayoutKind::Perk:
            case LayoutKind::Chest:
                item.detail = Get(e, "script_noteworthy");
                break;
            case LayoutKind::Zone:
                item.detail = Get(e, "targetname");
                break;
            case LayoutKind::Spawner:
                item.detail = Get(e, "script_noteworthy");
                break;
            default:
                break;
        }
        // Brush triggers carry a model ("*12") instead of an origin; their position
        // comes from the collision's brush models in a later stage.
        if (!ReadVec(Get(e, "origin"), &item.cod)) {
            std::string model = Get(e, "model");
            item.detail += item.detail.empty() ? "" : " ";
            item.detail += model.empty() ? "(no origin)" : "(brush model " + model + ")";
        }
        item.oot = ToOot(item.cod, scale);
        items.push_back(item);
    }
    return items;
}

static std::string Escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        if (static_cast<unsigned char>(c) >= 0x20) {
            out += c;
        }
    }
    return out;
}

std::string LayoutToJson(const std::string& mapName, const std::vector<LayoutItem>& items, float scale) {
    std::ostringstream out;
    out << "{\n  \"map\": \"" << Escape(mapName) << "\",\n  \"scale\": " << scale << ",\n  \"items\": [\n";
    for (size_t i = 0; i < items.size(); i++) {
        const LayoutItem& it = items[i];
        char pos[160];
        snprintf(pos, sizeof(pos), "[%.1f, %.1f, %.1f], \"cod\": [%.1f, %.1f, %.1f], \"yaw\": %.1f", it.oot.x, it.oot.y,
                 it.oot.z, it.cod.x, it.cod.y, it.cod.z, it.yaw);
        out << "    { \"kind\": \"" << LayoutKindName(it.kind) << "\", \"pos\": " << pos;
        if (!it.detail.empty()) {
            out << ", \"detail\": \"" << Escape(it.detail) << "\"";
        }
        if (it.cost > 0) {
            out << ", \"cost\": " << it.cost;
        }
        out << " }" << (i + 1 < items.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    return out.str();
}

} // namespace bomap
