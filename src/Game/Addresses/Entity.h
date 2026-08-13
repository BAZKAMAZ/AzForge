#pragma once

#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
        constexpr uintptr_t kEntities = 0x00517754;
        constexpr uintptr_t kEntitiesLiveOffset = 0x0C;
        constexpr uintptr_t kEntitiesFreeOffset = 0x10;
        constexpr uintptr_t kEntitiesTimeScaleOffset = 0x18;
        constexpr size_t kEntityPoolCount = 400;

        constexpr size_t kEntitySize = 0x5D4;
        constexpr uintptr_t kEntityDeltaOffset = 0x088;
        constexpr uintptr_t kEntityPositionOffset = 0x124;
        constexpr uintptr_t kEntityPreviousPositionOffset = 0x130;
        constexpr uintptr_t kEntityStateOffset = 0x178;
        constexpr uintptr_t kEntitySubStateOffset = 0x17C;
        constexpr uintptr_t kEntitySpawnOffset = 0x5CC;
        constexpr uintptr_t kEntityFlagsOffset = 0x48C;
        constexpr uintptr_t kEntityDeadOffset = 0x594;
        constexpr uintptr_t kEntityNextOffset = 0x5D0;

        // Body_Step __cdecl (body, dx, dy, jump, delta, flags), ADD ESP,0x18 at both call sites.
        constexpr uintptr_t kBodyStep = 0x0043F140;

        // The five Player_AdjustMove asks of the window
        constexpr uintptr_t kPlaceAskLeft = 0x00441D70;    // then dx = -1
        constexpr uintptr_t kPlaceAskRight = 0x00441DD0;   // then dx = +1
        constexpr uintptr_t kPlaceAskClimb = 0x00441E30;   // 'C' on his row or the two above
        constexpr uintptr_t kPlaceAskFooting = 0x00441E60; // two rows down is neither ' ' nor '-'

        // MOV CL,0x50 - it looks for 'P', and 'P' is in none of the 179 shipped room grids, so on
        constexpr uintptr_t kPlaceAskPipe = 0x00441E90;

        // Entity_PlayAnim (entity, name, flag)
        constexpr uintptr_t kEntityPlayAnim = 0x0042CD90;
        constexpr size_t kInventorySlotStride = 0x60;
        constexpr size_t kInventorySlotCount = 5;
        constexpr uintptr_t kInventoryNameOffset = 0x00;
        constexpr uintptr_t kInventoryIconOffset = 0x40;
        constexpr uintptr_t kInventoryPointsOutOffset = 0x44;
        constexpr uintptr_t kInventorySoundUseOffset = 0x48;
        constexpr uintptr_t kInventoryTypeOffset = 0x4C;
        constexpr uintptr_t kInventoryFlagsOffset = 0x50;

        // Inventory_Update counts a slot down only when the first is set and the second is clear,
        // so the second is the game's own "leave this one alone" - no hook needed to stop a timer.
        constexpr uint32_t kInventoryFlagTicks = 0x1;
        constexpr uint32_t kInventoryFlagHeld = 0x8;
        constexpr uintptr_t kInventoryTimeLeftOffset = 0x54;
        constexpr uintptr_t kInventoryTimeTotalOffset = 0x58;
        constexpr uintptr_t kInventoryBurnTimeOffset = 0x5C;

        constexpr uintptr_t kEntityKindOffset = 0x00;
        constexpr int kEntityKindPlayer = 1;
        constexpr uintptr_t kEntityNameOffset = 0x04;

        constexpr uintptr_t kSpawnKindOffset = 0x00;
        constexpr size_t kSpawnNameLength = 0x40;
        constexpr int kSpawnKindMonster = 10;
        constexpr int kSpawnKindBonus = 9;
        constexpr int kSpawnKindDoor = 2;

        struct SpawnLayout
        {
            int kind;
            uintptr_t name;
            uintptr_t position;
            uintptr_t liveByte; // 0 = none
        };

        constexpr SpawnLayout kSpawnLayouts[] =
        {
            { kSpawnKindMonster, 0x04, 0x44, 0x00 },
            { kSpawnKindBonus,   0x05, 0x48, 0x04 },
            { kSpawnKindDoor,    0x18, 0x58, 0x00 },
        };

        // Placement_ReadState __cdecl (record, visitor)
        constexpr uintptr_t kPlacementReadState = 0x0042C810;

        // SpawnPlacement __cdecl (room, record) slot filled at runtime
        constexpr uintptr_t kSpawnPlacementSlot = 0x00517710;

        constexpr uintptr_t kEntityMoveStep = 0x0043F140;
        constexpr uintptr_t kEntityMoveStateOffset = 0x120;

        // Entity_MoveStep only sorts out the way speed at entity+0x224 and steps by speed * direction * delta way: a ...
        constexpr uintptr_t kEntityStepWalk = 0x0043E480;

        // The body, entity-relative is what says the mapping is right.
        constexpr uintptr_t kEntityMoveFlagsOffset = 0x120; // what this body is allowed to do
        constexpr uintptr_t kEntityMoveStateNowOffset = 0x17C; // 1 and 6 are the two walking ones
        constexpr uintptr_t kEntityMoveLastDirOffset = 0x1A4;
        constexpr uintptr_t kEntityMoveStateTimeOffset = 0x1A8;
        constexpr uintptr_t kEntityMoveVelocityOffset = 0x1AC; // along the way it faces
        constexpr uintptr_t kEntityMoveVertVelocityOffset = 0x1B4;
        constexpr uintptr_t kEntityMoveHitFlagsOffset = 0x1C4; // what the last step ran into
        constexpr uintptr_t kEntityMoveHeightOffset = 0x220; // how far above the floor
        constexpr uintptr_t kEntityMoveGravityOffset = 0x22C; // multiplies the one below
        constexpr uintptr_t kEntityMoveFallSpeedOffset = 0x230; // copied from a global at spawn

        constexpr uintptr_t kEntityMoveAccelOffset = 0x248;

        // void __cdecl (*)(entity) from Entities_Update; null means the entity does not update
        constexpr uintptr_t kEntityThinkOffset = 0x5B8;
        constexpr uintptr_t kEntitiesUpdate = 0x004194B0;

        // Entities_FreeAll: __fastcall (container). Room change frees all; addresses are recycled.
        constexpr uintptr_t kEntitiesFreeAll = 0x00419DA0;

        // Entities_Alloc: __cdecl (). Pops free list, clears 0x5D4, links live. Null if pool empty.
        constexpr uintptr_t kEntitiesAlloc = 0x00419320;

        // Touch: void __cdecl (self, other, side). Both sides called; side 1 up, 2 down, 3 left, 4 right.
        constexpr uintptr_t kEntityTouchHandlerOffset = 0x5BC;

        // void __cdecl (self, flag) - monster touch calls this to die. Empty on mod-built entities.
        constexpr uintptr_t kEntityDieOffset = 0x5C4;
        constexpr uintptr_t kEntitiesResolveTouches = 0x00413CD0;

        // 0 = not in meetings. Ctrl spawn writes 1.
        constexpr uintptr_t kEntityTouchableOffset = 0x5B4;

        constexpr uintptr_t kEntityRockingAxisOffset = 0x4FC;
        constexpr uintptr_t kEntityRockingOffset = 0x508;

        // Entities_Update rebuilds object box +0x158 from the anim frame only when set.
        // Frame box +0x13C always follows. Monster_Spawn* sets it; without it the box stays fixed.
        constexpr uintptr_t kMonsterEntityBoxFollowsAnimOffset = 0x174;

        // Live bonus entity kind (placement kind is 9).
        constexpr int kEntityKindBonus = 3;

        // Glow Scene_SortObjects draws; defaults live in globals, not the type.
        constexpr uintptr_t kEntityGlowOffset = 0x534;
        constexpr uintptr_t kEntityGlow2Offset = 0x54C;
        constexpr uintptr_t kEntityGlowStateOffset = 0x524; // three dwords from globals below
        constexpr uintptr_t kEntityGlowPhaseOffset = 0x530; // random so bonuses pulse apart
        constexpr uintptr_t kEntityGlowSpriteOffset = 0x544;

        constexpr uintptr_t kBonusGlowState0 = 0x00691468;
        constexpr uintptr_t kBonusGlowState1 = 0x00691470;
        constexpr uintptr_t kBonusGlowState2 = 0x00691474;
        constexpr uintptr_t kBonusGlowSprite = 0x0069146C;

        // Door open scale: update writes 0..1 into +0x254 each frame.
        constexpr uintptr_t kEntityScaleXOffset = 0x250;
        constexpr uintptr_t kEntityScaleYOffset = 0x254;

        constexpr uintptr_t kEntityModelOffset = 0x274;
        constexpr uintptr_t kEntityTextureOffset = 0x278;
        constexpr uintptr_t kBonusSpawnOwnModelOffset = 0x58; // nonzero: the two below are used
        constexpr uintptr_t kBonusSpawnModelOffset = 0x5C;
        constexpr uintptr_t kBonusSpawnTextureOffset = 0x60;

        // Live monster entity kind (placement kind is 10).
        constexpr int kEntityKindMonster = 2;
        constexpr uintptr_t kMonsterEntityTagOffset = 0x044;
        constexpr uintptr_t kMonsterEntityObjBoxOffset = 0x158;
        constexpr uintptr_t kMonsterEntitySpeedUpOffset = 0x190;
        constexpr uintptr_t kMonsterEntitySpeedOffset = 0x224;
        constexpr uintptr_t kMonsterEntityPlayerOffset = 0x24C; // cached by the first think
        constexpr uintptr_t kMonsterEntityLevelOffset = 0x4C8;
        // 1.0 or 2.0 (hard). Written by every Spawn*, read by nothing -- write the speeds instead.
        constexpr uintptr_t kMonsterEntityHardOffset = 0x4E4;

        // type+0x54 * hard, and type+0x58 * hard twice -- what actually walks it.
        constexpr uintptr_t kMonsterEntitySpeedLowOffset = 0x234;
        constexpr uintptr_t kMonsterEntitySpeedTopOffset = 0x238;
        constexpr uintptr_t kMonsterEntityFacingOffset = 0x588; // -1 or 1
        constexpr uintptr_t kEntityCollidesOffset = 0x595;

        constexpr uint32_t kEntityHiddenFlag = 0x100; // in kEntityFlagsOffset

        constexpr uint32_t kEntityBlendFlag = 0x80;

        constexpr uintptr_t kEntitySecondModelOffset = 0x284;
        constexpr uintptr_t kEntitySecondTextureOffset = 0x288;

        // An entity can carry a light of its own: with bit 0x400 set Scene_SortObjects puts one up for it every frame...
        constexpr uint32_t kEntityLightFlag = 0x400; // in kEntityFlagsOffset
        constexpr uintptr_t kEntityLightOffsetOffset = 0x55C; // three floats, added to position
        constexpr uintptr_t kEntityLightColourOffset = 0x568; // three floats and a fourth unread
        constexpr uintptr_t kEntityLightAlphaOffset = 0x574; // carried, never read by lighting
        constexpr uintptr_t kEntityLightRangeOffset = 0x578; // the flicker is added to this

        // The four run together, so putting one back is one copy of 0x20 bytes.
        constexpr size_t kEntityLightBlockSize = 0x20;
        constexpr uintptr_t kInventorySetTorchLight = 0x00430660; // __cdecl (player, on)

        // Where a live entity keeps the two halves of playing one.
        constexpr uintptr_t kEntityAnimationOffset = 0x27C;
        constexpr uintptr_t kEntityAnimStateOffset = 0x4A0;

        // Collision boxes stop bodies; grid is floor/chain knowledge only.
        constexpr uintptr_t kEntityCollideRoomBoxes = 0x0040DEC0; // __cdecl (entity)
        constexpr uintptr_t kBoxResolveAgainstEntity = 0x00413960; // __cdecl (entity, box, flags)
    }
}
