#include <windows.h>

#include "src/Api/CtrlsApi.h"
#include "src/Api/ApiProperty.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Api/FrameApi.h"
#include "src/Api/MmdApi.h"
#include "src/Api/TextureApi.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Hook/InlineHook.h"

#include <string>

#include <stddef.h>
#include <string.h>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;
        using ApiProperty::Field;
        using ApiProperty::Kind;

        AmodCtrlsApi g_table = {};

        // Same grid as SDK ColumnAt/RowAt.
        constexpr float kCellSize = 0.1f;
        constexpr float kOriginX = -2.0f;
        constexpr float kOriginY = 1.45f;

        const Field kLiftFields[] = {
            { "move_speed", GameAddress::kLiftMoveSpeedOffset, Kind::Float, 0, 0, 0, true },
            { "stop_time",  GameAddress::kLiftStopTimeOffset,  Kind::Float, 0, 0, 0, true },
            { "wait_time",  GameAddress::kLiftWaitTimeOffset,  Kind::Float, 0, 0, 0, true },
            { "dir",        GameAddress::kLiftDirOffset,       Kind::Float, 0, 0, 0, true },
            { "horz",       GameAddress::kLiftHorzOffset,      Kind::Int,   0, 0, 0, false },
            { "state",      GameAddress::kLiftStateOffset,     Kind::Int,   0, 0, 0, true },
            { "timer",      GameAddress::kLiftTimerOffset,     Kind::Float, 0, 0, 0, true },
        };

        const Field kRestorePlatFields[] = {
            { "fall_speed",    GameAddress::kRestorePlatFallSpeedOffset, Kind::Float, 0, 0, 0, true },
            { "elevate_speed", GameAddress::kRestorePlatRiseSpeedOffset, Kind::Float, 0, 0, 0, true },
            { "fall_dist",     GameAddress::kRestorePlatFallDistOffset,  Kind::Float, 0, 0, 0, true },
        };

        const Field kConveyerFields[] = {
            { "speed",      GameAddress::kConveyerSpeedOffset,     Kind::Float, 0, 0, 0, true },
            { "cell_speed", GameAddress::kConveyerCellSpeedOffset, Kind::Float, 0, 0, 0, true },
            { "forward",    GameAddress::kConveyerForwardOffset,   Kind::Int,   0, 0, 0, true },
        };

        const Field kDoorFields[] = {
            // Door open state; update reads each frame: 0 shut, 1 opening.
            { "state",       0x04,                              Kind::Int,   0, 0, 0, true },
            { "open_speed",  GameAddress::kDoorOpenSpeedOffset,  Kind::Float, 0, 0, 0, true },
            { "crack_speed", GameAddress::kDoorCrackSpeedOffset, Kind::Float, 0, 0, 0, true },
            { "length",      GameAddress::kDoorLengthOffset,     Kind::Int,   0, 0, 0, false },
            { "door",        GameAddress::kDoorNameOffset,       Kind::Text,  0, 0, 0, false },
            { "tag",         GameAddress::kDoorTagOffset,        Kind::Text,  0, 0, 0, false },
            { "alight",      GameAddress::kDoorAlightOffset,     Kind::Int,   0, 0, 0, true },
        };

        const Field kDisappPlatFields[] = {
            { "tex_speed", GameAddress::kDisappPlatTexSpeedOffset, Kind::Float, 0, 0, 0, true },
            { "show_time", GameAddress::kDisappPlatShowTimeOffset, Kind::Float, 0, 0, 0, true },
            { "hide_time", GameAddress::kDisappPlatHideTimeOffset, Kind::Float, 0, 0, 0, true },
            { "wait_time", GameAddress::kDisappPlatWaitTimeOffset, Kind::Float, 0, 0, 0, true },
            { "length",    GameAddress::kDisappPlatLengthOffset,   Kind::Float, 0, 0, 0, false },
            { "shown",     GameAddress::kDisappPlatShownOffset,    Kind::Int,   0, 0, 0, true },
            { "timer",     GameAddress::kDisappPlatTimerOffset,    Kind::Float, 0, 0, 0, true },
            { "tag",       GameAddress::kDisappPlatTagOffset,      Kind::Text,  0, 0, 0, false },
        };

        const Field kForceFieldFields[] = {
            { "color",       GameAddress::kForceFieldColorOffset,      Kind::Floats, 4, 0, 0, true },
            { "vis_time",    GameAddress::kForceFieldVisTimeOffset,    Kind::Float,  0, 0, 0, true },
            { "invis_time",  GameAddress::kForceFieldInvisTimeOffset,  Kind::Float,  0, 0, 0, true },
            { "wait_time",   GameAddress::kForceFieldWaitTimeOffset,   Kind::Float,  0, 0, 0, true },
            { "horz",        GameAddress::kForceFieldHorzOffset,       Kind::Int,    0, 0, 0, false },
            { "bbox_scale",  GameAddress::kForceFieldBoxScaleOffset,   Kind::Float,  0, 0, 0, true },
            { "flash_time",  GameAddress::kForceFieldFlashTimeOffset,  Kind::Float,  0, 0, 0, true },
            { "flash_count", GameAddress::kForceFieldFlashCountOffset, Kind::Int,    0, 0, 0, true },
            { "flash_width", GameAddress::kForceFieldFlashWidthOffset, Kind::Float,  0, 0, 0, true },
            { "flash_fps",   GameAddress::kForceFieldFlashFpsOffset,   Kind::Float,  0, 0, 0, true },
            { "shock_speed", GameAddress::kForceFieldShockSpeedOffset, Kind::Float,  0, 0, 0, true },
            { "tag",         GameAddress::kForceFieldTagOffset,        Kind::Text,   0, 0, 0, false },
        };

        const Field kFireFields[] = {
            { "color",      GameAddress::kFireColorOffset,     Kind::Floats, 4, 0, 0, true },
            { "part_speed", GameAddress::kFirePartSpeedOffset, Kind::Float,  0, 0, 0, true },
            { "part_size",  GameAddress::kFirePartSizeOffset,  Kind::Float,  0, 0, 0, true },
            { "disperse",   GameAddress::kFireDisperseOffset,  Kind::Float,  0, 0, 0, true },
            { "bbox_scale", GameAddress::kFireBoxScaleOffset,  Kind::Float,  0, 0, 0, true },
            { "max_parts",  GameAddress::kFireMaxPartsOffset,  Kind::Int,    0, 0, 0, true },
            { "height",     GameAddress::kFireHeightOffset,    Kind::Float,  0, 0, 0, false },
        };

        const Field kLevelExitFields[] = {
            { "part_size",    GameAddress::kLevelExitPartSizeOffset,    Kind::Float,  0, 0, 0, true },
            { "max_parts",    GameAddress::kLevelExitMaxPartsOffset,    Kind::Int,    0, 0, 0, true },
            { "part_speed",   GameAddress::kLevelExitPartSpeedOffset,   Kind::Float,  0, 0, 0, true },
            { "color_top",    GameAddress::kLevelExitColorTopOffset,    Kind::Floats, 4, 0, 0, true },
            { "color_bottom", GameAddress::kLevelExitColorBottomOffset, Kind::Floats, 4, 0, 0, true },
            { "spawn_rot",    GameAddress::kLevelExitSpawnRotOffset,    Kind::Float,  0, 0, 0, true },
            { "alight",       GameAddress::kLevelExitAlightOffset,      Kind::Int,    0, 0, 0, true },
        };

        const Field kNoteFields[] = {
            { "active",     GameAddress::kNoteActiveOffset,     Kind::Int,    0, 0, 0, true },
            { "text",       GameAddress::kNoteTextOffset,       Kind::Text,   0, 0, 0, true },
            { "scr_pos",    GameAddress::kNoteScreenPosOffset,  Kind::Floats, 2, 0, 0, true },
            { "scr_size",   GameAddress::kNoteScreenSizeOffset, Kind::Floats, 2, 0, 0, true },
            { "t_hint",     GameAddress::kNoteHintTimeOffset,   Kind::Float,  0, 0, 0, true },
            { "t_hide",     GameAddress::kNoteHideTimeOffset,   Kind::Float,  0, 0, 0, true },
            { "time_delay", GameAddress::kNoteTimeDelayOffset,  Kind::Float,  0, 0, 0, true },
            { "condition",  GameAddress::kNoteConditionOffset,  Kind::Int,    0, 0, 0, true },
        };

        const Field kPipeValveFields[] = {
            { "length", GameAddress::kPipeValveLengthOffset, Kind::Int, 0, 0, 0, false },
            { "alight", GameAddress::kPipeValveAlightOffset, Kind::Int, 0, 0, 0, true },
        };

        const Field kLadderFields[] = {
            { "length", GameAddress::kRunLengthOffset,        Kind::Int, 0, 0, 0, false },
            { "pieces", GameAddress::kLadderModelCountOffset, Kind::Int, 0, 0, 0, false },
        };

        const Field kChainFields[] = {
            { "length", GameAddress::kRunLengthOffset,       Kind::Int, 0, 0, 0, false },
            { "pieces", GameAddress::kChainModelCountOffset, Kind::Int, 0, 0, 0, false },
            { "alight", GameAddress::kChainAlightOffset,     Kind::Int, 0, 0, 0, true },
        };

        const Field kAirwayDownFields[] = {
            { "length", GameAddress::kAirwayDownLengthOffset, Kind::Int, 0, 0, 0, false },
        };

        const Field kTeleportFields[] = {
            { "name",       GameAddress::kTeleportNameOffset,      Kind::Text,  0, 0, 0, false },
            { "target",     GameAddress::kTeleportTargetOffset,    Kind::Text,  0, 0, 0, true },
            { "time_start", GameAddress::kTeleportTimeStartOffset, Kind::Float, 0, 0, 0, true },
            { "time_work",  GameAddress::kTeleportTimeWorkOffset,  Kind::Float, 0, 0, 0, true },
            { "time_pause", GameAddress::kTeleportTimePauseOffset, Kind::Float, 0, 0, 0, true },
        };

        const Field kAirflowFields[] = {
            { "height",          GameAddress::kAirflowHeightOffset,        Kind::Float, 0, 0, 0, true },
            { "speed",           GameAddress::kAirflowSpeedOffset,         Kind::Float, 0, 0, 0, true },
            { "time_start",      GameAddress::kAirflowTimeStartOffset,     Kind::Float, 0, 0, 0, true },
            { "time_up",         GameAddress::kAirflowTimeUpOffset,        Kind::Float, 0, 0, 0, true },
            { "time_up_pause",   GameAddress::kAirflowTimeUpPauseOffset,   Kind::Float, 0, 0, 0, true },
            { "time_down",       GameAddress::kAirflowTimeDownOffset,      Kind::Float, 0, 0, 0, true },
            { "time_down_pause", GameAddress::kAirflowTimeDownPauseOffset, Kind::Float, 0, 0, 0, true },
            { "eject_up",        GameAddress::kAirflowEjectUpCountOffset,  Kind::Int,   0, 0, 0, false },
            { "eject_down",      GameAddress::kAirflowEjectDownCountOffset, Kind::Int,  0, 0, 0, false },
        };

        struct Schema
        {
            int recordKind;
            const Field* fields;
            size_t count;
        };

#define AMOD_CTRL_SCHEMA(kind, table) { kind, table, sizeof(table) / sizeof(table[0]) }

        const Schema kSchemas[] = {
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindConveyer,    kConveyerFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindDoor,        kDoorFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindDisappPlat,  kDisappPlatFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindForceField,  kForceFieldFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindFire,        kFireFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindLevelExit,   kLevelExitFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindRestorePlat, kRestorePlatFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindLift,        kLiftFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindNote,        kNoteFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindPipeValve,   kPipeValveFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindLadder,      kLadderFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindChain,       kChainFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindAirwayDown,  kAirwayDownFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindTeleport,    kTeleportFields),
            AMOD_CTRL_SCHEMA(GameAddress::kCtrlRecordKindAirflow,     kAirflowFields),
        };

#undef AMOD_CTRL_SCHEMA

        // Placement list also holds bonus/monster; count only ctrl kinds here.
        bool IsCtrlKind(int kind)
        {
            return kind >= 1 && kind <= 17 &&
                kind != GameAddress::kSpawnKindBonus && kind != GameAddress::kSpawnKindMonster;
        }

        bool IsCtrl(const uint8_t* entity)
        {
            return entity != nullptr && Readable(entity, GameAddress::kEntitySize) &&
                Read<int>(entity, GameAddress::kEntityKindOffset) == GameAddress::kEntityKindCtrl;
        }

        // Live-list order; skip non-ctrls.
        uint8_t* CtrlAt(int index)
        {
            int at = 0;
            for (uint8_t* entity = WorldApi::FirstLiveEntity();
                entity != nullptr && at <= index;
                entity = WorldApi::NextLiveEntity(entity))
            {
                if (!IsCtrl(entity))
                {
                    continue;
                }
                if (at == index)
                {
                    return entity;
                }
                ++at;
            }
            return nullptr;
        }

        uint8_t* RecordOf(const uint8_t* entity)
        {
            uint8_t* const record = entity != nullptr
                ? Read<uint8_t*>(entity, GameAddress::kEntitySpawnOffset) : nullptr;
            return (record != nullptr && Readable(record, sizeof(int))) ? record : nullptr;
        }

        const Schema* SchemaOf(const uint8_t* record)
        {
            if (record == nullptr)
            {
                return nullptr;
            }

            const int kind = Read<int>(record, 0);
            for (const Schema& schema : kSchemas)
            {
                if (schema.recordKind == kind)
                {
                    return &schema;
                }
            }
            return nullptr;
        }

        int FloorToInt(float value)
        {
            const int truncated = static_cast<int>(value);
            return (value < 0.0f && static_cast<float>(truncated) != value) ? truncated - 1 : truncated;
        }

        int EntityIndexOf(const uint8_t* wanted)
        {
            int at = 0;
            for (uint8_t* entity = WorldApi::FirstLiveEntity();
                entity != nullptr && at < static_cast<int>(GameAddress::kEntityPoolCount);
                entity = WorldApi::NextLiveEntity(entity), ++at)
            {
                if (entity == wanted)
                {
                    return at;
                }
            }
            return -1;
        }

        typedef void(__cdecl* CtrlThinkRoutine)(void* entity);
        typedef void(__cdecl* CtrlTouchRoutine)(void* entity, void* other, int side);

        struct Driving
        {
            ModContext* owner;
            uint8_t* entity; // claim dies with this entity
            AmodCtrlDriver driver;
            AmodCtrlUpdateHandler handler;
            void* user;
            AmodCtrlTouchHandler touchHandler;
            void* touchUser;
            CtrlThinkRoutine gameThink;
            CtrlTouchRoutine gameTouch;
            const uint8_t* record; // identity; entity is only the current seat
            uint32_t token;
        };

        std::vector<Driving> g_driving;

        Driving* ClaimOn(const uint8_t* entity)
        {
            for (Driving& claim : g_driving)
            {
                if (claim.entity == entity)
                {
                    return &claim;
                }
            }
            return nullptr;
        }

        int IndexOfCtrl(const uint8_t* wanted)
        {
            int at = 0;
            for (uint8_t* entity = WorldApi::FirstLiveEntity();
                entity != nullptr;
                entity = WorldApi::NextLiveEntity(entity))
            {
                if (!IsCtrl(entity))
                {
                    continue;
                }
                if (entity == wanted)
                {
                    return at;
                }
                ++at;
            }
            return -1;
        }

        void __cdecl CtrlThinkThunk(void* self)
        {
            uint8_t* const entity = static_cast<uint8_t*>(self);
            const Driving* const found = ClaimOn(entity);

            if (found == nullptr)
            {
                return;
            }

            const Driving claim = *found;

            if (claim.driver == AMOD_CTRL_DRIVER_GAME || claim.driver == AMOD_CTRL_DRIVER_BOTH)
            {
                if (claim.gameThink != nullptr)
                {
                    claim.gameThink(self);
                }
            }

            if (claim.handler == nullptr ||
                (claim.driver != AMOD_CTRL_DRIVER_MOD && claim.driver != AMOD_CTRL_DRIVER_BOTH))
            {
                return;
            }

            AmodCtrlFrame frame = {};
            frame.structSize = sizeof(frame);
            frame.index = IndexOfCtrl(entity);
            frame.name = ReadText(entity, GameAddress::kEntityNameOffset);
            frame.delta = Read<float>(entity, GameAddress::kEntityDeltaOffset);

            for (int axis = 0; axis < 3; ++axis)
            {
                frame.position[axis] = Read<float>(entity, GameAddress::kEntityPositionOffset +
                    static_cast<uintptr_t>(axis) * sizeof(float));
            }
            claim.handler(&frame, claim.user);
        }

        void __cdecl CtrlTouchThunk(void* self, void* other, int side)
        {
            uint8_t* const entity = static_cast<uint8_t*>(self);
            const Driving* const found = ClaimOn(entity);

            if (found == nullptr)
            {
                return;
            }

            // Copy claim; handler may Drive/Release mid-call.
            const Driving claim = *found;
            AmodCtrlTouchResult verdict = AMOD_CTRL_TOUCH_DEFAULT;

            if (claim.touchHandler != nullptr)
            {
                const uint8_t* const toucher = static_cast<const uint8_t*>(other);
                const bool known = toucher != nullptr && Readable(toucher, GameAddress::kEntitySize);

                AmodCtrlTouchInfo info = {};
                info.structSize = sizeof(info);
                info.index = IndexOfCtrl(entity);
                info.name = ReadText(entity, GameAddress::kEntityNameOffset);
                info.toucherKind = known ? Read<int>(toucher, GameAddress::kEntityKindOffset) : 0;
                info.isPlayer = info.toucherKind == GameAddress::kEntityKindPlayer ? 1 : 0;
                info.toucherName = known ? ReadText(toucher, GameAddress::kEntityNameOffset) : "";
                info.toucher = known ? EntityIndexOf(toucher) : -1;
                info.side = side;

                for (int axis = 0; axis < 3; ++axis)
                {
                    info.position[axis] = Read<float>(entity, GameAddress::kEntityPositionOffset +
                        static_cast<uintptr_t>(axis) * sizeof(float));
                }
                verdict = claim.touchHandler(&info, claim.touchUser);
            }

            if (verdict != AMOD_CTRL_TOUCH_HANDLED && claim.gameTouch != nullptr)
            {
                claim.gameTouch(self, other, side);
            }
        }

        void GiveSlotBack(Driving& claim)
        {
            if (claim.entity == nullptr || !IsCtrl(claim.entity))
            {
                return;
            }

            if (Read<CtrlThinkRoutine>(claim.entity, GameAddress::kEntityThinkOffset) == &CtrlThinkThunk)
            {
                const CtrlThinkRoutine back = claim.gameThink;
                memcpy(claim.entity + GameAddress::kEntityThinkOffset, &back, sizeof(back));
            }
            if (Read<CtrlTouchRoutine>(claim.entity, GameAddress::kEntityTouchHandlerOffset) == &CtrlTouchThunk)
            {
                const CtrlTouchRoutine back = claim.gameTouch;
                memcpy(claim.entity + GameAddress::kEntityTouchHandlerOffset, &back, sizeof(back));
            }
        }

        void ReleaseDriving(void* payload)
        {
            const uint8_t* const entity = static_cast<const uint8_t*>(payload);

            for (auto it = g_driving.begin(); it != g_driving.end(); ++it)
            {
                if (it->entity == entity)
                {
                    GiveSlotBack(*it);
                    g_driving.erase(it);
                    return;
                }
            }
        }

        bool StillTheSameThing(const Driving& claim)
        {
            return claim.entity != nullptr && IndexOfCtrl(claim.entity) >= 0 &&
                RecordOf(claim.entity) == claim.record;
        }

        // Id is on the record block, not the entity; survives room change.
        struct Known
        {
            AmodCtrlId id;
            uint8_t* room; // room list; detects stale ids
            const void* record;
        };

        std::vector<Known> g_known;

        // Ids from 1; level order; retired ids not reused.
        AmodCtrlId g_nextCtrlId = 1;

        void Remember(uint8_t* room, const void* record, AmodCtrlId id)
        {
            const Known fresh = { id, room, record };
            g_known.push_back(fresh);
        }

        bool RoomStillHolds(uint8_t* room, const void* record)
        {
            const int count = room != nullptr
                ? Read<int>(room, GameAddress::kRoomRecordCountOffset) : 0;
            uint8_t* const* const records = room != nullptr
                ? Read<uint8_t* const*>(room, GameAddress::kRoomRecordsOffset) : nullptr;

            for (int at = 0; records != nullptr && at < count; ++at)
            {
                if (records[at] == record)
                {
                    return true;
                }
            }
            return false;
        }

        AmodCtrlId IdForRecord(uint8_t* room, const void* record)
        {
            if (room == nullptr || record == nullptr)
            {
                return AMOD_CTRL_NONE;
            }

            for (const Known& known : g_known)
            {
                if (known.record == record && known.room == room)
                {
                    return known.id;
                }
            }

            Remember(room, record, g_nextCtrlId);
            return g_nextCtrlId++;
        }

        uint32_t g_knownGeneration = 0;

        void SyncIds()
        {
            const uint32_t generation = WorldApi::LevelGeneration();
            if (generation == g_knownGeneration)
            {
                return;
            }

            // Drop known entries from a level that was unloaded.
            g_known.clear();
            g_nextCtrlId = 1;
            g_knownGeneration = generation;

            const int rooms = WorldApi::RoomTotal();
            for (int at = 0; at < rooms; ++at)
            {
                uint8_t* const room = WorldApi::RoomAt(at);
                const int held = room != nullptr
                    ? Read<int>(room, GameAddress::kRoomRecordCountOffset) : 0;
                uint8_t* const* const records = room != nullptr
                    ? Read<uint8_t* const*>(room, GameAddress::kRoomRecordsOffset) : nullptr;

                for (int which = 0; records != nullptr && which < held; ++which)
                {
                    if (IsCtrlKind(Read<int>(records[which], GameAddress::kSpawnKindOffset)))
                    {
                        Remember(room, records[which], g_nextCtrlId++);
                    }
                }
            }
        }

        // Ids never reused; kept id means that thing or nothing.
        const Known* KnownById(AmodCtrlId id)
        {
            SyncIds();

            for (auto it = g_known.begin(); it != g_known.end(); ++it)
            {
                if (it->id != id)
                {
                    continue;
                }
                if (RoomStillHolds(it->room, it->record))
                {
                    return &*it;
                }
                g_known.erase(it);
                return nullptr;
            }
            return nullptr;
        }

        void ForgetIdsOf(const void* record)
        {
            for (auto it = g_known.begin(); it != g_known.end();)
            {
                it = it->record == record ? g_known.erase(it) : it + 1;
            }
        }

        typedef void(__cdecl* RecordFreeRoutine)(void* record);

        struct Doomed
        {
            const uint8_t* entity;
            void* record;
        };

        std::vector<Doomed> g_doomed;

        void LetGoOfSpentRecords()
        {
            const RecordFreeRoutine release = [] {
                void** const slot = static_cast<void**>(GameModule::Resolve(GameAddress::kRecordFreeSlot));
                return slot != nullptr ? reinterpret_cast<RecordFreeRoutine>(*slot) : nullptr;
                }();

            for (auto it = g_doomed.begin(); it != g_doomed.end();)
            {
                const bool stillReferenced = IndexOfCtrl(it->entity) >= 0 &&
                    RecordOf(it->entity) == it->record;

                if (stillReferenced)
                {
                    ++it;
                    continue;
                }

                ForgetIdsOf(it->record);

                if (release != nullptr)
                {
                    release(it->record);
                }
                it = g_doomed.erase(it);
            }
        }

        void TakeSlots(Driving& claim);
        void HoldSpawnSlot();

        void __cdecl DropDeadClaims()
        {
            HoldSpawnSlot();
            LetGoOfSpentRecords();
            for (auto it = g_driving.begin(); it != g_driving.end();)
            {
                if (StillTheSameThing(*it))
                {
                    TakeSlots(*it);
                    ++it;
                    continue;
                }
                GiveSlotBack(*it); // a no-op once the slots are the new occupant's

                ModContext* const owner = it->owner;
                const uint32_t token = it->token;

                it = g_driving.erase(it);
                if (owner != nullptr)
                {
                    owner->owned.Remove(token);
                }
            }
        }

        void TakeSlots(Driving& claim)
        {
            uint8_t* const ctrl = claim.entity;

            if (Read<CtrlThinkRoutine>(ctrl, GameAddress::kEntityThinkOffset) != &CtrlThinkThunk)
            {
                claim.gameThink = Read<CtrlThinkRoutine>(ctrl, GameAddress::kEntityThinkOffset);

                const CtrlThinkRoutine mine = &CtrlThinkThunk;
                memcpy(ctrl + GameAddress::kEntityThinkOffset, &mine, sizeof(mine));
            }
            if (Read<CtrlTouchRoutine>(ctrl, GameAddress::kEntityTouchHandlerOffset) != &CtrlTouchThunk)
            {
                claim.gameTouch = Read<CtrlTouchRoutine>(ctrl, GameAddress::kEntityTouchHandlerOffset);

                const CtrlTouchRoutine mineToo = &CtrlTouchThunk;
                memcpy(ctrl + GameAddress::kEntityTouchHandlerOffset, &mineToo, sizeof(mineToo));
            }
        }

        Driving* ClaimFor(ModContext* caller, uint8_t* ctrl)
        {
            Driving* const held = ClaimOn(ctrl);
            if (held != nullptr)
            {
                if (held->owner != caller)
                {
                    return nullptr;
                }
                held->record = RecordOf(ctrl);
                TakeSlots(*held);
                return held;
            }

            Driving fresh = {};
            fresh.owner = caller;
            fresh.entity = ctrl;
            fresh.record = RecordOf(ctrl);
            fresh.token = caller->owned.Add(&ReleaseDriving, ctrl);
            g_driving.push_back(fresh);

            TakeSlots(g_driving.back());
            return &g_driving.back();
        }

        int __cdecl ApiGetCount(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            int found = 0;
            for (uint8_t* entity = WorldApi::FirstLiveEntity();
                entity != nullptr && found < static_cast<int>(GameAddress::kEntityPoolCount);
                entity = WorldApi::NextLiveEntity(entity))
            {
                if (IsCtrl(entity))
                {
                    ++found;
                }
            }
            return found;
        }

        int __cdecl ApiGet(AmodMod mod, int index, AmodCtrl* out)
        {
            const uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;

            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d in this room", index);
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Get was given nowhere to put it");
            }

            AmodCtrl filled = {};
            filled.structSize = sizeof(filled);
            filled.entity = EntityIndexOf(ctrl);
            filled.name = ReadText(ctrl, GameAddress::kEntityNameOffset);
            filled.tag = ReadText(ctrl, GameAddress::kEntityTagOffset);
            // Entity tag-state byte; doors keep open/shut here.
            filled.tagOn = Read<uint8_t>(ctrl, GameAddress::kEntityTagStateOffset) != 0 ? 1 : 0;
            SyncIds();
            filled.id = IdForRecord(WorldApi::CurrentRoom(), RecordOf(ctrl));

            for (int axis = 0; axis < 3; ++axis)
            {
                filled.position[axis] = Read<float>(ctrl, GameAddress::kEntityPositionOffset +
                    static_cast<uintptr_t>(axis) * sizeof(float));
            }

            filled.column = FloorToInt((filled.position[0] - kOriginX) / kCellSize);
            filled.row = FloorToInt((kOriginY - filled.position[1]) / kCellSize);
            filled.isHidden = (Read<uint32_t>(ctrl, GameAddress::kEntityFlagsOffset) &
                GameAddress::kEntityHiddenFlag) != 0 ? 1 : 0;
            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiFind(AmodMod mod, const char* name, int from)
        {
            if (Caller(mod) == nullptr || name == nullptr)
            {
                return -1;
            }

            const size_t wanted = strlen(name);
            const int total = ApiGetCount(mod);

            for (int at = from < 0 ? 0 : from; at < total; ++at)
            {
                const uint8_t* const ctrl = CtrlAt(at);
                if (ctrl != nullptr &&
                    strncmp(ReadText(ctrl, GameAddress::kEntityNameOffset), name, wanted) == 0)
                {
                    return at;
                }
            }
            return -1;
        }

        int __cdecl ApiSetPosition(AmodMod mod, int index, const float* position)
        {
            uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;

            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to move", index);
            }
            if (position == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetPosition was given nowhere to move it to");
            }

            memcpy(ctrl + GameAddress::kEntityPositionOffset, position, 3 * sizeof(float));
            memcpy(ctrl + GameAddress::kEntityPreviousPositionOffset, position, 3 * sizeof(float));
            return 1;
        }

        int __cdecl ApiSetHidden(AmodMod mod, int index, int hidden)
        {
            uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;

            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to hide", index);
            }

            uint32_t flags = Read<uint32_t>(ctrl, GameAddress::kEntityFlagsOffset);
            flags = hidden != 0 ? (flags | GameAddress::kEntityHiddenFlag)
                : (flags & ~GameAddress::kEntityHiddenFlag);
            memcpy(ctrl + GameAddress::kEntityFlagsOffset, &flags, sizeof(flags));
            return 1;
        }

        int __cdecl ApiGetProperty(AmodMod mod, int index, const char* key, AmodValue* value)
        {
            uint8_t* const record = Caller(mod) != nullptr ? RecordOf(CtrlAt(index)) : nullptr;
            const Schema* const schema = SchemaOf(record);

            if (schema == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "ctrl %d carries no block this build knows", index);
            }
            if (key == nullptr || value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetProperty wants a key and somewhere to answer");
            }

            const Field* const field = ApiProperty::Find(schema->fields, schema->count, key);
            if (field == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "'%s' is not one of its keys", key);
            }
            return ApiProperty::Get(record, *field, value);
        }

        int __cdecl ApiSetProperty(AmodMod mod, int index, const char* key, const AmodValue* value)
        {
            uint8_t* const record = Caller(mod) != nullptr ? RecordOf(CtrlAt(index)) : nullptr;
            const Schema* const schema = SchemaOf(record);

            if (schema == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "ctrl %d carries no block this build knows", index);
            }
            if (key == nullptr || value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetProperty wants a key and a value");
            }

            const Field* const field = ApiProperty::Find(schema->fields, schema->count, key);
            if (field == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "'%s' is not one of its keys", key);
            }
            if (!field->writable)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "'%s' is read only", key);
            }
            return ApiProperty::Set(record, *field, *value);
        }

        int __cdecl ApiGetBox(AmodMod mod, int index, AmodBox* out)
        {
            const uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;

            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to read a box off", index);
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetBox was given nowhere to put it");
            }

            const uint8_t* const box = ctrl + GameAddress::kEntityFrameBoxOffset;

            AmodBox filled = {};
            filled.structSize = sizeof(filled);
            filled.min[0] = Read<float>(box, GameAddress::kBoxMinXOffset);
            filled.min[1] = Read<float>(box, GameAddress::kBoxMinYOffset);
            filled.min[2] = Read<float>(box, GameAddress::kBoxMinZOffset);
            filled.max[0] = Read<float>(box, GameAddress::kBoxMaxXOffset);
            filled.max[1] = Read<float>(box, GameAddress::kBoxMaxYOffset);
            filled.max[2] = Read<float>(box, GameAddress::kBoxMaxZOffset);
            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetBox(AmodMod mod, int index, const AmodBox* box)
        {
            uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;

            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to give a box to", index);
            }
            if (box == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetBox was given no box");
            }

            uint8_t* const at = ctrl + GameAddress::kEntityFrameBoxOffset;
            memcpy(at + GameAddress::kBoxMinXOffset, &box->min[0], sizeof(float));
            memcpy(at + GameAddress::kBoxMinYOffset, &box->min[1], sizeof(float));
            memcpy(at + GameAddress::kBoxMinZOffset, &box->min[2], sizeof(float));
            memcpy(at + GameAddress::kBoxMaxXOffset, &box->max[0], sizeof(float));
            memcpy(at + GameAddress::kBoxMaxYOffset, &box->max[1], sizeof(float));
            memcpy(at + GameAddress::kBoxMaxZOffset, &box->max[2], sizeof(float));
            return 1;
        }

        int __cdecl ApiDrive(AmodMod mod, int index, AmodCtrlDriver driver,
            AmodCtrlUpdateHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const ctrl = caller != nullptr ? CtrlAt(index) : nullptr;

            if (caller == nullptr)
            {
                return 0;
            }
            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to drive", index);
            }
            if (driver < AMOD_CTRL_DRIVER_GAME || driver > AMOD_CTRL_DRIVER_NOBODY)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d is not one of the four drivers", static_cast<int>(driver));
            }
            if (handler == nullptr &&
                (driver == AMOD_CTRL_DRIVER_MOD || driver == AMOD_CTRL_DRIVER_BOTH))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "driving it needs a handler to drive with");
            }

            Driving* const claim = ClaimFor(caller, ctrl);
            if (claim == nullptr)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod is already holding ctrl %d", index);
            }

            claim->driver = driver;
            claim->handler = handler;
            claim->user = user;
            return 1;
        }

        typedef void(__cdecl* ParseFileRoutine)(void* room, void* cells, const char* text);
        typedef void(__cdecl* CtrlSpawnRoutine)(void* room, void* record);

        const char* const kGridPlacedKinds[] = {
            "ladder", "chain", "pipe", "fire", "airway_down", "level_exit", "door"
        };

        bool IsWordChar(char letter)
        {
            return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
                (letter >= '0' && letter <= '9') || letter == '_';
        }

        // Block name the room already has, or null.
        const char* GridPlacedIn(const char* text)
        {
            int depth = 0;

            for (const char* at = text; *at != '\0'; ++at)
            {
                if (at[0] == '/' && at[1] == '/')
                {
                    while (*at != '\0' && *at != '\n') { ++at; }
                    if (*at == '\0') { break; }
                    continue;
                }
                if (*at == '{') { ++depth; continue; }
                if (*at == '}') { --depth; continue; }
                if (*at != '$' || depth != 0) { continue; }

                for (const char* const kind : kGridPlacedKinds)
                {
                    const size_t length = strlen(kind);
                    if (_strnicmp(at + 1, kind, length) == 0 && !IsWordChar(at[1 + length]))
                    {
                        return kind;
                    }
                    // Door name variants ($door_a and siblings).
                    if (strcmp(kind, "door") == 0 && _strnicmp(at + 1, "door_", 5) == 0)
                    {
                        return kind;
                    }
                }
            }
            return nullptr;
        }

        const char* ModKindName(int kind);

        // Kind numbers from Ctrl_Spawn jump table.
        const char* KindName(int kind)
        {
            static const char* const kNames[] = {
                "", "conveyer", "door", "disapp_plat", "force_field", "fire", "level_exit",
                "restore_plat", "lift", "bonus", "monster", "note", "pipe", "ladder", "chain",
                "airway_down", "teleport", "airflow"
            };
            return kind > 0 && kind < static_cast<int>(sizeof(kNames) / sizeof(kNames[0]))
                ? kNames[kind] : ModKindName(kind);
        }

        // roomIndex < 0 = current room.
        uint8_t* RoomFor(int roomIndex)
        {
            uint8_t* const parsing = WorldApi::ParsingRoom();
            uint8_t* const room = parsing != nullptr ? parsing
                : (roomIndex < 0 ? WorldApi::CurrentRoom() : WorldApi::RoomAt(roomIndex));
            return room != nullptr &&
                Read<uint32_t>(room, GameAddress::kRoomMagicOffset) == GameAddress::kRoomMagic
                ? room : nullptr;
        }

        bool RoomIsHere(const uint8_t* room)
        {
            return room != nullptr && room == WorldApi::CurrentRoom();
        }

        uint8_t* RawPlacementAt(uint8_t* room, int raw)
        {
            uint8_t* const* const records = room != nullptr
                ? Read<uint8_t* const*>(room, GameAddress::kRoomRecordsOffset) : nullptr;
            const int count = room != nullptr
                ? Read<int>(room, GameAddress::kRoomRecordCountOffset) : 0;

            return (records != nullptr && raw >= 0 && raw < count) ? records[raw] : nullptr;
        }

        int PlacementTotal(uint8_t* room)
        {
            const int count = room != nullptr
                ? Read<int>(room, GameAddress::kRoomRecordCountOffset) : 0;
            int ours = 0;

            for (int raw = 0; raw < count; ++raw)
            {
                const uint8_t* const record = RawPlacementAt(room, raw);
                ours += (record != nullptr && IsCtrlKind(Read<int>(record, GameAddress::kSpawnKindOffset)))
                    ? 1 : 0;
            }
            return ours;
        }

        uint8_t* PlacementAt(uint8_t* room, int at)
        {
            const int count = room != nullptr
                ? Read<int>(room, GameAddress::kRoomRecordCountOffset) : 0;
            int seen = 0;

            for (int raw = 0; at >= 0 && raw < count; ++raw)
            {
                uint8_t* const record = RawPlacementAt(room, raw);
                if (record == nullptr || !IsCtrlKind(Read<int>(record, GameAddress::kSpawnKindOffset)))
                {
                    continue;
                }
                if (seen == at)
                {
                    return record;
                }
                ++seen;
            }
            return nullptr;
        }

        const char* TagOfRecord(const uint8_t* record, const Schema* schema)
        {
            const Field* const field = schema != nullptr
                ? ApiProperty::Find(schema->fields, schema->count, "tag") : nullptr;
            return field != nullptr ? ReadText(record, field->offset) : "";
        }

        char* CellAt(uint8_t* room, size_t index)
        {
            return reinterpret_cast<char*>(room + GameAddress::kRoomCellsOffset +
                index * GameAddress::kRoomCellStride);
        }

        // Parse into our buffer; parser tokenises in place.
        int ParseInto(uint8_t* room, const char* block)
        {
            const ParseFileRoutine parse =
                reinterpret_cast<ParseFileRoutine>(GameModule::Resolve(GameAddress::kEntitiesParseFile));
            const CtrlSpawnRoutine spawn = RoomIsHere(room)
                ? reinterpret_cast<CtrlSpawnRoutine>(GameModule::Resolve(GameAddress::kCtrlSpawn))
                : nullptr;

            if (parse == nullptr)
            {
                return 0;
            }

            const int before = Read<int>(room, GameAddress::kRoomRecordCountOffset);
            std::vector<char> text(block, block + strlen(block) + 1);
            parse(room, room + GameAddress::kRoomCellsOffset, text.data());

            const int after = Read<int>(room, GameAddress::kRoomRecordCountOffset);
            uint8_t* const* const records =
                Read<uint8_t* const*>(room, GameAddress::kRoomRecordsOffset);

            if (records == nullptr)
            {
                return 0;
            }

            // Only entities this block just added.
            for (int at = before; spawn != nullptr && at < after; ++at)
            {
                spawn(room, records[at]);
            }
            return after - before;
        }

        struct Made
        {
            ModContext* owner;
            uint8_t* room;
            const void* record;
            uint32_t generation;
        };

        std::vector<Made> g_made;

        void RememberMade(ModContext* owner, uint8_t* room, int from, int to)
        {
            uint8_t* const* const records = room != nullptr
                ? Read<uint8_t* const*>(room, GameAddress::kRoomRecordsOffset) : nullptr;

            for (int at = from; records != nullptr && at < to; ++at)
            {
                const Made mine = { owner, room, records[at], WorldApi::LevelGeneration() };
                g_made.push_back(mine);
            }
        }

        // Mod kinds numbered past the game's dispatcher range.
        constexpr int kFirstModKind = 100;

        struct Registered
        {
            ModContext* owner;
            int kind;
            std::vector<char> name;
            AmodCtrlKindHandler handler;
            void* user;
        };

        std::vector<Registered> g_kinds;
        int g_nextKind = kFirstModKind;

        // Game record a mod has borrowed.
        struct Borrowed
        {
            const void* record;
            int kind;
        };

        std::vector<Borrowed> g_borrowed;

        int BorrowedKindOf(const void* record)
        {
            for (const Borrowed& known : g_borrowed)
            {
                if (known.record == record)
                {
                    return known.kind;
                }
            }
            return 0;
        }

        const Registered* KindOf(int kind)
        {
            for (const Registered& known : g_kinds)
            {
                if (known.kind == kind)
                {
                    return &known;
                }
            }
            return nullptr;
        }

        const char* ModKindName(int kind)
        {
            const Registered* const mine = KindOf(kind);
            return mine != nullptr ? mine->name.data() : "";
        }

        // Registered kind uses these two trampolines.
        void* AsUser(int kind)
        {
            return reinterpret_cast<void*>(static_cast<uintptr_t>(kind));
        }

        int Fill(AmodCtrlEventInfo& info, void* user, int index)
        {
            const int kind = static_cast<int>(reinterpret_cast<uintptr_t>(user));
            const uint8_t* const ctrl = CtrlAt(index);

            info.structSize = sizeof(info);
            info.record = RecordOf(ctrl);
            info.id = IdForRecord(WorldApi::CurrentRoom(), info.record);
            return kind;
        }

        void __cdecl KindUpdate(const AmodCtrlFrame* frame, void* user)
        {
            AmodCtrlEventInfo info = {};
            const Registered* const mine = KindOf(Fill(info, user, frame->index));

            if (mine == nullptr || mine->handler == nullptr)
            {
                return;
            }

            info.event = AMOD_CTRL_EVENT_UPDATE;
            info.frame = frame;
            mine->handler(&info, mine->user);
        }

        AmodCtrlTouchResult __cdecl KindTouch(const AmodCtrlTouchInfo* touch, void* user)
        {
            AmodCtrlEventInfo info = {};
            const Registered* const mine = KindOf(Fill(info, user, touch->index));

            if (mine == nullptr || mine->handler == nullptr)
            {
                return AMOD_CTRL_TOUCH_HANDLED;
            }

            info.event = AMOD_CTRL_EVENT_TOUCH;
            info.touch = touch;
            return static_cast<AmodCtrlTouchResult>(mine->handler(&info, mine->user));
        }

        typedef uint8_t* (__cdecl* EntityAllocRoutine)();
        typedef void* (__cdecl* CloneRoutine)(const void* source, size_t size);
        typedef void(__cdecl* AppendRoutine)(void* room, void* record);

        CtrlSpawnRoutine g_gameSpawn = nullptr;

        void WriteBox(uint8_t* entity, const AmodBox& box, bool solid)
        {
            uint8_t* const at = entity + GameAddress::kEntityFrameBoxOffset;
            const int kind = solid ? GameAddress::kRoomBoxPushes : 1;

            memcpy(at + GameAddress::kRoomBoxKindOffset, &kind, sizeof(kind));
            memcpy(at + GameAddress::kBoxMinXOffset, &box.min[0], sizeof(float));
            memcpy(at + GameAddress::kBoxMinYOffset, &box.min[1], sizeof(float));
            memcpy(at + GameAddress::kBoxMinZOffset, &box.min[2], sizeof(float));
            memcpy(at + GameAddress::kBoxMaxXOffset, &box.max[0], sizeof(float));
            memcpy(at + GameAddress::kBoxMaxYOffset, &box.max[1], sizeof(float));
            memcpy(at + GameAddress::kBoxMaxZOffset, &box.max[2], sizeof(float));
        }

        void WriteText(uint8_t* entity, uintptr_t offset, const char* text, size_t room)
        {
            const size_t length = text != nullptr ? strlen(text) : 0;
            memcpy(entity + offset, text != nullptr ? text : "", length + 1 < room ? length + 1 : room - 1);
        }

        uint8_t* BuildEntity(const AmodCtrlBody& body, uint8_t* record)
        {
            const EntityAllocRoutine alloc =
                reinterpret_cast<EntityAllocRoutine>(GameModule::Resolve(GameAddress::kEntitiesAlloc));
            uint8_t* const entity = alloc != nullptr ? alloc() : nullptr;

            if (entity == nullptr)
            {
                return nullptr;
            }

            const int kind = GameAddress::kEntityKindCtrl;
            memcpy(entity + GameAddress::kEntityKindOffset, &kind, sizeof(kind));
            WriteText(entity, GameAddress::kEntityNameOffset, body.name, GameAddress::kEntityTagOffset -
                GameAddress::kEntityNameOffset);
            WriteText(entity, GameAddress::kEntityTagOffset, body.tag, GameAddress::kEntityTagSize);

            for (int axis = 0; axis < 3; ++axis)
            {
                const uintptr_t at = static_cast<uintptr_t>(axis) * sizeof(float);
                memcpy(entity + GameAddress::kEntityPositionOffset + at, &body.position[axis], sizeof(float));
                memcpy(entity + GameAddress::kEntityPreviousPositionOffset + at, &body.position[axis], sizeof(float));
            }

            WriteBox(entity, body.box, body.isSolid != 0);

            void* const model = MmdApi::ModelFor(body.model);
            void* const texture = TextureApi::TextureFor(body.texture);
            memcpy(entity + GameAddress::kEntityModelOffset, &model, sizeof(model));
            memcpy(entity + GameAddress::kEntityTextureOffset, &texture, sizeof(texture));

            const uint32_t flags = body.isHidden != 0 ? GameAddress::kEntityHiddenFlag : 0;
            memcpy(entity + GameAddress::kEntityFlagsOffset, &flags, sizeof(flags));

            const uint8_t touchable = 1;
            memcpy(entity + GameAddress::kEntityTouchableOffset, &touchable, sizeof(touchable));
            memcpy(entity + GameAddress::kEntitySpawnOffset, &record, sizeof(record));
            return entity;
        }

        uint8_t* LiveHead()
        {
            uint8_t* const container = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kEntities));
            return container != nullptr && Readable(container, GameAddress::kEntitiesLiveOffset + sizeof(uintptr_t))
                ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(container, GameAddress::kEntitiesLiveOffset))
                : nullptr;
        }

        void ClaimBorrowed(uint8_t* room, const void* record, int kind)
        {
            const Registered* const mine = KindOf(kind);
            if (mine == nullptr)
            {
                return;
            }

            for (uint8_t* ctrl = LiveHead(); ctrl != nullptr;
                ctrl = reinterpret_cast<uint8_t*>(Read<uintptr_t>(ctrl, GameAddress::kEntityNextOffset)))
            {
                if (!IsCtrl(ctrl) || RecordOf(ctrl) != record)
                {
                    continue;
                }

                Driving* claim = ClaimOn(ctrl);
                if (claim == nullptr)
                {
                    Driving fresh = {};
                    fresh.entity = ctrl;
                    fresh.token = mine->owner->owned.Add(&ReleaseDriving, ctrl);
                    g_driving.push_back(fresh);
                    claim = &g_driving.back();
                }

                claim->owner = mine->owner;
                claim->record = static_cast<const uint8_t*>(record);
                claim->driver = AMOD_CTRL_DRIVER_BOTH;
                claim->handler = &KindUpdate;
                claim->touchHandler = &KindTouch;
                claim->user = AsUser(kind);
                claim->touchUser = AsUser(kind);
                TakeSlots(*claim);

                FORGE_LOG_INFO("Ctrls: kind %d took the thing on record %p", kind, record);
                return;
            }

            FORGE_LOG_ERROR("Ctrls: kind %d found nothing standing on record %p to take",
                kind, record);
        }

        // Spawn slot: game kinds go through; mod kinds claimed here.
        void __cdecl SpawnThunk(void* room, void* record)
        {
            const int kind = record != nullptr && Readable(record, sizeof(int))
                ? Read<int>(static_cast<const uint8_t*>(record), 0) : 0;
            const Registered* const mine = KindOf(kind);

            if (mine == nullptr)
            {
                if (g_gameSpawn != nullptr)
                {
                    g_gameSpawn(room, record);
                }

                const int borrowed = BorrowedKindOf(record);
                if (borrowed != 0)
                {
                    ClaimBorrowed(static_cast<uint8_t*>(room), record, borrowed);
                }
                else if (!g_borrowed.empty())
                {
                    FORGE_LOG_INFO("Ctrls: the game stood up record %p (kind %d), which nobody borrowed",
                        record, kind);
                }
                return;
            }

            AmodCtrlBody body = {};
            body.structSize = sizeof(body);
            body.isSolid = 1;

            AmodCtrlEventInfo asking = {};
            asking.structSize = sizeof(asking);
            asking.event = AMOD_CTRL_EVENT_BUILD;
            asking.record = record;
            asking.body = &body;

            if (mine->handler == nullptr || mine->handler(&asking, mine->user) == 0)
            {
                return;
            }

            uint8_t* const entity = BuildEntity(body, static_cast<uint8_t*>(record));
            if (entity == nullptr)
            {
                return;
            }

            // Claim for the owning mod at spawn.
            Driving* claim = ClaimOn(entity);
            if (claim == nullptr)
            {
                Driving fresh = {};
                fresh.entity = entity;
                fresh.token = mine->owner->owned.Add(&ReleaseDriving, entity);
                g_driving.push_back(fresh);
                claim = &g_driving.back();
            }

            claim->owner = mine->owner;
            claim->record = static_cast<const uint8_t*>(record);
            claim->driver = AMOD_CTRL_DRIVER_MOD;
            claim->handler = &KindUpdate;
            claim->touchHandler = &KindTouch;
            claim->user = AsUser(mine->kind);
            claim->touchUser = AsUser(mine->kind);
            claim->gameThink = nullptr;
            claim->gameTouch = nullptr;

            TakeSlots(*claim);
        }

        InlineHook g_parseHook;
        ParseFileRoutine g_originalParse = nullptr;

        int RoomIndexOf(const uint8_t* room)
        {
            for (int at = 0; at < WorldApi::RoomTotal(); ++at)
            {
                if (WorldApi::RoomAt(at) == room)
                {
                    return at;
                }
            }
            return -1;
        }

        void OfferBlocksToMods(uint8_t* room, const char* text)
        {
            const int where = RoomIndexOf(room);
            int depth = 0;

            for (const char* at = text; *at != '\0'; ++at)
            {
                if (at[0] == '/' && at[1] == '/')
                {
                    while (*at != '\0' && *at != '\n') { ++at; }
                    if (*at == '\0') { break; }
                    continue;
                }
                if (*at == '{') { ++depth; continue; }
                if (*at == '}') { --depth; continue; }
                if (*at != '$' || depth != 0) { continue; }

                const char* const name = at + 1;
                const char* end = name;
                while (IsWordChar(*end)) { ++end; }

                const size_t length = static_cast<size_t>(end - name);
                const Registered* mine = nullptr;

                for (const Registered& known : g_kinds)
                {
                    if (strlen(known.name.data()) == length &&
                        strncmp(known.name.data(), name, length) == 0)
                    {
                        mine = &known;
                        break;
                    }
                }

                at = end - 1;
                if (mine == nullptr || mine->handler == nullptr)
                {
                    continue;
                }

                // Copy block body so the mod sees what it wrote.
                const char* open = end;
                while (*open != '\0' && *open != '{' && *open != '$') { ++open; }
                if (*open != '{')
                {
                    continue;
                }

                int inside = 1;
                const char* close = open + 1;
                for (; *close != '\0' && inside > 0; ++close)
                {
                    inside += *close == '{' ? 1 : (*close == '}' ? -1 : 0);
                }

                const std::vector<char> body(open + 1, close - 1);
                std::vector<char> zeroed(body);
                zeroed.push_back('\0');

                AmodCtrlEventInfo asking = {};
                asking.structSize = sizeof(asking);
                asking.event = AMOD_CTRL_EVENT_PARSE;
                asking.text = zeroed.data();
                asking.roomIndex = where;

                // During level parse rooms are not yet indexed.
                WorldApi::SetParsingRoom(room);
                mine->handler(&asking, mine->user);
                WorldApi::SetParsingRoom(nullptr);

                at = close - 1;
            }
        }

        void __cdecl ParseFileDetour(void* room, void* cells, const char* text)
        {
            if (g_originalParse != nullptr)
            {
                g_originalParse(room, cells, text);
            }
            OfferBlocksToMods(static_cast<uint8_t*>(room), text);
        }

        void HoldSpawnSlot()
        {
            void** const slot = static_cast<void**>(GameModule::Resolve(GameAddress::kSpawnPlacementSlot));
            if (slot == nullptr || *slot == reinterpret_cast<void*>(&SpawnThunk))
            {
                return;
            }

            if (*slot != nullptr)
            {
                g_gameSpawn = reinterpret_cast<CtrlSpawnRoutine>(*slot);
            }
            *slot = reinterpret_cast<void*>(&SpawnThunk);
        }

        int __cdecl ApiRegisterKind(AmodMod mod, const char* name, AmodCtrlKindHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (name == nullptr || *name == '\0' || handler == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a kind wants a name and a way to answer for it");
            }

            Registered fresh = {};
            fresh.owner = caller;
            fresh.kind = g_nextKind++;
            fresh.name.assign(name, name + strlen(name) + 1);
            fresh.handler = handler;
            fresh.user = user;
            g_kinds.push_back(fresh);

            FORGE_LOG_INFO("Ctrls: '%s' is kind %d", name, fresh.kind);
            return fresh.kind;
        }

        AmodCtrlId __cdecl ApiMake(AmodMod mod, int roomIndex, int kind, const void* record, int size)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const room = caller != nullptr ? RoomFor(roomIndex) : nullptr;
            const Registered* const mine = KindOf(kind);

            const CloneRoutine clone =
                reinterpret_cast<CloneRoutine>(GameModule::Resolve(GameAddress::kMemClone));
            const AppendRoutine append =
                reinterpret_cast<AppendRoutine>(GameModule::Resolve(GameAddress::kRoomSpawnsAppend));

            if (caller == nullptr)
            {
                return AMOD_CTRL_NONE;
            }
            if (mine == nullptr || mine->owner != caller)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "%d is not a kind this mod registered", kind);
                return AMOD_CTRL_NONE;
            }
            if (record == nullptr || size < static_cast<int>(sizeof(int)))
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a record has to be at least its own kind wide");
                return AMOD_CTRL_NONE;
            }
            if (room == nullptr || clone == nullptr || append == nullptr)
            {
                Fail(mod, AMOD_FAIL_NOT_HERE, "there is no room %d to put one in", roomIndex);
                return AMOD_CTRL_NONE;
            }

            // First dword = kind for spawn dispatch.
            const uint8_t* const from = static_cast<const uint8_t*>(record);
            std::vector<uint8_t> bytes(from, from + size);
            memcpy(bytes.data(), &kind, sizeof(kind));

            uint8_t* const kept = static_cast<uint8_t*>(clone(bytes.data(), bytes.size()));
            if (kept == nullptr)
            {
                Fail(mod, AMOD_FAIL_REFUSED, "the game would not keep the record");
                return AMOD_CTRL_NONE;
            }

            append(room, kept);

            const Made made = { caller, room, kept, WorldApi::LevelGeneration() };
            g_made.push_back(made);

            if (RoomIsHere(room))
            {
                SpawnThunk(room, kept);
            }

            SyncIds();
            return IdForRecord(room, kept);
        }

        AmodCtrlId __cdecl ApiBorrow(AmodMod mod, int roomIndex, const char* block,
            AmodCtrlKindHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const room = caller != nullptr ? RoomFor(roomIndex) : nullptr;

            if (caller == nullptr)
            {
                return AMOD_CTRL_NONE;
            }
            if (block == nullptr || *block == '\0' || handler == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Borrow wants a block and a handler to hear it");
                return AMOD_CTRL_NONE;
            }
            if (room == nullptr)
            {
                Fail(mod, AMOD_FAIL_NOT_HERE, "there is no room %d to borrow into", roomIndex);
                return AMOD_CTRL_NONE;
            }

            const int before = Read<int>(room, GameAddress::kRoomRecordCountOffset);
            const int made = ParseInto(room, block);

            if (made <= 0)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "the block made nothing to borrow");
                return AMOD_CTRL_NONE;
            }

            RememberMade(caller, room, before, before + made);

            uint8_t* const record = RawPlacementAt(room, before);
            if (record == nullptr)
            {
                return AMOD_CTRL_NONE;
            }

            Registered borrowed = {};
            borrowed.owner = caller;
            borrowed.kind = g_nextKind++;
            borrowed.name.assign(1, '\0');
            borrowed.handler = handler;
            borrowed.user = user;
            g_kinds.push_back(borrowed);

            FORGE_LOG_INFO("Ctrls: borrowed kind %d, %d record(s) made, first at %p (kind %d)",
                borrowed.kind, made, record, Read<int>(record, 0));

            // Remember on the record so later spawns reclaim it.
            const Borrowed kept = { record, borrowed.kind };
            g_borrowed.push_back(kept);

            // Only if already live in the current room.
            if (RoomIsHere(room))
            {
                ClaimBorrowed(room, record, borrowed.kind);
            }

            SyncIds();
            return IdForRecord(room, record);
        }

        int __cdecl ApiCreateIn(AmodMod mod, int roomIndex, const char* block)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const room = caller != nullptr ? RoomFor(roomIndex) : nullptr;

            if (caller == nullptr)
            {
                return 0;
            }
            if (block == nullptr || *block == '\0')
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Create was given no block to read");
            }
            if (room == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no room %d to put a ctrl in", roomIndex);
            }

            const char* const gridPlaced = GridPlacedIn(block);
            if (gridPlaced != nullptr)
            {
                return Fail(mod, AMOD_FAIL_REFUSED,
                    "$%s is found by a walk of the room's grid, not placed, so it needs "
                    "CreateOn and the cells it is to be found on", gridPlaced);
            }

            const int before = Read<int>(room, GameAddress::kRoomRecordCountOffset);
            const int made = ParseInto(room, block);

            if (made <= 0)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT,
                    "the block made no ctrl; a missing $model or an unknown name does that");
            }

            RememberMade(caller, room, before, before + made);
            return made;
        }

        int __cdecl ApiCreate(AmodMod mod, const char* block)
        {
            return ApiCreateIn(mod, -1, block);
        }

        int __cdecl ApiPlacementCount(AmodMod mod, int roomIndex)
        {
            return Caller(mod) != nullptr ? PlacementTotal(RoomFor(roomIndex)) : 0;
        }

        int __cdecl ApiGetPlacement(AmodMod mod, int roomIndex, int at, AmodCtrlPlacement* out)
        {
            uint8_t* const room = Caller(mod) != nullptr ? RoomFor(roomIndex) : nullptr;
            uint8_t* const record = PlacementAt(room, at);

            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "room %d holds no placement %d", roomIndex, at);
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetPlacement was given nowhere to answer");
            }

            AmodCtrlPlacement filled = {};
            filled.structSize = sizeof(filled);
            filled.kind = Read<int>(record, GameAddress::kSpawnKindOffset);
            filled.kindName = KindName(filled.kind);
            filled.tag = TagOfRecord(record, SchemaOf(record));

            SyncIds();
            filled.id = IdForRecord(room, record);
            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiGetPlacementProperty(AmodMod mod, int roomIndex, int at, const char* key,
            AmodValue* value)
        {
            uint8_t* const record = Caller(mod) != nullptr
                ? PlacementAt(RoomFor(roomIndex), at) : nullptr;
            const Schema* const schema = SchemaOf(record);

            if (schema == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "placement %d carries no block this build knows", at);
            }
            if (key == nullptr || value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetPlacementProperty wants a key and somewhere to answer");
            }

            const Field* const field = ApiProperty::Find(schema->fields, schema->count, key);
            if (field == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "'%s' is not one of its keys", key);
            }
            return ApiProperty::Get(record, *field, value);
        }

        int __cdecl ApiSetPlacementProperty(AmodMod mod, int roomIndex, int at, const char* key,
            const AmodValue* value)
        {
            uint8_t* const record = Caller(mod) != nullptr
                ? PlacementAt(RoomFor(roomIndex), at) : nullptr;
            const Schema* const schema = SchemaOf(record);

            if (schema == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "placement %d carries no block this build knows", at);
            }
            if (key == nullptr || value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetPlacementProperty wants a key and a value");
            }

            const Field* const field = ApiProperty::Find(schema->fields, schema->count, key);
            if (field == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "'%s' is not one of its keys", key);
            }
            if (!field->writable)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "'%s' was spent when the thing was made", key);
            }
            return ApiProperty::Set(record, *field, *value);
        }

        int __cdecl ApiPlacementPropertyCount(AmodMod mod, int roomIndex, int at)
        {
            const Schema* const schema = Caller(mod) != nullptr
                ? SchemaOf(PlacementAt(RoomFor(roomIndex), at)) : nullptr;
            return schema != nullptr ? static_cast<int>(schema->count) : 0;
        }

        const char* __cdecl ApiPlacementPropertyNameAt(AmodMod mod, int roomIndex, int at, int which)
        {
            const Schema* const schema = Caller(mod) != nullptr
                ? SchemaOf(PlacementAt(RoomFor(roomIndex), at)) : nullptr;

            if (schema == nullptr || which < 0 || which >= static_cast<int>(schema->count))
            {
                return "";
            }
            return schema->fields[which].key;
        }

        int __cdecl ApiCreateOn(AmodMod mod, const char* block, const char* cells, int count)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const room = caller != nullptr ? WorldApi::CurrentRoom() : nullptr;
            const size_t total = GameAddress::kRoomGridColumns * GameAddress::kRoomGridRows;

            if (caller == nullptr)
            {
                return 0;
            }
            if (block == nullptr || *block == '\0')
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "CreateOn was given no block to read");
            }
            if (cells == nullptr || count <= 0 || static_cast<size_t>(count) > total)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT,
                    "CreateOn wants a grid of up to %d cells to build on", static_cast<int>(total));
            }
            if (room == nullptr || Read<uint32_t>(room, GameAddress::kRoomMagicOffset) != GameAddress::kRoomMagic)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "there is no room to put a ctrl in");
            }

            std::vector<char> was(total);
            for (size_t at = 0; at < total; ++at)
            {
                char* const cell = CellAt(room, at);
                was[at] = *cell;

                const char wanted = at < static_cast<size_t>(count) ? cells[at] : '\0';
                *cell = wanted != '\0' ? wanted : GameAddress::kRoomCellEmpty;
            }

            const int made = ParseInto(room, block);

            for (size_t at = 0; at < total; ++at)
            {
                const char wanted = at < static_cast<size_t>(count) ? cells[at] : '\0';
                if (wanted == '\0')
                {
                    *CellAt(room, at) = was[at];
                }
            }

            (void)made;
            return 1;
        }

        int __cdecl ApiRelease(AmodMod mod, int index);

        // Drop from room records so Room_Activate will not respawn it.
        bool ForgetRecord(uint8_t* room, const uint8_t* record)
        {
            // Room magic must still match.
            if (room == nullptr || !Readable(room, GameAddress::kRoomRecordCountOffset + sizeof(int)) ||
                Read<uint32_t>(room, GameAddress::kRoomMagicOffset) != GameAddress::kRoomMagic)
            {
                return false;
            }

            uint8_t** const records = Read<uint8_t**>(room, GameAddress::kRoomRecordsOffset);
            const int count = Read<int>(room, GameAddress::kRoomRecordCountOffset);

            if (records == nullptr || record == nullptr)
            {
                return false;
            }

            for (int at = 0; at < count; ++at)
            {
                if (records[at] != record)
                {
                    continue;
                }
                for (int move = at; move + 1 < count; ++move)
                {
                    records[move] = records[move + 1];
                }

                const int left = count - 1;
                memcpy(room + GameAddress::kRoomRecordCountOffset, &left, sizeof(left));
                return true;
            }
            return false;
        }

        int __cdecl ApiRemove(AmodMod mod, int index)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const ctrl = caller != nullptr ? CtrlAt(index) : nullptr;
            uint8_t* const room = caller != nullptr ? WorldApi::CurrentRoom() : nullptr;

            if (caller == nullptr)
            {
                return 0;
            }
            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to take away", index);
            }

            const Driving* const claim = ClaimOn(ctrl);
            if (claim != nullptr && claim->owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod is holding ctrl %d", index);
            }

            // Give slots back before marking dead.
            if (claim != nullptr)
            {
                ApiRelease(mod, index);
            }

            uint8_t* const record = RecordOf(ctrl);
            const bool wasTheRooms = room != nullptr &&
                Read<uint32_t>(room, GameAddress::kRoomMagicOffset) == GameAddress::kRoomMagic &&
                ForgetRecord(room, record);

            // If removed from room list, queue record for free; else room keeps it.
            if (wasTheRooms && record != nullptr)
            {
                const Doomed spent = { ctrl, record };
                g_doomed.push_back(spent);
            }

            // Dead flag; Entities_Update unlinks at frame end.
            const uint8_t dead = 1;
            memcpy(ctrl + GameAddress::kEntityDeadOffset, &dead, sizeof(dead));
            return 1;
        }

        int __cdecl ApiRemovePlacement(AmodMod mod, int roomIndex, int at)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const room = caller != nullptr ? RoomFor(roomIndex) : nullptr;
            uint8_t* const record = PlacementAt(room, at);

            if (caller == nullptr)
            {
                return 0;
            }
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "room %d holds no placement %d", roomIndex, at);
            }

            if (RoomIsHere(room))
            {
                for (int index = 0;; ++index)
                {
                    uint8_t* const ctrl = CtrlAt(index);
                    if (ctrl == nullptr)
                    {
                        break;
                    }
                    if (RecordOf(ctrl) == record)
                    {
                        return ApiRemove(mod, index);
                    }
                }
            }

            ForgetRecord(room, record);

            const Doomed spent = { nullptr, record };
            g_doomed.push_back(spent);
            return 1;
        }

        AmodCtrlId __cdecl ApiIdAt(AmodMod mod, int index)
        {
            uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;

            SyncIds();
            return ctrl != nullptr ? IdForRecord(WorldApi::CurrentRoom(), RecordOf(ctrl))
                : AMOD_CTRL_NONE;
        }

        int __cdecl ApiIndexOf(AmodMod mod, AmodCtrlId id)
        {
            const Known* const known = Caller(mod) != nullptr ? KnownById(id) : nullptr;

            if (known == nullptr || !RoomIsHere(known->room))
            {
                return -1;
            }

            for (int at = 0;; ++at)
            {
                const uint8_t* const ctrl = CtrlAt(at);
                if (ctrl == nullptr)
                {
                    return -1;
                }
                if (RecordOf(ctrl) == known->record)
                {
                    return at;
                }
            }
        }

        int __cdecl ApiRoomOf(AmodMod mod, AmodCtrlId id)
        {
            const Known* const known = Caller(mod) != nullptr ? KnownById(id) : nullptr;

            for (int at = 0; known != nullptr && at < WorldApi::RoomTotal(); ++at)
            {
                if (WorldApi::RoomAt(at) == known->room)
                {
                    return at;
                }
            }
            return -1;
        }

        int __cdecl ApiFindByTag(AmodMod mod, const char* tag)
        {
            if (Caller(mod) == nullptr)
            {
                return -1;
            }
            if (tag == nullptr || *tag == '\0')
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "FindByTag was given no tag to look for");
                return -1;
            }

            // First live ctrl with that tag.
            for (int at = 0;; ++at)
            {
                const uint8_t* const ctrl = CtrlAt(at);
                if (ctrl == nullptr)
                {
                    return -1;
                }
                if (strcmp(ReadText(ctrl, GameAddress::kEntityTagOffset), tag) == 0)
                {
                    return at;
                }
            }
        }

        int __cdecl ApiSetLook(AmodMod mod, int index, AmodModelId model, AmodTextureId texture)
        {
            uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;

            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to redress", index);
            }

            void* const shape = MmdApi::ModelFor(model);
            void* const skin = TextureApi::TextureFor(texture);

            if (shape != nullptr)
            {
                memcpy(ctrl + GameAddress::kEntityModelOffset, &shape, sizeof(shape));
            }
            if (skin != nullptr)
            {
                memcpy(ctrl + GameAddress::kEntityTextureOffset, &skin, sizeof(skin));
            }
            return shape != nullptr || skin != nullptr
                ? 1 : Fail(mod, AMOD_FAIL_NO_SUCH, "neither that model nor that texture is loaded");
        }

        int __cdecl ApiSetScale(AmodMod mod, int index, float x, float y)
        {
            uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;

            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to scale", index);
            }

            memcpy(ctrl + GameAddress::kEntityScaleXOffset, &x, sizeof(x));
            memcpy(ctrl + GameAddress::kEntityScaleYOffset, &y, sizeof(y));
            return 1;
        }

        int __cdecl ApiSetTouchable(AmodMod mod, int index, int touchable)
        {
            uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;

            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to take out of the way", index);
            }

            const uint8_t able = touchable != 0 ? 1 : 0;
            memcpy(ctrl + GameAddress::kEntityTouchableOffset, &able, sizeof(able));
            return 1;
        }

        int __cdecl ApiSetTag(AmodMod mod, int index, const char* tag)
        {
            uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;
            uint8_t* const record = RecordOf(ctrl);
            const Schema* const schema = SchemaOf(record);

            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to name", index);
            }
            if (tag == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetTag was given no name");
            }

            const Field* const field = schema != nullptr
                ? ApiProperty::Find(schema->fields, schema->count, "tag") : nullptr;

            if (field == nullptr)
            {
                return Fail(mod, AMOD_FAIL_REFUSED,
                    "a %s has nowhere in its block to keep a tag, so one written now would "
                    "be gone the moment the room is left",
                    KindName(Read<int>(record, GameAddress::kSpawnKindOffset)));
            }

            const size_t length = strlen(tag);
            if (length + 1 > GameAddress::kEntityTagSize)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a tag has room for %d letters",
                    static_cast<int>(GameAddress::kEntityTagSize) - 1);
            }

            memcpy(record + field->offset, tag, length + 1);
            memcpy(ctrl + GameAddress::kEntityTagOffset, tag, length + 1);
            return 1;
        }

        int __cdecl ApiSetTagOn(AmodMod mod, int index, int on)
        {
            uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;

            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to tag", index);
            }

            // Tag-state byte on the entity; doors keep open/shut here.
            const uint8_t state = on != 0 ? 1 : 0;
            memcpy(ctrl + GameAddress::kEntityTagStateOffset, &state, sizeof(state));
            return 1;
        }

        int __cdecl ApiOnTouch(AmodMod mod, int index, AmodCtrlTouchHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const ctrl = caller != nullptr ? CtrlAt(index) : nullptr;

            if (caller == nullptr)
            {
                return 0;
            }
            if (ctrl == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no ctrl %d to be told about", index);
            }

            // Not touchable => handler never fires.
            if (Read<uint8_t>(ctrl, GameAddress::kEntityTouchableOffset) == 0)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "ctrl %d is not one anything can touch", index);
            }

            Driving* const claim = ClaimFor(caller, ctrl);
            if (claim == nullptr)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod is already holding ctrl %d", index);
            }

            claim->touchHandler = handler;
            claim->touchUser = user;
            return 1;
        }

        int __cdecl ApiRelease(AmodMod mod, int index)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const ctrl = caller != nullptr ? CtrlAt(index) : nullptr;

            if (caller == nullptr)
            {
                return 0;
            }

            Driving* const claim = ctrl != nullptr ? ClaimOn(ctrl) : nullptr;
            if (claim == nullptr)
            {
                return 1;
            }
            if (claim->owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "ctrl %d is another mod's to hand back", index);
            }

            // Remove token first; ReleaseDriving clears the claim.
            const uint32_t token = claim->token;
            if (!caller->owned.Remove(token))
            {
                ReleaseDriving(ctrl);
            }
            return 1;
        }

        AmodCtrlDriver __cdecl ApiDriver(AmodMod mod, int index)
        {
            const uint8_t* const ctrl = Caller(mod) != nullptr ? CtrlAt(index) : nullptr;
            const Driving* const claim = ctrl != nullptr ? ClaimOn(ctrl) : nullptr;
            return claim != nullptr ? claim->driver : AMOD_CTRL_DRIVER_GAME;
        }

        int __cdecl ApiPropertyCount(AmodMod mod, int index)
        {
            const Schema* const schema = Caller(mod) != nullptr ? SchemaOf(RecordOf(CtrlAt(index))) : nullptr;
            return schema != nullptr ? static_cast<int>(schema->count) : 0;
        }

        const char* __cdecl ApiPropertyNameAt(AmodMod mod, int index, int at)
        {
            const Schema* const schema = Caller(mod) != nullptr ? SchemaOf(RecordOf(CtrlAt(index))) : nullptr;

            if (schema == nullptr || at < 0 || at >= static_cast<int>(schema->count))
            {
                return "";
            }
            return schema->fields[at].key;
        }
    }

    bool CtrlsApi::Install()
    {
        g_table.structSize = sizeof(AmodCtrlsApi);
        g_table.GetCount = &ApiGetCount;
        g_table.Get = &ApiGet;
        g_table.Find = &ApiFind;
        g_table.SetPosition = &ApiSetPosition;
        g_table.SetHidden = &ApiSetHidden;
        g_table.GetProperty = &ApiGetProperty;
        g_table.SetProperty = &ApiSetProperty;
        g_table.PropertyCount = &ApiPropertyCount;
        g_table.PropertyNameAt = &ApiPropertyNameAt;
        g_table.GetBox = &ApiGetBox;
        g_table.SetBox = &ApiSetBox;
        g_table.Drive = &ApiDrive;
        g_table.Release = &ApiRelease;
        g_table.Driver = &ApiDriver;
        g_table.OnTouch = &ApiOnTouch;
        g_table.Create = &ApiCreate;
        g_table.CreateOn = &ApiCreateOn;
        g_table.Remove = &ApiRemove;
        g_table.FindByTag = &ApiFindByTag;
        g_table.SetTagOn = &ApiSetTagOn;
        g_table.SetTag = &ApiSetTag;
        g_table.SetTouchable = &ApiSetTouchable;
        g_table.SetScale = &ApiSetScale;
        g_table.PlacementCount = &ApiPlacementCount;
        g_table.GetPlacement = &ApiGetPlacement;
        g_table.GetPlacementProperty = &ApiGetPlacementProperty;
        g_table.SetPlacementProperty = &ApiSetPlacementProperty;
        g_table.PlacementPropertyCount = &ApiPlacementPropertyCount;
        g_table.PlacementPropertyNameAt = &ApiPlacementPropertyNameAt;
        g_table.CreateIn = &ApiCreateIn;
        g_table.RemovePlacement = &ApiRemovePlacement;
        g_table.IdAt = &ApiIdAt;
        g_table.IndexOf = &ApiIndexOf;
        g_table.RoomOf = &ApiRoomOf;
        g_table.RegisterKind = &ApiRegisterKind;
        g_table.Make = &ApiMake;
        g_table.SetLook = &ApiSetLook;
        g_table.Borrow = &ApiBorrow;

        // Spawn via slot each frame; parser has no slot -- hook Entities_ParseFile.
        HoldSpawnSlot();

        void* const parse = GameModule::Resolve(GameAddress::kEntitiesParseFile);
        std::string mismatch;

        if (parse != nullptr &&
            GameModule::VerifyBytes(parse, kEntitiesParseFilePrologue,
                sizeof(kEntitiesParseFilePrologue), mismatch))
        {
            std::string error;
            if (g_parseHook.Install(parse, reinterpret_cast<void*>(&ParseFileDetour),
                kEntitiesParseFilePrologue, sizeof(kEntitiesParseFilePrologue),
                error))
            {
                g_originalParse = reinterpret_cast<ParseFileRoutine>(g_parseHook.Trampoline());
            }
            else
            {
                FORGE_LOG_ERROR("Cannot hook Entities_ParseFile: %s - a level cannot name a mod's "
                    "own kind", error.c_str());
            }
        }
        else if (parse != nullptr)
        {
            FORGE_LOG_ERROR("Entities_ParseFile does not open as expected (%s)", mismatch.c_str());
        }

        for (size_t at = sizeof(g_table.structSize); at + sizeof(void*) <= sizeof(g_table);
            at += sizeof(void*))
        {
            void* slot = nullptr;
            memcpy(&slot, reinterpret_cast<const uint8_t*>(&g_table) + at, sizeof(slot));

            if (slot == nullptr)
            {
                FORGE_LOG_ERROR("Ctrls interface leaves slot %d empty, so mods will never see it",
                    static_cast<int>(at / sizeof(void*)));
            }
        }

        FrameApi::AddInternalTick(&DropDeadClaims);

        FORGE_LOG_INFO("Ctrls interface installed");
        return true;
    }

    // Claims released via their owned tokens.
    void CtrlsApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_kinds.begin(); it != g_kinds.end();)
        {
            it = it->owner == owner ? g_kinds.erase(it) : it + 1;
        }

        for (auto it = g_made.begin(); it != g_made.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            if (it->generation != WorldApi::LevelGeneration())
            {
                it = g_made.erase(it);
                continue;
            }

            uint8_t* const room = it->room;
            uint8_t* const record = const_cast<uint8_t*>(static_cast<const uint8_t*>(it->record));

            // In current room: mark live entity dead too.
            if (RoomIsHere(room))
            {
                for (int at = 0;; ++at)
                {
                    uint8_t* const ctrl = CtrlAt(at);
                    if (ctrl == nullptr)
                    {
                        break;
                    }
                    if (RecordOf(ctrl) == record)
                    {
                        const uint8_t dead = 1;
                        memcpy(ctrl + GameAddress::kEntityDeadOffset, &dead, sizeof(dead));
                        break;
                    }
                }
            }

            ForgetRecord(room, record);
            ForgetIdsOf(record);

            const Doomed spent = { nullptr, record };
            g_doomed.push_back(spent);
            it = g_made.erase(it);
        }
    }

    void CtrlsApi::Uninstall()
    {
        while (!g_driving.empty())
        {
            GiveSlotBack(g_driving.back());
            g_driving.pop_back();
        }

        void** const spawn = static_cast<void**>(GameModule::Resolve(GameAddress::kSpawnPlacementSlot));
        if (spawn != nullptr && *spawn == reinterpret_cast<void*>(&SpawnThunk) && g_gameSpawn != nullptr)
        {
            *spawn = reinterpret_cast<void*>(g_gameSpawn);
        }

        g_parseHook.Uninstall();
        g_originalParse = nullptr;

        g_doomed.clear();
        g_known.clear();
        g_kinds.clear();
        g_borrowed.clear();
        g_table = AmodCtrlsApi{};
    }

    const AmodCtrlsApi* CtrlsApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
