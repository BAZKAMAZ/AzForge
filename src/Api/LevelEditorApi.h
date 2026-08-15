#pragma once

#include <LevelEditorApi.h>

namespace Forge
{
    struct ModContext;

    class LevelEditorApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodLevelEditorApi*                            Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
