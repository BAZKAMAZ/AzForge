#pragma once

#include <BoxMenuApi.h>

namespace Forge
{
    class BoxMenuApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodBoxMenuApi*                                Table();
    };
}
