#pragma once

// Black Ops' third-person player animations for Link (bomap playeranims): the body
// animations a player holding a gun plays (standing, aiming, walking and running each
// way) and the torso animations played over them (firing, reloading, raising and putting
// a gun away), per weapon kind. The game retargets them onto Link's skeleton at run time
// (ZombiesPlayerAnim.cpp); the player skeleton they were made for is written too, bones
// only, as the reference bind pose.

#include <string>
#include <vector>

namespace bomap {

struct PlayerAnimRole {
    const char* role;             // manifest key, e.g. "player_rifle_walk_f"
    std::vector<const char*> names; // xanims to try, in order
};

// The roles the game reads, each with the Black Ops animations that can fill it.
const std::vector<PlayerAnimRole>& PlayerAnimRoles();

// The manifest with every "player_" line replaced by `lines` (other roles are kept).
std::string MergeManifest(const std::string& oldManifest, const std::vector<std::string>& lines);

} // namespace bomap
