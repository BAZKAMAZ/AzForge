#pragma once

#include <PlayerApi.h>

namespace Forge
{
    struct ModContext;

    class PlayerApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodPlayerApi*                                 Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
