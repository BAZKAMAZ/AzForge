#pragma once

#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
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
        constexpr uintptr_t kRenderDeviceOffset = 0x0C; // IDirect3DDevice8*
        constexpr uintptr_t kRenderDeviceLostOffset = 0x20;
        constexpr uintptr_t kRenderViewportInfoOffset = 0xA8;
        constexpr uintptr_t kRenderViewportWidthOffset = 0x0C;
        constexpr uintptr_t kRenderViewportHeightOffset = 0x10;

        constexpr uintptr_t kMenuTakeClickResult = 0x00451660; // __thiscall (menu, dest[5])
        constexpr uintptr_t kClickResultMenuId = 0x00;
        constexpr uintptr_t kClickResultItemId = 0x04;
        constexpr uintptr_t kClickResultSubMenu = 0x08;
        constexpr uintptr_t kClickResultBoxMenu = 0x0C;
        constexpr uintptr_t kClickResultRegMenu = 0x10;

        constexpr uintptr_t kMenuParseScript = 0x004527A0; // __thiscall (menu, scriptFile)
        constexpr uintptr_t kMenuItemParse = 0x0045F550; // __thiscall (element, parser)
        constexpr uintptr_t kMenuFindItemById = 0x00451860; // __thiscall (menu, id)

        // NEW GAME reads the hovered id without checking it exists.
        constexpr uintptr_t kNewGameOnClick = 0x004582B0; // __thiscall (menu, x, y), RET 8
        constexpr uintptr_t kMenuOnClick = 0x00452310; // __thiscall (menu, x, y)
        constexpr uintptr_t kMenuHoveredInGroup = 0x00451920; // __thiscall (menu, group) -> element or null

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

        // Direct parsers (not vtable): Item/Logo RET 8; List RET 8; Table RET 4; others vtable +0x30
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

        constexpr uintptr_t kMenuIdOffset = 0x009;
        constexpr uintptr_t kMenuDisabledOffset = 0x209;

        // Manager: +0x08 registry, +0x38 count, +0x0C current, +0x10 overlay; stack[8] @ +0x14, top @ +0x34
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
    }
}
