#pragma once

#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
        // GameState_SetEvent: __thiscall (manager, id, a, b); stores id@+0, a@+4, b@+8; leave vtable+0x14, enter +0x10
        constexpr uintptr_t kGameStateSetEvent = 0x0043A5E0;
        constexpr uintptr_t kGameStateArgOffset = 0x04;

        // Death screen: fade phases; life spent in phase 1, respawn in phase 2.
        constexpr uintptr_t kGameStateUpdatePlayerDie = 0x0043A710;
        constexpr uintptr_t kPlayerRespawn = 0x00442760;

        struct SlotBaseSite
        {
            uintptr_t address;
            uint8_t length; // what the original occupies
            uint8_t prefixLength; // the opcode of the replacement, before the address
            uint8_t prefix[2];
            uint32_t addend; // added to the buffer, for a site that meant a field of a slot
            uint8_t original[7];
        };

        constexpr SlotBaseSite kSlotBaseSites[] =
        {
            // ADD ESI,0x2ac -> MOV ESI,buffer
            { 0x004303DD, 6, 1, { 0xBE, 0x00 }, 0, { 0x81, 0xC6, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // LEA EDI,[EBP+0x2ac] -> MOV EDI,buffer
            { 0x0043043E, 6, 1, { 0xBF, 0x00 }, 0, { 0x8D, 0xBD, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // LEA EAX,[EAX+EBP+0x2ac] -> ADD EAX,buffer (EAX is the index, EBP the player)
            { 0x00430489, 7, 1, { 0x05, 0x00 }, 0, { 0x8D, 0x84, 0x28, 0xAC, 0x02, 0x00, 0x00 } },
            // LEA ESI,[EBP+0x2ac] -> MOV ESI,buffer
            { 0x004304AE, 6, 1, { 0xBE, 0x00 }, 0, { 0x8D, 0xB5, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // LEA EAX,[ECX+EBP+0x2ac] -> LEA EAX,[ECX+buffer] (ECX is the index)
            { 0x00430500, 7, 2, { 0x8D, 0x81 }, 0, { 0x8D, 0x84, 0x29, 0xAC, 0x02, 0x00, 0x00 } },
            // LEA ECX,[EDX+0x2ac] -> MOV ECX,buffer
            { 0x00430516, 6, 1, { 0xB9, 0x00 }, 0, { 0x8D, 0x8A, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // LEA EAX,[EAX+EDX+0x2ac] -> ADD EAX,buffer (EAX is the index, EDX the player)
            { 0x00430541, 7, 1, { 0x05, 0x00 }, 0, { 0x8D, 0x84, 0x10, 0xAC, 0x02, 0x00, 0x00 } },
            // LEA ESI,[EDI+0x2ac] -> MOV ESI,buffer
            { 0x00431708, 6, 1, { 0xBE, 0x00 }, 0, { 0x8D, 0xB7, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // ADD EDI,0x2ac -> MOV EDI,buffer
            { 0x0044E730, 6, 1, { 0xBF, 0x00 }, 0, { 0x81, 0xC7, 0xAC, 0x02, 0x00, 0x00, 0x00 } },

            // Sites below also run on save/load.

            // ADD ECX,0x2ac -> MOV ECX,buffer
            { 0x00443FFA, 6, 1, { 0xB9, 0x00 }, 0, { 0x81, 0xC1, 0xAC, 0x02, 0x00, 0x00, 0x00 } },
            // LEA ECX,[EAX+EBX+0x2ac] -> LEA ECX,[EBX+buffer] (EBX walks the slots)
            { 0x00444013, 7, 2, { 0x8D, 0x8B }, 0, { 0x8D, 0x8C, 0x18, 0xAC, 0x02, 0x00, 0x00 } },
            // MOV [EDX+EBX+0x2ec],ECX -> MOV [EBX+buffer+0x40],ECX the slot's icon
            { 0x00444030, 7, 2, { 0x89, 0x8B }, 0x40, { 0x89, 0x8C, 0x1A, 0xEC, 0x02, 0x00, 0x00 } },
            // MOV [EDX+EBX+0x2f4],ECX -> MOV [EBX+buffer+0x48],ECX its use sound
            { 0x00444040, 7, 2, { 0x89, 0x8B }, 0x48, { 0x89, 0x8C, 0x1A, 0xF4, 0x02, 0x00, 0x00 } },
            // MOV [EDX+EBX+0x2f8],EAX -> MOV [EBX+buffer+0x4c],EAX and its type
            { 0x0044404A, 7, 2, { 0x89, 0x83 }, 0x4C, { 0x89, 0x84, 0x1A, 0xF8, 0x02, 0x00, 0x00 } },
            // LEA ECX,[EAX+EBX+0x2ac] -> LEA ECX,[EBX+buffer]
            { 0x00444058, 7, 2, { 0x8D, 0x8B }, 0, { 0x8D, 0x8C, 0x18, 0xAC, 0x02, 0x00, 0x00 } },
        };

        struct SlotCountSite
        {
            uintptr_t address; // the byte holding the count
            uint8_t opcode; // what has to be in front of it for this to be that site
            uintptr_t opcodeAt;
        };

        constexpr SlotCountSite kSlotCountSites[] =
        {
            { 0x00430416, 0xFF, 0x00430415 },
            { 0x00430477, 0xFE, 0x00430476 },
            { 0x004304E4, 0xFF, 0x004304E3 },
            { 0x0043052B, 0xF8, 0x0043052A },
            { 0x0043170F, 0xBB, 0x0043170E },
            { 0x0044E737, 0xBD, 0x0044E736 },
        };

        // Null until a game is running.
        constexpr uintptr_t kGameSession = 0x006928F0;
        constexpr uintptr_t kSessionPlayerOffset = 0x04;
        constexpr uintptr_t kSessionScoreOffset = 0xBDC;

        constexpr uintptr_t kPlayerInventoryOffset = 0x2AC;

        // Player_Update: input -> body step -> state. Everything below is read/written by it.
        constexpr uintptr_t kPlayerUpdate = 0x00443180;

        // Player_Die: __cdecl (entity, kind), ADD ESP,8; in kEntityHitHandler slot. kind 0/1/3.
        constexpr uintptr_t kPlayerDie = 0x004418C0;
        constexpr int kPlayerDieKindPlain = 0;
        constexpr int kPlayerDieKindCrashed = 1;
        constexpr int kPlayerDieKindAtOnce = 3;

        constexpr uintptr_t kPlayerSpawn = 0x00443C00;

        // body+0x6C collide flags; 0x44 at spawn (0x40 scorpion/spider); cleared while falling.
        constexpr uintptr_t kPlayerCollideFlagsOffset = 0x18C;

        // body+0x90: on stair; cleared each Body_Step; watched by Player_MovementChanged.
        constexpr uintptr_t kPlayerOnStairOffset = 0x1B0;
        constexpr uintptr_t kPlaceAskStair = 0x0043D380; // 0 none, 1 to the left, 2 to the right
        constexpr uintptr_t kPlayerStairSide = 0x0043D510;
        constexpr uintptr_t kPlayerMovementChanged = 0x0043FC30;
        constexpr uintptr_t kPlayerLastMovementOffset = 0x29C; // state, sub-state, dir, +0x1B0

        // body+0xF1: may-drop; Body_BeginDrop clears, Player_Update restores after 0.5s.
        constexpr uintptr_t kPlayerMayDropOffset = 0x211;
        constexpr uintptr_t kPlayerDropCooldown = 0x0069352C;

        // body+0x5C sub-state: 0 still, 1 air, 2/3 walk +/-x, 4/5 climb, 6 dropped (Body_BeginDrop).
        constexpr uintptr_t kBodySubStateOffset = 0x5C;
        constexpr uintptr_t kBodyBeginDrop = 0x0043E400;
        constexpr uintptr_t kPlayerPreviousStateOffset = 0x184;
        constexpr uintptr_t kPlayerPreviousSubStateOffset = 0x188;

        constexpr uintptr_t kPlayerBodyExtentOffset = 0x13C; // what Player_PlaceFill is handed second
        constexpr uintptr_t kPlayerBodyOffset = 0x120; // what the move and collide routines take
        constexpr uintptr_t kPlayerFallSpeedOffset = 0x220;
        constexpr uintptr_t kPlayerFadeTimerOffset = 0x25C; // counts down, then the hidden flag goes on
        // Facing from walk dir +0x1A4: -1 -> +pi/2, +1 -> -pi/2 (floats at 0x004CE0F8).
        constexpr uintptr_t kPlayerWalkDirOffset = 0x1A4;
        constexpr uintptr_t kPlayerFacingOffset = 0x26C; // pi one way, ~0.3145 the other
        constexpr uintptr_t kPlayerAnimSetOffset = 0x27C;
        constexpr uintptr_t kPlayerAnimStateOffset = 0x4A0;

        // Cell window 0x38: col/row + 10x4 cells; stands at col 2, row 3 (+0x16).
        constexpr uintptr_t kPlayerCellWindowOffset = 0x1D8;
        constexpr size_t kPlayerCellWindowSize = 0x38;
        constexpr uintptr_t kPlayerCellWindowColumnOffset = 0x00;
        constexpr uintptr_t kPlayerCellWindowRowOffset = 0x04;
        constexpr uintptr_t kPlayerCellWindowCellsOffset = 0x08;
        constexpr size_t kPlayerCellWindowColumns = 4;
        constexpr size_t kPlayerCellWindowRows = 10;
        constexpr size_t kPlayerCellWindowAtColumn = 2;
        constexpr size_t kPlayerCellWindowAtRow = 3;

        constexpr uintptr_t kPlayerPlaceFill = 0x0040D020;

        // Player entity global (also on session).
        constexpr uintptr_t kPlayerEntity = 0x006928F4;

        // Score on session; Score_Add is the only safe increment (float + extra life).
        constexpr uintptr_t kSessionScoreThisLevelOffset = 0xBE0;
        constexpr uintptr_t kSessionScoreAtLastLifeOffset = 0xBE4;

        constexpr uintptr_t kPlayerLives = 0x006934AC;

        // Set when death screen has taken him (timer or key).
        constexpr uintptr_t kPlayerTakenByDeathOffset = 0x594;

        // Per-state handlers from Player_Update switch on +0x178.
        constexpr uintptr_t kPlayerOnFootUpdate = 0x004428F0;

        // __cdecl (player, float* dirX, int* dirY), caller cleans 0xC. Player_Update reads the
        // input into two locals, hands them here to be squared with the window he is standing in,
        // and steps with what comes back. Everything the player does passes through it once.
        constexpr uintptr_t kPlayerAdjustMove = 0x00441ED0;
        constexpr uintptr_t kPlayerLadderUpdate = 0x00441AA0;
        constexpr uintptr_t kPlayerChainUpdate = 0x00442BD0;
        constexpr uintptr_t kPlayerPipeUpdate = 0x00442EB0;

        // Player flags (+0x590), not entity flags at +0x48C.
        constexpr uintptr_t kPlayerFlagsOffset = 0x590;
        constexpr uint32_t kPlayerFlagDead = 0x01; // input is ignored entirely
        constexpr uint32_t kPlayerFlagCrashed = 0x02; // fell to it; picks "crash" over "snd_die"

        constexpr uint32_t kPlayerFlagOnFire = 0x04;
        constexpr uint32_t kPlayerFlagShocked = 0x08;

        // Player_Die at entity +0x5C4 (set by Player_Spawn). Fire_TouchPlayer raises fire flag,
        // writes 1.0 into +0x25C, then calls it.
        constexpr float kPlayerBurnScale = 1.0f;

        // Inventory_IsCarrying/FindSlot/FindSlotIndexed: __cdecl; CMP *I,5 patches for slot count
        constexpr uintptr_t kInventoryIsCarrying = 0x004303D0;
        constexpr uintptr_t kInventoryFindSlot = 0x00430430;
        constexpr uintptr_t kInventoryFirstFree = 0x00430510;
        constexpr uintptr_t kInventoryTakeSlot = 0x00430550;
        constexpr uintptr_t kInventoryClearSlot = 0x00431130;

        constexpr uintptr_t kInventoryUpdate = 0x00431700;

        // Slot-count patches: CMP *I,5 at IsCarrying/FindSlot/FindSlotIndexed.

        constexpr size_t kInventorySlotsMax = 32;

        // Score_Add __thiscall (session, points, const float position[3]); null position = the player's.
        constexpr uintptr_t kScoreAdd = 0x00442620;

        constexpr uintptr_t kChainLength = 0x00693544;
        constexpr uintptr_t kChainTimer = 0x00693548;
        constexpr uintptr_t kChainPoints = 0x0069354C;
        constexpr uintptr_t kChainWindow = 0x00694664;

        // Chain message: __thiscall (messages, length).

        // Chain message table.
        constexpr uintptr_t kChainMessages = 0x00694650;
        constexpr uintptr_t kChainMessage = 0x0044BE60;

        // HUD; zeroes until Hud_Init.
        constexpr uintptr_t kHud = 0x006947D8;
        constexpr uintptr_t kHudInit = 0x0044E0F0;

        constexpr uintptr_t kHudPbarOffsOffset = 0x2C; // two floats
        constexpr uintptr_t kHudBonusPulseScaleOffset = 0x34;
        constexpr uintptr_t kHudLifePulseScaleOffset = 0x38;
        constexpr uintptr_t kHudLifePulseTimeOffset = 0x3C;
        constexpr uintptr_t kHudItemStartOffset = 0x44; // $items $start_pos
        constexpr uintptr_t kHudItemOffsOffset = 0x4C; // $items $offs, slot to slot
        constexpr uintptr_t kHudItemSizeOffset = 0x54; // $items $size
        constexpr uintptr_t kHudItemFrameOffset = 0x5C; // $item_frame, its texture
        constexpr uintptr_t kHudItemFrameSizeOffset = 0x6C; // and its size
        constexpr uintptr_t kHudPointsPosOffset = 0xC4;

        // Hud images: two heap pointers, refilled each level.
        constexpr size_t kHudSize = 0xD0;

        // Image record 0x28; Image_ParseRecord __cdecl; Images_Append __stdcall.
        constexpr uintptr_t kHudImagesOffset = 0x10; // $standard_level $images
        constexpr uintptr_t kHudBonusImagesOffset = 0x14; // $bonus_level $images
        constexpr uintptr_t kImageParseRecord = 0x00449950;
        constexpr uintptr_t kImagesAppend = 0x0044AE40;
        constexpr size_t kImageRecordSize = 0x28;
        constexpr uintptr_t kImagesArrayOffset = 0x00;
        constexpr uintptr_t kImagesCountOffset = 0x04;

    }
}
