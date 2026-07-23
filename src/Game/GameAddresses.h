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
        constexpr uintptr_t kMenuParseScript      = 0x004527A0; // __thiscall (menu, scriptFile)
        constexpr uintptr_t kMenuItemParse        = 0x0045F550; // __thiscall (element, parser)
        constexpr uintptr_t kMenuFindItemById     = 0x00451860; // __thiscall (menu, id)

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
        // What MenuItem_Render @0x0045f620 actually draws at: the middle of the rectangle,
        // worked out once during parsing. Writing the rectangle without bringing this along
        // changes nothing on screen.
        constexpr uintptr_t kMenuElementCentreOffset = 0x5C0;

        constexpr uintptr_t kMenuElementVisibleOffset = 0x714;
        constexpr uintptr_t kMenuElementIdOffset = 0x715;       // $id

        // Only present on elements MenuItem_Parse @0x0045f550 built, and the field
        // MenuItem_Render @0x0045f620 actually draws.
        //
        // The capacity is not a guess: the next field MenuItem_Parse writes is $plank_width
        // at +0x798, so the text occupies everything up to it.
        constexpr uintptr_t kMenuItemTextOffset = 0x758;
        constexpr size_t kMenuItemTextCapacity = 0x798 - 0x758;

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
