#pragma once

// Black Ops zombies sounds for the game's mixer (ZombiesSound.cpp): `bomap gamesound`
// decodes the sound aliases the game plays from the zones' loaded sound data into
// 16-bit PCM WAV files and writes zombies/sound/manifest.txt, which names each alias's
// files and settings and which aliases play each of the game's sound roles.
//
// Loaded sounds are WMA (xWMA packets), MS ADPCM or PCM. WMA is decoded by running
// ffmpeg (if it is installed); MS ADPCM and PCM are decoded here. Streamed sounds (the
// music, perk jingles and most voice lines) are not inside the zones and are skipped.

#include "fastfile.h"
#include "zone_walker.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bomap {

// The alias settings the game uses, from its 84-byte snd_alias_t.
struct AliasParams {
    float volMin = 1.0f, volMax = 1.0f;     // linear, 0..1
    float pitchMin = 1.0f, pitchMax = 1.0f; // playback rate
    float distMin = 0.0f, distMax = 0.0f;   // full volume inside distMin, silent past distMax
    bool loop = false;
    std::string secondary; // another alias played with this one
};
AliasParams ReadAliasParams(const Zone& zone, const WalkResult& walk, const SoundAliasRecord& alias);

// The loaded sound's format (snd_asset): 0 PCM, 6 MS ADPCM, 7 WMA.
struct LoadedSoundInfo {
    uint32_t frames = 0, rate = 0, channels = 0, format = 0;
    uint32_t packets = 0; // WMA: seek table entries (one per packet)
};
LoadedSoundInfo ReadLoadedSound(const Zone& zone, const SoundAliasRecord& alias);

// Black Ops' MS ADPCM: blocks of 512 frames, 262 bytes per channel.
std::vector<int16_t> DecodeMsAdpcm(const uint8_t* data, size_t size, int channels, size_t frames);

// A RIFF WAVE file of 16-bit PCM.
std::vector<uint8_t> WavFile(const std::vector<int16_t>& pcm, int rate, int channels);

// A sound role the game plays (ZombiesSound.h) and the aliases that can play it, in
// order of preference (the first one found in the zones is used).
struct SoundRole {
    std::string role;
    std::vector<std::string> aliases;
};
const std::vector<SoundRole>& DefaultSoundRoles();

// bomap gamesound <zone>... --out <dir> [--ffmpeg <path>] [--roles <file>] [--weapons a,b,...]
int SoundCommand(int argc, char** argv);

} // namespace bomap
