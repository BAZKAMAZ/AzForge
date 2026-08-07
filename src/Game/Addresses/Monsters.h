#pragma once

#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
        constexpr uintptr_t kMonsterResTable = 0x00691458;
        constexpr uintptr_t kMonsterResCount = 0x0069145C;
        constexpr size_t kMonsterResStride = 0xDC;
        constexpr uintptr_t kMonsterResNameOffset = 0x000; // $name, 0x40
        constexpr uintptr_t kMonsterResTexEasyOffset = 0x040;
        constexpr uintptr_t kMonsterResTexHardOffset = 0x044;
        constexpr uintptr_t kMonsterResModelOffset = 0x048;
        constexpr uintptr_t kMonsterResAnimationOffset = 0x04C;
        constexpr uintptr_t kMonsterResSpeedUpOffset = 0x050;
        constexpr uintptr_t kMonsterResSpeedOffset = 0x054;
        constexpr uintptr_t kMonsterResVertSpeedOffset = 0x058;
        constexpr uintptr_t kMonsterResPartColorOffset = 0x05C; // four floats
        constexpr uintptr_t kMonsterResPartColorHardOffset = 0x06C;
        constexpr uintptr_t kMonsterResBoxOffset = 0x07C; // 0x1c, as a room's boxes are
        constexpr uintptr_t kMonsterResBox2Offset = 0x098; // both fall back to $bbox
        constexpr uintptr_t kMonsterResObjBoxOffset = 0x0B4;
        constexpr uintptr_t kMonsterResJumpOffset = 0x0D0; // "j_skull" alone reads these
        constexpr uintptr_t kMonsterResJumpFactorOffset = 0x0D4;
        constexpr uintptr_t kMonsterResFallOffset = 0x0D8;

        // letting them through changes no monster the game already has.
        constexpr uintptr_t kMonsterJumpKeysGuard = 0x004302B9;
        constexpr uint8_t kMonsterJumpKeysGuardBytes[] = { 0x75, 0x4F };

        constexpr uintptr_t kMonsterDieBlast = 0x00691400; // $die_blast, 0x2c
        constexpr uintptr_t kMonsterDieBlastHard = 0x0069142C;

        constexpr uintptr_t kMonstersLoadResources = 0x0042FFD0;
        constexpr uintptr_t kMonstersFreeResources = 0x0042F660;
        constexpr uintptr_t kMonsterResAppend = 0x0042FF60; // (record**, int*) -> record
        constexpr uintptr_t kMonsterResFind = 0x0042F5C0; // (name) -> record, by $name
        constexpr uintptr_t kMonsterParseSpawn = 0x0042F730;
        constexpr uintptr_t kMonsterKill = 0x0042FDB0; // __fastcall (entity)

        // cached in the last dword.
        constexpr size_t kMonsterSpawnSize = 0x124;
        constexpr uintptr_t kMonsterSpawnNameOffset = 0x004; // $name, 0x40
        constexpr uintptr_t kMonsterSpawnPositionOffset = 0x044;
        constexpr uintptr_t kMonsterSpawnLevelOffset = 0x050; // $level; 1 and 3 are the hard ones
        constexpr uintptr_t kMonsterSpawnLiveOffset = 0x054; // cleared by Monster_Kill
        constexpr uintptr_t kMonsterSpawnSpeedOffset = 0x058; // $speed, doubled when $level is hard
        constexpr uintptr_t kMonsterSpawnDirOffset = 0x064; // $dir
        constexpr uintptr_t kMonsterSpawnAttackDistOffset = 0x068;
        constexpr uintptr_t kMonsterSpawnAttackTimeOffset = 0x06C; // every $*_time is two floats,
        constexpr uintptr_t kMonsterSpawnBluffTimeOffset = 0x074; // a range the monster picks in
        constexpr uintptr_t kMonsterSpawnRadiusOffset = 0x07C; // the circling monsters
        constexpr uintptr_t kMonsterSpawnAxisOffset = 0x080;
        constexpr uintptr_t kMonsterSpawnPhaseOffset = 0x08C;
        constexpr uintptr_t kMonsterSpawnPosLerpOffset = 0x090;
        constexpr uintptr_t kMonsterSpawnTagOffset = 0x09C; // $tag, 0x40
        constexpr uintptr_t kMonsterSpawnDecorOffset = 0x0DC;
        constexpr uintptr_t kMonsterSpawnMoveTypeOffset = 0x0F0;
        constexpr uintptr_t kMonsterSpawnWalkTimeOffset = 0x100;

        constexpr uintptr_t kMonsterSpawnWalkWaitOffset = 0x108;
        constexpr uintptr_t kMonsterSpawnSitTimeOffset = 0x110;
        constexpr uintptr_t kMonsterSpawnWaitTimeOffset = 0x118;
        constexpr uintptr_t kMonsterSpawnTypeOffset = 0x120; // the definition record

        constexpr uintptr_t kMonsterSpawnStateOffset = 0x0E0; // 0 "move", 1 "sit"
        constexpr uintptr_t kMonsterSpawnTimerOffset = 0x0E8;
        constexpr int kMonsterStateMove = 0;
        constexpr int kMonsterStateSit = 1;

        constexpr uintptr_t kMonsterSpawnWalkStateOffset = 0x0E4; // 0 walking, 1 standing still
        constexpr uintptr_t kMonsterSpawnWalkTimerOffset = 0x0EC;
        constexpr int kMonsterWalkStep = 0;
        constexpr int kMonsterWalkStay = 1;

        constexpr uintptr_t kMonsterStepMove = 0x004457D0; // __cdecl (entity, placement)
        constexpr uintptr_t kMonsterStepSit = 0x00445A40;

        // in the same block and the placement only appended, not spawned.
        constexpr uintptr_t kMonsterParseSpawnRoutine = 0x0042F730;

        constexpr float kMonsterCellZ = 0.2f;

        constexpr uintptr_t kMonsterSpawnPlacement = 0x0042FD60;
        constexpr uintptr_t kMonsterSpawnTable = 0x004CE4F8;
        constexpr size_t kMonsterSpawnTableCount = 10;

        // bat, which dies through an animation instead of at once.
        constexpr uintptr_t kMonsterOnHit = 0x00447200;
        constexpr uintptr_t kEntityHitHandlerOffset = 0x5C4;

        struct MonsterMovement
        {
            const char* name;
            uintptr_t think;

            bool speedInEntity;
        };

        constexpr MonsterMovement kMonsterMovements[] =
        {
            { "skull",    0x00448070, true  },
            { "snake",    0x00447AB0, false },
            { "snake2",   0x00447AC0, false },
            { "spider",   0x004474B0, true  }, // the scorpion takes this one on one of its branches
            { "scorpion", 0x00446E40, true  },
            { "j_skull",  0x00446800, true  },
            { "bat",      0x004463C0, false },
            { "mummy",    0x004460A0, true  },
            { "mummy2",   0x00445C30, true  },
        };

        // never touch those fields.
        constexpr uintptr_t kMonsterThinkMummy = 0x004460A0;
        constexpr uintptr_t kMonsterThinkMummy2 = 0x00445C30;

        constexpr uintptr_t kMonstersKilled = 0x00694694;
    }
}
