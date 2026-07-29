#pragma once

#include "src/Mods/ModRegistry.h"

#include <vector>

namespace Forge
{
    // Loads the mods on the game's own loading screen, and says so on it.
    //
    // The hook is Menu_RegisterAll, which the startup load calls once the menu manager exists
    // and before one menu is registered. A mod loaded there hears every menu the game builds,
    // and the loading screen is on screen for the whole of it.
    //
    // The repaint is the game's own Loading_SetProgress, called again with the percentage the
    // bar already stands at - there is no second drawing path to keep working.
    class ModLoadScreen
    {
    public:
        // The load order is held by reference: it belongs to the registry, which outlives
        // this, and copying it would leave the two able to disagree.
        static bool                                                 Install(const std::vector<ModRecord>& loadOrder);
        static void                                                 Uninstall();

        // Borrowing the screen after the game has started, for a mod being switched on or off.
        // Enter puts the game into the loading state and remembers what it was in; Leave puts
        // that back. Report writes the line along the bottom and draws a frame.
        //
        // The pair belongs on the loader's own frame tick: entering a state runs the leave and
        // enter of two game states, which is not something to do underneath a mod.
        static bool                                                 Enter();
        static void                                                 Leave();
        static void                                                 Report(const char* line);
    };
}
