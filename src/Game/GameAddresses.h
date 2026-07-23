#pragma once

#include <stdint.h>

namespace Forge
{
    // Addresses reversed from the shipped game.exe, which is linked at 0x00400000.
    //
    // These are hints, not the addresses AzForge binds to. GameSymbols identifies every
    // routine by a byte signature and only consults the hint to skip a scan when the code
    // is where it has always been. Keeping the hints is still worth it: they document the
    // reverse, they make the log readable, and they turn resolution into a lookup rather
    // than a search on the build everyone is actually running.
    namespace GameAddress
    {
        constexpr uintptr_t kPreferredImageBase   = 0x00400000;

        constexpr uintptr_t kPackageOpenFile      = 0x00422F00; // disk -> data.pak dispatcher
        constexpr uintptr_t kPackageReadFromDisk  = 0x00422660;
        constexpr uintptr_t kPackageReadFromPak   = 0x00422CA0;
        constexpr uintptr_t kPackageFree          = 0x00422200;

        constexpr uintptr_t kMemAllocTracked      = 0x00421DF0; // __cdecl (size, file, line)
        constexpr uintptr_t kMemFreeTracked       = 0x00421CE0; // __cdecl (block)
        constexpr uintptr_t kNameBufferAssign     = 0x004022B0; // __thiscall (dst, src)

        constexpr uintptr_t kAppShutdown          = 0x00424EF0; // last call in WinMain

        constexpr uintptr_t kLauncherOnInitDialog = 0x0040C7E0; // vtable +0x04
        constexpr uintptr_t kLauncherOnCommand    = 0x0040C5D0; // vtable +0x0C
        constexpr uintptr_t kDialogRunModal       = 0x00424520; // DialogBoxParamA wrapper

        // Runs after the dialog script has been applied and force-hides the registration
        // and purchase controls whatever the script asked for.
        constexpr uintptr_t kLauncherHideOptionalButtons = 0x0040C3B0;

        // Dialog template of the launcher inside the .rsrc of game.exe.
        constexpr uintptr_t kLauncherDialogTemplate = 101;

        // Parses a menu, stamps its $menu_id and puts it into the manager's registry. This
        // is how a menu first comes into being; Menu_LoadFromScript is the reload path for
        // one that is already registered.
        constexpr uintptr_t kMenuRegister         = 0x0043CA40; // __thiscall (manager, menu, script, menuId, force)

        constexpr uintptr_t kMenuLoadFromScript   = 0x0043C090; // __stdcall (menuId, scriptFile)

        // The menu manager is a global pointer. It is itself a CMenu; the 28 built-in menus
        // are sub-objects embedded in it, and the registry is a separate dynamic array of
        // pointers, so a heap menu can be added to it.
        constexpr uintptr_t kMenuManager             = 0x00692860; // CMenu**
        constexpr uintptr_t kMenuManagerCurrentOffset = 0x0C;       // the current menu
        constexpr uintptr_t kMenuManagerBoxOffset     = 0x10;       // the overlay over it

        // Registering and navigating menus. Menu_Register (above) parses a script FILE into a
        // menu and appends it to the registry; a null script with force set makes an empty
        // registered menu, which is how IDM_LEVEL_STAT comes to be and how a mod adds one.
        constexpr uintptr_t kMenuFindByMenuId  = 0x0043BF00; // __thiscall (manager, id) -> menu
        constexpr uintptr_t kCBaseMenuConstruct = 0x00451BE0; // __thiscall (block) -> block
        constexpr uintptr_t kMenuEnterMenu     = 0x0043C5A0; // __thiscall (manager, id), pushes
        constexpr uintptr_t kMenuGoBack        = 0x0043C660; // __thiscall (manager)
        constexpr uintptr_t kMenuShowBox       = 0x0043C610; // __thiscall (manager, id), overlay
        constexpr uintptr_t kMenuCloseAll      = 0x0043C6F0; // __thiscall (manager)

        constexpr uint32_t kSizeBaseMenu = 0x020C;
        constexpr uintptr_t kMenuParseScript      = 0x004527A0; // __thiscall (menu, scriptFile)
        constexpr uintptr_t kMenuItemParse        = 0x0045F550; // __thiscall (element, parser)
        constexpr uintptr_t kMenuFindItemById     = 0x00451860; // __thiscall (menu, id)

        // What Menu_ParseScript uses to turn text into objects, and what AzForge calls to do
        // the same with a fragment a mod supplies.
        //
        // The parser is built over a plain NUL terminated buffer - Menu_ParseScript only
        // reaches for Package_OpenFile to get one - and it keeps the pointer rather than a
        // copy, having first stripped the comments out of it in place. So the text has to be
        // writable and has to outlive the parser.
        //
        // The second argument is the character that introduces a key: 0x24 is '$'.
        constexpr uintptr_t kParserCreateFromBuffer = 0x00420F70; // __cdecl (text, keyPrefix)
        constexpr uintptr_t kParserDestroy          = 0x00421AB0; // __cdecl (parser)
        constexpr uintptr_t kParserEnterBlock       = 0x00421B40; // __cdecl (parser, name)
        constexpr uintptr_t kParserNextBlock        = 0x00421350; // __cdecl (parser)
        constexpr uintptr_t kParserLeaveBlock       = 0x00421C60; // __cdecl (parser)

        // Menu_AddElement does far more than append: it finds or creates the group named by
        // $group, grows the group's array, writes the element's back pointer to the menu and
        // rebuilds the circular navigation ring across the whole group. Reproducing that by
        // hand is exactly the mistake feeding the parser avoids.
        constexpr uintptr_t kMenuAddElement       = 0x004525C0; // __thiscall (menu, element)

        // One constructor per kind. Each is __thiscall on the freshly allocated block and
        // answers it, and each writes its own vftable, which is what makes them tellable
        // apart from one another at all.
        constexpr uintptr_t kMenuItemCreate       = 0x0045F4F0;
        constexpr uintptr_t kMenuLogoCreate       = 0x0045F2A0;
        constexpr uintptr_t kTextButtonCreate     = 0x0045EB80;
        constexpr uintptr_t kImageButtonCreate    = 0x0045EAC0;
        constexpr uintptr_t kHotkeyButtonCreate   = 0x0045E130;
        constexpr uintptr_t kCastleButtonCreate   = 0x0045D380;
        constexpr uintptr_t kMenuListCreate       = 0x0045BF50;
        constexpr uintptr_t kMenuSliderCreate     = 0x0045B460;

        // Four kinds are parsed by a routine Menu_ParseScript calls directly rather than
        // through the vtable, and each cleans up its own stack, so the argument list is not
        // a matter of taste:
        //
        //   MenuItem_Parse  (parser, float itemsOffs)  RET 8
        //   MenuLogo_Parse  (parser, float itemsOffs)  RET 8
        //   MenuList_Parse  (parser, Menu* menu)       RET 8
        //   MenuTable_Parse (parser)
        //
        // The other five go through vtable +0x30, which takes the parser alone.
        constexpr uintptr_t kMenuLogoParse        = 0x0045F2D0;
        constexpr uintptr_t kMenuListParse        = 0x0045CAE0;
        constexpr uintptr_t kElementParseVtableSlot = 0x30;

        // A table: its own constructor and parser, and Menu_AddTable to link it into the
        // array at menu+0x54. MenuTable_Parse is __thiscall (block, parser), RET 4, and
        // answers a bool rather than throwing - it behaves like an element parser even
        // though a table is not one.
        constexpr uintptr_t kMenuTableCreate      = 0x00459790; // __thiscall (block)
        constexpr uintptr_t kMenuTableParse       = 0x0045A0B0; // __thiscall (block, parser)
        constexpr uintptr_t kMenuAddTable         = 0x00452780; // __thiscall (menu, block)

        // A table is destroyed through vtable slot 0, not the 0x34 an element uses.
        constexpr uintptr_t kTableDestroyVtableSlot = 0x00;

        // The three decoration parsers, all __cdecl and all answering a whole collection
        // rather than adding to one - Menu_ParseScript stores what they return straight into
        // the menu. They also throw rather than answering false when their block is missing,
        // which is why the fragment is inspected before any of them is called.
        constexpr uintptr_t kMenuParseImages         = 0x0044B320; // (parser, "images")
        constexpr uintptr_t kMenuParseAnimatedImages = 0x0044B5C0; // (parser)
        constexpr uintptr_t kMenuParseStaticText     = 0x0044B7E0; // (parser)

        // Grows a tracked block, keeping what was in it.
        constexpr uintptr_t kMemReallocTracked = 0x00421EC0; // (block, size, file, line)

        constexpr uint8_t kParserKeyPrefix = 0x24;              // '$'

        // How the game disposes of an element whose block the parser refused: the scalar
        // deleting destructor, which frees the block as well as running the destructor.
        constexpr uintptr_t kElementDestroyVtableSlot = 0x34;

        // Menu object.
        constexpr uintptr_t kMenuIdOffset = 0x009;          // $menu_id
        constexpr uintptr_t kMenuGroupsOffset = 0x04C;
        constexpr uintptr_t kMenuGroupCountOffset = 0x050;
        constexpr uintptr_t kMenuGroupStride = 0x10C;
        constexpr uintptr_t kMenuGroupItemsOffset = 0x104;
        constexpr uintptr_t kMenuGroupItemCountOffset = 0x108;

        // Menu element, common part parsed by MenuButton_ParseCommon @0x0045a600.
        constexpr uintptr_t kMenuElementRectOffset = 0x294;     // $rect_size, four floats
        constexpr uintptr_t kMenuElementPosOffset = 0x5B8;      // $pos, two floats
        // Horizontal and vertical alignment packed into one word by MenuButton_ParseCommon
        // @0x0045a600: 0x02 right, 0x04 horizontally centred, 0x08 top, 0x20 vertically
        // centred, and the absence of a bit meaning left or bottom.
        constexpr uintptr_t kMenuElementAlignOffset = 0x5C8;

        // What MenuItem_Render @0x0045f620 actually draws at: the middle of the rectangle,
        // worked out once during parsing. Writing the rectangle without bringing this along
        // changes nothing on screen.
        constexpr uintptr_t kMenuElementCentreOffset = 0x5C0;

        constexpr uintptr_t kMenuElementVisibleOffset = 0x714;
        constexpr uintptr_t kMenuElementIdOffset = 0x715;       // $id

        // The rectangle of the $caption block, derived from $rect_len, and the flag saying
        // the block was there at all. A logo overwrites the rectangle with its own edges.
        constexpr uintptr_t kMenuElementCaptionRectOffset = 0x2A4;
        constexpr uintptr_t kMenuElementHasCaptionOffset = 0x2B4;

        // What MenuItem_Parse @0x0045f550 adds on top of the common part.
        constexpr uintptr_t kMenuItemPlankWidthOffset = 0x798;
        constexpr uintptr_t kMenuItemCursor3dOffset = 0x79C;    // $cursor3d block present
        constexpr uintptr_t kMenuItemCursorOrientOffset = 0x7A4; // three floats
        constexpr uintptr_t kMenuItemCursorPosOffset = 0x7B0;    // three floats

        // Menu_ParseScript @0x004527a0 builds nine kinds of element, each a different size.
        // Mem_AllocTracked @0x00421df0 keeps the requested size in its header, so the kind
        // of any element can be read back from the block it lives in without the loader
        // having to watch every parser.
        constexpr uintptr_t kTrackedHeaderSize = 0x118;
        constexpr uintptr_t kTrackedMagicOffset = 0x000;    // u16 0xAFAF
        constexpr uintptr_t kTrackedSizeOffset = 0x008;     // u32 requested size
        constexpr uint16_t kTrackedMagic = 0xAFAF;

        constexpr uint32_t kSizeMenuItem = 0x07C4;      // $menu_items / $item
        constexpr uint32_t kSizeMenuLogo = 0x0770;      // $menu_items / $logo
        constexpr uint32_t kSizeTextButton = 0x09B8;    // $text_buttons / $button
        constexpr uint32_t kSizeImageButton = 0x07DC;   // $img_buttons / $button
        constexpr uint32_t kSizeHotkeyButton = 0x0BD4;  // $hotkey_buttons / $button
        constexpr uint32_t kSizeCastleButton = 0x2320;  // $castle_buttons / $button
        constexpr uint32_t kSizeMenuList = 0x08AC;      // $lists / $list
        constexpr uint32_t kSizeMenuSlider = 0x0788;    // $sliders / $slider
        constexpr uint32_t kSizeMenuTable = 0x00A0;     // $tables / $table

        // Only present on elements MenuItem_Parse @0x0045f550 built, and the field
        // MenuItem_Render @0x0045f620 actually draws.
        //
        // The capacity is not a guess: the next field MenuItem_Parse writes is $plank_width
        // at +0x798, so the text occupies everything up to it.
        constexpr uintptr_t kMenuItemTextOffset = 0x758;
        constexpr size_t kMenuItemTextCapacity = 0x798 - 0x758;

        // Decorative $images, filled by Menu_ParseImages @0x0044b320. The menu keeps a
        // pointer to an eight byte collection of { void* items; int count; }, and each
        // entry is 40 bytes laid out by FUN_00449950 @0x00449950. They are not individual
        // allocations, so their kind cannot be read from an allocator header.
        constexpr uintptr_t kMenuImagesOffset = 0x074;
        constexpr uintptr_t kCollectionItemsOffset = 0x000;
        constexpr uintptr_t kCollectionCountOffset = 0x004;
        constexpr uintptr_t kImageStride = 0x28;

        constexpr uintptr_t kImageTextureOffset = 0x00;     // $texture, resolved handle
        constexpr uintptr_t kImagePosOffset = 0x04;         // $pos, or $offs when +0x0C set
        constexpr uintptr_t kImageIsOffsetOffset = 0x0C;
        constexpr uintptr_t kImageSizeOffset = 0x10;        // $size
        constexpr uintptr_t kImageHorzFlipOffset = 0x18;
        constexpr uintptr_t kImageVertFlipOffset = 0x19;
        constexpr uintptr_t kImageHorzReptOffset = 0x1C;    // $horz_rept, default 1
        constexpr uintptr_t kImageVertReptOffset = 0x20;    // $vert_rept, default 1
        constexpr uintptr_t kImageUnregOnlyOffset = 0x24;   // $unreg_only

        // $images_anim, filled by Menu_ParseAnimatedImages @0x0044b5c0. Same collection
        // shape, records of 24 bytes written field by field at the end of that routine:
        // position, size, $unreg_only and the animation object.
        constexpr uintptr_t kMenuAnimImagesOffset = 0x078;
        constexpr uintptr_t kAnimImageStride = 0x18;
        constexpr uintptr_t kAnimImageUnregOnlyOffset = 0x10;
        constexpr uintptr_t kAnimImageAnimationOffset = 0x14;

        // $static_text, filled by Menu_ParseStaticText @0x0044b7e0. Records of 0x470 bytes
        // laid out by FUN_0044a540 @0x0044a540, which the castle button also uses for its
        // own $text_castle block.
        constexpr uintptr_t kMenuStaticTextOffset = 0x084;
        constexpr uintptr_t kStaticTextStride = 0x470;
        constexpr uintptr_t kStaticTextUnregOnlyOffset = 0x46C;

        // $logo, laid out by MenuLogo_Parse @0x0045f2d0. Two sprites and the size its
        // rectangle is built from. A logo is the one element that does not go through
        // Layout_ApplyAlignment: the parser hangs the edges off $pos by hand and keeps a
        // second copy of them right behind the first.
        constexpr uintptr_t kLogoSpriteNormalOffset = 0x758;    // $sp1
        constexpr uintptr_t kLogoSpriteFocusedOffset = 0x75C;   // $sp2
        constexpr uintptr_t kLogoSpriteOffset = 0x760;          // the one being drawn
        constexpr uintptr_t kLogoTwoSpritesOffset = 0x764;      // both $sp1 and $sp2 loaded
        constexpr uintptr_t kLogoSwapOnFocusOffset = 0x765;
        constexpr uintptr_t kLogoSizeOffset = 0x768;            // $size

        // $list, laid out by MenuList_Parse @0x0045cae0. Its $items block is a run of $text
        // entries in records of 0x48 bytes: the text, then two words MenuList_AddItem
        // @0x0045ca50 leaves at zero for a scripted entry.
        //
        // The array and its count are two fields of the list rather than a collection of
        // their own, and the count is an int, not a pointer to the end: MenuList_AddItem
        // grows the array through FUN_0045c650 @0x0045c650, which reallocates to
        // (count + 1) * 0x48 and returns the new last record.
        constexpr uintptr_t kListItemsOffset = 0x758;
        constexpr uintptr_t kListItemCountOffset = 0x75C;
        constexpr size_t kListItemStride = 0x48;
        constexpr size_t kListItemTextCapacity = 0x40;

        // How many entries fit across, worked out from the width of the rectangle.
        constexpr uintptr_t kListColumnsOffset = 0x764;

        // The three sub-buttons a list draws itself from, at +0x768, +0x7d4 and +0x840, each
        // 0x6c bytes with its rectangle at +0x4c and the key that works it at +0x60. They
        // come out of MenuList_Parse as "left", "text" and "right", bound to VK_LEFT, space
        // and VK_RIGHT.
        constexpr uintptr_t kListLeftRectOffset = 0x7B4;
        constexpr uintptr_t kListLeftKeyOffset = 0x7C8;
        constexpr uintptr_t kListTextRectOffset = 0x820;
        constexpr uintptr_t kListTextKeyOffset = 0x834;
        constexpr uintptr_t kListRightRectOffset = 0x88C;
        constexpr uintptr_t kListRightKeyOffset = 0x8A0;

        // $tables. Menu_AddTable @0x00452780 keeps them in an array of pointers of its own,
        // right behind the groups, which is why walking the groups never turns one up. Two
        // of the shipped menus are built from nothing else.
        constexpr uintptr_t kMenuTablesOffset = 0x054;
        constexpr uintptr_t kMenuTableCountOffset = 0x058;

        // $table, laid out by MenuTable_Parse @0x0045a0b0. Not an element: it has no $pos,
        // no $rect_size and no vtable parse slot, and it lays its rows out from $line_pos,
        // $line_step and $line_count instead.
        constexpr uintptr_t kTableIdOffset = 0x004;             // $id, 0x40 bytes
        constexpr uintptr_t kTableHeaderColorOffset = 0x044;
        constexpr uintptr_t kTableHeaderScaleOffset = 0x054;
        constexpr uintptr_t kTableBodyColorOffset = 0x05C;
        constexpr uintptr_t kTableBodyScaleOffset = 0x06C;
        constexpr uintptr_t kTableLinePosOffset = 0x074;
        constexpr uintptr_t kTableLineStepOffset = 0x078;
        constexpr uintptr_t kTableLineCountOffset = 0x07C;
        constexpr uintptr_t kTableLinesVertAlignOffset = 0x080;
        constexpr uintptr_t kTablePointStepOffset = 0x08C;      // defaults to 5
        constexpr uintptr_t kTablePointMarginOffset = 0x090;    // defaults to 10
        constexpr uintptr_t kTableFontIdOffset = 0x09C;

        // Its $col blocks, in the same { records; count } shape the list uses for $items.
        // A column is 0x50 bytes: $title, $horz_pos, $align and $fixed.
        constexpr uintptr_t kTableColumnsOffset = 0x098;
        constexpr uintptr_t kTableColumnCountOffset = 0x094;
        constexpr size_t kTableColumnStride = 0x50;
        constexpr uintptr_t kTableColumnTitleOffset = 0x00;     // 0x40 bytes
        constexpr uintptr_t kTableColumnHorzPosOffset = 0x40;
        constexpr uintptr_t kTableColumnAlignOffset = 0x44;
        constexpr uintptr_t kTableColumnFixedOffset = 0x4C;

        // $item_anim, laid out by ButtonAnim_Parse @0x004445e0: five colours of four floats
        // followed by five scales of two, 0x78 bytes in all. Only the two image kinds carry
        // one. A castle button keeps the one ImageButton_Parse gave it and a second of its
        // own, both filled from the same block but falling back to different templates that
        // Menu_LoadMenuCfg @0x00444fb0 reads out of menu.cfg.
        constexpr uintptr_t kButtonAnimOffset = 0x764;
        constexpr uintptr_t kCastleAnimOffset = 0xADC;

        // Control name map used by Launcher_ApplyItemLayout: entries of
        // { u32 controlId, const char* section, const char* key }, count in a separate dword.
        constexpr uintptr_t kLauncherControlMap = 0x004FC180;
        constexpr uintptr_t kLauncherControlMapCount = 0x004FC204;

        // Launcher object layout, reached through the ECX of its virtual methods.
        constexpr uintptr_t kLauncherWindowOffset = 0x04;
        constexpr uintptr_t kLauncherLogoBitmapOffset = 0x0C;
        constexpr uintptr_t kLauncherLogoRectOffset = 0x10; // x, y, width, height
        constexpr uintptr_t kLauncherLogoDcOffset = 0x20;
    }

    // Package_OpenFile @0x00422f00 opens with two whole, position-independent instructions
    // that together occupy exactly the five bytes an inline jump needs:
    //   56              PUSH ESI
    //   8B 74 24 08     MOV ESI,dword ptr [ESP + 0x8]
    constexpr uint8_t kPackageOpenFilePrologue[] = { 0x56, 0x8B, 0x74, 0x24, 0x08 };

    // App_Shutdown @0x00424ef0 opens with
    //   53              PUSH EBX
    //   68 B0 CC 4C 00  PUSH 0x004cccb0
    // so six bytes have to be taken to keep the immediate intact; nothing inside the
    // routine jumps into that range. The immediate is an address in this build, which is
    // why a build where it differs is refused rather than patched: the executable carries
    // no relocations, so an address that moved means the code was rebuilt, not relocated.
    constexpr uint8_t kAppShutdownPrologue[] = { 0x53, 0x68, 0xB0, 0xCC, 0x4C, 0x00 };

    // Each of the launcher targets opens with a PUSH of a 32-bit immediate that must not be
    // cut in half, so more than the five bytes a jump needs are taken and the remainder is
    // padded with NOPs.
    //
    //   Launcher_OnInitDialog  PUSH ESI / PUSH EDI / PUSH 3 / PUSH 0x004c7bdc   -> 9
    //   Launcher_OnCommand     PUSH -1 / PUSH 0x004c5548                        -> 7
    //   Dialog_RunModal        MOV EAX,[ESP+0xC] / TEST EAX,EAX                 -> 6
    constexpr uint8_t kLauncherOnInitDialogPrologue[] =
        { 0x56, 0x57, 0x6A, 0x03, 0x68, 0xDC, 0x7B, 0x4C, 0x00 };
    constexpr uint8_t kLauncherOnCommandPrologue[] =
        { 0x6A, 0xFF, 0x68, 0x48, 0x55, 0x4C, 0x00 };
    constexpr uint8_t kDialogRunModalPrologue[] =
        { 0x8B, 0x44, 0x24, 0x0C, 0x85, 0xC0 };

    //   Launcher_HideOptionalButtons  PUSH ESI / PUSH 0 / PUSH 0x3EC                 -> 8
    constexpr uint8_t kLauncherHideOptionalButtonsPrologue[] =
        { 0x56, 0x6A, 0x00, 0x68, 0xEC, 0x03, 0x00, 0x00 };

    //   Menu_ParseScript      PUSH EBP / MOV EBP,ESP / PUSH -1                       -> 5
    //   Menu_LoadFromScript   PUSH ESI / PUSH EDI / MOV EDI,[ESP+0xC]                -> 6
    //   MenuItem_Parse        PUSH ESI / PUSH EDI / MOV EDI,[ESP+0xC]                -> 6
    //   Menu_Register         MOV EAX,[ESP+8] / PUSH EBX                              -> 5
    constexpr uint8_t kMenuRegisterPrologue[] =
        { 0x8B, 0x44, 0x24, 0x08, 0x53 };
    constexpr uint8_t kMenuParseScriptPrologue[] =
        { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
    constexpr uint8_t kMenuLoadFromScriptPrologue[] =
        { 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C };
    constexpr uint8_t kMenuItemParsePrologue[] =
        { 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C };
}
