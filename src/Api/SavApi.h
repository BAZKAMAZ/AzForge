#pragma once

#include <SavApi.h>

namespace Forge
{
    struct ModContext;

    class SavApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static void                                                 ForgetMod(ModContext* owner);

        static void                                                 ReplayStore(ModContext* owner);

        static const AmodSavApi*                                    Table();
    };
}
