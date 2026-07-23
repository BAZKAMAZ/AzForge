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

        // What building an object out of a script fragment needs.
        ParserCreateFromBuffer,
        ParserDestroy,
        ParserEnterBlock,
        ParserNextBlock,
        ParserLeaveBlock,
        MenuAddElement,

        // A constructor per kind, and the two parsers besides MenuItem_Parse that
        // Menu_ParseScript calls directly instead of through the vtable.
        MenuItemCreate,
        MenuLogoCreate,
        TextButtonCreate,
        ImageButtonCreate,
        HotkeyButtonCreate,
        CastleButtonCreate,
        MenuListCreate,
        MenuSliderCreate,
        MenuLogoParse,
        MenuListParse,

        // A table is not an element either: it has its own constructor and parser, goes to
        // Menu_AddTable rather than Menu_AddElement, and is destroyed through a different
        // vtable slot.
        MenuTableCreate,
        MenuTableParse,
        MenuAddTable,

        // Decorations are not elements: these answer a whole collection, so what they build
        // has to be merged into what the menu already holds.
        MenuParseImages,
        MenuParseAnimatedImages,
        MenuParseStaticText,
        MemReallocTracked,

        // Menu navigation: registering a whole menu, and moving between them.
        MenuFindByMenuId,
        CBaseMenuConstruct,
        MenuEnterMenu,
        MenuGoBack,
        MenuShowBox,
        MenuCloseAll,

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
