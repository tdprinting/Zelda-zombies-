// bomap: reads a Black Ops map fastfile from your own install, for Zombies of Time.
//
//   bomap info   <map.ff>               header, zone size, script strings and asset counts
//   bomap ents   <map.ff>               the map's raw entities
//   bomap layout <map.ff> [out.json]    windows, wall buys, doors, chest and perks in OoT coordinates
//
// Nothing is uploaded or saved anywhere except the file you name.

#include "entities.h"
#include "fastfile.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace bomap;

static constexpr float kScale = 1.0f;

static std::string MapName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

static void PrintInfo(const Zone& zone) {
    printf("magic          %s\n", zone.magic.c_str());
    printf("version        %u\n", zone.version);
    printf("zone size      %u bytes (inflated %zu)\n", zone.size, zone.data.size());
    printf("external size  %u bytes\n", zone.externalSize);
    printf("script strings %zu\n", zone.scriptStrings.size());
    for (size_t i = 0; i < zone.scriptStrings.size() && i < 10; i++) {
        printf("  %s\n", zone.scriptStrings[i].c_str());
    }
    printf("assets         %zu\n", zone.assets.size());
    std::map<uint32_t, int> counts;
    for (const ZoneAsset& a : zone.assets) {
        counts[a.type]++;
    }
    for (const auto& [type, count] : counts) {
        printf("  %-16s 0x%02X  %d\n", AssetTypeName(type), type, count);
    }
    printf("map entities   %s\n", FindEntityString(zone.data) ? "found" : "not found");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: bomap info|ents|layout <map.ff> [out.json]\n"
                        "Black Ops maps are in your install's zone folder (zone/Common/ or zone/<language>/),\n"
                        "for example zombie_cod5_prototype.ff for Nacht der Untoten.\n");
        return 2;
    }
    std::string command = argv[1];
    std::string path = argv[2];
    try {
        Zone zone = ReadFastfile(path);
        if (command == "info") {
            PrintInfo(zone);
            return 0;
        }
        std::optional<std::string> text = FindEntityString(zone.data);
        if (!text) {
            fprintf(stderr, "no map entities in %s (is it a map's fastfile?)\n", path.c_str());
            return 1;
        }
        if (command == "ents") {
            fwrite(text->data(), 1, text->size(), stdout);
            printf("\n");
            return 0;
        }
        if (command == "layout") {
            std::vector<LayoutItem> items = ExtractLayout(ParseEntities(*text), kScale);
            std::string json = LayoutToJson(MapName(path), items, kScale);
            if (argc >= 4) {
                std::ofstream(argv[3]) << json;
                printf("wrote %zu layout items to %s\n", items.size(), argv[3]);
            } else {
                fwrite(json.data(), 1, json.size(), stdout);
            }
            return 0;
        }
        fprintf(stderr, "unknown command %s\n", command.c_str());
        return 2;
    } catch (const std::exception& e) {
        fprintf(stderr, "bomap: %s\n", e.what());
        return 1;
    }
}
