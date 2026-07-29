#pragma once

#include "src/Mods/ModRegistry.h"

#include <stddef.h>
#include <string>
#include <vector>

namespace Forge
{
    // When a mod's code is loaded.
    //
    // A mod's files and its code are two different things and they are not needed at the same
    // time. The files have to be in place before the game reads a resource, which happens from
    // the moment data.pak is opened, so the override table is always built up front. The code
    // does not: almost every mod is interested in the menus, the world or the frame, none of
    // which exist yet when the loader starts.
    //
    // So the code is loaded on the loading screen, where the wait is visible and where the
    // player can be told what is happening. The exception is a mod that changes the launcher,
    // which is long gone by then - it declares "launcher" and is loaded before it.
    enum class ModPhase
    {
        BeforeLauncher, // only the mods that declared they change the launcher
        LoadingScreen   // everything else, with the loading screen held open for it
    };

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

        // Loads the mods belonging to this phase. afterEach, when given, runs once per mod
        // and is how the loading screen repaints itself between them.
        static void                                                 Load(const std::vector<ModRecord>& loadOrder, ModPhase phase, void (*afterEach)());

        static void                                                 UnloadAll();

        // Brings one mod into step with its switch, in this run: loads it if it is on and is
        // not up, takes it down if it is off and is. Answers false with a reason when it could
        // not, which is a mod that has to be up before the launcher or a load that failed.
        //
        // The caller is responsible for calling this where no mod is on the stack: taking a
        // mod down frees its DLL, and a callback of its own running at the time would return
        // into nothing.
        static bool                                                 Reload(const ModRecord& record, std::string& error);

        // Counted across both phases, so the loading screen can report the whole picture
        // rather than only what it loaded itself.
        static size_t                                               LoadedCount();
        static size_t                                               FailedCount();

        // How many mods in this order carry code at all, in either phase.
        static size_t                                               CodeModCount(const std::vector<ModRecord>& loadOrder);

        // What the mod screen needs to say about this run: which mods came up, and why one
        // did not. Keyed by id rather than by index, because the load order is not the
        // order anything is displayed in.
        static bool                                                 IsLoaded(const std::string& id);
        static const char*                                          FailureFor(const std::string& id);
    };
}
