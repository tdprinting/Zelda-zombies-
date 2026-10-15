// bomap gamesound: Black Ops sound aliases as WAV files and a manifest for the game's
// mixer. See sound_export.h.

#include "sound_export.h"

#include "asset_export.h"
#include "zone_stream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace bomap {

namespace {

uint32_t Rd32(const std::vector<uint8_t>& d, size_t o) {
    return o + 4 <= d.size() ? static_cast<uint32_t>(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | static_cast<uint32_t>(d[o + 3]) << 24) : 0;
}

uint16_t Rd16(const std::vector<uint8_t>& d, size_t o) {
    return o + 2 <= d.size() ? static_cast<uint16_t>(d[o] | d[o + 1] << 8) : 0;
}

void Put16(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

void Put32(std::vector<uint8_t>& v, uint32_t x) {
    Put16(v, x & 0xFFFF);
    Put16(v, x >> 16);
}

void PutTag(std::vector<uint8_t>& v, const char* tag) {
    v.insert(v.end(), tag, tag + 4);
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

std::vector<uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Runs a command line, quoting it for cmd.exe on Windows. Returns the exit status.
int Run(const std::string& cmd) {
#ifdef _WIN32
    return std::system(("\"" + cmd + "\"").c_str());
#else
    return std::system(cmd.c_str());
#endif
}

std::string Quote(const std::string& s) {
    return "\"" + s + "\"";
}

#ifdef _WIN32
constexpr const char* kNull = "NUL";
#else
constexpr const char* kNull = "/dev/null";
#endif

// WMA packets wrapped in a WAVE file ffmpeg's WMA v2 decoder reads. Black Ops stores
// xWMA: the packets with no ASF header. The decoder needs the stream's average byte rate
// (it picks its tables from the bit rate) and the encoder flags, which xWMA leaves out;
// Black Ops' sounds use exponent VLCs, the bit reservoir and variable block lengths (0xF).
std::vector<uint8_t> WmaWave(const uint8_t* data, size_t size, const LoadedSoundInfo& info, uint32_t byteRate) {
    uint32_t packets = std::max<uint32_t>(1, info.packets);
    uint32_t blockAlign = static_cast<uint32_t>(size / packets);
    std::vector<uint8_t> fmt;
    Put16(fmt, 0x161); // WMA v2
    Put16(fmt, info.channels);
    Put32(fmt, info.rate);
    Put32(fmt, byteRate);
    Put16(fmt, blockAlign);
    Put16(fmt, 16);
    Put16(fmt, 10); // extra bytes: samples per block (4), encode options (2), super block align (4)
    Put32(fmt, 0);
    Put16(fmt, 0x000F);
    Put32(fmt, 0);
    std::vector<uint8_t> out;
    PutTag(out, "RIFF");
    Put32(out, static_cast<uint32_t>(4 + 8 + fmt.size() + 8 + size));
    PutTag(out, "WAVE");
    PutTag(out, "fmt ");
    Put32(out, static_cast<uint32_t>(fmt.size()));
    out.insert(out.end(), fmt.begin(), fmt.end());
    PutTag(out, "data");
    Put32(out, static_cast<uint32_t>(size));
    out.insert(out.end(), data, data + size);
    return out;
}

// Decodes WMA with ffmpeg. The byte rate isn't stored, so xWMA's rates are tried, the
// usual one first; a wrong rate makes the decoder report errors or stop early, the right
// one decodes every frame cleanly.
bool DecodeWma(const std::string& ffmpeg, const std::string& tmpDir, const uint8_t* data, size_t size,
               const LoadedSoundInfo& info, std::vector<int16_t>& pcm) {
    // Black Ops' zombies sounds are 48 kbps mono and 96 kbps stereo; the rest are tried after.
    std::vector<uint32_t> rates = info.channels == 2 ? std::vector<uint32_t>{ 12000, 16000, 20000, 24000, 8000, 6000 }
                                                     : std::vector<uint32_t>{ 6000, 8000, 4000, 12000, 2500, 16000 };
    for (uint32_t r : { 2500u, 4000u, 6000u, 8000u, 12000u, 16000u, 20000u, 24000u, 32000u, 40000u, 48000u }) {
        if (std::find(rates.begin(), rates.end(), r) == rates.end()) {
            rates.push_back(r);
        }
    }
    std::string in = tmpDir + "/.gamesound_in.wav", out = tmpDir + "/.gamesound_out.pcm",
                err = tmpDir + "/.gamesound_err.txt";
    bool ok = false;
    for (uint32_t rate : rates) {
        WriteFile(in, WmaWave(data, size, info, rate));
        std::remove(out.c_str());
        int status = Run(Quote(ffmpeg) + " -nostdin -hide_banner -loglevel error -y -i " + Quote(in) +
                         " -f s16le -acodec pcm_s16le " + Quote(out) + " 2> " + Quote(err));
        std::vector<uint8_t> errText = ReadFileBytes(err);
        std::vector<uint8_t> bytes = ReadFileBytes(out);
        size_t frames = bytes.size() / (2 * std::max<uint32_t>(1, info.channels));
        if (std::getenv("BOMAP_SOUND_DEBUG") != nullptr) {
            printf("    rate %u: status %d, %zu of %u frames, %s\n", rate, status, frames, info.frames,
                   std::string(errText.begin(), errText.end()).c_str());
        }
        // The last frame of a sound can come up short (the encoder's delay): up to one
        // WMA frame (2048) is padded with silence.
        if (status == 0 && errText.empty() && frames + 2048 >= info.frames) {
            pcm.resize(bytes.size() / 2);
            std::memcpy(pcm.data(), bytes.data(), pcm.size() * 2);
            ok = true;
            break;
        }
    }
    std::remove(in.c_str());
    std::remove(out.c_str());
    std::remove(err.c_str());
    return ok;
}

bool FfmpegPresent(const std::string& ffmpeg) {
    return Run(Quote(ffmpeg) + " -hide_banner -version > " + kNull + " 2>&1") == 0;
}

struct ZoneData {
    std::string path;
    Zone zone;
    WalkResult walk;
};

struct Variant {
    const ZoneData* zone = nullptr;
    const SoundAliasRecord* rec = nullptr;
};

std::string SafeName(const std::string& s) {
    std::string out;
    for (char c : s) {
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
    }
    return out;
}

std::string Fmt(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3g", v);
    return buf;
}

} // namespace

AliasParams ReadAliasParams(const Zone& zone, const WalkResult& walk, const SoundAliasRecord& a) {
    AliasParams p;
    if (a.alias == kNoData) {
        return p;
    }
    const std::vector<uint8_t>& d = zone.data;
    size_t s = a.alias;
    p.loop = (Rd32(d, s + 20) & 1) != 0; // flags: bit 0 looping
    p.volMin = Rd16(d, s + 44) / 65535.0f;
    p.volMax = Rd16(d, s + 46) / 65535.0f;
    p.pitchMin = Rd16(d, s + 50) / 32767.0f;
    p.pitchMax = Rd16(d, s + 52) / 32767.0f;
    p.distMin = Rd16(d, s + 56);
    p.distMax = Rd16(d, s + 58);
    if (p.volMax < p.volMin) {
        std::swap(p.volMax, p.volMin);
    }
    if (p.pitchMax < p.pitchMin) {
        std::swap(p.pitchMax, p.pitchMin);
    }
    size_t sec = walk.Pointer(s + 12);
    if (sec != kNoData && walk.stream) {
        p.secondary = walk.stream->CString(sec);
    }
    return p;
}

LoadedSoundInfo ReadLoadedSound(const Zone& zone, const SoundAliasRecord& a) {
    LoadedSoundInfo info;
    if (!a.loaded || a.asset == kNoData) {
        return info;
    }
    const std::vector<uint8_t>& d = zone.data;
    info.frames = Rd32(d, a.asset + 4);
    info.rate = Rd32(d, a.asset + 8);
    info.channels = Rd32(d, a.asset + 12);
    info.format = Rd32(d, a.asset + 28);
    info.packets = Rd32(d, a.asset + 40);
    return info;
}

std::vector<int16_t> DecodeMsAdpcm(const uint8_t* data, size_t size, int channels, size_t frames) {
    static const int kCoef1[7] = { 256, 512, 0, 192, 240, 460, 392 };
    static const int kCoef2[7] = { 0, -256, 0, 64, 0, -208, -232 };
    static const int kAdapt[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };
    std::vector<int16_t> out;
    if (channels < 1 || channels > 2) {
        return out;
    }
    const size_t block = 262u * static_cast<size_t>(channels);
    out.reserve(frames * channels);
    auto s16 = [](const uint8_t* p) { return static_cast<int16_t>(p[0] | p[1] << 8); };
    for (size_t at = 0; at + 7u * channels <= size && out.size() < frames * channels; at += block) {
        const uint8_t* b = data + at;
        size_t end = std::min(size, at + block);
        int pred[2], delta[2], s1[2], s2[2];
        for (int c = 0; c < channels; c++) {
            pred[c] = std::min<int>(b[c], 6);
            delta[c] = s16(b + channels + 2 * c);
            s1[c] = s16(b + 3 * channels + 2 * c);
            s2[c] = s16(b + 5 * channels + 2 * c);
        }
        for (int c = 0; c < channels; c++) {
            out.push_back(static_cast<int16_t>(s2[c]));
        }
        for (int c = 0; c < channels; c++) {
            out.push_back(static_cast<int16_t>(s1[c]));
        }
        int c = 0;
        for (size_t i = at + 7u * channels; i < end; i++) {
            for (int shift = 4; shift >= 0; shift -= 4) {
                int nibble = (data[i] >> shift) & 0xF;
                int signedNibble = nibble >= 8 ? nibble - 16 : nibble;
                int predicted = (s1[c] * kCoef1[pred[c]] + s2[c] * kCoef2[pred[c]]) / 256 + signedNibble * delta[c];
                predicted = std::clamp(predicted, -32768, 32767);
                out.push_back(static_cast<int16_t>(predicted));
                s2[c] = s1[c];
                s1[c] = predicted;
                delta[c] = std::max(16, kAdapt[nibble] * delta[c] / 256);
                c = (c + 1) % channels;
            }
        }
    }
    out.resize(std::min(out.size(), frames * channels));
    return out;
}

std::vector<uint8_t> WavFile(const std::vector<int16_t>& pcm, int rate, int channels) {
    std::vector<uint8_t> out;
    uint32_t bytes = static_cast<uint32_t>(pcm.size() * 2);
    PutTag(out, "RIFF");
    Put32(out, 36 + bytes);
    PutTag(out, "WAVE");
    PutTag(out, "fmt ");
    Put32(out, 16);
    Put16(out, 1);
    Put16(out, static_cast<uint32_t>(channels));
    Put32(out, static_cast<uint32_t>(rate));
    Put32(out, static_cast<uint32_t>(rate * channels * 2));
    Put16(out, static_cast<uint32_t>(channels * 2));
    Put16(out, 16);
    PutTag(out, "data");
    Put32(out, bytes);
    for (int16_t s : pcm) {
        Put16(out, static_cast<uint16_t>(s));
    }
    return out;
}

// The game's sound roles (ZombiesSound.h) and Black Ops' aliases for them: Nacht der
// Untoten's own first, then the shared zombies ones. Only names; the sounds come from
// the player's zones.
const std::vector<SoundRole>& DefaultSoundRoles() {
    static const std::vector<SoundRole> roles = {
        // buying
        { "purchase", { "cha_ching", "zmb_cha_ching" } },
        { "deny", { "no_cha_ching", "zmb_no_cha_ching" } },
        { "wall_buy", { "zmb_weap_wall" } },
        { "ammo_buy", { "wpn_ammo_pickup_plr" } },
        { "door_open", { "zmb_door_wood_open", "zmb_heavy_door_open" } },
        { "debris_clear", { "zmb_wooden_door_fall", "zmb_door_wood_open" } },
        // windows
        { "board_break", { "break_boards", "zmb_break_boards" } },
        { "board_tear", { "remove_boards", "zmb_bar_pull" } },
        { "board_repair", { "repair_boards", "zmb_repair_boards" } },
        { "board_slam", { "zmb_board_slam" } },
        // the Mystery Box
        { "box_open", { "zmb_lid_open" } },
        { "box_close", { "zmb_lid_close" } },
        { "box_music", { "zmb_music_box" } },
        { "box_teddy", { "zmb_laugh_child" } },
        { "box_move", { "zmb_box_move" } },
        { "box_poof", { "zmb_box_poof" } },
        { "box_land", { "zmb_box_poof_land" } },
        { "box_laugh", { "zmb_ann_vox_laugh_l" } },
        // power-ups
        { "powerup_spawn", { "zmb_spawn_powerup" } },
        { "powerup_loop", { "zmb_spawn_powerup_loop" } },
        { "powerup_grab", { "zmb_powerup_grabbed" } },
        { "powerup_max_ammo", { "zmb_full_ammo" } },
        { "powerup_insta_kill", { "zmb_insta_kill" } },
        { "powerup_insta_loop", { "zmb_insta_kill_loop" } },
        { "powerup_double_loop", { "zmb_double_point_loop" } },
        { "powerup_nuke", { "evt_nuked" } },
        { "powerup_nuke_flash", { "evt_nuke_flash" } },
        { "powerup_carpenter", { "evt_carpenter" } },
        { "powerup_carpenter_end", { "evt_carpenter_end" } },
        { "powerup_end", { "zmb_points_loop_off" } },
        // perks, power and Pack-a-Punch
        { "perk_loop", { "zmb_perks_machine_loop" } },
        { "perk_jingle", { "zmb_perks_broken_jingle" } },
        { "perk_dispense", { "evt_bottle_dispense" } },
        { "perk_open", { "evt_perk_bottle_open" } },
        { "perk_drink", { "evt_perk_swallow" } },
        { "perk_burp", { "evt_belch" } },
        { "perk_bottle_break", { "evt_bottle_break" } },
        { "perk_deny", { "evt_perk_deny" } },
        { "power_switch", { "zmb_switch_flip" } },
        { "power_on", { "zmb_perks_power_on" } },
        { "power_surge", { "evt_generator_surge" } },
        { "pap_upgrade", { "zmb_perks_packa_upgrade" } },
        { "pap_ready", { "zmb_perks_packa_ready" } },
        { "pap_loop", { "zmb_perks_packa_loop" } },
        { "pap_deny", { "zmb_perks_packa_deny" } },
        { "pap_knuckle", { "zmb_perks_packa_knuckle_0", "zmb_perks_packa_knuckle_1" } },
        // zombies
        { "zombie_amb", { "zmb_zombie_vocals_amb", "zmb_vocals_zombie_ambience" } },
        { "zombie_attack", { "zmb_zombie_vocals_attack", "zmb_vocals_zombie_attack" } },
        { "zombie_sprint", { "zmb_zombie_vocals_sprint", "zmb_vocals_zombie_sprint" } },
        { "zombie_death", { "zmb_zombie_vocals_death", "zmb_vocals_zombie_death" } },
        { "zombie_behind", { "zmb_zombie_vocals_behind", "zmb_vocals_zombie_behind" } },
        { "zombie_taunt", { "zmb_zombie_vocals_taunt", "zmb_vocals_zombie_taunt" } },
        { "zombie_board", { "zmb_zombie_vocals_board", "zmb_vocals_zombie_teardown" } },
        { "zombie_crawler", { "crawl_vocals", "zmb_vocals_zombie_crawler" } },
        { "zombie_swipe", { "attack_whoosh", "zmb_attack_whoosh" } },
        { "zombie_hit", { "prj_bullet_impact_small_flesh" } },
        { "zombie_headshot", { "prj_bullet_impact_headshot" } },
        { "zombie_gib", { "chr_death_gibs", "zmb_death_gibs" } },
        { "zombie_head_gib", { "chr_zombie_head_gib", "zmb_zombie_head_gib" } },
        { "zombie_spawn", { "zmb_zombie_spawn" } },
        { "zombie_step", { "step_zombie" } },
        // hellhounds
        { "dog_round_start", { "zmb_dog_round_start" } },
        { "dog_spawn", { "zmb_hellhound_spawn" } },
        { "dog_prespawn", { "zmb_hellhound_prespawn" } },
        { "dog_explode", { "zmb_hellhound_explode" } },
        { "dog_amb", { "zmb_hellhound_vocals_amb" } },
        { "dog_bite", { "zmb_hellhound_vocals_bite" } },
        { "dog_death", { "zmb_hellhound_vocals_death" } },
        // players
        { "player_hit", { "evt_player_swiped" } },
        { "player_heartbeat", { "chr_heart_beat_ingame" } },
        { "player_down", { "chr_heart_beat" } },
        { "melee_hit", { "wpn_melee_hit" } },
        // weapons beyond each gun's own (gun_<weapon>_* roles are added from the weapons)
        { "raygun_impact", { "wpn_rgun_exp" } },
        { "raygun_loop", { "wpn_rgun_loop" } },
        { "explosion", { "wpn_grenade_explode", "exp_mortar" } },
        { "dry_fire_pistol", { "wpn_dryfire_pistol_plr" } },
        { "dry_fire_smg", { "wpn_dryfire_smg_plr" } },
        { "dry_fire_rifle", { "wpn_dryfire_rifle_plr" } },
        { "gun_raise", { "wpn_weap_raise_plr" } },
        { "gun_putaway", { "wpn_weap_putaway_plr" } },
        { "gun_pickup", { "wpn_weap_pickup_plr" } },
        // Nacht der Untoten's ambience
        { "amb_spooky", { "amb_spooky" } },
        { "amb_spooky_2d", { "amb_spooky_2d" } },
        { "amb_vocals", { "amb_vocals" } },
        // streamed in Black Ops (music and voices aren't in the zones); used if present
        { "round_start", { "mus_zombie_round_start" } },
        { "round_end", { "mus_zombie_round_over" } },
        { "game_over", { "mus_zombie_game_over" } },
    };
    return roles;
}

int SoundCommand(int argc, char** argv) {
    std::vector<std::string> paths;
    std::string outDir, ffmpeg = "ffmpeg", rolesFile;
    std::set<std::string> only; // --only <alias>: just these (checking a decode)
    std::set<std::string> weapons; // --weapons a,b,...: only these guns' sounds
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) {
            outDir = argv[++i];
        } else if (a == "--ffmpeg" && i + 1 < argc) {
            ffmpeg = argv[++i];
        } else if (a == "--roles" && i + 1 < argc) {
            rolesFile = argv[++i];
        } else if (a == "--weapons" && i + 1 < argc) {
            std::istringstream list(argv[++i]);
            for (std::string w; std::getline(list, w, ',');) {
                weapons.insert(w);
            }
        } else if (a == "--only" && i + 1 < argc) {
            only.insert(argv[++i]);
        } else {
            paths.push_back(a);
        }
    }
    if (paths.empty() || outDir.empty()) {
        fprintf(stderr,
                "usage: bomap gamesound <zone.ff>... --out <dir> [--ffmpeg <path>] [--roles <file>] [--weapons a,b,...]\n"
                "  zone/Common/zombie_cod5_prototype.ff and common_zombie.ff hold Nacht der Untoten's\n"
                "  sounds. Most are WMA, decoded with ffmpeg (https://ffmpeg.org); pass --ffmpeg if it\n"
                "  isn't on your PATH. --roles adds or replaces roles: lines of <role> <alias>...\n"
                "  --weapons limits the guns' sounds to those weapons (all of them otherwise).\n");
        return 2;
    }
    std::filesystem::create_directories(outDir);

    // Every zone stays loaded: the aliases point into their data.
    std::vector<std::unique_ptr<ZoneData>> zones;
    for (const std::string& p : paths) {
        auto z = std::make_unique<ZoneData>();
        z->path = p;
        z->zone = ReadFastfile(p);
        z->walk = WalkZone(z->zone);
        zones.push_back(std::move(z));
    }
    // Alias name -> its loaded variants, from the first zone that has any.
    std::map<std::string, std::vector<Variant>> aliases;
    for (const auto& z : zones) {
        std::map<std::string, std::vector<Variant>> here;
        for (const auto& [index, bank] : z->walk.sounds) {
            for (const SoundAliasRecord& a : bank.aliases) {
                if (a.loaded && a.data != kNoData) {
                    here[a.name].push_back({ z.get(), &a });
                }
            }
        }
        for (auto& [name, v] : here) {
            aliases.emplace(name, std::move(v));
        }
    }

    // Roles: the built-in table, the weapons' own sounds, then --roles.
    std::vector<SoundRole> roles = DefaultSoundRoles();
    std::set<std::string> wanted;        // aliases to write
    std::set<std::string> weaponsSeen;
    // Each weapon's sounds, by slot in its 62 sound names (the _plr ones are first person).
    static const std::pair<int, const char*> kGunSlots[] = {
        { 8, "fire" }, { 7, "fire" }, { 16, "fire_last" }, { 18, "dry" }, { 54, "raise" },
        { 56, "first_raise" }, { 58, "putaway" },
    };
    for (const auto& z : zones) {
        for (const WeaponInfo& w : ReadWeapons(z->zone, z->walk)) {
            // A listed gun brings its Pack-a-Punched weapon (m1911_zm, m1911_upgraded_zm).
            std::string base = w.name;
            size_t up = base.find("_upgraded");
            if (up != std::string::npos) {
                base.erase(up, 9);
            }
            if (!weaponsSeen.insert(w.name).second || (!weapons.empty() && !weapons.count(base))) {
                continue;
            }
            std::set<std::string> done;
            for (const auto& [slot, role] : kGunSlots) {
                for (const auto& [s, alias] : w.sounds) {
                    if (s == slot && !done.count(role) && aliases.count(alias)) {
                        roles.push_back({ "gun_" + w.name + "_" + role, { alias } });
                        done.insert(role);
                    }
                }
            }
            // The view animations name their sounds in notetracks ("sndnt#<alias>"): the
            // game plays those directly by alias.
            for (const auto& [slot, animName] : w.anims) {
                for (const auto& zz : zones) {
                    int ai = zz->walk.Find(0x04, animName);
                    if (ai < 0) {
                        continue;
                    }
                    auto rec = zz->walk.anims.find(ai);
                    const WalkedAsset& asset = zz->walk.assets[ai];
                    if (rec == zz->walk.anims.end() || rec->second.notify == kNoData || asset.header == kNoData) {
                        break;
                    }
                    int count = zz->zone.data[asset.header + 34];
                    for (int k = 0; k < count; k++) {
                        uint16_t id = Rd16(zz->zone.data, rec->second.notify + k * 8);
                        std::string note = id < zz->zone.scriptStrings.size() ? zz->zone.scriptStrings[id] : "";
                        if (note.rfind("sndnt#", 0) == 0 && aliases.count(note.substr(6))) {
                            wanted.insert(note.substr(6));
                        }
                    }
                    break;
                }
            }
        }
    }
    if (!rolesFile.empty()) {
        std::ifstream f(rolesFile);
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ls(line);
            SoundRole r;
            if (!(ls >> r.role) || r.role[0] == '#') {
                continue;
            }
            for (std::string a; ls >> a;) {
                r.aliases.push_back(a);
            }
            roles.erase(std::remove_if(roles.begin(), roles.end(), [&](const SoundRole& o) { return o.role == r.role; }),
                        roles.end());
            roles.push_back(r);
        }
    }
    std::vector<std::pair<std::string, std::string>> roleLines; // role, alias
    std::vector<std::string> rolesMissing;
    for (const SoundRole& r : roles) {
        auto it = std::find_if(r.aliases.begin(), r.aliases.end(), [&](const std::string& a) { return aliases.count(a) > 0; });
        if (it == r.aliases.end()) {
            rolesMissing.push_back(r.role);
            continue;
        }
        // Roles listing several aliases that are all present (pap_knuckle) use them all.
        std::string list;
        for (; it != r.aliases.end(); ++it) {
            if (aliases.count(*it)) {
                list += (list.empty() ? "" : " ") + *it;
                wanted.insert(*it);
            }
        }
        roleLines.push_back({ r.role, list });
    }
    // Secondary aliases play along with their primary.
    for (std::set<std::string> next = wanted; !next.empty();) {
        std::set<std::string> more;
        for (const std::string& a : next) {
            const Variant& v = aliases[a].front();
            std::string sec = ReadAliasParams(v.zone->zone, v.zone->walk, *v.rec).secondary;
            if (!sec.empty() && aliases.count(sec) && !wanted.count(sec)) {
                more.insert(sec);
                wanted.insert(sec);
            }
        }
        next = more;
    }

    if (!only.empty()) {
        wanted = only;
    }
    bool haveFfmpeg = FfmpegPresent(ffmpeg);
    if (!haveFfmpeg) {
        printf("ffmpeg not found (%s): WMA sounds are skipped; install ffmpeg or pass --ffmpeg <path>\n", ffmpeg.c_str());
    }
    std::map<std::pair<const ZoneData*, size_t>, std::string> written; // sound data -> file
    std::ostringstream manifest;
    manifest << "# Zombies of Time sounds, written by tools/bomap gamesound from the player's own Black Ops install.\n"
                "# alias <name> <volMin> <volMax> <pitchMin> <pitchMax> <distMin> <distMax> <loop> <secondary|-> <file>...\n"
                "# role <role> <alias>...\n";
    int files = 0, failed = 0, aliasCount = 0;
    size_t totalBytes = 0;
    for (const std::string& name : wanted) {
        std::vector<std::string> names;
        for (const Variant& v : aliases[name]) {
            auto key = std::make_pair(v.zone, v.rec->data);
            auto done = written.find(key);
            if (done != written.end()) {
                names.push_back(done->second);
                continue;
            }
            LoadedSoundInfo info = ReadLoadedSound(v.zone->zone, *v.rec);
            const uint8_t* data = v.zone->zone.data.data() + v.rec->data;
            size_t size = std::min<size_t>(v.rec->dataSize, v.zone->zone.data.size() - v.rec->data);
            std::vector<int16_t> pcm;
            bool ok = false;
            if (info.channels < 1 || info.channels > 2 || info.rate < 4000 || info.rate > 96000) {
                ok = false;
            } else if (info.format == 7) {
                ok = haveFfmpeg && DecodeWma(ffmpeg, outDir, data, size, info, pcm);
            } else if (info.format == 6) {
                pcm = DecodeMsAdpcm(data, size, static_cast<int>(info.channels), info.frames);
                ok = !pcm.empty();
            } else if (info.format == 0) {
                pcm.assign(size / 2, 0);
                std::memcpy(pcm.data(), data, pcm.size() * 2);
                ok = true;
            }
            if (!ok) {
                printf("  could not decode %s (format %u)\n", name.c_str(), info.format);
                failed++;
                continue;
            }
            pcm.resize(static_cast<size_t>(info.frames) * info.channels, 0);
            std::string file = SafeName(name) + "_" + std::to_string(names.size()) + ".wav";
            std::vector<uint8_t> wav = WavFile(pcm, static_cast<int>(info.rate), static_cast<int>(info.channels));
            WriteFile(outDir + "/" + file, wav);
            totalBytes += wav.size();
            files++;
            written[key] = file;
            names.push_back(file);
        }
        if (names.empty()) {
            continue;
        }
        const Variant& v = aliases[name].front();
        AliasParams p = ReadAliasParams(v.zone->zone, v.zone->walk, *v.rec);
        manifest << "alias " << name << " " << Fmt(p.volMin) << " " << Fmt(p.volMax) << " " << Fmt(p.pitchMin) << " "
                 << Fmt(p.pitchMax) << " " << Fmt(p.distMin) << " " << Fmt(p.distMax) << " " << (p.loop ? 1 : 0) << " "
                 << (p.secondary.empty() || !wanted.count(p.secondary) ? "-" : p.secondary);
        for (const std::string& n : names) {
            manifest << " " << n;
        }
        manifest << "\n";
        aliasCount++;
    }
    for (const auto& [role, list] : roleLines) {
        manifest << "role " << role << " " << list << "\n";
    }
    std::ofstream(outDir + "/manifest.txt") << manifest.str();
    printf("wrote %d sound files (%.1f MB) for %d aliases and %zu roles to %s\n", files, totalBytes / 1048576.0,
           aliasCount, roleLines.size(), outDir.c_str());
    if (failed > 0) {
        printf("%d sounds could not be decoded\n", failed);
    }
    if (!rolesMissing.empty()) {
        printf("roles with no loaded sound in these zones (the game keeps its own for them):");
        for (const std::string& r : rolesMissing) {
            printf(" %s", r.c_str());
        }
        printf("\n");
    }
    return 0;
}

} // namespace bomap
