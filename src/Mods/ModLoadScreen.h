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

        // What a mod being started on this screen may do to it: say where it has got to, and have
        // it painted there and then. Up() is false anywhere else, and the rest do nothing.
        static bool                                                 Up();
        static void                                                 Step(int percent, const char* line);

        static bool                                                 Install(const std::vector<ModRecord>& loadOrder);
        static bool                                                 Enter();
    };
}
