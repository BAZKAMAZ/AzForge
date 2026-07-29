#include "pch.h"

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

        // All three are __thiscall, emulated as __fastcall.
        typedef void(__fastcall* SetProgressRoutine)(void* manager, void* unused, int percent);
        typedef int(__fastcall* RegisterAllRoutine)(void* menuManager, void* unused);
        typedef void(__fastcall* SetEventRoutine)(void* manager, void* unused, int id, int a, int b);

        InlineHook g_hook;
        RegisterAllRoutine g_originalRegisterAll = nullptr;
        SetProgressRoutine g_setProgress = nullptr;
        SetEventRoutine g_setEvent = nullptr;
        uint8_t* g_manager = nullptr;

        // What the game was doing when the screen was borrowed: the event id and both of its
        // arguments, which is all GameState_SetEvent needs to put it back.
        int g_previous[3] = {};
        bool g_borrowed = false;

        const std::vector<ModRecord>* g_loadOrder = nullptr;

        // Loading happens once, whatever else reaches the routine this hangs off.
        bool g_done = false;

        // How many there were to load, so the line can say "3 of 7" while it is still 3.
        size_t g_total = 0;

        // The screen the game is on, or null when it is not the loading screen. Read each time
        // rather than cached: a stale pointer here would be written through.
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

        // Turns the help line on by claiming the music has started, which is the second of the
        // two flags Menu_DrawHelpText wants. Put back afterwards: Loading_ShouldStay reads it
        // to decide whether it still has its own one-time work to do.
        void ShowHelpText(uint8_t* screen, bool show)
        {
            if (screen != nullptr)
            {
                *(screen + kLoadingMusicStartedOffset) = show ? 1 : 0;
            }
        }

        // Menu_SetHelpText copied $press_key into the help object at +0x138 when the script was
        // parsed, and that copy is the one drawn. $press_key is written too, so anything reading
        // the screen back sees the same line.
        void SetLine(uint8_t* screen, const char* text)
        {
            if (screen == nullptr || text == nullptr)
            {
                return;
            }

            strncpy_s(reinterpret_cast<char*>(screen + kLoadingHelpTextOffset),
                      kLoadingHelpTextLength, text, _TRUNCATE);
            strncpy_s(reinterpret_cast<char*>(screen + kLoadingPressKeyOffset),
                      kLoadingPressKeyLength, text, _TRUNCATE);
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

        // Draws a frame with the bar left where it stands. The game's own load is only part way
        // through at this point, so moving the bar would report work that has not been done.
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

        void __cdecl AfterEachMod()
        {
            uint8_t* const screen = CurrentLoadingScreen();
            WriteProgressLine(screen);
            Repaint(screen);
        }

        void LoadMods()
        {
            if (g_done || g_loadOrder == nullptr)
            {
                return;
            }

            // Claimed first, so a routine reached from inside the load cannot start it again.
            g_done = true;

            uint8_t* const screen = CurrentLoadingScreen();
            if (screen == nullptr)
            {
                FORGE_LOG_WARN("The loading screen was not up when the game came to register "
                               "its menus, so the mods are loaded without it");
                ModHost::Load(*g_loadOrder, ModPhase::LoadingScreen, nullptr);
                return;
            }

            // Held open and left held: Loading_ShouldStay reads this to decide whether the game
            // may fall through into the main menu, so the player gets to read how it went.
            SetWaitKey(screen, true);
            ShowHelpText(screen, true);

            g_total = ModHost::CodeModCount(*g_loadOrder);
            WriteProgressLine(screen);
            Repaint(screen);

            ModHost::Load(*g_loadOrder, ModPhase::LoadingScreen, &AfterEachMod);

            WriteProgressLine(screen);
            Repaint(screen);

            // Handed back to the game, which has its own first-time work behind this flag.
            ShowHelpText(screen, false);
        }

        int __fastcall RegisterAllDetour(void* menuManager, void* unused)
        {
            // Mods go first. The manager exists by now and not one menu is registered, so a mod
            // that subscribes at AmodInit hears every menu the game builds, in order and once
            // each, and can register one of its own alongside them.
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

        // The flag says "this is the startup screen", which is what the help line along the
        // bottom is drawn behind - and that line is the whole point of borrowing the screen.
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

    bool ModLoadScreen::Install(const std::vector<ModRecord>& loadOrder)
    {
        void* const target = GameSymbols::Address(GameSymbol::MenuRegisterAll);
        if (target == nullptr)
        {
            FORGE_LOG_WARN("Menu_RegisterAll was not located, so mods cannot be loaded on the "
                           "loading screen");
            return false;
        }

        g_setProgress = reinterpret_cast<SetProgressRoutine>(
            GameSymbols::Address(GameSymbol::LoadingSetProgress));
        // Not signature-resolved: it is only ever reached from here, and the symbols around it
        // have already proved this is the build it belongs to.
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
