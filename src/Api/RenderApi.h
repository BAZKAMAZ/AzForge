#pragma once

#include <RenderApi.h>

namespace Forge
{
    struct ModContext;

    class RenderApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodRenderApi*                                 Table();

        static void                                                 ForgetMod(ModContext* owner);

        static void                                                 AddInternalPresent(void(__cdecl* draw)());
    };
}
