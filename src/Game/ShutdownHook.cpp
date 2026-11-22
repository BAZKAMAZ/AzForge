#include "src/Game/ShutdownHook.h"

#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

namespace Forge
{
    namespace
    {
        typedef void(__cdecl* AppShutdownRoutine)();

        InlineHook g_hook;
        AppShutdownRoutine g_original = nullptr;
        void (*g_onShutdown)() = nullptr;
        void (*g_afterGame)() = nullptr;

        bool g_inDetour = false;
        bool g_askedUninstall = false;

        void __cdecl AppShutdownDetour()
        {
            void (*const callback)() = g_onShutdown;
            void (*const after)() = g_afterGame;

            g_inDetour = true;

            // The loader goes FIRST, and the order is the whole point. App_Shutdown destroys the
            // sound device, the texture manager and the tracked allocations; every ForgetMod hands
            // resources back to those managers. Cleaning up afterwards meant releasing a voice
            // through a null device, releasing texture records the manager had already freed, and
            // freeing the same tracked block twice - three separate crash reports, one order.
            if (callback != nullptr)
            {
                callback();
            }

            // Read after the callback: it asked for the uninstall that would have freed this
            const AppShutdownRoutine original = g_original;

            if (original != nullptr)
            {
                original();
            }

            g_inDetour = false;

            if (g_askedUninstall)
            {
                ShutdownHook::Uninstall();
            }

            // The log and the crash handler come down last, so a fault in the game's own
            // shutdown still reaches forge.log
            if (after != nullptr)
            {
                after();
            }
        }
    }

    bool ShutdownHook::Install(void (*onShutdown)(), void (*afterGame)())
    {
        if (g_hook.IsInstalled())
        {
            return true;
        }

        if (onShutdown == nullptr)
        {
            FORGE_LOG_ERROR("The shutdown hook was given no callback");
            return false;
        }

        void* const target = GameSymbols::Address(GameSymbol::AppShutdown);
        if (target == nullptr)
        {
            FORGE_LOG_ERROR("App_Shutdown was not located, the loader cannot clean up on exit");
            return false;
        }

        std::string mismatch;
        if (!GameModule::VerifyBytes(target, kAppShutdownPrologue,
                                     sizeof(kAppShutdownPrologue), mismatch))
        {
            FORGE_LOG_ERROR("App_Shutdown does not open with the instructions AzForge steals (%s); "
                            "the loader cannot clean up on exit", mismatch.c_str());
            return false;
        }

        std::string error;
        if (!g_hook.Install(target, reinterpret_cast<void*>(&AppShutdownDetour),
                            kAppShutdownPrologue, sizeof(kAppShutdownPrologue), error))
        {
            FORGE_LOG_ERROR("Cannot hook App_Shutdown: %s", error.c_str());
            return false;
        }

        g_original = reinterpret_cast<AppShutdownRoutine>(g_hook.Trampoline());
        g_onShutdown = onShutdown;
        g_afterGame = afterGame;

        FORGE_LOG_INFO("App_Shutdown hooked at 0x%08X, the loader will be torn down with the game",
                       static_cast<unsigned>(reinterpret_cast<uintptr_t>(target)));
        return true;
    }

    void ShutdownHook::Uninstall()
    {
        if (!g_hook.IsInstalled())
        {
            return;
        }

        // Asked for from inside the detour, where the trampoline is still being called through:
        // uninstalling frees it, so the detour does this once the game's shutdown has returned.
        if (g_inDetour)
        {
            g_askedUninstall = true;
            return;
        }

        g_hook.Uninstall();
        g_original = nullptr;
        g_onShutdown = nullptr;
        g_afterGame = nullptr;
        g_askedUninstall = false;
    }

    bool ShutdownHook::IsInstalled()
    {
        return g_hook.IsInstalled();
    }
}
