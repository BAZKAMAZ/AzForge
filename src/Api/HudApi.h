#pragma once

#include <HudApi.h>

namespace Forge
{
    struct ModContext;

    class HudApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodHudApi*                                    Table();

        // A HUD a mod changed goes back to what the script said when the mod goes away.
        static void                                                 ForgetMod(ModContext* owner);
    };
}
