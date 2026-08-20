#pragma once

#include <InputApi.h>

namespace Forge
{
    struct ModContext;

    class InputApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodInputApi*                                  Table();

        static void                                                 ForgetMod(ModContext* owner);

        static void                                                 ReplayStore(ModContext* owner);

        static void                                                 BeforeConfigWrite();
        static void                                                 AfterConfigWrite();
    };
}
