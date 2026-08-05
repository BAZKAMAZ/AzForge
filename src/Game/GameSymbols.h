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
        ParserCreateFromBuffer,
        ParserDestroy,
        ParserEnterBlock,
        ParserNextBlock,
        ParserLeaveBlock,
        MenuAddElement,
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
        MenuTableCreate,
        MenuTableParse,
        MenuAddTable,
        MenuParseImages,
        MenuParseAnimatedImages,
        MenuParseStaticText,
        MemReallocTracked,
        MenuFindByMenuId,
        CBaseMenuConstruct,
        MenuEnterMenu,
        MenuGoBack,
        MenuShowBox,
        MenuCloseAll,
        MenuTakeClickResult,
        GameFrameTick,
        GameConsoleWrite,
        CmdRegister,
        ConsoleExecute,
        RenderBegin,
        RenderPresent,
        MenuItemRender,
        MenuDrawDecorations,
        MenuFindFont,
        LevelLoad,
        LevelFree,
        LevelActivateRoom,
        LoadingSetProgress,
        MenuRegisterAll,
        BonusOnTouch,
        BonusesFreeResources,
        MonstersFreeResources,
        MonsterParseSpawn,
        MonsterSpawnPlacement,
        MonsterKill,
        LoadOrFindModel,
        ModelRelease,
        ModelDrawNode,
        ModelRenderAnimated,
        SceneDrawModels,
        LoadOrFindAnim,
        LoadOrFindTexture,

        Count
    };

    class GameSymbols
    {
    public:
        static bool                                                 Resolve();
        static bool                                                 IsResolved();

        static void*                                                Address(GameSymbol symbol);
        static const char*                                          Name(GameSymbol symbol);
    };
}
