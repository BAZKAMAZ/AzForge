#pragma once

#include <windows.h>

#include "src/Api/ModContext.h"
#include "src/Mods/ModRegistry.h"

#include <filesystem>
#include <string>

namespace Forge
{
    class ModLibrary
    {
    public:
        ModLibrary() = default;
        ~ModLibrary();

        ModLibrary(const ModLibrary&) = delete;
        ModLibrary& operator=(const ModLibrary&) = delete;

        bool                                                        Load(const ModRecord& record, std::string& error);
        void                                                        Unload();

        bool                                                        IsLoaded() const;
        const std::string&                                          Id() const;

    private:
        bool                                                        WriteTransientCopy(const std::filesystem::path& path, const std::vector<uint8_t>& bytes, std::string& error);
        void                                                        ReleaseTransientFile();

        ModContext m_context;
        HMODULE m_module = nullptr;
        HANDLE m_transientFile = INVALID_HANDLE_VALUE;
        std::filesystem::path m_transientPath;
        void(__cdecl* m_shutdown)() = nullptr;
        bool m_initialised = false;
    };
}
