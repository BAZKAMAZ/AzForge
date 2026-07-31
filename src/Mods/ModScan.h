#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace Forge
{
    class ModScan
    {
    public:
        static void                                                 Initialize();
        static void                                                 Shutdown();

        static bool                                                 IsClean(const std::string& modId, const std::vector<uint8_t>& payload, std::string& reason);
    };
}
