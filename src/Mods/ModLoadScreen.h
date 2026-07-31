#pragma once

#include "src/Mods/ModRegistry.h"

#include <vector>

namespace Forge
{
    class ModLoadScreen
    {
    public:
        static void                                                 Uninstall();
        static void                                                 Leave();
        static void                                                 Report(const char* line);

        static bool                                                 Install(const std::vector<ModRecord>& loadOrder);
        static bool                                                 Enter();
    };
}
