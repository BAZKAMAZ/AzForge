#pragma once

#include "src/Mods/ModRegistry.h"

#include <vector>

namespace Forge
{
    // Loads the mods on the game's own loading screen, and says so on it.
    //
    // The hook is Loading_SetProgress, and the moment is the step that reports 100: the game
    // has finished loading, the menu manager is built and every menu registered - both happen
    // between 40 and 55 - and the loading screen is still the state on screen. A mod loaded
    // here can therefore add a menu, read the world and take a frame, none of which existed
    // when the loader started.
    //
    // While the mods load, $wait_key is forced on so the game cannot fall through to the main
    // menu, and the $press_key line is rewritten after each one. The repaint is the game's own
    // Loading_SetProgress, called again with the same percentage - there is no second drawing
    // path to keep working.
    class ModLoadScreen
    {
    public:
        // The load order is held by reference: it belongs to the registry, which outlives
        // this, and copying it would leave the two able to disagree.
        static bool                                                 Install(const std::vector<ModRecord>& loadOrder);
        static void                                                 Uninstall();
    };
}
