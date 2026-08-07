#pragma once

#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
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
        constexpr uintptr_t kRoomCarryPosition = 0x0043DD50;
        constexpr uintptr_t kSessionCrossRoom = 0x00442F80;

        constexpr uintptr_t kLevelElapsed = 0x006946D4;
        constexpr uintptr_t kLevelParTime = 0x00694630;  // Results_Format scores the difference

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
        constexpr size_t kRoomLightTriangleStride = 0x24;

        // Room_Free 0x00412A40; allocation tags it uses when releasing.
        constexpr uintptr_t kRoomShadowTrianglesOffset = 0x2578; // 29 pairs of pointer, count
        constexpr size_t kRoomShadowTriangleLists = 29;
        constexpr size_t kRoomShadowTriangleStride = 0x24;
        constexpr uintptr_t kRoomStaticsOffset = 0x2660;
        constexpr uintptr_t kRoomAddStatic = 0x00412CD0; // __thiscall (room, record, translucent, lit)
        constexpr size_t kRoomStaticSize = 0x60;
        constexpr uintptr_t kRoomStaticFlagsOffset = 0x00;
        constexpr uintptr_t kRoomStaticModelOffset = 0x04;
        constexpr uintptr_t kRoomStaticTextureOffset = 0x08;
        constexpr uintptr_t kRoomStaticMatrixOffset = 0x10; // 4x4 floats, straight into the node
        constexpr uintptr_t kRoomStaticBlendOffset = 0x58; // 2 multiplies, else adds
        constexpr uintptr_t kRoomStaticNextOffset = 0x5C;
        constexpr uint32_t kRoomStaticLitFlag = 0x01;
        constexpr uint32_t kRoomStaticTranslucentFlag = 0x02;
        constexpr uint32_t kRoomStaticHiddenFlag = 0x04; // set: Scene_SortObjects skips it

        // Room ambient sound; Room_Activate starts one per record.
        constexpr uintptr_t kRoomLoopSoundsOffset = 0x2664; // stride 0x10
        constexpr uintptr_t kRoomLoopSoundCountOffset = 0x2668;
        constexpr uintptr_t kRoomLoopSoundIdOffset = 0x00;
        constexpr uintptr_t kRoomLoopSoundPositionOffset = 0x04; // three floats
        constexpr size_t kRoomLoopSoundStride = 0x10;
        constexpr size_t kRoomSize = 0x2684;

        // Grows the loop-sound array by one 0x10 record. __stdcall (&array, &count); RET 8.
        constexpr uintptr_t kRoomSoundsAppend = 0x00412E90;

        // Grows room light array by one 0x20 record; returns the new one.
        constexpr uintptr_t kRoomLightsAppend = 0x004378D0;
        constexpr uintptr_t kRoomAddLight = 0x00412FC0; // __thiscall (room, file light entry)

        // Room light sources (triangles are the baked result).
        constexpr uintptr_t kRoomLightsOffset = 0x266C;
        constexpr uintptr_t kRoomLightCountOffset = 0x2670;
        constexpr size_t kRoomLightStride = 0x20;

        // RoomSpawns_Append __cdecl (room, record) Mem_Clone __cdecl (source, size)
        constexpr uintptr_t kRoomSpawnsAppend = 0x00412F70;
        constexpr uintptr_t kMemClone = 0x0043FA60;

        constexpr float kCellToWorld = 0.1f;
        constexpr float kCellOriginX = -2.0f;
        constexpr float kCellOriginY = 1.45f;
        constexpr float kCellCentre = 0.05f;
        constexpr float kBonusHeight = 0.1f;

        // Room grid: memcpy(room+0x104, file, 0x2440); cell = 8 bytes.
        constexpr size_t kRoomGridColumns = 40;
        constexpr size_t kRoomGridRows = 29;
        constexpr size_t kRoomCellStride = 0x08;
        constexpr size_t kRoomGridSize = 0x2440;
        constexpr size_t kRoomGridRowStride = 0x140;

        // The only byte anything reads, movement included; +0x03 on are zero in every room
        constexpr uintptr_t kRoomCellTypeOffset = 0x00;
        constexpr uintptr_t kRoomCellFlagAOffset = 0x01;
        constexpr uintptr_t kRoomCellFlagBOffset = 0x02;

        // Across the 179 rooms: 'F' and '1' floor, 'I' chain and '|' pipe one column wide,
        // '-' '=' ladder always two with 'C' its top, and 'A' 'B' 'D' 'E' 'i' 'w' still unnamed.
        constexpr char kRoomCellWall = 'W';
        constexpr char kRoomCellEmpty = ' ';
        constexpr char kRoomCellChain = 'I';
        constexpr char kRoomCellPipe = '|';
        constexpr char kRoomCellLadder = '-';
        constexpr char kRoomCellLadderTop = 'C';

        // Box_Parse: $min → +0x04/+0x08/+0x0C, $max → +0x10/+0x14/+0x18.
        constexpr uintptr_t kBoxParse = 0x0043FB60;
        constexpr uintptr_t kBoxMinXOffset = 0x04;
        constexpr uintptr_t kBoxMinYOffset = 0x08;
        constexpr uintptr_t kBoxMinZOffset = 0x0C;
        constexpr uintptr_t kBoxMaxXOffset = 0x10;
        constexpr uintptr_t kBoxMaxYOffset = 0x14;
        constexpr uintptr_t kBoxMaxZOffset = 0x18;

        // Room collision box stride 0x1C; +0x00 = kind.
        constexpr uintptr_t kRoomDrawBox = 0x00405D60;
        constexpr size_t kRoomCollisionBoxStride = 0x1C;

        constexpr uintptr_t kRoomBoxKindOffset = 0x00;
        constexpr int kRoomBoxPushes = 0;
        constexpr int kRoomBoxReportsOnly = 1;
        constexpr int kRoomBoxPushesWhenAsked = 2;
    }
}
