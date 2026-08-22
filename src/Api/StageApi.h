#pragma once

#include <StageApi.h>

namespace Forge
{
    struct ModContext;

    class StageApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodStageApi*                                  Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
