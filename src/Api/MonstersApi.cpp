#include <windows.h>

#include "src/Api/MonstersApi.h"
#include "src/Api/ApiProperty.h"
#include "src/Api/ApiRead.h"
#include "src/Api/FrameApi.h"
#include "src/Api/MmdApi.h"
#include "src/Api/ModContext.h"
#include "src/Api/Placement.h"
#include "src/Api/TextureApi.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;
        using ApiProperty::Field;
        using ApiProperty::Kind;

        AmodMonstersApi g_table = {};

        int TypeTotal()
        {
            const int* const count = static_cast<const int*>(GameModule::Resolve(GameAddress::kMonsterResCount));
            return (count != nullptr && *count > 0 && *count < 0x1000) ? *count : 0;
        }

        uint8_t* TypeRecord(int index)
        {
            uint8_t** const table = static_cast<uint8_t**>(GameModule::Resolve(GameAddress::kMonsterResTable));

            if (table == nullptr || *table == nullptr || index < 0 || index >= TypeTotal())
            {
                return nullptr;
            }

            uint8_t* const record = *table + static_cast<size_t>(index) * GameAddress::kMonsterResStride;
            return Readable(record, GameAddress::kMonsterResStride) ? record : nullptr;
        }

        int TypeIndexOf(const char* name)
        {
            if (name == nullptr)
            {
                return -1;
            }

            const int total = TypeTotal();
            for (int index = 0; index < total; ++index)
            {
                const uint8_t* const record = TypeRecord(index);
                if (record != nullptr && strcmp(ReadText(record, GameAddress::kMonsterResNameOffset), name) == 0)
                {
                    return index;
                }
            }
            return -1;
        }

        uint8_t* TypeByName(const char* name)
        {
            const int index = TypeIndexOf(name);
            return index >= 0 ? TypeRecord(index) : nullptr;
        }

        void FillType(const uint8_t* record, AmodMonsterType& out)
        {
            out = AmodMonsterType{};
            out.structSize = sizeof(AmodMonsterType);
            out.name = ReadText(record, GameAddress::kMonsterResNameOffset);
            out.speed = Read<float>(record, GameAddress::kMonsterResSpeedOffset);
            out.speedUp = Read<float>(record, GameAddress::kMonsterResSpeedUpOffset);
            out.vertSpeed = Read<float>(record, GameAddress::kMonsterResVertSpeedOffset);

            for (int at = 0; at < 4; ++at)
            {
                const uintptr_t step = static_cast<uintptr_t>(at) * sizeof(float);
                out.partColor[at] = Read<float>(record, GameAddress::kMonsterResPartColorOffset + step);
                out.partColorHard[at] = Read<float>(record, GameAddress::kMonsterResPartColorHardOffset + step);
            }

            // Record holds model pointer; path is on the model (what Find takes).
            const uint8_t* const model = Read<const uint8_t*>(record, GameAddress::kMonsterResModelOffset);
            out.model = (model != nullptr && Readable(model, GameAddress::kModelNameOffset + 4) && Read<uint32_t>(model, GameAddress::kModelMagicOffset) 
                == GameAddress::kModelMagic)

                ? ReadText(model, GameAddress::kModelNameOffset)
                : "";
        }

        int __cdecl ApiGetTypeCount(AmodMod mod)
        {
            return Caller(mod) != nullptr ? TypeTotal() : 0;
        }

        int __cdecl ApiGetType(AmodMod mod, int index, AmodMonsterType* out)
        {
            const uint8_t* const record = Caller(mod) != nullptr ? TypeRecord(index) : nullptr;
            if (record == nullptr || out == nullptr)
            {
                return 0;
            }

            AmodMonsterType filled;
            FillType(record, filled);
            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiFindType(AmodMod mod, const char* name, AmodMonsterType* out)
        {
            return ApiGetType(mod, Caller(mod) != nullptr ? TypeIndexOf(name) : -1, out);
        }

        struct DefinedType
        {
            std::string name;
            ModContext* owner;
        };

        std::vector<DefinedType> g_definedTypes;

        // Type def deferred until the table exists.
        struct PendingType
        {
            ModContext* owner;
            std::string name;
            std::string basedOn;
            std::string texEasy;
            std::string texHard;
            std::string model;
            float speed;
            float speedUp;
            float vertSpeed;
        };

        std::vector<PendingType> g_pendingTypes;

        typedef uint8_t* (__stdcall* AppendRoutine)(uint8_t** table, int* count);

        uint32_t ResolveResource(uintptr_t slot, const char* name)
        {
            void** const at = static_cast<void**>(GameModule::Resolve(slot));
            const auto routine = at != nullptr
                ? reinterpret_cast<uint32_t(__cdecl*)(const char*)>(*at)
                : nullptr;

            return (routine != nullptr && name != nullptr && name[0] != '\0') ? routine(name) : 0;
        }

        bool WriteCode(void* at, const uint8_t* bytes, size_t length)
        {
            DWORD previous = 0;
            if (!VirtualProtect(at, length, PAGE_EXECUTE_READWRITE, &previous))
            {
                return false;
            }

            memcpy(at, bytes, length);

            DWORD restored = 0;
            VirtualProtect(at, length, previous, &restored);
            FlushInstructionCache(GetCurrentProcess(), at, length);
            return true;
        }

        bool g_jumpKeysOpen = false;

        // Jump-keys guard: NOP so parse accepts any name.
        void OpenJumpKeys()
        {
            uint8_t* const at = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kMonsterJumpKeysGuard));
            std::string mismatch;

            if (at == nullptr || !GameModule::VerifyBytes(at, GameAddress::kMonsterJumpKeysGuardBytes, 
                sizeof(GameAddress::kMonsterJumpKeysGuardBytes), mismatch))
            {
                FORGE_LOG_ERROR("The jump keys stay shut: 0x%08X does not hold what it was " "reversed as (%s)", 
                    static_cast<unsigned>(GameAddress::kMonsterJumpKeysGuard), mismatch.c_str());
                return;
            }

            static const uint8_t through[] = { 0x90, 0x90 };
            g_jumpKeysOpen = WriteCode(at, through, sizeof(through));
        }

        void CloseJumpKeys()
        {
            uint8_t* const at = g_jumpKeysOpen
                ? static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kMonsterJumpKeysGuard))
                : nullptr;

            if (at != nullptr)
            {
                WriteCode(at, GameAddress::kMonsterJumpKeysGuardBytes, sizeof(GameAddress::kMonsterJumpKeysGuardBytes));
            }
            g_jumpKeysOpen = false;
        }

        typedef void(__cdecl* FreeResourcesRoutine)();

        InlineHook g_freeHook;
        FreeResourcesRoutine g_originalFree = nullptr;

        void __cdecl FreeResourcesDetour()
        {
            static const uintptr_t shared[] = {
                GameAddress::kMonsterResTexEasyOffset, GameAddress::kMonsterResTexHardOffset, GameAddress::kMonsterResModelOffset, 
                GameAddress::kMonsterResAnimationOffset
            };

            for (const DefinedType& defined : g_definedTypes)
            {
                uint8_t* const record = TypeByName(defined.name.c_str());
                if (record == nullptr)
                {
                    continue;
                }

                for (const uintptr_t offset : shared)
                {
                    memset(record + offset, 0, sizeof(uint32_t));
                }
            }

            g_originalFree();
        }

        bool WatchTeardown()
        {
            if (g_originalFree != nullptr)
            {
                return true;
            }

            void* const target = GameSymbols::Address(GameSymbol::MonstersFreeResources);
            std::string mismatch;

            if (target == nullptr || !GameModule::VerifyBytes(target, kMonstersFreeResourcesPrologue, sizeof(kMonstersFreeResourcesPrologue), mismatch))
            {
                FORGE_LOG_ERROR("Monsters_FreeResources does not open as expected, so a monster of " "a mod's own cannot be given anything to look like");
                return false;
            }

            std::string error;
            if (!g_freeHook.Install(target, reinterpret_cast<void*>(&FreeResourcesDetour), kMonstersFreeResourcesPrologue, 
                sizeof(kMonstersFreeResourcesPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Monsters_FreeResources: %s", error.c_str());
                return false;
            }

            g_originalFree = reinterpret_cast<FreeResourcesRoutine>(g_freeHook.Trampoline());
            return true;
        }

        bool DefineType(const PendingType& wanted)
        {
            if (!WatchTeardown())
            {
                return false;
            }

            if (TypeIndexOf(wanted.name.c_str()) >= 0)
            {
                FORGE_LOG_ERROR("A monster of name '%s' is already known", wanted.name.c_str());
                return false;
            }

            const uint8_t* const source = TypeByName(wanted.basedOn.c_str());
            if (source == nullptr)
            {
                FORGE_LOG_ERROR("Monster '%s' is based on '%s', which monster_res.txt does not hold", wanted.name.c_str(), wanted.basedOn.c_str());
                return false;
            }

            uint8_t record[GameAddress::kMonsterResStride];
            memcpy(record, source, sizeof(record));

            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kMonsterResNameOffset), GameAddress::kSpawnNameLength, wanted.name.c_str(), _TRUNCATE);

            // Unnamed fields stay the base's. Teardown clears shared handles before free.

            const uint32_t model = ResolveResource(GameAddress::kResolveModelSlot, wanted.model.c_str());
            const uint32_t easy = ResolveResource(GameAddress::kResolveTextureSlot, wanted.texEasy.c_str());
            const uint32_t hard = ResolveResource(GameAddress::kResolveTextureSlot, wanted.texHard.c_str());

            if (model != 0)
            {
                memcpy(record + GameAddress::kMonsterResModelOffset, &model, sizeof(model));
            }
            if (easy != 0)
            {
                memcpy(record + GameAddress::kMonsterResTexEasyOffset, &easy, sizeof(easy));
            }
            if (hard != 0)
            {
                memcpy(record + GameAddress::kMonsterResTexHardOffset, &hard, sizeof(hard));
            }

            if (wanted.speed > 0.0f)
            {
                memcpy(record + GameAddress::kMonsterResSpeedOffset, &wanted.speed, sizeof(float));
            }
            if (wanted.speedUp > 0.0f)
            {
                memcpy(record + GameAddress::kMonsterResSpeedUpOffset, &wanted.speedUp, sizeof(float));
            }
            if (wanted.vertSpeed > 0.0f)
            {
                memcpy(record + GameAddress::kMonsterResVertSpeedOffset, &wanted.vertSpeed, sizeof(float));
            }

            uint8_t** const table = static_cast<uint8_t**>(GameModule::Resolve(GameAddress::kMonsterResTable));
            int* const count = static_cast<int*>(GameModule::Resolve(GameAddress::kMonsterResCount));
            const AppendRoutine append = reinterpret_cast<AppendRoutine>(GameModule::Resolve(GameAddress::kMonsterResAppend));

            if (table == nullptr || count == nullptr || append == nullptr)
            {
                return false;
            }

            uint8_t* const appended = append(table, count);
            if (appended == nullptr)
            {
                FORGE_LOG_ERROR("The monster table would not grow for '%s'", wanted.name.c_str());
                return false;
            }

            memcpy(appended, record, sizeof(record));

            DefinedType defined;
            defined.name = wanted.name;
            defined.owner = wanted.owner;
            g_definedTypes.push_back(defined);
            return true;
        }

        void __cdecl ApplyPendingTypes()
        {
            if (g_pendingTypes.empty() || TypeTotal() == 0)
            {
                return;
            }

            const std::vector<PendingType> pending = g_pendingTypes;
            g_pendingTypes.clear();

            for (const PendingType& wanted : pending)
            {
                DefineType(wanted);
            }
        }

        int __cdecl ApiDefineType(AmodMod mod, const AmodMonsterTypeDef* type)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || type == nullptr || type->structSize < sizeof(uint32_t))
            {
                return 0;
            }

            AmodMonsterTypeDef wanted = {};
            memcpy(&wanted, type, type->structSize < sizeof(wanted) ? type->structSize : sizeof(wanted));

            if (wanted.name == nullptr || wanted.name[0] == '\0' || wanted.basedOn == nullptr || wanted.basedOn[0] == '\0')
            {
                return 0;
            }

            PendingType held;
            held.owner = context;
            held.name = wanted.name;
            held.basedOn = wanted.basedOn;
            held.texEasy = wanted.texEasy != nullptr ? wanted.texEasy : "";
            held.texHard = wanted.texHard != nullptr ? wanted.texHard : "";
            held.model = wanted.model != nullptr ? wanted.model : "";
            held.speed = wanted.speed;
            held.speedUp = wanted.speedUp;
            held.vertSpeed = wanted.vertSpeed;

            if (TypeTotal() != 0)
            {
                return DefineType(held) ? 1 : 0;
            }

            g_pendingTypes.push_back(held);
            return 1;
        }

        bool OwnsType(ModContext* caller, const char* name)
        {
            for (const DefinedType& defined : g_definedTypes)
            {
                if (defined.owner == caller && defined.name == name)
                {
                    return true;
                }
            }
            return false;
        }

        constexpr Field kTypeFields[] = {
            { "name", GameAddress::kMonsterResNameOffset, Kind::Text, 0, 0, 0, false }, 
            { "tex_easy", GameAddress::kMonsterResTexEasyOffset, Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true }, 
            { "tex_hard", GameAddress::kMonsterResTexHardOffset, Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true }, 
            { "model", GameAddress::kMonsterResModelOffset, Kind::Resource, 0, 0, GameAddress::kResolveModelSlot, true }, 
            { "animation", GameAddress::kMonsterResAnimationOffset, Kind::Resource, 0, 0, GameAddress::kResolveAnimSlot, true }, 
            { "speed_up", GameAddress::kMonsterResSpeedUpOffset, Kind::Float, 0, 0, 0, true }, 
            { "speed", GameAddress::kMonsterResSpeedOffset, Kind::Float, 0, 0, 0, true }, 
            { "vert_speed", GameAddress::kMonsterResVertSpeedOffset, Kind::Float, 0, 0, 0, true }, 
            { "part_color", GameAddress::kMonsterResPartColorOffset, Kind::Floats, 4, 0, 0, true }, 
            { "part_color_hard", GameAddress::kMonsterResPartColorHardOffset, Kind::Floats, 4, 0, 0, true }, 
            { "jump", GameAddress::kMonsterResJumpOffset, Kind::Float, 0, 0, 0, true }, 
            { "jump_factor", GameAddress::kMonsterResJumpFactorOffset, Kind::Float, 0, 0, 0, true }, 
            { "fall", GameAddress::kMonsterResFallOffset, Kind::Float, 0, 0, 0, true }, };

        constexpr size_t kTypeFieldCount = sizeof(kTypeFields) / sizeof(Field);

        int __cdecl ApiTypePropertyCount(AmodMod mod)
        {
            return Caller(mod) != nullptr ? static_cast<int>(kTypeFieldCount) : 0;
        }

        const char* __cdecl ApiTypePropertyNameAt(AmodMod mod, int index)
        {
            return (Caller(mod) != nullptr && index >= 0 && index < static_cast<int>(kTypeFieldCount))
                ? kTypeFields[index].key
                : "";
        }

        int __cdecl ApiHasTypeProperty(AmodMod mod, const char* name, const char* key)
        {
            return (Caller(mod) != nullptr && TypeByName(name) != nullptr && ApiProperty::Find(kTypeFields, kTypeFieldCount, key) != nullptr)
                ? 1
                : 0;
        }

        int __cdecl ApiGetTypeProperty(AmodMod mod, const char* name, const char* key, AmodValue* value)
        {
            const uint8_t* const record = Caller(mod) != nullptr ? TypeByName(name) : nullptr;
            const Field* const field = ApiProperty::Find(kTypeFields, kTypeFieldCount, key);

            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "the game knows no monster called '%s'", name != nullptr ? name : "");
            }
            if (field == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "a monster definition has no property called '%s'", key != nullptr ? key : "");
            }
            return ApiProperty::Get(record, *field, value);
        }

        // KeepType defined with the rollback below.
        void KeepType(ModContext* owner, uint8_t* record);

        int __cdecl ApiSetTypeProperty(AmodMod mod, const char* name, const char* key, const AmodValue* value)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const record = caller != nullptr ? TypeByName(name) : nullptr;
            const Field* const field = ApiProperty::Find(kTypeFields, kTypeFieldCount, key);

            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "the game knows no monster called '%s'", name != nullptr ? name : "");
            }
            if (field == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "a monster definition has no property called '%s'", key != nullptr ? key : "");
            }
            if (value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetTypeProperty was given no value for '%s'", key);
            }

            // Game-owned type: snapshot whole record first, restore on unload.
            if (!OwnsType(caller, name))
            {
                KeepType(caller, record);
            }

            if (ApiProperty::Set(record, *field, *value) != 0)
            {
                return 1;
            }

            // Resource resolve returned 0 -- game will not load that file.
            return field->kind == Kind::Resource && value->kind == AMOD_VALUE_STRING
                ? Fail(mod, AMOD_FAIL_REFUSED, "the game will not load '%s' for '%s'",
                    value->text != nullptr ? value->text : "", key)
                : Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "'%s' would not take that value", key);
        }

        // Do not remove table rows; indices would shift for the rest of the frame.
        bool IsMonsterKind(int kind);

        uint8_t* MonsterPlacement(const uint8_t* entity)
        {
            if (entity == nullptr || !Readable(entity, GameAddress::kEntitySize) || entity[GameAddress::kEntityDeadOffset] != 0)
            {
                return nullptr;
            }

            uint8_t* const record = Read<uint8_t*>(entity, GameAddress::kEntitySpawnOffset);

            return (record != nullptr && Readable(record, GameAddress::kMonsterSpawnSize) && Read<int>(record, GameAddress::kSpawnKindOffset) 
                == GameAddress::kSpawnKindMonster && record[GameAddress::kMonsterSpawnLiveOffset] != 0)
                ? record
                : nullptr;
        }

        uint8_t* MonsterAt(int index)
        {
            if (index < 0)
            {
                return nullptr;
            }

            int seen = 0;
            for (uint8_t* entity = WorldApi::FirstLiveEntity(); entity != nullptr && Readable(entity, GameAddress::kEntitySize); entity = WorldApi::NextLiveEntity(entity))
            {
                if (MonsterPlacement(entity) == nullptr)
                {
                    continue;
                }
                if (seen == index)
                {
                    return entity;
                }
                ++seen;
            }
            return nullptr;
        }

        // IdOfMonster needs MonsterAt/Placement; defined with the id registry.
        uint32_t IdOfMonster(const uint8_t* entity);

        int __cdecl ApiGetCount(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            int count = 0;
            for (uint8_t* entity = WorldApi::FirstLiveEntity(); entity != nullptr && Readable(entity, GameAddress::kEntitySize); entity = WorldApi::NextLiveEntity(entity))
            {
                count += MonsterPlacement(entity) != nullptr ? 1 : 0;
            }
            return count;
        }

        void WriteFloat(uint8_t* record, uintptr_t offset, float value)
        {
            memcpy(record + offset, &value, sizeof(value));
        }

        const void* ThinkOf(const uint8_t* entity)
        {
            return Read<const void*>(entity, GameAddress::kEntityThinkOffset);
        }

        const char* MovementOf(const uint8_t* entity)
        {
            const void* const think = ThinkOf(entity);

            for (const GameAddress::MonsterMovement& movement : GameAddress::kMonsterMovements)
            {
                if (GameModule::Resolve(movement.think) == think)
                {
                    return movement.name;
                }
            }
            return "";
        }

        // Written somewhere nothing reads.
        bool SpeedInEntity(const uint8_t* entity)
        {
            const void* const think = ThinkOf(entity);

            for (const GameAddress::MonsterMovement& movement : GameAddress::kMonsterMovements)
            {
                if (GameModule::Resolve(movement.think) == think)
                {
                    return movement.speedInEntity;
                }
            }
            return false;
        }

        float SpeedOf(const uint8_t*, const uint8_t* record)
        {
            return Read<float>(record, GameAddress::kMonsterSpawnSpeedOffset);
        }

        void SetSpeed(uint8_t* entity, uint8_t* record, float speed)
        {
            WriteFloat(record, GameAddress::kMonsterSpawnSpeedOffset, speed);

            if (SpeedInEntity(entity))
            {
                WriteFloat(entity, GameAddress::kMonsterEntitySpeedOffset, speed);
            }
        }

        bool HasStateCycle(const uint8_t* entity)
        {
            const void* const think = ThinkOf(entity);

            return think == GameModule::Resolve(GameAddress::kMonsterThinkMummy) || think == GameModule::Resolve(GameAddress::kMonsterThinkMummy2);
        }

        uint8_t* PlacementAt(int roomIndex, int index);
        int IndexOfMonster(const uint8_t* entity);
        uint8_t* EntityOf(const uint8_t* record);

        int PlacedIndexOf(const uint8_t* record)
        {
            for (int at = 0; ; ++at)
            {
                const uint8_t* const one = PlacementAt(-1, at);
                if (one == nullptr)
                {
                    return -1;
                }
                if (one == record)
                {
                    return at;
                }
            }
        }

        int __cdecl ApiGet(AmodMod mod, int index, AmodMonster* out)
        {
            const uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            const uint8_t* const record = entity != nullptr ? MonsterPlacement(entity) : nullptr;
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no monster %d; the room has %d", index, ApiGetCount(mod));
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Get was given nowhere to put the monster");
            }

            const int level = Read<int>(record, GameAddress::kMonsterSpawnLevelOffset);

            AmodMonster filled = {};
            filled.structSize = sizeof(filled);
            filled.name = NameAt(record, GameAddress::kMonsterSpawnNameOffset)
                ? ReadText(record, GameAddress::kMonsterSpawnNameOffset)
                : "";
            filled.tag = NameAt(record, GameAddress::kMonsterSpawnTagOffset)
                ? ReadText(record, GameAddress::kMonsterSpawnTagOffset)
                : "";
            filled.moves = MovementOf(entity);
            filled.level = level;
            filled.facing = Read<int>(entity, GameAddress::kMonsterEntityFacingOffset);
            filled.speed = SpeedOf(entity, record);
            filled.hasState = HasStateCycle(entity) ? 1 : 0;
            filled.state = filled.hasState != 0
                ? Read<int>(record, GameAddress::kMonsterSpawnStateOffset)
                : 0;
            filled.timeLeft = filled.hasState != 0
                ? Read<float>(record, GameAddress::kMonsterSpawnTimerOffset)
                : 0.0f;
            filled.isHard = (level == 1 || level == 3) ? 1 : 0;
            filled.isDecor = record[GameAddress::kMonsterSpawnDecorOffset] != 0 ? 1 : 0;
            filled.id = IdOfMonster(entity);
            filled.index = IndexOfMonster(entity);
            filled.placed = PlacedIndexOf(record);

            for (int axis = 0; axis < 3; ++axis)
            {
                filled.position[axis] = Read<float>(entity, GameAddress::kEntityPositionOffset + static_cast<uintptr_t>(axis) * sizeof(float));
            }

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        typedef int(__cdecl* MoveRoutine)(void* state, int dirX, int dirY, int dirZ, float delta, int flags);

        int __cdecl ApiMove(AmodMod mod, int index, int dirX, int dirY, int dirZ)
        {
            uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            const MoveRoutine step = reinterpret_cast<MoveRoutine>(GameModule::Resolve(GameAddress::kEntityMoveStep));

            if (entity == nullptr || step == nullptr)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "there is no monster %d to move", index);
                return -1;
            }

            const float delta = Read<float>(entity, GameAddress::kEntityDeltaOffset);
            return step(entity + GameAddress::kEntityMoveStateOffset, dirX, dirY, dirZ, delta, 0) != 0
                ? 1
                : 0;
        }

        typedef void(__cdecl* KillRoutine)(void* entity);

        int __cdecl ApiKill(AmodMod mod, int index)
        {
            uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            const uint8_t* const record = entity != nullptr ? MonsterPlacement(entity) : nullptr;
            const KillRoutine kill = reinterpret_cast<KillRoutine>(GameSymbols::Address(GameSymbol::MonsterKill));

            if (record == nullptr || kill == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no monster %d to kill", index);
            }

            const bool counts = record[GameAddress::kMonsterSpawnDecorOffset] == 0;
            kill(entity);

            // What caused the kill.
            int* const killed = counts ? static_cast<int*>(GameModule::Resolve(GameAddress::kMonstersKilled))
                : nullptr;

            if (killed != nullptr)
            {
                *killed += 1;
            }
            return 1;
        }

        constexpr Field kMonsterFields[] = {
            { "spawn_speed", GameAddress::kMonsterSpawnSpeedOffset, Kind::Float, 0, 0, 0, true }, 
            { "state", GameAddress::kMonsterSpawnStateOffset, Kind::Int, 0, 0, 0, true }, 
            { "time_left", GameAddress::kMonsterSpawnTimerOffset, Kind::Float, 0, 0, 0, true }, 
            { "level", GameAddress::kMonsterSpawnLevelOffset, Kind::Int, 0, 0, 0, true }, 
            { "dir", GameAddress::kMonsterSpawnDirOffset, Kind::Int, 0, 0, 0, true }, 
            { "radius", GameAddress::kMonsterSpawnRadiusOffset, Kind::Float, 0, 0, 0, true }, 
            { "axis", GameAddress::kMonsterSpawnAxisOffset, Kind::Floats, 3, 0, 0, true }, 
            { "phase", GameAddress::kMonsterSpawnPhaseOffset, Kind::Float, 0, 0, 0, true }, 
            { "attack_dist", GameAddress::kMonsterSpawnAttackDistOffset, Kind::Float, 0, 0, 0, true }, 
            { "attack_time", GameAddress::kMonsterSpawnAttackTimeOffset, Kind::Floats, 2, 0, 0, true }, 
            { "bluff_time", GameAddress::kMonsterSpawnBluffTimeOffset, Kind::Floats, 2, 0, 0, true }, 
            { "walk_time", GameAddress::kMonsterSpawnWalkTimeOffset, Kind::Floats, 2, 0, 0, true }, 
            { "walk_wait", GameAddress::kMonsterSpawnWalkWaitOffset, Kind::Floats, 2, 0, 0, true }, 
            { "walk_state", GameAddress::kMonsterSpawnWalkStateOffset, Kind::Int, 0, 0, 0, true }, 
            { "walk_left", GameAddress::kMonsterSpawnWalkTimerOffset, Kind::Float, 0, 0, 0, true }, 
            { "sit_time", GameAddress::kMonsterSpawnSitTimeOffset, Kind::Floats, 2, 0, 0, true }, 
            { "wait_time", GameAddress::kMonsterSpawnWaitTimeOffset, Kind::Floats, 2, 0, 0, true }, 
            { "pos_lerp", GameAddress::kMonsterSpawnPosLerpOffset, Kind::Float, 0, 0, 0, true }, 
            { "decor", GameAddress::kMonsterSpawnDecorOffset, Kind::Int, 0, 0, 0, false },

            { "live", GameAddress::kMonsterSpawnLiveOffset, Kind::Int, 0, 0, 0, true }, 
            { "tag", GameAddress::kMonsterSpawnTagOffset, Kind::Text, 0, 0, 0, false }, };

        constexpr size_t kMonsterFieldCount = sizeof(kMonsterFields) / sizeof(Field);

        constexpr Field kEntityFields[] = {
            { "facing",  GameAddress::kMonsterEntityFacingOffset,   Kind::Int,   0, 0, 0, true },

            // $level 1 or 2, written by spawn.
            { "hard",    GameAddress::kMonsterEntityHardOffset,     Kind::Float, 0, 0, 0, true },

            // Live speed and the low/top pair walking states use.
            { "speed", GameAddress::kMonsterEntitySpeedOffset, Kind::Float, 0, 0, 0, true }, 
            { "speed_min", GameAddress::kMonsterEntitySpeedLowOffset, Kind::Float, 0, 0, 0, true }, 
            { "speed_max", GameAddress::kMonsterEntitySpeedTopOffset, Kind::Float, 0, 0, 0, true },

            // Walk body; accel is left 0 by the game on every monster.
            { "accel", GameAddress::kEntityMoveAccelOffset, Kind::Float, 0, 0, 0, true }, 
            { "gravity", GameAddress::kEntityMoveGravityOffset, Kind::Float, 0, 0, 0, true }, 
            { "fall_speed", GameAddress::kEntityMoveFallSpeedOffset, Kind::Float, 0, 0, 0, true }, 
            { "velocity", GameAddress::kEntityMoveVelocityOffset, Kind::Float, 0, 0, 0, true }, 
            { "vert_velocity", GameAddress::kEntityMoveVertVelocityOffset, Kind::Float, 0, 0, 0, true }, 
            { "height", GameAddress::kEntityMoveHeightOffset, Kind::Float, 0, 0, 0, true }, 
            { "move_state", GameAddress::kEntityMoveStateNowOffset, Kind::Int, 0, 0, 0, true }, 
            { "x", GameAddress::kEntityPositionOffset, Kind::Float, 0, 0, 0, true },
            { "y", GameAddress::kEntityPositionOffset + 0x04, Kind::Float, 0, 0, 0, true },
            { "z", GameAddress::kEntityPositionOffset + 0x08, Kind::Float, 0, 0, 0, true },

            // What the draw turns the model by. A game model is drawn facing +x at yaw 0, and the
            // spawn writes pi for $dir -1, so a mod's model has to be built facing +x to agree.
            { "pitch", GameAddress::kEntityPitchOffset, Kind::Float, 0, 0, 0, true },
            { "yaw", GameAddress::kEntityYawOffset, Kind::Float, 0, 0, 0, true },
            { "roll", GameAddress::kEntityRollOffset, Kind::Float, 0, 0, 0, true },

            // With "turns" on, the game walks pitch/yaw to these instead of the mod snapping them.
            { "pitch_to", GameAddress::kEntityWantedPitchOffset, Kind::Float, 0, 0, 0, true },
            { "yaw_to", GameAddress::kEntityWantedYawOffset, Kind::Float, 0, 0, 0, true },
            { "turns", GameAddress::kEntityFlagsOffset, Kind::Flag, 0, GameAddress::kEntityTurnsFlag, 0, true },

            // Three floats the draw multiplies by; a zero on any axis draws as one.
            { "scale", GameAddress::kEntityScaleOffset, Kind::Floats, 3, 0, 0, true },

            // Per-entity look without touching the type.
            { "model", GameAddress::kEntityModelOffset, Kind::Resource, 0, 0, GameAddress::kResolveModelSlot, true }, 
            { "texture", GameAddress::kEntityTextureOffset, Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true },

            // One texture per draw; second material = second model.
            { "model2", GameAddress::kEntitySecondModelOffset, Kind::Resource, 0, 0, GameAddress::kResolveModelSlot, true }, 
            { "texture2", GameAddress::kEntitySecondTextureOffset, Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true },

            // Blend pass; alpha only counts there.
            { "blend", GameAddress::kEntityFlagsOffset, Kind::Flag, 0, GameAddress::kEntityBlendFlag, 0, true }, };

        constexpr size_t kEntityFieldCount = sizeof(kEntityFields) / sizeof(Field);

        constexpr Field kSpeedInEntity = { "speed", GameAddress::kMonsterEntitySpeedOffset, Kind::Float, 0, 0, 0, true };
        constexpr Field kSpeedInRecord = { "speed", GameAddress::kMonsterSpawnSpeedOffset, Kind::Float, 0, 0, 0, true };

        bool SpeedKey(const char* key)
        {
            return key != nullptr && strcmp(key, "speed") == 0;
        }

        int __cdecl ApiPropertyCount(AmodMod mod)
        {
            return Caller(mod) != nullptr
                ? static_cast<int>(kMonsterFieldCount + kEntityFieldCount) + 1
                : 0;
        }

        const char* __cdecl ApiPropertyNameAt(AmodMod mod, int index)
        {
            const int total = ApiPropertyCount(mod);
            const int own = static_cast<int>(kMonsterFieldCount);

            if (index < 0 || index >= total)
            {
                return "";
            }
            if (index == total - 1)
            {
                return kSpeedInEntity.key;
            }
            return index < own ? kMonsterFields[index].key : kEntityFields[index - own].key;
        }

        int __cdecl ApiHasProperty(AmodMod mod, int index, const char* key)
        {
            return (Caller(mod) != nullptr && MonsterAt(index) != nullptr && (SpeedKey(key) || ApiProperty::Find(kMonsterFields, 
                kMonsterFieldCount, key) != nullptr || ApiProperty::Find(kEntityFields, kEntityFieldCount, key) != nullptr))
                ? 1
                : 0;
        }

        int __cdecl ApiGetProperty(AmodMod mod, int index, const char* key, AmodValue* value)
        {
            const uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            const uint8_t* const record = entity != nullptr ? MonsterPlacement(entity) : nullptr;
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no monster %d to read '%s' off", index, key != nullptr ? key : "");
            }
            if (value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetProperty was given nowhere to put '%s'", key != nullptr ? key : "");
            }

            if (SpeedKey(key))
            {
                return ApiProperty::Get(record, kSpeedInRecord, value);
            }

            const Field* const mine = ApiProperty::Find(kEntityFields, kEntityFieldCount, key);
            if (mine != nullptr)
            {
                return ApiProperty::Get(entity, *mine, value);
            }

            const Field* const field = ApiProperty::Find(kMonsterFields, kMonsterFieldCount, key);
            return field != nullptr
                ? ApiProperty::Get(record, *field, value)
                : Fail(mod, AMOD_FAIL_NO_SUCH, "a monster has no property called '%s'", key != nullptr ? key : "");
        }

        int __cdecl ApiSetProperty(AmodMod mod, int index, const char* key, const AmodValue* value)
        {
            uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            uint8_t* const record = entity != nullptr ? MonsterPlacement(entity) : nullptr;
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no monster %d to write '%s' on", index, key != nullptr ? key : "");
            }
            if (value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetProperty was given no value for '%s'", key != nullptr ? key : "");
            }

            if (SpeedKey(key))
            {
                if (value->kind != AMOD_VALUE_FLOAT || value->count < 1)
                {
                    return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "'speed' is one number, and was given %s", value->kind 
                        == AMOD_VALUE_FLOAT ? "none" : "something else");
                }
                SetSpeed(entity, record, value->numbers[0]);
                return 1;
            }

            const Field* const mine = ApiProperty::Find(kEntityFields, kEntityFieldCount, key);
            if (mine != nullptr)
            {
                return ApiProperty::Set(entity, *mine, *value) != 0
                    ? 1
                    : Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "'%s' would not take that value", key);
            }

            const Field* const field = ApiProperty::Find(kMonsterFields, kMonsterFieldCount, key);
            if (field == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "a monster has no property called '%s'", key != nullptr ? key : "");
            }
            return ApiProperty::Set(record, *field, *value) != 0
                ? 1
                : Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "'%s' would not take that value", key);
        }

        // Type the monster was built from.

        uint8_t* PlacementAt(int roomIndex, int index)
        {
            uint8_t* const room = roomIndex < 0 ? WorldApi::CurrentRoom()
                : WorldApi::RoomAt(roomIndex);

            if (room == nullptr || !Readable(room, GameAddress::kRoomSpawnCountOffset + sizeof(int)) || index < 0)
            {
                return nullptr;
            }

            uint8_t** const list = Read<uint8_t**>(room, GameAddress::kRoomSpawnListOffset);
            const int count = Read<int>(room, GameAddress::kRoomSpawnCountOffset);

            if (list == nullptr || count <= 0 || !Readable(list, static_cast<size_t>(count) * sizeof(void*)))
            {
                return nullptr;
            }

            int seen = 0;
            for (int at = 0; at < count; ++at)
            {
                uint8_t* const record = list[at];
                if (record == nullptr || !Readable(record, GameAddress::kMonsterSpawnSize) || Read<int>(record, GameAddress::kSpawnKindOffset) 
                    != GameAddress::kSpawnKindMonster)
                {
                    continue;
                }
                if (seen == index)
                {
                    return record;
                }
                ++seen;
            }
            return nullptr;
        }

        bool AllDigits(const char* text)
        {
            if (text == nullptr || *text == '\0')
            {
                return false;
            }
            for (const char* at = text; *at != '\0'; ++at)
            {
                if (*at < '0' || *at > '9')
                {
                    return false;
                }
            }
            return true;
        }

        // Parse name_col_row from the right (name may contain underscores).
        bool ParseKey(const char* key, char* name, size_t size, int& column, int& row)
        {
            column = -1;
            row = -1;

            if (key == nullptr || *key == '\0')
            {
                return false;
            }
            strncpy_s(name, size, key, _TRUNCATE);

            char* const rowAt = strrchr(name, '_');
            if (rowAt == nullptr || !AllDigits(rowAt + 1))
            {
                return true;
            }

            *rowAt = '\0';
            char* const columnAt = strrchr(name, '_');
            if (columnAt == nullptr || !AllDigits(columnAt + 1))
            {
                *rowAt = '_';
                return true;
            }

            column = atoi(columnAt + 1);
            row = atoi(rowAt + 1);
            *columnAt = '\0';
            return true;
        }

        bool PlacementIs(const uint8_t* record, const char* name, int column, int row)
        {
            if (record == nullptr || !NameAt(record, GameAddress::kMonsterSpawnNameOffset) || strcmp(ReadText(record, 
                GameAddress::kMonsterSpawnNameOffset), name) != 0)
            {
                return false;
            }
            if (column < 0)
            {
                return true;
            }

            const float wantX = column * GameAddress::kCellToWorld + GameAddress::kCellOriginX + GameAddress::kCellCentre;
            const float wantY = GameAddress::kCellOriginY - row * GameAddress::kCellToWorld - GameAddress::kCellCentre;
            const float half = GameAddress::kCellToWorld * 0.5f;

            return fabsf(Read<float>(record, GameAddress::kMonsterSpawnPositionOffset) - wantX) < half && fabsf(Read<float>(record, 
                GameAddress::kMonsterSpawnPositionOffset + 0x04) - wantY) < half;
        }

        AmodMonsterId __cdecl ApiFind(AmodMod mod, const char* key)
        {
            char name[64];
            int column = 0;
            int row = 0;

            if (Caller(mod) == nullptr || !ParseKey(key, name, sizeof(name), column, row))
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Find wants a name and a cell, like 'spider_15_7'");
                return AMOD_MONSTER_NONE;
            }

            for (uint8_t* entity = WorldApi::FirstLiveEntity(); entity != nullptr && Readable(entity, GameAddress::kEntitySize); entity = WorldApi::NextLiveEntity(entity))
            {
                if (PlacementIs(MonsterPlacement(entity), name, column, row))
                {
                    return IdOfMonster(entity);
                }
            }

            Fail(mod, AMOD_FAIL_NO_SUCH, column < 0 ? "no '%s' is standing in this room"
                : "no '%s' is standing on %d,%d in this room",
                name, column, row);
            return AMOD_MONSTER_NONE;
        }

        int __cdecl ApiFindPlaced(AmodMod mod, int roomIndex, const char* key)
        {
            char name[64];
            int column = 0;
            int row = 0;

            if (Caller(mod) == nullptr || !ParseKey(key, name, sizeof(name), column, row))
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "FindPlaced wants a name and a cell, like 'spider_15_7'");
                return -1;
            }

            for (int index = 0; ; ++index)
            {
                const uint8_t* const record = PlacementAt(roomIndex, index);
                if (record == nullptr)
                {
                    break;
                }
                if (PlacementIs(record, name, column, row))
                {
                    return index;
                }
            }

            Fail(mod, AMOD_FAIL_NO_SUCH, column < 0 ? "room %d was given no '%s'"
                : "room %d was given no '%s' on %d,%d",
                roomIndex, name, column, row);
            return -1;
        }

        int __cdecl ApiGetPlacedCount(AmodMod mod, int roomIndex)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            int count = 0;
            while (PlacementAt(roomIndex, count) != nullptr)
            {
                ++count;
            }
            return count;
        }

        int __cdecl ApiGetPlaced(AmodMod mod, int roomIndex, int index, AmodMonster* out)
        {
            const uint8_t* const record = Caller(mod) != nullptr ? PlacementAt(roomIndex, index)
                : nullptr;
            if (record == nullptr || out == nullptr)
            {
                return 0;
            }

            const int level = Read<int>(record, GameAddress::kMonsterSpawnLevelOffset);

            AmodMonster filled = {};
            filled.structSize = sizeof(filled);
            filled.name = NameAt(record, GameAddress::kMonsterSpawnNameOffset)
                ? ReadText(record, GameAddress::kMonsterSpawnNameOffset)
                : "";
            filled.tag = NameAt(record, GameAddress::kMonsterSpawnTagOffset)
                ? ReadText(record, GameAddress::kMonsterSpawnTagOffset)
                : "";
            filled.moves = "";
            filled.level = level;
            filled.facing = Read<int>(record, GameAddress::kMonsterSpawnDirOffset) < 0 ? -1 : 1;
            filled.speed = Read<float>(record, GameAddress::kMonsterSpawnSpeedOffset);
            filled.isHard = (level == 1 || level == 3) ? 1 : 0;
            filled.isDecor = record[GameAddress::kMonsterSpawnDecorOffset] != 0 ? 1 : 0;
            filled.placed = index;

            // Live index only in the current room.
            const uint8_t* const standing = EntityOf(record);
            filled.index = standing != nullptr ? IndexOfMonster(standing) : -1;

            for (int axis = 0; axis < 3; ++axis)
            {
                filled.position[axis] = Read<float>(record, GameAddress::kMonsterSpawnPositionOffset + static_cast<uintptr_t>(axis) * sizeof(float));
            }

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiGetPlacedProperty(AmodMod mod, int roomIndex, int index, const char* key, AmodValue* value)
        {
            const uint8_t* const record = Caller(mod) != nullptr ? PlacementAt(roomIndex, index)
                : nullptr;
            const Field* const field = ApiProperty::Find(kMonsterFields, kMonsterFieldCount, key);

            return (record != nullptr && field != nullptr) ? ApiProperty::Get(record, *field, value)
                : 0;
        }

        int __cdecl ApiSetPlacedProperty(AmodMod mod, int roomIndex, int index, const char* key, const AmodValue* value)
        {
            uint8_t* const record = Caller(mod) != nullptr ? PlacementAt(roomIndex, index) : nullptr;
            const Field* const field = ApiProperty::Find(kMonsterFields, kMonsterFieldCount, key);

            return (record != nullptr && field != nullptr && value != nullptr)
                ? ApiProperty::Set(record, *field, *value)
                : 0;
        }

        int __cdecl ApiGetKilledCount(AmodMod mod)
        {
            const int* const killed = static_cast<const int*>(GameModule::Resolve(GameAddress::kMonstersKilled));
            return (Caller(mod) != nullptr && killed != nullptr) ? *killed : 0;
        }

        // Handler may refuse one placement; others pass untouched.

        struct DefinedKind
        {
            ModContext* owner;
            std::string name;
            std::string movesLike;
            void* borrowedThink; // to hand a monster back when the mod goes
            AmodMonsterThinkHandler behaviour;
            void* user;
        };

        std::vector<DefinedKind> g_kinds;

        DefinedKind* KindOf(const char* name)
        {
            for (DefinedKind& kind : g_kinds)
            {
                if (kind.name == name)
                {
                    return &kind;
                }
            }
            return nullptr;
        }

        struct Behaviour
        {
            ModContext* owner;
            std::string name;
            AmodMonsterActHandler handler;
            void* user;

            void* gameThink;
        };

        std::vector<Behaviour> g_behaviours;

        Behaviour* BehaviourFor(const char* name)
        {
            for (Behaviour& one : g_behaviours)
            {
                if (name != nullptr && one.name == name)
                {
                    return &one;
                }
            }
            return nullptr;
        }

        // Mod-owned monster kind.
        struct MonsterKind
        {
            ModContext* owner;
            int kind;
            std::string name;
            AmodMonsterKindHandler handler;
            void* user;

            uint8_t* standIn;
        };

        std::vector<MonsterKind> g_ownKinds;

        typedef void* (__cdecl* CloneRoutine)(const void* source, size_t size);
        typedef void(__cdecl* AppendPlacementRoutine)(void* room, void* record);
        typedef void(__cdecl* SpawnOneRoutine)(void* record);

        MonsterKind* OwnKindOf(int kind)
        {
            for (MonsterKind& one : g_ownKinds)
            {
                if (one.kind == kind)
                {
                    return &one;
                }
            }
            return nullptr;
        }

        bool IsMonsterKind(int kind)
        {
            return OwnKindOf(kind) != nullptr;
        }

        // Records belonging to a mod kind.
        struct KindRecord
        {
            const void* record;
            int kind;
            uint32_t generation;

            // actsLike think, resolved once at build.
            void* speciesTouch;
        };

        std::vector<KindRecord> g_kindRecords;

        int KindOfRecord(const void* record)
        {
            for (const KindRecord& one : g_kindRecords)
            {
                if (record != nullptr && one.record == record)
                {
                    return one.kind;
                }
            }
            return 0;
        }

        int OwnKindByName(const char* name)
        {
            for (const MonsterKind& one : g_ownKinds)
            {
                if (name != nullptr && one.name == name)
                {
                    return one.kind;
                }
            }
            return 0;
        }

        struct StandIn
        {
            std::string name;
            std::vector<uint8_t> row;
        };

        std::vector<StandIn> g_standIns;

        uint8_t* StandInFor(const char* name)
        {
            for (StandIn& one : g_standIns)
            {
                if (name != nullptr && one.name == name)
                {
                    return one.row.data();
                }
            }
            return nullptr;
        }

        typedef uint8_t* (__cdecl* MonsterResFindRoutine)(const char* name);

        InlineHook g_findHook;
        MonsterResFindRoutine g_originalFind = nullptr;

        uint8_t* __cdecl FindDetour(const char* name)
        {
            uint8_t* const mine = StandInFor(name);
            if (mine != nullptr)
            {
                return mine;
            }

            return g_originalFind != nullptr ? g_originalFind(name) : nullptr;
        }

        bool InstallFindHook()
        {
            if (g_findHook.IsInstalled())
            {
                return true;
            }

            void* const target = GameModule::Resolve(GameAddress::kMonsterResFind);
            std::string mismatch;

            if (target == nullptr || !GameModule::VerifyBytes(target, kMonsterResFindPrologue, sizeof(kMonsterResFindPrologue), mismatch))
            {
                FORGE_LOG_ERROR("MonsterRes_FindByName does not open as expected, so a room file " "cannot name a kind of a mod's own");
                return false;
            }

            std::string error;
            if (!g_findHook.Install(target, reinterpret_cast<void*>(&FindDetour), kMonsterResFindPrologue, sizeof(kMonsterResFindPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook MonsterRes_FindByName: %s", error.c_str());
                return false;
            }

            g_originalFind = reinterpret_cast<MonsterResFindRoutine>(g_findHook.Trampoline());
            return true;
        }

        // One borrowed monster (not every monster of a name).
        struct BorrowedMonster
        {
            ModContext* owner;
            const void* record;
            AmodMonsterActHandler handler;
            void* user;
            void* gameThink;
            uint32_t generation;
        };

        std::vector<BorrowedMonster> g_borrowedMonsters;

        BorrowedMonster* BorrowedFor(const void* record)
        {
            for (BorrowedMonster& one : g_borrowedMonsters)
            {
                if (record != nullptr && one.record == record)
                {
                    return &one;
                }
            }
            return nullptr;
        }

        // Drop borrows from previous level generation.
        void DropDeadBorrows()
        {
            const uint32_t generation = WorldApi::LevelGeneration();
            for (auto it = g_borrowedMonsters.begin(); it != g_borrowedMonsters.end();)
            {
                it = it->generation != generation ? g_borrowedMonsters.erase(it) : it + 1;
            }

            // Drop kind records from previous level generation.
            for (auto it = g_kindRecords.begin(); it != g_kindRecords.end();)
            {
                it = it->generation != generation ? g_kindRecords.erase(it) : it + 1;
            }
        }

        typedef void(__cdecl* SpawnPlacementRoutine)(void* record);

        InlineHook g_spawnHook;
        SpawnPlacementRoutine g_originalSpawn = nullptr;

        // Creator of one of the ten stock movements.
        SpawnPlacementRoutine CreatorFor(const std::string& movesLike)
        {
            const uint8_t* const table = static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kMonsterSpawnTable));
            if (table == nullptr || !Readable(table, GameAddress::kMonsterSpawnTableCount * 2 * sizeof(void*)))
            {
                return nullptr;
            }

            for (size_t at = 0; at < GameAddress::kMonsterSpawnTableCount; ++at)
            {
                const char* const name = Read<const char*>(table, at * 2 * sizeof(void*));
                const void* const creator = Read<const void*>(table, at * 2 * sizeof(void*) + sizeof(void*));

                if (name != nullptr && Readable(name, 1) && movesLike == name)
                {
                    return reinterpret_cast<SpawnPlacementRoutine>(const_cast<void*>(creator));
                }
            }
            return nullptr;
        }

        // Parser block into the placement it just appended.

        typedef void(__cdecl* ParseSpawnRoutine)(void* room, void* grid, void* parser);
        typedef int(__cdecl* FindKeyRoutine)(void* parser, const char* key);
        typedef int(__cdecl* ReadFloat4Routine)(void* parser, float* out);

        InlineHook g_parseHook;
        ParseSpawnRoutine g_originalParse = nullptr;

        uint8_t* LastPlacement(uint8_t* room)
        {
            if (room == nullptr || !Readable(room, GameAddress::kRoomSpawnCountOffset + sizeof(int)))
            {
                return nullptr;
            }

            uint8_t** const list = Read<uint8_t**>(room, GameAddress::kRoomSpawnListOffset);
            const int count = Read<int>(room, GameAddress::kRoomSpawnCountOffset);

            if (list == nullptr || count <= 0 || !Readable(list, static_cast<size_t>(count) * sizeof(void*)))
            {
                return nullptr;
            }

            uint8_t* const record = list[count - 1];
            return (record != nullptr && Readable(record, GameAddress::kMonsterSpawnSize) && Read<int>(record, GameAddress::kSpawnKindOffset) 
                == GameAddress::kSpawnKindMonster)
                ? record
                : nullptr;
        }

        bool ReadPos(void* parser, float* pos)
        {
            const FindKeyRoutine find = reinterpret_cast<FindKeyRoutine>(GameModule::Resolve(GameAddress::kParserFindKey));
            const ReadFloat4Routine read = reinterpret_cast<ReadFloat4Routine>(GameModule::Resolve(GameAddress::kParserReadFloat4));

            return find != nullptr && read != nullptr && find(parser, "pos") != 0 && read(parser, pos) != 0;
        }

        // May walk from where it was put.
        void ApplyPatrolRange(uint8_t* record, const float* pos)
        {
            const float left = pos[2] < 0.0f ? -pos[2] : pos[2];
            const float right = pos[3] < 0.0f ? -pos[3] : pos[3];

            WriteFloat(record, 0x5C, left * GameAddress::kCellToWorld);
            WriteFloat(record, 0x60, right * GameAddress::kCellToWorld);
        }

        // Place from midpoint, half distance, along unit direction.
        void ApplyArc(uint8_t* record, const float* pos)
        {
            const float second[3] = {
                pos[2] * GameAddress::kCellToWorld + GameAddress::kCellOriginX + GameAddress::kCellCentre, GameAddress::kCellOriginY - pos[3] * 
                GameAddress::kCellToWorld - GameAddress::kCellCentre, GameAddress::kMonsterCellZ
            };

            float axis[3];
            float length = 0.0f;

            for (int at = 0; at < 3; ++at)
            {
                axis[at] = Read<float>(record, GameAddress::kMonsterSpawnPositionOffset + static_cast<uintptr_t>(at) * sizeof(float)) - second[at];
                length += axis[at] * axis[at];
            }

            length = sqrtf(length);
            if (length <= 0.0f)
            {
                return;
            }

            const float radius = length * 0.5f;
            for (int at = 0; at < 3; ++at)
            {
                axis[at] /= length;
                WriteFloat(record, GameAddress::kMonsterSpawnAxisOffset + static_cast<uintptr_t>(at) * sizeof(float), axis[at]);
                WriteFloat(record, GameAddress::kMonsterSpawnPositionOffset + static_cast<uintptr_t>(at) * sizeof(float), second[at] + axis[at] * radius);
            }
            WriteFloat(record, GameAddress::kMonsterSpawnRadiusOffset, radius);
        }

        void ApplyMovementFields(uint8_t* record, const std::string& movesLike, void* parser)
        {
            const bool patrols = movesLike == "skull" || movesLike == "j_skull" || movesLike == "mummy" || movesLike == "mummy2";
            const bool arcs = movesLike == "bat";

            if (!patrols && !arcs)
            {
                return; // the spider, the scorpion and the snakes read nothing extra
            }

            float pos[4] = {};
            if (!ReadPos(parser, pos))
            {
                return;
            }

            if (patrols)
            {
                ApplyPatrolRange(record, pos);
                return;
            }
            ApplyArc(record, pos);
        }

        void __cdecl ParseDetour(void* room, void* grid, void* parser)
        {
            g_originalParse(room, grid, parser);

            if (g_kinds.empty() || room == nullptr || parser == nullptr)
            {
                return;
            }

            uint8_t* const record = LastPlacement(static_cast<uint8_t*>(room));
            if (record == nullptr)
            {
                return;
            }

            const char* const name = ReadText(record, GameAddress::kMonsterSpawnNameOffset);
            for (const DefinedKind& kind : g_kinds)
            {
                if (kind.name == name)
                {
                    ApplyMovementFields(record, kind.movesLike, parser);
                    return;
                }
            }
        }

        bool InstallParseHook()
        {
            void* const target = GameSymbols::Address(GameSymbol::MonsterParseSpawn);
            if (target == nullptr)
            {
                return false;
            }

            std::string mismatch;
            if (!GameModule::VerifyBytes(target, kMonsterParseSpawnPrologue, sizeof(kMonsterParseSpawnPrologue), mismatch))
            {
                FORGE_LOG_ERROR("Monster_ParseSpawn does not open as expected (%s), so a kind of a " "mod's own will spawn without the fields its movement needs"
                    , mismatch.c_str());
                return false;
            }

            std::string error;
            if (!g_parseHook.Install(target, reinterpret_cast<void*>(&ParseDetour), kMonsterParseSpawnPrologue, sizeof(kMonsterParseSpawnPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Monster_ParseSpawn: %s", error.c_str());
                return false;
            }

            g_originalParse = reinterpret_cast<ParseSpawnRoutine>(g_parseHook.Trampoline());
            return true;
        }

        uint8_t* EntityOf(const uint8_t* record)
        {
            for (uint8_t* entity = WorldApi::FirstLiveEntity(); entity != nullptr && Readable(entity, GameAddress::kEntitySize); entity = WorldApi::NextLiveEntity(entity))
            {
                if (Read<const uint8_t*>(entity, GameAddress::kEntitySpawnOffset) == record)
                {
                    return entity;
                }
            }
            return nullptr;
        }

        int IndexOfMonster(const uint8_t* entity)
        {
            int seen = 0;
            for (uint8_t* at = WorldApi::FirstLiveEntity(); at != nullptr && Readable(at, GameAddress::kEntitySize); at = WorldApi::NextLiveEntity(at))
            {
                if (MonsterPlacement(at) == nullptr)
                {
                    continue;
                }
                if (at == entity)
                {
                    return seen;
                }
                ++seen;
            }
            return -1;
        }

        struct TrackedMonster
        {
            uint32_t id;
            uint8_t* entity;
            uint8_t* record;
        };

        std::vector<TrackedMonster> g_tracked;
        uint32_t g_nextMonsterId = 1;

        void SyncIds()
        {
            std::vector<TrackedMonster> present;
            present.reserve(g_tracked.size());

            for (uint8_t* entity = WorldApi::FirstLiveEntity(); entity != nullptr && Readable(entity, GameAddress::kEntitySize); entity = WorldApi::NextLiveEntity(entity))
            {
                uint8_t* const record = MonsterPlacement(entity);
                if (record == nullptr)
                {
                    continue;
                }

                TrackedMonster kept = { 0, entity, record };
                for (const TrackedMonster& known : g_tracked)
                {
                    if (known.entity == entity && known.record == record)
                    {
                        kept.id = known.id;
                        break;
                    }
                }

                // Ids never reused; a kept id means that monster or nothing.
                kept.id = kept.id != 0 ? kept.id : g_nextMonsterId++;
                present.push_back(kept);
            }

            g_tracked.swap(present);
        }

        uint32_t IdOfMonster(const uint8_t* entity)
        {
            SyncIds();
            for (const TrackedMonster& known : g_tracked)
            {
                if (known.entity == entity)
                {
                    return known.id;
                }
            }
            return AMOD_MONSTER_NONE;
        }

        uint8_t* MonsterById(AmodMonsterId id)
        {
            if (id == AMOD_MONSTER_NONE)
            {
                return nullptr;
            }

            SyncIds();
            for (const TrackedMonster& known : g_tracked)
            {
                if (known.id == id)
                {
                    return known.entity;
                }
            }
            return nullptr;
        }

        // Map a draw back to its monster.
        uint8_t* EntityOfNode(uint32_t item)
        {
            uint8_t* const scene = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kScene));
            uint8_t* const node = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(item));

            if (scene == nullptr || node == nullptr || !Readable(scene + GameAddress::kSceneNodeCountOffset, sizeof(int)))
            {
                return nullptr;
            }

            uint8_t* const pool = scene + GameAddress::kSceneNodePoolOffset;
            const int filled = Read<int>(scene, GameAddress::kSceneNodeCountOffset);
            const size_t span = node >= pool ? static_cast<size_t>(node - pool) : 0;

            if (node < pool || span % GameAddress::kSceneNodeStride != 0 || filled <= 0 || filled > 
                static_cast<int>(GameAddress::kSceneNodeCapacity) || span / GameAddress::kSceneNodeStride >= static_cast<size_t>(filled))
            {
                return nullptr;
            }

            const size_t wanted = span / GameAddress::kSceneNodeStride;
            uint8_t** const head = static_cast<uint8_t**>(GameModule::Resolve(GameAddress::kEntities + GameAddress::kEntitiesLiveOffset));

            size_t seen = 0;
            for (uint8_t* entity = head != nullptr ? *head : nullptr;
                entity != nullptr && Readable(entity, GameAddress::kEntitySize);
                entity = Read<uint8_t*>(entity, GameAddress::kEntityNextOffset))
            {
                if ((Read<uint32_t>(entity, GameAddress::kEntityFlagsOffset) & GameAddress::kEntityHiddenFlag) != 0 
                    || Read<uintptr_t>(entity, GameAddress::kEntityModelOffset) == 0)
                {
                    continue;
                }

                if (seen == wanted)
                {
                    return entity;
                }
                ++seen;

                // Second model is the node behind the first, same entity.
                if (Read<uintptr_t>(entity, GameAddress::kEntitySecondModelOffset) != 0)
                {
                    if (seen == wanted)
                    {
                        return entity;
                    }
                    ++seen;
                }
            }
            return nullptr;
        }

        AmodMonsterId __cdecl ApiIdOfDraw(AmodMod mod, uint32_t item)
        {
            if (Caller(mod) == nullptr)
            {
                return AMOD_MONSTER_NONE;
            }

            uint8_t* const entity = EntityOfNode(item);
            if (entity == nullptr)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "that draw is not one of the world's entities - a room's own mesh, or a mod's own");
                return AMOD_MONSTER_NONE;
            }
            if (MonsterPlacement(entity) == nullptr)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "that draw belongs to something that is not a monster");
                return AMOD_MONSTER_NONE;
            }
            return IdOfMonster(entity);
        }

        AmodMonsterId __cdecl ApiIdAt(AmodMod mod, int index)
        {
            uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            if (entity == nullptr)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "there is no monster %d in this room", index);
                return AMOD_MONSTER_NONE;
            }
            return IdOfMonster(entity);
        }

        int __cdecl ApiIndexOf(AmodMod mod, AmodMonsterId id)
        {
            if (Caller(mod) == nullptr)
            {
                return -1;
            }

            const uint8_t* const entity = MonsterById(id);
            if (entity == nullptr)
            {
                Fail(mod, id == AMOD_MONSTER_NONE ? AMOD_FAIL_BAD_ARGUMENT : AMOD_FAIL_NO_SUCH, "monster %u is not in this room any more", id);
                return -1;
            }
            return IndexOfMonster(entity);
        }

        // Restore borrowed movement rather than a no-op think.
        void __cdecl ThinkThunk(void* self)
        {
            uint8_t* const entity = static_cast<uint8_t*>(self);
            const uint8_t* const record = MonsterPlacement(entity);
            const char* const name = record != nullptr ? ReadText(record, GameAddress::kMonsterSpawnNameOffset) : nullptr;

            DefinedKind* const kind = name != nullptr ? KindOf(name) : nullptr;
            Behaviour* const hung = name != nullptr ? BehaviourFor(name) : nullptr;
            BorrowedMonster* const mine = BorrowedFor(record);

            MonsterKind* const own = record != nullptr ? OwnKindOf(KindOfRecord(record)) : nullptr;

            if (kind == nullptr && hung == nullptr && mine == nullptr && own == nullptr)
            {
                void* const nothing = nullptr;
                memcpy(entity + GameAddress::kEntityThinkOffset, &nothing, sizeof(nothing));
                return;
            }

            // No handler left; restore original think.
            if (own == nullptr && mine == nullptr && hung == nullptr && kind->behaviour == nullptr)
            {
                void* const back = kind->borrowedThink;
                memcpy(entity + GameAddress::kEntityThinkOffset, &back, sizeof(back));

                if (back != nullptr)
                {
                    reinterpret_cast<void(__cdecl*)(void*)>(back)(self);
                }
                return;
            }

            AmodMonsterThinkInfo info = {};
            info.structSize = sizeof(info);
            info.index = IndexOfMonster(entity);
            info.id = IdOfMonster(entity);
            info.delta = Read<float>(entity, GameAddress::kEntityDeltaOffset);
            info.facing = Read<int>(entity, GameAddress::kMonsterEntityFacingOffset);

            for (int axis = 0; axis < 3; ++axis)
            {
                info.position[axis] = Read<float>(entity, GameAddress::kEntityPositionOffset + static_cast<uintptr_t>(axis) * sizeof(float));
            }

            if (mine != nullptr)
            {
                const int verdict = mine->handler != nullptr
                    ? mine->handler(&info, mine->user) : AMOD_MONSTER_ALSO_GAME;

                if (verdict != AMOD_MONSTER_ALSO_GAME)
                {
                    return;
                }

                if (own == nullptr)
                {
                    if (mine->gameThink != nullptr)
                    {
                        reinterpret_cast<void(__cdecl*)(void*)>(mine->gameThink)(self);
                    }
                    return;
                }
            }

            if (own != nullptr)
            {
                AmodMonsterKindInfo asking = {};
                asking.structSize = sizeof(asking);
                asking.event = AMOD_MONSTER_EVENT_UPDATE;
                asking.kind = own->kind;
                asking.record = record;
                asking.think = &info;

                if (own->handler != nullptr)
                {
                    own->handler(&asking, own->user);
                }
                return;
            }

            if (hung != nullptr)
            {
                const int verdict = hung->handler != nullptr
                    ? hung->handler(&info, hung->user) : AMOD_MONSTER_ALSO_GAME;

                if (verdict == AMOD_MONSTER_ALSO_GAME && hung->gameThink != nullptr)
                {
                    reinterpret_cast<void(__cdecl*)(void*)>(hung->gameThink)(self);
                }
                return;
            }

            kind->behaviour(&info, kind->user);
        }

        void CarryOverSpeed(const uint8_t* record)
        {
            uint8_t* const entity = EntityOf(record);
            if (entity == nullptr)
            {
                return;
            }

            // Zero speed would stop a monster the placement leaves alone.
            const float wanted = Read<float>(record, GameAddress::kMonsterSpawnSpeedOffset);

            if (wanted != 0.0f && SpeedInEntity(entity))
            {
                WriteFloat(entity, GameAddress::kMonsterEntitySpeedOffset, wanted);
            }

            // Kind ends up looking the way the placement asked.
            const int dir = Read<int>(record, GameAddress::kMonsterSpawnDirOffset) < 0 ? -1 : 1;
            memcpy(entity + GameAddress::kMonsterEntityFacingOffset, &dir, sizeof(dir));
        }

        struct SpawnHandler
        {
            uint32_t id;
            ModContext* owner;
            AmodMonsterSpawnHandler handler;
            void* user;
        };

        std::vector<SpawnHandler> g_spawnHandlers;
        uint32_t g_nextSpawnHandlerId = 1;

        // Index in room list, or -1 if not the walked-in room.
        int IndexOfPlacement(const uint8_t* record)
        {
            for (int index = 0; ; ++index)
            {
                const uint8_t* const at = PlacementAt(-1, index);
                if (at == nullptr)
                {
                    return -1;
                }
                if (at == record)
                {
                    return index;
                }
            }
        }

        // After game build; monster already has index and id.
        void TellSpawned(const uint8_t* record)
        {
            uint8_t* const entity = !g_spawnHandlers.empty() ? EntityOf(record) : nullptr;
            if (entity == nullptr)
            {
                return;
            }

            AmodMonsterSpawnInfo info = {};
            info.structSize = sizeof(info);
            info.id = IdOfMonster(entity);
            info.index = IndexOfMonster(entity);
            info.placed = IndexOfPlacement(record);
            info.name = NameAt(record, GameAddress::kMonsterSpawnNameOffset)
                ? ReadText(record, GameAddress::kMonsterSpawnNameOffset)
                : "";
            info.tag = NameAt(record, GameAddress::kMonsterSpawnTagOffset)
                ? ReadText(record, GameAddress::kMonsterSpawnTagOffset)
                : "";
            info.level = Read<int>(record, GameAddress::kMonsterSpawnLevelOffset);
            info.isDecor = record[GameAddress::kMonsterSpawnDecorOffset] != 0 ? 1 : 0;

            for (int axis = 0; axis < 3; ++axis)
            {
                info.position[axis] = Read<float>(entity, GameAddress::kEntityPositionOffset + static_cast<uintptr_t>(axis) * sizeof(float));
            }

            const std::vector<SpawnHandler> snapshot = g_spawnHandlers;
            for (const SpawnHandler& entry : snapshot)
            {
                if (entry.handler != nullptr)
                {
                    entry.handler(&info, entry.user);
                }
            }
        }

        struct PlaceHandler
        {
            uint32_t id;
            ModContext* owner;
            AmodMonsterPlaceHandler handler;
            void* user;
        };

        std::vector<PlaceHandler> g_placeHandlers;
        uint32_t g_nextPlaceHandlerId = 1;

        int RoomIndexNow()
        {
            const uint8_t* const here = WorldApi::ParsingRoom() != nullptr ? WorldApi::ParsingRoom()
                : WorldApi::CurrentRoom();
            const int total = here != nullptr ? WorldApi::RoomTotal() : 0;

            for (int at = 0; at < total; ++at)
            {
                if (WorldApi::RoomAt(at) == here)
                {
                    return at;
                }
            }
            return -1;
        }

        // False when a handler refuses the placement.
        bool AskPlaceHandlers(const uint8_t* record)
        {
            if (g_placeHandlers.empty())
            {
                return true;
            }

            AmodMonsterPlaceInfo info = {};
            info.structSize = sizeof(info);
            info.name = ReadText(record, GameAddress::kMonsterSpawnNameOffset);
            info.tag = ReadText(record, GameAddress::kMonsterSpawnTagOffset);
            info.level = Read<int>(record, GameAddress::kMonsterSpawnLevelOffset);
            info.isDecor = Read<int>(record, GameAddress::kMonsterSpawnDecorOffset) != 0 ? 1 : 0;
            info.roomIndex = RoomIndexNow();
            memcpy(info.position, record + GameAddress::kMonsterSpawnPositionOffset, sizeof(info.position));

            const std::vector<PlaceHandler> snapshot = g_placeHandlers;
            for (const PlaceHandler& entry : snapshot)
            {
                if (entry.handler != nullptr && entry.handler(&info, entry.user) == 0)
                {
                    return false;
                }
            }
            return true;
        }

        void TakeThinkFor(const uint8_t* record)
        {
            Behaviour* const hung = g_behaviours.empty()
                ? nullptr : BehaviourFor(ReadText(record, GameAddress::kMonsterSpawnNameOffset));
            BorrowedMonster* const one = g_borrowedMonsters.empty() ? nullptr : BorrowedFor(record);

            uint8_t* const entity = (hung != nullptr || one != nullptr) ? EntityOf(record) : nullptr;
            if (entity == nullptr)
            {
                return;
            }

            void* const was = Read<void*>(entity, GameAddress::kEntityThinkOffset);
            if (was != reinterpret_cast<void*>(&ThinkThunk))
            {
                if (hung != nullptr)
                {
                    hung->gameThink = was;
                }
                if (one != nullptr)
                {
                    one->gameThink = was;
                }
            }

            void* const mine = reinterpret_cast<void*>(&ThinkThunk);
            memcpy(entity + GameAddress::kEntityThinkOffset, &mine, sizeof(mine));
        }

        void BuildMonsterKind(void* room, void* record, int kind, void* user);

        void __cdecl SpawnDetour(void* placement)
        {
            uint8_t* const record = static_cast<uint8_t*>(placement);

            // Every monster build, not only mod kinds.
            const bool monster = record != nullptr && Readable(record, GameAddress::kMonsterSpawnSize) && Read<int>(record, 
                GameAddress::kSpawnKindOffset) == GameAddress::kSpawnKindMonster && record[GameAddress::kMonsterSpawnLiveOffset] != 0;

            if (monster && !AskPlaceHandlers(record))
            {
                return;
            }

            // Mod-kind records are built by the kind; game is never asked.
            int owned = monster ? KindOfRecord(record) : 0;
            if (monster && owned == 0)
            {
                owned = OwnKindByName(ReadText(record, GameAddress::kMonsterSpawnNameOffset));
                if (owned != 0)
                {
                    const KindRecord marked = { record, owned, WorldApi::LevelGeneration(), nullptr };
                    g_kindRecords.push_back(marked);
                }
            }

            if (owned != 0)
            {
                BuildMonsterKind(nullptr, record, owned, nullptr);
                TellSpawned(record);
                return;
            }

            DefinedKind* const kind = (monster && !g_kinds.empty()) ? KindOf(ReadText(record, GameAddress::kMonsterSpawnNameOffset))
                : nullptr;

            if (kind == nullptr)
            {
                g_originalSpawn(placement);
                if (monster)
                {
                    // For every monster, not only mod names.
                    CarryOverSpeed(record);

                    TakeThinkFor(record);
                    TellSpawned(record);
                }
                return;
            }

            const SpawnPlacementRoutine creator = CreatorFor(kind->movesLike);
            if (creator == nullptr)
            {
                return;
            }

            creator(placement);
            CarryOverSpeed(record);

            // Own behaviour takes the think slot instead.
            if (kind->behaviour != nullptr)
            {
                uint8_t* const entity = EntityOf(record);
                if (entity != nullptr)
                {
                    void* const mine = reinterpret_cast<void*>(&ThinkThunk);
                    memcpy(entity + GameAddress::kEntityThinkOffset, &mine, sizeof(mine));
                }
            }

            // Last so handler sees final think already installed.
            TellSpawned(record);
        }

        bool InstallSpawnHook()
        {
            // Shared helper used by more than one path.
            if (g_spawnHook.IsInstalled())
            {
                return true;
            }

            void* const target = GameSymbols::Address(GameSymbol::MonsterSpawnPlacement);
            if (target == nullptr)
            {
                return false;
            }

            std::string mismatch;
            if (!GameModule::VerifyBytes(target, kMonsterSpawnPlacementPrologue, sizeof(kMonsterSpawnPlacementPrologue), mismatch))
            {
                FORGE_LOG_ERROR("Monster_SpawnPlacement does not open as expected (%s), so a mod " "cannot teach it a name of its own", mismatch.c_str());
                return false;
            }

            std::string error;
            if (!g_spawnHook.Install(target, reinterpret_cast<void*>(&SpawnDetour), kMonsterSpawnPlacementPrologue, 
                sizeof(kMonsterSpawnPlacementPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Monster_SpawnPlacement: %s", error.c_str());
                return false;
            }

            g_originalSpawn = reinterpret_cast<SpawnPlacementRoutine>(g_spawnHook.Trampoline());
            return true;
        }

        int __cdecl ApiDefineKind(AmodMod mod, const char* name, const char* movesLike)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || g_originalSpawn == nullptr || name == nullptr || name[0] == '\0' || movesLike == nullptr || movesLike[0] == '\0')
            {
                return 0;
            }

            if (CreatorFor(movesLike) == nullptr)
            {
                FORGE_LOG_ERROR("'%s' is not one of the ten kinds of movement the game has, so " "'%s' has nothing to move like", movesLike, name);
                return 0;
            }

            for (const DefinedKind& kind : g_kinds)
            {
                if (kind.name == name)
                {
                    return 0;
                }
            }

            DefinedKind kind;
            kind.owner = context;
            kind.name = name;
            kind.movesLike = movesLike;
            kind.borrowedThink = nullptr;
            kind.behaviour = nullptr;
            kind.user = nullptr;

            for (const GameAddress::MonsterMovement& movement : GameAddress::kMonsterMovements)
            {
                if (kind.movesLike == movement.name)
                {
                    kind.borrowedThink = GameModule::Resolve(movement.think);
                }
            }

            g_kinds.push_back(kind);
            return 1;
        }

        void ReleasePlaceHandler(void* payload)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload));
            for (auto it = g_placeHandlers.begin(); it != g_placeHandlers.end(); ++it)
            {
                if (it->id == id)
                {
                    g_placeHandlers.erase(it);
                    return;
                }
            }
        }

        AmodToken __cdecl ApiAddPlaceHandler(AmodMod mod, AmodMonsterPlaceHandler handler, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || handler == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }

            PlaceHandler entry;
            entry.id = g_nextPlaceHandlerId++;
            entry.owner = context;
            entry.handler = handler;
            entry.user = user;
            g_placeHandlers.push_back(entry);

            return context->owned.Add(&ReleasePlaceHandler, reinterpret_cast<void*>(static_cast<uintptr_t>(entry.id)));
        }

        void __cdecl ApiRemovePlaceHandler(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Caller(mod);
            if (context != nullptr)
            {
                context->owned.Remove(token);
            }
        }

        int __cdecl ApiRemovePlaced(AmodMod mod, int roomIndex, int index)
        {
            uint8_t* const record = Caller(mod) != nullptr ? PlacementAt(roomIndex, index) : nullptr;
            if (record == nullptr || record[GameAddress::kMonsterSpawnLiveOffset] == 0)
            {
                return 0;
            }

            record[GameAddress::kMonsterSpawnLiveOffset] = 0;

            uint8_t* const entity = EntityOf(record);
            if (entity != nullptr)
            {
                entity[GameAddress::kEntityDeadOffset] = 1;
            }
            return 1;
        }

        KindRecord* RecordEntryOf(const void* record)
        {
            for (KindRecord& one : g_kindRecords)
            {
                if (record != nullptr && one.record == record)
                {
                    return &one;
                }
            }
            return nullptr;
        }

        void __cdecl KindTouchThunk(void* self, void* other, int side)
        {
            uint8_t* const entity = static_cast<uint8_t*>(self);
            const uint8_t* const record = MonsterPlacement(entity);
            KindRecord* const entry = record != nullptr ? RecordEntryOf(record) : nullptr;
            MonsterKind* const own = entry != nullptr ? OwnKindOf(entry->kind) : nullptr;

            if (entry == nullptr)
            {
                return;
            }

            int verdict = AMOD_MONSTER_ALSO_GAME;

            if (own != nullptr && own->handler != nullptr)
            {
                const uint8_t* const met = static_cast<const uint8_t*>(other);

                AmodMonsterTouchInfo touching = {};
                touching.structSize = sizeof(touching);
                touching.index = IndexOfMonster(entity);
                touching.id = IdOfMonster(entity);
                touching.name = ReadText(record, GameAddress::kMonsterSpawnNameOffset);
                touching.tag = ReadText(record, GameAddress::kMonsterSpawnTagOffset);
                touching.side = side;
                touching.toucherKind = met != nullptr && Readable(met, sizeof(int))
                    ? Read<int>(met, GameAddress::kEntityKindOffset) : 0;
                touching.isPlayer = touching.toucherKind == GameAddress::kEntityKindPlayer ? 1 : 0;

                for (int axis = 0; axis < 3; ++axis)
                {
                    touching.position[axis] = Read<float>(entity, GameAddress::kEntityPositionOffset + static_cast<uintptr_t>(axis) * sizeof(float));
                }

                AmodMonsterKindInfo asking = {};
                asking.structSize = sizeof(asking);
                asking.event = AMOD_MONSTER_EVENT_TOUCH;
                asking.kind = entry->kind;
                asking.record = record;
                asking.touch = &touching;

                verdict = own->handler(&asking, own->user);
            }

            if (verdict == AMOD_MONSTER_ALSO_GAME && entry->speciesTouch != nullptr)
            {
                reinterpret_cast<void(__cdecl*)(void*, void*, int)>(entry->speciesTouch)(self, other, side);
            }
        }

        uint8_t* BuildKindEntity(const AmodMonsterBody& body, uint8_t* record)
        {
            typedef uint8_t* (__cdecl* EntityAllocRoutine)();
            const EntityAllocRoutine alloc = reinterpret_cast<EntityAllocRoutine>(GameModule::Resolve(GameAddress::kEntitiesAlloc));

            uint8_t* const entity = alloc != nullptr ? alloc() : nullptr;
            if (entity == nullptr)
            {
                return nullptr;
            }

            const int kind = GameAddress::kEntityKindMonster;
            memcpy(entity + GameAddress::kEntityKindOffset, &kind, sizeof(kind));

            strncpy_s(reinterpret_cast<char*>(entity + GameAddress::kEntityNameOffset), GameAddress::kSpawnNameLength, body.name 
                != nullptr ? body.name : "", _TRUNCATE);
            strncpy_s(reinterpret_cast<char*>(entity + GameAddress::kMonsterEntityTagOffset), GameAddress::kSpawnNameLength, body.tag 
                != nullptr ? body.tag : "", _TRUNCATE);

            for (int axis = 0; axis < 3; ++axis)
            {
                const uintptr_t at = static_cast<uintptr_t>(axis) * sizeof(float);
                memcpy(entity + GameAddress::kEntityPositionOffset + at, &body.position[axis], sizeof(float));
                memcpy(entity + GameAddress::kEntityPreviousPositionOffset + at, &body.position[axis], sizeof(float));
            }

            const int facing = body.facing != 0 ? body.facing : 1;
            memcpy(entity + GameAddress::kMonsterEntityFacingOffset, &facing, sizeof(facing));

            const bool ownHit = body.hitBox.structSize != 0 && (body.hitBox.min[0] != body.hitBox.max[0] || body.hitBox.min[1] != body.hitBox.max[1]);

            const AmodBox& hit = ownHit ? body.hitBox : body.box;

            struct { uintptr_t at; const AmodBox* shape; } boxes[] = {
                { GameAddress::kEntityFrameBoxOffset, &body.box }, { GameAddress::kMonsterEntityObjBoxOffset, &hit }
            };

            for (const auto& one : boxes)
            {
                uint8_t* const box = entity + one.at;
                const int reports = 1;
                memcpy(box + GameAddress::kRoomBoxKindOffset, &reports, sizeof(reports));
                memcpy(box + GameAddress::kBoxMinXOffset, one.shape->min, sizeof(one.shape->min));
                memcpy(box + GameAddress::kBoxMaxXOffset, one.shape->max, sizeof(one.shape->max));
            }

            // Animation source, if any.
            const char* const from = body.animatedLike != nullptr ? body.animatedLike : body.actsLike;
            const uint8_t* const source = from != nullptr ? TypeByName(from) : nullptr;

            void* model = MmdApi::ModelFor(body.modelId);
            void* texture = TextureApi::TextureFor(body.textureId);

            // Animation walks model frames; nothing without a model.
            if (source != nullptr && model == nullptr)
            {
                model = Read<void*>(source, GameAddress::kMonsterResModelOffset);

                if (texture == nullptr)
                {
                    texture = Read<void*>(source, GameAddress::kMonsterResTexEasyOffset);
                }
            }

            memcpy(entity + GameAddress::kEntityModelOffset, &model, sizeof(model));
            memcpy(entity + GameAddress::kEntityTextureOffset, &texture, sizeof(texture));

            uint32_t flags = 0x1010u;
            if (model == nullptr || body.isHidden != 0)
            {
                flags |= GameAddress::kEntityHiddenFlag;
            }
            memcpy(entity + GameAddress::kEntityFlagsOffset, &flags, sizeof(flags));

            // Rest of species spawn setup before it is a live monster.
            const int allowed = 0x43;
            memcpy(entity + GameAddress::kEntityMoveFlagsOffset, &allowed, sizeof(allowed));
            entity[GameAddress::kEntityCollidesOffset] = 1;

            // Box-follows-anim switch; every shipped monster sets it.
            entity[GameAddress::kMonsterEntityBoxFollowsAnimOffset] = body.keepHitBox != 0 ? 0 : 1;

            const uint8_t touchable = 1;
            memcpy(entity + GameAddress::kEntityTouchableOffset, &touchable, sizeof(touchable));
            memcpy(entity + GameAddress::kEntitySpawnOffset, &record, sizeof(record));

            void* const think = reinterpret_cast<void*>(&ThinkThunk);
            memcpy(entity + GameAddress::kEntityThinkOffset, &think, sizeof(think));

            void* const animation = body.animationId != 0
                ? MmdApi::AnimationFor(body.animationId)
                : (source != nullptr ? Read<void*>(source, GameAddress::kMonsterResAnimationOffset)
                    : nullptr);

            if (animation != nullptr)
            {
                memcpy(entity + GameAddress::kEntityAnimationOffset, &animation, sizeof(animation));

                typedef void(__cdecl* AnimPlayRoutine)(void* animation, const char* frameset, void* state, const float* position, int flag);
                const AnimPlayRoutine play = reinterpret_cast<AnimPlayRoutine>(GameModule::Resolve(GameAddress::kAnimPlay));

                if (play != nullptr)
                {
                    play(animation, body.frameset != nullptr ? body.frameset : "move", entity + GameAddress::kEntityAnimStateOffset,
                        reinterpret_cast<const float*>(entity + GameAddress::kEntityPositionOffset), 0);
                }
            }

            // Die handler.
            void* const die = GameSymbols::Address(GameSymbol::MonsterKill);
            if (die != nullptr)
            {
                memcpy(entity + GameAddress::kEntityDieOffset, &die, sizeof(die));
            }

            // Touch: kind first, then actsLike.
            void* const touch = reinterpret_cast<void*>(&KindTouchThunk);
            memcpy(entity + GameAddress::kEntityTouchHandlerOffset, &touch, sizeof(touch));
            return entity;
        }

        // Placement build callback for a registered kind.
        void BuildMonsterKind(void* room, void* record, int kind, void*)
        {
            (void)room;

            MonsterKind* const mine = OwnKindOf(kind);
            uint8_t* const at = static_cast<uint8_t*>(record);

            if (mine == nullptr || mine->handler == nullptr || at == nullptr || !Readable(at, GameAddress::kMonsterSpawnSize) || 
                at[GameAddress::kMonsterSpawnLiveOffset] == 0)
            {
                return;
            }

            AmodMonsterBody body = {};
            body.structSize = sizeof(body);
            body.name = ReadText(at, GameAddress::kMonsterSpawnNameOffset);
            body.tag = ReadText(at, GameAddress::kMonsterSpawnTagOffset);
            body.facing = Read<int>(at, GameAddress::kMonsterSpawnDirOffset);
            body.box.structSize = sizeof(body.box);
            body.hitBox.structSize = sizeof(body.hitBox);
            memcpy(body.position, at + GameAddress::kMonsterSpawnPositionOffset, sizeof(body.position));

            AmodMonsterKindInfo asking = {};
            asking.structSize = sizeof(asking);
            asking.event = AMOD_MONSTER_EVENT_BUILD;
            asking.kind = kind;
            asking.record = at;
            asking.body = &body;

            if (mine->handler(&asking, mine->user) == 0)
            {
                return;
            }

            KindRecord* const entry = RecordEntryOf(at);
            if (entry != nullptr)
            {
                entry->speciesTouch = nullptr;
            }

            if (body.actsLike != nullptr && entry != nullptr)
            {
                const uint8_t* const type = TypeByName(body.actsLike);

                for (const MonsterTouchSite& site : kMonsterTouchSites)
                {
                    if (strcmp(site.actsLike, body.actsLike) == 0)
                    {
                        entry->speciesTouch = GameModule::Resolve(site.address);
                        break;
                    }
                }

                if (type != nullptr)
                {
                    memcpy(at + GameAddress::kMonsterSpawnTypeOffset, &type, sizeof(type));
                }

                if (type == nullptr || entry->speciesTouch == nullptr)
                {
                    entry->speciesTouch = nullptr;
                    FORGE_LOG_WARN("Monsters: kind %d acts like '%s', which is no species the game " "has both a type and a touch for, so contact falls to the kind " "alone", kind, body.actsLike);
                }
            }

            if (BuildKindEntity(body, at) == nullptr)
            {
                FORGE_LOG_ERROR("Monsters: the entity pool would not give kind %d one to stand on", kind);
            }
        }

        int __cdecl ApiBorrowMonster(AmodMod mod, int index, AmodMonsterActHandler handler, void* user)
        {
            ModContext* const context = Caller(mod);
            uint8_t* const entity = context != nullptr ? MonsterAt(index) : nullptr;
            const uint8_t* const record = entity != nullptr ? MonsterPlacement(entity) : nullptr;

            if (record == nullptr || handler == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no monster %d to take over", index);
            }

            BorrowedMonster* found = BorrowedFor(record);
            if (found == nullptr)
            {
                g_borrowedMonsters.push_back(BorrowedMonster{});
                found = &g_borrowedMonsters.back();
                found->record = record;
            }

            found->owner = context;
            found->handler = handler;
            found->user = user;
            found->generation = WorldApi::LevelGeneration();

            void* const was = Read<void*>(entity, GameAddress::kEntityThinkOffset);
            if (was != reinterpret_cast<void*>(&ThinkThunk))
            {
                found->gameThink = was;
            }

            void* const mine = reinterpret_cast<void*>(&ThinkThunk);
            memcpy(entity + GameAddress::kEntityThinkOffset, &mine, sizeof(mine));
            return 1;
        }

        // Restore think on whatever stands on that record now.
        void HandBackMonster(const BorrowedMonster& one)
        {
            uint8_t* const entity = EntityOf(static_cast<const uint8_t*>(one.record));
            if (entity == nullptr)
            {
                return;
            }

            if (Read<void*>(entity, GameAddress::kEntityThinkOffset) == reinterpret_cast<void*>(&ThinkThunk))
            {
                memcpy(entity + GameAddress::kEntityThinkOffset, &one.gameThink, sizeof(void*));
            }
        }

        int __cdecl ApiReturnMonster(AmodMod mod, int index)
        {
            ModContext* const context = Caller(mod);
            uint8_t* const entity = context != nullptr ? MonsterAt(index) : nullptr;
            const uint8_t* const record = entity != nullptr ? MonsterPlacement(entity) : nullptr;
            BorrowedMonster* const found = record != nullptr ? BorrowedFor(record) : nullptr;

            if (found == nullptr || found->owner != context)
            {
                return 0;
            }

            HandBackMonster(*found);

            for (auto it = g_borrowedMonsters.begin(); it != g_borrowedMonsters.end(); ++it)
            {
                if (it->record == record)
                {
                    g_borrowedMonsters.erase(it);
                    break;
                }
            }
            return 1;
        }

        int __cdecl ApiPlayAnim(AmodMod mod, int index, const char* frameset)
        {
            uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            if (entity == nullptr || frameset == nullptr || frameset[0] == '\0')
            {
                return 0;
            }

            uint8_t* const state = entity + GameAddress::kEntityAnimStateOffset;
            void* const animation = Read<void*>(state, GameAddress::kAnimStateAnimationOffset);

            typedef void(__cdecl* AnimPlayRoutine)(void* animation, const char* frameset, void* state, const float* position, int flag);
            const AnimPlayRoutine play = reinterpret_cast<AnimPlayRoutine>(GameModule::Resolve(GameAddress::kAnimPlay));

            if (animation == nullptr || play == nullptr)
            {
                return 0;
            }

            play(animation, frameset, state, reinterpret_cast<const float*>(entity + GameAddress::kEntityPositionOffset), 0);
            return 1;
        }

        int __cdecl ApiGetAnim(AmodMod mod, int index, AmodAnimState* out)
        {
            const uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            return entity != nullptr
                ? MmdApi::ReadAnimState(entity + GameAddress::kEntityAnimStateOffset, out)
                : 0;
        }

        int __cdecl ApiRegisterMonsterKind(AmodMod mod, const char* name, AmodMonsterKindHandler handler, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || name == nullptr || name[0] == '\0' || handler == nullptr)
            {
                return 0;
            }

            const int kind = Placement::RegisterKind(context, name, &BuildMonsterKind, nullptr, nullptr);
            if (kind == 0)
            {
                FORGE_LOG_ERROR("Monsters: '%s' is a kind something already registered", name);
                return 0;
            }

            MonsterKind fresh;
            fresh.owner = context;
            fresh.kind = kind;
            fresh.name = name;
            fresh.handler = handler;
            fresh.user = user;

            // Parser stand-in row.
            fresh.standIn = StandInFor(name);
            if (fresh.standIn == nullptr)
            {
                StandIn made;
                made.name = name;
                made.row.assign(GameAddress::kMonsterResStride, 0);
                g_standIns.push_back(made);

                fresh.standIn = g_standIns.back().row.data();
                strncpy_s(reinterpret_cast<char*>(fresh.standIn), GameAddress::kSpawnNameLength, name, _TRUNCATE);
                WriteFloat(fresh.standIn, GameAddress::kMonsterResSpeedOffset, 0.0f);
            }

            g_ownKinds.push_back(fresh);
            InstallFindHook();

            return kind;
        }

        int __cdecl ApiPlaceMonsterKind(AmodMod mod, int kind, int roomIndex, const AmodMonsterPlacement* placement)
        {
            ModContext* const context = Caller(mod);
            const MonsterKind* const mine = OwnKindOf(kind);

            if (context == nullptr || placement == nullptr || placement->structSize < sizeof(uint32_t) || mine == nullptr || mine->owner != context)
            {
                return 0;
            }

            AmodMonsterPlacement where = {};
            memcpy(&where, placement, placement->structSize < sizeof(where) ? placement->structSize : sizeof(where));

            if (where.column < 0 || where.column >= static_cast<int>(GameAddress::kRoomGridColumns) || where.row < 0 || 
                where.row >= static_cast<int>(GameAddress::kRoomGridRows))
            {
                return 0;
            }

            uint8_t* const room = roomIndex < 0 ? WorldApi::CurrentRoom() : WorldApi::RoomAt(roomIndex);
            const CloneRoutine clone = reinterpret_cast<CloneRoutine>(GameModule::Resolve(GameAddress::kMemClone));
            const AppendPlacementRoutine append = reinterpret_cast<AppendPlacementRoutine>(GameModule::Resolve(GameAddress::kRoomSpawnsAppend));

            if (room == nullptr || clone == nullptr || append == nullptr)
            {
                return 0;
            }

            uint8_t record[GameAddress::kMonsterSpawnSize] = {};
            const int head = GameAddress::kSpawnKindMonster;
            memcpy(record + GameAddress::kSpawnKindOffset, &head, sizeof(head));
            record[GameAddress::kMonsterSpawnLiveOffset] = 1;

            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kMonsterSpawnNameOffset), GameAddress::kSpawnNameLength, mine->name.c_str(), _TRUNCATE);

            if (where.tag != nullptr)
            {
                strncpy_s(reinterpret_cast<char*>(record + GameAddress::kMonsterSpawnTagOffset), GameAddress::kSpawnNameLength, where.tag, _TRUNCATE);
            }

            const int facing = where.facing != 0 ? where.facing : 1;
            memcpy(record + GameAddress::kMonsterSpawnDirOffset, &facing, sizeof(facing));
            memcpy(record + GameAddress::kMonsterSpawnLevelOffset, &where.level, sizeof(where.level));

            // Depth a monster stands at.
            const float position[3] = {
                where.column * GameAddress::kCellToWorld + GameAddress::kCellOriginX + GameAddress::kCellCentre, GameAddress::kCellOriginY - 
                where.row * GameAddress::kCellToWorld - GameAddress::kCellCentre, GameAddress::kMonsterCellZ
            };
            memcpy(record + GameAddress::kMonsterSpawnPositionOffset, position, sizeof(position));

            // Fields the game placing path also sets.
            record[0x94] = 1;

            void* const placed = clone(record, GameAddress::kMonsterSpawnSize);
            if (placed == nullptr)
            {
                return 0;
            }

            const KindRecord marked = { placed, kind, WorldApi::LevelGeneration(), nullptr };
            g_kindRecords.push_back(marked);

            append(room, placed);

            if (room == WorldApi::CurrentRoom())
            {
                const SpawnOneRoutine spawn = reinterpret_cast<SpawnOneRoutine>(GameSymbols::Address(GameSymbol::MonsterSpawnPlacement));
                if (spawn != nullptr)
                {
                    spawn(placed);
                }
            }
            return 1;
        }

        int __cdecl ApiSetBehaviour(AmodMod mod, const char* name, AmodMonsterActHandler handler, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || name == nullptr || name[0] == '\0' || handler == nullptr)
            {
                return 0;
            }

            Behaviour* found = BehaviourFor(name);
            if (found == nullptr)
            {
                g_behaviours.push_back(Behaviour{});
                found = &g_behaviours.back();
                found->name = name;
            }

            found->owner = context;
            found->handler = handler;
            found->user = user;

            // Take over already-standing monsters of that name too.
            for (uint8_t* entity = WorldApi::FirstLiveEntity(); entity != nullptr && Readable(entity, GameAddress::kEntitySize); entity = WorldApi::NextLiveEntity(entity))
            {
                const uint8_t* const record = MonsterPlacement(entity);
                if (record == nullptr || strcmp(ReadText(record, GameAddress::kMonsterSpawnNameOffset), name) != 0)
                {
                    continue;
                }

                void* const was = Read<void*>(entity, GameAddress::kEntityThinkOffset);
                if (was != reinterpret_cast<void*>(&ThinkThunk))
                {
                    found->gameThink = was;
                }

                void* const mine = reinterpret_cast<void*>(&ThinkThunk);
                memcpy(entity + GameAddress::kEntityThinkOffset, &mine, sizeof(mine));
            }
            return 1;
        }

        // Restore original think; monster is the game's again.
        void HandBackBehaviour(const Behaviour& one)
        {
            for (uint8_t* entity = WorldApi::FirstLiveEntity(); entity != nullptr && Readable(entity, GameAddress::kEntitySize); entity = WorldApi::NextLiveEntity(entity))
            {
                const uint8_t* const record = MonsterPlacement(entity);
                if (record == nullptr || strcmp(ReadText(record, GameAddress::kMonsterSpawnNameOffset), one.name.c_str()) != 0)
                {
                    continue;
                }

                if (Read<void*>(entity, GameAddress::kEntityThinkOffset) == reinterpret_cast<void*>(&ThinkThunk))
                {
                    memcpy(entity + GameAddress::kEntityThinkOffset, &one.gameThink, sizeof(void*));
                }
            }
        }

        int __cdecl ApiClearBehaviour(AmodMod mod, const char* name)
        {
            ModContext* const context = Caller(mod);
            Behaviour* const found = context != nullptr && name != nullptr ? BehaviourFor(name)
                : nullptr;

            if (found == nullptr || found->owner != context)
            {
                return 0;
            }

            HandBackBehaviour(*found);

            for (auto it = g_behaviours.begin(); it != g_behaviours.end(); ++it)
            {
                if (it->name == name)
                {
                    g_behaviours.erase(it);
                    break;
                }
            }
            return 1;
        }

        int __cdecl ApiDefineBehaviour(AmodMod mod, const char* name, AmodMonsterThinkHandler handler, void* user)
        {
            ModContext* const context = Caller(mod);
            DefinedKind* const kind = context != nullptr && name != nullptr ? KindOf(name) : nullptr;

            if (kind == nullptr || kind->owner != context || handler == nullptr)
            {
                return 0;
            }

            kind->behaviour = handler;
            kind->user = user;
            return 1;
        }

        struct KillHandler
        {
            uint32_t id;
            ModContext* owner;
            AmodMonsterKillHandler handler;
            void* user;
        };

        std::vector<KillHandler> g_killHandlers;
        uint32_t g_nextKillHandlerId = 1;

        typedef void(__cdecl* MonsterKillRoutine)(void* entity);

        InlineHook g_killHook;
        MonsterKillRoutine g_originalKill = nullptr;

        AmodMonsterKillResult AskHandlers(const uint8_t* entity, const uint8_t* record)
        {
            AmodMonsterKillInfo info = {};
            info.structSize = sizeof(info);
            info.name = NameAt(record, GameAddress::kMonsterSpawnNameOffset)
                ? ReadText(record, GameAddress::kMonsterSpawnNameOffset)
                : "";
            info.tag = NameAt(record, GameAddress::kMonsterSpawnTagOffset)
                ? ReadText(record, GameAddress::kMonsterSpawnTagOffset)
                : "";
            info.level = Read<int>(record, GameAddress::kMonsterSpawnLevelOffset);
            info.isDecor = record[GameAddress::kMonsterSpawnDecorOffset] != 0 ? 1 : 0;
            info.id = IdOfMonster(entity);

            for (int axis = 0; axis < 3; ++axis)
            {
                info.position[axis] = Read<float>(entity, GameAddress::kEntityPositionOffset + static_cast<uintptr_t>(axis) * sizeof(float));
            }

            const std::vector<KillHandler> snapshot = g_killHandlers;
            for (const KillHandler& entry : snapshot)
            {
                if (entry.handler == nullptr)
                {
                    continue;
                }

                const AmodMonsterKillResult verdict = entry.handler(&info, entry.user);
                if (verdict != AMOD_MONSTER_KILL_DEFAULT)
                {
                    return verdict;
                }
            }
            return AMOD_MONSTER_KILL_DEFAULT;
        }

        void __cdecl KillDetour(void* entity)
        {
            uint8_t* const self = static_cast<uint8_t*>(entity);
            const uint8_t* const record = MonsterPlacement(self);

            const AmodMonsterKillResult verdict = (!g_killHandlers.empty() && record != nullptr) ? AskHandlers(self, record)
                : AMOD_MONSTER_KILL_DEFAULT;

            switch (verdict)
            {
            case AMOD_MONSTER_KILL_REFUSE: return;

            case AMOD_MONSTER_KILL_HANDLED: self[GameAddress::kEntityDeadOffset] = 1;
                const_cast<uint8_t*>(record)[GameAddress::kMonsterSpawnLiveOffset] = 0;
                return;

            default: g_originalKill(entity);
                return;
            }
        }

        bool InstallKillHook()
        {
            void* const target = GameSymbols::Address(GameSymbol::MonsterKill);
            if (target == nullptr)
            {
                return false;
            }

            std::string mismatch;
            if (!GameModule::VerifyBytes(target, kMonsterKillPrologue, sizeof(kMonsterKillPrologue), mismatch))
            {
                FORGE_LOG_ERROR("Monster_Kill does not open as expected (%s), so a mod cannot be " "told about a monster dying", mismatch.c_str());
                return false;
            }

            std::string error;
            if (!g_killHook.Install(target, reinterpret_cast<void*>(&KillDetour), kMonsterKillPrologue, sizeof(kMonsterKillPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Monster_Kill: %s", error.c_str());
                return false;
            }

            g_originalKill = reinterpret_cast<MonsterKillRoutine>(g_killHook.Trampoline());
            return true;
        }

        void WriteRange(uint8_t* record, uintptr_t offset, float low, float high)
        {
            WriteFloat(record, offset, low);
            WriteFloat(record, offset + sizeof(float), high);
        }

        bool Place(int roomIndex, const AmodMonsterPlacement& where)
        {
            if (where.name == nullptr || where.column < 0 || where.column >= static_cast<int>(GameAddress::kRoomGridColumns) || where.row < 0 
                || where.row >= static_cast<int>(GameAddress::kRoomGridRows))
            {
                return false;
            }

            const uint8_t* const type = TypeByName(where.name);
            uint8_t* const room = roomIndex < 0 ? WorldApi::CurrentRoom() : WorldApi::RoomAt(roomIndex);

            const CloneRoutine clone = reinterpret_cast<CloneRoutine>(GameModule::Resolve(GameAddress::kMemClone));
            const AppendPlacementRoutine append = reinterpret_cast<AppendPlacementRoutine>(GameModule::Resolve(GameAddress::kRoomSpawnsAppend));
            const SpawnOneRoutine spawn = reinterpret_cast<SpawnOneRoutine>(GameSymbols::Address(GameSymbol::MonsterSpawnPlacement));

            if (type == nullptr || room == nullptr || clone == nullptr || append == nullptr || spawn == nullptr)
            {
                return false;
            }

            const int kind = GameAddress::kSpawnKindMonster;
            const bool hard = where.level == 1 || where.level == 3;
            const int facing = where.facing < 0 ? -1 : 1;

            uint8_t record[GameAddress::kMonsterSpawnSize] = {};
            memcpy(record + GameAddress::kSpawnKindOffset, &kind, sizeof(kind));
            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kMonsterSpawnNameOffset), GameAddress::kSpawnNameLength, where.name, _TRUNCATE);

            WriteFloat(record, GameAddress::kMonsterSpawnPositionOffset, where.column * GameAddress::kCellToWorld + 
                GameAddress::kCellOriginX + GameAddress::kCellCentre);

            WriteFloat(record, GameAddress::kMonsterSpawnPositionOffset + 0x04, GameAddress::kCellOriginY - where.row * 
                GameAddress::kCellToWorld - GameAddress::kCellCentre);

            WriteFloat(record, GameAddress::kMonsterSpawnPositionOffset + 0x08, GameAddress::kMonsterCellZ);

            memcpy(record + GameAddress::kMonsterSpawnLevelOffset, &where.level, sizeof(int));
            record[GameAddress::kMonsterSpawnLiveOffset] = 1;
            record[0x94] = 1;
            record[GameAddress::kMonsterSpawnDecorOffset] = where.decor != 0 ? 1 : 0;

            WriteFloat(record, GameAddress::kMonsterSpawnSpeedOffset, Read<float>(type, GameAddress::kMonsterResSpeedOffset) * (hard ? 2.0f : 1.0f));
            memcpy(record + GameAddress::kMonsterSpawnDirOffset, &facing, sizeof(facing));
            WriteFloat(record, GameAddress::kMonsterSpawnAttackDistOffset, (where.attackDist > 0.0f ? where.attackDist : 5.0f) * GameAddress::kCellToWorld);

            // Default ranges when a script leaves them out.
            WriteRange(record, GameAddress::kMonsterSpawnAttackTimeOffset, 5.0f, 10.0f);
            WriteRange(record, GameAddress::kMonsterSpawnBluffTimeOffset, 5.0f, 10.0f);
            WriteRange(record, GameAddress::kMonsterSpawnWalkTimeOffset, 10.0f, 20.0f);
            WriteRange(record, GameAddress::kMonsterSpawnWalkWaitOffset, 5.0f, 10.0f);
            WriteRange(record, GameAddress::kMonsterSpawnSitTimeOffset, 10.0f, 20.0f);
            WriteRange(record, GameAddress::kMonsterSpawnWaitTimeOffset, 5.0f, 10.0f);

            if (where.tag != nullptr)
            {
                strncpy_s(reinterpret_cast<char*>(record + GameAddress::kMonsterSpawnTagOffset), GameAddress::kSpawnNameLength, where.tag, _TRUNCATE);
            }

            memcpy(record + GameAddress::kMonsterSpawnTypeOffset, &type, sizeof(type));

            // Same two numbers a script $pos carries.
            const float pos[4] = {
                static_cast<float>(where.column), static_cast<float>(where.row), static_cast<float>(where.extraA), static_cast<float>(where.extraB)
            };

            const DefinedKind* const kindOf = KindOf(where.name);
            const std::string moves = kindOf != nullptr ? kindOf->movesLike : std::string(where.name);

            if (moves == "skull" || moves == "j_skull" || moves == "mummy" || moves == "mummy2")
            {
                ApplyPatrolRange(record, pos);
            }
            else if (moves == "bat")
            {
                ApplyArc(record, pos);
            }

            void* const placed = clone(record, GameAddress::kMonsterSpawnSize);
            if (placed == nullptr)
            {
                return false;
            }

            append(room, placed);
            // Spawn now only in current room.
            if (room == WorldApi::CurrentRoom())
            {
                spawn(placed);
            }
            return true;
        }

        // Missing fields filled with sane defaults.
        void FitToMovement(uint8_t* entity, uint8_t* record, const GameAddress::MonsterMovement& movement)
        {
            const float span = 5.0f * GameAddress::kCellToWorld;
            const std::string moves = movement.name;

            if (moves == "skull" || moves == "j_skull" || moves == "mummy" || moves == "mummy2")
            {
                if (Read<float>(record, 0x5C) == 0.0f && Read<float>(record, 0x60) == 0.0f)
                {
                    WriteFloat(record, 0x5C, span);
                    WriteFloat(record, 0x60, span);
                }

                // Starts walking with the timer its keys ask for.
                const int walking = GameAddress::kMonsterStateMove;
                memcpy(record + GameAddress::kMonsterSpawnStateOffset, &walking, sizeof(walking));
                WriteFloat(record, GameAddress::kMonsterSpawnTimerOffset, Read<float>(record, GameAddress::kMonsterSpawnWalkTimeOffset));
            }
            else if (moves == "bat" && Read<float>(record, GameAddress::kMonsterSpawnRadiusOffset) == 0.0f)
            {
                WriteFloat(record, GameAddress::kMonsterSpawnRadiusOffset, span);
                WriteFloat(record, GameAddress::kMonsterSpawnAxisOffset, 1.0f);
                WriteFloat(record, GameAddress::kMonsterSpawnAxisOffset + 0x04, 0.0f);
                WriteFloat(record, GameAddress::kMonsterSpawnAxisOffset + 0x08, 0.0f);

                for (int axis = 0; axis < 3; ++axis)
                {
                    const uintptr_t step = static_cast<uintptr_t>(axis) * sizeof(float);
                    WriteFloat(record, GameAddress::kMonsterSpawnPositionOffset + step, Read<float>(entity, GameAddress::kEntityPositionOffset + step));
                }
            }

            // Ensure fields the new movement reads hold something.
            const float inEntity = Read<float>(entity, GameAddress::kMonsterEntitySpeedOffset);
            const float inRecord = Read<float>(record, GameAddress::kMonsterSpawnSpeedOffset);

            if (movement.speedInEntity && inEntity == 0.0f)
            {
                WriteFloat(entity, GameAddress::kMonsterEntitySpeedOffset, inRecord);
            }
            if (!movement.speedInEntity && inRecord == 0.0f)
            {
                WriteFloat(record, GameAddress::kMonsterSpawnSpeedOffset, inEntity);
            }
        }

        int __cdecl ApiSetMovement(AmodMod mod, int index, const char* movesLike)
        {
            uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            uint8_t* const record = entity != nullptr ? MonsterPlacement(entity) : nullptr;

            if (record == nullptr || movesLike == nullptr)
            {
                return 0;
            }

            for (const GameAddress::MonsterMovement& movement : GameAddress::kMonsterMovements)
            {
                if (strcmp(movement.name, movesLike) != 0)
                {
                    continue;
                }

                void* const think = GameModule::Resolve(movement.think);
                if (think == nullptr)
                {
                    return 0;
                }

                FitToMovement(entity, record, movement);
                memcpy(entity + GameAddress::kEntityThinkOffset, &think, sizeof(think));
                return 1;
            }
            return 0;
        }

        int __cdecl ApiPlace(AmodMod mod, const char* name, int column, int row)
        {
            AmodMonsterPlacement where = {};
            where.structSize = sizeof(where);
            where.name = name;
            where.column = column;
            where.row = row;

            return (Caller(mod) != nullptr && Place(-1, where)) ? 1 : 0;
        }

        int __cdecl ApiPlaceIn(AmodMod mod, int roomIndex, const AmodMonsterPlacement* placement)
        {
            if (Caller(mod) == nullptr || placement == nullptr || placement->structSize < sizeof(uint32_t))
            {
                return 0;
            }

            AmodMonsterPlacement where = {};
            memcpy(&where, placement, placement->structSize < sizeof(where) ? placement->structSize : sizeof(where));

            return Place(roomIndex, where) ? 1 : 0;
        }

        int __cdecl ApiPlaceEx(AmodMod mod, const AmodMonsterPlacement* placement)
        {
            return ApiPlaceIn(mod, -1, placement);
        }

        int __cdecl ApiKillIn(AmodMod mod, int roomIndex, int index)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            if (roomIndex < 0)
            {
                return g_table.Kill != nullptr ? g_table.Kill(mod, index) : 0;
            }

            uint8_t* const record = PlacementAt(roomIndex, index);
            if (record == nullptr || record[GameAddress::kMonsterSpawnLiveOffset] == 0)
            {
                return 0;
            }

            // roomIndex may be current room; then a live monster is on that record.
            uint8_t* const entity = EntityOf(record);
            if (entity != nullptr)
            {
                const KillRoutine kill = reinterpret_cast<KillRoutine>(GameSymbols::Address(GameSymbol::MonsterKill));
                if (kill != nullptr)
                {
                    kill(entity);
                    return 1;
                }

                entity[GameAddress::kEntityDeadOffset] = 1;
            }

            record[GameAddress::kMonsterSpawnLiveOffset] = 0;

            int* const killed = static_cast<int*>(GameModule::Resolve(GameAddress::kMonstersKilled));
            if (killed != nullptr && record[GameAddress::kMonsterSpawnDecorOffset] == 0)
            {
                *killed += 1;
            }
            return 1;
        }

        struct TouchHandler
        {
            uint32_t id;
            ModContext* owner;
            AmodMonsterTouchHandler handler;
            void* user;
        };

        std::vector<TouchHandler> g_touchHandlers;
        uint32_t g_nextTouchHandlerId = 1;

        typedef void(__cdecl* TouchRoutine)(void* monster, void* other, int side);

        struct TouchSite
        {
            InlineHook hook;
            TouchRoutine original;
        };

        TouchSite g_touchSites[sizeof(kMonsterTouchSites) / sizeof(kMonsterTouchSites[0])] = {};

        AmodMonsterTouchResult AskTouch(const uint8_t* monster, const uint8_t* other, int side)
        {
            const uint8_t* const record = MonsterPlacement(monster);
            if (record == nullptr || g_touchHandlers.empty())
            {
                return AMOD_MONSTER_TOUCH_DEFAULT;
            }

            const bool readable = other != nullptr && Readable(other, GameAddress::kEntitySize);

            AmodMonsterTouchInfo info = {};
            info.structSize = sizeof(info);
            info.index = IndexOfMonster(monster);
            info.id = IdOfMonster(monster);
            info.name = NameAt(record, GameAddress::kMonsterSpawnNameOffset)
                ? ReadText(record, GameAddress::kMonsterSpawnNameOffset)
                : "";
            info.tag = NameAt(record, GameAddress::kMonsterSpawnTagOffset)
                ? ReadText(record, GameAddress::kMonsterSpawnTagOffset)
                : "";
            info.toucherKind = readable ? Read<int>(other, GameAddress::kEntityKindOffset) : 0;
            info.isPlayer = info.toucherKind == GameAddress::kEntityKindPlayer ? 1 : 0;
            info.side = side;

            for (int axis = 0; axis < 3; ++axis)
            {
                info.position[axis] = Read<float>(monster, GameAddress::kEntityPositionOffset + static_cast<uintptr_t>(axis) * sizeof(float));
            }

            const std::vector<TouchHandler> snapshot = g_touchHandlers;
            for (const TouchHandler& entry : snapshot)
            {
                if (entry.handler == nullptr)
                {
                    continue;
                }

                const AmodMonsterTouchResult verdict = entry.handler(&info, entry.user);
                if (verdict != AMOD_MONSTER_TOUCH_DEFAULT)
                {
                    return verdict;
                }
            }
            return AMOD_MONSTER_TOUCH_DEFAULT;
        }

        // One hook per touch site; each falls through to its original.
        template <size_t Site> void __cdecl TouchDetour(void* monster, void* other, int side)
        {
            if (AskTouch(static_cast<const uint8_t*>(monster), static_cast<const uint8_t*>(other), side) == AMOD_MONSTER_TOUCH_DEFAULT)
            {
                g_touchSites[Site].original(monster, other, side);
            }
        }

        bool InstallTouchHooks()
        {
            static void* const detours[] = {
                reinterpret_cast<void*>(&TouchDetour<0>), reinterpret_cast<void*>(&TouchDetour<1>), reinterpret_cast<void*>(&TouchDetour<2>), 
                reinterpret_cast<void*>(&TouchDetour<3>), reinterpret_cast<void*>(&TouchDetour<4>), reinterpret_cast<void*>(&TouchDetour<5>), };

            size_t at = 0;
            for (const MonsterTouchSite& site : kMonsterTouchSites)
            {
                void* const target = GameModule::Resolve(site.address);
                std::string mismatch;

                if (target == nullptr || !GameModule::VerifyBytes(target, site.prologue, site.length, mismatch))
                {
                    FORGE_LOG_ERROR("The monster touch at 0x%08X is not what it was reversed as", static_cast<unsigned>(site.address));
                    return false;
                }

                std::string error;
                if (!g_touchSites[at].hook.Install(target, detours[at], site.prologue, site.length, error))
                {
                    FORGE_LOG_ERROR("Cannot hook the monster touch at 0x%08X: %s", static_cast<unsigned>(site.address), error.c_str());
                    return false;
                }

                g_touchSites[at].original = reinterpret_cast<TouchRoutine>(g_touchSites[at].hook.Trampoline());
                ++at;
            }
            return true;
        }

        void ReleaseTouchHandler(void* payload)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload));
            for (auto it = g_touchHandlers.begin(); it != g_touchHandlers.end(); ++it)
            {
                if (it->id == id)
                {
                    g_touchHandlers.erase(it);
                    return;
                }
            }
        }

        AmodToken __cdecl ApiAddTouchHandler(AmodMod mod, AmodMonsterTouchHandler handler, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || handler == nullptr || g_touchSites[0].original == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }

            TouchHandler entry;
            entry.id = g_nextTouchHandlerId++;
            entry.owner = context;
            entry.handler = handler;
            entry.user = user;
            g_touchHandlers.push_back(entry);

            return context->owned.Add(&ReleaseTouchHandler, reinterpret_cast<void*>(static_cast<uintptr_t>(entry.id)));
        }

        void __cdecl ApiRemoveTouchHandler(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Caller(mod);
            if (context != nullptr)
            {
                context->owned.Remove(token);
            }
        }

        void ReleaseKillHandler(void* payload)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload));
            for (auto it = g_killHandlers.begin(); it != g_killHandlers.end(); ++it)
            {
                if (it->id == id)
                {
                    g_killHandlers.erase(it);
                    return;
                }
            }
        }

        AmodToken __cdecl ApiAddKillHandler(AmodMod mod, AmodMonsterKillHandler handler, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || handler == nullptr || g_originalKill == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }

            KillHandler entry;
            entry.id = g_nextKillHandlerId++;
            entry.owner = context;
            entry.handler = handler;
            entry.user = user;
            g_killHandlers.push_back(entry);

            return context->owned.Add(&ReleaseKillHandler, reinterpret_cast<void*>(static_cast<uintptr_t>(entry.id)));
        }

        void ReleaseSpawnHandler(void* payload)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload));
            for (auto it = g_spawnHandlers.begin(); it != g_spawnHandlers.end(); ++it)
            {
                if (it->id == id)
                {
                    g_spawnHandlers.erase(it);
                    return;
                }
            }
        }

        AmodToken __cdecl ApiAddSpawnHandler(AmodMod mod, AmodMonsterSpawnHandler handler, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || handler == nullptr || g_originalSpawn == nullptr)
            {
                Fail(mod, handler == nullptr ? AMOD_FAIL_BAD_ARGUMENT : AMOD_FAIL_NOT_OFFERED, 
                    "the spawner is not hooked, so nothing can be told about a monster appearing");
                return AMOD_INVALID_TOKEN;
            }

            SpawnHandler entry;
            entry.id = g_nextSpawnHandlerId++;
            entry.owner = context;
            entry.handler = handler;
            entry.user = user;
            g_spawnHandlers.push_back(entry);

            return context->owned.Add(&ReleaseSpawnHandler, reinterpret_cast<void*>(static_cast<uintptr_t>(entry.id)));
        }

        void __cdecl ApiRemoveSpawnHandler(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Caller(mod);
            if (context != nullptr)
            {
                context->owned.Remove(token);
            }
        }

        // Box field order is the game's; untangle on read, restore on write.
        void ReadBox(const uint8_t* box, AmodBox& out)
        {
            out.min[0] = Read<float>(box, GameAddress::kBoxMinXOffset);
            out.min[1] = Read<float>(box, GameAddress::kBoxMinYOffset);
            out.min[2] = Read<float>(box, GameAddress::kBoxMinZOffset);
            out.max[0] = Read<float>(box, GameAddress::kBoxMaxXOffset);
            out.max[1] = Read<float>(box, GameAddress::kBoxMaxYOffset);
            out.max[2] = Read<float>(box, GameAddress::kBoxMaxZOffset);
        }

        void WriteBox(uint8_t* box, const AmodBox& from)
        {
            memcpy(box + GameAddress::kBoxMinXOffset, &from.min[0], sizeof(float));
            memcpy(box + GameAddress::kBoxMinYOffset, &from.min[1], sizeof(float));
            memcpy(box + GameAddress::kBoxMinZOffset, &from.min[2], sizeof(float));
            memcpy(box + GameAddress::kBoxMaxXOffset, &from.max[0], sizeof(float));
            memcpy(box + GameAddress::kBoxMaxYOffset, &from.max[1], sizeof(float));
            memcpy(box + GameAddress::kBoxMaxZOffset, &from.max[2], sizeof(float));
        }

        uintptr_t TypeBoxOffset(int which)
        {
            switch (which)
            {
            case AMOD_MONSTER_BOX_SECOND: return GameAddress::kMonsterResBox2Offset;
            case AMOD_MONSTER_BOX_OBJECT: return GameAddress::kMonsterResObjBoxOffset;
            case AMOD_MONSTER_BOX_MAIN:   return GameAddress::kMonsterResBoxOffset;
            }
            return 0;
        }

        int __cdecl ApiGetTypeBox(AmodMod mod, const char* name, int which, AmodBox* out)
        {
            const uintptr_t offset = TypeBoxOffset(which);
            const uint8_t* const record = Caller(mod) != nullptr ? TypeByName(name) : nullptr;
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "the game knows no monster called '%s'", name != nullptr ? name : "");
            }
            if (offset == 0)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d is not one of the three boxes", which);
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetTypeBox was given nowhere to put the box");
            }

            AmodBox filled = {};
            filled.structSize = sizeof(filled);
            ReadBox(record + offset, filled);
            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        struct BoxSnapshot
        {
            ModContext* owner;
            uint8_t* record;
            int which;
            uint8_t bytes[GameAddress::kRoomCollisionBoxStride];
        };

        std::vector<BoxSnapshot> g_boxSnapshots;

        // Whole-record snapshot (0xDC); exact rollback.
        struct TypeSnapshot
        {
            ModContext* owner;
            uint8_t* record;
            uint8_t bytes[GameAddress::kMonsterResStride];
        };

        std::vector<TypeSnapshot> g_typeSnapshots;

        void KeepType(ModContext* owner, uint8_t* record)
        {
            for (const TypeSnapshot& kept : g_typeSnapshots)
            {
                if (kept.record == record)
                {
                    return;
                }
            }

            TypeSnapshot kept;
            kept.owner = owner;
            kept.record = record;
            memcpy(kept.bytes, record, sizeof(kept.bytes));
            g_typeSnapshots.push_back(kept);
        }

        void KeepBox(ModContext* owner, uint8_t* record, int which, uintptr_t offset)
        {
            for (const BoxSnapshot& kept : g_boxSnapshots)
            {
                if (kept.record == record && kept.which == which)
                {
                    return;
                }
            }

            BoxSnapshot kept;
            kept.owner = owner;
            kept.record = record;
            kept.which = which;
            memcpy(kept.bytes, record + offset, sizeof(kept.bytes));
            g_boxSnapshots.push_back(kept);
        }

        int __cdecl ApiSetTypeBox(AmodMod mod, const char* name, int which, const AmodBox* box)
        {
            const uintptr_t offset = TypeBoxOffset(which);
            ModContext* const caller = Caller(mod);
            uint8_t* const record = caller != nullptr ? TypeByName(name) : nullptr;
            if (record == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "the game knows no monster called '%s'", name != nullptr ? name : "");
            }
            if (offset == 0)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d is not one of the three boxes", which);
            }
            if (box == nullptr || box->structSize < sizeof(AmodBox))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetTypeBox was given no box to write");
            }

            KeepBox(caller, record, which, offset);
            WriteBox(record + offset, *box);
            return 1;
        }

        // Live: $bbox=+0x13C, $obj_bbox=+0x158. $bbox2 is not a live slot.
        uintptr_t LiveBoxOffset(int which)
        {
            switch (which)
            {
            case AMOD_MONSTER_BOX_MAIN:   return GameAddress::kEntityFrameBoxOffset;
            case AMOD_MONSTER_BOX_OBJECT: return GameAddress::kMonsterEntityObjBoxOffset;
            }
            return 0;
        }

        int __cdecl ApiGetLiveBox(AmodMod mod, int index, int which, AmodBox* out)
        {
            const uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            const uintptr_t offset = LiveBoxOffset(which);

            if (entity == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no monster %d to read a box off", index);
            }
            if (offset == 0)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a living monster carries the main box and the object box, not %d", which);
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetBox was given nowhere to put the box");
            }

            AmodBox filled = {};
            filled.structSize = sizeof(filled);
            ReadBox(entity + offset, filled);
            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetLiveBox(AmodMod mod, int index, int which, const AmodBox* box)
        {
            uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            const uintptr_t offset = LiveBoxOffset(which);

            if (entity == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no monster %d to write a box on", index);
            }
            if (offset == 0)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a living monster carries the main box and the object box, not %d", which);
            }
            if (box == nullptr || box->structSize < sizeof(AmodBox))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetBox was given no box to write");
            }

            WriteBox(entity + offset, *box);
            return 1;
        }

        // GetBox/SetBox = live object box.
        int __cdecl ApiGetBox(AmodMod mod, int index, AmodBox* out)
        {
            return ApiGetLiveBox(mod, index, AMOD_MONSTER_BOX_OBJECT, out);
        }

        int __cdecl ApiSetBox(AmodMod mod, int index, const AmodBox* box)
        {
            return ApiSetLiveBox(mod, index, AMOD_MONSTER_BOX_OBJECT, box);
        }

        void __cdecl ApiRemoveKillHandler(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Caller(mod);
            if (context != nullptr)
            {
                context->owned.Remove(token);
            }
        }
    }

    bool MonstersApi::Install()
    {
        g_table.structSize = sizeof(AmodMonstersApi);
        g_table.GetTypeCount = &ApiGetTypeCount;
        g_table.GetType = &ApiGetType;
        g_table.FindType = &ApiFindType;
        g_table.DefineType = &ApiDefineType;
        g_table.GetTypeProperty = &ApiGetTypeProperty;
        g_table.SetTypeProperty = &ApiSetTypeProperty;
        g_table.HasTypeProperty = &ApiHasTypeProperty;
        g_table.TypePropertyCount = &ApiTypePropertyCount;
        g_table.TypePropertyNameAt = &ApiTypePropertyNameAt;
        g_table.GetCount = &ApiGetCount;
        g_table.Get = &ApiGet;
        g_table.SetMovement = &ApiSetMovement;
        g_table.Move = &ApiMove;
        g_table.Place = &ApiPlace;
        g_table.PlaceEx = &ApiPlaceEx;
        g_table.Kill = &ApiKill;
        g_table.GetProperty = &ApiGetProperty;
        g_table.SetProperty = &ApiSetProperty;
        g_table.HasProperty = &ApiHasProperty;
        g_table.PropertyCount = &ApiPropertyCount;
        g_table.PropertyNameAt = &ApiPropertyNameAt;
        g_table.GetKilledCount = &ApiGetKilledCount;
        g_table.GetPlacedCount = &ApiGetPlacedCount;
        g_table.GetPlaced = &ApiGetPlaced;
        g_table.GetPlacedProperty = &ApiGetPlacedProperty;
        g_table.SetPlacedProperty = &ApiSetPlacedProperty;
        g_table.GetTypeBox = &ApiGetTypeBox;
        g_table.SetTypeBox = &ApiSetTypeBox;
        g_table.GetBox = &ApiGetBox;
        g_table.SetBox = &ApiSetBox;
        g_table.IdAt = &ApiIdAt;
        g_table.IndexOf = &ApiIndexOf;
        g_table.Find = &ApiFind;
        g_table.FindPlaced = &ApiFindPlaced;
        g_table.AddSpawnHandler = &ApiAddSpawnHandler;
        g_table.RemoveSpawnHandler = &ApiRemoveSpawnHandler;
        g_table.Cancel = &ApiRemoveSpawnHandler;
        g_table.IdOfDraw = &ApiIdOfDraw;
        g_table.GetLiveBox = &ApiGetLiveBox;
        g_table.SetLiveBox = &ApiSetLiveBox;

        FrameApi::AddInternalTick(&ApplyPendingTypes);

        // Before game reads monster_res.txt (~85% startup load).
        OpenJumpKeys();

        // Spawn+parse hooks give it something to move along.
        if (InstallSpawnHook() && InstallParseHook())
        {
            g_table.DefineKind = &ApiDefineKind;
            g_table.DefineBehaviour = &ApiDefineBehaviour;
        }

        if (InstallSpawnHook())
        {
            g_table.AddPlaceHandler = &ApiAddPlaceHandler;
            g_table.RemovePlaceHandler = &ApiRemovePlaceHandler;
            g_table.RemovePlaced = &ApiRemovePlaced;
            g_table.SetBehaviour = &ApiSetBehaviour;
            g_table.ClearBehaviour = &ApiClearBehaviour;
            g_table.BorrowMonster = &ApiBorrowMonster;
            g_table.ReturnMonster = &ApiReturnMonster;

            FrameApi::AddInternalTick(&DropDeadBorrows);
        }

        // Mod kinds go through Placement-owned spawn slot.
        if (Placement::Install())
        {
            g_table.RegisterKind = &ApiRegisterMonsterKind;
            g_table.PlaceKind = &ApiPlaceMonsterKind;
        }

        g_table.PlaceIn = &ApiPlaceIn;
        g_table.KillIn = &ApiKillIn;
        g_table.PlayAnim = &ApiPlayAnim;
        g_table.GetAnim = &ApiGetAnim;
        if (InstallKillHook())
        {
            g_table.AddKillHandler = &ApiAddKillHandler;
            g_table.RemoveKillHandler = &ApiRemoveKillHandler;
        }
        if (InstallTouchHooks())
        {
            g_table.AddTouchHandler = &ApiAddTouchHandler;
            g_table.RemoveTouchHandler = &ApiRemoveTouchHandler;
        }

        return true;
    }

    void MonstersApi::Uninstall()
    {
        CloseJumpKeys();

        g_spawnHook.Uninstall();
        g_findHook.Uninstall();
        g_originalFind = nullptr;
        g_parseHook.Uninstall();
        g_killHook.Uninstall();
        g_freeHook.Uninstall();

        for (TouchSite& site : g_touchSites)
        {
            site.hook.Uninstall();
            site.original = nullptr;
        }

        g_originalSpawn = nullptr;
        g_originalParse = nullptr;
        g_originalKill = nullptr;
        g_originalFree = nullptr;

        g_kinds.clear();
        g_behaviours.clear();
        g_placeHandlers.clear();
        g_borrowedMonsters.clear();
        g_kindRecords.clear();
        g_standIns.clear();
        g_ownKinds.clear();
        g_killHandlers.clear();
        g_touchHandlers.clear();
        g_pendingTypes.clear();
        g_definedTypes.clear();
        g_boxSnapshots.clear();
        g_typeSnapshots.clear();
        g_tracked.clear();
        g_spawnHandlers.clear();
        g_table = AmodMonstersApi{};
    }

    const AmodMonstersApi* MonstersApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void MonstersApi::ForgetMod(ModContext* owner)
    {

        // Restore borrowed think before the kind that knew it is dropped.
        for (uint8_t* entity = WorldApi::FirstLiveEntity(); entity != nullptr && Readable(entity, GameAddress::kEntitySize); entity = WorldApi::NextLiveEntity(entity))
        {
            const uint8_t* const record = MonsterPlacement(entity);
            const DefinedKind* const kind = record != nullptr ? KindOf(ReadText(record, GameAddress::kMonsterSpawnNameOffset))
                : nullptr;

            if (kind != nullptr && kind->owner == owner && kind->borrowedThink != nullptr)
            {
                memcpy(entity + GameAddress::kEntityThinkOffset, &kind->borrowedThink, sizeof(void*));
            }
        }

        for (int room = 0; room < WorldApi::RoomTotal(); ++room)
        {
            for (int at = 0; ; ++at)
            {
                uint8_t* const record = PlacementAt(room, at);
                if (record == nullptr)
                {
                    break;
                }

                const int kind = OwnKindByName(ReadText(record, GameAddress::kMonsterSpawnNameOffset));
                const MonsterKind* const mine = kind != 0 ? OwnKindOf(kind) : nullptr;

                if (mine == nullptr || mine->owner != owner || record[GameAddress::kMonsterSpawnLiveOffset] == 0)
                {
                    continue;
                }

                uint8_t* const entity = EntityOf(record);
                if (entity != nullptr)
                {
                    entity[GameAddress::kEntityDeadOffset] = 1;
                }

                record[GameAddress::kMonsterSpawnLiveOffset] = 0;
            }
        }

        for (auto it = g_kindRecords.begin(); it != g_kindRecords.end();)
        {
            const MonsterKind* const mine = OwnKindOf(it->kind);
            it = (mine != nullptr && mine->owner == owner) ? g_kindRecords.erase(it) : it + 1;
        }

        for (auto it = g_ownKinds.begin(); it != g_ownKinds.end();)
        {
            it = it->owner == owner ? g_ownKinds.erase(it) : it + 1;
        }

        for (auto it = g_borrowedMonsters.begin(); it != g_borrowedMonsters.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            HandBackMonster(*it);
            it = g_borrowedMonsters.erase(it);
        }

        // Hand back behaviours this mod was driving.
        for (auto it = g_behaviours.begin(); it != g_behaviours.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            HandBackBehaviour(*it);
            it = g_behaviours.erase(it);
        }

        for (auto it = g_placeHandlers.begin(); it != g_placeHandlers.end();)
        {
            it = it->owner == owner ? g_placeHandlers.erase(it) : ++it;
        }

        for (auto it = g_typeSnapshots.begin(); it != g_typeSnapshots.end();)
        {
            if (it->owner == owner && Readable(it->record, GameAddress::kMonsterResStride))
            {
                memcpy(it->record, it->bytes, sizeof(it->bytes));
            }
            it = it->owner == owner ? g_typeSnapshots.erase(it) : ++it;
        }

        // Restore boxes first; live monsters of that kind get the restored object box.
        for (auto it = g_boxSnapshots.begin(); it != g_boxSnapshots.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            const uintptr_t offset = TypeBoxOffset(it->which);
            if (offset != 0 && Readable(it->record, GameAddress::kMonsterResStride))
            {
                memcpy(it->record + offset, it->bytes, sizeof(it->bytes));

                if (it->which == AMOD_MONSTER_BOX_OBJECT)
                {
                    const char* const kind = ReadText(it->record, GameAddress::kMonsterResNameOffset);
                    for (uint8_t* entity = WorldApi::FirstLiveEntity(); entity != nullptr && Readable(entity, GameAddress::kEntitySize); 
                        entity = WorldApi::NextLiveEntity(entity))
                    {
                        const uint8_t* const record = MonsterPlacement(entity);
                        if (record != nullptr && strcmp(ReadText(record, GameAddress::kMonsterSpawnNameOffset), kind) == 0)
                        {
                            memcpy(entity + GameAddress::kMonsterEntityObjBoxOffset, it->bytes, sizeof(it->bytes));
                        }
                    }
                }
            }
            it = g_boxSnapshots.erase(it);
        }

        for (auto it = g_kinds.begin(); it != g_kinds.end();)
        {
            it = it->owner == owner ? g_kinds.erase(it) : ++it;
        }
        for (auto it = g_killHandlers.begin(); it != g_killHandlers.end();)
        {
            it = it->owner == owner ? g_killHandlers.erase(it) : ++it;
        }
        for (auto it = g_touchHandlers.begin(); it != g_touchHandlers.end();)
        {
            it = it->owner == owner ? g_touchHandlers.erase(it) : ++it;
        }
        for (auto it = g_pendingTypes.begin(); it != g_pendingTypes.end();)
        {
            it = it->owner == owner ? g_pendingTypes.erase(it) : ++it;
        }

        for (DefinedType& defined : g_definedTypes)
        {
            defined.owner = defined.owner == owner ? nullptr : defined.owner;
        }
    }
}
