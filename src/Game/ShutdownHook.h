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
    // The CALLBACK runs first and the original second, and that order is not a preference
    // either: App_Shutdown destroys the sound device, the texture manager and the tracked
    // allocations, while the callback hands mod resources back to exactly those. Cleaning up
    // after the game meant a voice stopped through a null device, texture records released
    // after the manager had freed them, and the same tracked block freed twice.
    //
    // The catch that made the old order look necessary is real and is handled here: the
    // callback uninstalls this hook, which frees the trampoline the original is reached
    // through. Uninstall therefore holds off while the detour is on the stack and happens once
    // the game's own shutdown has returned. `afterGame` is for what has to outlive the game -
    // the crash handler and the log, so a fault inside App_Shutdown is still reported.
    class ShutdownHook
    {
    public:
        static bool                                                 Install(void (*onShutdown)(), void (*afterGame)() = nullptr);
        static void                                                 Uninstall();
        static bool                                                 IsInstalled();
    };
}
