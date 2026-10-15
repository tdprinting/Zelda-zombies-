#pragma once

// Zombies of Time: the Mystery Box drawn with the player's own Black Ops models
// (ZombiesBox.cpp). Where the box's models were imported, the Mystery Chest station
// plays Black Ops' box: the lid opens, guns cycle up out of it for about four seconds,
// the prize then waits about twelve seconds to be taken, and after enough uses the
// teddy bear comes up and the box flies off to another spot. Without the import the
// station keeps Ocarina of Time's golden torch and hands the prize over at once.

#include <functional>
#include <string>

// GameInteractor.h pulls in z64.h with the C++ headers it depends on already included.
#include "soh/Enhancements/game-interactor/GameInteractor.h"

namespace Zombies {

struct BoxPrize {
    std::string label;               // shown in the take prompt
    const char* gunBoName = nullptr; // a gun's Black Ops name: its world model floats up
    u8 item = 0;                     // otherwise an Ocarina of Time item (ITEM_*) floats up
    std::function<void(PlayState*)> give;
};

// The imported box is drawn at the chest, so the chest plays the box's cycle.
bool Box_Active();
// The buyer paid. roll() runs as the cycle ends: true with the prize filled in, or false
// when the bear comes up; then move() runs once the box has flown away.
void Box_Start(PlayState* play, int who, std::function<bool(BoxPrize*)> roll, std::function<void(PlayState*)> move);
// While the box is in use the chest isn't for sale. Fills the prompt for this player
// (the buyer takes the prize with their buy button) and returns true; false when idle.
bool Box_Busy(int who, const std::string& button, std::string* prompt);
// The buyer pressed their buy button at the box.
void Box_Take(PlayState* play, int who);
// Drawn with the world, once per view.
void Box_Draw(PlayState* play);

// From ZombiesStations.cpp: where the chest is and its torch, when it's placed.
bool Stations_Chest(Vec3f* pos, Actor** torch);

} // namespace Zombies
