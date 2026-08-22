#pragma once

#include <AzmApi.h>

namespace Forge
{
    struct ModContext;

    class AzmApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodAzmApi*                                    Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
