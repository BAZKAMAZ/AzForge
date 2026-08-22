#pragma once

#include <ModManagerApi.h>

#include <string>

namespace Forge
{
    struct ModContext;

    class ModManagerApi
    {
    public:
        static bool                                                 Install();

        static const AmodModManagerApi*                             Table();
        static void                                                 Clear();

        static int                                                  Find(const char* id);
        static bool                                                 SetEnabled(int index, bool enabled, std::string& error);
    };
}
