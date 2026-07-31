#pragma once

#include <string>

namespace Forge
{
    class ModState
    {
    public:
        static void                                                 Load();
        static void                                                 Save();
        static void                                                 Clear();

        static bool                                                 IsEnabled(const std::string& id);
        static bool                                                 SetEnabled(const std::string& id, bool enabled);

    };
}
