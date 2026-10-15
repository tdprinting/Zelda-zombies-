// bomap: reads a Black Ops map fastfile from your own install, for Zombies of Time.
//
//   bomap info   <map.ff>               header, zone size, script strings and asset counts
//   bomap ents   <map.ff>               the map's raw entities
//   bomap layout <map.ff> [out.json]    windows, wall buys, doors, chest and perks in OoT coordinates
//   bomap list   <zone.ff> [--all]      every asset's type and name, in load order (--all adds nested ones)
//   bomap model  <zone.ff> <name> <out.obj|out.json>   LOD 0 mesh; JSON adds bones, weights, materials
//   bomap anim   <zone.ff> <name> <out.json>           keyframes per bone, frame count, rate, looping
//   bomap materials <zone.ff> <model|material>         textures each material needs, and where they live
//   bomap image  <zone.ff> <name> <out.png>            an image whose pixels are in the zone
//   bomap sounds <zone.ff>                             every alias in the zone's sound banks
//   bomap weapons <zone.ff>                            each weapon's models and animations
//   bomap gamemodel <zone> <xmodel|*N> <out.zzmodel> [--main <dir>] [--attach <xmodel>
//                   [--attach-zone <zone>] [--attach-tag <bone>]]   the game's model file
//                   (*N is one of the map's brush models, such as a barricade board;
//                   --attach merges a head onto a body, or a gun onto view hands at tag_weapon)
//   bomap gameanim <zone> <xanim> <out.zzanim> [--model <zone> <xmodel>]... [--attach-tag <bone>]
//                   the game's animation file; the models give the bind pose
//   bomap mapgeo <zone> <out.zzmap> [--main <dir>] [--scale s] [--large] [--no-props]  the game's map file
//
// <zone> is a fastfile (.ff) or an already inflated zone. --main is the install's main/
// folder, whose .iwd archives hold most textures.
//
// Nothing is uploaded or saved anywhere except the file you name.

#include "asset_export.h"
#include "entities.h"
#include "game_export.h"
#include "fastfile.h"
#include "zone_walker.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
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

static int ListAssets(const Zone& zone, bool nested) {
    WalkResult walk = WalkZone(zone);
    for (const WalkedAsset& a : walk.assets) {
        if (!a.listed && !nested) {
            continue;
        }
        printf("%-10zu %s%-16s %s%s\n", a.start, a.listed ? "" : "  ", AssetTypeName(a.type), a.name.c_str(),
               a.header == kNoData ? "  (reference)" : "");
    }
    size_t listed = 0;
    for (const WalkedAsset& a : walk.assets) {
        listed += a.listed ? 1 : 0;
    }
    if (!walk.complete) {
        fprintf(stderr, "stopped at asset %d of %zu (%s), file offset %zu: %s\n", walk.stoppedAsset, zone.assets.size(),
                walk.stoppedAsset >= 0 ? AssetTypeName(zone.assets[walk.stoppedAsset].type) : "?", walk.stoppedAt,
                walk.error.c_str());
        return 1;
    }
    fprintf(stderr, "walked all %zu assets, ending at %zu of %zu bytes\n", listed, walk.stoppedAt, zone.data.size());
    return 0;
}

static bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

static int GameCommand(const std::string& command, const Zone& zone, int argc, char** argv) {
    std::vector<std::string> args(argv + 3, argv + argc);
    std::string mainDir, attachName, attachZone, attachTag;
    std::vector<std::pair<std::string, std::string>> bindModels; // zone, xmodel
    MapGeoOptions mapOptions;
    std::vector<std::string> positional;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--main" && i + 1 < args.size()) {
            mainDir = args[++i];
        } else if (args[i] == "--model" && i + 2 < args.size()) {
            std::string z = args[++i];
            bindModels.emplace_back(z, args[++i]);
        } else if (args[i] == "--attach" && i + 1 < args.size()) {
            attachName = args[++i];
        } else if (args[i] == "--attach-zone" && i + 1 < args.size()) {
            attachZone = args[++i];
        } else if (args[i] == "--attach-tag" && i + 1 < args.size()) {
            attachTag = args[++i];
        } else if (args[i] == "--scale" && i + 1 < args.size()) {
            mapOptions.scale = std::stof(args[++i]);
        } else if (args[i] == "--large") {
            mapOptions.maxTexture = 128;
        } else if (args[i] == "--no-props") {
            mapOptions.staticModels = false;
        } else {
            positional.push_back(args[i]);
        }
    }
    WalkResult walk = WalkZone(zone);
    if (!walk.complete) {
        fprintf(stderr, "warning: the zone walk stopped early (%s); later assets are missing\n", walk.error.c_str());
    }
    if (command == "mapgeo") {
        if (positional.size() != 1) {
            fprintf(stderr, "usage: bomap mapgeo <zone> <out.zzmap> [--main <dir>] [--scale s] [--large] [--no-props]\n");
            return 2;
        }
        mapOptions.mainDir = mainDir;
        MapGeoInfo info;
        std::vector<uint8_t> bytes = WriteMapGeo(zone, walk, mapOptions, &info);
        if (!WriteFile(positional[0], bytes)) {
            fprintf(stderr, "can't write %s\n", positional[0].c_str());
            return 1;
        }
        printf("wrote %s (%zu bytes)\n", positional[0].c_str(), bytes.size());
        printf("collision  %zu verts, %zu tris (%zu wall-only); %zu world brushes -> %zu tris, %zu terrain tris, %zu "
               "brushes left out\n",
               info.colVerts, info.colTris, info.wallOnlyTris, info.worldBrushes, info.brushTris, info.terrainTris,
               info.skippedBrushes);
        if (info.colWeld > 0) {
            printf("           welded on a %d unit grid to fit OoT's 32767 collision triangles\n", info.colWeld);
        }
        printf("visible    %zu meshes, %zu verts, %zu tris, %zu static models\n", info.meshes, info.verts, info.tris,
               info.staticModels);
        printf("materials  %zu, images %zu (%zu colour maps not found%s)\n", info.materials, info.images,
               info.missingImages, mainDir.empty() ? ": pass --main <install>/main for .iwd textures" : "");
        printf("bounds     (%.0f %.0f %.0f) .. (%.0f %.0f %.0f), spawn (%.0f %.0f %.0f)\n", info.boundsMin[0],
               info.boundsMin[1], info.boundsMin[2], info.boundsMax[0], info.boundsMax[1], info.boundsMax[2],
               info.spawn[0], info.spawn[1], info.spawn[2]);
        return 0;
    }
    if (positional.size() != 2) {
        fprintf(stderr, "usage: bomap %s <zone> <name> <out>%s\n", command.c_str(),
                command == "gamemodel" ? " [--main <dir>] [--attach <xmodel> [--attach-zone <zone>] [--attach-tag <bone>]]"
                                       : " [--model <zone> <xmodel>]... [--attach-tag <bone>]");
        return 2;
    }
    const std::string& name = positional[0];
    const std::string& out = positional[1];
    if (command == "gamemodel" && name.size() > 1 && name[0] == '*') {
        // A brush model of the map, such as a barricade board.
        Model brush = ReadBrushModel(zone, walk, atoi(name.c_str() + 1));
        TextureSource textures(zone, walk, mainDir);
        GameModelInfo info;
        std::vector<uint8_t> bytes = WriteGameModel(brush, walk, &textures, 128, &info);
        if (!WriteFile(out, bytes)) {
            fprintf(stderr, "can't write %s\n", out.c_str());
            return 1;
        }
        printf("wrote %s: %zu surfaces, %zu verts, %zu tris, %zu images\n", out.c_str(), info.surfaces, info.verts,
               info.tris, info.images);
        return 0;
    }
    if (command == "gamemodel") {
        int index = walk.Find(0x05, name);
        if (index < 0) {
            fprintf(stderr, "no xmodel named %s\n", name.c_str());
            return 1;
        }
        Model model = ReadModel(zone, walk, index);
        // The part to attach may live in another zone (a gun on the shared view hands).
        std::optional<Zone> otherZone;
        std::optional<WalkResult> otherWalk;
        std::optional<TextureSource> otherTextures;
        if (!attachName.empty()) {
            const Zone* az = &zone;
            const WalkResult* aw = &walk;
            if (!attachZone.empty()) {
                otherZone = ReadFastfile(attachZone);
                otherWalk = WalkZone(*otherZone);
                otherTextures.emplace(*otherZone, *otherWalk, mainDir);
                az = &*otherZone;
                aw = &*otherWalk;
            }
            int ai = aw->Find(0x05, attachName);
            if (ai < 0) {
                fprintf(stderr, "no xmodel named %s to attach\n", attachName.c_str());
                return 1;
            }
            model = AttachModel(model, ReadModel(*az, *aw, ai), attachTag);
        }
        TextureSource textures(zone, walk, mainDir);
        GameModelInfo info;
        std::vector<uint8_t> bytes = WriteGameModel(model, walk, &textures, 128, &info, otherWalk ? &*otherWalk : nullptr,
                                                    otherTextures ? &*otherTextures : nullptr);
        if (!WriteFile(out, bytes)) {
            fprintf(stderr, "can't write %s\n", out.c_str());
            return 1;
        }
        printf("wrote %s: %zu bones, %zu surfaces, %zu verts, %zu tris, %zu images\n", out.c_str(), info.bones,
               info.surfaces, info.verts, info.tris, info.images);
        for (const std::string& m : info.missingImages) {
            printf("  no pixels for %s%s\n", m.c_str(), mainDir.empty() ? " (pass --main <install>/main)" : "");
        }
        return 0;
    }
    int index = walk.Find(0x04, name);
    if (index < 0) {
        fprintf(stderr, "no xanim named %s\n", name.c_str());
        return 1;
    }
    Anim anim = ReadAnim(zone, walk, index);
    std::optional<Model> bind;
    // Each --model after the first is attached to the first (at --attach-tag), as a
    // view model's gun hangs off the view hands.
    for (const auto& [modelZone, modelName] : bindModels) {
        Zone mz = ReadFastfile(modelZone);
        WalkResult mw = WalkZone(mz);
        int mi = mw.Find(0x05, modelName);
        if (mi < 0) {
            fprintf(stderr, "no xmodel named %s in %s\n", modelName.c_str(), modelZone.c_str());
            return 1;
        }
        Model m = ReadModel(mz, mw, mi);
        bind = bind ? AttachModel(*bind, m, attachTag) : m;
    }
    if (!bind) {
        fprintf(stderr, "note: no --model given, so only rotations are written (Black Ops stores bone "
                        "translations relative to the model's bind pose)\n");
    }
    GameAnimInfo info;
    std::vector<uint8_t> bytes = WriteGameAnim(anim, bind ? &*bind : nullptr, &info);
    if (!WriteFile(out, bytes)) {
        fprintf(stderr, "can't write %s\n", out.c_str());
        return 1;
    }
    printf("wrote %s: %zu bones (%zu with positions), %d frames, %.2f s, %s, moves %.1f units (%.1f/s)\n", out.c_str(),
           info.bones, info.bonesWithPos, info.frames, info.seconds, anim.looping ? "looping" : "once",
           info.rootDistance, info.seconds > 0 ? info.rootDistance / info.seconds : 0.0f);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: bomap info|ents|layout|list|model|anim|materials|image|sounds <zone.ff> ...\n"
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
        if (command == "model" || command == "anim" || command == "image" || command == "materials") {
            if (argc < (command == "materials" ? 4 : 5)) {
                fprintf(stderr, "usage: bomap %s <zone.ff> <name>%s\n", command.c_str(), command == "materials" ? "" : " <out>");
                return 2;
            }
            WalkResult walk = WalkZone(zone);
            std::string name = argv[3];
            uint32_t type = command == "model" ? 0x05 : command == "anim" ? 0x04 : command == "image" ? 0x08 : 0x05;
            int index = walk.Find(type, name);
            if (index < 0 && command == "materials") {
                index = walk.Find(0x06, name);
            }
            if (index < 0) {
                fprintf(stderr, "no %s named %s in %s%s\n", command == "materials" ? "model or material" : AssetTypeName(type),
                        name.c_str(), path.c_str(), walk.complete ? "" : (" (walk stopped early: " + walk.error + ")").c_str());
                return 1;
            }
            if (command == "materials") {
                std::string report = MaterialReport(zone, walk, index);
                fwrite(report.data(), 1, report.size(), stdout);
                return 0;
            }
            std::string out = argv[4];
            std::ofstream file(out, std::ios::binary);
            if (command == "image") {
                std::vector<uint8_t> png = ImageToPng(zone, walk, index);
                file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
            } else if (command == "anim") {
                file << AnimToJson(zone, walk, index);
            } else {
                bool obj = out.size() >= 4 && out.substr(out.size() - 4) == ".obj";
                file << (obj ? ModelToObj(zone, walk, index) : ModelToJson(zone, walk, index));
            }
            printf("wrote %s\n", out.c_str());
            return 0;
        }
        if (command == "gamemodel" || command == "gameanim" || command == "mapgeo") {
            return GameCommand(command, zone, argc, argv);
        }
        if (command == "weapons") {
            std::string report = WeaponReport(zone, WalkZone(zone));
            fwrite(report.data(), 1, report.size(), stdout);
            return 0;
        }
        if (command == "sounds") {
            std::string report = SoundReport(zone, WalkZone(zone));
            fwrite(report.data(), 1, report.size(), stdout);
            return 0;
        }
        if (command == "list") {
            return ListAssets(zone, argc >= 4 && std::string(argv[3]) == "--all");
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
