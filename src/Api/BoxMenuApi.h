#pragma once

#include <BoxMenuApi.h>

namespace Forge
{
    struct ModContext;

    class BoxMenuApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static void                                                 ForgetMod(ModContext* owner);

        static const AmodBoxMenuApi*                                Table();
    };
}
