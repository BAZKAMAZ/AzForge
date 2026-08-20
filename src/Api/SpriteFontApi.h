#pragma once

#include <SpriteFontApi.h>

namespace Forge
{
    struct ModContext;

    class SpriteFontApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodSpriteFontApi*                             Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
