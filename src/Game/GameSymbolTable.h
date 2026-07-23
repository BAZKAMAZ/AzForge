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
        }
    };
}
