#pragma once

namespace Forge
{
    // Detour on App_Shutdown @0x00424ef0, the last call WinMain makes.
    //
    // This is the loader's only reliable notice that the game is going away. The proxy that
    // starts AzForge cannot be the one to stop it: DllMain runs its process-detach under the
    // loader lock, where releasing mod libraries is not allowed, and by then the game has
    // already gone.
    //
    // The original runs first and the callback second, which is not a preference but a
    // requirement: the callback tears the loader down, including this hook, and the
    // trampoline that reaches the original is freed with it.
    class ShutdownHook
    {
    public:
        static bool                                                 Install(void (*onShutdown)());
        static void                                                 Uninstall();
        static bool                                                 IsInstalled();
    };
}
