#pragma once

#include <InputApi.h>

namespace Forge
{
    class InputApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodInputApi*                                  Table();
    };
}
