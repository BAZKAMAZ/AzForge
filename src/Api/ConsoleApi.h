#pragma once

#include <ConsoleApi.h>

namespace Forge
{
    struct ModContext;

    class ConsoleApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodConsoleApi*                                Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
