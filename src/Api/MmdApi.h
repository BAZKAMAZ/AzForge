#pragma once

#include <MmdApi.h>

#include <stdint.h>

namespace Forge
{
    struct ModContext;

    class MmdApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodMmdApi*                                    Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
