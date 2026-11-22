#pragma once

#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
        // Entities_ParseFile: __cdecl (room, room+0x104, text). One parser for all three placement files.
        constexpr uintptr_t kEntitiesParseFile = 0x0042C270;
        constexpr uintptr_t kLevelParseRoomEntry = 0x0040D6E0;

        // Parse routines per block name. Last three unused by shipped levels.
        constexpr uintptr_t kCtrlParseLadder = 0x00437250;
        constexpr uintptr_t kCtrlParseConveyer = 0x00436B50;
        constexpr uintptr_t kCtrlParseChain = 0x00436760;
        constexpr uintptr_t kCtrlParseDoor = 0x00435C10; // Door_ParseSpawn, takes the name too
        constexpr uintptr_t kCtrlParseDisappPlat = 0x00435550;
        constexpr uintptr_t kCtrlParsePipe = 0x00435130;
        constexpr uintptr_t kCtrlParseForceField = 0x004342B0;
        constexpr uintptr_t kCtrlParseFire = 0x00433C10;
        constexpr uintptr_t kCtrlParseLevelExit = 0x00433530;
        constexpr uintptr_t kCtrlParseRestorePlat = 0x00432F30;
        constexpr uintptr_t kCtrlParseLift = 0x004323B0;
        constexpr uintptr_t kCtrlParseNote = 0x0042F0E0;
        constexpr uintptr_t kCtrlParseAirwayDown = 0x0042E450; // unused by the levels
        constexpr uintptr_t kCtrlParseTeleport = 0x0042DA90; // unused by the levels
        constexpr uintptr_t kCtrlParseAirflow = 0x0042D670; // unused by the levels

        // Ctrl_Spawn jump table 0x0042C648, kinds 1..17. Numbers from the table, not write order.
        constexpr uintptr_t kCtrlSpawn = 0x0042C570;
        constexpr uintptr_t kCtrlSpawnConveyer = 0x00436E50; // 1
        constexpr uintptr_t kCtrlSpawnDoor = 0x004363D0; // 2
        constexpr uintptr_t kCtrlSpawnDisappPlat = 0x004358C0; // 3
        constexpr uintptr_t kCtrlSpawnForceField = 0x00434C60; // 4
        constexpr uintptr_t kCtrlSpawnFire = 0x004340F0; // 5
        constexpr uintptr_t kCtrlSpawnLevelExit = 0x00433A60; // 6
        constexpr uintptr_t kCtrlSpawnRestorePlat = 0x00433380; // 7
        constexpr uintptr_t kCtrlSpawnLift = 0x00432CF0; // 8
        constexpr uintptr_t kCtrlSpawnBonus = 0x00431320; // 9
        constexpr uintptr_t kCtrlSpawnMonster = 0x0042FD60; // 10
        constexpr uintptr_t kCtrlSpawnNote = 0x0042EDD0; // 11
        constexpr uintptr_t kCtrlSpawnPipeValve = 0x00435000; // 12
        constexpr uintptr_t kCtrlSpawnLadder = 0x00437130; // 13
        constexpr uintptr_t kCtrlSpawnChain = 0x00436640; // 14
        constexpr uintptr_t kCtrlSpawnAirwayDown = 0x0042E3B0; // 15
        constexpr uintptr_t kCtrlSpawnTeleport = 0x0042E140; // 16
        constexpr uintptr_t kCtrlSpawnAirflow = 0x0042D4F0; // 17

        // Tag + state; no registry, walk to find.
        constexpr uintptr_t kEntityTagOffset = 0x44;
        constexpr size_t kEntityTagSize = 0x40; // it ends where the state byte begins
        constexpr uintptr_t kEntityTagStateOffset = 0x84; // only on a tagged thing
        constexpr uintptr_t kEntitiesFindByTag = 0x00419370; // __cdecl (name)
        constexpr uintptr_t kTagsTest = 0x0042E930; // __cdecl (note record)

        constexpr uintptr_t kNoteConditionsOffset = 0x184;
        constexpr uintptr_t kNoteConditionCountOffset = 0x188;
        constexpr uintptr_t kNoteConditionModeOffset = 0x18C; // 1 all, 0 any
        constexpr size_t kNoteConditionStride = 0x102; // name[0x100], present, tag_on

        // Ctrl = entity kind 4; record at kEntitySpawnOffset, read every frame by think.
        constexpr int kEntityKindCtrl = 4;

        // Lift record, 100 bytes; entity name = block + _vert/_horz.
        constexpr int kCtrlRecordKindLift = 8;
        constexpr size_t kLiftRecordSize = 100;
        constexpr uintptr_t kLiftRecordKindOffset = 0x00;
        constexpr uintptr_t kLiftModelOffset = 0x04;
        constexpr uintptr_t kLiftTextureOffset = 0x08;
        constexpr uintptr_t kLiftMoveSpeedOffset = 0x0C; // $move_speed, default 0.5
        constexpr uintptr_t kLiftStopTimeOffset = 0x10; // $stop_time
        constexpr uintptr_t kLiftWaitTimeOffset = 0x14; // $wait_time
        constexpr uintptr_t kLiftDirOffset = 0x18; // $dir
        constexpr uintptr_t kLiftHorzOffset = 0x1C; // $horz, one byte
        constexpr uintptr_t kLiftAlightOffset = 0x1D; // $alight, one byte
        constexpr uintptr_t kLiftEndAOffset = 0x20; // three floats
        constexpr uintptr_t kLiftOriginOffset = 0x2C;
        constexpr uintptr_t kLiftEndBOffset = 0x38;
        constexpr uintptr_t kLiftWidthOffset = 0x44; // twice the model's own
        constexpr uintptr_t kLiftStateOffset = 0x4C; // written by the spawn and the think
        constexpr uintptr_t kLiftTimerOffset = 0x50;
        constexpr uintptr_t kLiftVoiceOffset = 0x54;
        constexpr uintptr_t kLiftSoundLoopOffset = 0x58;
        constexpr uintptr_t kLiftSoundStopOffset = 0x5C;
        constexpr uintptr_t kLiftSoundTouchOffset = 0x60;

        // restore_plat record, 0x44 bytes.
        constexpr int kCtrlRecordKindRestorePlat = 7;
        constexpr size_t kRestorePlatRecordSize = 0x44;
        constexpr uintptr_t kRestorePlatModelOffset = 0x04;
        constexpr uintptr_t kRestorePlatTextureOffset = 0x08;
        constexpr uintptr_t kRestorePlatPositionOffset = 0x0C; // three floats
        constexpr uintptr_t kRestorePlatAlightOffset = 0x18; // one byte
        constexpr uintptr_t kRestorePlatFallSpeedOffset = 0x1C;
        constexpr uintptr_t kRestorePlatRiseSpeedOffset = 0x20; // $elevate_speed
        constexpr uintptr_t kRestorePlatFallDistOffset = 0x24; // $fall_dist, already in cells
        constexpr uintptr_t kRestorePlatSoundDownOffset = 0x34;
        constexpr uintptr_t kRestorePlatSoundDownStopOffset = 0x38;
        constexpr uintptr_t kRestorePlatSoundUpOffset = 0x3C;
        constexpr uintptr_t kRestorePlatSoundUpStopOffset = 0x40;

        // conveyer record, 0x38 bytes; one per $length cell.
        constexpr int kCtrlRecordKindConveyer = 1;
        constexpr size_t kConveyerRecordSize = 0x38;
        constexpr uintptr_t kConveyerModelOffset = 0x04;
        constexpr uintptr_t kConveyerTextureOffset = 0x08;
        constexpr uintptr_t kConveyerCellSpeedOffset = 0x0C; // $speed divided by the cell size
        constexpr uintptr_t kConveyerSpeedOffset = 0x10; // $speed as written
        constexpr uintptr_t kConveyerForwardOffset = 0x14; // one byte, speed > 0
        constexpr uintptr_t kConveyerPositionOffset = 0x18; // three floats
        constexpr uintptr_t kConveyerAlightOffset = 0x34; // one byte

        // Kind 9 = bonus, 10 = monster (shared with AMOD_ENTITY_*).
        constexpr int kCtrlRecordKindDoor = 2;
        constexpr int kCtrlRecordKindDisappPlat = 3;
        constexpr int kCtrlRecordKindForceField = 4;
        constexpr int kCtrlRecordKindFire = 5;
        constexpr int kCtrlRecordKindLevelExit = 6;
        constexpr int kCtrlRecordKindNote = 11;
        constexpr int kCtrlRecordKindPipeValve = 12;

        // door, 0xC0; grid-placed; $tag is text.
        constexpr uintptr_t kDoorOpenSpeedOffset = 0x0C;
        constexpr uintptr_t kDoorCrackSpeedOffset = 0x10;
        constexpr uintptr_t kDoorLengthOffset = 0x14;
        constexpr uintptr_t kDoorNameOffset = 0x18; // 64 bytes: door_a, door_b, door_c
        constexpr uintptr_t kDoorPositionOffset = 0x58;
        constexpr uintptr_t kDoorSoundOffset = 0x64;
        constexpr uintptr_t kDoorSoundCrackOffset = 0x68;
        constexpr uintptr_t kDoorSoundCloseOffset = 0x6C;
        constexpr uintptr_t kDoorModelOffset = 0x70;
        constexpr uintptr_t kDoorTextureOffset = 0x74;
        constexpr uintptr_t kDoorAlightOffset = 0x78;
        constexpr uintptr_t kDoorTagOffset = 0x79; // 71 bytes

        // disapp_plat, 0x90
        constexpr uintptr_t kDisappPlatModelOffset = 0x04; // and a second one at +0x08
        constexpr uintptr_t kDisappPlatTextureOffset = 0x0C;
        constexpr uintptr_t kDisappPlatSoundOffset = 0x10;
        constexpr uintptr_t kDisappPlatPositionOffset = 0x14;
        constexpr uintptr_t kDisappPlatTexSpeedOffset = 0x2C;
        constexpr uintptr_t kDisappPlatShowTimeOffset = 0x30;
        constexpr uintptr_t kDisappPlatHideTimeOffset = 0x34;
        constexpr uintptr_t kDisappPlatWaitTimeOffset = 0x38;
        constexpr uintptr_t kDisappPlatLengthOffset = 0x3C;
        constexpr uintptr_t kDisappPlatShownOffset = 0x40; // one byte
        constexpr uintptr_t kDisappPlatTimerOffset = 0x44;
        constexpr uintptr_t kDisappPlatTagOffset = 0x48; // 68 bytes
        constexpr uintptr_t kDisappPlatFramesOffset = 0x8C; // the $frames list, not a tag

        // force_field, 0xC0; $shock_speed parsed but unused by levels.
        constexpr uintptr_t kForceFieldFramesOffset = 0x04; // the $frames list, not a tag
        constexpr uintptr_t kForceFieldSoundOffset = 0x0C;
        constexpr uintptr_t kForceFieldColorOffset = 0x10; // four floats
        constexpr uintptr_t kForceFieldLengthOffset = 0x20;
        constexpr uintptr_t kForceFieldVisTimeOffset = 0x24;
        constexpr uintptr_t kForceFieldInvisTimeOffset = 0x28;
        constexpr uintptr_t kForceFieldWaitTimeOffset = 0x2C;
        constexpr uintptr_t kForceFieldPositionOffset = 0x30;
        constexpr uintptr_t kForceFieldHorzOffset = 0x54; // one byte
        constexpr uintptr_t kForceFieldBoxScaleOffset = 0x58;
        constexpr uintptr_t kForceFieldFlashTimeOffset = 0x5C;
        constexpr uintptr_t kForceFieldFlashCountOffset = 0x60;
        constexpr uintptr_t kForceFieldFlashWidthOffset = 0x64;
        constexpr uintptr_t kForceFieldFlashFpsOffset = 0x68;
        constexpr uintptr_t kForceFieldShockSpeedOffset = 0x6C;
        constexpr uintptr_t kForceFieldTagOffset = 0x70; // 80 bytes
        constexpr uintptr_t kForceFieldFlashTimerOffset = 0xB0;
        constexpr uintptr_t kForceFieldTimerOffset = 0xB4;
        constexpr uintptr_t kForceFieldOnOffset = 0xB8;   // one byte; the think copies it to entity+0x5B4
        constexpr uintptr_t kForceFieldVoiceOffset = 0xBC;

        // fire, 0x50; grid-placed on 'F'.
        constexpr uintptr_t kFireTextureOffset = 0x04;
        constexpr uintptr_t kFireColorOffset = 0x08; // four floats
        constexpr uintptr_t kFirePartSpeedOffset = 0x18;
        constexpr uintptr_t kFirePartSizeOffset = 0x1C;
        constexpr uintptr_t kFireDisperseOffset = 0x20;
        constexpr uintptr_t kFireBoxScaleOffset = 0x24;
        constexpr uintptr_t kFireMaxPartsOffset = 0x28;
        constexpr uintptr_t kFireHeightOffset = 0x2C;
        constexpr uintptr_t kFirePositionOffset = 0x34;

        // level_exit, 0x7C
        constexpr uintptr_t kLevelExitPartTexOffset = 0x04;
        constexpr uintptr_t kLevelExitPartSizeOffset = 0x08;
        constexpr uintptr_t kLevelExitMaxPartsOffset = 0x0C;
        constexpr uintptr_t kLevelExitPartSpeedOffset = 0x10;
        constexpr uintptr_t kLevelExitColorTopOffset = 0x14;
        constexpr uintptr_t kLevelExitColorBottomOffset = 0x24;
        constexpr uintptr_t kLevelExitSpawnRotOffset = 0x34;
        constexpr uintptr_t kLevelExitAlightOffset = 0x38;
        constexpr uintptr_t kLevelExitPositionOffset = 0x40;

        // note, 0x198; only ctrl with its own text.
        constexpr uintptr_t kNoteActiveOffset = 0x04; // one byte
        constexpr uintptr_t kNoteRectOffset = 0x0C; // two corners, six floats
        constexpr uintptr_t kNoteHasHideRectOffset = 0x24; // one byte
        constexpr uintptr_t kNoteTextOffset = 0x44; // 256 bytes
        constexpr uintptr_t kNoteScreenPosOffset = 0x144;
        constexpr uintptr_t kNoteScreenSizeOffset = 0x14C;
        constexpr uintptr_t kNoteHintTimeOffset = 0x154; // $t_hint
        constexpr uintptr_t kNoteHideTimeOffset = 0x158; // $t_hide
        constexpr uintptr_t kNoteHasArrowOffset = 0x15C; // one byte
        constexpr uintptr_t kNoteConditionOffset = 0x18C;
        constexpr uintptr_t kNoteTimeDelayOffset = 0x194;

        // pipe, 0x20; only 't' (valve) becomes a record; rest is geometry -> '|'.
        constexpr uintptr_t kPipeValveModelsOffset = 0x04;
        constexpr uintptr_t kPipeValveModelCountOffset = 0x08;
        constexpr uintptr_t kPipeValvePositionOffset = 0x0C;
        constexpr uintptr_t kPipeValveLengthOffset = 0x18;
        constexpr uintptr_t kPipeValveAlightOffset = 0x1C;

        // ladder 13 / chain 14: run is geometry; marked cell = record.
        // Grid normalised: '='->'-', 'i'->'I', 't'->'|'. Chain writes 'P' into cross cells.
        // Entity is a hidden marker; models are room decoration from the parse.
        constexpr int kCtrlRecordKindLadder = 13;
        constexpr int kCtrlRecordKindChain = 14;
        constexpr uintptr_t kRunPositionOffset = 0x04; // three floats, same in 12, 13 and 14
        constexpr uintptr_t kRunLengthOffset = 0x10;
        constexpr uintptr_t kLadderModelsOffset = 0x18;
        constexpr uintptr_t kLadderModelCountOffset = 0x1C;
        constexpr uintptr_t kChainAlightOffset = 0x14;
        constexpr uintptr_t kChainModelsOffset = 0x18;
        constexpr uintptr_t kChainModelCountOffset = 0x1C;

        // Unused by levels; records allocated directly (not Mem_Clone).

        // airway_down, 0xA0, kind 15; grid 'Y' (no room uses it).
        constexpr int kCtrlRecordKindAirwayDown = 15;
        constexpr char kRoomCellAirwayDown = 'Y';
        constexpr uintptr_t kAirwayDownPositionOffset = 0x04;
        constexpr uintptr_t kAirwayDownLengthOffset = 0x10;
        constexpr uintptr_t kAirwayDownTopOffset = 0x14;
        constexpr uintptr_t kAirwayDownBottomOffset = 0x20;

        // teleport, 0x108, kind 16; $pos (not grid-placed).
        constexpr int kCtrlRecordKindTeleport = 16;
        constexpr uintptr_t kTeleportNameOffset = 0x04; // 64 bytes, "_noname_" by default
        constexpr uintptr_t kTeleportTargetOffset = 0x44; // 64 bytes
        constexpr uintptr_t kTeleportBoxOffset = 0x90;
        constexpr uintptr_t kTeleportModelOffset = 0xAC;
        constexpr uintptr_t kTeleportTextureOffset = 0xB0;
        constexpr uintptr_t kTeleportTimeStartOffset = 0xB4;
        constexpr uintptr_t kTeleportTimeWorkOffset = 0xB8;
        constexpr uintptr_t kTeleportTimePauseOffset = 0xBC;
        constexpr uintptr_t kTeleportPositionOffset = 0xC8;
        constexpr uintptr_t kTeleportSoundOffset = 0xE8;
        constexpr uintptr_t kTeleportSoundInOutOffset = 0xEC;
        constexpr uintptr_t kTeleportPartsPosOffset = 0xF0;
        constexpr uintptr_t kTeleportPartsOffset = 0x100;
        constexpr uintptr_t kTeleportPartsInOutOffset = 0x104;

        // airflow, 0x13C, kind 17; $height/$speed + stage ring + eject lists.
        constexpr int kCtrlRecordKindAirflow = 17;
        constexpr uintptr_t kAirflowPositionOffset = 0x04;
        constexpr uintptr_t kAirflowBoxOffset = 0x10;
        constexpr uintptr_t kAirflowEjectUpOffset = 0x2C;
        constexpr uintptr_t kAirflowEjectUpCountOffset = 0x30;
        constexpr uintptr_t kAirflowEjectDownOffset = 0x34;
        constexpr uintptr_t kAirflowEjectDownCountOffset = 0x38;
        constexpr uintptr_t kAirflowHeightOffset = 0x3C; // $height plus the box top
        constexpr uintptr_t kAirflowSpeedOffset = 0x40;
        constexpr uintptr_t kAirflowTimeStartOffset = 0x44;
        constexpr uintptr_t kAirflowTimeUpOffset = 0x48;
        constexpr uintptr_t kAirflowTimeUpPauseOffset = 0x5C;
        constexpr uintptr_t kAirflowTimeDownOffset = 0x70;
        constexpr uintptr_t kAirflowTimeDownPauseOffset = 0x84;

        // Block-header names in _ctrl.txt.
        constexpr uintptr_t kCtrlNameConveyer = 0x004CE124;
        constexpr uintptr_t kCtrlNameDisappPlat = 0x004CE130;
        constexpr uintptr_t kCtrlNameForceField = 0x004CE13C;
        constexpr uintptr_t kCtrlNameLevelExit = 0x004CE148;
        constexpr uintptr_t kCtrlNameRestorePlat = 0x004CE154;
        constexpr uintptr_t kCtrlNameLift = 0x004CE164;
        constexpr uintptr_t kCtrlNameNote = 0x004CE16C;
        constexpr uintptr_t kCtrlNameAirwayDown = 0x004CE174;
        constexpr uintptr_t kCtrlNameTeleport = 0x004CE180;
        constexpr uintptr_t kCtrlNameAirflow = 0x004CE268;
        constexpr uintptr_t kCtrlNamePipe = 0x004C9E88;
        constexpr uintptr_t kCtrlNameChain = 0x004C9E90;
        constexpr uintptr_t kCtrlNameLadder = 0x004C9EA8;
        constexpr uintptr_t kCtrlNameFire = 0x004C9E74;
        constexpr uintptr_t kCtrlNameWall = 0x004C9EB0;
    }
}
