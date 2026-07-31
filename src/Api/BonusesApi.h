#pragma once

#include <BonusesApi.h>

#include <stdint.h>

namespace Forge
{
    struct ModContext;

    class BonusesApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodBonusesApi*                                Table();

        static const uint8_t*                                       FindType(const char* resourceId);

        static void                                                 ForgetMod(ModContext* owner);
    };
}
