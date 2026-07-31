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

        void __cdecl AppShutdownDetour()
        {
            const AppShutdownRoutine original = g_original;
            void (*const callback)() = g_onShutdown;

            if (original != nullptr)
            {
                original();
            }

            if (callback != nullptr)
            {
                callback();
            }
        }
    }

    bool ShutdownHook::Install(void (*onShutdown)())
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

        g_hook.Uninstall();
        g_original = nullptr;
        g_onShutdown = nullptr;
    }

    bool ShutdownHook::IsInstalled()
    {
        return g_hook.IsInstalled();
    }
}
