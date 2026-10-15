#include "player_anims.h"

#include <sstream>

namespace bomap {

// Body roles come in two families, as Black Ops' own: one-handed (pistol, which the Ray
// Gun uses too) and two-handed (rifle). Torso roles are per weapon kind where Black Ops
// has one (smg, shotgun, lmg); the game falls back to the rifle's when a kind is missing.
const std::vector<PlayerAnimRole>& PlayerAnimRoles() {
    static const std::vector<PlayerAnimRole> kRoles = {
        // Standing with the gun up, and aiming down the sights.
        { "player_pistol_idle", { "pb_stand_alert_pistol" } },
        { "player_rifle_idle", { "pb_stand_alert" } },
        { "player_pistol_ads", { "pb_stand_ads_pistol" } },
        { "player_rifle_ads", { "pb_stand_ads" } },
        // Walking (aiming pace) and running each way: f(orward), b(ack), l(eft), r(ight).
        { "player_pistol_walk_f", { "pb_combatwalk_forward_loop_pistol" } },
        { "player_pistol_walk_b", { "pb_combatwalk_back_loop_pistol" } },
        { "player_pistol_walk_l", { "pb_combatwalk_left_loop_pistol" } },
        { "player_pistol_walk_r", { "pb_combatwalk_right_loop_pistol" } },
        { "player_rifle_walk_f", { "pb_stand_shoot_walk_forward", "pb_rifle_stand_shuffle_f" } },
        { "player_rifle_walk_b", { "pb_stand_shoot_walk_back", "pb_rifle_stand_shuffle_b" } },
        { "player_rifle_walk_l", { "pb_stand_shoot_walk_left", "pb_rifle_stand_shuffle_l" } },
        { "player_rifle_walk_r", { "pb_stand_shoot_walk_right", "pb_rifle_stand_shuffle_r" } },
        { "player_pistol_run_f", { "pb_combatrun_forward_loop_pistol", "pb_pistol_run_fast" } },
        { "player_pistol_run_b", { "pb_combatrun_back_loop_pistol" } },
        { "player_pistol_run_l", { "pb_combatrun_left_loop_pistol" } },
        { "player_pistol_run_r", { "pb_combatrun_right_loop_pistol" } },
        { "player_rifle_run_f", { "pb_combatrun_forward_loop" } },
        { "player_rifle_run_b", { "pb_combatrun_back_loop" } },
        { "player_rifle_run_l", { "pb_combatrun_left_loop" } },
        { "player_rifle_run_r", { "pb_combatrun_right_loop" } },
        { "player_pistol_sprint", { "pb_sprint_pistol" } },
        { "player_rifle_sprint", { "pb_sprint" } },
        // Torso: a shot (hip and aimed), a reload, raising a gun and putting it away.
        { "player_pistol_fire", { "pt_stand_shoot_pistol" } },
        { "player_pistol_fire_ads", { "pt_stand_shoot_ads_pistol" } },
        { "player_rifle_fire", { "pt_stand_shoot", "pt_rifle_fire" } },
        { "player_rifle_fire_ads", { "pt_stand_shoot_ads", "pt_rifle_fire_ads" } },
        { "player_smg_fire", { "pt_stand_shoot_auto" } },
        { "player_smg_fire_ads", { "pt_stand_shoot_auto_ads" } },
        { "player_shotgun_fire", { "pt_stand_shoot_shotgun" } },
        { "player_pistol_reload", { "pt_reload_stand_pistol" } },
        { "player_rifle_reload", { "pt_reload_stand_rifle" } },
        { "player_smg_reload", { "pt_reload_stand_auto", "pt_reload_stand_auto_mp40" } },
        { "player_shotgun_reload", { "pt_shot_stand_reload" } },
        { "player_lmg_reload", { "pt_lmg_stand_reload" } },
        { "player_pistol_raise", { "pt_pistol_stand_raise" } },
        { "player_pistol_putaway", { "pt_pistol_stand_putaway" } },
        { "player_rifle_raise", { "pt_rifle_stand_raise" } },
        { "player_rifle_putaway", { "pt_rifle_stand_putaway" } },
    };
    return kRoles;
}

std::string MergeManifest(const std::string& oldManifest, const std::vector<std::string>& lines) {
    std::istringstream in(oldManifest);
    std::string out, line;
    while (std::getline(in, line)) {
        if (line.rfind("player_", 0) == 0 || line.rfind("# player animations", 0) == 0) {
            continue;
        }
        out += line + "\n";
    }
    if (!lines.empty()) {
        out += "# player animations (bomap playeranims)\n";
        for (const std::string& l : lines) {
            out += l + "\n";
        }
    }
    return out;
}

} // namespace bomap
