#pragma once

#include "src/Mods/ModRegistry.h"

#include <stdint.h>
#include <string>
#include <vector>

namespace Forge
{
    enum class ModFileResult
    {
        NotFound,
        Loaded,
        Failed
    };

    class ModFileSystem
    {
    public:
        static void                                                 Build(const std::vector<ModRecord>& loadOrder);
        static void                                                 Clear();

        static ModFileResult                                        Read(const char* resourcePath, std::vector<uint8_t>& out, std::string& providerId, std::string& error);

        static std::string                                          IconResource(const ModRecord& record);

        static size_t                                               FileCount();
    };
}
