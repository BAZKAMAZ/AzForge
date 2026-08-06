#pragma once

#include <SoundApi.h>

namespace Forge
{
    struct ModContext;

    class SoundApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodSoundApi*                                  Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
