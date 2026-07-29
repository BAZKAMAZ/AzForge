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

        // __thiscall (manager, percent), emulated as __fastcall.
        typedef void(__fastcall* SetProgressRoutine)(void* manager, void* unused, int percent);

        InlineHook g_hook;
        SetProgressRoutine g_originalSetProgress = nullptr;
        uint8_t* g_manager = nullptr;

        const std::vector<ModRecord>* g_loadOrder = nullptr;

        // Loading happens once. The flag guards against the startup sequence being run a
        // second time - a new game does not do that today, but the cost of being sure is one
        // comparison and the cost of being wrong is every mod loaded twice.
        bool g_done = false;

        // How many there were to load, so the line can say "3 of 7" while it is still 3.
        size_t g_total = 0;

        // The screen the game is on, or null when it is not on the loading screen. Read each
        // time rather than cached: the state changes underneath and a stale pointer here
        // would be written through.
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

        void SetPressKeyText(uint8_t* screen, const char* text)
        {
            if (screen == nullptr || text == nullptr)
            {
                return;
            }

            char* const line = reinterpret_cast<char*>(screen + kLoadingPressKeyOffset);
            strncpy_s(line, kLoadingPressKeyLength, text, _TRUNCATE);
        }

        // What the player reads while this happens.
        void WriteProgressLine()
        {
            uint8_t* const screen = CurrentLoadingScreen();
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

            SetPressKeyText(screen, line);
        }

        // Runs after each mod: rewrite the line, then let the game draw its own screen with
        // it. The percentage is the one the game had already reached, so the bar does not
        // move backwards while the text does.
        void __cdecl AfterEachMod()
        {
            WriteProgressLine();

            if (g_originalSetProgress != nullptr && g_manager != nullptr)
            {
                g_originalSetProgress(g_manager, nullptr, kLoadingProgressComplete);
            }
        }

        void __fastcall SetProgressDetour(void* manager, void* unused, int percent)
        {
            g_originalSetProgress(manager, unused, percent);

            if (g_done || percent != kLoadingProgressComplete || g_loadOrder == nullptr)
            {
                return;
            }

            // Claimed before anything else runs, so a routine reached from inside the load -
            // a mod that draws, most of all - cannot start the whole thing again.
            g_done = true;

            uint8_t* const screen = CurrentLoadingScreen();
            if (screen == nullptr)
            {
                // The bar reached 100 outside the loading screen, which is not the startup
                // load. Nothing to hold open, so the mods are loaded without the screen.
                ModHost::Load(*g_loadOrder, ModPhase::LoadingScreen, nullptr);
                return;
            }

            // Held open for as long as this takes. Loading_ShouldStay keeps the game here
            // while the flag is set, so the state cannot move on underneath the load - and
            // the flag goes back to whatever the script asked for afterwards, because it is
            // the script that decides whether the player presses a key to continue.
            const uint8_t wasWaiting = *(screen + kLoadingWaitKeyOffset);
            SetWaitKey(screen, true);

            g_total = ModHost::CodeModCount(*g_loadOrder);

            WriteProgressLine();
            ModHost::Load(*g_loadOrder, ModPhase::LoadingScreen, &AfterEachMod);
            WriteProgressLine();

            *(screen + kLoadingWaitKeyOffset) = wasWaiting;
        }
    }

    bool ModLoadScreen::Install(const std::vector<ModRecord>& loadOrder)
    {
        void* const target = GameSymbols::Address(GameSymbol::LoadingSetProgress);
        if (target == nullptr)
        {
            FORGE_LOG_WARN("Loading_SetProgress was not located, so mods cannot be loaded on "
                           "the loading screen");
            return false;
        }

        g_manager = static_cast<uint8_t*>(GameModule::Resolve(kGameStateManager));
        if (g_manager == nullptr)
        {
            FORGE_LOG_WARN("The game state was not located, so mods cannot be loaded on the "
                           "loading screen");
            return false;
        }

        std::string mismatch;
        if (!GameModule::VerifyBytes(target, kLoadingSetProgressPrologue,
                                     sizeof(kLoadingSetProgressPrologue), mismatch))
        {
            FORGE_LOG_ERROR("Loading_SetProgress does not open as expected (%s)",
                            mismatch.c_str());
            return false;
        }

        std::string error;
        if (!g_hook.Install(target, reinterpret_cast<void*>(&SetProgressDetour),
                            kLoadingSetProgressPrologue, sizeof(kLoadingSetProgressPrologue),
                            error))
        {
            FORGE_LOG_ERROR("Cannot hook Loading_SetProgress: %s", error.c_str());
            return false;
        }

        g_originalSetProgress = reinterpret_cast<SetProgressRoutine>(g_hook.Trampoline());
        g_loadOrder = &loadOrder;
        g_done = false;

        FORGE_LOG_INFO("Mods will be loaded on the loading screen");
        return true;
    }

    void ModLoadScreen::Uninstall()
    {
        g_hook.Uninstall();
        g_originalSetProgress = nullptr;
        g_manager = nullptr;
        g_loadOrder = nullptr;
        g_done = false;
        g_total = 0;
    }
}
