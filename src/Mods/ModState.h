#pragma once

#include <string>

namespace Forge
{
    // Which mods the player has switched off, remembered between runs.
    //
    // The file is a line per mod, "<id> 0" or "<id> 1", written next to the mods themselves.
    // It is deliberately something a person can read and repair in a text editor: a player
    // whose game will not start because of a mod needs a way back in that does not require
    // the game to start.
    //
    // A mod not named in the file is enabled. That way the file only ever grows when someone
    // switches something off, and installing a mod does not require touching it.
    //
    // Switching a mod off takes effect on the next start, not at once. Unloading a mod while
    // the game is running is a separate problem: the loader can take back everything the mod
    // registered, but not what the game has since done with it.
    class ModState
    {
    public:
        static void                                                 Load();
        static void                                                 Save();

        static bool                                                 IsEnabled(const std::string& id);

        // Records the wish. Returns false when nothing changed, so a caller can avoid
        // rewriting the file for a switch that was already in that position.
        static bool                                                 SetEnabled(const std::string& id, bool enabled);

        static void                                                 Clear();
    };
}
