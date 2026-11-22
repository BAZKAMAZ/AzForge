#pragma once

#include <ObjectsMenuApi.h>

namespace Forge
{
    struct ModContext;

    class ObjectsMenuApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodObjectsMenuApi*                            Table();

        static void                                                 ForgetMod(ModContext* owner);

        static bool                                                 AskMenu(const char* ownId, bool overlay);
        static bool                                                 AskBack();
    };
}
