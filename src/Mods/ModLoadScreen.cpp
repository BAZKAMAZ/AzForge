#include "src/Mods/ModLoadScreen.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"
#include "src/Mods/ModHost.h"

#include <stdio.h>
#include <string.h>

namespace Forge
{
    namespace
    {
        using namespace GameAddress;

        // All three are __thiscall → __fastcall (EDX unused).
        typedef void(__fastcall* SetProgressRoutine)(void* manager, void* unused, int percent);
        typedef int(__fastcall* RegisterAllRoutine)(void* menuManager, void* unused);
        typedef void(__fastcall* SetEventRoutine)(void* manager, void* unused, int id, int a, int b);

        InlineHook g_hook;
        RegisterAllRoutine g_originalRegisterAll = nullptr;
        SetProgressRoutine g_setProgress = nullptr;
        SetEventRoutine g_setEvent = nullptr;
        uint8_t* g_manager = nullptr;

        // Saved event id + args for Leave().
        int g_previous[3] = {};
        bool g_borrowed = false;

        const std::vector<ModRecord>* g_loadOrder = nullptr;
        bool g_done = false;
        size_t g_total = 0;

        uint8_t* CurrentLoadingScreen()
        {
            if (g_manager == nullptr)
            {
                return nullptr;
            }

            int state = 0;
            memcpy(&state, g_manager + kGameStateIdOffset, sizeof(state));
            if (state != kGameStateLoading)
            {
                return nullptr;
            }

            uint8_t* screen = nullptr;
            memcpy(&screen, g_manager + kGameStateCurrentOffset, sizeof(screen));
            return screen;
        }

        void SetWaitKey(uint8_t* screen, bool wait)
        {
            if (screen != nullptr)
            {
                *(screen + kLoadingWaitKeyOffset) = wait ? 1 : 0;
            }
        }

        void ShowHelpText(uint8_t* screen, bool show)
        {
            if (screen != nullptr)
            {
                *(screen + kLoadingMusicStartedOffset) = show ? 1 : 0;
            }
        }

        // Drawn string lives in help object (+0x13C); $press_key is mirrored
        void SetLine(uint8_t* screen, const char* text)
        {
            if (screen == nullptr || text == nullptr)
            {
                return;
            }

            strncpy_s(reinterpret_cast<char*>(screen + kLoadingHelpTextOffset), kLoadingHelpTextLength, text, _TRUNCATE);
            strncpy_s(reinterpret_cast<char*>(screen + kLoadingPressKeyOffset), kLoadingPressKeyLength, text, _TRUNCATE);
        }

        void WriteProgressLine(uint8_t* screen)
        {
            if (screen == nullptr)
            {
                return;
            }

            const size_t loaded = ModHost::LoadedCount();
            const size_t failed = ModHost::FailedCount();

            char line[128];
            if (failed == 0)
            {
                snprintf(line, sizeof(line), "Mods loaded %zu of %zu", loaded, g_total);
            }
            else
            {
                snprintf(line, sizeof(line), "Mods loaded %zu out of %zu (%zu were not loaded)",
                    loaded, g_total, failed);
            }

            SetLine(screen, line);
        }

        // Leave the bar where the game put it — only repaint the caption
        void Repaint(uint8_t* screen)
        {
            if (g_setProgress == nullptr || g_manager == nullptr || screen == nullptr)
            {
                return;
            }

            int percent = 0;
            memcpy(&percent, screen + kLoadingPercentOffset, sizeof(percent));
            g_setProgress(g_manager, nullptr, percent);
        }

        int g_wasPercent = 100;

        void __cdecl AfterEachMod()
        {
            uint8_t* const screen = CurrentLoadingScreen();

            // A mod may have driven the bar for its own loading; the game's own place goes back,
            // so the next one does not start from wherever the last left off.
            if (screen != nullptr)
            {
                memcpy(screen + kLoadingPercentOffset, &g_wasPercent, sizeof(g_wasPercent));
            }

            WriteProgressLine(screen);
            Repaint(screen);
        }

        void LoadMods()
        {
            if (g_done || g_loadOrder == nullptr)
            {
                return;
            }

            g_done = true;

            uint8_t* const screen = CurrentLoadingScreen();
            if (screen == nullptr)
            {
                FORGE_LOG_WARN("The loading screen was not up when the game came to register "
                    "its menus, so the mods are loaded without it");
                ModHost::Load(*g_loadOrder, ModPhase::LoadingScreen, nullptr);
                return;
            }

            // Hold the screen open so the player can read the result.
            SetWaitKey(screen, true);
            ShowHelpText(screen, true);

            memcpy(&g_wasPercent, screen + kLoadingPercentOffset, sizeof(g_wasPercent));

            g_total = ModHost::CodeModCount(*g_loadOrder);
            WriteProgressLine(screen);
            Repaint(screen);

            ModHost::Load(*g_loadOrder, ModPhase::LoadingScreen, &AfterEachMod);

            WriteProgressLine(screen);
            Repaint(screen);

            ShowHelpText(screen, false);
        }

        int __fastcall RegisterAllDetour(void* menuManager, void* unused)
        {
            // Mods first — manager exists, no menus registered yet.
            LoadMods();
            return g_originalRegisterAll(menuManager, unused);
        }
    }

    bool ModLoadScreen::Enter()
    {
        if (g_setEvent == nullptr || g_manager == nullptr || g_borrowed)
        {
            return false;
        }

        memcpy(g_previous, g_manager + kGameStateIdOffset, sizeof(g_previous));

        // arg=1 → startup-screen flag (help line drawn).
        g_setEvent(g_manager, nullptr, kGameStateLoading, 1, 0);

        uint8_t* const screen = CurrentLoadingScreen();
        if (screen == nullptr)
        {
            return false;
        }

        ShowHelpText(screen, true);
        g_borrowed = true;
        return true;
    }

    void ModLoadScreen::Leave()
    {
        if (!g_borrowed)
        {
            return;
        }

        g_borrowed = false;
        g_setEvent(g_manager, nullptr, g_previous[0], g_previous[1], g_previous[2]);
    }

    void ModLoadScreen::Report(const char* line)
    {
        uint8_t* const screen = CurrentLoadingScreen();
        SetLine(screen, line);
        Repaint(screen);
    }

    bool ModLoadScreen::Up()
    {
        return CurrentLoadingScreen() != nullptr;
    }

    void ModLoadScreen::Step(int percent, const char* line)
    {
        uint8_t* const screen = CurrentLoadingScreen();

        if (screen == nullptr)
        {
            return;
        }

        if (percent >= 0)
        {
            const int wanted = percent > 100 ? 100 : percent;

            memcpy(screen + kLoadingPercentOffset, &wanted, sizeof(wanted));
        }

        if (line != nullptr)
        {
            SetLine(screen, line);
        }

        Repaint(screen);
    }

    bool ModLoadScreen::Install(const std::vector<ModRecord>& loadOrder)
    {
        void* const target = GameSymbols::Address(GameSymbol::MenuRegisterAll);
        if (target == nullptr)
        {
            FORGE_LOG_WARN("Menu_RegisterAll was not located, so mods cannot be loaded on the "
                "loading screen");
            return false;
        }

        g_setProgress = reinterpret_cast<SetProgressRoutine>(GameSymbols::Address(GameSymbol::LoadingSetProgress));
        g_setEvent = reinterpret_cast<SetEventRoutine>(GameModule::Resolve(kGameStateSetEvent));
        g_manager = static_cast<uint8_t*>(GameModule::Resolve(kGameStateManager));

        if (g_setProgress == nullptr || g_manager == nullptr)
        {
            FORGE_LOG_WARN("The loading screen was not located, so mods cannot be loaded on it");
            return false;
        }

        std::string mismatch;
        if (!GameModule::VerifyBytes(target, kMenuRegisterAllPrologue,
            sizeof(kMenuRegisterAllPrologue), mismatch))
        {
            FORGE_LOG_ERROR("Menu_RegisterAll does not open as expected (%s)", mismatch.c_str());
            return false;
        }

        std::string error;
        if (!g_hook.Install(target, reinterpret_cast<void*>(&RegisterAllDetour),
            kMenuRegisterAllPrologue, sizeof(kMenuRegisterAllPrologue), error))
        {
            FORGE_LOG_ERROR("Cannot hook Menu_RegisterAll: %s", error.c_str());
            return false;
        }

        g_originalRegisterAll = reinterpret_cast<RegisterAllRoutine>(g_hook.Trampoline());
        g_loadOrder = &loadOrder;
        g_done = false;

        FORGE_LOG_INFO("Mods will be loaded on the loading screen, before the game registers "
            "its menus");
        return true;
    }

    void ModLoadScreen::Uninstall()
    {
        g_hook.Uninstall();
        g_originalRegisterAll = nullptr;
        g_setProgress = nullptr;
        g_setEvent = nullptr;
        g_borrowed = false;
        g_manager = nullptr;
        g_loadOrder = nullptr;
        g_done = false;
        g_total = 0;
    }
}