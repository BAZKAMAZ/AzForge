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

        // Turns the help line on while the mods load, by claiming the music has started.
        //
        // The flag is put back afterwards rather than left set, because Loading_ShouldStay
        // reads it to decide whether it still has its one-time work to do - setting it and
        // calling the routine behind 0x00691330. Leaving it set would make the game skip that
        // for good; putting it back means the only thing changed is what was drawn while the
        // loading was going on.
        void ShowHelpText(uint8_t* screen, bool show)
        {
            if (screen != nullptr)
            {
                *(screen + kLoadingMusicStartedOffset) = show ? 1 : 0;
            }
        }

        // $press_key, and only that. The caption at +0x2a0 is what Loading_Render draws in the
        // middle of the screen, and it is where the script's $text - "LOADING" - ends up:
        // writing there replaces the game's own word for what it is doing, which is not this
        // to say.
        //
        // Menu_DrawHelpText draws $press_key only once both +0x134 and +0x135 are set, and
        // the second is normally set by Loading_ShouldStay, which does not run until the
        // startup load has returned - so without help the line would not appear until the
        // loading was over. It is set for the length of the load and put back afterwards,
        // which is what ShowHelpText below does.
        void SetLine(uint8_t* screen, const char* text)
        {
            if (screen == nullptr || text == nullptr)
            {
                return;
            }

            // The help object's own copy is the one drawn. $press_key is written too, so that
            // anything reading the screen back - the game reparsing it, a mod looking at it -
            // sees the same thing rather than the line the script started with.
            strncpy_s(reinterpret_cast<char*>(screen + kLoadingHelpTextOffset),
                      kLoadingHelpTextLength, text, _TRUNCATE);
            strncpy_s(reinterpret_cast<char*>(screen + kLoadingPressKeyOffset),
                      kLoadingPressKeyLength, text, _TRUNCATE);
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

            SetLine(screen, line);
        }

        // Runs after each mod: rewrite the line, move the bar to match how many are done, and
        // let the game draw its own screen with both. The bar is restarted for the mods rather
        // than left at the 100 the game finished on - it is reporting a different piece of
        // work, and a bar that never moves reports nothing.
        void __cdecl AfterEachMod()
        {
            WriteProgressLine();

            if (g_originalSetProgress == nullptr || g_manager == nullptr)
            {
                return;
            }

            const size_t done = ModHost::LoadedCount() + ModHost::FailedCount();
            const int percent = g_total == 0
                                    ? kLoadingProgressComplete
                                    : static_cast<int>((done * kLoadingProgressComplete) / g_total);

            g_originalSetProgress(g_manager, nullptr, percent);
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
                // The bar reached 100 without the loading screen being the state, which is
                // not the startup load. The mods still have to be loaded, but there is
                // nothing to hold open and nothing to report on.
                FORGE_LOG_WARN("The loading screen was not on screen when the game finished "
                               "loading, so the mods are loaded without it");
                ModHost::Load(*g_loadOrder, ModPhase::LoadingScreen, nullptr);
                return;
            }

            // Held open for as long as this takes, and left held afterwards. Loading_ShouldStay
            // reads this flag to decide whether the game may fall through into the main menu,
            // so leaving it set is what gives the player time to read how it went - which is
            // the whole reason for reporting it here rather than only in the log.
            SetWaitKey(screen, true);
            ShowHelpText(screen, true);

            g_total = ModHost::CodeModCount(*g_loadOrder);

            // The bar starts again from nothing: it is reporting the mods now, not the game.
            WriteProgressLine();
            g_originalSetProgress(g_manager, nullptr, 0);

            ModHost::Load(*g_loadOrder, ModPhase::LoadingScreen, &AfterEachMod);

            WriteProgressLine();
            g_originalSetProgress(g_manager, nullptr, kLoadingProgressComplete);

            // Handed back to the game, which has its own first-time work behind this flag.
            // The line stays on screen because the game sets it again a frame later.
            ShowHelpText(screen, false);
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
