#pragma once

#include "src/Game/GameAddresses.h"
#include "src/Game/GameSymbols.h"

#include <stdint.h>

namespace Forge
{
    struct GameSymbolDefinition
    {
        const char* name;
        uintptr_t hint;         // address in the build this was reversed from
        const char* pattern;
    };

    // Wildcards cover the operand of every call that leaves the pattern and every absolute
    // address of a global. Everything else, including short jumps, is literal: a short jump
    // is relative to the instruction after it and survives the routine being moved, and the
    // shape of the control flow is what makes a signature specific.
    //
    // This table lives in a header so the signature test can check the patterns AzForge
    // actually ships rather than a copy of them.
    inline constexpr GameSymbolDefinition kGameSymbolTable[static_cast<size_t>(GameSymbol::Count)] =
    {
        {
            "Package_OpenFile", GameAddress::kPackageOpenFile,
            // PUSH ESI / MOV ESI,[ESP+8] / inline strlen / CALL Package_ReadFromDisk
            "56 8B 74 24 08 57 8B F9 85 F6 74 29 8B C6 8D 50 01 8A 08 40 84 C9 75 F9 "
            "2B C2 74 19 56 8B CF E8 ?? ?? ?? ??"
        },
        {
            "Mem_AllocTracked", GameAddress::kMemAllocTracked,
            // LEA EDI,[EBX+0x118] is the tracking header size, which is what separates this
            // from any other malloc wrapper in the executable.
            "53 8B 5C 24 08 56 57 8D BB 18 01 00 00 57 E8 ?? ?? ?? ?? 8B F0 83 C4 04"
        },
        {
            "Mem_FreeTracked", GameAddress::kMemFreeTracked,
            // ADD EAX,-0x118 walks back to the header and MOV EDX,0xAFAF is the magic it
            // compares against.
            "8B 44 24 04 85 C0 74 75 8B 0D ?? ?? ?? ?? 05 E8 FE FF FF 85 C9 74 57 "
            "BA AF AF 00 00 66 39 10"
        },
        {
            "NameBuffer_Assign", GameAddress::kNameBufferAssign,
            // PUSH 0xFF / PUSH 0x100 around the copy into the fixed 0x100 name buffer.
            "8B 44 24 04 56 8B F1 85 C0 74 21 68 FF 00 00 00 50 68 00 01 00 00 56 "
            "E8 ?? ?? ?? ??"
        },
        {
            "App_Shutdown", GameAddress::kAppShutdown,
            // The banner write, then the two fopen probes for play.url and the promo
            // launcher, with XOR BL,BL between them.
            "53 68 ?? ?? ?? ?? 6A 01 E8 ?? ?? ?? ?? 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? "
            "32 DB E8 ?? ?? ?? ?? 83 C4 10 85 C0 74 15"
        },
        {
            "Launcher_OnInitDialog", GameAddress::kLauncherOnInitDialog,
            // Registers the com_raster cvar with flags 3, then fills the language combo.
            "56 57 6A 03 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? 8B F9 E8 ?? ?? ?? ?? 83 C4 0C "
            "B9 ?? ?? ?? ?? A3 ?? ?? ?? ??"
        },
        {
            "Launcher_OnCommand", GameAddress::kLauncherOnCommand,
            // Structured exception frame, then the security cookie the switch sits behind.
            "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 83 EC 08 56 57 A1 ?? ?? ?? ?? "
            "33 C4 50 8D 44 24 14 64"
        },
        {
            "Dialog_RunModal", GameAddress::kDialogRunModal,
            // Rejects a null template, then pushes Dialog_Proc into DialogBoxParamA.
            "8B 44 24 0C 85 C0 75 03 C2 0C 00 8B 54 24 04 51 8B 4C 24 0C 68 ?? ?? ?? ?? "
            "51 50 52 FF 15 ?? ?? ?? ??"
        },
        {
            "Launcher_HideOptionalButtons", GameAddress::kLauncherHideOptionalButtons,
            // Three Dialog_ShowItem calls with the ids 0x3EC, 0x3EA and 0x3EE and a zero
            // visibility, which is the whole routine.
            "56 6A 00 68 EC 03 00 00 8B F1 E8 ?? ?? ?? ?? 6A 00 68 EA 03 00 00 8B CE "
            "E8 ?? ?? ?? ?? 6A 00 68 EE 03 00 00"
        },
        {
            "Menu_Register", GameAddress::kMenuRegister,
            // Parses through the menu in EDI, then checks the force flag against the result.
            "8B 44 24 08 53 57 8B 7C 24 0C 8B D9 50 8B CF E8 ?? ?? ?? ?? 80 7C 24 18 00 "
            "75 09 84 C0 75 05 5F 5B C2 10 00"
        },
        {
            "Menu_LoadFromScript", GameAddress::kMenuLoadFromScript,
            // Inline strlen of the menu id, then the lookup in the registry.
            "56 57 8B 7C 24 0C 85 FF 74 4E 8B C7 8D 70 01 90 8A 10 40 84 D2 75 F9 "
            "2B C6 74 3D 57 E8 ?? ?? ?? ??"
        },
        {
            "Menu_ParseScript", GameAddress::kMenuParseScript,
            // Structured exception frame with a 0xD0 byte frame.
            "55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 81 EC D0 00 00 00 53 56 57 "
            "A1 ?? ?? ?? ?? 33 C5 50 8D 45 F4 64 A3 ?? ?? ?? ??"
        },
        {
            "MenuItem_Parse", GameAddress::kMenuItemParse,
            // Delegates to MenuButton_ParseCommon and bails out when it fails, then reaches
            // for the element's own text field at +0x758.
            "56 57 8B 7C 24 0C 57 8B F1 E8 ?? ?? ?? ?? 84 C0 75 07 5F 32 C0 5E C2 08 00 "
            "8D 86 58 07 00 00 50"
        },
        {
            "Menu_FindItemById", GameAddress::kMenuFindItemById,
            // Walks the groups at +0x4C comparing the id each element keeps at +0x715.
            "83 EC 08 83 7C 24 0C 00 75 08 33 C0 83 C4 08 C2 04 00 53 55 8B 69 4C 8B 49 50 "
            "56 57 C7 44 24 10"
        },
        {
            "Parser_CreateFromBuffer", GameAddress::kParserCreateFromBuffer,
            // Rejects a null or empty buffer through an inline strlen, then allocates the
            // 0x55C byte parser. The size is what tells this apart from every other wrapper.
            "57 8B 7C 24 08 85 FF 74 5A 8B C7 8D 50 01 8B FF 8A 08 40 84 C9 75 F9 2B C2 "
            "74 48 56 68 8E 00 00 00 68 ?? ?? ?? ?? 68 5C 05 00 00"
        },
        {
            "Parser_Destroy", GameAddress::kParserDestroy,
            // Aligns the stack, takes the security cookie, then walks the 0x13 dword block
            // the parser keeps at +0x4C8.
            "55 8B EC 83 E4 F8 83 EC 5C A1 ?? ?? ?? ?? 33 C4 89 44 24 58 53 8B 5D 08 56 57 "
            "8D B3 C8 04 00 00 B9 13 00 00 00"
        },
        {
            "Parser_EnterBlock", GameAddress::kParserEnterBlock,
            // Cookie, then the null check on the parser that returns false in AL.
            "83 EC 48 A1 ?? ?? ?? ?? 33 C4 89 44 24 44 8B 4C 24 50 56 8B 74 24 50 89 4C 24 04 "
            "85 F6 75 12 32 C0 5E"
        },
        {
            "Parser_NextBlock", GameAddress::kParserNextBlock,
            // The two long jumps that leave when the parser or its buffer is gone, and the
            // depth at +0x4C8 it checks after them.
            "83 EC 48 A1 ?? ?? ?? ?? 33 C4 89 44 24 44 56 8B 74 24 50 85 F6 0F 84 22 01 00 00 "
            "83 3E 00 0F 84 19 01 00 00 83 BE C8 04 00 00 00"
        },
        {
            "Parser_LeaveBlock", GameAddress::kParserLeaveBlock,
            // Reads the nesting depth at +0x4C4 and indexes the 0x4C byte frames behind it.
            "83 EC 50 A1 ?? ?? ?? ?? 33 C4 89 44 24 4C 53 8B 5C 24 58 85 DB 74 54 "
            "8B 93 C4 04 00 00 8D 43 04 85 D2 7C 3D 56 8B F2 6B F6 4C"
        },
        {
            "Menu_AddElement", GameAddress::kMenuAddElement,
            // Looks the element's $group up at +0x194 and creates it when the lookup fails,
            // which is the pair of calls no other routine makes.
            "83 EC 08 53 8B 5C 24 10 55 56 57 8B E9 8D BB 94 01 00 00 57 89 6C 24 14 "
            "E8 ?? ?? ?? ?? 8B F0 85 F6 75 0A 57 8B CD E8 ?? ?? ?? ??"
        },

        // The constructors. Every one of them writes its class's vftable, and that address
        // is kept literal here rather than wildcarded like other globals: it is what tells
        // one constructor from the next, several of them being otherwise identical, and this
        // executable carries no relocations, so an address that moved means a rebuild.
        {
            "MenuItem_Create", GameAddress::kMenuItemCreate,
            "56 8B F1 E8 ?? ?? ?? ?? D9 05 ?? ?? ?? ?? D9 9E B8 05 00 00 32 C0 D9 EE "
            "88 86 58 07 00 00 D9 96 BC 05 00 00 88 86 BC 07 00 00"
        },
        {
            "MenuLogo_Create", GameAddress::kMenuLogoCreate,
            // The whole routine: chain to the common constructor, store the vftable, answer
            // the block.
            "56 8B F1 E8 ?? ?? ?? ?? C7 06 D4 34 4D 00 8B C6 5E C3"
        },
        {
            "TextButton_Create", GameAddress::kTextButtonCreate,
            // Opens an exception frame. The scope table it registers is the only thing that
            // separates this from MenuList_Create, so that address stays literal too.
            "6A FF 68 08 64 4C 00 64 A1 00 00 00 00 50 51 56 A1 ?? ?? ?? ?? 33 C4 50 "
            "8D 44 24 0C 64 A3 00 00 00 00 8B F1"
        },
        {
            "ImageButton_Create", GameAddress::kImageButtonCreate,
            // Stores its vftable, then fills three floats at +0x764..+0x770 with one.
            "56 8B F1 E8 ?? ?? ?? ?? D9 E8 C7 06 DC 33 4D 00 D9 96 70 07 00 00 "
            "D9 96 6C 07 00 00 33 C0 D9 96 68 07 00 00"
        },
        {
            "HotkeyButton_Create", GameAddress::kHotkeyButtonCreate,
            // Chains to TextButton_Create, then works on the buffer at +0x9b8 that holds the
            // key name.
            "53 56 57 8B F1 E8 ?? ?? ?? ?? 68 FF 00 00 00 33 DB C7 06 34 33 4D 00 "
            "88 9E B8 09 00 00"
        },
        {
            "CastleButton_Create", GameAddress::kCastleButtonCreate,
            // Chains to ImageButton_Create, then fills the door scales at +0xae0..+0xae8.
            "56 8B F1 E8 ?? ?? ?? ?? D9 E8 C7 06 BC 32 4D 00 D9 96 E8 0A 00 00 "
            "D9 96 E4 0A 00 00 8D 8E 14 13 00 00"
        },
        {
            "MenuList_Create", GameAddress::kMenuListCreate,
            // The same exception frame TextButton_Create opens, with its own scope table.
            "6A FF 68 84 63 4C 00 64 A1 00 00 00 00 50 51 56 A1 ?? ?? ?? ?? 33 C4 50 "
            "8D 44 24 0C 64 A3 00 00 00 00 8B F1"
        },
        {
            "MenuSlider_Create", GameAddress::kMenuSliderCreate,
            // Zeroes +0x780 and raises the two flags at +0x5fc and +0x5fe before the vftable.
            "56 8B F1 E8 ?? ?? ?? ?? D9 EE B0 01 D9 9E 80 07 00 00 88 86 FC 05 00 00 "
            "88 86 FE 05 00 00 C7 06 DC 30 4D 00"
        },
        {
            "MenuLogo_Parse", GameAddress::kMenuLogoParse,
            // Delegates to MenuButton_ParseCommon, cleans up eight bytes on the way out, then
            // reaches for the $sp1 key.
            "56 57 8B 7C 24 0C 57 8B F1 E8 ?? ?? ?? ?? 84 C0 75 05 5F 5E C2 08 00 "
            "55 6A 00 68 1C 35 4D 00 57"
        },
        {
            "MenuList_Parse", GameAddress::kMenuListParse,
            // Security cookie over a 0x48 byte frame, then the parser is picked up from the
            // stack and handed straight to MenuButton_ParseCommon.
            "83 EC 48 A1 ?? ?? ?? ?? 33 C4 89 44 24 44 56 57 8B 7C 24 54 57 8B F1 "
            "E8 ?? ?? ?? ?? 84 C0"
        },

        {
            "MenuTable_Create", GameAddress::kMenuTableCreate,
            // Writes its vftable and zeroes the six fields around it. The vftable stays
            // literal, as every constructor's does.
            "8B C1 33 C9 C7 00 0C 2F 4D 00 89 88 88 00 00 00 89 48 7C 89 88 98 00 00 00 "
            "89 88 94 00 00 00 89 88 84 00 00 00"
        },
        {
            "MenuTable_Parse", GameAddress::kMenuTableParse,
            // 0x60 frame with a cookie, then the $id key it opens before anything else.
            "83 EC 60 A1 ?? ?? ?? ?? 33 C4 89 44 24 5C 53 55 8B 6C 24 6C 68 ?? ?? ?? ?? "
            "55 8B D9 E8 ?? ?? ?? ??"
        },
        {
            "Menu_AddTable", GameAddress::kMenuAddTable,
            // The whole routine: append a pointer to the array at this+0x54 through
            // Collection_AppendPointer, then store the table into the slot it returns.
            "8D 41 58 83 C1 54 50 51 8D 4C 24 0C E8 ?? ?? ?? ?? 85 C0 74 06 8B 4C 24 04 "
            "89 08 B0 01 C2 04 00"
        },

        // The decoration parsers. Each opens an exception frame, and the scope table it
        // registers is kept literal for the same reason the constructors keep their vftable:
        // their prologues are otherwise the same routine three times over.
        {
            "Menu_ParseImages", GameAddress::kMenuParseImages,
            "55 8B EC 6A FF 68 20 60 4C 00 64 A1 00 00 00 00 50 83 EC 44 53 56 57 "
            "A1 ?? ?? ?? ?? 33 C5 50 8D 45 F4 64 A3 00 00 00 00"
        },
        {
            "Menu_ParseAnimatedImages", GameAddress::kMenuParseAnimatedImages,
            "55 8B EC 6A FF 68 40 60 4C 00 64 A1 00 00 00 00 50 83 EC 3C 53 56 57 "
            "A1 ?? ?? ?? ?? 33 C5 50 8D 45 F4 64 A3 00 00 00 00"
        },
        {
            "Menu_ParseStaticText", GameAddress::kMenuParseStaticText,
            // The 0x474 byte frame is the 0x470 byte record it builds each $static into.
            "55 8D AC 24 8C FB FF FF 81 EC 74 04 00 00 6A FF 68 60 60 4C 00 "
            "64 A1 00 00 00 00 50 83 EC 20 A1 ?? ?? ?? ?? 33 C5"
        },
        {
            "Mem_ReallocTracked", GameAddress::kMemReallocTracked,
            // LEA EBX,[EDI+0x118] adds the tracking header to the requested size, the same
            // 0x118 that separates Mem_AllocTracked from any other wrapper.
            "53 55 8B 6C 24 0C 56 57 8B 7C 24 18 8D 9F 18 01 00 00 53 E8 ?? ?? ?? ?? "
            "8B F0 83 C4 04 85 F6 75 1E"
        },
        {
            "Menu_FindByMenuId", GameAddress::kMenuFindByMenuId,
            // Inline strlen of the id, then the walk over the registry count at manager+0x38.
            "55 8B 6C 24 08 56 85 ED 0F 84 8A 00 00 00 8B C5 8D 70 01 8A 10 40 84 D2 "
            "75 F9 2B C6 74 7A 53 8B 59 38"
        },
        {
            "CBaseMenu_Construct", GameAddress::kCBaseMenuConstruct,
            // Stores CBaseMenu::vftable and clears the five click-result fields at +0x5c.
            // The vftable stays literal, as every constructor's does.
            "53 33 DB 33 C0 56 8B F1 C7 06 AC 1A 4D 00 88 9E 08 01 00 00 89 46 5C "
            "89 46 60 89 46 64 89 46 68 89 46 6C"
        },
        {
            "Menu_EnterMenu", GameAddress::kMenuEnterMenu,
            // Finds the target, checks its disabled flag at +0x209, then reads the current
            // menu at manager+0xc. The 0x4e/0x45 branch distances tell it from Menu_ShowBox,
            // whose prologue is otherwise identical.
            "8B 44 24 04 56 57 50 8B F1 E8 ?? ?? ?? ?? 8B F8 85 FF 74 4E 80 BF 09 02 00 00 00 "
            "75 45 8B 4E 0C"
        },
        {
            "Menu_ShowBox", GameAddress::kMenuShowBox,
            // The same shape as Menu_EnterMenu with 0x34/0x2b branch distances: it sets the
            // overlay at manager+0x10 rather than switching the current menu.
            "8B 44 24 04 56 57 50 8B F1 E8 ?? ?? ?? ?? 8B F8 85 FF 74 34 80 BF 09 02 00 00 00 "
            "75 2B 8B 4E 0C"
        },
        {
            "Menu_GoBack", GameAddress::kMenuGoBack,
            // Reads the overlay at manager+0x10; when it is clear it pops the stack, and the
            // MOV dword [ESI+0x10],0 that clears the overlay is the tail unique to it.
            "56 8B F1 8B 4E 10 85 C9 74 28 6A 00 E8 ?? ?? ?? ?? 8B 4E 10 8B 01 8B 50 54 FF D2 "
            "8B 4E 0C C7 46 10 00 00 00 00"
        },
        {
            "Menu_CloseAll", GameAddress::kMenuCloseAll,
            // Also reads manager+0x10 first, but pushes EDI and closes both overlay and
            // current before unwinding, which the early PUSH EDI and second manager+0xc read
            // set apart from Menu_GoBack.
            "56 8B F1 8B 4E 10 57 85 C9 74 11 6A 00 E8 ?? ?? ?? ?? 8B 4E 10 8B 01 8B 50 54 "
            "FF D2 8B 4E 0C 85 C9 74 11"
        },
        {
            "Menu_TakeClickResult", GameAddress::kMenuTakeClickResult,
            // Copies the five click fields from menu+0x5c into the destination and clears
            // them - the whole routine, and unmistakable for it.
            "8B 51 5C 8B 44 24 04 89 10 8B 51 60 89 50 04 8B 51 64 89 50 08 8B 51 68 "
            "89 50 0C 8B 51 6C 89 50 10 33 C0 89 41 5C"
        },
        {
            "Game_FrameTick", GameAddress::kGameFrameTick,
            // The frame gate opens with a 0x50 frame, then GameState_Current and the
            // FUN_00424fd0 timer read into the local pair - the shape no other routine has.
            "83 EC 50 56 57 E8 ?? ?? ?? ?? 8B F0 8D 44 24 0C 50 68 ?? ?? ?? ?? "
            "C7 44 24 14 00 00 00 00"
        },
        {
            "GameConsoleWrite", GameAddress::kGameConsoleWrite,
            // A 0x408 frame for the vsprintf buffer, the cookie, then the format argument
            // fetched from the far end of the frame - the console print and nothing else.
            "81 EC 08 04 00 00 A1 ?? ?? ?? ?? 33 C4 89 84 24 04 04 00 00 56 "
            "8B B4 24 14 04 00 00 8B C6"
        },
        {
            "Cmd_Register", GameAddress::kCmdRegister,
            // Rejects a null name or handler, then the inline strlen before it hashes the
            // name into the command table at config+0x08.
            "57 8B 7C 24 08 85 FF 74 66 55 8B 6C 24 10 85 ED 74 5C 8B C7 56 "
            "8D 70 01 8A 10 40 84 D2 75 F9 2B C6"
        },
        {
            "Console_Execute", GameAddress::kConsoleExecute,
            // The exception frame with the console scope table, then the 0xc byte frame it
            // tokenises the line in.
            "6A FF 68 B8 58 4C 00 64 A1 00 00 00 00 50 83 EC 0C 53 56 57"
        },
        {
            "Render_Begin", GameAddress::kRenderBegin,
            // The device-check at +0x0c, the early-out that returns 0 in AL, the resize test at
            // +0x58, then TestCooperativeLevel through the device vtable. The Present pointer call
            // is an import, so it is wildcarded.
            "56 8B F1 83 7E 0C 00 75 04 32 C0 5E C3 80 7E 58 00 74 22 8B 46 04 6A 01 6A 00 50 "
            "FF 15 ?? ?? ?? ??"
        },
        {
            "Render_Present", GameAddress::kRenderPresent,
            // The device-lost gate at +0x20, then the load of the device at +0x0c and the
            // call through its EndScene slot at vtable +0x8c - the shape no other routine has.
            // The "EndScene" string push is an absolute, so it is wildcarded.
            "56 8B F1 80 7E 20 00 0F 85 85 00 00 00 8B 46 0C 8B 08 8B 91 8C 00 00 00 "
            "50 FF D2 85 C0 7D 10 50 68 ?? ?? ?? ?? E8 ?? ?? ?? ??"
        },
        {
            "MenuItem_Render", GameAddress::kMenuItemRender,
            // The $plank_width test at +0x798, then the ItemAnim state read at +0x5d1/+0x5cc/
            // +0x5b5 and the LEA of the drawing centre at +0x5c0 - all structure-relative, so
            // the whole prologue is literal.
            "83 EC 10 56 8B F1 8B 86 98 07 00 00 85 C0 74 35 0F B6 8E D1 05 00 00 "
            "D9 86 CC 05 00 00 80 BE B5 05 00 00 00 51 0F 94 C2 0F B6 CA 51 51 "
            "8D 96 C0 05 00 00 D9 1C 24 50 52"
        },
        {
            "Menu_DrawDecorations", GameAddress::kMenuDrawDecorations,
            // Reads the $image collection at menu+0x74 and the $images_anim at menu+0x78, draws
            // each, then tests the background-tile flag at menu+0xF4 - the decoration draw and
            // nothing else. The two draw calls are wildcarded.
            "83 EC 0C 56 8B F1 8B 46 74 6A 00 50 E8 ?? ?? ?? ?? 8B 4E 78 51 "
            "E8 ?? ?? ?? ?? 83 C4 0C 80 BE F4 00 00 00 00 74 68"
        },
        {
            "Menu_FindFont", GameAddress::kMenuFindFont,
            // Defaults a null name to the "default" string, then walks the font registry count
            // and items (both absolute globals, wildcarded) comparing names. The "default" push
            // is an absolute too.
            "53 55 8B 6C 24 0C 56 57 85 ED 75 05 BD ?? ?? ?? ?? 8B 1D ?? ?? ?? ?? "
            "33 F6 85 DB 7E 3B 8B 3D ?? ?? ?? ??"
        },
        {
            "Level_Load", GameAddress::kLevelLoad,
            // The structured exception frame, then MOV EAX,0x27d0 - the frame it reserves for
            // the one room entry it parses into, which no other routine asks for. The pushed
            // handler, the alloca probe and the cookie are absolutes, so they are wildcarded.
            "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 B8 D0 27 00 00 E8 ?? ?? ?? ?? "
            "A1 ?? ?? ?? ?? 33 C4"
        },
        {
            "Level_Free", GameAddress::kLevelFree,
            // The banner write, then the room loop: the count at +0x930 against a zeroed EDI
            // and the rooms at +0x92c, both structure-relative and both unique to the level.
            "56 57 68 ?? ?? ?? ?? 6A 01 8B F1 E8 ?? ?? ?? ?? 33 FF 83 C4 08 "
            "39 BE 30 09 00 00 7E 23 53 33 DB 8B 86 2C 09 00 00 03 C3 50 8B CE"
        },
        {
            "Level_ActivateRoom", GameAddress::kLevelActivateRoom,
            // The maze test at +0x934 and its warning, then the maze cursor at +0x93c that the
            // first argument is added to - the step through the maze and nothing else. Only the
            // warning string and the console call are absolutes.
            "56 8B F1 8B 86 34 09 00 00 85 C0 75 15 68 ?? ?? ?? ?? 6A 01 "
            "E8 ?? ?? ?? ?? 83 C4 08 33 C0 5E C2 08 00 53 8B 9E 3C 09 00 00 03 5C 24 0C"
        },
        {
            "Loading_SetProgress", GameAddress::kLoadingSetProgress,
            // The loading-state test at +0x00 and the current state at +0x0c, then the call
            // through its vtable +0x0c with the percentage - the shape no other routine has.
            // The two render calls go through absolute pointers, so they are wildcarded.
            "56 8B F1 83 3E 07 75 31 83 7E 0C 00 74 2B 8B 4E 0C 8B 01 8B 54 24 08 "
            "8B 40 0C 6A 00 52 FF D0 FF 15 ?? ?? ?? ?? 84 C0 74 10"
        },
        {
            "Menu_RegisterAll", GameAddress::kMenuRegisterAll,
            // The first two of its twenty-eight Menu_Register calls: the embedded menus at
            // manager+0x40 and manager+0x24c, each with its script. Only the script strings
            // and the calls are absolutes.
            "56 6A 00 6A 00 8B F1 68 ?? ?? ?? ?? 8D 46 40 50 E8 ?? ?? ?? ?? 6A 00 6A 00 "
            "68 ?? ?? ?? ?? 8D 8E 4C 02 00 00 51 8B CE E8 ?? ?? ?? ??"
        }
    };
}
