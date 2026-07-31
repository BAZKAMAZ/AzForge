#pragma once

#include "src/Mods/ModRegistry.h"

#include <stddef.h>
#include <string>
#include <vector>

namespace Forge
{
    enum class ModPhase
    {
        BeforeLauncher,
        LoadingScreen
    };

    class ModHost
    {
    public:
        static void                                                 SweepCache();

        static void                                                 Load(const std::vector<ModRecord>& loadOrder, ModPhase phase, void (*afterEach)());
        static void                                                 UnloadAll();
        static bool                                                 Reload(const ModRecord& record, std::string& error);

        static size_t                                               LoadedCount();
        static size_t                                               FailedCount();
        static size_t                                               CodeModCount(const std::vector<ModRecord>& loadOrder);

        static bool                                                 IsLoaded(const std::string& id);
        static const char* FailureFor(const std::string& id);
    };
}