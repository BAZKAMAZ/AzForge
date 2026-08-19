#pragma once

#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
        constexpr uintptr_t kDemoParse = 0x00438370; // __thiscall (script, path) -> bool
        constexpr uintptr_t kDemoParseMoveCmd = 0x004381E0; // __cdecl (parser, out[8])
        constexpr uintptr_t kDemoParseTimeList = 0x00437F50; // __thiscall (list, parser) -> bool
        constexpr uintptr_t kDemoParseCmdList = 0x00438080; // __thiscall (list, parser, name) -> bool
        constexpr uintptr_t kDemoLoadList = 0x00438630; // __thiscall (player, parser)

        constexpr uintptr_t kDemoActive = 0x00437930; // __cdecl () -> CDemoPlayer*
        constexpr uintptr_t kDemoIsPlaying = 0x004377A0; // __thiscall (player) -> bool
        constexpr uintptr_t kDemoPlayerPlace = 0x00437870; // __thiscall (player) -> int[4] or null
        constexpr uintptr_t kDemoBreak = 0x004377C0; // __thiscall (player)
        constexpr uintptr_t kDemoNextScript = 0x00437810; // __thiscall (player)

        // Demo ticks.
        constexpr uintptr_t kDemoTickTime = 0x00437C10; // __thiscall (player)
        constexpr uintptr_t kDemoTickCamera = 0x00437F30; // __cdecl ()
        constexpr uintptr_t kDemoApplyCameraStart = 0x00437730; // __thiscall (script)
        constexpr uintptr_t kDemoTickCameraPos = 0x00437D30; // __thiscall (player)
        constexpr uintptr_t kDemoTickCameraView = 0x00437E30; // __thiscall (player)

        constexpr uintptr_t kDemoGetMove = 0x00437BE0; // __thiscall (player, pos[3], out[3], delta)
        constexpr uintptr_t kDemoGetMoveScript = 0x00437A00; // __thiscall (script, pos[3], out[3], delta)
        constexpr uintptr_t kDemoAdvanceMove = 0x004376F0; // __thiscall (script)
        constexpr uintptr_t kDemoHasMoveLeft = 0x00437710; // __thiscall (script) -> bool

        constexpr uintptr_t kDemoNextTimeCmd = 0x00437B10; // __thiscall (script) -> record or null
        constexpr uintptr_t kDemoNextViewCmd = 0x00437B40;
        constexpr uintptr_t kDemoNextCamCmd = 0x00437B70;
        constexpr uintptr_t kDemoFreeMoveCmds = 0x00437970; // __thiscall (script)

        // CDemoPlayer.
        constexpr uintptr_t kDemoPlayerScriptsOffset = 0x00;
        constexpr uintptr_t kDemoPlayerCountOffset = 0x04;
        constexpr uintptr_t kDemoPlayerIndexOffset = 0x08;
        constexpr uintptr_t kDemoPlayerCurrentOffset = 0x0C;
        constexpr uintptr_t kDemoPlayerPauseLeftOffset = 0x10;
        constexpr uintptr_t kDemoPlayerFactorLeftOffset = 0x14;
        constexpr uintptr_t kDemoPlayerFactorSpanOffset = 0x18;
        constexpr uintptr_t kDemoPlayerScaleOffset = 0x1C;
        constexpr uintptr_t kDemoPlayerFactorOffset = 0x20;
        constexpr uintptr_t kDemoPlayerCamSpanOffset = 0x24;
        constexpr uintptr_t kDemoPlayerCamElapsedOffset = 0x28;
        constexpr uintptr_t kDemoPlayerCamCmdOffset = 0x2C;
        constexpr uintptr_t kDemoPlayerCamMoverOffset = 0x30;
        constexpr uintptr_t kDemoPlayerViewSpanOffset = 0x64;
        constexpr uintptr_t kDemoPlayerViewElapsedOffset = 0x68;
        constexpr uintptr_t kDemoPlayerViewCmdOffset = 0x6C;
        constexpr uintptr_t kDemoPlayerViewMoverOffset = 0x70;

        constexpr uintptr_t kLevelDemoPlayerOffset = 0x48;
        constexpr uint32_t kLevelRecordMagic = 0x1F1F1F1F;

        // Demo script.
        constexpr size_t kDemoScriptSize = 0x58;
        constexpr uintptr_t kDemoScriptElapsedOffset = 0x00;
        constexpr uintptr_t kDemoScriptBreakTimeOffset = 0x04;
        constexpr uintptr_t kDemoScriptMoveCmdsOffset = 0x08;
        constexpr uintptr_t kDemoScriptMoveCountOffset = 0x0C;
        constexpr uintptr_t kDemoScriptPlayerPosOffset = 0x10;
        constexpr uintptr_t kDemoScriptRoomPosOffset = 0x18;
        constexpr uintptr_t kDemoScriptHasPlaceOffset = 0x20;
        constexpr uintptr_t kDemoScriptMoveIndexOffset = 0x24;
        constexpr uintptr_t kDemoScriptEnteringOffset = 0x28;
        constexpr uintptr_t kDemoScriptPauseLeftOffset = 0x2C;
        constexpr uintptr_t kDemoScriptTimeCursorOffset = 0x30;
        constexpr uintptr_t kDemoScriptViewCursorOffset = 0x34;
        constexpr uintptr_t kDemoScriptCamCursorOffset = 0x38;
        constexpr uintptr_t kDemoScriptCameraPendingOffset = 0x3C;
        constexpr uintptr_t kDemoScriptHasCameraOffset = 0x3D;
        constexpr uintptr_t kDemoScriptCameraPosOffset = 0x40;
        constexpr uintptr_t kDemoScriptCameraViewOffset = 0x4C;

        // $move_cmd.
        constexpr size_t kDemoMoveCmdSize = 0x20;
        constexpr uintptr_t kDemoMoveCmdPauseOffset = 0x00;
        constexpr uintptr_t kDemoMoveCmdHorzOffset = 0x04;
        constexpr uintptr_t kDemoMoveCmdVertOffset = 0x08;
        constexpr uintptr_t kDemoMoveCmdJumpOffset = 0x0C;
        constexpr uintptr_t kDemoMoveCmdEndCoordOffset = 0x10;
        constexpr uintptr_t kDemoMoveCmdTimeListOffset = 0x14;
        constexpr uintptr_t kDemoMoveCmdViewListOffset = 0x18;
        constexpr uintptr_t kDemoMoveCmdCamListOffset = 0x1C;

        // Command list.
        constexpr size_t kDemoListSize = 0x0C;
        constexpr uintptr_t kDemoListItemsOffset = 0x00;
        constexpr uintptr_t kDemoListCountOffset = 0x04;
        constexpr uintptr_t kDemoListCursorOffset = 0x08;

        // $time_cmd.
        constexpr size_t kDemoTimeCmdSize = 0x0C;
        constexpr uintptr_t kDemoTimeCmdPauseOffset = 0x00;
        constexpr uintptr_t kDemoTimeCmdFactorOffset = 0x04;
        constexpr uintptr_t kDemoTimeCmdTimeOffset = 0x08;

        // $cam_cmd / $view_cmd.
        constexpr size_t kDemoCamCmdSize = 0x20;
        constexpr uintptr_t kDemoCamCmdPosOffset = 0x00;
        constexpr uintptr_t kDemoCamCmdSpeedOffset = 0x0C;
        constexpr uintptr_t kDemoCamCmdTimeOffset = 0x18;
        constexpr uintptr_t kDemoCamCmdAutoOffset = 0x1C;

        // Camera mover.
        constexpr uintptr_t kMoverStart = 0x0044EE10; // __thiscall (mover, pos, aux, from, fromSpeed, time)
        constexpr uintptr_t kMoverAdvance = 0x0044EF20; // __thiscall (mover, pos, elapsed)

        constexpr uintptr_t kDemoMode = 0x00694635;
        constexpr uintptr_t kDemoBreakArmed = 0x0069463C;
        constexpr uintptr_t kDemoElapsed = 0x006946D4;

        constexpr uintptr_t kDemoCameraPos = 0x006946EC;
        constexpr uintptr_t kDemoCameraAux = 0x00694710;
        constexpr uintptr_t kDemoCameraView = 0x006946F8;
        constexpr uintptr_t kDemoCameraViewAux = 0x0069471C;
        constexpr uintptr_t kDemoCameraUp = 0x00694704;
        constexpr uintptr_t kDemoCameraFree = 0x00694788;
        constexpr uintptr_t kDemoViewFree = 0x00694789;

        constexpr uintptr_t kTimeScaleSlot = 0x006913BC; // __cdecl (float)
        constexpr uintptr_t kTimeScaleGetSlot = 0x006913C0; // __cdecl () -> float
        constexpr uintptr_t kLevelRecordSlot = 0x0069139C; // __cdecl ()
        constexpr uintptr_t kBreakDemoMenu = 0x004CF020; // "IDM_BREAK_DEMOLEVEL"
    }
}