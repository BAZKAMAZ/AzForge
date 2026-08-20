#include "src/Mods/ModLibrary.h"
#include "src/Api/BlockExpressionApi.h"
#include "src/Api/BoxMenuApi.h"
#include "src/Api/ConsoleApi.h"
#include "src/Api/FrameApi.h"
#include "src/Api/InteropApi.h"
#include "src/Api/LauncherApi.h"
#include "src/Api/ObjectsMenuApi.h"
#include "src/Api/RenderApi.h"
#include "src/Api/BonusesApi.h"
#include "src/Api/CameraApi.h"
#include "src/Api/ParticlesApi.h"
#include "src/Api/FilesApi.h"
#include "src/Api/ConfigApi.h"
#include "src/Api/DatApi.h"
#include "src/Api/SavApi.h"
#include "src/Api/LevelEditorApi.h"
#include "src/Api/DemoApi.h"
#include "src/Api/InputApi.h"
#include "src/Mods/ModStore.h"
#include "src/Api/CtrlsApi.h"
#include "src/Api/HudApi.h"
#include "src/Api/MonstersApi.h"
#include "src/Api/MmdApi.h"
#include "src/Api/PlayerApi.h"
#include "src/Api/SoundApi.h"
#include "src/Api/SpriteFontApi.h"
#include "src/Api/TextureApi.h"
#include "src/Api/InventoryApi.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Core/ForgeSubsystems.h"
#include "src/Mods/ModScan.h"
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

        // Hold delete-on-close; share so LoadLibrary can map while this handle lives.
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

        // Refuse before mapping if a required subsystem is missing.
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

        // Last cheap refusal point — DllMain runs inside LoadLibrary.
        if (!ModScan::IsClean(record.manifest.id, bytes, error))
        {
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
        m_context.capabilities = record.manifest.capabilities;
        m_context.privileged = record.privileged;
        BuildApiTable(m_context);

        for (const std::string& capability : m_context.capabilities)
        {
            FORGE_LOG_INFO("[%s] declares '%s'", m_context.id.c_str(), capability.c_str());
        }

        ModStore::Load(&m_context, m_context.id);
        ConfigApi::ReplayStore(&m_context);
        InputApi::ReplayStore(&m_context);
        DatApi::ReplayStore(&m_context);
        SavApi::ReplayStore(&m_context);

        const int result = init(&m_context.table);
        if (result != AMOD_OK)
        {
            error = DescribeInitResult(result);
            m_shutdown = nullptr; // AmodInit failed — do not call AmodShutdown
            Unload();
            return false;
        }

        FrameApi::Register(&m_context,
            reinterpret_cast<AmodUpdateRoutine>(GetProcAddress(m_module, "AmodUpdate")));

        m_initialised = true;
        return true;
    }

    void ModLibrary::Unload()
    {
        FrameApi::ForgetMod(&m_context);
        LauncherApi::ForgetMod(&m_context);
        ObjectsMenuApi::ForgetMod(&m_context);
        BoxMenuApi::ForgetMod(&m_context);
        BlockExpressionApi::ForgetMod(&m_context);
        ConsoleApi::ForgetMod(&m_context);
        RenderApi::ForgetMod(&m_context);
        WorldApi::ForgetMod(&m_context);
        CtrlsApi::ForgetMod(&m_context);
        BonusesApi::ForgetMod(&m_context);
        InventoryApi::ForgetMod(&m_context);
        HudApi::ForgetMod(&m_context);
        MonstersApi::ForgetMod(&m_context);
        MmdApi::ForgetMod(&m_context);
        SpriteFontApi::ForgetMod(&m_context);
        TextureApi::ForgetMod(&m_context);
        SoundApi::ForgetMod(&m_context);
        PlayerApi::ForgetMod(&m_context);
        CameraApi::ForgetMod(&m_context);
        ParticlesApi::ForgetMod(&m_context);
        FilesApi::ForgetMod(&m_context);
        ConfigApi::ForgetMod(&m_context);
        DatApi::ForgetMod(&m_context);
        SavApi::ForgetMod(&m_context);
        LevelEditorApi::ForgetMod(&m_context);
        DemoApi::ForgetMod(&m_context);
        InputApi::ForgetMod(&m_context);

        ModStore::Save(&m_context);
        ModStore::Drop(&m_context);

        // Detours last — they point into the DLL about to go.
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