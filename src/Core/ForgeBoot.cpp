#include <string>
#include <set>
#include <fstream>
#include "src/Core/ForgeBoot.h"
#include "src/Api/BoxMenuApi.h"
#include "src/Api/ConsoleApi.h"
#include "src/Api/InteropApi.h"
#include "src/Api/BonusesApi.h"
#include "src/Api/HudApi.h"
#include "src/Api/MonstersApi.h"
#include "src/Api/MmdApi.h"
#include "src/Api/CameraApi.h"
#include "src/Api/ParticlesApi.h"
#include "src/Api/FilesApi.h"
#include "src/Api/AzmApi.h"
#include "src/Api/ShaderApi.h"
#include "src/Api/ConfigApi.h"
#include "src/Api/DatApi.h"
#include "src/Api/SavApi.h"
#include "src/Api/CtrlsApi.h"
#include "src/Api/PlayerApi.h"
#include "src/Api/SoundApi.h"
#include "src/Api/TextureApi.h"
#include "src/Api/InputApi.h"
#include "src/Api/LevelEditorApi.h"
#include "src/Api/DemoApi.h"
#include "src/Api/SpriteFontApi.h"
#include "src/Api/StageApi.h"
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
#include "src/Game/D3d8Hook.h"
#include "src/Game/PackageHook.h"
#include "src/Game/ShutdownHook.h"
#include "src/Mods/ModFileSystem.h"
#include "src/Mods/ModHost.h"
#include "src/Mods/ModLoadScreen.h"
#include "src/Mods/ModScan.h"
#include "src/Mods/ModScreen.h"
#include "src/Mods/ModStore.h"
#include "src/Mods/ModState.h"

namespace Forge
{
    namespace
    {
        HMODULE g_module = nullptr;
        ModRegistry g_registry;
        bool g_running = false;

        // Disables subsystems listed in mods/skip.txt for fault isolation.
        bool Skipped(const char* name)
        {
            static std::set<std::string> named;
            static bool read = false;

            if (!read)
            {
                read = true;

                std::ifstream file(Paths::ModsDirectory() / L"skip.txt");
                std::string line;

                while (std::getline(file, line))
                {
                    while (!line.empty() && (line.back() == 13 || line.back() == 32))
                    {
                        line.pop_back();
                    }

                    if (!line.empty())
                    {
                        named.insert(line);
                        FORGE_LOG_WARN("'%s' is named in skip.txt and is not installed", line.c_str());
                    }
                }
            }

            return named.count(name) != 0;
        }

        // The name is written once: Record both installs the subsystem and reports it.
        void Install(const char* name, bool (*install)())
        {
            Subsystems::Record(name, !Skipped(name) && install());
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

        Log::Open(Paths::LogFile().wstring());

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
        Install("launcher", &LauncherApi::Install);
        Install("objects_menu", &ObjectsMenuApi::Install);
        Install("box_menu", &BoxMenuApi::Install);
        Install("frame", &FrameApi::Install);

        FrameApi::AddInternalTick(&ModStore::Tick, true);

        Install("console", &ConsoleApi::Install);
        Install("render", &RenderApi::Install);
        Install("world", &WorldApi::Install);
        Install("bonuses", &BonusesApi::Install);
        Install("inventory", &InventoryApi::Install);
        Install("hud", &HudApi::Install);
        Install("monsters", &MonstersApi::Install);
        Install("mmd", &MmdApi::Install);
        Install("textures", &TextureApi::Install);
        Install("sound", &SoundApi::Install);
        Install("player", &PlayerApi::Install);
        Install("ctrls", &CtrlsApi::Install);
        Install("camera", &CameraApi::Install);
        Install("particles", &ParticlesApi::Install);
        Install("files", &FilesApi::Install);
        Install("config", &ConfigApi::Install);
        Install("scores", &DatApi::Install);
        Install("sav", &SavApi::Install);
        Install("interop", &InteropApi::Install);
        Install("input", &InputApi::Install);
        Install("level_editor", &LevelEditorApi::Install);
        Install("demo", &DemoApi::Install);
        Install("sprite_font", &SpriteFontApi::Install);
        Install("stage", &StageApi::Install);
        Install("azm", &AzmApi::Install);
        Install("shader", &ShaderApi::Install);

        ModManagerApi::Install();
        Install("mod_screen", &ModScreen::Install);
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
        ModScreen::Uninstall();
        ModLoadScreen::Uninstall();
        PackageHook::Uninstall();
        D3d8Hook::Detach();
        ShutdownHook::Uninstall();
        LauncherApi::Uninstall();
        ObjectsMenuApi::Uninstall();
        BoxMenuApi::Uninstall();
        FrameApi::Uninstall();
        ConsoleApi::Uninstall();
        HudApi::Uninstall();
        MonstersApi::Uninstall();
        AzmApi::Uninstall();
        ShaderApi::Uninstall();
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
        LevelEditorApi::Uninstall();
        DemoApi::Uninstall();
        SpriteFontApi::Uninstall();
        StageApi::Uninstall();
        ModFileSystem::Clear();
        ModScan::Shutdown();
        ModState::Clear();
        ModManagerApi::Clear();

        ModHost::ReleaseCache();

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

extern "C" __declspec(dllexport) void* __cdecl AzForge_HookD3d8(void* d3d8)
{
    return Forge::D3d8Hook::Attach(d3d8);
}

extern "C" __declspec(dllexport) void __cdecl AzForge_Shutdown()
{
    Forge::Boot::Stop();
}