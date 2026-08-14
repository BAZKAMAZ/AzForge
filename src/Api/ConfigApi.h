#pragma once

#include <ConfigApi.h>

namespace Forge
{
    struct ModContext;

    class ConfigApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static void                                                 ForgetMod(ModContext* owner);

        static const AmodConfigApi*                                 Table();
    };
}
