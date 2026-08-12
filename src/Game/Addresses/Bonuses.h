#pragma once

#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
        constexpr uintptr_t kBonusLevelFlag = 0x00694628;
        constexpr uintptr_t kBonusLevelTimeLeft = 0x0069462C;
        constexpr uintptr_t kLevelBonusTimeOffset = 0x804;
        constexpr uintptr_t kLevelBonusCountOffset = 0x808;
        constexpr uintptr_t kLevelTimeOffset = 0x80C;
        constexpr uintptr_t kLevelNumberOffset = 0x810;
        constexpr uintptr_t kLevelDemoLevelOffset = 0x814;
        constexpr uintptr_t kLevelUseForDemoOffset = 0x815;
        constexpr uintptr_t kLevelMusicStartOffset = 0x818;
        constexpr uintptr_t kLevelMenuStatOffset = 0x81C;
        constexpr uintptr_t kLevelStartRoomXOffset = 0x91C;
        constexpr uintptr_t kLevelStartRoomYOffset = 0x920;
        constexpr uintptr_t kLevelStartCellOffset = 0x924;
        constexpr size_t kLevelNameLength = 0x100;

        constexpr uintptr_t kBonusesLeft = 0x006946A8;
        constexpr uintptr_t kMegaBonusesLeft = 0x006946B0;
        constexpr uintptr_t kOtherBonusesLeft = 0x006946A0;

        // What the dispatcher branches on, at the head of every placement record.
        constexpr int kPlacementKindBonus = 9;

        constexpr size_t kBonusSpawnSize = 0xBC;
        constexpr uintptr_t kBonusSpawnLiveOffset = 0x04;
        constexpr uintptr_t kBonusSpawnNameOffset = 0x05;
        constexpr uintptr_t kBonusSpawnPositionOffset = 0x48;
        constexpr uintptr_t kBonusSpawnPhaseOffset = 0x54; // Bonus_Think's rocking phase
        constexpr uintptr_t kBonusSpawnTagOffset = 0x64; // $tag, 0x40
        constexpr uintptr_t kBonusSpawnDecorOffset = 0xA4; // $decor, counts for nothing
        constexpr uintptr_t kBonusSpawnTimeOffset = 0xB4; // overrides the type's $time
        constexpr uintptr_t kBonusSpawnTypeOffset = 0xB8;

        // touching pair, each with itself first.
        constexpr uintptr_t kBonusOnTouch = 0x00430B80;
        constexpr uintptr_t kBonusThink = 0x00430A90;

        // Placement -> live bonus. Ninth ctrl-dispatcher branch (= kCtrlSpawnBonus).
        // __cdecl, plain RET, one arg. Refuses silently: null, kind!=9, live byte clear, type at +0xB8 unresolved.
        constexpr uintptr_t kBonusSpawn = 0x00431320;

        constexpr uintptr_t kBonusResFind = 0x00430680; // __cdecl (name) -> type record

        constexpr uintptr_t kBonusResTable = 0x0069148C;
        constexpr uintptr_t kBonusResCount = 0x00691490;
        constexpr size_t kBonusResStride = 0x118;
        constexpr uintptr_t kBonusResNameOffset = 0x000;
        constexpr uintptr_t kBonusResIdOffset = 0x040;
        constexpr uintptr_t kBonusResTextureOffset = 0x080;
        constexpr uintptr_t kBonusResTexAnimOffset = 0x084;
        constexpr uintptr_t kBonusResModelOffset = 0x088;
        constexpr uintptr_t kBonusResIconOffset = 0x08C; // $icon, what the HUD draws
        constexpr uintptr_t kBonusResGlowOffset = 0x090;
        constexpr uintptr_t kBonusResGlow2Offset = 0x0A0;
        constexpr uintptr_t kBonusResPointsTakeOffset = 0x0B0;
        constexpr uintptr_t kBonusResPointsOutOffset = 0x0B4;
        constexpr uintptr_t kBonusResBoxOffset = 0x0B8;
        constexpr uintptr_t kBonusResSoundTakeOffset = 0x0D4;
        constexpr uintptr_t kBonusResSoundUseOffset = 0x0D8;
        constexpr uintptr_t kBonusResFlagsOffset = 0x0DC;

        // the entity at spawn, so changing it only takes on the next spawn.
        constexpr uintptr_t kBonusResRockingAmpOffset = 0x0E0;
        constexpr uintptr_t kBonusResRockingSpeedOffset = 0x0E4;
        constexpr uintptr_t kBonusResRockingAxisOffset = 0x0E8;
        constexpr uintptr_t kBonusResTrainIndexOffset = 0x0F4;
        constexpr uintptr_t kBonusResTimeOffset = 0x0F8;
        constexpr uintptr_t kBonusResLightColorOffset = 0x0FC;
        constexpr uintptr_t kBonusResLightRadiusOffset = 0x10C;
        constexpr uintptr_t kBonusResBonusLightRadiusOffset = 0x110;
        constexpr uintptr_t kBonusResBurnTimeOffset = 0x114;

        constexpr uint32_t kBonusFlagTotem = 0x01;
        constexpr uint32_t kBonusFlagTorch = 0x02;
        constexpr uint32_t kBonusFlagMakesTrain = 0x04;
        constexpr uint32_t kBonusFlagSuppressed = 0x08; // slot only: skip the game's tick

        constexpr uintptr_t kBonusResAppend = 0x00431690;

        // records holding the same handle release it twice.
        constexpr uintptr_t kBonusesFreeResources = 0x00430700;
    }
}
