#pragma once

#include <CtrlsApi.h>

namespace Forge
{
    struct ModContext;

    class CtrlsApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static void                                                 ForgetMod(ModContext* owner);

        static const AmodCtrlsApi*                                  Table();
    };
}
