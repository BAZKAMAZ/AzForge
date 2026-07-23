#pragma once

#include <stddef.h>

namespace Forge
{
    enum class GameSymbol
    {
        PackageOpenFile,
        MemAllocTracked,
        MemFreeTracked,
        NameBufferAssign,
        AppShutdown,
        LauncherOnInitDialog,
        LauncherOnCommand,
        DialogRunModal,
        LauncherHideOptionalButtons,
        MenuRegister,
        MenuLoadFromScript,
        MenuParseScript,
        MenuItemParse,
        MenuFindItemById,

        Count
    };

    // Locates the game routines AzForge binds to.
    //
    // Each symbol carries a byte signature and the address it sits at in the build it was
    // reversed from. The address is a hint, not the answer: the signature is checked there
    // first because that costs nothing, and only when it does not match is the executable
    // scanned. A build where the code moved but was not rewritten therefore still works.
    //
    // Resolution never guesses. A signature that matches nowhere fails, and so does one
    // that matches more than once - an ambiguous signature is a defect in the signature,
    // and picking the first hit would silently bind to the wrong function.
    class GameSymbols
    {
    public:
        static bool                                                 Resolve();
        static bool                                                 IsResolved();

        static void*                                                Address(GameSymbol symbol);
        static const char*                                          Name(GameSymbol symbol);
    };
}
