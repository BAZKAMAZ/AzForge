#pragma once

#include <ModManagerApi.h>

namespace Forge
{
    struct ModContext;

    // Loader side of the mod manager.
    //
    // The table is built once and handed only to the mod marked privileged - core.amod, loaded
    // from beside AzForge.dll. Every other mod gets a null, which is the same way every other
    // subsystem says it is not there.
    class ModManagerApi
    {
    public:
        // Registers the frame tick that applies reloads. It runs before any mod's own update,
        // which is the one point in the frame where no mod is on the stack and a DLL can be
        // let go of.
        static bool                                                 Install();

        static const AmodModManagerApi*                             Table();
        static void                                                 Clear();
    };
}
