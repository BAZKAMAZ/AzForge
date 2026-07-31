#pragma once

#include <LauncherApi.h>

namespace Forge
{
    struct ModContext;

    class LauncherApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodLauncherApi*                               Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
