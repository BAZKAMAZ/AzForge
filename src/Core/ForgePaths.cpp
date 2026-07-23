#include "pch.h"

#include "src/Core/ForgePaths.h"

namespace Forge
{
    namespace
    {
        std::filesystem::path g_gameDirectory;
        std::filesystem::path g_forgeModuleFile;
        std::filesystem::path g_hostExecutableFile;
        std::filesystem::path g_modsDirectory;
        std::filesystem::path g_cacheDirectory;
        std::filesystem::path g_logFile;
        bool g_initialized = false;

        bool QueryModuleFile(HMODULE module, std::filesystem::path& out)
        {
            std::wstring buffer(MAX_PATH, L'\0');

            for (;;)
            {
                const DWORD copied = GetModuleFileNameW(module, buffer.data(),
                                                        static_cast<DWORD>(buffer.size()));
                if (copied == 0)
                {
                    return false;
                }

                if (copied < buffer.size())
                {
                    buffer.resize(copied);
                    out = std::filesystem::path(buffer);
                    return true;
                }

                if (buffer.size() >= 32768)
                {
                    return false;
                }

                buffer.resize(buffer.size() * 2);
            }
        }
    }

    bool Paths::Initialize(HMODULE forgeModule)
    {
        if (g_initialized)
        {
            return true;
        }

        if (!QueryModuleFile(forgeModule, g_forgeModuleFile))
        {
            return false;
        }

        if (!QueryModuleFile(nullptr, g_hostExecutableFile))
        {
            return false;
        }

        g_gameDirectory = g_forgeModuleFile.parent_path();
        g_modsDirectory = g_gameDirectory / L"mods";
        g_cacheDirectory = g_modsDirectory / L".cache";
        g_logFile = g_gameDirectory / L"forge.log";

        g_initialized = true;
        return true;
    }

    const std::filesystem::path& Paths::GameDirectory()
    {
        return g_gameDirectory;
    }

    const std::filesystem::path& Paths::ForgeModuleFile()
    {
        return g_forgeModuleFile;
    }

    const std::filesystem::path& Paths::HostExecutableFile()
    {
        return g_hostExecutableFile;
    }

    const std::filesystem::path& Paths::ModsDirectory()
    {
        return g_modsDirectory;
    }

    const std::filesystem::path& Paths::CacheDirectory()
    {
        return g_cacheDirectory;
    }

    const std::filesystem::path& Paths::LogFile()
    {
        return g_logFile;
    }
}
