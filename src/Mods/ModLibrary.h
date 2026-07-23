#pragma once

#include "src/Api/ModContext.h"
#include "src/Mods/ModRegistry.h"

#include <filesystem>
#include <string>

namespace Forge
{
    // One loaded mod DLL.
    //
    // The DLL cannot be handed to the Windows loader from inside the archive, and mapping a
    // PE by hand would take the module out of the loader's list, which is exactly where a
    // debugger looks for modules and their symbols. So the entry is written out and loaded
    // by path, marked temporary so the file system keeps it in cache rather than committing
    // it to disk where it can.
    //
    // Removal is done explicitly, after FreeLibrary. FILE_FLAG_DELETE_ON_CLOSE is set as
    // well, but it is only a backstop for the paths where the image never got mapped:
    // Windows will not unlink a file that still backs a loaded image, so a copy left behind
    // by a crash survives until the next run sweeps the cache.
    //
    // The file name carries the process id so that two copies of the game running at once
    // do not fight over it, and so that a sweep can tell a leftover from a live file.
    //
    // Instances must not move once loaded: the mod holds a pointer to the table inside
    // m_context, and that table's self field points back at the context.
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
