#include "pch.h"

#include "src/Core/ForgeBoot.h"
#include "src/Api/BoxMenuApi.h"
#include "src/Api/ConsoleApi.h"
#include "src/Api/InteropApi.h"
#include "src/Api/FrameApi.h"
#include "src/Api/LauncherApi.h"
#include "src/Api/ObjectsMenuApi.h"
#include "src/Api/RenderApi.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Core/ForgeSubsystems.h"
#include "src/Core/ForgeText.h"
#include "src/Core/ForgeVersion.h"
#include "src/Game/GameFunctions.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Game/PackageHook.h"
#include "src/Game/ShutdownHook.h"
#include "src/Mods/ModFileSystem.h"
#include "src/Mods/ModHost.h"
#include "src/Mods/ModLoadScreen.h"
#include "src/Mods/ModScan.h"
#include "src/Mods/ModState.h"

namespace Forge
{
    namespace
    {
        HMODULE g_module = nullptr;
        ModRegistry g_registry;
        bool g_running = false;

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

        // Brought up before any mod is read, so the first one is scanned like the rest.
        ModScan::Initialize();

        // Read before the scan, because a mod switched off provides neither files nor code
        // and there is no point discovering what it would have provided.
        ModState::Load();

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
            return false;
        }

        ModFileSystem::Build(g_registry.LoadOrder());

        // With nothing to replace there is no reason to sit in front of every resource the
        // game opens, so the detour is only installed when at least one mod provides a file.
        if (ModFileSystem::FileCount() != 0)
        {
            PackageHook::Install();
        }

        // Each of these stands or falls on its own and a failure is not fatal: the subsystem's
        // table is handed to mods as null, which the interface documents. What is recorded is
        // which ones made it, so the log says so plainly and a mod that declared it needs one
        // is refused with a reason rather than loading against a null.
        Subsystems::Record("launcher", LauncherApi::Install());
        Subsystems::Record("objects_menu", ObjectsMenuApi::Install());
        Subsystems::Record("box_menu", BoxMenuApi::Install());
        Subsystems::Record("frame", FrameApi::Install());
        Subsystems::Record("console", ConsoleApi::Install());
        Subsystems::Record("render", RenderApi::Install());
        Subsystems::Record("world", WorldApi::Install());
        Subsystems::Record("interop", InteropApi::Install());

        // Mod-owned data with nothing to install and no game routine behind it, so it is
        // there whenever the loader is.
        Subsystems::Record("block_expression", true);

        Subsystems::WriteSummary();

        // Only the mods that change the launcher, because the launcher is the one thing that
        // has already happened by the time the loading screen comes up. Everything else waits
        // for the screen, where the menus and the world exist and where the wait is visible.
        ModHost::Load(g_registry.LoadOrder(), ModPhase::BeforeLauncher, nullptr);

        // Everything else waits for the loading screen. There is deliberately no loading them
        // here when the hook cannot be installed: a mod loaded before the launcher meets a
        // game with no menus, no world and no frames, and would fail in ways that look like
        // the mod's fault rather than the loader's. Refusing is the honest outcome, and the
        // reason is in the log.
        ModLoadScreen::Install(g_registry.LoadOrder());

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
        ModLoadScreen::Uninstall();
        PackageHook::Uninstall();
        ShutdownHook::Uninstall();
        LauncherApi::Uninstall();
        ObjectsMenuApi::Uninstall();
        BoxMenuApi::Uninstall();
        FrameApi::Uninstall();
        ConsoleApi::Uninstall();
        RenderApi::Uninstall();
        WorldApi::Uninstall();
        InteropApi::Uninstall();
        ModFileSystem::Clear();
        ModScan::Shutdown();
        ModState::Clear();
        Subsystems::Clear();
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
    // The result is deliberately dropped. It says whether mods are running, and the caller is
    // the winmm proxy, which has no business acting on that: a game whose executable AzForge
    // does not recognise must still start, unmodified and without a dialog in the way. The
    // reason is in forge.log, which is where someone looking for it will be.
    (void)Forge::Boot::Run();
}

extern "C" __declspec(dllexport) void __cdecl AzForge_Shutdown()
{
    Forge::Boot::Stop();
}
