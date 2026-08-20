#pragma once

#include <stdint.h>

namespace Forge
{
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

        constexpr uintptr_t kGameFrameTick = 0x00425000; // __cdecl (void)
        constexpr uintptr_t kFrameDelta = 0x0067A5F8; // float

        // Render_Present: __thiscall (manager), vtable +0x44; device @ +0x0C, lost @ +0x20, viewport @ +0xA8

        // g_pfnDrawStyledText __cdecl (font, text, 0, centre[2], scale[2], colour[4], align, 0)
        constexpr uintptr_t kDrawStyledTextPtr = 0x00691308;

        // g_pfnDrawSprite __cdecl (sprite, x, y, w, h, 0, colour[4], a7, a8)
        constexpr uintptr_t kDrawSpritePtr = 0x006912F0;

        // g_pfnDrawStaticText __cdecl (a1, pos, a3, a4, a5, colour[4], a7)
        constexpr uintptr_t kDrawStaticTextPtr = 0x00691310;

        // Console: Config** @ kConfig, cmds @ +0x08; Write __cdecl; Register __thiscall; handler __cdecl(argc,argv)
        constexpr uintptr_t kConfig = 0x0067A0D8;
        constexpr uintptr_t kConfigCommandsOffset = 0x08;

        // Azangara.cfg written by Config_Write: walks config+0x04 down +0x18 links and prints
        // `name "value"`. A mod-registered cvar shows for free if its flags allow it.
        constexpr uintptr_t kConfigCvarsOffset = 0x04;
        constexpr uintptr_t kConfigRegisterCvar = 0x00423590; // __thiscall (config, name, default, flags)
        constexpr uintptr_t kCvarFind = 0x004231C0;           // __thiscall (config, name) -> cvar
        constexpr uintptr_t kCvarSetString = 0x00424890;      // __thiscall (cvar, text)
        constexpr uintptr_t kCvarSetFloat = 0x00424930;       // __thiscall (cvar, float)
        constexpr uintptr_t kConfigWrite = 0x00423060;        // __thiscall (config, path or null, force)

        // nsVar (0x1C bytes). +0x10 = default, +0x14 = live value. Config_Write prints +0x14.
        constexpr size_t kCvarSize = 0x1C;
        constexpr uintptr_t kCvarNameOffset = 0x04;
        constexpr uintptr_t kCvarFlagsOffset = 0x08;
        constexpr uintptr_t kCvarFloatOffset = 0x0C;
        constexpr uintptr_t kCvarDefaultOffset = 0x10;
        constexpr uintptr_t kCvarValueTextOffset = 0x14;
        constexpr uintptr_t kCvarNextOffset = 0x18;

        // Config_Write: bit 1 says write me, bit 4 says never
        constexpr uint32_t kCvarArchiveFlag = 0x01;
        constexpr uint32_t kCvarNeverWriteFlag = 0x04;
        constexpr uintptr_t kGameConsoleWrite = 0x004220C0;
        constexpr uintptr_t kCmdRegister = 0x00423990;
        constexpr uintptr_t kConsoleExecute = 0x004237B0;

        // Cmd node: +0x00 cmd, +0x04 name, +0x08 prev, +0x0C next; command: +0x00 name, +0x04 handler
        constexpr uintptr_t kCmdFind = 0x004232F0; // __thiscall (table, name)
        constexpr size_t kCmdBuckets = 256;
        constexpr uintptr_t kCmdNodeCommandOffset = 0x00;
        constexpr uintptr_t kCmdNodeNameOffset = 0x04;
        constexpr uintptr_t kCmdNodePrevOffset = 0x08;
        constexpr uintptr_t kCmdNodeNextOffset = 0x0C;
        constexpr uintptr_t kCmdCommandNameOffset = 0x00;

        // Parser: buffer writable and must outlive parser; keyPrefix 0x24 = '$'
        constexpr uintptr_t kParserCreateFromBuffer = 0x00420F70; // __cdecl (text, keyPrefix)
        constexpr uintptr_t kParserDestroy = 0x00421AB0; // __cdecl (parser)
        constexpr uintptr_t kParserEnterBlock = 0x00421B40; // __cdecl (parser, name)
        constexpr uintptr_t kParserNextBlock = 0x00421350; // __cdecl (parser)
        constexpr uintptr_t kParserLeaveBlock = 0x00421C60; // __cdecl (parser)

        constexpr uintptr_t kMemReallocTracked = 0x00421EC0; // (block, size, file, line)
        constexpr uint8_t kParserKeyPrefix = 0x24; // '$'
        constexpr uintptr_t kElementDestroyVtableSlot = 0x34;

        constexpr uintptr_t kTrackedHeaderSize = 0x118;
        constexpr uintptr_t kTrackedMagicOffset = 0x000; // u16 0xAFAF
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

        // One record per key and eight joystick records.
        constexpr uintptr_t kInputKeyRecordsOffset = 0x004;
        constexpr size_t kInputKeyRecordStride = 0x38;
        constexpr size_t kInputKeyRecordCount = 0x100;
        constexpr uintptr_t kInputJoyRecordsOffset = 0x3908;
        constexpr size_t kInputJoyRecordCount = 8; // joy_left/right/down/up, joy1..joy4
        constexpr uintptr_t kInputJoyStateOffset = 0x3AC8;
        constexpr uintptr_t kInputJoyCapturedOffset = 0x3AD0; // joySetCapture

        constexpr uintptr_t kKeyNameOffset = 0x00;
        constexpr uintptr_t kKeyBindKindOffset = 0x24; // 0 none, 1 cvar, 2 command
        constexpr uintptr_t kKeyBindTargetOffset = 0x28;
        constexpr uintptr_t kKeyCvarHeldOffset = 0x2C;
        constexpr uintptr_t kKeyParamCountOffset = 0x2C;
        constexpr uintptr_t kKeyParamsOffset = 0x30;
        constexpr uintptr_t kKeyCommandHeldOffset = 0x34;

        constexpr int kKeyBindNone = 0;
        constexpr int kKeyBindCvar = 1;
        constexpr int kKeyBindCommand = 2;

        constexpr uintptr_t kInputFindKey = 0x00426DC0; // __thiscall (input, name) -> record
        constexpr uintptr_t kInputUnbindKey = 0x00426D00; // __stdcall (record), frees parameters
        constexpr uintptr_t kInputBindCvar = 0x00427580; // __stdcall (record, cvar, held), RET 0xC
        constexpr uintptr_t kInputBindCommand = 0x004274C0; // __stdcall (record, command, argc, argv, held), RET 0x14
        // Input_Update polls GetAsyncKeyState each frame; this slot lets a mod inject key presses.
        constexpr uintptr_t kGetAsyncKeyStateSlot = 0x004C7228;
        constexpr uintptr_t kInputUpdate = 0x004271A0; // __fastcall (input)

        constexpr uintptr_t kGameIsPaused = 0x00426750; // __cdecl () -> bool
        constexpr uintptr_t kGameSetPaused = 0x00426760; // __cdecl (bool)

        // Both RET 8 → __stdcall
        constexpr uintptr_t kInputGetCursor = 0x00426940; // (float* x, float* y)
        constexpr uintptr_t kInputSetCursor = 0x004268B0; // (float x, float y)

        constexpr uintptr_t kGameWindow = 0x0067CB9C; // HWND*

        constexpr uintptr_t kParserFindKey = 0x00421560;
        constexpr uintptr_t kParserReadFloat4 = 0x00421740;
    }
}
