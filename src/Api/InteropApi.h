#pragma once

#include <InteropApi.h>

namespace Forge
{
    struct ModContext;

    class InteropApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodInteropApi*                                Table();

        // Removes every hook this mod installed. Runs before the mod's DLL is unmapped.
        static void                                                 ForgetMod(ModContext* owner);
    };
}
