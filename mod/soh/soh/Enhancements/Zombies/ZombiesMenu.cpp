// Zombies of Time: settings, under Enhancements > Zombies of Time in the SoH menu.

#include "ZombiesMode.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/SohGui/MenuTypes.h"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/SohGui.hpp"

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
}

namespace Zombies {

static const std::map<int32_t, const char*> kBuyButtons = {
    { 0, "L" },
    { 1, "D-pad Down" },
};

static void RegisterZombiesWidgets() {
    using namespace UIWidgets;

    SohGui::mSohMenu->AddSidebarEntry("Enhancements", "Zombies of Time", 2);
    WidgetPath path = { "Enhancements", "Zombies of Time", SECTION_COLUMN_1 };

    SohGui::mSohMenu->AddWidget(path, "Zombies of Time", WIDGET_SEPARATOR_TEXT);
    SohGui::mSohMenu->AddWidget(path, "Enable Zombies of Time", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ZOMBIES("Enabled"))
        .Options(CheckboxOptions().Tooltip(
            "Round-based undead survival. Stand anywhere with open ground and press the buy button to start.\n\n"
            "Undead come in waves that grow each round. Hits and kills earn points; spend them at the torches "
            "around you on weapons, perks, the Mystery Chest and Pack-a-Punch.\n\n"
            "Songs help: the Sun's Song turns the power on, the Song of Storms slows the horde and "
            "Zelda's Lullaby unseals Pack-a-Punch."));
    SohGui::mSohMenu->AddWidget(path, "Buy Button", WIDGET_CVAR_COMBOBOX)
        .CVar(CVAR_ZOMBIES("BuyButton"))
        .Options(ComboboxOptions()
                     .ComboMap(kBuyButtons)
                     .DefaultIndex(0)
                     .Tooltip("Button that starts a run and buys from a station."));
    SohGui::mSohMenu->AddWidget(path, "Keep Items After Run", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ZOMBIES("KeepItems"))
        .Options(CheckboxOptions().Tooltip(
            "Off: your inventory, health and magic go back to how they were when the run started.\n"
            "On: everything bought during the run stays on this save file."));
    SohGui::mSohMenu->AddWidget(path, "Best Round", WIDGET_TEXT).PreFunc([](WidgetInfo& info) {
        static std::string label;
        label = "Best run: " + std::to_string(CVarGetInteger(CVAR_ZOMBIES("BestRound"), 0)) + " rounds";
        info.name = label.c_str();
    });

    path.column = SECTION_COLUMN_2;
    SohGui::mSohMenu->AddWidget(path, "Difficulty", WIDGET_SEPARATOR_TEXT);
    SohGui::mSohMenu->AddWidget(path, "Max Undead Alive: %d", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_ZOMBIES("MaxAlive"))
        .Options(IntSliderOptions().Min(2).Max(24).DefaultValue(8).Format("%d").Tooltip(
            "How many undead can be up at once. Black Ops allows 24; OoT arenas feel crowded past 10."));
    SohGui::mSohMenu->AddWidget(path, "Health Scaling: %d%%", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_ZOMBIES("HealthScaling"))
        .Options(IntSliderOptions().Min(0).Max(300).Step(10).DefaultValue(100).Format("%d%%").Tooltip(
            "How fast undead health grows each round. 0% keeps every round at base health."));
    SohGui::mSohMenu->AddWidget(path, "Starting Points: %d", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_ZOMBIES("StartingPoints"))
        .Options(IntSliderOptions().Min(0).Max(10000).Step(100).DefaultValue(kStartingPoints).Format("%d"));
    SohGui::mSohMenu->AddWidget(path, "Starting Round: %d", WIDGET_CVAR_SLIDER_INT)
        .CVar(CVAR_ZOMBIES("StartingRound"))
        .Options(IntSliderOptions().Min(1).Max(50).DefaultValue(1).Format("%d"));
    SohGui::mSohMenu->AddWidget(path, "Wolfos Rounds", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ZOMBIES("WolfosRounds"))
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Every few rounds a pack of White Wolfos replaces the undead, like Black Ops hellhound rounds. "
            "The last one always drops Max Ammo."));
    SohGui::mSohMenu->AddWidget(path, "Barricades", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ZOMBIES("Barricades"))
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Undead come in through four boarded windows around the start point and tear the boards off "
            "before they can reach Link. Hold the buy button at a window to put boards back for 10 points each. "
            "Takes effect on the next run."));
    SohGui::mSohMenu->AddWidget(path, "Horde Pull", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ZOMBIES("HordePull"))
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Undead always shamble toward Link instead of waiting for him to come close."));
    SohGui::mSohMenu->AddWidget(path, "Redead Scream", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ZOMBIES("RedeadScream"))
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Redeads and Gibdos freeze Link with their scream, as in the base game."));
    SohGui::mSohMenu->AddWidget(path, "Force Night", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_ZOMBIES("ForceNight"))
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Sets the clock to midnight at the start of the run and of every round. "
            "Stalchildren only appear at night; day runs use Redeads instead."));
}

static RegisterMenuInitFunc menuInitFunc(RegisterZombiesWidgets);

} // namespace Zombies
