// Zombies of Time: Black Ops sounds.
//
// `bomap gamesound` (tools/bomap) writes the zombies sounds from the player's own Black
// Ops install as 16-bit WAV files in zombies/sound/, with manifest.txt:
//
//   alias <name> <volMin> <volMax> <pitchMin> <pitchMax> <distMin> <distMax> <loop> <secondary|-> <file>...
//   role <role> <alias>...
//
// The game thread asks for a sound by role or alias; the files are read on first use and
// kept. Each play picks one of the alias's files (not the one it played last), a volume
// and pitch in the alias's range, and hands a voice to the audio thread. SoH's audio
// thread renders Ocarina of Time's synth into a buffer and then calls ZombiesSound_Mix
// (mod/patches/0004-zombies-sound-mix.patch), which adds the voices into it: resampled
// to the output rate, faded by distance from the nearest player (Black Ops' distMin to
// distMax) and panned by where they are against that player's camera. Nothing from Black
// Ops ships with the mode: without zombies/sound/, every call returns false and the
// caller plays its Ocarina of Time sound.

#include "ZombiesSound.h"
#include "ZombiesGuns.h"
#include "ZombiesMode.h"
#include "ZombiesModel.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "soh/Enhancements/audio/AudioSettings.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace Zombies {

namespace {

constexpr int kMaxVoices = 24;
constexpr float kMixGain = 0.9f; // Black Ops' levels against Ocarina of Time's synth

struct Pcm {
    std::vector<int16_t> samples; // interleaved
    uint32_t rate = 0;
    int channels = 1;
    uint32_t frames = 0;
};

struct Alias {
    float volMin = 1.0f, volMax = 1.0f, pitchMin = 1.0f, pitchMax = 1.0f;
    float distMin = 0.0f, distMax = 0.0f;
    bool loop = false;
    std::string secondary;
    std::vector<std::string> files;
    int last = -1; // the file played last, not picked again straight away
};

struct Voice {
    int id = 0;
    std::shared_ptr<const Pcm> pcm;
    double pos = 0.0;  // source frame
    double step = 1.0; // source frames per output frame, before the output rate
    float volume = 1.0f;
    bool positional = false;
    Vec3f at = {};
    float distMin = 0.0f, distMax = 0.0f;
    bool loop = false;
    bool stop = false;
    float gainL = -1.0f, gainR = -1.0f; // last buffer's, ramped from (-1: start at target)
    uint32_t started = 0;
};

struct Listener {
    Vec3f pos;
    float rightX, rightZ; // the camera's right, flat
};

struct Command {
    enum Kind { Play, Move, Stop, StopAll } kind;
    Voice voice;  // Play
    int id = 0;   // Move, Stop
    Vec3f at = {};
};

// Game thread only.
bool sLoaded = false;
bool sAvailable = false;
std::unordered_map<std::string, Alias> sAliases;
std::unordered_map<std::string, std::vector<std::string>> sRoles;
std::unordered_map<std::string, std::shared_ptr<const Pcm>> sFiles; // null: unreadable
std::unordered_map<const ZAnim*, bool> sAnimHasSounds;
std::string sDir;
uint32_t sRand = 0x9E3779B9u;
int sNextId = 1;
int sAmbienceTimer = 120;
int sVocalsTimer = 300;
struct Kept {
    int handle = 0; // 0: the role isn't imported
    uint32_t frame = 0;
};
std::unordered_map<const void*, Kept> sKept;
uint32_t sFrame = 0;
void (*sLog)(const char*, const char*) = nullptr;
std::atomic<void (*)(const int16_t*, uint32_t, uint32_t)> sCapture{ nullptr };

// Shared with the audio thread, under sLock. The lock, the queue and the voices are never
// destroyed: the audio thread can still be mixing while static destructors run at exit.
std::mutex& sLock = *new std::mutex;
std::vector<Command>& sCommands = *new std::vector<Command>;
Listener sListeners[2];
int sListenerCount = 0;
float sVolume = 1.0f; // master and sound effects settings
bool sMirror = false;
// Audio thread only.
std::vector<Voice>& sVoices = *new std::vector<Voice>;
uint32_t sMixClock = 0;

float Rand01() {
    // xorshift: the game's own random numbers stay untouched
    sRand ^= sRand << 13;
    sRand ^= sRand >> 17;
    sRand ^= sRand << 5;
    return static_cast<float>(sRand & 0xFFFFFF) / 16777216.0f;
}

uint32_t Le32(const std::vector<uint8_t>& d, size_t o) {
    return static_cast<uint32_t>(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | static_cast<uint32_t>(d[o + 3]) << 24);
}

uint16_t Le16(const std::vector<uint8_t>& d, size_t o) {
    return static_cast<uint16_t>(d[o] | d[o + 1] << 8);
}

// A 16-bit PCM WAV file, mono or stereo.
std::shared_ptr<const Pcm> ReadWav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return nullptr;
    }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) {
        return nullptr;
    }
    auto pcm = std::make_shared<Pcm>();
    bool fmtOk = false;
    for (size_t at = 12; at + 8 <= d.size();) {
        uint32_t size = Le32(d, at + 4);
        size_t body = at + 8;
        if (body + size > d.size()) {
            size = static_cast<uint32_t>(d.size() - body);
        }
        if (std::memcmp(d.data() + at, "fmt ", 4) == 0 && size >= 16) {
            uint16_t format = Le16(d, body);
            pcm->channels = Le16(d, body + 2);
            pcm->rate = Le32(d, body + 4);
            uint16_t bits = Le16(d, body + 14);
            fmtOk = format == 1 && bits == 16 && (pcm->channels == 1 || pcm->channels == 2) && pcm->rate >= 4000 &&
                    pcm->rate <= 192000;
        } else if (std::memcmp(d.data() + at, "data", 4) == 0 && fmtOk) {
            size_t count = size / 2;
            pcm->samples.resize(count);
            for (size_t i = 0; i < count; i++) {
                pcm->samples[i] = static_cast<int16_t>(Le16(d, body + i * 2));
            }
            pcm->frames = static_cast<uint32_t>(count / static_cast<size_t>(pcm->channels));
            // Two frames at least: the mixer interpolates between a frame and the next.
            return pcm->frames > 1 ? pcm : nullptr;
        }
        at = body + size + (size & 1);
    }
    return nullptr;
}

void LoadManifest() {
    sLoaded = true;
    sAvailable = false;
    sAliases.clear();
    sRoles.clear();
    sFiles.clear();
    sAnimHasSounds.clear();
    sDir = Ship::Context::GetPathRelativeToAppDirectory("zombies/sound");
    std::ifstream f(sDir + "/manifest.txt");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ls(line);
        std::string kind, name;
        if (!(ls >> kind >> name)) {
            continue;
        }
        if (kind == "alias") {
            Alias a;
            int loop = 0;
            if (!(ls >> a.volMin >> a.volMax >> a.pitchMin >> a.pitchMax >> a.distMin >> a.distMax >> loop >>
                  a.secondary)) {
                continue;
            }
            a.loop = loop != 0;
            if (a.secondary == "-") {
                a.secondary.clear();
            }
            for (std::string file; ls >> file;) {
                a.files.push_back(file);
            }
            if (!a.files.empty()) {
                sAliases[name] = a;
            }
        } else if (kind == "role") {
            std::vector<std::string> list;
            for (std::string a; ls >> a;) {
                list.push_back(a);
            }
            sRoles[name] = list;
        }
    }
    sAvailable = !sAliases.empty();
}

void EnsureLoaded() {
    if (!sLoaded) {
        LoadManifest();
    }
}

std::shared_ptr<const Pcm> File(const std::string& name) {
    auto it = sFiles.find(name);
    if (it != sFiles.end()) {
        return it->second;
    }
    std::shared_ptr<const Pcm> pcm = ReadWav(sDir + "/" + name);
    sFiles[name] = pcm;
    return pcm;
}

const std::string* RoleAlias(const char* role) {
    EnsureLoaded();
    auto it = sRoles.find(role);
    if (it == sRoles.end() || it->second.empty()) {
        return nullptr;
    }
    // A role with several aliases (Pack-a-Punch's knuckle cracks) picks one.
    size_t pick = std::min(it->second.size() - 1, static_cast<size_t>(Rand01() * static_cast<float>(it->second.size())));
    return &it->second[pick];
}

void Push(const Command& c) {
    std::lock_guard<std::mutex> lock(sLock);
    if (sCommands.size() < 256) {
        sCommands.push_back(c);
    }
}

// Starts one alias; returns the voice id, 0 if nothing played.
int Start(const std::string& name, const Vec3f* pos, float volume, bool forceLoop, int depth) {
    EnsureLoaded();
    auto it = sAliases.find(name);
    if (it == sAliases.end()) {
        return 0;
    }
    Alias& a = it->second;
    int count = static_cast<int>(a.files.size());
    int pick = std::min(count - 1, static_cast<int>(Rand01() * static_cast<float>(count)));
    if (count > 1 && pick == a.last) {
        pick = (pick + 1) % count;
    }
    a.last = pick;
    std::shared_ptr<const Pcm> pcm = File(a.files[pick]);
    if (pcm == nullptr) {
        return 0;
    }
    Command c{ Command::Play, {}, 0, {} };
    Voice& v = c.voice;
    v.id = sNextId++;
    if (sNextId <= 0) {
        sNextId = 1;
    }
    v.pcm = pcm;
    float pitch = a.pitchMin + (a.pitchMax - a.pitchMin) * Rand01();
    v.step = static_cast<double>(pcm->rate) * static_cast<double>(std::clamp(pitch, 0.25f, 4.0f));
    v.volume = volume * (a.volMin + (a.volMax - a.volMin) * Rand01());
    v.positional = pos != nullptr;
    v.at = pos != nullptr ? *pos : Vec3f{};
    // No distances: Black Ops plays it flat for its own player (a gun's _plr sounds), so
    // it is heard as recorded near the player and fades for the other one.
    v.distMin = a.distMax > a.distMin ? a.distMin : std::max(a.distMin, 120.0f);
    v.distMax = a.distMax > a.distMin ? a.distMax : v.distMin + 1500.0f;
    v.loop = a.loop || forceLoop;
    Push(c);
    if (sLog != nullptr) {
        sLog(name.c_str(), a.files[pick].c_str());
    }
    if (depth == 0 && !a.secondary.empty() && !forceLoop) {
        Start(a.secondary, pos, volume, false, 1);
    }
    return v.id;
}

// The gains for one voice against the nearest listener.
void Gains(const Voice& v, float* left, float* right, bool* stereo) {
    float gain = v.volume * sVolume * kMixGain;
    *stereo = v.pcm->channels == 2;
    if (!v.positional || sListenerCount == 0) {
        *left = *right = gain;
        return;
    }
    const Listener* best = &sListeners[0];
    float bestDist = 1e30f;
    for (int i = 0; i < sListenerCount; i++) {
        float dx = v.at.x - sListeners[i].pos.x, dy = v.at.y - sListeners[i].pos.y, dz = v.at.z - sListeners[i].pos.z;
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < bestDist) {
            bestDist = d;
            best = &sListeners[i];
        }
    }
    float fade = 1.0f;
    if (bestDist > v.distMin) {
        fade = 1.0f - (bestDist - v.distMin) / (v.distMax - v.distMin);
        fade = std::clamp(fade, 0.0f, 1.0f);
        fade *= fade; // falls off faster near the edge, like Black Ops' curves
    }
    gain *= fade;
    // Within distMin a stereo sound plays as recorded (a gun in the player's hands).
    if (*stereo && bestDist <= v.distMin) {
        *left = *right = gain;
        return;
    }
    *stereo = false;
    float pan = 0.0f;
    float dx = v.at.x - best->pos.x, dz = v.at.z - best->pos.z;
    float flat = std::sqrt(dx * dx + dz * dz);
    if (flat > 1.0f) {
        pan = (dx * best->rightX + dz * best->rightZ) / flat;
        // Close by, it's around the player rather than off to one side.
        pan *= std::min(1.0f, flat / 150.0f);
    }
    if (sMirror) {
        pan = -pan;
    }
    // Constant power, with the far side kept a little so nothing is in one ear only.
    float angle = (std::clamp(pan, -1.0f, 1.0f) * 0.8f + 1.0f) * 0.785398f;
    *left = gain * std::cos(angle) * 1.41421f;
    *right = gain * std::sin(angle) * 1.41421f;
}

} // namespace

bool Sound_Available() {
    EnsureLoaded();
    return sAvailable;
}

bool Sound_Has(const char* role) {
    return RoleAlias(role) != nullptr;
}

bool Sound_HasAlias(const std::string& alias) {
    EnsureLoaded();
    return sAliases.count(alias) > 0;
}

bool Sound_Play(const char* role, const Vec3f* pos, float volume) {
    const std::string* alias = RoleAlias(role);
    return alias != nullptr && Start(*alias, pos, volume, false, 0) != 0;
}

bool Sound_PlayAlias(const std::string& alias, const Vec3f* pos, float volume) {
    return Start(alias, pos, volume, false, 0) != 0;
}

int Sound_Loop(const char* role, const Vec3f* pos, float volume) {
    const std::string* alias = RoleAlias(role);
    return alias != nullptr ? Start(*alias, pos, volume, true, 0) : 0;
}

void Sound_Move(int handle, const Vec3f& pos) {
    if (handle > 0) {
        Push({ Command::Move, {}, handle, pos });
    }
}

void Sound_Stop(int handle) {
    if (handle > 0) {
        Push({ Command::Stop, {}, handle, {} });
    }
}

bool Sound_Keep(const void* key, const char* role, const Vec3f* pos, float volume) {
    auto it = sKept.find(key);
    if (it == sKept.end()) {
        it = sKept.emplace(key, Kept{ Sound_Loop(role, pos, volume), sFrame }).first;
    } else if (it->second.handle != 0 && pos != nullptr) {
        Sound_Move(it->second.handle, *pos);
    }
    it->second.frame = sFrame;
    return it->second.handle != 0;
}

void Sound_StopAll() {
    sKept.clear();
    Push({ Command::StopAll, {}, 0, {} });
    // Look for a new or changed import next time.
    sLoaded = false;
}

static std::string Upgraded(const std::string& bo) {
    if (bo.size() > 3 && bo.compare(bo.size() - 3, 3, "_zm") == 0) {
        return bo.substr(0, bo.size() - 3) + "_upgraded_zm";
    }
    return bo + "_upgraded";
}

bool Sound_GunFire(const char* boName, int gunClass, bool pap, const Vec3f* pos) {
    if (boName != nullptr && boName[0] != '\0') {
        if (pap && Sound_Play(("gun_" + Upgraded(boName) + "_fire").c_str(), pos)) {
            return true;
        }
        if (Sound_Play((std::string("gun_") + boName + "_fire").c_str(), pos)) {
            return true;
        }
    }
    // Borrowed from a gun of the same class.
    static const char* const kClassGun[] = { "m1911_zm", "mp40_zm", "m14_zm", "ithaca_zm", "rpk_zm", "ray_gun_zm" };
    static const GunClass kClasses[] = { GunClass::Pistol, GunClass::Smg, GunClass::Rifle,
                                         GunClass::Shotgun, GunClass::Lmg, GunClass::Launcher };
    for (int i = 0; i < 6; i++) {
        if (static_cast<int>(kClasses[i]) == gunClass) {
            return Sound_Play((std::string("gun_") + kClassGun[i] + "_fire").c_str(), pos);
        }
    }
    return false;
}

bool Sound_GunRole(const char* boName, const char* what, const Vec3f* pos) {
    if (boName != nullptr && Sound_Play((std::string("gun_") + boName + "_" + what).c_str(), pos)) {
        return true;
    }
    std::string generic = std::strcmp(what, "dry") == 0 ? "dry_fire_smg" : std::string("gun_") + what;
    return Sound_Play(generic.c_str(), pos);
}

bool Sound_AnimNotes(const ZAnim* anim, float fromSec, float toSec, const Vec3f* pos) {
    if (anim == nullptr || !Sound_Available()) {
        return false;
    }
    auto known = sAnimHasSounds.find(anim);
    if (known == sAnimHasSounds.end()) {
        bool any = false;
        for (const std::string& n : Anim_Notes(anim)) {
            any = any || (n.rfind("sndnt#", 0) == 0 && Sound_HasAlias(n.substr(6)));
        }
        known = sAnimHasSounds.emplace(anim, any).first;
    }
    if (!known->second) {
        return false;
    }
    for (const std::string& n : Anim_NotesBetween(anim, fromSec, toSec)) {
        if (n.rfind("sndnt#", 0) == 0) {
            Sound_PlayAlias(n.substr(6), pos);
        }
    }
    return true;
}

void Sound_Update(PlayState* play, bool ambience) {
    if (play == nullptr) {
        return;
    }
    EnsureLoaded();
    // Kept loops nobody kept this frame or the last stop.
    sFrame++;
    for (auto it = sKept.begin(); it != sKept.end();) {
        if (sFrame - it->second.frame > 2) {
            Sound_Stop(it->second.handle);
            it = sKept.erase(it);
        } else {
            ++it;
        }
    }
    Listener ls[2];
    int count = 0;
    Player* link = GET_PLAYER(play);
    Vec3f eye = play->view.eye, at = play->view.lookAt;
    float fx = at.x - eye.x, fz = at.z - eye.z;
    float len = std::sqrt(fx * fx + fz * fz);
    if (len < 0.001f) {
        fx = 0.0f;
        fz = 1.0f;
        len = 1.0f;
    }
    if (link != nullptr) {
        // Heard from the player, facing where their camera does: their own gun and
        // footsteps are close by and centred.
        ls[count++] = { { link->actor.world.pos.x, link->actor.world.pos.y + 50.0f, link->actor.world.pos.z },
                        -fz / len,
                        fx / len };
    }
    Vec3f eye2, at2;
    if (count < 2 && Coop_Listener(&eye2, &at2)) {
        float gx = at2.x - eye2.x, gz = at2.z - eye2.z;
        float l2 = std::sqrt(gx * gx + gz * gz);
        if (l2 > 0.001f) {
            ls[count++] = { at2, -gz / l2, gx / l2 };
        }
    }
    float volume = AudioSettings_GetMasterVolume() * static_cast<float>(CVarGetInteger("gSettings.Volume.SFX", 100)) / 100.0f;
    {
        std::lock_guard<std::mutex> lock(sLock);
        sListeners[0] = ls[0];
        sListeners[1] = ls[1];
        sListenerCount = count;
        sVolume = volume;
        sMirror = AudioSettings_GetMirroredWorld() != 0;
    }
    // Nacht der Untoten's ambience: Black Ops' spooky one-shots around the players, and
    // the distant moans now and then.
    if (ambience && sAvailable && count > 0) {
        if (--sAmbienceTimer <= 0) {
            sAmbienceTimer = 20 * 6 + static_cast<int>(Rand01() * 20.0f * 10.0f);
            float angle = Rand01() * 6.2831853f, dist = 150.0f + Rand01() * 350.0f;
            Vec3f p = { ls[0].pos.x + std::sin(angle) * dist, ls[0].pos.y + 40.0f, ls[0].pos.z + std::cos(angle) * dist };
            if (!Sound_Play("amb_spooky", &p)) {
                Sound_Play("amb_spooky_2d");
            }
        }
        if (--sVocalsTimer <= 0) {
            sVocalsTimer = 20 * 15 + static_cast<int>(Rand01() * 20.0f * 20.0f);
            float angle = Rand01() * 6.2831853f;
            Vec3f p = { ls[0].pos.x + std::sin(angle) * 600.0f, ls[0].pos.y, ls[0].pos.z + std::cos(angle) * 600.0f };
            Sound_Play("amb_vocals", &p, 0.7f);
        }
    }
}

void Sound_SetLog(void (*log)(const char*, const char*)) {
    sLog = log;
}

void Sound_SetCapture(void (*capture)(const int16_t*, uint32_t, uint32_t)) {
    sCapture.store(capture);
}

// Audio thread: apply the game's commands, then add every voice into the output.
static void Mix(int16_t* out, uint32_t frames, uint32_t rate) {
    std::lock_guard<std::mutex> lock(sLock);
    for (Command& c : sCommands) {
        switch (c.kind) {
            case Command::Play:
                if (static_cast<int>(sVoices.size()) >= kMaxVoices) {
                    // Full: the oldest one-shot makes way.
                    auto oldest = sVoices.end();
                    for (auto it = sVoices.begin(); it != sVoices.end(); ++it) {
                        if (!it->loop && (oldest == sVoices.end() || it->started < oldest->started)) {
                            oldest = it;
                        }
                    }
                    if (oldest == sVoices.end()) {
                        break;
                    }
                    sVoices.erase(oldest);
                }
                c.voice.started = sMixClock++;
                sVoices.push_back(std::move(c.voice));
                break;
            case Command::Move:
                for (Voice& v : sVoices) {
                    if (v.id == c.id) {
                        v.at = c.at;
                    }
                }
                break;
            case Command::Stop:
                for (Voice& v : sVoices) {
                    if (v.id == c.id) {
                        v.stop = true;
                    }
                }
                break;
            case Command::StopAll:
                for (Voice& v : sVoices) {
                    v.stop = true;
                }
                break;
        }
    }
    sCommands.clear();
    if (frames == 0 || rate == 0) {
        return;
    }
    const float invFrames = 1.0f / static_cast<float>(frames);
    for (Voice& v : sVoices) {
        const Pcm& p = *v.pcm;
        float left, right;
        bool stereo;
        Gains(v, &left, &right, &stereo);
        if (v.stop) {
            left = right = 0.0f; // fade out over this buffer, then gone
        }
        if (v.gainL < 0.0f) {
            v.gainL = left;
            v.gainR = right;
        }
        double step = v.step / static_cast<double>(rate);
        float gl = v.gainL, gr = v.gainR;
        float dl = (left - gl) * invFrames, dr = (right - gr) * invFrames;
        const int16_t* s = p.samples.data();
        const double end = static_cast<double>(p.frames);
        for (uint32_t i = 0; i < frames; i++) {
            if (v.pos >= end - 1.0) {
                if (!v.loop) {
                    v.pos = end;
                    break;
                }
                // Wrapped, however far a high pitch stepped past the end.
                v.pos = std::fmod(v.pos - (end - 1.0), end - 1.0);
            }
            uint32_t i0 = static_cast<uint32_t>(v.pos);
            float t = static_cast<float>(v.pos - static_cast<double>(i0));
            float l, r;
            if (p.channels == 2) {
                float l0 = static_cast<float>(s[i0 * 2]), l1 = static_cast<float>(s[i0 * 2 + 2]);
                float r0 = static_cast<float>(s[i0 * 2 + 1]), r1 = static_cast<float>(s[i0 * 2 + 3]);
                l = l0 + (l1 - l0) * t;
                r = r0 + (r1 - r0) * t;
                if (!stereo) {
                    l = r = (l + r) * 0.5f;
                }
            } else {
                float a = static_cast<float>(s[i0]), b = static_cast<float>(s[i0 + 1]);
                l = r = a + (b - a) * t;
            }
            gl += dl;
            gr += dr;
            int mixedL = out[i * 2] + static_cast<int>(l * gl);
            int mixedR = out[i * 2 + 1] + static_cast<int>(r * gr);
            out[i * 2] = static_cast<int16_t>(std::clamp(mixedL, -32768, 32767));
            out[i * 2 + 1] = static_cast<int16_t>(std::clamp(mixedR, -32768, 32767));
            v.pos += step;
        }
        v.gainL = left;
        v.gainR = right;
    }
    sVoices.erase(std::remove_if(sVoices.begin(), sVoices.end(),
                                 [](const Voice& v) { return v.stop || v.pos >= static_cast<double>(v.pcm->frames) - 1.0; }),
                  sVoices.end());
}

} // namespace Zombies

extern "C" void ZombiesSound_Mix(int16_t* samples, uint32_t frames, uint32_t rate) {
    Zombies::Mix(samples, frames, rate);
    auto capture = Zombies::sCapture.load();
    if (capture != nullptr) {
        capture(samples, frames, rate);
    }
}
