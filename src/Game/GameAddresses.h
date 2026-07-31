#pragma once

#include <stdint.h>

namespace Forge
{
    // Addresses reversed from the shipped game.exe, linked at 0x00400000.
    // Hints only — GameSymbols resolves by byte signature.
    namespace GameAddress
    {
        constexpr uintptr_t kPreferredImageBase = 0x00400000;

        constexpr uintptr_t kPackageOpenFile = 0x00422F00;
        constexpr uintptr_t kPackageReadFromDisk = 0x00422660;
        constexpr uintptr_t kPackageReadFromPak = 0x00422CA0;
        constexpr uintptr_t kPackageFree = 0x00422200;

        constexpr uintptr_t kMemAllocTracked = 0x00421DF0; // __cdecl (size, file, line)
        constexpr uintptr_t kMemFreeTracked = 0x00421CE0; // __cdecl (block)
        constexpr uintptr_t kNameBufferAssign = 0x004022B0; // __thiscall (dst, src)

        constexpr uintptr_t kAppShutdown = 0x00424EF0;

        constexpr uintptr_t kLauncherOnInitDialog = 0x0040C7E0; // vtable +0x04
        constexpr uintptr_t kLauncherOnCommand = 0x0040C5D0; // vtable +0x0C
        constexpr uintptr_t kDialogRunModal = 0x00424520;
        constexpr uintptr_t kLauncherHideOptionalButtons = 0x0040C3B0;
        constexpr uintptr_t kLauncherDialogTemplate = 101;

        constexpr uintptr_t kMenuRegister = 0x0043CA40; // __thiscall (manager, menu, script, menuId, force)
        constexpr uintptr_t kMenuLoadFromScript = 0x0043C090; // __stdcall (menuId, scriptFile)
        constexpr uintptr_t kMenuRegisterAll = 0x0043CAB0; // __thiscall (manager) -> bool

        constexpr uintptr_t kMenuManager = 0x00692860; // CMenu**
        constexpr uintptr_t kMenuManagerCurrentOffset = 0x0C;
        constexpr uintptr_t kMenuManagerBoxOffset = 0x10;

        constexpr uintptr_t kMenuFindByMenuId = 0x0043BF00; // __thiscall (manager, id) -> menu
        constexpr uintptr_t kCBaseMenuConstruct = 0x00451BE0; // __thiscall (block) -> block
        constexpr uintptr_t kMenuEnterMenu = 0x0043C5A0; // __thiscall (manager, id)
        constexpr uintptr_t kMenuGoBack = 0x0043C660; // __thiscall (manager)
        constexpr uintptr_t kMenuShowBox = 0x0043C610; // __thiscall (manager, id)
        constexpr uintptr_t kMenuCloseAll = 0x0043C6F0; // __thiscall (manager)

        constexpr uint32_t kSizeBaseMenu = 0x020C;

        constexpr uintptr_t kGameFrameTick = 0x00425000; // __cdecl (void)
        constexpr uintptr_t kFrameDelta = 0x0067A5F8;    // float

        // Render_Present  __thiscall (manager)  vtable +0x44
        // device @ manager+0x0C, device-lost @ +0x20
        // viewport info @ +0xA8, width +0xB4, height +0xB8

        // g_pfnDrawStyledText __cdecl (font, text, 0, centre[2], scale[2], colour[4], align, 0)
        constexpr uintptr_t kDrawStyledTextPtr = 0x00691308;

        // g_pfnDrawSprite __cdecl (sprite, x, y, w, h, 0, colour[4], a7, a8)
        constexpr uintptr_t kDrawSpritePtr = 0x006912F0;

        // g_pfnDrawStaticText __cdecl (a1, pos, a3, a4, a5, colour[4], a7)
        constexpr uintptr_t kDrawStaticTextPtr = 0x00691310;

        constexpr uintptr_t kSystemFont = 0x00503598;
        constexpr uintptr_t kMenuFindFont = 0x00444380; // __cdecl (const char* name) -> font

        // Slot 0 = render for every drawable element kind (except $table).
        constexpr uintptr_t kElementRenderSlot = 0x00;
        constexpr uintptr_t kElementVtables[] =
        {
            0x004D3564, // $item
            0x004D34D4, // $logo
            0x004D3464, // text_buttons $button
            0x004D33DC, // img_buttons $button
            0x004D3334, // hotkey_buttons $button
            0x004D32BC, // castle_buttons $button
            0x004D3154, // $list
            0x004D30DC, // $slider
        };

        constexpr uintptr_t kMenuItemRender = 0x0045F620; // __thiscall (element)
        constexpr uintptr_t kMenuDrawDecorations = 0x00451420; // __thiscall (menu)
        constexpr uintptr_t kRenderBegin = 0x0047B230; // __thiscall (manager)
        constexpr uintptr_t kRenderPresent = 0x0047A110; // __thiscall (manager)
        constexpr uintptr_t kRenderManagerPtr = 0x0067CB40; // CRenderState**
        constexpr uintptr_t kRenderDeviceOffset = 0x0C;       // IDirect3DDevice8*
        constexpr uintptr_t kRenderDeviceLostOffset = 0x20;
        constexpr uintptr_t kRenderViewportInfoOffset = 0xA8;
        constexpr uintptr_t kRenderViewportWidthOffset = 0x0C;
        constexpr uintptr_t kRenderViewportHeightOffset = 0x10;

        // Console: Config** @ kConfig, command table @ +0x08
        // GameConsoleWrite __cdecl (level, fmt, ...)
        // Cmd_Register __thiscall (config, name, handler)  handler is __cdecl(argc, argv)
        // Console_Execute __thiscall (config, line)
        constexpr uintptr_t kConfig = 0x0067A0D8;
        constexpr uintptr_t kConfigCommandsOffset = 0x08;
        constexpr uintptr_t kGameConsoleWrite = 0x004220C0;
        constexpr uintptr_t kCmdRegister = 0x00423990;
        constexpr uintptr_t kConsoleExecute = 0x004237B0;

        // Cmd node: +0x00 command, +0x04 name, +0x08 prev, +0x0C next
        // command object: +0x00 name, +0x04 handler
        constexpr uintptr_t kCmdFind = 0x004232F0; // __thiscall (table, name)
        constexpr size_t kCmdBuckets = 256;
        constexpr uintptr_t kCmdNodeCommandOffset = 0x00;
        constexpr uintptr_t kCmdNodeNameOffset = 0x04;
        constexpr uintptr_t kCmdNodePrevOffset = 0x08;
        constexpr uintptr_t kCmdNodeNextOffset = 0x0C;
        constexpr uintptr_t kCmdCommandNameOffset = 0x00;

        constexpr uintptr_t kMenuTakeClickResult = 0x00451660; // __thiscall (menu, dest[5])
        constexpr uintptr_t kClickResultMenuId = 0x00;
        constexpr uintptr_t kClickResultItemId = 0x04;
        constexpr uintptr_t kClickResultSubMenu = 0x08;
        constexpr uintptr_t kClickResultBoxMenu = 0x0C;
        constexpr uintptr_t kClickResultRegMenu = 0x10;

        constexpr uintptr_t kMenuParseScript = 0x004527A0; // __thiscall (menu, scriptFile)
        constexpr uintptr_t kMenuItemParse = 0x0045F550; // __thiscall (element, parser)
        constexpr uintptr_t kMenuFindItemById = 0x00451860; // __thiscall (menu, id)

        // Parser: buffer must be writable and outlive the parser.
        // keyPrefix 0x24 = '$'
        constexpr uintptr_t kParserCreateFromBuffer = 0x00420F70; // __cdecl (text, keyPrefix)
        constexpr uintptr_t kParserDestroy = 0x00421AB0; // __cdecl (parser)
        constexpr uintptr_t kParserEnterBlock = 0x00421B40; // __cdecl (parser, name)
        constexpr uintptr_t kParserNextBlock = 0x00421350; // __cdecl (parser)
        constexpr uintptr_t kParserLeaveBlock = 0x00421C60; // __cdecl (parser)

        constexpr uintptr_t kMenuAddElement = 0x004525C0; // __thiscall (menu, element)

        // Constructors: __thiscall (block) -> block, each writes its own vftable.
        constexpr uintptr_t kMenuItemCreate = 0x0045F4F0;
        constexpr uintptr_t kMenuLogoCreate = 0x0045F2A0;
        constexpr uintptr_t kTextButtonCreate = 0x0045EB80;
        constexpr uintptr_t kImageButtonCreate = 0x0045EAC0;
        constexpr uintptr_t kHotkeyButtonCreate = 0x0045E130;
        constexpr uintptr_t kCastleButtonCreate = 0x0045D380;
        constexpr uintptr_t kMenuListCreate = 0x0045BF50;
        constexpr uintptr_t kMenuSliderCreate = 0x0045B460;

        // Direct parsers (not via vtable):
        // MenuItem_Parse  (parser, float itemsOffs)  RET 8
        // MenuLogo_Parse  (parser, float itemsOffs)  RET 8
        // MenuList_Parse  (parser, Menu* menu)       RET 8
        //   MenuTable_Parse (parser)
        // Others go through vtable +0x30 (parser alone).
        constexpr uintptr_t kMenuLogoParse = 0x0045F2D0;
        constexpr uintptr_t kMenuListParse = 0x0045CAE0;
        constexpr uintptr_t kElementParseVtableSlot = 0x30;

        constexpr uintptr_t kMenuTableCreate = 0x00459790; // __thiscall (block)
        constexpr uintptr_t kMenuTableParse = 0x0045A0B0; // __thiscall (block, parser) RET 4
        constexpr uintptr_t kMenuAddTable = 0x00452780; // __thiscall (menu, block)
        constexpr uintptr_t kTableDestroyVtableSlot = 0x00;

        constexpr uintptr_t kMenuParseImages = 0x0044B320; // __cdecl (parser, "images")
        constexpr uintptr_t kMenuParseAnimatedImages = 0x0044B5C0; // __cdecl (parser)
        constexpr uintptr_t kMenuParseStaticText = 0x0044B7E0; // __cdecl (parser)

        constexpr uintptr_t kMemReallocTracked = 0x00421EC0; // (block, size, file, line)
        constexpr uint8_t kParserKeyPrefix = 0x24;           // '$'
        constexpr uintptr_t kElementDestroyVtableSlot = 0x34;

        constexpr uintptr_t kMenuIdOffset = 0x009;
        constexpr uintptr_t kMenuDisabledOffset = 0x209;

        // Manager: +0x08 registry, +0x38 count, +0x0C current, +0x10 overlay
        // +0x14 stack[8], +0x34 stack top (-1 = empty)
        constexpr uintptr_t kMenuManagerRegistryOffset = 0x08;
        constexpr uintptr_t kMenuManagerRegistryCountOffset = 0x38;
        constexpr uintptr_t kMenuManagerStackOffset = 0x14;
        constexpr uintptr_t kMenuManagerStackTopOffset = 0x34;
        constexpr size_t kMenuManagerStackCapacity = 8;
        constexpr uintptr_t kMenuGroupsOffset = 0x04C;
        constexpr uintptr_t kMenuGroupCountOffset = 0x050;
        constexpr uintptr_t kMenuGroupStride = 0x10C;
        constexpr uintptr_t kMenuGroupItemsOffset = 0x104;
        constexpr uintptr_t kMenuGroupItemCountOffset = 0x108;

        constexpr uintptr_t kMenuElementRectOffset = 0x294;
        constexpr uintptr_t kMenuElementPosOffset = 0x5B8;
        // Align bits: 0x02 right, 0x04 h-center, 0x08 top, 0x20 v-center
        constexpr uintptr_t kMenuElementAlignOffset = 0x5C8;
        constexpr uintptr_t kMenuElementCentreOffset = 0x5C0;
        constexpr uintptr_t kMenuElementVisibleOffset = 0x714;
        constexpr uintptr_t kMenuElementIdOffset = 0x715;
        constexpr uintptr_t kMenuElementCaptionRectOffset = 0x2A4;
        constexpr uintptr_t kMenuElementHasCaptionOffset = 0x2B4;

        constexpr uintptr_t kMenuItemPlankWidthOffset = 0x798;
        constexpr uintptr_t kMenuItemCursor3dOffset = 0x79C;
        constexpr uintptr_t kMenuItemCursorOrientOffset = 0x7A4;
        constexpr uintptr_t kMenuItemCursorPosOffset = 0x7B0;

        constexpr uintptr_t kTrackedHeaderSize = 0x118;
        constexpr uintptr_t kTrackedMagicOffset = 0x000;    // u16 0xAFAF
        constexpr uintptr_t kTrackedSizeOffset = 0x008;
        constexpr uint16_t kTrackedMagic = 0xAFAF;

        constexpr uint32_t kSizeMenuItem = 0x07C4;
        constexpr uint32_t kSizeMenuLogo = 0x0770;
        constexpr uint32_t kSizeTextButton = 0x09B8;
        constexpr uint32_t kSizeImageButton = 0x07DC;
        constexpr uint32_t kSizeHotkeyButton = 0x0BD4;
        constexpr uint32_t kSizeCastleButton = 0x2320;
        constexpr uint32_t kSizeMenuList = 0x08AC;
        constexpr uint32_t kSizeMenuSlider = 0x0788;
        constexpr uint32_t kSizeMenuTable = 0x00A0;

        constexpr uintptr_t kMenuItemTextOffset = 0x758;
        constexpr size_t kMenuItemTextCapacity = 0x798 - 0x758;

        constexpr uintptr_t kMenuImagesOffset = 0x074;
        constexpr uintptr_t kCollectionItemsOffset = 0x000;
        constexpr uintptr_t kCollectionCountOffset = 0x004;
        constexpr uintptr_t kImageStride = 0x28;
        constexpr uintptr_t kImageTextureOffset = 0x00;
        constexpr uintptr_t kImagePosOffset = 0x04;
        constexpr uintptr_t kImageIsOffsetOffset = 0x0C;
        constexpr uintptr_t kImageSizeOffset = 0x10;
        constexpr uintptr_t kImageHorzFlipOffset = 0x18;
        constexpr uintptr_t kImageVertFlipOffset = 0x19;
        constexpr uintptr_t kImageHorzReptOffset = 0x1C;
        constexpr uintptr_t kImageVertReptOffset = 0x20;
        constexpr uintptr_t kImageUnregOnlyOffset = 0x24;

        constexpr uintptr_t kMenuAnimImagesOffset = 0x078;
        constexpr uintptr_t kAnimImageStride = 0x18;
        constexpr uintptr_t kAnimImageUnregOnlyOffset = 0x10;
        constexpr uintptr_t kAnimImageAnimationOffset = 0x14;

        constexpr uintptr_t kMenuStaticTextOffset = 0x084;
        constexpr uintptr_t kStaticTextStride = 0x470;
        constexpr uintptr_t kStaticTextUnregOnlyOffset = 0x46C;

        constexpr uintptr_t kLogoSpriteNormalOffset = 0x758;
        constexpr uintptr_t kLogoSpriteFocusedOffset = 0x75C;
        constexpr uintptr_t kLogoSpriteOffset = 0x760;
        constexpr uintptr_t kLogoTwoSpritesOffset = 0x764;
        constexpr uintptr_t kLogoSwapOnFocusOffset = 0x765;
        constexpr uintptr_t kLogoSizeOffset = 0x768;

        constexpr uintptr_t kListItemsOffset = 0x758;
        constexpr uintptr_t kListItemCountOffset = 0x75C;
        constexpr size_t kListItemStride = 0x48;
        constexpr size_t kListItemTextCapacity = 0x40;
        constexpr uintptr_t kListColumnsOffset = 0x764;
        constexpr uintptr_t kListLeftRectOffset = 0x7B4;
        constexpr uintptr_t kListLeftKeyOffset = 0x7C8;
        constexpr uintptr_t kListTextRectOffset = 0x820;
        constexpr uintptr_t kListTextKeyOffset = 0x834;
        constexpr uintptr_t kListRightRectOffset = 0x88C;
        constexpr uintptr_t kListRightKeyOffset = 0x8A0;

        constexpr uintptr_t kMenuTablesOffset = 0x054;
        constexpr uintptr_t kMenuTableCountOffset = 0x058;

        constexpr uintptr_t kTableIdOffset = 0x004;
        constexpr uintptr_t kTableHeaderColorOffset = 0x044;
        constexpr uintptr_t kTableHeaderScaleOffset = 0x054;
        constexpr uintptr_t kTableBodyColorOffset = 0x05C;
        constexpr uintptr_t kTableBodyScaleOffset = 0x06C;
        constexpr uintptr_t kTableLinePosOffset = 0x074;
        constexpr uintptr_t kTableLineStepOffset = 0x078;
        constexpr uintptr_t kTableLineCountOffset = 0x07C;
        constexpr uintptr_t kTableLinesVertAlignOffset = 0x080;
        constexpr uintptr_t kTablePointStepOffset = 0x08C;
        constexpr uintptr_t kTablePointMarginOffset = 0x090;
        constexpr uintptr_t kTableFontIdOffset = 0x09C;
        constexpr uintptr_t kTableColumnsOffset = 0x098;
        constexpr uintptr_t kTableColumnCountOffset = 0x094;
        constexpr size_t kTableColumnStride = 0x50;
        constexpr uintptr_t kTableColumnTitleOffset = 0x00;
        constexpr uintptr_t kTableColumnHorzPosOffset = 0x40;
        constexpr uintptr_t kTableColumnAlignOffset = 0x44;
        constexpr uintptr_t kTableColumnFixedOffset = 0x4C;

        constexpr uintptr_t kButtonAnimOffset = 0x764;
        constexpr uintptr_t kCastleAnimOffset = 0xADC;

        constexpr uintptr_t kLauncherControlMap = 0x004FC180;
        constexpr uintptr_t kLauncherControlMapCount = 0x004FC204;

        constexpr uintptr_t kLauncherWindowOffset = 0x04;
        constexpr uintptr_t kLauncherLogoBitmapOffset = 0x0C;
        constexpr uintptr_t kLauncherLogoRectOffset = 0x10;
        constexpr uintptr_t kLauncherLogoDcOffset = 0x20;

        constexpr uintptr_t kLoadingSetProgress = 0x0043A560; // __thiscall (manager, percent)
        constexpr uintptr_t kGameStateManager = 0x006914A8;
        constexpr uintptr_t kGameStateIdOffset = 0x00;
        constexpr uintptr_t kGameStateCurrentOffset = 0x0C;
        constexpr int kGameStateLoading = 7;
        constexpr int kLoadingProgressComplete = 100;

        // GameState_SetEvent __thiscall (manager, id, a, b)
        // stores id@+0, a@+4, b@+8; leave via vtable+0x14, enter via +0x10
        constexpr uintptr_t kGameStateSetEvent = 0x0043A5E0;
        constexpr uintptr_t kGameStateArgOffset = 0x04;

        constexpr uintptr_t kLoadingTextOffset = 0x018;
        constexpr uintptr_t kLoadingInitialOffset = 0x134;
        constexpr uintptr_t kLoadingMusicStartedOffset = 0x135;
        constexpr uintptr_t kLoadingPercentOffset = 0x29C;
        constexpr uintptr_t kLoadingCaptionOffset = 0x2A0;
        constexpr size_t kLoadingCaptionLength = 0x100;
        constexpr uintptr_t kLoadingPressKeyOffset = 0x4A0;
        constexpr size_t kLoadingPressKeyLength = 0x108;
        constexpr uintptr_t kLoadingHelpTextOffset = 0x13C;
        constexpr size_t kLoadingHelpTextLength = 0x104;
        constexpr uintptr_t kLoadingWaitKeyOffset = 0x5A8;

        constexpr uintptr_t kInput = 0x0067CBB0;
        constexpr uintptr_t kInputKeyStateOffset = 0x3804;
        constexpr size_t kInputKeyCount = 0xFF;
        constexpr uintptr_t kInputCursorInWindowOffset = 0x3905;
        constexpr uintptr_t kInputSwallowingOffset = 0x3AD1;
        constexpr uintptr_t kInputAnyPressedOffset = 0x3AD2;

        // Both RET 8 → __stdcall
        constexpr uintptr_t kInputGetCursor = 0x00426940; // (float* x, float* y)
        constexpr uintptr_t kInputSetCursor = 0x004268B0; // (float x, float y)

        constexpr uintptr_t kGameWindow = 0x0067CB9C;   // HWND*

        constexpr uintptr_t kLevel = 0x00516D58; // CLevel (static)
        constexpr uintptr_t kLevelLoad = 0x0040E4B0; // __thiscall (level, script) -> bool
        constexpr uintptr_t kLevelFree = 0x0040E050; // __thiscall (level)
        constexpr uintptr_t kLevelActivateRoom = 0x0040E110; // __thiscall (level, dx, dy) -> room

        constexpr uintptr_t kLevelScriptOffset = 0x000;
        constexpr uintptr_t kLevelNextLevelOffset = 0x100;
        constexpr uintptr_t kLevelNameOffset = 0x200;
        constexpr uintptr_t kLevelEnableCastleOffset = 0x300;
        constexpr uintptr_t kLevelBackgroundOffset = 0x400;
        constexpr uintptr_t kLevelMusicOffset = 0x500;
        constexpr uintptr_t kLevelStartTitleOffset = 0x600;
        constexpr uintptr_t kLevelEndTitleOffset = 0x700;
        constexpr uintptr_t kLevelBonusOffset = 0x800;
        constexpr uintptr_t kLevelBonusTimeOffset = 0x804;
        constexpr uintptr_t kLevelBonusCountOffset = 0x808;
        constexpr uintptr_t kLevelTimeOffset = 0x80C;
        constexpr uintptr_t kLevelNumberOffset = 0x810;
        constexpr uintptr_t kLevelDemoLevelOffset = 0x814;
        constexpr uintptr_t kLevelUseForDemoOffset = 0x815;
        constexpr uintptr_t kLevelMusicStartOffset = 0x818;
        constexpr uintptr_t kLevelMenuStatOffset = 0x81C;
        constexpr uintptr_t kLevelStartRoomXOffset = 0x91C;
        constexpr uintptr_t kLevelStartRoomYOffset = 0x920;
        constexpr uintptr_t kLevelStartCellOffset = 0x924;
        constexpr size_t kLevelNameLength = 0x100;

        constexpr uintptr_t kLevelRoomsOffset = 0x92C;
        constexpr uintptr_t kLevelRoomCountOffset = 0x930;
        constexpr uintptr_t kLevelMazeOffset = 0x934;
        constexpr uintptr_t kLevelMazeHeightOffset = 0x938;
        constexpr uintptr_t kLevelMazeXOffset = 0x93C;
        constexpr uintptr_t kLevelMazeYOffset = 0x940;
        constexpr uintptr_t kLevelCurrentRoomIdOffset = 0x944;
        constexpr uintptr_t kLevelCurrentRoomOffset = 0x948;
        constexpr uintptr_t kLevelPreviousRoomOffset = 0x94C;

        constexpr size_t kMazeRowStride = 0x08;
        constexpr uintptr_t kMazeRowIdsOffset = 0x00;
        constexpr uintptr_t kMazeRowWidthOffset = 0x04;

        constexpr size_t kRoomEntryStride = 0x2794;
        constexpr uintptr_t kRoomEntryIdOffset = 0x0000;
        constexpr uintptr_t kRoomEntryRoomOffset = 0x0004;
        constexpr uintptr_t kRoomEntryFloorTexOffset = 0x2688;
        constexpr uintptr_t kRoomEntryWallTexOffset = 0x268C;
        constexpr uintptr_t kRoomEntryMusicOffset = 0x2690;

        constexpr uintptr_t kRoomMagicOffset = 0x0000;
        constexpr uintptr_t kRoomFileOffset = 0x0004;
        constexpr uintptr_t kRoomCellsOffset = 0x0104;
        constexpr uintptr_t kRoomGeometryModelOffset = 0x2554;
        constexpr uintptr_t kRoomLightmapModelOffset = 0x2558;
        constexpr uintptr_t kRoomBackWallModelOffset = 0x255C;
        constexpr uintptr_t kRoomLightmapTextureOffset = 0x2560;
        constexpr uintptr_t kRoomHasBackWallOffset = 0x2564;
        constexpr uintptr_t kRoomCollisionBoxesOffset = 0x2568;
        constexpr uintptr_t kRoomCollisionBoxCountOffset = 0x256C;
        constexpr uintptr_t kRoomLightTrianglesOffset = 0x2570;
        constexpr uintptr_t kRoomLightTriangleCountOffset = 0x2574;
        constexpr uintptr_t kRoomLoadedOffset = 0x2674;
        constexpr uintptr_t kRoomSpawnListOffset = 0x2678;
        constexpr uintptr_t kRoomSpawnCountOffset = 0x267C;
        constexpr uintptr_t kRoomEntitiesOffset = 0x2680;

        constexpr uintptr_t kEntities = 0x00517754;
        constexpr uintptr_t kEntitiesLiveOffset = 0x0C;
        constexpr uintptr_t kEntitiesFreeOffset = 0x10;
        constexpr uintptr_t kEntitiesTimeScaleOffset = 0x18;
        constexpr size_t kEntityPoolCount = 400;

        constexpr size_t kEntitySize = 0x5D4;
        constexpr uintptr_t kEntityDeltaOffset = 0x088;
        constexpr uintptr_t kEntityPositionOffset = 0x124;
        constexpr uintptr_t kEntityPreviousPositionOffset = 0x130;
        constexpr uintptr_t kEntityStateOffset = 0x178;
        constexpr uintptr_t kEntitySubStateOffset = 0x17C;
        constexpr uintptr_t kEntitySpawnOffset = 0x5CC;
        constexpr uintptr_t kEntityFlagsOffset = 0x48C;
        constexpr uintptr_t kEntityDeadOffset = 0x594;
        constexpr uintptr_t kEntityNextOffset = 0x5D0;

        constexpr uintptr_t kPlayerInventoryOffset = 0x2AC;
        constexpr size_t kInventorySlotStride = 0x60;
        constexpr size_t kInventorySlotCount = 5;
        constexpr uintptr_t kInventoryNameOffset = 0x00;
        constexpr uintptr_t kInventoryIconOffset = 0x40;
        constexpr uintptr_t kInventoryPointsOutOffset = 0x44;
        constexpr uintptr_t kInventorySoundUseOffset = 0x48;
        constexpr uintptr_t kInventoryTypeOffset = 0x4C;
        constexpr uintptr_t kInventoryFlagsOffset = 0x50;
        constexpr uintptr_t kInventoryTimeLeftOffset = 0x54;
        constexpr uintptr_t kInventoryTimeTotalOffset = 0x58;
        constexpr uintptr_t kInventoryBurnTimeOffset = 0x5C;

        // Inventory_IsCarrying __cdecl (player, name) -> bool
        // Inventory_FindSlot   __cdecl (player, name) -> slot
        // Inventory_FirstFree  __cdecl (player, int* index) -> slot
        // Inventory_TakeSlot   __cdecl (player, name, int* index) -> slot
        // Inventory_ClearSlot  __cdecl (player, slot)
        constexpr uintptr_t kInventoryIsCarrying = 0x004303D0;
        constexpr uintptr_t kInventoryFindSlot = 0x00430430;
        constexpr uintptr_t kInventoryFirstFree = 0x00430510;
        constexpr uintptr_t kInventoryTakeSlot = 0x00430550;
        constexpr uintptr_t kInventoryClearSlot = 0x00431130;

        constexpr uintptr_t kInventoryUpdate = 0x00431700;

        // Inventory_IsCarrying      83 FF 05   CMP EDI,5
        // Inventory_FindSlot        83 FE 05   CMP ESI,5
        // Inventory_FindSlotIndexed 83 FF 05   CMP EDI,5
        //   Inventory_FirstFreeSlot   83 F8 05   CMP EAX,5
        // Inventory_Update          BB 05..    MOV EBX,5
        // Hud_DrawInventory         BD 05..    MOV EBP,5

        struct SlotCountSite
        {
            uintptr_t address;      // the byte holding the count
            uint8_t opcode;         // what has to be in front of it for this to be that site
            uintptr_t opcodeAt;
        };

        // index is already in a register of its own at the three that compute one.

        // the player. An inventory of more than five therefore does not survive a save.
        constexpr size_t kInventorySlotsMax = 32;

        struct SlotBaseSite
        {
            uintptr_t address;
            uint8_t length;         // what the original occupies
            uint8_t prefixLength;   // the opcode of the replacement, before the address
            uint8_t prefix[2];
            uint32_t addend;        // added to the buffer, for a site that meant a field of a slot
            uint8_t original[7];
        };

        constexpr SlotBaseSite kSlotBaseSites[] =
        {
            // ADD ESI,0x2ac  ->  MOV ESI,buffer
            { 0x004303DD, 6, 1, { 0xBE, 0x00 }, 0, { 0x81, 0xC6, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // LEA EDI,[EBP+0x2ac]  ->  MOV EDI,buffer
            { 0x0043043E, 6, 1, { 0xBF, 0x00 }, 0, { 0x8D, 0xBD, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // LEA EAX,[EAX+EBP+0x2ac]  ->  ADD EAX,buffer   (EAX is the index, EBP the player)
            { 0x00430489, 7, 1, { 0x05, 0x00 }, 0, { 0x8D, 0x84, 0x28, 0xAC, 0x02, 0x00, 0x00 } },
            // LEA ESI,[EBP+0x2ac]  ->  MOV ESI,buffer
            { 0x004304AE, 6, 1, { 0xBE, 0x00 }, 0, { 0x8D, 0xB5, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // LEA EAX,[ECX+EBP+0x2ac]  ->  LEA EAX,[ECX+buffer]   (ECX is the index)
            { 0x00430500, 7, 2, { 0x8D, 0x81 }, 0, { 0x8D, 0x84, 0x29, 0xAC, 0x02, 0x00, 0x00 } },
            // LEA ECX,[EDX+0x2ac]  ->  MOV ECX,buffer
            { 0x00430516, 6, 1, { 0xB9, 0x00 }, 0, { 0x8D, 0x8A, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // LEA EAX,[EAX+EDX+0x2ac]  ->  ADD EAX,buffer   (EAX is the index, EDX the player)
            { 0x00430541, 7, 1, { 0x05, 0x00 }, 0, { 0x8D, 0x84, 0x10, 0xAC, 0x02, 0x00, 0x00 } },
            // LEA ESI,[EDI+0x2ac]  ->  MOV ESI,buffer
            { 0x00431708, 6, 1, { 0xBE, 0x00 }, 0, { 0x8D, 0xB7, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // ADD EDI,0x2ac  ->  MOV EDI,buffer
            { 0x0044E730, 6, 1, { 0xBF, 0x00 }, 0, { 0x81, 0xC7, 0xAC, 0x02, 0x00, 0x00, 0x00 } },

            // the size, and the loop behind it resolves each slot's type again by $res_id. The

            // is a save the game can still read.

            // ADD ECX,0x2ac  ->  MOV ECX,buffer
            { 0x00443FFA, 6, 1, { 0xB9, 0x00 }, 0, { 0x81, 0xC1, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // LEA ECX,[EAX+EBX+0x2ac]  ->  LEA ECX,[EBX+buffer]   (EBX walks the slots)
            { 0x00444013, 7, 2, { 0x8D, 0x8B }, 0, { 0x8D, 0x8C, 0x18, 0xAC, 0x02, 0x00, 0x00 } },
            // MOV [EDX+EBX+0x2ec],ECX  ->  MOV [EBX+buffer+0x40],ECX    the slot's icon
            { 0x00444030, 7, 2, { 0x89, 0x8B }, 0x40, { 0x89, 0x8C, 0x1A, 0xEC, 0x02, 0x00, 0x00 } },
            // MOV [EDX+EBX+0x2f4],ECX  ->  MOV [EBX+buffer+0x48],ECX    its use sound
            { 0x00444040, 7, 2, { 0x89, 0x8B }, 0x48, { 0x89, 0x8C, 0x1A, 0xF4, 0x02, 0x00, 0x00 } },
            // MOV [EDX+EBX+0x2f8],EAX  ->  MOV [EBX+buffer+0x4c],EAX    and its type
            { 0x0044404A, 7, 2, { 0x89, 0x83 }, 0x4C, { 0x89, 0x84, 0x1A, 0xF8, 0x02, 0x00, 0x00 } },
            // LEA ECX,[EAX+EBX+0x2ac]  ->  LEA ECX,[EBX+buffer]
            { 0x00444058, 7, 2, { 0x8D, 0x8B }, 0, { 0x8D, 0x8C, 0x18, 0xAC, 0x02, 0x00, 0x00 } },
        };

        constexpr SlotCountSite kSlotCountSites[] =
        {
            { 0x00430416, 0xFF, 0x00430415 },
            { 0x00430477, 0xFE, 0x00430476 },
            { 0x004304E4, 0xFF, 0x004304E3 },
            { 0x0043052B, 0xF8, 0x0043052A },
            { 0x0043170F, 0xBB, 0x0043170E },
            { 0x0044E737, 0xBD, 0x0044E736 },
        };

        constexpr uintptr_t kEntityKindOffset = 0x00;
        constexpr int kEntityKindPlayer = 1;
        constexpr uintptr_t kEntityNameOffset = 0x04;

        // The session object: Score_Add takes it as its this, the HUD reaches the player
        // through it. Null until a game is running.
        constexpr uintptr_t kGameSession = 0x006928F0;
        constexpr uintptr_t kSessionPlayerOffset = 0x04;
        constexpr uintptr_t kSessionScoreOffset = 0xBDC;

        // Score_Add __thiscall (session, points, const float position[3]); null position = the
        // player's. Scores, spawns the floating number, and awards the threshold life.
        constexpr uintptr_t kScoreAdd = 0x00442620;

        constexpr uintptr_t kChainLength = 0x00693544;
        constexpr uintptr_t kChainTimer = 0x00693548;
        constexpr uintptr_t kChainPoints = 0x0069354C;
        constexpr uintptr_t kChainWindow = 0x00694664;

        // What a chain of that length shows and plays: __thiscall (messages, length) ->

        // block of them; RET 4 is the length alone, which is what makes it look like a cdecl of
        // one argument until it is called that way and reads rubbish.
        constexpr uintptr_t kChainMessages = 0x00694650;
        constexpr uintptr_t kChainMessage = 0x0044BE60;

        // Plays one at a place, __cdecl (id, const float position[3], 0, volume, speed). A runtime
        // slot, like the resolvers.
        constexpr uintptr_t kPlayEffectSlot = 0x00691354;

        constexpr uintptr_t kBonusesLeft = 0x006946A8;
        constexpr uintptr_t kMegaBonusesLeft = 0x006946B0;
        constexpr uintptr_t kOtherBonusesLeft = 0x006946A0;

        constexpr uintptr_t kSpawnKindOffset = 0x00;
        constexpr size_t kSpawnNameLength = 0x40;
        constexpr int kSpawnKindMonster = 10;
        constexpr int kSpawnKindBonus = 9;
        constexpr int kSpawnKindDoor = 2;

        struct SpawnLayout
        {
            int kind;
            uintptr_t name;
            uintptr_t position;
            uintptr_t liveByte; // 0 = none
        };

        constexpr SpawnLayout kSpawnLayouts[] =
        {
            { kSpawnKindMonster, 0x04, 0x44, 0x00 },
            { kSpawnKindBonus,   0x05, 0x48, 0x04 },
            { kSpawnKindDoor,    0x18, 0x58, 0x00 },
        };

        // RoomSpawns_Append __cdecl (room, record)
        // Mem_Clone         __cdecl (source, size)
        constexpr uintptr_t kRoomSpawnsAppend = 0x00412F70;
        constexpr uintptr_t kMemClone = 0x0043FA60;

        // Placement_ReadState __cdecl (record, visitor)
        constexpr uintptr_t kPlacementReadState = 0x0042C810;

        // SpawnPlacement __cdecl (room, record)  slot filled at runtime
        constexpr uintptr_t kSpawnPlacementSlot = 0x00517710;

        constexpr size_t kBonusSpawnSize = 0xBC;
        constexpr uintptr_t kBonusSpawnLiveOffset = 0x04;
        constexpr uintptr_t kBonusSpawnNameOffset = 0x05;
        constexpr uintptr_t kBonusSpawnPositionOffset = 0x48;
        constexpr uintptr_t kBonusSpawnPhaseOffset = 0x54;      // Bonus_Think's rocking phase
        constexpr uintptr_t kBonusSpawnTagOffset = 0x64;        // $tag, 0x40
        constexpr uintptr_t kBonusSpawnDecorOffset = 0xA4;      // $decor, counts for nothing
        constexpr uintptr_t kBonusSpawnTimeOffset = 0xB4;       // overrides the type's $time
        constexpr uintptr_t kBonusSpawnTypeOffset = 0xB8;

        // Bonus_OnTouch __cdecl (bonus, toucher, side), reached through the entity's own slot, so a

        // touching pair, each with itself first.
        constexpr uintptr_t kBonusOnTouch = 0x00430B80;
        constexpr uintptr_t kBonusThink = 0x00430A90;
        // Entity_MoveStep __cdecl (state, dirX, dirY, dirZ, delta, flags) -> hit something.

        constexpr uintptr_t kEntityMoveStep = 0x0043F140;
        constexpr uintptr_t kEntityMoveStateOffset = 0x120;

        constexpr uintptr_t kEntityThinkOffset = 0x5B8;
        constexpr uintptr_t kEntityTouchHandlerOffset = 0x5BC;

        // out of the type, added to the position. Nothing writes the position itself.
        constexpr uintptr_t kEntityRockingAxisOffset = 0x4FC;
        constexpr uintptr_t kEntityRockingOffset = 0x508;

        // writing the type changes the ones spawned after.
        constexpr uintptr_t kEntityModelOffset = 0x274;
        constexpr uintptr_t kEntityTextureOffset = 0x278;
        constexpr uintptr_t kBonusSpawnOwnModelOffset = 0x58;   // nonzero: the two below are used
        constexpr uintptr_t kBonusSpawnModelOffset = 0x5C;
        constexpr uintptr_t kBonusSpawnTextureOffset = 0x60;

        constexpr uintptr_t kMonsterResTable = 0x00691458;
        constexpr uintptr_t kMonsterResCount = 0x0069145C;
        constexpr size_t kMonsterResStride = 0xDC;
        constexpr uintptr_t kMonsterResNameOffset = 0x000;      // $name, 0x40
        constexpr uintptr_t kMonsterResTexEasyOffset = 0x040;
        constexpr uintptr_t kMonsterResTexHardOffset = 0x044;
        constexpr uintptr_t kMonsterResModelOffset = 0x048;
        constexpr uintptr_t kMonsterResAnimationOffset = 0x04C;
        constexpr uintptr_t kMonsterResSpeedUpOffset = 0x050;
        constexpr uintptr_t kMonsterResSpeedOffset = 0x054;
        constexpr uintptr_t kMonsterResVertSpeedOffset = 0x058;
        constexpr uintptr_t kMonsterResPartColorOffset = 0x05C;      // four floats
        constexpr uintptr_t kMonsterResPartColorHardOffset = 0x06C;
        constexpr uintptr_t kMonsterResBoxOffset = 0x07C;            // 0x1c, as a room's boxes are
        constexpr uintptr_t kMonsterResBox2Offset = 0x098;           // both fall back to $bbox
        constexpr uintptr_t kMonsterResObjBoxOffset = 0x0B4;
        constexpr uintptr_t kMonsterResJumpOffset = 0x0D0;           // "j_skull" alone reads these
        constexpr uintptr_t kMonsterResJumpFactorOffset = 0x0D4;
        constexpr uintptr_t kMonsterResFallOffset = 0x0D8;

        // letting them through changes no monster the game already has.
        constexpr uintptr_t kMonsterJumpKeysGuard = 0x004302B9;
        constexpr uint8_t kMonsterJumpKeysGuardBytes[] = { 0x75, 0x4F };

        constexpr uintptr_t kMonsterDieBlast = 0x00691400;      // $die_blast, 0x2c
        constexpr uintptr_t kMonsterDieBlastHard = 0x0069142C;

        constexpr uintptr_t kMonstersLoadResources = 0x0042FFD0;
        constexpr uintptr_t kMonstersFreeResources = 0x0042F660;
        constexpr uintptr_t kMonsterResAppend = 0x0042FF60;     // (record**, int*) -> record
        constexpr uintptr_t kMonsterResFind = 0x0042F5C0;       // (name) -> record, by $name
        constexpr uintptr_t kMonsterParseSpawn = 0x0042F730;
        constexpr uintptr_t kMonsterKill = 0x0042FDB0;          // __fastcall (entity)

        // cached in the last dword.
        constexpr size_t kMonsterSpawnSize = 0x124;
        constexpr uintptr_t kMonsterSpawnNameOffset = 0x004;    // $name, 0x40
        constexpr uintptr_t kMonsterSpawnPositionOffset = 0x044;
        constexpr uintptr_t kMonsterSpawnLevelOffset = 0x050;   // $level; 1 and 3 are the hard ones
        constexpr uintptr_t kMonsterSpawnLiveOffset = 0x054;    // cleared by Monster_Kill
        constexpr uintptr_t kMonsterSpawnSpeedOffset = 0x058;   // $speed, doubled when $level is hard
        constexpr uintptr_t kMonsterSpawnDirOffset = 0x064;     // $dir
        constexpr uintptr_t kMonsterSpawnAttackDistOffset = 0x068;
        constexpr uintptr_t kMonsterSpawnAttackTimeOffset = 0x06C;   // every $*_time is two floats,
        constexpr uintptr_t kMonsterSpawnBluffTimeOffset = 0x074;    // a range the monster picks in
        constexpr uintptr_t kMonsterSpawnRadiusOffset = 0x07C;       // the circling monsters
        constexpr uintptr_t kMonsterSpawnAxisOffset = 0x080;
        constexpr uintptr_t kMonsterSpawnPhaseOffset = 0x08C;
        constexpr uintptr_t kMonsterSpawnPosLerpOffset = 0x090;
        constexpr uintptr_t kMonsterSpawnTagOffset = 0x09C;     // $tag, 0x40
        constexpr uintptr_t kMonsterSpawnDecorOffset = 0x0DC;
        constexpr uintptr_t kMonsterSpawnMoveTypeOffset = 0x0F0;
        constexpr uintptr_t kMonsterSpawnWalkTimeOffset = 0x100;
        constexpr uintptr_t kMonsterSpawnWalkWaitOffset = 0x108;
        constexpr uintptr_t kMonsterSpawnSitTimeOffset = 0x110;
        constexpr uintptr_t kMonsterSpawnWaitTimeOffset = 0x118;
        constexpr uintptr_t kMonsterSpawnTypeOffset = 0x120;    // the definition record

        // and how long until it changes. Both are rolled and stepped by Monster_Think.
        constexpr uintptr_t kMonsterSpawnStateOffset = 0x0E0;   // 0 "move", 1 "sit"
        constexpr uintptr_t kMonsterSpawnTimerOffset = 0x0E8;
        constexpr int kMonsterStateMove = 0;
        constexpr int kMonsterStateSit = 1;

        // A live monster. The placement is kind 10, the entity itself is kind 2.
        constexpr int kEntityKindMonster = 2;
        constexpr uintptr_t kMonsterEntityTagOffset = 0x044;
        constexpr uintptr_t kMonsterEntityObjBoxOffset = 0x158;
        constexpr uintptr_t kMonsterEntitySpeedUpOffset = 0x190;
        constexpr uintptr_t kMonsterEntitySpeedOffset = 0x224;
        constexpr uintptr_t kMonsterEntityPlayerOffset = 0x24C;  // cached by the first think
        constexpr uintptr_t kMonsterEntityLevelOffset = 0x4C8;
        constexpr uintptr_t kMonsterEntityHardOffset = 0x4E4;    // 1.0, or 2.0 for a hard $level
        constexpr uintptr_t kMonsterEntityFacingOffset = 0x588;  // -1 or 1
        constexpr uintptr_t kEntityCollidesOffset = 0x595;

        // the single door a mod can stand in.

        // kept, and never becomes anything.
        // Monster_ParseSpawn __cdecl (room, grid, parser). Keys on $name as well, so a name the

        // in the same block and the placement only appended, not spawned.
        constexpr uintptr_t kMonsterParseSpawnRoutine = 0x0042F730;

        constexpr uintptr_t kParserFindKey = 0x00421560;
        constexpr uintptr_t kParserReadFloat4 = 0x00421740;

        constexpr float kMonsterCellZ = 0.2f;

        constexpr uintptr_t kMonsterSpawnPlacement = 0x0042FD60;
        constexpr uintptr_t kMonsterSpawnTable = 0x004CE4F8;
        constexpr size_t kMonsterSpawnTableCount = 10;

        // bat, which dies through an animation instead of at once.
        constexpr uintptr_t kMonsterOnHit = 0x00447200;
        constexpr uintptr_t kEntityHitHandlerOffset = 0x5C4;

        // what chose the creator, but nothing in the entity spells the kind out.
        struct MonsterMovement
        {
            const char* name;
            uintptr_t think;

            // instead, every frame. So which of the two is "the speed" depends on the kind.
            bool speedInEntity;
        };

        constexpr MonsterMovement kMonsterMovements[] =
        {
            { "skull",    0x00448070, true  },
            { "snake",    0x00447AB0, false },
            { "snake2",   0x00447AC0, false },
            { "spider",   0x004474B0, true  },   // the scorpion takes this one on one of its branches
            { "scorpion", 0x00446E40, true  },
            { "j_skull",  0x00446800, true  },
            { "bat",      0x004463C0, false },
            { "mummy",    0x004460A0, true  },
            { "mummy2",   0x00445C30, true  },
        };

        // never touch those fields.
        constexpr uintptr_t kMonsterThinkMummy = 0x004460A0;
        constexpr uintptr_t kMonsterThinkMummy2 = 0x00445C30;

        constexpr uintptr_t kMonstersKilled = 0x00694694;

        constexpr uintptr_t kBonusResFind = 0x00430680; // __cdecl (name) -> type record

        constexpr float kCellToWorld = 0.1f;
        constexpr float kCellOriginX = -2.0f;
        constexpr float kCellOriginY = 1.45f;
        constexpr float kCellCentre = 0.05f;
        constexpr float kBonusHeight = 0.1f;

        constexpr uintptr_t kBonusResTable = 0x0069148C;
        constexpr uintptr_t kBonusResCount = 0x00691490;
        constexpr size_t kBonusResStride = 0x118;
        constexpr uintptr_t kBonusResNameOffset = 0x000;
        constexpr uintptr_t kBonusResIdOffset = 0x040;
        constexpr uintptr_t kBonusResTextureOffset = 0x080;
        constexpr uintptr_t kBonusResTexAnimOffset = 0x084;
        constexpr uintptr_t kBonusResModelOffset = 0x088;
        constexpr uintptr_t kBonusResIconOffset = 0x08C;        // $icon, what the HUD draws
        constexpr uintptr_t kBonusResGlowOffset = 0x090;
        constexpr uintptr_t kBonusResGlow2Offset = 0x0A0;
        constexpr uintptr_t kBonusResPointsTakeOffset = 0x0B0;
        constexpr uintptr_t kBonusResPointsOutOffset = 0x0B4;
        constexpr uintptr_t kBonusResBoxOffset = 0x0B8;
        constexpr uintptr_t kBonusResSoundTakeOffset = 0x0D4;
        constexpr uintptr_t kBonusResSoundUseOffset = 0x0D8;
        constexpr uintptr_t kBonusResFlagsOffset = 0x0DC;

        // the entity at spawn, so changing it only takes on the next spawn.
        constexpr uintptr_t kBonusResRockingAmpOffset = 0x0E0;
        constexpr uintptr_t kBonusResRockingSpeedOffset = 0x0E4;
        constexpr uintptr_t kBonusResRockingAxisOffset = 0x0E8;
        constexpr uintptr_t kBonusResTrainIndexOffset = 0x0F4;
        constexpr uintptr_t kBonusResTimeOffset = 0x0F8;
        constexpr uintptr_t kBonusResLightColorOffset = 0x0FC;
        constexpr uintptr_t kBonusResLightRadiusOffset = 0x10C;
        constexpr uintptr_t kBonusResBonusLightRadiusOffset = 0x110;
        constexpr uintptr_t kBonusResBurnTimeOffset = 0x114;

        constexpr uint32_t kBonusFlagTotem = 0x01;
        constexpr uint32_t kBonusFlagTorch = 0x02;
        constexpr uint32_t kBonusFlagMakesTrain = 0x04;
        constexpr uint32_t kBonusFlagSuppressed = 0x08;         // slot only: skip the game's tick

        // BonusRes_Append __stdcall (record**, int* count) -> the new record. RET 8, not the cdecl

        // with no level loaded.
        constexpr uintptr_t kBonusResAppend = 0x00431690;

        // The gameplay HUD: one static object, built by Hud_Init @0x0044e0f0 out of

        // author already knows. Zeroes until then.
        constexpr uintptr_t kHud = 0x006947D8;
        constexpr uintptr_t kHudInit = 0x0044E0F0;

        constexpr uintptr_t kHudPbarOffsOffset = 0x2C;          // two floats
        constexpr uintptr_t kHudBonusPulseScaleOffset = 0x34;
        constexpr uintptr_t kHudLifePulseScaleOffset = 0x38;
        constexpr uintptr_t kHudLifePulseTimeOffset = 0x3C;
        constexpr uintptr_t kHudItemStartOffset = 0x44;         // $items $start_pos
        constexpr uintptr_t kHudItemOffsOffset = 0x4C;          // $items $offs, slot to slot
        constexpr uintptr_t kHudItemSizeOffset = 0x54;          // $items $size
        constexpr uintptr_t kHudItemFrameOffset = 0x5C;         // $item_frame, its texture
        constexpr uintptr_t kHudItemFrameSizeOffset = 0x6C;     // and its size
        constexpr uintptr_t kHudPointsPosOffset = 0xC4;
        // The script's fields end at 0xCC; the quest-bonus bag begins there (QuestBag_Load
        // 0x0044EC60), two heap pointers the game refills every level and frees.
        constexpr size_t kHudSize = 0xD0;

        // One record is 0x28 bytes and is what Image_ParseRecord reads a $image into.

        // Image_ParseRecord __cdecl (parser, record)
        // Images_Append     __stdcall (record**, int* count) -> the new record   RET 8, not cdecl

        // the count and removing records shortens it: nothing may be written past it.
        constexpr uintptr_t kHudImagesOffset = 0x10;            // $standard_level $images
        constexpr uintptr_t kHudBonusImagesOffset = 0x14;       // $bonus_level $images
        constexpr uintptr_t kImageParseRecord = 0x00449950;
        constexpr uintptr_t kImagesAppend = 0x0044AE40;
        constexpr size_t kImageRecordSize = 0x28;
        constexpr uintptr_t kImagesArrayOffset = 0x00;
        constexpr uintptr_t kImagesCountOffset = 0x04;

        // Bonuses_FreeResources __cdecl (), called once from the shutdown block. Releases the

        // records holding the same handle release it twice.
        constexpr uintptr_t kBonusesFreeResources = 0x00430700;

        // The resolvers the parsers go through, __cdecl (name) -> id, 0 when there is no such
        // resource. Runtime slots, so they are read when needed.
        constexpr uintptr_t kResolveModelSlot = 0x006912B8;
        constexpr uintptr_t kResolveTextureSlot = 0x006912A4;
        constexpr uintptr_t kResolveSoundSlot = 0x00691344;

        constexpr size_t kRoomGridColumns = 40;
        constexpr size_t kRoomGridRows = 29;
        constexpr size_t kRoomCellStride = 0x08;

        // Collision box 0x1C: minZ, minX, minY, gap, maxX, maxY, maxZ
        constexpr size_t kRoomCollisionBoxStride = 0x1C;
        constexpr uintptr_t kRoomBoxMinZOffset = 0x00;
        constexpr uintptr_t kRoomBoxMinXOffset = 0x04;
        constexpr uintptr_t kRoomBoxMinYOffset = 0x08;
        constexpr uintptr_t kRoomBoxMaxXOffset = 0x10;
        constexpr uintptr_t kRoomBoxMaxYOffset = 0x14;
        constexpr uintptr_t kRoomBoxMaxZOffset = 0x18;
    }

    // Package_OpenFile: PUSH ESI / MOV ESI,[ESP+8]  → 5 bytes
    constexpr uint8_t kPackageOpenFilePrologue[] = { 0x56, 0x8B, 0x74, 0x24, 0x08 };

    // App_Shutdown: PUSH EBX / PUSH 0x004cccb0  → 6 bytes (immediate must stay intact)
    constexpr uint8_t kAppShutdownPrologue[] = { 0x53, 0x68, 0xB0, 0xCC, 0x4C, 0x00 };

    // Launcher_OnInitDialog: PUSH ESI / PUSH EDI / PUSH 3 / PUSH 0x004c7bdc  → 9
    constexpr uint8_t kLauncherOnInitDialogPrologue[] =
    { 0x56, 0x57, 0x6A, 0x03, 0x68, 0xDC, 0x7B, 0x4C, 0x00 };
    // Launcher_OnCommand: PUSH -1 / PUSH 0x004c5548  → 7
    constexpr uint8_t kLauncherOnCommandPrologue[] =
    { 0x6A, 0xFF, 0x68, 0x48, 0x55, 0x4C, 0x00 };
    // Dialog_RunModal: MOV EAX,[ESP+0xC] / TEST EAX,EAX  → 6
    constexpr uint8_t kDialogRunModalPrologue[] =
    { 0x8B, 0x44, 0x24, 0x0C, 0x85, 0xC0 };

    // Launcher_HideOptionalButtons: PUSH ESI / PUSH 0 / PUSH 0x3EC  → 8
    constexpr uint8_t kLauncherHideOptionalButtonsPrologue[] =
    { 0x56, 0x6A, 0x00, 0x68, 0xEC, 0x03, 0x00, 0x00 };

    // Menu_Register: MOV EAX,[ESP+8] / PUSH EBX  → 5
    constexpr uint8_t kMenuRegisterPrologue[] =
    { 0x8B, 0x44, 0x24, 0x08, 0x53 };
    // Menu_ParseScript: PUSH EBP / MOV EBP,ESP / PUSH -1  → 5
    constexpr uint8_t kMenuParseScriptPrologue[] =
    { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
    // Menu_LoadFromScript: PUSH ESI / PUSH EDI / MOV EDI,[ESP+0xC]  → 6
    constexpr uint8_t kMenuLoadFromScriptPrologue[] =
    { 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C };
    // MenuItem_Parse: PUSH ESI / PUSH EDI / MOV EDI,[ESP+0xC]  → 6
    constexpr uint8_t kMenuItemParsePrologue[] =
    { 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C };

    // Menu_TakeClickResult: MOV EDX,[ECX+0x5c] / MOV EAX,[ESP+0x4]  → 7
    constexpr uint8_t kMenuTakeClickResultPrologue[] =
    { 0x8B, 0x51, 0x5C, 0x8B, 0x44, 0x24, 0x04 };

    // Game_FrameTick: SUB ESP,0x50 / PUSH ESI / PUSH EDI  → 5
    constexpr uint8_t kGameFrameTickPrologue[] =
    { 0x83, 0xEC, 0x50, 0x56, 0x57 };

    // Render_Begin: PUSH ESI / MOV ESI,ECX / CMP dword [ESI+0x0C],0  → 7
    constexpr uint8_t kRenderBeginPrologue[] =
    { 0x56, 0x8B, 0xF1, 0x83, 0x7E, 0x0C, 0x00 };

    // Render_Present: PUSH ESI / MOV ESI,ECX / CMP byte [ESI+0x20],0  → 7
    constexpr uint8_t kRenderPresentPrologue[] =
    { 0x56, 0x8B, 0xF1, 0x80, 0x7E, 0x20, 0x00 };

    // MenuItem_Render: SUB ESP,0x10 / PUSH ESI / MOV ESI,ECX  → 6
    constexpr uint8_t kMenuItemRenderPrologue[] =
    { 0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1 };

    // Menu_DrawDecorations: SUB ESP,0x0C / PUSH ESI / MOV ESI,ECX  → 6
    constexpr uint8_t kMenuDrawDecorationsPrologue[] =
    { 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1 };

    // Level_ActivateRoom: PUSH ESI / MOV ESI,ECX / MOV EAX,[ESI+0x934]  → 9
    constexpr uint8_t kLevelActivateRoomPrologue[] =
    { 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x34, 0x09, 0x00, 0x00 };

    // Level_Load: PUSH -1 / PUSH 0x004c55bb  → 7
    constexpr uint8_t kLevelLoadPrologue[] =
    { 0x6A, 0xFF, 0x68, 0xBB, 0x55, 0x4C, 0x00 };
    // Level_Free: PUSH ESI / PUSH EDI / PUSH 0x004c8b7c  → 7
    constexpr uint8_t kLevelFreePrologue[] =
    { 0x56, 0x57, 0x68, 0x7C, 0x8B, 0x4C, 0x00 };

    // Bonuses_FreeResources: PUSH 0x4ce7a0  → 5
    constexpr uint8_t kBonusesFreeResourcesPrologue[] =
    { 0x68, 0xA0, 0xE7, 0x4C, 0x00 };

    // Bonus_OnTouch: PUSH ECX / PUSH EBX / PUSH EBP / MOV EBP,[ESP+0x10]  → 7
    constexpr uint8_t kBonusOnTouchPrologue[] =
    { 0x51, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x10 };

    // Loading_SetProgress: PUSH ESI / MOV ESI,ECX / CMP [ESI],7  → 6
    constexpr uint8_t kLoadingSetProgressPrologue[] =
    { 0x56, 0x8B, 0xF1, 0x83, 0x3E, 0x07 };

    // Menu_RegisterAll: PUSH ESI / PUSH 0 / PUSH 0  → 5
    constexpr uint8_t kMenuRegisterAllPrologue[] =
    { 0x56, 0x6A, 0x00, 0x6A, 0x00 };

    // Monster_SpawnPlacement: PUSH EDI / MOV EDI,[ESP+8]  → 5. __cdecl, RET with no operand.
    constexpr uint8_t kMonsterSpawnPlacementPrologue[] =
    { 0x57, 0x8B, 0x7C, 0x24, 0x08 };

    // Monster_Kill: SUB ESP,0xc / PUSH ESI / MOV ESI,[ESP+0x14]  → 8. __cdecl.
    constexpr uint8_t kMonsterKillPrologue[] =
    { 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0x74, 0x24, 0x14 };

    // Monster_ParseSpawn: SUB ESP,0x15c  → 6, one instruction. __cdecl.
    constexpr uint8_t kMonsterParseSpawnPrologue[] =
    { 0x81, 0xEC, 0x5C, 0x01, 0x00, 0x00 };

    // Monsters_FreeResources: CMP [0x00691458],0  → 7
    constexpr uint8_t kMonstersFreeResourcesPrologue[] =
    { 0x83, 0x3D, 0x58, 0x14, 0x69, 0x00, 0x00 };

    // The six touch handlers, one per family, all __cdecl (monster, other, side). Between them
    // they hold every rule of what a monster does to the player and the player to it.
    struct MonsterTouchSite
    {
        uintptr_t address;
        uint8_t prologue[8];
        size_t length;
    };

    constexpr MonsterTouchSite kMonsterTouchSites[] =
    {
        { 0x00445D90, { 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C },             6 },  // mummy, mummy2
        { 0x00446570, { 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C },             6 },  // bat
        { 0x004469F0, { 0x53, 0x56, 0x8B, 0x74, 0x24, 0x0C },             6 },  // j_skull
        { 0x00447790, { 0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10 },       7 },  // spider, scorpion
        { 0x00447CD0, { 0x53, 0x8B, 0x5C, 0x24, 0x08 },                   5 },  // the snakes
        { 0x00448270, { 0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10 },       7 },  // skull
    };
}
