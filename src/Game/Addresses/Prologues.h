#pragma once

#include <stdint.h>

namespace Forge
{
    // The four player state handlers, all __cdecl; the ladder takes only the entity.
    // Player_UpdateOnFoot: PUSH EBX / PUSH ESI / MOV ESI,[ESP+0xC] → 6
    constexpr uint8_t kPlayerOnFootUpdatePrologue[] = { 0x53, 0x56, 0x8B, 0x74, 0x24, 0x0C };

    // Player_UpdateLadder: FLDZ / PUSH EBX / PUSH ESI / FST [0x006934E4] → 10
    constexpr uint8_t kPlayerLadderUpdatePrologue[] =
    { 0xD9, 0xEE, 0x53, 0x56, 0xD9, 0x15, 0xE4, 0x34, 0x69, 0x00 };

    // Player_UpdateChain: FLDZ / PUSH EBX / PUSH EBP / FST [0x006934E4] → 10
    constexpr uint8_t kPlayerChainUpdatePrologue[] =
    { 0xD9, 0xEE, 0x53, 0x55, 0xD9, 0x15, 0xE4, 0x34, 0x69, 0x00 };

    // Player_UpdatePipe: MOV EAX,[ESP+8] / PUSH EBX → 5
    constexpr uint8_t kPlayerPipeUpdatePrologue[] = { 0x8B, 0x44, 0x24, 0x08, 0x53 };

    // Player_AdjustMove: MOV ECX,[ESP+4] / MOV EAX,[ECX+0x178] → 10
    constexpr uint8_t kPlayerAdjustMovePrologue[] =
    { 0x8B, 0x4C, 0x24, 0x04, 0x8B, 0x81, 0x78, 0x01, 0x00, 0x00 };

    // Package_OpenFile: PUSH ESI / MOV ESI,[ESP+8] → 5 bytes
    constexpr uint8_t kPackageOpenFilePrologue[] = { 0x56, 0x8B, 0x74, 0x24, 0x08 };

    // App_Shutdown: PUSH EBX / PUSH 0x004cccb0 → 6 bytes (immediate must stay intact)
    constexpr uint8_t kAppShutdownPrologue[] = { 0x53, 0x68, 0xB0, 0xCC, 0x4C, 0x00 };

    // Launcher_OnInitDialog: PUSH ESI / PUSH EDI / PUSH 3 / PUSH 0x004c7bdc → 9
    constexpr uint8_t kLauncherOnInitDialogPrologue[] =
    { 0x56, 0x57, 0x6A, 0x03, 0x68, 0xDC, 0x7B, 0x4C, 0x00 };
    // Launcher_OnCommand: PUSH -1 / PUSH 0x004c5548 → 7
    constexpr uint8_t kLauncherOnCommandPrologue[] =
    { 0x6A, 0xFF, 0x68, 0x48, 0x55, 0x4C, 0x00 };
    // Dialog_RunModal: MOV EAX,[ESP+0xC] / TEST EAX,EAX → 6
    constexpr uint8_t kDialogRunModalPrologue[] =
    { 0x8B, 0x44, 0x24, 0x0C, 0x85, 0xC0 };

    // Launcher_HideOptionalButtons: PUSH ESI / PUSH 0 / PUSH 0x3EC → 8
    constexpr uint8_t kLauncherHideOptionalButtonsPrologue[] =
    { 0x56, 0x6A, 0x00, 0x68, 0xEC, 0x03, 0x00, 0x00 };

    // Menu_Register: MOV EAX,[ESP+8] / PUSH EBX → 5
    constexpr uint8_t kMenuRegisterPrologue[] =
    { 0x8B, 0x44, 0x24, 0x08, 0x53 };
    // Menu_ParseScript: PUSH EBP / MOV EBP,ESP / PUSH -1 → 5
    constexpr uint8_t kMenuParseScriptPrologue[] =
    { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
    // Menu_LoadFromScript: PUSH ESI / PUSH EDI / MOV EDI,[ESP+0xC] → 6
    constexpr uint8_t kMenuLoadFromScriptPrologue[] =
    { 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C };
    // MenuItem_Parse: PUSH ESI / PUSH EDI / MOV EDI,[ESP+0xC] → 6
    constexpr uint8_t kMenuItemParsePrologue[] =
    { 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C };

    // Menu_TakeClickResult: MOV EDX,[ECX+0x5c] / MOV EAX,[ESP+0x4] → 7
    constexpr uint8_t kMenuTakeClickResultPrologue[] =
    { 0x8B, 0x51, 0x5C, 0x8B, 0x44, 0x24, 0x04 };

    // Game_FrameTick: SUB ESP,0x50 / PUSH ESI / PUSH EDI → 5
    constexpr uint8_t kGameFrameTickPrologue[] =
    { 0x83, 0xEC, 0x50, 0x56, 0x57 };

    // Render_Begin: PUSH ESI / MOV ESI,ECX / CMP dword [ESI+0x0C],0 → 7
    constexpr uint8_t kRenderBeginPrologue[] =
    { 0x56, 0x8B, 0xF1, 0x83, 0x7E, 0x0C, 0x00 };

    // Render_Present: PUSH ESI / MOV ESI,ECX / CMP byte [ESI+0x20],0 → 7
    constexpr uint8_t kRenderPresentPrologue[] =
    { 0x56, 0x8B, 0xF1, 0x80, 0x7E, 0x20, 0x00 };

    // MenuItem_Render: SUB ESP,0x10 / PUSH ESI / MOV ESI,ECX → 6
    constexpr uint8_t kMenuItemRenderPrologue[] =
    { 0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1 };

    // Menu_DrawDecorations: SUB ESP,0x0C / PUSH ESI / MOV ESI,ECX → 6
    constexpr uint8_t kMenuDrawDecorationsPrologue[] =
    { 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0xF1 };

    // Level_ActivateRoom: PUSH ESI / MOV ESI,ECX / MOV EAX,[ESI+0x934] → 9
    constexpr uint8_t kLevelActivateRoomPrologue[] =
    { 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x34, 0x09, 0x00, 0x00 };

    // Level_Load: PUSH -1 / PUSH 0x004c55bb → 7
    constexpr uint8_t kLevelLoadPrologue[] =
    { 0x6A, 0xFF, 0x68, 0xBB, 0x55, 0x4C, 0x00 };
    // Level_Free: PUSH ESI / PUSH EDI / PUSH 0x004c8b7c → 7
    constexpr uint8_t kLevelFreePrologue[] =
    { 0x56, 0x57, 0x68, 0x7C, 0x8B, 0x4C, 0x00 };

    // Bonuses_FreeResources: PUSH 0x4ce7a0 → 5
    constexpr uint8_t kBonusesFreeResourcesPrologue[] =
    { 0x68, 0xA0, 0xE7, 0x4C, 0x00 };

    // Bonus_OnTouch: PUSH ECX / PUSH EBX / PUSH EBP / MOV EBP,[ESP+0x10] → 7
    constexpr uint8_t kBonusOnTouchPrologue[] =
    { 0x51, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x10 };

    // Loading_SetProgress: PUSH ESI / MOV ESI,ECX / CMP [ESI],7 → 6
    constexpr uint8_t kLoadingSetProgressPrologue[] =
    { 0x56, 0x8B, 0xF1, 0x83, 0x3E, 0x07 };

    // Menu_RegisterAll: PUSH ESI / PUSH 0 / PUSH 0 → 5
    constexpr uint8_t kMenuRegisterAllPrologue[] =
    { 0x56, 0x6A, 0x00, 0x6A, 0x00 };

    // Monster_SpawnPlacement: PUSH EDI / MOV EDI,[ESP+8] → 5. __cdecl, RET with no operand.
    constexpr uint8_t kMonsterSpawnPlacementPrologue[] =
    { 0x57, 0x8B, 0x7C, 0x24, 0x08 };

    // Monster_Kill: SUB ESP,0xc / PUSH ESI / MOV ESI,[ESP+0x14] → 8. __cdecl.
    constexpr uint8_t kMonsterKillPrologue[] =
    { 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0x74, 0x24, 0x14 };

    // Entities_ParseFile: SUB ESP,0x44 / MOV EAX,[0x4fd650] → 8. __cdecl (room, cells, text).
    constexpr uint8_t kEntitiesParseFilePrologue[] =
    { 0x83, 0xEC, 0x44, 0xA1, 0x50, 0xD6, 0x4F, 0x00 };

    // Monster_ParseSpawn: SUB ESP,0x15c → 6, one instruction. __cdecl.
    constexpr uint8_t kMonsterParseSpawnPrologue[] =
    { 0x81, 0xEC, 0x5C, 0x01, 0x00, 0x00 };

    // LoadOrFindModel: PUSH EDI / MOV EDI,[ESP+8] → 5. __cdecl.
    constexpr uint8_t kLoadOrFindModelPrologue[] =
    { 0x57, 0x8B, 0x7C, 0x24, 0x08 };

    // Model_Release: PUSH ESI / MOV ESI,[ESP+8] → 5. __cdecl.
    constexpr uint8_t kModelReleasePrologue[] =
    { 0x56, 0x8B, 0x74, 0x24, 0x08 };

    // LoadOrFindTexture: PUSH ECX / PUSH EDI / MOV EDI,[ESP+0xc] → 6. __thiscall, RET 0xC.
    constexpr uint8_t kLoadOrFindTexturePrologue[] =
    { 0x51, 0x57, 0x8B, 0x7C, 0x24, 0x0C };

    // Model_DrawNode: MOV ECX,[0x00503590] → 6, one instruction. __cdecl.
    constexpr uint8_t kModelDrawNodePrologue[] =
    { 0x8B, 0x0D, 0x90, 0x35, 0x50, 0x00 };

    // Model_Render: MOV EAX,[ESP+4] / TEST EAX,EAX → 6. __cdecl.
    constexpr uint8_t kModelRenderPrologue[] =
    { 0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0 };

    // Model_RenderAnimated: PUSH EBX / MOV EBX,[ESP+8] → 5. __cdecl.
    constexpr uint8_t kModelRenderAnimatedPrologue[] =
    { 0x53, 0x8B, 0x5C, 0x24, 0x08 };

    // Scene_DrawModels: SUB ESP,0x8c → 6, one instruction. __thiscall, scene in ECX.
    constexpr uint8_t kSceneDrawModelsPrologue[] =
    { 0x81, 0xEC, 0x8C, 0x00, 0x00, 0x00 };

    // Scene_SortObjects: SUB ESP,0x3c / PUSH EBX / MOV EBX,ECX → 6. __thiscall, scene in ECX.
    constexpr uint8_t kSceneSortObjectsPrologue[] =
    { 0x83, 0xEC, 0x3C, 0x53, 0x8B, 0xD9 };

    // LoadOrFindAnim: PUSH ESI / MOV ESI,[ESP+8] → 5. __cdecl.
    constexpr uint8_t kLoadOrFindAnimPrologue[] =
    { 0x56, 0x8B, 0x74, 0x24, 0x08 };

    // Monsters_FreeResources: CMP [0x00691458],0 → 7
    constexpr uint8_t kMonstersFreeResourcesPrologue[] =
    { 0x83, 0x3D, 0x58, 0x14, 0x69, 0x00, 0x00 };

    // Camera_Update: FLDZ / SUB ESP,0x1C → 5
    constexpr uint8_t kCameraUpdatePrologue[] = { 0xD9, 0xEE, 0x83, 0xEC, 0x1C };

    // Camera_Place: MOV EAX,[ESP+4] / FLD [ECX+0x68] → 7
    constexpr uint8_t kCameraPlacePrologue[] = { 0x8B, 0x44, 0x24, 0x04, 0xD9, 0x41, 0x68 };

    // Matrix_Perspective: PUSH ECX / FLD [ESP+8] → 5 (the args stay ESP-relative)
    constexpr uint8_t kMatrixPerspectivePrologue[] = { 0x51, 0xD9, 0x44, 0x24, 0x08 };

    // Six touch handlers, __cdecl (monster, other, side).
    struct MonsterTouchSite
    {
        uintptr_t address;
        uint8_t prologue[8];
        size_t length;
    };

    constexpr MonsterTouchSite kMonsterTouchSites[] =
    {
        { 0x00445D90, { 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C },             6 }, // mummy, mummy2
        { 0x00446570, { 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C },             6 }, // bat
        { 0x004469F0, { 0x53, 0x56, 0x8B, 0x74, 0x24, 0x0C },             6 }, // j_skull
        { 0x00447790, { 0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10 },       7 }, // spider, scorpion
        { 0x00447CD0, { 0x53, 0x8B, 0x5C, 0x24, 0x08 },                   5 }, // the snakes
        { 0x00448270, { 0x53, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10 },       7 }, // skull
    };
}
