#pragma once

#include <filesystem>

namespace Forge
{
    // Every path the loader uses is derived from the location of AzForge.dll, which sits
    // next to game.exe. Deriving from the module rather than the working directory keeps
    // the layout correct when the game is started through a shortcut.
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
