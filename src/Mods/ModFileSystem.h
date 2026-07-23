#pragma once

#include "src/Mods/ModRegistry.h"

#include <stdint.h>
#include <string>
#include <vector>

namespace Forge
{
    enum class ModFileResult
    {
        NotFound,   // no mod claims this resource, the game should read its own copy
        Loaded,     // out holds the replacement
        Failed      // a mod claims the resource but its archive could not be read
    };

    // Index of every resource the loaded mods replace, built once at boot.
    //
    // Lookup is case-insensitive and slash-agnostic, matching the original: the pak branch
    // in Package_FindEntryInPak @0x00422b60 lowercases the requested name before comparing,
    // and the disk branch goes through fopen, which is case-insensitive on Windows.
    //
    // When several mods provide the same resource the later one in the load order wins, and
    // the override is reported at boot so a shadowed mod is never a silent surprise.
    class ModFileSystem
    {
    public:
        static void                                                 Build(const std::vector<ModRecord>& loadOrder);
        static void                                                 Clear();

        static ModFileResult                                        Read(const char* resourcePath, std::vector<uint8_t>& out, std::string& providerId, std::string& error);

        static size_t                                               FileCount();
    };
}
