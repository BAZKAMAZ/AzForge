#include "src/Core/ForgeBoot.h"
#include "src/Api/BoxMenuApi.h"
#include "src/Api/ConsoleApi.h"
#include "src/Api/InteropApi.h"
#include "src/Api/BonusesApi.h"
#include "src/Api/HudApi.h"
#include "src/Api/MonstersApi.h"
#include "src/Api/MmdApi.h"
#include "src/Api/CameraApi.h"
#include "src/Api/CtrlsApi.h"
#include "src/Api/PlayerApi.h"
#include "src/Api/SoundApi.h"
#include "src/Api/TextureApi.h"
#include "src/Api/InputApi.h"
#include "src/Api/InventoryApi.h"
#include "src/Api/ModManagerApi.h"
#include "src/Api/FrameApi.h"
#include "src/Api/LauncherApi.h"
#include "src/Api/ObjectsMenuApi.h"
#include "src/Api/RenderApi.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeCrash.h"
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

        Crash::Install();
        ModHost::SweepCache();
        ModScan::Initialize();
        ModState::Load();

        if (!g_registry.Scan(Paths::ModsDirectory()))
        {
            FORGE_LOG_ERROR("Mod discovery failed, no mods will be loaded");
            g_registry.Clear();
        }

        // core.amod lives beside the loader; added after scan so it leads the order.
        g_registry.AddPrivileged(Paths::ForgeModuleFile().parent_path() / "core.amod");

        WriteLoadOrder();

        if (!GameModule::Initialize() || !GameSymbols::Resolve() || !GameFunctions::Bind())
        {
            FORGE_LOG_ERROR("This is not the game AzForge was built for. No mods are loaded and "
                "the process is left exactly as it was.");
            return false;
        }

        ModFileSystem::Build(g_registry.LoadOrder());

        if (ModFileSystem::FileCount() != 0)
        {
            PackageHook::Install();
        }

        // Failures are non-fatal: subsystem table entry is null, mod that needs it is refused.
        Subsystems::Record("launcher", LauncherApi::Install());
        Subsystems::Record("objects_menu", ObjectsMenuApi::Install());
        Subsystems::Record("box_menu", BoxMenuApi::Install());
        Subsystems::Record("frame", FrameApi::Install());
        Subsystems::Record("console", ConsoleApi::Install());
        Subsystems::Record("render", RenderApi::Install());
        Subsystems::Record("world", WorldApi::Install());
        Subsystems::Record("bonuses", BonusesApi::Install());
        Subsystems::Record("inventory", InventoryApi::Install());
        Subsystems::Record("hud", HudApi::Install());
        Subsystems::Record("monsters", MonstersApi::Install());
        Subsystems::Record("mmd", MmdApi::Install());
        Subsystems::Record("textures", TextureApi::Install());
        Subsystems::Record("sound", SoundApi::Install());
        Subsystems::Record("player", PlayerApi::Install());
        Subsystems::Record("ctrls", CtrlsApi::Install());
        Subsystems::Record("camera", CameraApi::Install());
        Subsystems::Record("interop", InteropApi::Install());
        Subsystems::Record("input", InputApi::Install());

        ModManagerApi::Install();
        Subsystems::Record("block_expression", true);
        Subsystems::WriteSummary();

        // Launcher mods load immediately; everything else waits for the loading screen.
        ModHost::Load(g_registry.LoadOrder(), ModPhase::BeforeLauncher, nullptr);
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
        HudApi::Uninstall();
        MonstersApi::Uninstall();
        MmdApi::Uninstall();
        TextureApi::Uninstall();
        SoundApi::Uninstall();
        PlayerApi::Uninstall();
        CtrlsApi::Uninstall();
        CameraApi::Uninstall();
        RenderApi::Uninstall();
        WorldApi::Uninstall();
        InteropApi::Uninstall();
        InputApi::Uninstall();
        ModFileSystem::Clear();
        ModScan::Shutdown();
        ModState::Clear();
        ModManagerApi::Clear();
        Subsystems::Clear();
        g_registry.Clear();

        Crash::Uninstall();
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
    (void)Forge::Boot::Run();
}

extern "C" __declspec(dllexport) void __cdecl AzForge_Shutdown()
{
    Forge::Boot::Stop();
}