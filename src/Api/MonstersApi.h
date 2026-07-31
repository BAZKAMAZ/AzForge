#pragma once

#include <MonstersApi.h>

#include <stdint.h>

namespace Forge
{
    struct ModContext;

    class MonstersApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodMonstersApi*                               Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
