#pragma once

#include <DemoApi.h>

namespace Forge
{
    struct ModContext;

    class DemoApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodDemoApi*                                   Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
