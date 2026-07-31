#pragma once

#include <ModManagerApi.h>

namespace Forge
{
    struct ModContext;

    class ModManagerApi
    {
    public:
        static bool                                                 Install();

        static const AmodModManagerApi*                             Table();
        static void                                                 Clear();
    };
}
