#pragma once

#include <windows.h>

#include <filesystem>

namespace Forge
{
    class Paths
    {
    public:
        static bool                                                 Initialize(HMODULE forgeModule);

        static const std::filesystem::path&                         GameDirectory();
        static const std::filesystem::path&                         ForgeModuleFile();
        static const std::filesystem::path&                         HostExecutableFile();
        static const std::filesystem::path&                         ModsDirectory();
        static const std::filesystem::path&                         CacheDirectory();
        static const std::filesystem::path&                         LogFile();
    };
}
