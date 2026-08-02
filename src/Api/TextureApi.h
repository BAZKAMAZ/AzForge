#pragma once

#include <TextureApi.h>

#include <stdint.h>

namespace Forge
{
    struct ModContext;

    class TextureApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodTextureApi*                                Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
