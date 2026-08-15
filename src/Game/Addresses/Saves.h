#pragma once
#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
        // MOV ECX, g_scoreMgr / CALL ScoreSaver_Save
        // MOV ECX, g_gamePacks / CALL GamePacks_Save
        constexpr uintptr_t kScoreManager = 0x00695014;
        constexpr uintptr_t kScoreSaverSave = 0x00450610; // __thiscall (mgr)
        constexpr uintptr_t kScoreTotalCount = 0x00450580; // __thiscall (mgr)
        constexpr uintptr_t kScoreCastlesOffset = 0x04; // Castle**
        constexpr uintptr_t kScoreCastleCountOffset = 0x08;
        constexpr uintptr_t kCastleEntriesOffset = 0x04; // ScoreEntry*, packed
        constexpr uintptr_t kCastleEntryCountOffset = 0x08;
        constexpr size_t kScoreEntrySize = 0x8C;
        constexpr uintptr_t kScoreEntryNameOffset = 0x00; // char[0x40]
        constexpr uintptr_t kScoreEntryLevelOffset = 0x40; // char[0x40], last byte NUL on load
        constexpr uintptr_t kScoreEntryTimeOffset = 0x80; // float
        constexpr uintptr_t kScoreEntryScoreOffset = 0x84;
        constexpr size_t kScoreTextSize = 0x40;
        constexpr uintptr_t kScoreEntryFilledOffset = 0x88; // decides if entry survives the save

        // Castle 0x0C: header, entry array, count. ScoreMgr_GrowCastles grows mgr+0x04 by count*4+4.
        constexpr size_t kCastleSize = 0x0C;
        constexpr uintptr_t kScoreDropUnfilled = 0x00450550; // __thiscall (mgr)
        constexpr uintptr_t kCastleList = 0x00695020;
        constexpr uintptr_t kCastleIndexOfLevel = 0x00450E20; // __thiscall (castleList, levelPath)

        // Grows the castle array if needed, then returns a ranked 0x8C slot.
        constexpr uintptr_t kScoreInsertSlot = 0x00450850; // __thiscall (mgr, score, castle)
        constexpr uintptr_t kScoreInitScript = 0x004D1850;

        // ---- packs.sav --------------------------------------------------------------------------
        constexpr uintptr_t kGamePacks = 0x0069502C;
        constexpr uintptr_t kGamePacksWrite = 0x004533B0; // __thiscall (packs, path)
        constexpr uintptr_t kGamePacksWriteAndFree = 0x00453560;
        constexpr uintptr_t kGamePacksLoad = 0x004535A0; // __thiscall (packs)
        constexpr const char* kGamePacksFile = "packs.sav";

        constexpr uintptr_t kGamePacksOpenLevel = 0x004537F0; // __thiscall (packs, levelId)
        constexpr uintptr_t kGamePacksCurrentOffset = 0x04;
        constexpr uintptr_t kGamePacksTotalOffset = 0x08;
        constexpr uintptr_t kGamePacksDescOffset = 0x0C; // 0x244 each
        constexpr uintptr_t kGamePacksOpenOffset = 0x10;
        constexpr uintptr_t kGamePacksOpenCountOffset = 0x14;
        constexpr size_t kGamePackDescSize = 0x244;
        constexpr size_t kOpenLevelSize = 0x100;

        // ---- saves/saveNNN.sav ------------------------------------------------------------------
        constexpr uintptr_t kGameSaveToFile = 0x00439530; // __cdecl (path, caption) -> bool in AL
        constexpr uintptr_t kGameLoadFromFile = 0x00439740; // __cdecl (path)
        constexpr uintptr_t kGameReadSaveHeader = 0x004399C0; // __cdecl (path, char out[0x770]) -> bool
        constexpr size_t kSaveHeaderSize = 0x770;
        constexpr uintptr_t kSaveCaptionOffset = 0x03;
        constexpr uintptr_t kSaveVersionOffset = 0x44;
        constexpr uintptr_t kSaveLevelOffset = 0x48;
        constexpr size_t kSaveCaptionSize = 65;

        // ---- the level section
        constexpr size_t kSaveLevelSection = 0x770; // straight after the header
        constexpr size_t kSaveLevelPathSize = 0x100;
        constexpr size_t kSaveMazeCursorSize = 8;

        // ---- room things (typed records) --------------------------------------------------------
        constexpr uintptr_t kRoomThingsOffset = 0x2678; // array of pointers
        constexpr uintptr_t kRoomThingCountOffset = 0x267C;
        constexpr uintptr_t kRoomContainerOffset = 0x2680; // POINTER to container (not the container)
        constexpr uint32_t kSaveThingMagic = 0x1F1F1F1F;

        constexpr uintptr_t kThingTagOffset = 0x00;
        constexpr uintptr_t kThingKind2Offset = 0x04;
        constexpr uintptr_t kThingKind9Offset = 0x04;
        constexpr uintptr_t kThingKind10Offset = 0x54;
        constexpr uintptr_t kThingKind11Offset = 0x04;
        constexpr uintptr_t kContainerExtraOffset = 0x08;
        constexpr uintptr_t kContainerExtraFlagOffset = 0x48;
        constexpr uintptr_t kContainerStateOffset = 0x44;

        // ---- level stats (0x58 bytes, written as one block) ------------------------------------
        constexpr uintptr_t kLevelStats = 0x00694680;
        constexpr size_t kLevelStatsSize = 0x58;
        constexpr uintptr_t kStatMonstersOffset = 0x10;
        constexpr uintptr_t kStatSuperOffset = 0x1C;
        constexpr uintptr_t kStatCrystalsOffset = 0x24;
        constexpr uintptr_t kStatBonusesOffset = 0x2C;
        constexpr uintptr_t kStatLightRoomsOffset = 0x34;
        constexpr uintptr_t kStatDarkRoomsOffset = 0x3C;
        constexpr uintptr_t kStatSecretRoomsOffset = 0x44;
        constexpr uintptr_t kStatEighthOffset = 0x4C;
    }
}