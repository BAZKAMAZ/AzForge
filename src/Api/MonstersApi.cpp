#include <windows.h>

#include "src/Api/MonstersApi.h"
#include "src/Api/ApiProperty.h"
#include "src/Api/ApiRead.h"
#include "src/Api/FrameApi.h"
#include "src/Api/ModContext.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Hook/InlineHook.h"

#include <math.h>
#include <stddef.h>
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
            const int* const count =
                static_cast<const int*>(GameModule::Resolve(GameAddress::kMonsterResCount));
            return (count != nullptr && *count > 0 && *count < 0x1000) ? *count : 0;
        }

        uint8_t* TypeRecord(int index)
        {
            uint8_t** const table =
                static_cast<uint8_t**>(GameModule::Resolve(GameAddress::kMonsterResTable));

            if (table == nullptr || *table == nullptr || index < 0 || index >= TypeTotal())
            {
                return nullptr;
            }

            uint8_t* const record =
                *table + static_cast<size_t>(index) * GameAddress::kMonsterResStride;
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
                if (record != nullptr &&
                    strcmp(ReadText(record, GameAddress::kMonsterResNameOffset), name) == 0)
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
                out.partColorHard[at] =
                    Read<float>(record, GameAddress::kMonsterResPartColorHardOffset + step);
            }
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

        // A definition a mod asked for before the game had a table to put it in.
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
                                     ? reinterpret_cast<uint32_t (__cdecl*)(const char*)>(*at)
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

        // compares the name against.
        void OpenJumpKeys()
        {
            uint8_t* const at =
                static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kMonsterJumpKeysGuard));
            std::string mismatch;

            if (at == nullptr ||
                !GameModule::VerifyBytes(at, GameAddress::kMonsterJumpKeysGuardBytes,
                                         sizeof(GameAddress::kMonsterJumpKeysGuardBytes), mismatch))
            {
                FORGE_LOG_ERROR("The jump keys stay shut: 0x%08X does not hold what it was "
                                "reversed as (%s)",
                                static_cast<unsigned>(GameAddress::kMonsterJumpKeysGuard),
                                mismatch.c_str());
                return;
            }

            static const uint8_t through[] = { 0x90, 0x90 };
            g_jumpKeysOpen = WriteCode(at, through, sizeof(through));
        }

        void CloseJumpKeys()
        {
            uint8_t* const at =
                g_jumpKeysOpen
                    ? static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kMonsterJumpKeysGuard))
                    : nullptr;

            if (at != nullptr)
            {
                WriteCode(at, GameAddress::kMonsterJumpKeysGuardBytes,
                          sizeof(GameAddress::kMonsterJumpKeysGuardBytes));
            }
            g_jumpKeysOpen = false;
        }

        typedef void (__cdecl* FreeResourcesRoutine)();

        InlineHook g_freeHook;
        FreeResourcesRoutine g_originalFree = nullptr;

        void __cdecl FreeResourcesDetour()
        {
            static const uintptr_t shared[] =
            {
                GameAddress::kMonsterResTexEasyOffset, GameAddress::kMonsterResTexHardOffset,
                GameAddress::kMonsterResModelOffset,   GameAddress::kMonsterResAnimationOffset
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

            void* const target = GameModule::Resolve(GameAddress::kMonstersFreeResources);
            std::string mismatch;

            if (target == nullptr ||
                !GameModule::VerifyBytes(target, kMonstersFreeResourcesPrologue,
                                         sizeof(kMonstersFreeResourcesPrologue), mismatch))
            {
                FORGE_LOG_ERROR("Monsters_FreeResources does not open as expected, so a monster of "
                                "a mod's own cannot be given anything to look like");
                return false;
            }

            std::string error;
            if (!g_freeHook.Install(target, reinterpret_cast<void*>(&FreeResourcesDetour),
                                    kMonstersFreeResourcesPrologue,
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
                FORGE_LOG_ERROR("Monster '%s' is based on '%s', which monster_res.txt does not hold",
                                wanted.name.c_str(), wanted.basedOn.c_str());
                return false;
            }

            uint8_t record[GameAddress::kMonsterResStride];
            memcpy(record, source, sizeof(record));

            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kMonsterResNameOffset),
                      GameAddress::kSpawnNameLength, wanted.name.c_str(), _TRUNCATE);

            // Whatever the mod did not name for itself stays the base's - model, textures and

            // Sharing those handles is only safe because WatchTeardown clears them out of this
            // record before the game frees every record it holds.
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

            uint8_t** const table =
                static_cast<uint8_t**>(GameModule::Resolve(GameAddress::kMonsterResTable));
            int* const count = static_cast<int*>(GameModule::Resolve(GameAddress::kMonsterResCount));
            const AppendRoutine append =
                reinterpret_cast<AppendRoutine>(GameModule::Resolve(GameAddress::kMonsterResAppend));

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
            memcpy(&wanted, type,
                   type->structSize < sizeof(wanted) ? type->structSize : sizeof(wanted));

            if (wanted.name == nullptr || wanted.name[0] == '\0' ||
                wanted.basedOn == nullptr || wanted.basedOn[0] == '\0')
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

        constexpr Field kTypeFields[] =
        {
            { "name",            GameAddress::kMonsterResNameOffset,          Kind::Text,     0, 0, 0, false },
            { "tex_easy",        GameAddress::kMonsterResTexEasyOffset,       Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true },
            { "tex_hard",        GameAddress::kMonsterResTexHardOffset,       Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true },
            { "model",           GameAddress::kMonsterResModelOffset,         Kind::Resource, 0, 0, GameAddress::kResolveModelSlot,   true },
            { "speed_up",        GameAddress::kMonsterResSpeedUpOffset,       Kind::Float,    0, 0, 0, true },
            { "speed",           GameAddress::kMonsterResSpeedOffset,         Kind::Float,    0, 0, 0, true },
            { "vert_speed",      GameAddress::kMonsterResVertSpeedOffset,     Kind::Float,    0, 0, 0, true },
            { "part_color",      GameAddress::kMonsterResPartColorOffset,     Kind::Floats,   4, 0, 0, true },
            { "part_color_hard", GameAddress::kMonsterResPartColorHardOffset, Kind::Floats,   4, 0, 0, true },
            { "jump",            GameAddress::kMonsterResJumpOffset,          Kind::Float,    0, 0, 0, true },
            { "jump_factor",     GameAddress::kMonsterResJumpFactorOffset,    Kind::Float,    0, 0, 0, true },
            { "fall",            GameAddress::kMonsterResFallOffset,          Kind::Float,    0, 0, 0, true },
        };

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
            return (Caller(mod) != nullptr && TypeByName(name) != nullptr &&
                    ApiProperty::Find(kTypeFields, kTypeFieldCount, key) != nullptr)
                       ? 1
                       : 0;
        }

        int __cdecl ApiGetTypeProperty(AmodMod mod, const char* name, const char* key,
                                       AmodValue* value)
        {
            const uint8_t* const record = Caller(mod) != nullptr ? TypeByName(name) : nullptr;
            const Field* const field = ApiProperty::Find(kTypeFields, kTypeFieldCount, key);

            return (record != nullptr && field != nullptr) ? ApiProperty::Get(record, *field, value)
                                                           : 0;
        }

        int __cdecl ApiSetTypeProperty(AmodMod mod, const char* name, const char* key,
                                       const AmodValue* value)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const record = OwnsType(caller, name) ? TypeByName(name) : nullptr;
            const Field* const field = ApiProperty::Find(kTypeFields, kTypeFieldCount, key);

            return (record != nullptr && field != nullptr && value != nullptr)
                       ? ApiProperty::Set(record, *field, *value)
                       : 0;
        }

        // would shift every number after it for the rest of the frame.
        uint8_t* MonsterPlacement(const uint8_t* entity)
        {
            if (entity == nullptr || !Readable(entity, GameAddress::kEntitySize) ||
                entity[GameAddress::kEntityDeadOffset] != 0)
            {
                return nullptr;
            }

            uint8_t* const record = Read<uint8_t*>(entity, GameAddress::kEntitySpawnOffset);

            return (record != nullptr && Readable(record, GameAddress::kMonsterSpawnSize) &&
                    Read<int>(record, GameAddress::kSpawnKindOffset) == GameAddress::kSpawnKindMonster &&
                    record[GameAddress::kMonsterSpawnLiveOffset] != 0)
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
            for (uint8_t* entity = WorldApi::FirstLiveEntity();
                 entity != nullptr && Readable(entity, GameAddress::kEntitySize);
                 entity = WorldApi::NextLiveEntity(entity))
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

        int __cdecl ApiGetCount(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            int count = 0;
            for (uint8_t* entity = WorldApi::FirstLiveEntity();
                 entity != nullptr && Readable(entity, GameAddress::kEntitySize);
                 entity = WorldApi::NextLiveEntity(entity))
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

        // somewhere nothing looks at.
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

            return think == GameModule::Resolve(GameAddress::kMonsterThinkMummy) ||
                   think == GameModule::Resolve(GameAddress::kMonsterThinkMummy2);
        }

        int __cdecl ApiGet(AmodMod mod, int index, AmodMonster* out)
        {
            const uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            const uint8_t* const record = entity != nullptr ? MonsterPlacement(entity) : nullptr;
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

            for (int axis = 0; axis < 3; ++axis)
            {
                filled.position[axis] = Read<float>(entity, GameAddress::kEntityPositionOffset +
                                                                static_cast<uintptr_t>(axis) * sizeof(float));
            }

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        typedef int (__cdecl* MoveRoutine)(void* state, int dirX, int dirY, int dirZ, float delta,
                                           int flags);

        int __cdecl ApiMove(AmodMod mod, int index, int dirX, int dirY, int dirZ)
        {
            uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            const MoveRoutine step =
                reinterpret_cast<MoveRoutine>(GameModule::Resolve(GameAddress::kEntityMoveStep));

            if (entity == nullptr || step == nullptr)
            {
                return -1;
            }

            const float delta = Read<float>(entity, GameAddress::kEntityDeltaOffset);
            return step(entity + GameAddress::kEntityMoveStateOffset, dirX, dirY, dirZ, delta, 0) != 0
                       ? 1
                       : 0;
        }

        typedef void (__cdecl* KillRoutine)(void* entity);

        int __cdecl ApiKill(AmodMod mod, int index)
        {
            uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            const uint8_t* const record = entity != nullptr ? MonsterPlacement(entity) : nullptr;
            const KillRoutine kill =
                reinterpret_cast<KillRoutine>(GameModule::Resolve(GameAddress::kMonsterKill));

            if (record == nullptr || kill == nullptr)
            {
                return 0;
            }

            const bool counts = record[GameAddress::kMonsterSpawnDecorOffset] == 0;
            kill(entity);

            // caused it.
            int* const killed =
                counts ? static_cast<int*>(GameModule::Resolve(GameAddress::kMonstersKilled))
                       : nullptr;

            if (killed != nullptr)
            {
                *killed += 1;
            }
            return 1;
        }

        constexpr Field kMonsterFields[] =
        {
            { "spawn_speed", GameAddress::kMonsterSpawnSpeedOffset,      Kind::Float, 0, 0, 0, true },
            { "state",       GameAddress::kMonsterSpawnStateOffset,      Kind::Int,   0, 0, 0, true },
            { "time_left",   GameAddress::kMonsterSpawnTimerOffset,      Kind::Float, 0, 0, 0, true },
            { "level",       GameAddress::kMonsterSpawnLevelOffset,      Kind::Int,   0, 0, 0, true },
            { "dir",         GameAddress::kMonsterSpawnDirOffset,        Kind::Int,   0, 0, 0, true },
            { "attack_dist", GameAddress::kMonsterSpawnAttackDistOffset, Kind::Float,  0, 0, 0, true },
            { "attack_time", GameAddress::kMonsterSpawnAttackTimeOffset, Kind::Floats, 2, 0, 0, true },
            { "bluff_time",  GameAddress::kMonsterSpawnBluffTimeOffset,  Kind::Floats, 2, 0, 0, true },
            { "walk_time",   GameAddress::kMonsterSpawnWalkTimeOffset,   Kind::Floats, 2, 0, 0, true },
            { "sit_time",    GameAddress::kMonsterSpawnSitTimeOffset,    Kind::Floats, 2, 0, 0, true },
            { "wait_time",   GameAddress::kMonsterSpawnWaitTimeOffset,   Kind::Floats, 2, 0, 0, true },
            { "pos_lerp",    GameAddress::kMonsterSpawnPosLerpOffset,    Kind::Float, 0, 0, 0, true },
            { "decor",       GameAddress::kMonsterSpawnDecorOffset,      Kind::Int,   0, 0, 0, false },

            { "live",        GameAddress::kMonsterSpawnLiveOffset,       Kind::Int,   0, 0, 0, true },
            { "tag",         GameAddress::kMonsterSpawnTagOffset,        Kind::Text,  0, 0, 0, false },
        };

        constexpr size_t kMonsterFieldCount = sizeof(kMonsterFields) / sizeof(Field);

        constexpr Field kEntityFields[] =
        {
            { "facing",  GameAddress::kMonsterEntityFacingOffset,   Kind::Int,   0, 0, 0, true },
            { "x",       GameAddress::kEntityPositionOffset,        Kind::Float, 0, 0, 0, true },
            { "y",       GameAddress::kEntityPositionOffset + 0x04, Kind::Float, 0, 0, 0, true },
            { "z",       GameAddress::kEntityPositionOffset + 0x08, Kind::Float, 0, 0, 0, true },

            // dressed differently from the rest of its kind without touching the type.
            { "model",   GameAddress::kEntityModelOffset,   Kind::Resource, 0, 0, GameAddress::kResolveModelSlot,   true },
            { "texture", GameAddress::kEntityTextureOffset, Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true },
        };

        constexpr size_t kEntityFieldCount = sizeof(kEntityFields) / sizeof(Field);

        constexpr Field kSpeedInEntity =
            { "speed", GameAddress::kMonsterEntitySpeedOffset, Kind::Float, 0, 0, 0, true };
        constexpr Field kSpeedInRecord =
            { "speed", GameAddress::kMonsterSpawnSpeedOffset, Kind::Float, 0, 0, 0, true };

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
            return (Caller(mod) != nullptr && MonsterAt(index) != nullptr &&
                    (SpeedKey(key) ||
                     ApiProperty::Find(kMonsterFields, kMonsterFieldCount, key) != nullptr ||
                     ApiProperty::Find(kEntityFields, kEntityFieldCount, key) != nullptr))
                       ? 1
                       : 0;
        }

        int __cdecl ApiGetProperty(AmodMod mod, int index, const char* key, AmodValue* value)
        {
            const uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            const uint8_t* const record = entity != nullptr ? MonsterPlacement(entity) : nullptr;
            if (record == nullptr || value == nullptr)
            {
                return 0;
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
            return field != nullptr ? ApiProperty::Get(record, *field, value) : 0;
        }

        int __cdecl ApiSetProperty(AmodMod mod, int index, const char* key, const AmodValue* value)
        {
            uint8_t* const entity = Caller(mod) != nullptr ? MonsterAt(index) : nullptr;
            uint8_t* const record = entity != nullptr ? MonsterPlacement(entity) : nullptr;
            if (record == nullptr || value == nullptr)
            {
                return 0;
            }

            if (SpeedKey(key))
            {
                if (value->kind != AMOD_VALUE_FLOAT || value->count < 1)
                {
                    return 0;
                }
                SetSpeed(entity, record, value->numbers[0]);
                return 1;
            }

            const Field* const mine = ApiProperty::Find(kEntityFields, kEntityFieldCount, key);
            if (mine != nullptr)
            {
                return ApiProperty::Set(entity, *mine, *value);
            }

            const Field* const field = ApiProperty::Find(kMonsterFields, kMonsterFieldCount, key);
            return field != nullptr ? ApiProperty::Set(record, *field, *value) : 0;
        }

        // built from, which is what a mod reaches here.

        uint8_t* PlacementAt(int roomIndex, int index)
        {
            uint8_t* const room = roomIndex < 0 ? WorldApi::CurrentRoom()
                                                : WorldApi::RoomAt(roomIndex);

            if (room == nullptr ||
                !Readable(room, GameAddress::kRoomSpawnCountOffset + sizeof(int)) || index < 0)
            {
                return nullptr;
            }

            uint8_t** const list = Read<uint8_t**>(room, GameAddress::kRoomSpawnListOffset);
            const int count = Read<int>(room, GameAddress::kRoomSpawnCountOffset);

            if (list == nullptr || count <= 0 ||
                !Readable(list, static_cast<size_t>(count) * sizeof(void*)))
            {
                return nullptr;
            }

            int seen = 0;
            for (int at = 0; at < count; ++at)
            {
                uint8_t* const record = list[at];
                if (record == nullptr || !Readable(record, GameAddress::kMonsterSpawnSize) ||
                    Read<int>(record, GameAddress::kSpawnKindOffset) != GameAddress::kSpawnKindMonster)
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

            for (int axis = 0; axis < 3; ++axis)
            {
                filled.position[axis] = Read<float>(record, GameAddress::kMonsterSpawnPositionOffset +
                                                                static_cast<uintptr_t>(axis) * sizeof(float));
            }

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiGetPlacedProperty(AmodMod mod, int roomIndex, int index, const char* key,
                                         AmodValue* value)
        {
            const uint8_t* const record = Caller(mod) != nullptr ? PlacementAt(roomIndex, index)
                                                                 : nullptr;
            const Field* const field = ApiProperty::Find(kMonsterFields, kMonsterFieldCount, key);

            return (record != nullptr && field != nullptr) ? ApiProperty::Get(record, *field, value)
                                                           : 0;
        }

        int __cdecl ApiSetPlacedProperty(AmodMod mod, int roomIndex, int index, const char* key,
                                         const AmodValue* value)
        {
            uint8_t* const record = Caller(mod) != nullptr ? PlacementAt(roomIndex, index) : nullptr;
            const Field* const field = ApiProperty::Find(kMonsterFields, kMonsterFieldCount, key);

            return (record != nullptr && field != nullptr && value != nullptr)
                       ? ApiProperty::Set(record, *field, *value)
                       : 0;
        }

        int __cdecl ApiGetKilledCount(AmodMod mod)
        {
            const int* const killed =
                static_cast<const int*>(GameModule::Resolve(GameAddress::kMonstersKilled));
            return (Caller(mod) != nullptr && killed != nullptr) ? *killed : 0;
        }

        // and lets every other placement through untouched.

        struct DefinedKind
        {
            ModContext* owner;
            std::string name;
            std::string movesLike;
            void* borrowedThink;                    // to hand a monster back when the mod goes
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

        typedef void (__cdecl* SpawnPlacementRoutine)(void* record);

        InlineHook g_spawnHook;
        SpawnPlacementRoutine g_originalSpawn = nullptr;

        // The creator of one of the ten, out of the game's own table.
        SpawnPlacementRoutine CreatorFor(const std::string& movesLike)
        {
            const uint8_t* const table =
                static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kMonsterSpawnTable));
            if (table == nullptr ||
                !Readable(table, GameAddress::kMonsterSpawnTableCount * 2 * sizeof(void*)))
            {
                return nullptr;
            }

            for (size_t at = 0; at < GameAddress::kMonsterSpawnTableCount; ++at)
            {
                const char* const name =
                    Read<const char*>(table, at * 2 * sizeof(void*));
                const void* const creator =
                    Read<const void*>(table, at * 2 * sizeof(void*) + sizeof(void*));

                if (name != nullptr && Readable(name, 1) && movesLike == name)
                {
                    return reinterpret_cast<SpawnPlacementRoutine>(const_cast<void*>(creator));
                }
            }
            return nullptr;
        }

        // block the parser is still in, into the placement it has just appended.

        typedef void (__cdecl* ParseSpawnRoutine)(void* room, void* grid, void* parser);
        typedef int (__cdecl* FindKeyRoutine)(void* parser, const char* key);
        typedef int (__cdecl* ReadFloat4Routine)(void* parser, float* out);

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

            if (list == nullptr || count <= 0 ||
                !Readable(list, static_cast<size_t>(count) * sizeof(void*)))
            {
                return nullptr;
            }

            uint8_t* const record = list[count - 1];
            return (record != nullptr && Readable(record, GameAddress::kMonsterSpawnSize) &&
                    Read<int>(record, GameAddress::kSpawnKindOffset) == GameAddress::kSpawnKindMonster)
                       ? record
                       : nullptr;
        }

        bool ReadPos(void* parser, float* pos)
        {
            const FindKeyRoutine find =
                reinterpret_cast<FindKeyRoutine>(GameModule::Resolve(GameAddress::kParserFindKey));
            const ReadFloat4Routine read =
                reinterpret_cast<ReadFloat4Routine>(GameModule::Resolve(GameAddress::kParserReadFloat4));

            return find != nullptr && read != nullptr && find(parser, "pos") != 0 &&
                   read(parser, pos) != 0;
        }

        // may walk from where it was put.
        void ApplyPatrolRange(uint8_t* record, const float* pos)
        {
            const float left = pos[2] < 0.0f ? -pos[2] : pos[2];
            const float right = pos[3] < 0.0f ? -pos[3] : pos[3];

            WriteFloat(record, 0x5C, left * GameAddress::kCellToWorld);
            WriteFloat(record, 0x60, right * GameAddress::kCellToWorld);
        }

        // put, from the midpoint, over half the distance, along the unit direction.
        void ApplyArc(uint8_t* record, const float* pos)
        {
            const float second[3] =
            {
                pos[2] * GameAddress::kCellToWorld + GameAddress::kCellOriginX + GameAddress::kCellCentre,
                GameAddress::kCellOriginY - pos[3] * GameAddress::kCellToWorld - GameAddress::kCellCentre,
                GameAddress::kMonsterCellZ
            };

            float axis[3];
            float length = 0.0f;

            for (int at = 0; at < 3; ++at)
            {
                axis[at] = Read<float>(record, GameAddress::kMonsterSpawnPositionOffset +
                                                   static_cast<uintptr_t>(at) * sizeof(float)) -
                           second[at];
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
                WriteFloat(record, GameAddress::kMonsterSpawnAxisOffset +
                                       static_cast<uintptr_t>(at) * sizeof(float), axis[at]);
                WriteFloat(record, GameAddress::kMonsterSpawnPositionOffset +
                                       static_cast<uintptr_t>(at) * sizeof(float),
                           second[at] + axis[at] * radius);
            }
            WriteFloat(record, GameAddress::kMonsterSpawnRadiusOffset, radius);
        }

        void ApplyMovementFields(uint8_t* record, const std::string& movesLike, void* parser)
        {
            const bool patrols = movesLike == "skull" || movesLike == "j_skull" ||
                                 movesLike == "mummy" || movesLike == "mummy2";
            const bool arcs = movesLike == "bat";

            if (!patrols && !arcs)
            {
                return;     // the spider, the scorpion and the snakes read nothing extra
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
            void* const target = GameModule::Resolve(GameAddress::kMonsterParseSpawnRoutine);
            if (target == nullptr)
            {
                return false;
            }

            std::string mismatch;
            if (!GameModule::VerifyBytes(target, kMonsterParseSpawnPrologue,
                                         sizeof(kMonsterParseSpawnPrologue), mismatch))
            {
                FORGE_LOG_ERROR("Monster_ParseSpawn does not open as expected (%s), so a kind of a "
                                "mod's own will spawn without the fields its movement needs",
                                mismatch.c_str());
                return false;
            }

            std::string error;
            if (!g_parseHook.Install(target, reinterpret_cast<void*>(&ParseDetour),
                                     kMonsterParseSpawnPrologue,
                                     sizeof(kMonsterParseSpawnPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Monster_ParseSpawn: %s", error.c_str());
                return false;
            }

            g_originalParse = reinterpret_cast<ParseSpawnRoutine>(g_parseHook.Trampoline());
            return true;
        }

        uint8_t* EntityOf(const uint8_t* record)
        {
            for (uint8_t* entity = WorldApi::FirstLiveEntity();
                 entity != nullptr && Readable(entity, GameAddress::kEntitySize);
                 entity = WorldApi::NextLiveEntity(entity))
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
            for (uint8_t* at = WorldApi::FirstLiveEntity();
                 at != nullptr && Readable(at, GameAddress::kEntitySize);
                 at = WorldApi::NextLiveEntity(at))
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

        // movement it borrowed rather than left with a think that does nothing.
        void __cdecl ThinkThunk(void* self)
        {
            uint8_t* const entity = static_cast<uint8_t*>(self);
            const uint8_t* const record = MonsterPlacement(entity);
            DefinedKind* const kind =
                record != nullptr ? KindOf(ReadText(record, GameAddress::kMonsterSpawnNameOffset))
                                  : nullptr;

            if (kind == nullptr || kind->behaviour == nullptr)
            {
                void* const back = kind != nullptr ? kind->borrowedThink : nullptr;
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
            info.delta = Read<float>(entity, GameAddress::kEntityDeltaOffset);
            info.facing = Read<int>(entity, GameAddress::kMonsterEntityFacingOffset);

            for (int axis = 0; axis < 3; ++axis)
            {
                info.position[axis] = Read<float>(entity, GameAddress::kEntityPositionOffset +
                                                              static_cast<uintptr_t>(axis) * sizeof(float));
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

            if (SpeedInEntity(entity))
            {
                WriteFloat(entity, GameAddress::kMonsterEntitySpeedOffset,
                           Read<float>(record, GameAddress::kMonsterSpawnSpeedOffset));
            }

            // kind ends up looking the way the placement asked.
            const int dir = Read<int>(record, GameAddress::kMonsterSpawnDirOffset) < 0 ? -1 : 1;
            memcpy(entity + GameAddress::kMonsterEntityFacingOffset, &dir, sizeof(dir));
        }

        void __cdecl SpawnDetour(void* placement)
        {
            uint8_t* const record = static_cast<uint8_t*>(placement);
            const bool ours = !g_kinds.empty() && record != nullptr &&
                              Readable(record, GameAddress::kMonsterSpawnSize) &&
                              Read<int>(record, GameAddress::kSpawnKindOffset) ==
                                  GameAddress::kSpawnKindMonster &&
                              record[GameAddress::kMonsterSpawnLiveOffset] != 0;

            DefinedKind* const kind =
                ours ? KindOf(ReadText(record, GameAddress::kMonsterSpawnNameOffset)) : nullptr;

            if (kind == nullptr)
            {
                g_originalSpawn(placement);
                if (ours)
                {
                    CarryOverSpeed(record);
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

            // with a behaviour of its own takes that slot over instead.
            if (kind->behaviour != nullptr)
            {
                uint8_t* const entity = EntityOf(record);
                if (entity != nullptr)
                {
                    void* const mine = reinterpret_cast<void*>(&ThinkThunk);
                    memcpy(entity + GameAddress::kEntityThinkOffset, &mine, sizeof(mine));
                }
            }
        }

        bool InstallSpawnHook()
        {
            void* const target = GameModule::Resolve(GameAddress::kMonsterSpawnPlacement);
            if (target == nullptr)
            {
                return false;
            }

            std::string mismatch;
            if (!GameModule::VerifyBytes(target, kMonsterSpawnPlacementPrologue,
                                         sizeof(kMonsterSpawnPlacementPrologue), mismatch))
            {
                FORGE_LOG_ERROR("Monster_SpawnPlacement does not open as expected (%s), so a mod "
                                "cannot teach it a name of its own", mismatch.c_str());
                return false;
            }

            std::string error;
            if (!g_spawnHook.Install(target, reinterpret_cast<void*>(&SpawnDetour),
                                     kMonsterSpawnPlacementPrologue,
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
            if (context == nullptr || g_originalSpawn == nullptr || name == nullptr ||
                name[0] == '\0' || movesLike == nullptr || movesLike[0] == '\0')
            {
                return 0;
            }

            if (CreatorFor(movesLike) == nullptr)
            {
                FORGE_LOG_ERROR("'%s' is not one of the ten kinds of movement the game has, so "
                                "'%s' has nothing to move like", movesLike, name);
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

        int __cdecl ApiDefineBehaviour(AmodMod mod, const char* name,
                                       AmodMonsterThinkHandler handler, void* user)
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

        typedef void (__cdecl* MonsterKillRoutine)(void* entity);

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

            for (int axis = 0; axis < 3; ++axis)
            {
                info.position[axis] = Read<float>(entity, GameAddress::kEntityPositionOffset +
                                                              static_cast<uintptr_t>(axis) * sizeof(float));
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

            const AmodMonsterKillResult verdict =
                (!g_killHandlers.empty() && record != nullptr) ? AskHandlers(self, record)
                                                               : AMOD_MONSTER_KILL_DEFAULT;

            switch (verdict)
            {
            case AMOD_MONSTER_KILL_REFUSE:
                return;

            case AMOD_MONSTER_KILL_HANDLED:
                self[GameAddress::kEntityDeadOffset] = 1;
                const_cast<uint8_t*>(record)[GameAddress::kMonsterSpawnLiveOffset] = 0;
                return;

            default:
                g_originalKill(entity);
                return;
            }
        }

        bool InstallKillHook()
        {
            void* const target = GameModule::Resolve(GameAddress::kMonsterKill);
            if (target == nullptr)
            {
                return false;
            }

            std::string mismatch;
            if (!GameModule::VerifyBytes(target, kMonsterKillPrologue, sizeof(kMonsterKillPrologue),
                                         mismatch))
            {
                FORGE_LOG_ERROR("Monster_Kill does not open as expected (%s), so a mod cannot be "
                                "told about a monster dying", mismatch.c_str());
                return false;
            }

            std::string error;
            if (!g_killHook.Install(target, reinterpret_cast<void*>(&KillDetour), kMonsterKillPrologue,
                                    sizeof(kMonsterKillPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Monster_Kill: %s", error.c_str());
                return false;
            }

            g_originalKill = reinterpret_cast<MonsterKillRoutine>(g_killHook.Trampoline());
            return true;
        }

        typedef void* (__cdecl* CloneRoutine)(const void* source, size_t size);
        typedef void (__cdecl* AppendPlacementRoutine)(void* room, void* record);
        typedef void (__cdecl* SpawnOneRoutine)(void* record);

        void WriteRange(uint8_t* record, uintptr_t offset, float low, float high)
        {
            WriteFloat(record, offset, low);
            WriteFloat(record, offset + sizeof(float), high);
        }

        bool Place(const AmodMonsterPlacement& where)
        {
            if (where.name == nullptr ||
                where.column < 0 || where.column >= static_cast<int>(GameAddress::kRoomGridColumns) ||
                where.row < 0 || where.row >= static_cast<int>(GameAddress::kRoomGridRows))
            {
                return false;
            }

            const uint8_t* const type = TypeByName(where.name);
            uint8_t* const room = WorldApi::CurrentRoom();

            const CloneRoutine clone =
                reinterpret_cast<CloneRoutine>(GameModule::Resolve(GameAddress::kMemClone));
            const AppendPlacementRoutine append =
                reinterpret_cast<AppendPlacementRoutine>(GameModule::Resolve(GameAddress::kRoomSpawnsAppend));
            const SpawnOneRoutine spawn =
                reinterpret_cast<SpawnOneRoutine>(GameModule::Resolve(GameAddress::kMonsterSpawnPlacement));

            if (type == nullptr || room == nullptr || clone == nullptr || append == nullptr ||
                spawn == nullptr)
            {
                return false;
            }

            const int kind = GameAddress::kSpawnKindMonster;
            const bool hard = where.level == 1 || where.level == 3;
            const int facing = where.facing < 0 ? -1 : 1;

            uint8_t record[GameAddress::kMonsterSpawnSize] = {};
            memcpy(record + GameAddress::kSpawnKindOffset, &kind, sizeof(kind));
            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kMonsterSpawnNameOffset),
                      GameAddress::kSpawnNameLength, where.name, _TRUNCATE);

            WriteFloat(record, GameAddress::kMonsterSpawnPositionOffset,
                       where.column * GameAddress::kCellToWorld + GameAddress::kCellOriginX +
                           GameAddress::kCellCentre);
            WriteFloat(record, GameAddress::kMonsterSpawnPositionOffset + 0x04,
                       GameAddress::kCellOriginY - where.row * GameAddress::kCellToWorld -
                           GameAddress::kCellCentre);
            WriteFloat(record, GameAddress::kMonsterSpawnPositionOffset + 0x08,
                       GameAddress::kMonsterCellZ);

            memcpy(record + GameAddress::kMonsterSpawnLevelOffset, &where.level, sizeof(int));
            record[GameAddress::kMonsterSpawnLiveOffset] = 1;
            record[0x94] = 1;
            record[GameAddress::kMonsterSpawnDecorOffset] = where.decor != 0 ? 1 : 0;

            WriteFloat(record, GameAddress::kMonsterSpawnSpeedOffset,
                       Read<float>(type, GameAddress::kMonsterResSpeedOffset) * (hard ? 2.0f : 1.0f));
            memcpy(record + GameAddress::kMonsterSpawnDirOffset, &facing, sizeof(facing));
            WriteFloat(record, GameAddress::kMonsterSpawnAttackDistOffset,
                       (where.attackDist > 0.0f ? where.attackDist : 5.0f) * GameAddress::kCellToWorld);

            // The ranges the parser hands out when a script leaves them out.
            WriteRange(record, GameAddress::kMonsterSpawnAttackTimeOffset, 5.0f, 10.0f);
            WriteRange(record, GameAddress::kMonsterSpawnBluffTimeOffset, 5.0f, 10.0f);
            WriteRange(record, GameAddress::kMonsterSpawnWalkTimeOffset, 10.0f, 20.0f);
            WriteRange(record, GameAddress::kMonsterSpawnWalkWaitOffset, 5.0f, 10.0f);
            WriteRange(record, GameAddress::kMonsterSpawnSitTimeOffset, 10.0f, 20.0f);
            WriteRange(record, GameAddress::kMonsterSpawnWaitTimeOffset, 5.0f, 10.0f);

            if (where.tag != nullptr)
            {
                strncpy_s(reinterpret_cast<char*>(record + GameAddress::kMonsterSpawnTagOffset),
                          GameAddress::kSpawnNameLength, where.tag, _TRUNCATE);
            }

            memcpy(record + GameAddress::kMonsterSpawnTypeOffset, &type, sizeof(type));

            // The same two numbers a script's $pos carries, through the same two paths.
            const float pos[4] =
            {
                static_cast<float>(where.column), static_cast<float>(where.row),
                static_cast<float>(where.extraA), static_cast<float>(where.extraB)
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
            spawn(placed);
            return true;
        }

        // not got is filled in with something sane rather than left at nothing.
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

                // It starts walking, with the timer its own keys ask for.
                const int walking = GameAddress::kMonsterStateMove;
                memcpy(record + GameAddress::kMonsterSpawnStateOffset, &walking, sizeof(walking));
                WriteFloat(record, GameAddress::kMonsterSpawnTimerOffset,
                           Read<float>(record, GameAddress::kMonsterSpawnWalkTimeOffset));
            }
            else if (moves == "bat" &&
                     Read<float>(record, GameAddress::kMonsterSpawnRadiusOffset) == 0.0f)
            {
                WriteFloat(record, GameAddress::kMonsterSpawnRadiusOffset, span);
                WriteFloat(record, GameAddress::kMonsterSpawnAxisOffset, 1.0f);
                WriteFloat(record, GameAddress::kMonsterSpawnAxisOffset + 0x04, 0.0f);
                WriteFloat(record, GameAddress::kMonsterSpawnAxisOffset + 0x08, 0.0f);

                for (int axis = 0; axis < 3; ++axis)
                {
                    const uintptr_t step = static_cast<uintptr_t>(axis) * sizeof(float);
                    WriteFloat(record, GameAddress::kMonsterSpawnPositionOffset + step,
                               Read<float>(entity, GameAddress::kEntityPositionOffset + step));
                }
            }

            // whichever the new movement reads is made to hold something.
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

            return (Caller(mod) != nullptr && Place(where)) ? 1 : 0;
        }

        int __cdecl ApiPlaceEx(AmodMod mod, const AmodMonsterPlacement* placement)
        {
            if (Caller(mod) == nullptr || placement == nullptr ||
                placement->structSize < sizeof(uint32_t))
            {
                return 0;
            }

            AmodMonsterPlacement where = {};
            memcpy(&where, placement,
                   placement->structSize < sizeof(where) ? placement->structSize : sizeof(where));

            return Place(where) ? 1 : 0;
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

        typedef void (__cdecl* TouchRoutine)(void* monster, void* other, int side);

        struct TouchSite
        {
            InlineHook hook;
            TouchRoutine original;
        };

        TouchSite g_touchSites[sizeof(kMonsterTouchSites) /
                               sizeof(kMonsterTouchSites[0])] = {};

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
                info.position[axis] = Read<float>(monster, GameAddress::kEntityPositionOffset +
                                                               static_cast<uintptr_t>(axis) * sizeof(float));
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

        // One per site, so each knows which original to fall through to.
        template <size_t Site>
        void __cdecl TouchDetour(void* monster, void* other, int side)
        {
            if (AskTouch(static_cast<const uint8_t*>(monster), static_cast<const uint8_t*>(other),
                         side) == AMOD_MONSTER_TOUCH_DEFAULT)
            {
                g_touchSites[Site].original(monster, other, side);
            }
        }

        bool InstallTouchHooks()
        {
            static void* const detours[] =
            {
                reinterpret_cast<void*>(&TouchDetour<0>), reinterpret_cast<void*>(&TouchDetour<1>),
                reinterpret_cast<void*>(&TouchDetour<2>), reinterpret_cast<void*>(&TouchDetour<3>),
                reinterpret_cast<void*>(&TouchDetour<4>), reinterpret_cast<void*>(&TouchDetour<5>),
            };

            size_t at = 0;
            for (const MonsterTouchSite& site : kMonsterTouchSites)
            {
                void* const target = GameModule::Resolve(site.address);
                std::string mismatch;

                if (target == nullptr ||
                    !GameModule::VerifyBytes(target, site.prologue, site.length, mismatch))
                {
                    FORGE_LOG_ERROR("The monster touch at 0x%08X is not what it was reversed as",
                                    static_cast<unsigned>(site.address));
                    return false;
                }

                std::string error;
                if (!g_touchSites[at].hook.Install(target, detours[at], site.prologue, site.length,
                                                   error))
                {
                    FORGE_LOG_ERROR("Cannot hook the monster touch at 0x%08X: %s",
                                    static_cast<unsigned>(site.address), error.c_str());
                    return false;
                }

                g_touchSites[at].original =
                    reinterpret_cast<TouchRoutine>(g_touchSites[at].hook.Trampoline());
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

            return context->owned.Add(&ReleaseTouchHandler,
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(entry.id)));
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

            return context->owned.Add(&ReleaseKillHandler,
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(entry.id)));
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

        FrameApi::AddInternalTick(&ApplyPendingTypes);

        // Before the game reads monster_res.txt, which it does at 85% of the startup load.
        OpenJumpKeys();

        // gives it something to move along.
        if (InstallSpawnHook() && InstallParseHook())
        {
            g_table.DefineKind = &ApiDefineKind;
            g_table.DefineBehaviour = &ApiDefineBehaviour;
        }
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
        g_killHandlers.clear();
        g_touchHandlers.clear();
        g_pendingTypes.clear();
        g_definedTypes.clear();
        g_table = AmodMonstersApi{};
    }

    const AmodMonstersApi* MonstersApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void MonstersApi::ForgetMod(ModContext* owner)
    {

        // kind that knows what that was is thrown away.
        for (uint8_t* entity = WorldApi::FirstLiveEntity();
             entity != nullptr && Readable(entity, GameAddress::kEntitySize);
             entity = WorldApi::NextLiveEntity(entity))
        {
            const uint8_t* const record = MonsterPlacement(entity);
            const DefinedKind* const kind =
                record != nullptr ? KindOf(ReadText(record, GameAddress::kMonsterSpawnNameOffset))
                                  : nullptr;

            if (kind != nullptr && kind->owner == owner && kind->borrowedThink != nullptr)
            {
                memcpy(entity + GameAddress::kEntityThinkOffset, &kind->borrowedThink,
                       sizeof(void*));
            }
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
