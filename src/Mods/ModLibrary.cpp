#include "pch.h"

#include "src/Mods/ModLibrary.h"

#include "src/Api/BlockExpressionApi.h"
#include "src/Api/BoxMenuApi.h"
#include "src/Api/ConsoleApi.h"
#include "src/Api/FrameApi.h"
#include "src/Api/InteropApi.h"
#include "src/Api/LauncherApi.h"
#include "src/Api/ObjectsMenuApi.h"
#include "src/Api/RenderApi.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Core/ForgeSubsystems.h"
#include "src/Core/ForgeText.h"

#include <stdio.h>

namespace Forge
{
    namespace
    {
        typedef int(__cdecl* InitRoutine)(const AmodApi* api);
        typedef void(__cdecl* ShutdownRoutine)();

        std::string DescribeLoadFailure(DWORD code)
        {
            switch (code)
            {
            case ERROR_BAD_EXE_FORMAT:
                return "the DLL is not a 32-bit module; mods must be built for Win32";
            case ERROR_MOD_NOT_FOUND:
                return "the DLL depends on a library that is not present; build mods with the "
                       "static runtime (/MT)";
            default:
                break;
            }

            char buffer[96];
            snprintf(buffer, sizeof(buffer), "the DLL could not be loaded (error %lu)", code);
            return buffer;
        }

        std::string DescribeInitResult(int result)
        {
            switch (result)
            {
            case AMOD_ERROR_API_VERSION:
                return "the mod needs a newer AzForge than this one";
            case AMOD_ERROR:
                return "the mod reported a failure";
            default:
                break;
            }

            char buffer[64];
            snprintf(buffer, sizeof(buffer), "the mod returned %d from AmodInit", result);
            return buffer;
        }
    }

    ModLibrary::~ModLibrary()
    {
        Unload();
    }

    bool ModLibrary::WriteTransientCopy(const std::filesystem::path& path,
                                        const std::vector<uint8_t>& bytes, std::string& error)
    {
        std::error_code creation;
        std::filesystem::create_directories(path.parent_path(), creation);
        if (creation)
        {
            error = "cannot create the cache directory: " + creation.message();
            return false;
        }

        const HANDLE writer = CreateFileW(path.c_str(), GENERIC_WRITE,
                                          FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                          CREATE_ALWAYS,
                                          FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN, nullptr);
        if (writer == INVALID_HANDLE_VALUE)
        {
            char buffer[96];
            snprintf(buffer, sizeof(buffer), "cannot create the transient copy (error %lu)", GetLastError());
            error = buffer;
            return false;
        }

        DWORD written = 0;
        const bool ok = !bytes.empty() &&
                        WriteFile(writer, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                        written == bytes.size();
        CloseHandle(writer);

        if (!ok)
        {
            error = "cannot write the transient copy";
            std::error_code removal;
            std::filesystem::remove(path, removal);
            return false;
        }

        // Reopened purely to hold the delete-on-close reference. Sharing is left wide open
        // so the module loader can map the file while this handle stays alive.
        m_transientFile = CreateFileW(path.c_str(), GENERIC_READ | DELETE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (m_transientFile == INVALID_HANDLE_VALUE)
        {
            char buffer[96];
            snprintf(buffer, sizeof(buffer), "cannot hold the transient copy (error %lu)", GetLastError());
            error = buffer;
            std::error_code removal;
            std::filesystem::remove(path, removal);
            return false;
        }

        m_transientPath = path;
        return true;
    }

    void ModLibrary::ReleaseTransientFile()
    {
        if (m_transientFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_transientFile);
            m_transientFile = INVALID_HANDLE_VALUE;
        }

        if (!m_transientPath.empty())
        {
            // Closing the last handle is what removes the file. Deleting explicitly covers
            // the case where the module loader still held it a moment ago.
            std::error_code removal;
            std::filesystem::remove(m_transientPath, removal);
            m_transientPath.clear();
        }
    }

    bool ModLibrary::Load(const ModRecord& record, std::string& error)
    {
        error.clear();

        if (m_module != nullptr)
        {
            error = "the mod is already loaded";
            return false;
        }

        if (record.manifest.entry.empty())
        {
            error = "the manifest declares no entry";
            return false;
        }

        // Checked before the DLL is copied out and mapped: a mod that cannot work here should
        // cost nothing and say why, rather than load and discover a null table it may never
        // have checked for.
        for (const std::string& subsystem : record.manifest.requiredSubsystems)
        {
            if (!Subsystems::IsAvailable(subsystem))
            {
                error = "the mod requires the '" + subsystem +
                        "' subsystem, which this loader did not install";
                return false;
            }
        }

        std::vector<uint8_t> bytes;
        if (!record.archive->ReadByName(record.manifest.entry, bytes, error))
        {
            return false;
        }

        if (bytes.empty())
        {
            error = "the entry is empty";
            return false;
        }

        wchar_t suffix[24] = {};
        _snwprintf_s(suffix, _TRUNCATE, L".%lu.dll", GetCurrentProcessId());

        const std::filesystem::path path =
            Paths::CacheDirectory() / (FromUtf8(record.manifest.id) + suffix);

        if (!WriteTransientCopy(path, bytes, error))
        {
            return false;
        }

        m_module = LoadLibraryW(path.c_str());
        if (m_module == nullptr)
        {
            error = DescribeLoadFailure(GetLastError());
            ReleaseTransientFile();
            return false;
        }

        const InitRoutine init = reinterpret_cast<InitRoutine>(GetProcAddress(m_module, "AmodInit"));
        m_shutdown = reinterpret_cast<ShutdownRoutine>(GetProcAddress(m_module, "AmodShutdown"));

        if (init == nullptr || m_shutdown == nullptr)
        {
            error = "the DLL does not export both AmodInit and AmodShutdown";
            Unload();
            return false;
        }

        m_context.id = record.manifest.id;
        m_context.version = record.manifest.version;
        BuildApiTable(m_context);

        const int result = init(&m_context.table);
        if (result != AMOD_OK)
        {
            error = DescribeInitResult(result);
            m_shutdown = nullptr; // AmodInit failed, so AmodShutdown must not be called
            Unload();
            return false;
        }

        // Optional, and resolved only once the mod is up: a mod that exports AmodUpdate is
        // driven every frame until it is unloaded.
        FrameApi::Register(&m_context,
            reinterpret_cast<AmodUpdateRoutine>(GetProcAddress(m_module, "AmodUpdate")));

        m_initialised = true;
        return true;
    }

    void ModLibrary::Unload()
    {
        // Released before AmodShutdown, so nothing the loader holds calls into code that is
        // about to unload.
        FrameApi::ForgetMod(&m_context);
        LauncherApi::ForgetMod(&m_context);
        ObjectsMenuApi::ForgetMod(&m_context);
        BoxMenuApi::ForgetMod(&m_context);
        BlockExpressionApi::ForgetMod(&m_context);
        ConsoleApi::ForgetMod(&m_context);
        RenderApi::ForgetMod(&m_context);
        WorldApi::ForgetMod(&m_context);

        // Last of the release calls and before anything unmaps this mod: a detour of the
        // mod's own points into the DLL that is about to go, so the game's code has to stop
        // referring to it first.
        InteropApi::ForgetMod(&m_context);
        m_context.owned.ReleaseAll();

        if (m_initialised && m_shutdown != nullptr)
        {
            m_shutdown();
        }

        m_initialised = false;
        m_shutdown = nullptr;

        if (m_module != nullptr)
        {
            FreeLibrary(m_module);
            m_module = nullptr;
        }

        ReleaseTransientFile();

        m_context.id.clear();
        m_context.version.clear();
        m_context.table = {};
    }

    bool ModLibrary::IsLoaded() const
    {
        return m_module != nullptr;
    }

    const std::string& ModLibrary::Id() const
    {
        return m_context.id;
    }
}
