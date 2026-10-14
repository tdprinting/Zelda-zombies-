// Zombies of Time: atmosphere and secrets.
//
// - Round music: the Shadow Temple drums while undead rounds are on, the mini-boss
//   theme for Wolfos rounds, silence between rounds, like Black Ops' round music.
// - Navi as the announcer: short callouts with her voice, in place of Samantha.
// - The music easter egg: three Gold Skulltulas hide somewhere around the arena.
//   Killing all three plays a song, as finding Black Ops' hidden teddy bears does.

#include "ZombiesMode.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <vector>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "seqcmd.h"
#include "sequence.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace Zombies {

static constexpr int kSkullCount = 3;
static constexpr int kPointsPerSkull = 100;
static constexpr int kLowHealthCooldown = 30 * 20;
// Gold Skulltula, ground-walking type (1). Its token flag is 0, so it never touches the
// save file's Skulltula flags, and it is killed before it can drop a token.
static constexpr s16 kGoldSkulltulaParams = (1 << 13) | (1 << 8);

struct Skull {
    Actor* actor;
    s16 lastHealth;
};

static std::vector<Skull> sSkulls;
static int sSkullsFound = 0;
static bool sSongPlayed = false;
static int sLowHealthTimer = 0;
static bool sMusicPlaying = false;

static bool MusicOn() {
    return CVarGetInteger(CVAR_ZOMBIES("Music"), 1) != 0;
}

void Navi(const std::string& text, u16 voice) {
    Announce("NAVI: " + text, 3);
    Sfx_PlaySfxCentered(voice);
}

// ---------------------------------------------------------------------------
// Music

static void PlayMusic(u16 seqId) {
    if (!MusicOn()) {
        return;
    }
    SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 10, 0, seqId);
    sMusicPlaying = true;
}

static void StopMusic(int fadeFrames) {
    if (sMusicPlaying) {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, fadeFrames);
        sMusicPlaying = false;
    }
}

// Puts the scene's own music or ambience back after a run.
static void RestoreSceneMusic(PlayState* play) {
    if (play == nullptr) {
        return;
    }
    if (play->sequenceCtx.seqId != NA_BGM_NO_MUSIC) {
        SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 30, 0, play->sequenceCtx.seqId);
    } else if (play->sequenceCtx.natureAmbienceId != NATURE_ID_NONE &&
               play->sequenceCtx.natureAmbienceId != NATURE_ID_DISABLED) {
        Audio_PlayNatureAmbienceSequence(play->sequenceCtx.natureAmbienceId);
    }
}

void Extras_OnRoundStart(bool dogRound) {
    if (sSongPlayed) {
        return; // the easter egg song plays out
    }
    PlayMusic(dogRound ? NA_BGM_MINI_BOSS : NA_BGM_SHADOW_TEMPLE);
    if (dogRound) {
        Sfx_PlaySfxCentered(NA_SE_VO_NAVY_ENEMY); // "Watch out!"
    }
}

void Extras_OnRoundEnd() {
    if (!sSongPlayed) {
        StopMusic(40);
    }
}

// ---------------------------------------------------------------------------
// Gold Skulltulas

static void ClearSkulls() {
    for (Skull& skull : sSkulls) {
        if (skull.actor != nullptr && skull.actor->update != nullptr) {
            Actor_Kill(skull.actor);
        }
    }
    sSkulls.clear();
}

static void PlaceSkulls(PlayState* play) {
    ClearSkulls();
    const Run& run = GetRun();
    for (int i = 0; i < kSkullCount; i++) {
        // Somewhere out of the way: past the stations, in a different direction each.
        for (int attempt = 0; attempt < 12; attempt++) {
            s16 angle =
                static_cast<s16>(run.originYaw + i * 0x5555 + static_cast<s16>((Rand_ZeroOne() - 0.5f) * 0x3000));
            float dist = 450.0f + Rand_ZeroOne() * 400.0f;
            float x = run.origin.x + Math_SinS(angle) * dist;
            float z = run.origin.z + Math_CosS(angle) * dist;
            float y;
            if (!FloorAt(play, x, z, run.origin.y, &y)) {
                continue;
            }
            Actor* actor = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_SW, x, y, z, 0, static_cast<s16>(angle), 0,
                                       kGoldSkulltulaParams);
            if (actor != nullptr) {
                actor->room = -1;
                sSkulls.push_back({ actor, actor->colChkInfo.health });
            }
            break;
        }
    }
}

static void OnSkullFound(PlayState* play, Actor* actor) {
    sSkullsFound++;
    Vec3f pos = actor->world.pos;
    SoundSource_PlaySfxAtFixedWorldPos(play, &pos, 30, NA_SE_SY_KINSTA_MARK_APPEAR);
    AwardPoints(0, kPointsPerSkull);
    // Gone before its death animation spawns a token, so nothing reaches the save file.
    Actor_Kill(actor);

    if (sSkullsFound < kSkullCount) {
        Navi(std::to_string(kSkullCount - sSkullsFound) + " MORE GOLD SKULLTULA" +
                 (kSkullCount - sSkullsFound == 1 ? "" : "S"),
             NA_SE_VO_NAVY_HEAR);
        return;
    }
    // All three: the song plays over the rounds until it ends.
    sSongPlayed = true;
    if (MusicOn()) {
        SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, 0, NA_BGM_GERUDO_VALLEY);
        sMusicPlaying = true;
    }
    Announce("THE SKULLTULAS SING", 5);
}

static void UpdateSkulls(PlayState* play) {
    for (size_t i = 0; i < sSkulls.size();) {
        Skull& skull = sSkulls[i];
        if (skull.actor == nullptr || skull.actor->update == nullptr) {
            sSkulls.erase(sSkulls.begin() + i);
            continue;
        }
        if (skull.actor->colChkInfo.health == 0 && skull.lastHealth != 0) {
            Actor* actor = skull.actor;
            sSkulls.erase(sSkulls.begin() + i);
            OnSkullFound(play, actor);
            continue;
        }
        skull.lastHealth = skull.actor->colChkInfo.health;
        i++;
    }
}

void Extras_OnActorDestroy(Actor* actor) {
    for (Skull& skull : sSkulls) {
        if (skull.actor == actor) {
            skull.actor = nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// Run lifecycle

void Extras_OnRunStart(PlayState* play) {
    sSkullsFound = 0;
    sSongPlayed = false;
    sLowHealthTimer = 0;
    PlaceSkulls(play);
}

void Extras_OnRunEnd(PlayState* play, bool linkIsDead) {
    ClearSkulls();
    if (sMusicPlaying) {
        StopMusic(30);
        if (!linkIsDead) {
            RestoreSceneMusic(play);
        }
    }
}

void Extras_OnSceneReload() {
    // Every actor was unloaded; the skulls that were left are lost for this run.
    sSkulls.clear();
    sMusicPlaying = false;
}

void Extras_Update(PlayState* play, Player* player) {
    UpdateSkulls(play);

    // Navi's low-health warning, like Black Ops' "you're down" callouts, but before it happens.
    if (sLowHealthTimer > 0) {
        sLowHealthTimer--;
    } else if (gSaveContext.health > 0 && gSaveContext.health <= 0x10 && gSaveContext.healthCapacity >= 0x30) {
        Navi("LISTEN! YOU NEED HEARTS!", NA_SE_VO_NAVY_CALL);
        sLowHealthTimer = kLowHealthCooldown;
    }
    (void)player;
}

} // namespace Zombies
