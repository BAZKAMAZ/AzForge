#pragma once

#include <InputApi.h>

namespace Forge
{
    struct ModContext;

    class InputApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodInputApi*                                  Table();

        static bool                                                 HoldKeyboard(ModContext* owner, bool hold);

        static void                                                 ForgetMod(ModContext* owner);

        static void                                                 HoldSystemCursor(bool hold);

        static void                                                 FillKeysIfStalled();

        static int                                                  WheelDelta();

        static void                                                 ReplayStore(ModContext* owner);

        static void                                                 BeforeConfigWrite();
        static void                                                 AfterConfigWrite();
    };
}
