#pragma once

#include <DatApi.h>

namespace Forge
{
    struct ModContext;

    class DatApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static void                                                 ForgetMod(ModContext* owner);

        static void                                                 ReplayStore(ModContext* owner);

        static const AmodDatApi*                                    Table();
    };
}
