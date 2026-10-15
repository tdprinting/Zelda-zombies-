// Tests bomap against a small fastfile built here, laid out the way the decompiled
// Black Ops loader reads one. It checks the reader matches that layout; whether the
// layout matches retail files is checked by running bomap on a real install.

#include "entities.h"
#include "fastfile.h"
#include "sound_export.h"

#include <zlib.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

using namespace bomap;

static int sFailures = 0;

#define EXPECT_TRUE(cond)                                               \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            sFailures++;                                                \
        }                                                               \
    } while (0)

static void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; i++) {
        v.push_back(static_cast<uint8_t>(x >> (i * 8)));
    }
}

static void PutStr(std::vector<uint8_t>& v, const std::string& s) {
    v.insert(v.end(), s.begin(), s.end());
    v.push_back(0);
}

static const char* kEntities =
    "{\n\"classname\" \"worldspawn\"\n}\n"
    "{\n\"origin\" \"100 200 8\"\n\"angles\" \"0 90 0\"\n\"classname\" \"info_player_start\"\n}\n"
    "{\n\"origin\" \"-512 64 0\"\n\"angles\" \"0 180 0\"\n\"targetname\" \"exterior_goal\"\n\"classname\" "
    "\"script_struct\"\n}\n"
    "{\n\"model\" \"*3\"\n\"zombie_weapon_upgrade\" \"kar98k_zm\"\n\"targetname\" \"weapon_upgrade\"\n"
    "\"classname\" \"trigger_use\"\n}\n"
    "{\n\"origin\" \"40 -300 16\"\n\"zombie_cost\" \"1000\"\n\"script_flag\" \"upstairs\"\n\"targetname\" "
    "\"zombie_debris\"\n\"classname\" \"trigger_use_touch\"\n}\n"
    "{\n\"origin\" \"0 0 0\"\n\"targetname\" \"treasure_chest_use\"\n\"script_noteworthy\" \"start_chest\"\n"
    "\"classname\" \"trigger_use\"\n}\n"
    "{\n\"origin\" \"5 5 5\"\n\"classname\" \"actor_zombie_ger_ber_wehr\"\n}\n";

static std::vector<uint8_t> BuildFastfile() {
    std::vector<uint8_t> zone;
    // XFile: size, externalSize, blockSize[7]
    for (int i = 0; i < 9; i++) {
        PutU32(zone, 0);
    }
    // XAssetList: 2 script strings, 3 assets, both inline
    PutU32(zone, 2);
    PutU32(zone, 0xFFFFFFFF);
    PutU32(zone, 3);
    PutU32(zone, 0xFFFFFFFF);
    PutU32(zone, 0xFFFFFFFF);
    PutU32(zone, 0xFFFFFFFF);
    PutStr(zone, "zombie_cod5_prototype");
    PutStr(zone, "exterior_goal");
    PutU32(zone, kAssetImage);
    PutU32(zone, 0xFFFFFFFF);
    PutU32(zone, kAssetClipMap);
    PutU32(zone, 0xFFFFFFFF);
    PutU32(zone, kAssetMapEnts);
    PutU32(zone, 0xFFFFFFFF);
    // MapEnts: name and entity text, as some of the asset data.
    PutStr(zone, "maps/zombie_cod5_prototype.d3dbsp");
    PutStr(zone, kEntities);
    uint32_t size = static_cast<uint32_t>(zone.size() - 36);
    memcpy(zone.data(), &size, 4);

    std::vector<uint8_t> compressed(compressBound(static_cast<uLong>(zone.size())));
    uLongf compressedSize = static_cast<uLongf>(compressed.size());
    compress(compressed.data(), &compressedSize, zone.data(), static_cast<uLong>(zone.size()));
    compressed.resize(compressedSize);

    std::vector<uint8_t> file = { 'I', 'W', 'f', 'f', 'u', '1', '0', '0' };
    PutU32(file, kFastfileVersion);
    file.insert(file.end(), compressed.begin(), compressed.end());
    return file;
}

static bool Throws(const std::vector<uint8_t>& file) {
    try {
        ParseFastfile(file);
    } catch (const std::runtime_error&) { return true; }
    return false;
}

static void TestHeaderAndAssetList() {
    Zone zone = ParseFastfile(BuildFastfile());
    EXPECT_TRUE(zone.version == 473);
    EXPECT_TRUE(zone.scriptStrings.size() == 2);
    EXPECT_TRUE(zone.scriptStrings[1] == "exterior_goal");
    EXPECT_TRUE(zone.assets.size() == 3);
    EXPECT_TRUE(zone.assets[2].type == kAssetMapEnts);
    EXPECT_TRUE(zone.size == zone.data.size() - 36);
    EXPECT_TRUE(std::string(AssetTypeName(kAssetGfxWorld)) == "gfxworld");
}

static void TestRejectsOtherFiles() {
    std::vector<uint8_t> file = BuildFastfile();
    std::vector<uint8_t> signedFile = file;
    signedFile[4] = '0';
    EXPECT_TRUE(Throws(signedFile));
    std::vector<uint8_t> wrongVersion = file;
    wrongVersion[8] = 0x10;
    EXPECT_TRUE(Throws(wrongVersion));
    std::vector<uint8_t> truncated(file.begin(), file.begin() + 20);
    EXPECT_TRUE(Throws(truncated));
}

static void TestLayout() {
    Zone zone = ParseFastfile(BuildFastfile());
    auto text = FindEntityString(zone.data);
    EXPECT_TRUE(text.has_value());
    std::vector<Entity> ents = ParseEntities(*text);
    EXPECT_TRUE(ents.size() == 7);
    EXPECT_TRUE(ents[0].at("classname") == "worldspawn");

    std::vector<LayoutItem> items = ExtractLayout(ents, 1.0f);
    EXPECT_TRUE(items.size() == 6);
    EXPECT_TRUE(items[0].kind == LayoutKind::PlayerStart);
    // Z-up to Y-up: (100, 200, 8) -> (100, 8, -200)
    EXPECT_TRUE(items[0].oot.x == 100.0f && items[0].oot.y == 8.0f && items[0].oot.z == -200.0f);
    EXPECT_TRUE(items[1].kind == LayoutKind::Window && items[1].yaw == 180.0f);
    EXPECT_TRUE(items[2].kind == LayoutKind::WallBuy && items[2].detail.find("kar98k_zm") == 0);
    EXPECT_TRUE(items[2].detail.find("brush model *3") != std::string::npos);
    EXPECT_TRUE(items[3].kind == LayoutKind::Debris && items[3].cost == 1000 && items[3].detail == "upstairs");
    EXPECT_TRUE(items[4].kind == LayoutKind::Chest);
    EXPECT_TRUE(items[5].kind == LayoutKind::Spawner);

    std::string json = LayoutToJson("zombie_cod5_prototype", items, 1.0f);
    EXPECT_TRUE(json.find("\"kind\": \"debris\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"cost\": 1000") != std::string::npos);
}

// MS ADPCM: a block's header gives the first two samples, then each nibble predicts the
// next from the last two; Black Ops' blocks hold 512 frames in 262 bytes per channel.
static void TestSounds() {
    std::vector<uint8_t> block(262, 0);
    block[0] = 0;                      // predictor 0: next = last
    block[1] = 16;                     // delta 16
    block[3] = 100;                    // sample 1 = 100
    block[5] = 50;                     // sample 2 = 50 (played first)
    block[7] = 0x10;                   // +1 * 16, then +0
    std::vector<int16_t> pcm = DecodeMsAdpcm(block.data(), block.size(), 1, 512);
    EXPECT_TRUE(pcm.size() == 512);
    EXPECT_TRUE(pcm.size() >= 4 && pcm[0] == 50 && pcm[1] == 100 && pcm[2] == 116 && pcm[3] == 116);
    std::vector<uint8_t> wav = WavFile(pcm, 44100, 1);
    EXPECT_TRUE(wav.size() == 44 + 1024);
    EXPECT_TRUE(std::memcmp(wav.data(), "RIFF", 4) == 0 && std::memcmp(wav.data() + 8, "WAVEfmt ", 8) == 0);
    EXPECT_TRUE(!DefaultSoundRoles().empty() && DefaultSoundRoles()[0].role == "purchase");
}

int main() {
    TestHeaderAndAssetList();
    TestSounds();
    TestRejectsOtherFiles();
    TestLayout();
    if (sFailures == 0) {
        std::printf("All bomap tests passed\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d failure(s)\n", sFailures);
    return EXIT_FAILURE;
}
