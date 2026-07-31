#pragma once

#include <InventoryApi.h>

namespace Forge
{
    struct ModContext;

    struct ModContext;

    class InventoryApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodInventoryApi*                              Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
