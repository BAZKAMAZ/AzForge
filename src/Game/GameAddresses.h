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

        // Menu_Update @0x0043C7C0 clears these five globals, drains the clicked menu's
        // pending result into them through Menu_TakeClickResult, then acts on them. They are
        // the click result: which menu, which button, and the $sub_menu, $box_menu and
        // $reg_menu that button carried. Hooking the drain is the point where the click is
        // known but not yet acted on.
        // WinMain's idle work: Game_FrameTick runs once per frame when no window message is
        // waiting. It works out the frame delta, keeps it in a global in seconds, and drives
        // the current game state's update through its vtable. Hooking it is where a mod gets
        // a frame of its own, in every state - menu and gameplay alike.
        constexpr uintptr_t kGameFrameTick = 0x00425000; // __cdecl (void)
        constexpr uintptr_t kFrameDelta = 0x0067A5F8;    // float, seconds since last frame

        // The Direct3D 8 render manager (CRenderState @0x00695c68), reached through the
        // pointer the game keeps at kRenderManagerPtr. CRenderState::Present is the last
        // render call each frame: on entry the scene is still open (CRenderState::Begin ran
        // BeginScene), and it calls IDirect3DDevice8::EndScene then Present. Hooking its entry
        // is where a mod draws an overlay over the finished frame, before EndScene.
        //
        //   Render_Present  __thiscall (manager)   vtable +0x44, @0x0047a110
        //
        // The device is at manager+0x0C, and manager+0x20 is a device-lost flag that Present
        // and Begin both honour. The backbuffer size lives in an embedded struct at
        // manager+0xA8, width at +0xB4 and height at +0xB8.
        // The game's styled-text draw is an indirect call through the global function pointer
        // at 0x00691308, __cdecl with eight arguments:
        //   draw(font, text, 0, centre[2], scale[2], colour[4], align, 0)
        // scale points at two floats (x,y from ItemAnim_GetScale) and colour at four (RGBA
        // 0..1 from ItemAnim_GetColor). The pointer is null until the game sets it at run time,
        // so a hook swaps it lazily. Swapping it lets a mod recolour, fade or resize a native
        // item's label while the game keeps drawing it.
        constexpr uintptr_t kDrawStyledTextPtr       = 0x00691308; // void(__cdecl*)(...)

        // The low-level sprite blit is another runtime-set __cdecl pointer, at 0x006912f0:
        //   draw(sprite, x, y, w, h, 0, colour[4], a7, a8)
        // Menu_DrawSprite @0x00449b90 calls it with the colour computed by ItemAnim_GetColor, so
        // the sprite kinds (img_button, castle) draw through it; MenuLogo_Render passes a null
        // colour. Swapping it lets a mod recolour or fade a native sprite element.
        constexpr uintptr_t kDrawSpritePtr           = 0x006912F0; // void(__cdecl*)(...)

        // The $static_text decoration draw is another runtime-set __cdecl pointer, at 0x00691310,
        // used only by StaticText_DrawItem @0x0044a6b0:
        //   draw(a1, pos, a3, a4, a5, colour[4], a7)   -- seven args, colour is arg index 5
        // colour points at four floats in the static_text record (record+0x454). A $static_text
        // is a decoration, not a vtable element, so this is how a mod recolours it.
        constexpr uintptr_t kDrawStaticTextPtr       = 0x00691310; // void(__cdecl*)(...)

        // The system font (scripts/fnt_sys2.txt) is a static font object at 0x00503598, built by
        // LoadGameFont @0x004258a0 at render init. It is the font the help text and info readouts
        // draw with, and the default a mod's Render::Text hands to g_pfnDrawStyledText.
        constexpr uintptr_t kSystemFont              = 0x00503598;

        // Menu_FindFont @0x00444380 resolves a font by the name it was given in the menu_fonts
        // block of menu_cfg.txt (bigitem, multy_e, and so on) to its font object, or the default
        // font for a null or unknown name. __cdecl (const char* name) -> font. The registry it
        // searches is filled by Menu_LoadMenuCfg @0x00444fb0, entries of 0x44 bytes, name at +0
        // and the font object at +0x40.
        constexpr uintptr_t kMenuFindFont            = 0x00444380;

        // Every menu element kind draws through slot 0 of its own vtable (the render method):
        // MenuItem_Render is slot 0 of CMenuButton::vftable, and each other kind has its render
        // at slot 0 of its vftable. Patching that one slot in each vtable with a shared thunk
        // wraps the draw of every element of every kind - the universal element-draw hook.
        // The vtable each element carries at element+0 identifies its kind, so the thunk finds
        // the original render to chain to. Confirmed renders: item 0x0045f620, logo 0x0045f3e0,
        // img_button 0x0045e840, slider 0x0045b9d0.
        // Slot 0 is the render for every drawable element kind. A $table is the exception: its
        // slot 0 is the scalar deleting destructor (kTableDestroyVtableSlot below), not a render,
        // so it must not be patched - and a table is not drawn as an ordinary element anyway.
        constexpr uintptr_t kElementRenderSlot = 0x00;
        constexpr uintptr_t kElementVtables[] =
        {
            0x004D3564, // menu_items  $item   (CMenuButton::vftable, slot0 = MenuItem_Render)
            0x004D34D4, // $logo                (slot0 = MenuLogo_Render)
            0x004D3464, // text_buttons $button (slot0 = TextButton_Render)
            0x004D33DC, // img_buttons  $button (slot0 = ImageButton_Render)
            0x004D3334, // hotkey_buttons $button (slot0 = HotkeyButton_Render)
            0x004D32BC, // castle_buttons $button (slot0 = CastleButton_Render)
            0x004D3154, // lists $list          (slot0 = MenuList_Render)
            0x004D30DC, // sliders $slider      (slot0 = MenuSlider_Render)
        };

        // MenuItem_Render @0x0045f620 draws a $item's plank and label (the label through
        // g_pfnDrawStyledText @0x00691308). It is the item's vtable render, called once per
        // visible $item each frame; detouring it lets a mod draw before or after the game's
        // own draw of an item, or replace it. __thiscall (element). The element's $id is the
        // inline string at element+0x715 (kMenuElementIdOffset).
        constexpr uintptr_t kMenuItemRender          = 0x0045F620;

        // Menu_DrawDecorations @0x00451420 draws a menu's background - the $image scene, animated
        // images and the background tile - before Menu_DrawElements draws the text and buttons on
        // top. Detouring it and skipping the original lets a mod draw a custom background in its
        // place. __thiscall (menu); the menu's $menu_id is the inline string at menu+0x09.
        constexpr uintptr_t kMenuDrawDecorations     = 0x00451420;

        // CRenderState::Begin (manager vtable +0x40, @0x0047b230) opens the frame: it calls
        // IDirect3DDevice8::BeginScene and returns non-zero when the frame will be drawn. It runs
        // before the game draws the world and menu, so a detour that fires after it is where a mod
        // sets render state that affects the whole frame (wireframe, and so on) or draws a layer
        // under everything. __thiscall (manager).
        constexpr uintptr_t kRenderBegin             = 0x0047B230;

        constexpr uintptr_t kRenderPresent           = 0x0047A110;
        constexpr uintptr_t kRenderManagerPtr        = 0x0067CB40; // CRenderState**
        constexpr uintptr_t kRenderDeviceOffset      = 0x0C;       // IDirect3DDevice8*
        constexpr uintptr_t kRenderDeviceLostOffset  = 0x20;       // byte, set while lost
        constexpr uintptr_t kRenderViewportInfoOffset = 0xA8;
        constexpr uintptr_t kRenderViewportWidthOffset = 0x0C;     // within the info struct
        constexpr uintptr_t kRenderViewportHeightOffset = 0x10;

        // The developer console (nsConsole): a Quake-style console with named commands, cvars
        // and text output. The command table hangs off a global config singleton at +0x08.
        //
        //   GameConsoleWrite(level, fmt, ...)   __cdecl variadic; level 0 needs the
        //                                       'developer' cvar, level 1 always writes
        //   Cmd_Register(config, name, handler) __thiscall, handler is __cdecl(argc, argv)
        //   Console_Execute(config, line)       __thiscall, runs a command line
        constexpr uintptr_t kConfig            = 0x0067A0D8; // Config** singleton
        constexpr uintptr_t kConfigCommandsOffset = 0x08;
        constexpr uintptr_t kGameConsoleWrite  = 0x004220C0;
        constexpr uintptr_t kCmdRegister       = 0x00423990;
        constexpr uintptr_t kConsoleExecute    = 0x004237B0;

        constexpr uintptr_t kMenuTakeClickResult = 0x00451660; // __thiscall (menu, dest[5])
        constexpr uintptr_t kClickResultMenuId = 0x00;    // dest indices, each a char*
        constexpr uintptr_t kClickResultItemId = 0x04;
        constexpr uintptr_t kClickResultSubMenu = 0x08;
        constexpr uintptr_t kClickResultBoxMenu = 0x0C;
        constexpr uintptr_t kClickResultRegMenu = 0x10;
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

        // Both ways into a menu refuse one whose flag at +0x209 is set: Menu_EnterMenu
        // @0x0043c5a0 and Menu_ShowBox @0x0043c610 each test it after the registry lookup.
        // Setting it is therefore how a menu is taken out of service without touching the
        // navigation the game may already be holding it in.
        constexpr uintptr_t kMenuDisabledOffset = 0x209;

        // The menu manager's registry and its navigation. The registry is the array of every
        // registered menu; the navigation is what the player walked through to get here.
        //
        //   +0x08 CMenu**  the registry              +0x38 int  how many
        //   +0x0c CMenu*   the menu on screen        +0x10 CMenu*  the overlay over it
        //   +0x14 CMenu*[8] the menus stepped out of, and +0x34 the top, -1 when empty
        //
        // Menu_EnterMenu pushes the current menu onto that stack and Menu_GoBack pops it, so
        // a menu can be off the registry and still be reachable through the stack. That is
        // what makes freeing one unsafe while the manager can still name it.
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

        // The loading screen, and the startup load that runs behind it.
        //
        // Loading_SetProgress @0x0043a560 __thiscall (manager, percent) moves the bar and
        // draws a frame for it. The startup load calls it at 30, 40, 55, 70, 85, 95 and 100,
        // and the menu manager is built and every menu registered between 40 and 55 - so by
        // the time it reaches 100 the game is loaded, the menus exist, and the loading screen
        // is still the state on screen. That is where mods are loaded: late enough that a mod
        // can add a menu, early enough that the player has not left the screen.
        //
        // It does nothing unless the manager is in the loading state, so calling it to repaint
        // between mods is safe wherever it is called from.
        constexpr uintptr_t kLoadingSetProgress = 0x0043A560;
        constexpr uintptr_t kGameStateManager   = 0x006914A8;

        constexpr uintptr_t kGameStateIdOffset = 0x00;      // 7 while the loading screen is up
        constexpr uintptr_t kGameStateCurrentOffset = 0x0C; // the CEventLoad while it is
        constexpr int kGameStateLoading = 7;
        constexpr int kLoadingProgressComplete = 100;

        // Inside CEventLoad, laid out by Loading_Parse @0x0044fce0. $wait_key is what holds
        // the screen: Loading_ShouldStay @0x0044ff20 keeps the game on it while the flag is
        // set and no key has been pressed, so clearing it is what lets the game fall through
        // to the main menu.
        constexpr uintptr_t kLoadingPressKeyOffset = 0x4A0;  // the line along the bottom
        constexpr size_t kLoadingPressKeyLength = 0x108;
        constexpr uintptr_t kLoadingWaitKeyOffset = 0x5A8;   // a bool

        // The game world. There is one level at a time and it is a static object rather than
        // a pointer, so the address is the level: an empty one before the first is loaded and
        // again after Level_Free, which is why a mod asks whether a level is loaded rather
        // than whether the pointer is null.
        //
        //   Level_Load          __thiscall (level, script)  @0x0040e4b0 -> bool
        //   Level_Free          __thiscall (level)          @0x0040e050
        //   Level_ActivateRoom  __thiscall (level, dx, dy)  @0x0040e110 -> room, or null
        //   Level_FindRoomById  __thiscall (level, id)      @0x0040d9c0 -> entry, or null
        //   Level_FindRoomPos   __thiscall (level, id, x, y)@0x0040d670 -> bool
        //
        // Level_ActivateRoom is the only way a room becomes current, the first one included,
        // so it is where a mod learns the player moved between rooms.
        constexpr uintptr_t kLevel                = 0x00516D58; // CLevel, not a pointer
        constexpr uintptr_t kLevelLoad            = 0x0040E4B0;
        constexpr uintptr_t kLevelFree            = 0x0040E050;
        constexpr uintptr_t kLevelActivateRoom    = 0x0040E110;

        // CLevel, laid out by Level_ParseHeader @0x0040d320 and Level_Load. Every name is a
        // fixed 0x100-byte buffer, so a level with no $music holds an empty string rather
        // than a null.
        constexpr uintptr_t kLevelScriptOffset       = 0x000; // the script it was loaded from
        constexpr uintptr_t kLevelNextLevelOffset    = 0x100; // $next_level
        constexpr uintptr_t kLevelNameOffset         = 0x200; // $level_name
        constexpr uintptr_t kLevelEnableCastleOffset = 0x300; // $enable_castle
        constexpr uintptr_t kLevelBackgroundOffset   = 0x400; // $background
        constexpr uintptr_t kLevelMusicOffset        = 0x500; // $music
        constexpr uintptr_t kLevelStartTitleOffset   = 0x600; // $start_title
        constexpr uintptr_t kLevelEndTitleOffset     = 0x700; // $end_title
        constexpr uintptr_t kLevelBonusOffset        = 0x800; // $bonus, a bool
        constexpr uintptr_t kLevelBonusTimeOffset    = 0x804; // $bonus_time
        constexpr uintptr_t kLevelBonusCountOffset   = 0x808; // $bonus_count
        constexpr uintptr_t kLevelTimeOffset         = 0x80C; // $level_time
        constexpr uintptr_t kLevelNumberOffset       = 0x810; // $level_number, never 0
        constexpr uintptr_t kLevelDemoLevelOffset    = 0x814; // $demo_level, a bool
        constexpr uintptr_t kLevelUseForDemoOffset   = 0x815; // $use_for_demo, a bool
        constexpr uintptr_t kLevelMusicStartOffset   = 0x818; // $music_start
        constexpr uintptr_t kLevelMenuStatOffset     = 0x81C; // $menu_stat
        constexpr uintptr_t kLevelStartRoomXOffset   = 0x91C; // $player_start/$room_pos
        constexpr uintptr_t kLevelStartRoomYOffset   = 0x920;
        constexpr uintptr_t kLevelStartCellOffset    = 0x924; // $player_start/$pos, two floats
        constexpr size_t kLevelNameLength = 0x100;

        // A $bonus level loads neither monsters nor bonuses: Level_ParseRoomEntry reads those
        // two keys only when the flag is clear.

        constexpr uintptr_t kLevelRoomsOffset        = 0x92C; // the room entries
        constexpr uintptr_t kLevelRoomCountOffset    = 0x930;
        constexpr uintptr_t kLevelMazeOffset         = 0x934; // one row per $maze line
        constexpr uintptr_t kLevelMazeHeightOffset   = 0x938;
        constexpr uintptr_t kLevelMazeXOffset        = 0x93C; // where the player is on the maze
        constexpr uintptr_t kLevelMazeYOffset        = 0x940;
        constexpr uintptr_t kLevelCurrentRoomIdOffset = 0x944;
        constexpr uintptr_t kLevelCurrentRoomOffset  = 0x948; // the entry, not the index
        constexpr uintptr_t kLevelPreviousRoomOffset = 0x94C;

        // A maze row is { int* roomIds; int width }, and rows are ragged: $maze lines are
        // parsed one comma-separated list at a time, so each carries its own width. A cell
        // holding 0 is a hole in the maze rather than a room.
        constexpr size_t kMazeRowStride = 0x08;
        constexpr uintptr_t kMazeRowIdsOffset = 0x00;
        constexpr uintptr_t kMazeRowWidthOffset = 0x04;

        // A room entry is the level's record of a room, with the room itself embedded at +4.
        constexpr size_t kRoomEntryStride = 0x2794;
        constexpr uintptr_t kRoomEntryIdOffset = 0x0000;       // $id
        constexpr uintptr_t kRoomEntryRoomOffset = 0x0004;     // the CRoom, 0x2684 bytes
        constexpr uintptr_t kRoomEntryFloorTexOffset = 0x2688; // $floor_tex, a texture
        constexpr uintptr_t kRoomEntryWallTexOffset = 0x268C;  // $wall_tex, a texture
        constexpr uintptr_t kRoomEntryMusicOffset = 0x2690;    // $music, 0x100 bytes

        // CRoom, filled in by Room_Load @0x004130f0 out of the .room file. Offsets are from
        // the room itself, which is the entry plus kRoomEntryRoomOffset.
        constexpr uintptr_t kRoomMagicOffset = 0x0000;          // 0x9999 once loaded
        constexpr uintptr_t kRoomFileOffset = 0x0004;           // the .room, 0x100 bytes
        constexpr uintptr_t kRoomCellsOffset = 0x0104;          // the cell grid
        constexpr uintptr_t kRoomGeometryModelOffset = 0x2554;
        constexpr uintptr_t kRoomLightmapModelOffset = 0x2558;
        constexpr uintptr_t kRoomBackWallModelOffset = 0x255C;
        constexpr uintptr_t kRoomLightmapTextureOffset = 0x2560; // 256x256
        constexpr uintptr_t kRoomHasBackWallOffset = 0x2564;
        constexpr uintptr_t kRoomCollisionBoxesOffset = 0x2568;
        constexpr uintptr_t kRoomCollisionBoxCountOffset = 0x256C;
        constexpr uintptr_t kRoomLightTrianglesOffset = 0x2570;
        constexpr uintptr_t kRoomLightTriangleCountOffset = 0x2574;
        constexpr uintptr_t kRoomLoadedOffset = 0x2674;         // a bool
        constexpr uintptr_t kRoomEntitiesOffset = 0x2680;

        // The grid the whole of movement and collision is expressed in: 29 rows of 40 cells,
        // one record of 8 bytes each, the type character at +0. The rest of the record is
        // what the room's own geometry made of that cell and is not read here.
        constexpr size_t kRoomGridColumns = 40;
        constexpr size_t kRoomGridRows = 29;
        constexpr size_t kRoomCellStride = 0x08;

        // A collision box is 0x1c bytes and does not hold two corners in order: the z of the
        // near face comes first, then the near x and y, a gap, and then the far x, y and z.
        // r_room_bbox in the .so; CheckEMoveRoomCollision is what reads them.
        constexpr size_t kRoomCollisionBoxStride = 0x1C;
        constexpr uintptr_t kRoomBoxMinZOffset = 0x00;
        constexpr uintptr_t kRoomBoxMinXOffset = 0x04;
        constexpr uintptr_t kRoomBoxMinYOffset = 0x08;
        constexpr uintptr_t kRoomBoxMaxXOffset = 0x10;
        constexpr uintptr_t kRoomBoxMaxYOffset = 0x14;
        constexpr uintptr_t kRoomBoxMaxZOffset = 0x18;
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

    //   Menu_TakeClickResult  MOV EDX,[ECX+0x5c] / MOV EAX,[ESP+0x4]                 -> 7
    // Both are position independent, so seven bytes are taken and two padded with NOPs.
    constexpr uint8_t kMenuTakeClickResultPrologue[] =
        { 0x8B, 0x51, 0x5C, 0x8B, 0x44, 0x24, 0x04 };

    //   Game_FrameTick  SUB ESP,0x50 / PUSH ESI / PUSH EDI                            -> 5
    constexpr uint8_t kGameFrameTickPrologue[] =
        { 0x83, 0xEC, 0x50, 0x56, 0x57 };

    //   Render_Begin    PUSH ESI / MOV ESI,ECX / CMP dword [ESI+0x0C],0               -> 7
    constexpr uint8_t kRenderBeginPrologue[] =
        { 0x56, 0x8B, 0xF1, 0x83, 0x7E, 0x0C, 0x00 };

    //   Render_Present  PUSH ESI / MOV ESI,ECX / CMP byte [ESI+0x20],0                -> 7
    // All three are position independent, so seven bytes are taken and two padded with NOPs;
    // the CMP is followed by a near JNZ outside the stolen range.
    constexpr uint8_t kRenderPresentPrologue[] =
        { 0x56, 0x8B, 0xF1, 0x80, 0x7E, 0x20, 0x00 };

    //   MenuItem_Render  SUB ESP,0x10 / PUSH ESI / MOV ESI,ECX                         -> 6
    constexpr uint8_t kMenuItemRenderPrologue[] =
        { 0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1 };

    //   Menu_DrawDecorations  SUB ESP,0x0C / PUSH ESI / MOV ESI,ECX                     -> 6
    constexpr uint8_t kMenuDrawDecorationsPrologue[] =
        { 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1 };

    //   Level_ActivateRoom  PUSH ESI / MOV ESI,ECX / MOV EAX,[ESI+0x934]                -> 9
    // The load reaches the maze through ESI, so the range moves with the routine; the JNZ
    // that follows it is the first instruction outside, and nothing branches into the range.
    constexpr uint8_t kLevelActivateRoomPrologue[] =
        { 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x34, 0x09, 0x00, 0x00 };

    //   Level_Load  PUSH -1 / PUSH 0x004c55bb                                           -> 7
    //   Level_Free  PUSH ESI / PUSH EDI / PUSH 0x004c8b7c                                -> 7
    // Both push a 32-bit immediate that must not be cut in half. As with App_Shutdown the
    // immediate is an address in this build, so a build where it differs is refused rather
    // than patched: the executable carries no relocations, and an address that moved means
    // the code was rebuilt.
    constexpr uint8_t kLevelLoadPrologue[] =
        { 0x6A, 0xFF, 0x68, 0xBB, 0x55, 0x4C, 0x00 };
    constexpr uint8_t kLevelFreePrologue[] =
        { 0x56, 0x57, 0x68, 0x7C, 0x8B, 0x4C, 0x00 };

    //   Loading_SetProgress  PUSH ESI / MOV ESI,ECX / CMP [ESI],7                        -> 6
    // The compare reaches the state through ESI, so the range moves with the routine, and the
    // JNZ after it is the first instruction outside.
    constexpr uint8_t kLoadingSetProgressPrologue[] =
        { 0x56, 0x8B, 0xF1, 0x83, 0x3E, 0x07 };
}
