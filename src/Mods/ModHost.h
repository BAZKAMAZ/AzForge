#pragma once

#include "src/Mods/ModRegistry.h"

#include <stddef.h>
#include <vector>

namespace Forge
{
    // Owns the loaded mod DLLs.
    //
    // Mods come up in load order and go down in the reverse of the order they came up in,
    // so a mod that builds on another is always torn down while that other one is still
    // there. A mod whose DLL fails to load or whose AmodInit refuses does not stop the rest:
    // the failure is reported and the remaining mods continue, since one broken mod is not a
    // reason to disable everything a player installed.
    class ModHost
    {
    public:
        // Removes whatever a previous run left in the cache. A copy that is still mapped by
        // another running instance of the game cannot be removed and is left alone, which
        // is why the transient names carry a process id.
        static void                                                 SweepCache();

        static void                                                 LoadAll(const std::vector<ModRecord>& loadOrder);
        static void                                                 UnloadAll();

        static size_t                                               LoadedCount();
    };
}
