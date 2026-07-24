#include "pch.h"

#include "src/Core/ForgeBoot.h"
#include "src/Api/BoxMenuApi.h"
#include "src/Api/FrameApi.h"
#include "src/Api/LauncherApi.h"
#include "src/Api/ObjectsMenuApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Core/ForgeText.h"
#include "src/Core/ForgeVersion.h"
#include "src/Game/GameFunctions.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Game/PackageHook.h"
#include "src/Game/ShutdownHook.h"
#include "src/Mods/ModFileSystem.h"
#include "src/Mods/ModHost.h"

namespace Forge
{
    namespace
    {
        HMODULE g_module = nullptr;
        ModRegistry g_registry;
        bool g_running = false;

        std::string PathToUtf8(const std::filesystem::path& path)
        {
            return ToUtf8(path.wstring());
        }

        void WriteBanner()
        {
            Log::WriteSeparator();
            FORGE_LOG_INFO("AzForge %s, mod API %d", AZFORGE_VERSION_STRING, kApiVersionCurrent);
            FORGE_LOG_INFO("Host      : %s", PathToUtf8(Paths::HostExecutableFile()).c_str());
            FORGE_LOG_INFO("Loader    : %s", PathToUtf8(Paths::ForgeModuleFile()).c_str());
            FORGE_LOG_INFO("Mods      : %s", PathToUtf8(Paths::ModsDirectory()).c_str());
            Log::WriteSeparator();
        }

        void WriteLoadOrder()
        {
            const std::vector<ModRecord>& order = g_registry.LoadOrder();

            Log::WriteSeparator();
            if (order.empty())
            {
                FORGE_LOG_INFO("Load order is empty");
            }
            else
            {
                FORGE_LOG_INFO("Load order:");
                for (size_t i = 0; i < order.size(); ++i)
                {
                    const ModManifest& manifest = order[i].manifest;
                    FORGE_LOG_INFO("  %zu. %s %s  \"%s\"%s%s",
                                   i + 1,
                                   manifest.id.c_str(),
                                   manifest.version.c_str(),
                                   manifest.name.c_str(),
                                   manifest.author.empty() ? "" : " by ",
                                   manifest.author.c_str());
                }
            }

            FORGE_LOG_INFO("%zu mod(s) ready, %zu rejected",
                           order.size(), g_registry.RejectedCount());
            Log::WriteSeparator();
        }
    }

    void Boot::SetModuleHandle(HMODULE module)
    {
        g_module = module;
    }

    bool Boot::Run()
    {
        if (g_running)
        {
            return true;
        }

        if (!Paths::Initialize(g_module))
        {
            return false;
        }

        if (!Log::Open(Paths::LogFile().wstring()))
        {
            return false;
        }

        g_running = true;

        WriteBanner();

        // Done before anything else and regardless of what follows: a run that never gets
        // as far as loading mods should still clear what an earlier one left behind.
        ModHost::SweepCache();

        if (!g_registry.Scan(Paths::ModsDirectory()))
        {
            FORGE_LOG_ERROR("Mod discovery failed, no mods will be loaded");
            g_registry.Clear();
        }

        WriteLoadOrder();

        // Without the game routines nothing can be replaced and nothing can be handed back
        // to the game, and a code mod would be running against a binary the loader cannot
        // account for. Standing down entirely is the only honest outcome.
        if (!GameModule::Initialize() || !GameSymbols::Resolve() || !GameFunctions::Bind())
        {
            FORGE_LOG_ERROR("This is not the game AzForge was built for. No mods are loaded and "
                            "the process is left exactly as it was.");
            return true;
        }

        ModFileSystem::Build(g_registry.LoadOrder());

        // With nothing to replace there is no reason to sit in front of every resource the
        // game opens, so the detour is only installed when at least one mod provides a file.
        if (ModFileSystem::FileCount() != 0)
        {
            PackageHook::Install();
        }

        LauncherApi::Install();
        ObjectsMenuApi::Install();
        BoxMenuApi::Install();
        FrameApi::Install();
        ModHost::LoadAll(g_registry.LoadOrder());
        ShutdownHook::Install(&Boot::Stop);

        return true;
    }

    void Boot::Stop()
    {
        if (!g_running)
        {
            return;
        }

        FORGE_LOG_INFO("AzForge shutting down");

        ModHost::UnloadAll();
        PackageHook::Uninstall();
        ShutdownHook::Uninstall();
        LauncherApi::Uninstall();
        ObjectsMenuApi::Uninstall();
        BoxMenuApi::Uninstall();
        FrameApi::Uninstall();
        ModFileSystem::Clear();
        g_registry.Clear();

        Log::Close();
        g_running = false;
    }

    const ModRegistry& Boot::Registry()
    {
        return g_registry;
    }
}

extern "C" __declspec(dllexport) void __cdecl AzForge_Boot()
{
    Forge::Boot::Run();
}

extern "C" __declspec(dllexport) void __cdecl AzForge_Shutdown()
{
    Forge::Boot::Stop();
}
