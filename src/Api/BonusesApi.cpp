#include "src/Api/BonusesApi.h"

#include "src/Api/ApiProperty.h"
#include "src/Api/ApiRead.h"
#include "src/Api/FrameApi.h"
#include "src/Api/ModContext.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;

        // --- the bonus table ---------------------------------------------------------------

        const uint8_t* BonusRecord(int index)
        {
            const uint8_t* const table =
                static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kBonusResTable));
            const int* const count =
                static_cast<const int*>(GameModule::Resolve(GameAddress::kBonusResCount));

            if (table == nullptr || count == nullptr || index < 0)
            {
                return nullptr;
            }

            const uint8_t* const records = *reinterpret_cast<const uint8_t* const*>(table);
            if (records == nullptr || index >= *count)
            {
                return nullptr;
            }

            const uint8_t* const record =
                records + static_cast<size_t>(index) * GameAddress::kBonusResStride;
            return Readable(record, GameAddress::kBonusResStride) ? record : nullptr;
        }

        void FillBonusType(const uint8_t* record, AmodBonusType& out)
        {
            const uint32_t flags = Read<uint32_t>(record, GameAddress::kBonusResFlagsOffset);

            out = AmodBonusType{};
            out.structSize = sizeof(AmodBonusType);
            out.name = ReadText(record, GameAddress::kBonusResNameOffset);
            out.resourceId = ReadText(record, GameAddress::kBonusResIdOffset);
            out.pointsOnTake = Read<int>(record, GameAddress::kBonusResPointsTakeOffset);
            out.pointsOnRunOut = Read<int>(record, GameAddress::kBonusResPointsOutOffset);
            out.time = Read<float>(record, GameAddress::kBonusResTimeOffset);
            out.burnTime = Read<float>(record, GameAddress::kBonusResBurnTimeOffset);
            out.isTotem = (flags & GameAddress::kBonusFlagTotem) != 0 ? 1 : 0;
            out.isTorch = (flags & GameAddress::kBonusFlagTorch) != 0 ? 1 : 0;
            out.makesTrain = (flags & GameAddress::kBonusFlagMakesTrain) != 0 ? 1 : 0;
            out.trainIndex = Read<int>(record, GameAddress::kBonusResTrainIndexOffset);
            out.lightRadius = Read<float>(record, GameAddress::kBonusResLightRadiusOffset);
            out.bonusLightRadius = Read<float>(record, GameAddress::kBonusResBonusLightRadiusOffset);

            for (int at = 0; at < 4; ++at)
            {
                out.glowColor[at] = Read<float>(record, GameAddress::kBonusResGlowOffset +
                                                            static_cast<uintptr_t>(at) * sizeof(float));
            }
            for (int at = 0; at < 3; ++at)
            {
                out.lightColor[at] = Read<float>(record, GameAddress::kBonusResLightColorOffset +
                                                             static_cast<uintptr_t>(at) * sizeof(float));
            }
        }

        AmodBonusesApi g_bonusTable = {};

        int __cdecl ApiGetBonusTypeCount(AmodMod mod)
        {
            const int* const count =
                static_cast<const int*>(GameModule::Resolve(GameAddress::kBonusResCount));
            if (Caller(mod) == nullptr || count == nullptr)
            {
                return 0;
            }
            return (*count > 0 && *count < 0x1000) ? *count : 0;
        }

        int __cdecl ApiGetBonusType(AmodMod mod, int index, AmodBonusType* out)
        {
            const uint8_t* const record = Caller(mod) != nullptr ? BonusRecord(index) : nullptr;
            if (record == nullptr || out == nullptr || out->structSize < sizeof(uint32_t))
            {
                return 0;
            }

            AmodBonusType filled;
            FillBonusType(record, filled);

            const uint32_t copied = out->structSize < sizeof(filled)
                                        ? out->structSize
                                        : static_cast<uint32_t>(sizeof(filled));
            memcpy(out, &filled, copied);
            memcpy(out, &copied, sizeof(copied));
            return 1;
        }

        int __cdecl ApiFindBonusType(AmodMod mod, const char* name, AmodBonusType* out)
        {
            if (Caller(mod) == nullptr || name == nullptr)
            {
                return 0;
            }

            const int count = ApiGetBonusTypeCount(mod);

            for (const uintptr_t field : { GameAddress::kBonusResNameOffset,
                                           GameAddress::kBonusResIdOffset })
            {
                for (int index = 0; index < count; ++index)
                {
                    const uint8_t* const record = BonusRecord(index);
                    if (record != nullptr && strcmp(ReadText(record, field), name) == 0)
                    {
                        return ApiGetBonusType(mod, index, out);
                    }
                }
            }
            return 0;
        }

        typedef void* (__cdecl* CloneRoutine)(const void* source, size_t size);
        typedef void (__cdecl* AppendRoutine)(void* room, void* record);
        typedef void (__cdecl* SpawnRoutine)(void* room, void* record);

        SpawnRoutine PlacementSpawner()
        {
            void** const slot =
                static_cast<void**>(GameModule::Resolve(GameAddress::kSpawnPlacementSlot));
            return slot != nullptr ? reinterpret_cast<SpawnRoutine>(*slot) : nullptr;
        }

        bool PlaceBonus(const AmodBonusPlacement& where)
        {
            if (where.name == nullptr ||
                where.column < 0 || where.column >= static_cast<int>(GameAddress::kRoomGridColumns) ||
                where.row < 0 || where.row >= static_cast<int>(GameAddress::kRoomGridRows))
            {
                return false;
            }

            uint8_t* const room = WorldApi::CurrentRoom();

            const CloneRoutine clone =
                reinterpret_cast<CloneRoutine>(GameModule::Resolve(GameAddress::kMemClone));
            const AppendRoutine append =
                reinterpret_cast<AppendRoutine>(GameModule::Resolve(GameAddress::kRoomSpawnsAppend));
            const SpawnRoutine spawn = PlacementSpawner();

            if (room == nullptr || clone == nullptr || append == nullptr || spawn == nullptr)
            {
                return false;
            }

            typedef void* (__cdecl* FindTypeRoutine)(const char* name);
            const FindTypeRoutine findType =
                reinterpret_cast<FindTypeRoutine>(GameModule::Resolve(GameAddress::kBonusResFind));

            void* const typeRecord = findType != nullptr ? findType(where.name) : nullptr;
            if (typeRecord == nullptr)
            {
                return false;
            }

            uint8_t record[GameAddress::kBonusSpawnSize] = {};
            const int kind = GameAddress::kSpawnKindBonus;
            memcpy(record + GameAddress::kSpawnKindOffset, &kind, sizeof(kind));
            record[GameAddress::kBonusSpawnLiveOffset] = 1;

            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kBonusSpawnNameOffset),
                      GameAddress::kSpawnNameLength,
                      static_cast<const char*>(typeRecord) + GameAddress::kBonusResNameOffset,
                      _TRUNCATE);

            memcpy(record + GameAddress::kBonusSpawnTypeOffset, &typeRecord, sizeof(typeRecord));

            if (where.tag != nullptr)
            {
                strncpy_s(reinterpret_cast<char*>(record + GameAddress::kBonusSpawnTagOffset),
                          GameAddress::kSpawnNameLength, where.tag, _TRUNCATE);
            }
            if (where.time > 0.0f)
            {
                memcpy(record + GameAddress::kBonusSpawnTimeOffset, &where.time, sizeof(float));
            }
            record[GameAddress::kBonusSpawnDecorOffset] = where.decor != 0 ? 1 : 0;

            const float position[3] =
            {
                where.column * GameAddress::kCellToWorld + GameAddress::kCellOriginX + GameAddress::kCellCentre,
                GameAddress::kCellOriginY - where.row * GameAddress::kCellToWorld - GameAddress::kCellCentre,
                GameAddress::kBonusHeight
            };
            memcpy(record + GameAddress::kBonusSpawnPositionOffset, position, sizeof(position));

            void* const placed = clone(record, GameAddress::kBonusSpawnSize);
            if (placed == nullptr)
            {
                return false;
            }

            append(room, placed);
            spawn(room, placed);
            return true;
        }

        int __cdecl ApiPlaceBonus(AmodMod mod, const char* name, int column, int row)
        {
            AmodBonusPlacement where = {};
            where.structSize = sizeof(where);
            where.name = name;
            where.column = column;
            where.row = row;

            return (Caller(mod) != nullptr && PlaceBonus(where)) ? 1 : 0;
        }

        int __cdecl ApiPlaceBonusEx(AmodMod mod, const AmodBonusPlacement* placement)
        {
            if (Caller(mod) == nullptr || placement == nullptr ||
                placement->structSize < sizeof(uint32_t))
            {
                return 0;
            }

            AmodBonusPlacement where = {};
            memcpy(&where, placement,
                   placement->structSize < sizeof(where) ? placement->structSize : sizeof(where));

            return PlaceBonus(where) ? 1 : 0;
        }

        // --- teaching the game a new kind of bonus -----------------------------------------

        typedef uint32_t (__cdecl* ResolveResourceRoutine)(const char* name);
        typedef uint8_t* (__stdcall* BonusResAppendRoutine)(uint8_t** table, int* count);

        // Zero for a name the game has no such resource under, which is a refusal rather than
        // something to write into a record.
        uint32_t ResolveResource(uintptr_t slot, const char* name)
        {
            void** const at = static_cast<void**>(GameModule::Resolve(slot));
            const ResolveResourceRoutine routine =
                at != nullptr ? reinterpret_cast<ResolveResourceRoutine>(*at) : nullptr;

            return (routine != nullptr && name != nullptr) ? routine(name) : 0;
        }

        int BonusTypeTotal()
        {
            const int* const count =
                static_cast<const int*>(GameModule::Resolve(GameAddress::kBonusResCount));
            return (count != nullptr && *count > 0 && *count < 0x1000) ? *count : 0;
        }

        int BonusRecordIndexOf(const char* text)
        {
            const int total = BonusTypeTotal();

            for (int index = 0; index < total; ++index)
            {
                const uint8_t* const record = BonusRecord(index);
                if (record != nullptr &&
                    strcmp(ReadText(record, GameAddress::kBonusResIdOffset), text) == 0)
                {
                    return index;
                }
            }
            return -1;
        }

        bool WriteResource(uint8_t* record, uintptr_t offset, uintptr_t slot, const char* name,
                           const char* what, const char* id)
        {
            if (name == nullptr)
            {
                return true;
            }

            const uint32_t resolved = ResolveResource(slot, name);
            if (resolved == 0)
            {
                FORGE_LOG_ERROR("Bonus '%s' asks for a %s the game has no '%s' for", id, what, name);
                return false;
            }

            memcpy(record + offset, &resolved, sizeof(resolved));
            return true;
        }

        struct DefinedType
        {
            std::string id;
            ModContext* owner;
        };

        std::vector<DefinedType> g_definedTypes;

        // The record this mod defined and still owns, or null.
        uint8_t* OwnedType(ModContext* caller, const char* resourceId)
        {
            if (caller == nullptr || resourceId == nullptr)
            {
                return nullptr;
            }

            for (const DefinedType& defined : g_definedTypes)
            {
                if (defined.owner == caller && defined.id == resourceId)
                {
                    const int index = BonusRecordIndexOf(resourceId);
                    return index >= 0 ? const_cast<uint8_t*>(BonusRecord(index)) : nullptr;
                }
            }
            return nullptr;
        }

        typedef void (__cdecl* FreeBonusResourcesRoutine)();

        InlineHook g_freeBonusResourcesHook;
        FreeBonusResourcesRoutine g_originalFreeBonusResources = nullptr;

        void __cdecl FreeBonusResourcesDetour()
        {
            static const uintptr_t owned[] =
            {
                GameAddress::kBonusResTextureOffset, GameAddress::kBonusResTexAnimOffset,
                GameAddress::kBonusResModelOffset,   GameAddress::kBonusResIconOffset,
                GameAddress::kBonusResSoundTakeOffset, GameAddress::kBonusResSoundUseOffset
            };

            for (const DefinedType& defined : g_definedTypes)
            {
                const int index = BonusRecordIndexOf(defined.id.c_str());
                uint8_t* const record = index >= 0 ? const_cast<uint8_t*>(BonusRecord(index))
                                                   : nullptr;
                if (record == nullptr)
                {
                    continue;
                }

                for (const uintptr_t offset : owned)
                {
                    memset(record + offset, 0, sizeof(uint32_t));
                }
            }

            g_originalFreeBonusResources();
        }

        bool WatchBonusTeardown()
        {
            if (g_originalFreeBonusResources != nullptr)
            {
                return true;
            }

            void* const target = GameSymbols::Address(GameSymbol::BonusesFreeResources);
            std::string mismatch;
            if (target == nullptr ||
                !GameModule::VerifyBytes(target, kBonusesFreeResourcesPrologue,
                                         sizeof(kBonusesFreeResourcesPrologue), mismatch))
            {
                FORGE_LOG_ERROR("Bonuses_FreeResources does not open as expected, so a type cannot "
                                "be added without the game freeing its resources twice");
                return false;
            }

            std::string error;
            if (!g_freeBonusResourcesHook.Install(target,
                                                 reinterpret_cast<void*>(&FreeBonusResourcesDetour),
                                                 kBonusesFreeResourcesPrologue,
                                                 sizeof(kBonusesFreeResourcesPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Bonuses_FreeResources: %s", error.c_str());
                return false;
            }

            g_originalFreeBonusResources =
                reinterpret_cast<FreeBonusResourcesRoutine>(g_freeBonusResourcesHook.Trampoline());
            return true;
        }

        bool DefineBonusType(const AmodBonusTypeDef& wanted, ModContext* owner)
        {
            if (!WatchBonusTeardown())
            {
                return false;
            }

            if (BonusRecordIndexOf(wanted.resourceId) >= 0)
            {
                FORGE_LOG_ERROR("A bonus of id '%s' is already known", wanted.resourceId);
                return false;
            }

            const int base = BonusRecordIndexOf(wanted.basedOn);
            const uint8_t* const source = base >= 0 ? BonusRecord(base) : nullptr;
            if (source == nullptr)
            {
                FORGE_LOG_ERROR("Bonus '%s' is based on '%s', which is not one of the %d types the "
                                "game knows", wanted.resourceId, wanted.basedOn, BonusTypeTotal());
                return false;
            }

            uint8_t** const table =
                static_cast<uint8_t**>(GameModule::Resolve(GameAddress::kBonusResTable));
            int* const count = static_cast<int*>(GameModule::Resolve(GameAddress::kBonusResCount));
            const BonusResAppendRoutine appendType =
                reinterpret_cast<BonusResAppendRoutine>(GameModule::Resolve(GameAddress::kBonusResAppend));

            if (table == nullptr || count == nullptr || appendType == nullptr)
            {
                return false;
            }

            uint8_t record[GameAddress::kBonusResStride];
            memcpy(record, source, sizeof(record));

            const char* const behaviour = wanted.behavesAs != nullptr && wanted.behavesAs[0] != '\0'
                                              ? wanted.behavesAs
                                              : wanted.resourceId;

            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kBonusResNameOffset),
                      GameAddress::kSpawnNameLength, behaviour, _TRUNCATE);
            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kBonusResIdOffset),
                      GameAddress::kSpawnNameLength, wanted.resourceId, _TRUNCATE);

            if (!WriteResource(record, GameAddress::kBonusResModelOffset, GameAddress::kResolveModelSlot,
                               wanted.model, "model", wanted.resourceId) ||
                !WriteResource(record, GameAddress::kBonusResTextureOffset, GameAddress::kResolveTextureSlot,
                               wanted.texture, "texture", wanted.resourceId) ||
                !WriteResource(record, GameAddress::kBonusResIconOffset, GameAddress::kResolveTextureSlot,
                               wanted.icon, "icon", wanted.resourceId) ||
                !WriteResource(record, GameAddress::kBonusResSoundTakeOffset, GameAddress::kResolveSoundSlot,
                               wanted.soundOnTake, "sound", wanted.resourceId) ||
                !WriteResource(record, GameAddress::kBonusResSoundUseOffset, GameAddress::kResolveSoundSlot,
                               wanted.soundOnUse, "sound", wanted.resourceId))
            {
                return false;
            }

            if ((wanted.fields & AMOD_BONUS_FIELD_POINTS_TAKE) != 0)
            {
                memcpy(record + GameAddress::kBonusResPointsTakeOffset, &wanted.pointsOnTake,
                       sizeof(int));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_POINTS_OUT) != 0)
            {
                memcpy(record + GameAddress::kBonusResPointsOutOffset, &wanted.pointsOnRunOut,
                       sizeof(int));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_TIME) != 0)
            {
                memcpy(record + GameAddress::kBonusResTimeOffset, &wanted.time, sizeof(float));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_GLOW) != 0)
            {
                memcpy(record + GameAddress::kBonusResGlowOffset, wanted.glowColor,
                       sizeof(wanted.glowColor));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_GLOW2) != 0)
            {
                memcpy(record + GameAddress::kBonusResGlow2Offset, wanted.glowColor2,
                       sizeof(wanted.glowColor2));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_ROCKING) != 0)
            {
                memcpy(record + GameAddress::kBonusResRockingAmpOffset, &wanted.rockingAmp,
                       sizeof(float));
                memcpy(record + GameAddress::kBonusResRockingSpeedOffset, &wanted.rockingSpeed,
                       sizeof(float));
                memcpy(record + GameAddress::kBonusResRockingAxisOffset, wanted.rockingAxis,
                       sizeof(wanted.rockingAxis));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_LIGHT) != 0)
            {
                memcpy(record + GameAddress::kBonusResLightColorOffset, wanted.lightColor,
                       sizeof(wanted.lightColor));
                memcpy(record + GameAddress::kBonusResLightRadiusOffset, &wanted.lightRadius,
                       sizeof(float));
            }

            uint32_t flags = Read<uint32_t>(record, GameAddress::kBonusResFlagsOffset);
            if ((wanted.fields & AMOD_BONUS_FIELD_TRAIN) != 0)
            {
                flags = wanted.makesTrain != 0 ? (flags | GameAddress::kBonusFlagMakesTrain)
                                               : (flags & ~GameAddress::kBonusFlagMakesTrain);
                memcpy(record + GameAddress::kBonusResTrainIndexOffset, &wanted.trainIndex,
                       sizeof(int));
            }

            if ((wanted.fields & AMOD_BONUS_FIELD_TIMER) != 0)
            {
                flags &= ~(GameAddress::kBonusFlagTotem | GameAddress::kBonusFlagSuppressed);
                if (wanted.timer != AMOD_BONUS_TIMER_NONE)
                {
                    flags |= GameAddress::kBonusFlagTotem;
                }
                if (wanted.timer == AMOD_BONUS_TIMER_MOD)
                {
                    flags |= GameAddress::kBonusFlagSuppressed;
                }
            }
            memcpy(record + GameAddress::kBonusResFlagsOffset, &flags, sizeof(flags));

            uint8_t* const appended = appendType(table, count);
            if (appended == nullptr)
            {
                FORGE_LOG_ERROR("The bonus table would not grow for '%s'", wanted.resourceId);
                return false;
            }

            memcpy(appended, record, sizeof(record));
            DefinedType defined;
            defined.id = wanted.resourceId;
            defined.owner = owner;
            g_definedTypes.push_back(defined);
            return true;
        }

        struct PendingType
        {
            ModContext* owner;
            AmodBonusTypeDef def;

            std::string resourceId;
            std::string basedOn;
            std::string behavesAs;
            std::string model;
            std::string texture;
            std::string icon;
            std::string soundOnTake;
            std::string soundOnUse;

            void Own(const AmodBonusTypeDef& source)
            {
                def = source;

                resourceId = Text(source.resourceId);
                basedOn = Text(source.basedOn);
                behavesAs = Text(source.behavesAs);
                model = Text(source.model);
                texture = Text(source.texture);
                icon = Text(source.icon);
                soundOnTake = Text(source.soundOnTake);
                soundOnUse = Text(source.soundOnUse);
            }

            const AmodBonusTypeDef& Rebound()
            {
                def.resourceId = Pointer(resourceId);
                def.basedOn = Pointer(basedOn);
                def.behavesAs = Pointer(behavesAs);
                def.model = Pointer(model);
                def.texture = Pointer(texture);
                def.icon = Pointer(icon);
                def.soundOnTake = Pointer(soundOnTake);
                def.soundOnUse = Pointer(soundOnUse);
                return def;
            }

        private:
            static std::string Text(const char* from) { return from != nullptr ? from : ""; }
            static const char* Pointer(const std::string& text)
            {
                return text.empty() ? nullptr : text.c_str();
            }
        };

        std::vector<PendingType> g_pendingTypes;

        void __cdecl ApplyPendingTypes()
        {
            if (g_pendingTypes.empty() || BonusTypeTotal() == 0 || WorldApi::IsLevelLoaded())
            {
                return;
            }

            for (PendingType& pending : g_pendingTypes)
            {
                DefineBonusType(pending.Rebound(), pending.owner);
            }
            g_pendingTypes.clear();
        }

        int __cdecl ApiDefineBonusType(AmodMod mod, const AmodBonusTypeDef* def)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || def == nullptr || def->structSize < sizeof(uint32_t))
            {
                return 0;
            }

            AmodBonusTypeDef wanted = {};
            memcpy(&wanted, def, def->structSize < sizeof(wanted) ? def->structSize : sizeof(wanted));

            if (wanted.resourceId == nullptr || wanted.resourceId[0] == '\0' ||
                wanted.basedOn == nullptr)
            {
                FORGE_LOG_ERROR("A bonus type needs an id of its own and a type to be based on");
                return 0;
            }

            if (WorldApi::IsLevelLoaded())
            {
                FORGE_LOG_ERROR("Bonus '%s' cannot be defined while a level is loaded",
                                wanted.resourceId);
                return 0;
            }

            if (BonusTypeTotal() != 0)
            {
                return DefineBonusType(wanted, context) ? 1 : 0;
            }

            PendingType pending;
            pending.owner = context;
            pending.Own(wanted);
            g_pendingTypes.push_back(pending);
            return 1;
        }

        void RefreshSpawnedTexture(const uint8_t* type)
        {
            const uint32_t texture = Read<uint32_t>(type, GameAddress::kBonusResTextureOffset);

            for (uint8_t* entity = WorldApi::FirstLiveEntity();
                 entity != nullptr && Readable(entity, GameAddress::kEntitySize);
                 entity = WorldApi::NextLiveEntity(entity))
            {
                const uint8_t* const record = Read<const uint8_t*>(entity, GameAddress::kEntitySpawnOffset);
                if (record == nullptr || !Readable(record, GameAddress::kBonusSpawnSize) ||
                    Read<int>(record, GameAddress::kSpawnKindOffset) != GameAddress::kSpawnKindBonus ||
                    Read<const uint8_t*>(record, GameAddress::kBonusSpawnTypeOffset) != type ||
                    Read<uint8_t>(record, GameAddress::kBonusSpawnOwnModelOffset) != 0)
                {
                    continue;
                }

                memcpy(entity + GameAddress::kEntityTextureOffset, &texture, sizeof(texture));
            }
        }

        int __cdecl ApiSetBonusTexture(AmodMod mod, const char* resourceId, const char* file)
        {
            uint8_t* const type = file != nullptr ? OwnedType(Caller(mod), resourceId) : nullptr;
            const uint32_t texture = type != nullptr
                                         ? ResolveResource(GameAddress::kResolveTextureSlot, file)
                                         : 0;
            if (texture == 0)
            {
                return 0;
            }

            memcpy(type + GameAddress::kBonusResTextureOffset, &texture, sizeof(texture));
            RefreshSpawnedTexture(type);
            return 1;
        }

        // --- the keys a bonus type carries --------------------------------------------------
        //
        // The same shape a menu element's keys have, over the record bonus_res.txt is read into.
        // The names are the script's own, so what a mod writes here and what an author writes in
        // the file are the same vocabulary.

        using ApiProperty::Field;
        using ApiProperty::Kind;

        constexpr Field kTypeFields[] =
        {
            { "name",               GameAddress::kBonusResNameOffset,           Kind::Text,   0, 0, 0, false },
            { "res_id",             GameAddress::kBonusResIdOffset,             Kind::Text,   0, 0, 0, false },

            { "texture",            GameAddress::kBonusResTextureOffset,        Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true },
            { "model",              GameAddress::kBonusResModelOffset,          Kind::Resource, 0, 0, GameAddress::kResolveModelSlot,   true },
            { "icon",               GameAddress::kBonusResIconOffset,           Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true },
            { "sound_take",         GameAddress::kBonusResSoundTakeOffset,      Kind::Resource, 0, 0, GameAddress::kResolveSoundSlot,   true },
            { "sound_use",          GameAddress::kBonusResSoundUseOffset,       Kind::Resource, 0, 0, GameAddress::kResolveSoundSlot,   true },

            { "glow_color",         GameAddress::kBonusResGlowOffset,           Kind::Floats, 4, 0, 0, true },
            { "glow_color2",        GameAddress::kBonusResGlow2Offset,          Kind::Floats, 4, 0, 0, true },
            { "light_color",        GameAddress::kBonusResLightColorOffset,     Kind::Floats, 3, 0, 0, true },
            { "light_radius",       GameAddress::kBonusResLightRadiusOffset,    Kind::Float,  0, 0, 0, true },
            { "bonus_light_radius", GameAddress::kBonusResBonusLightRadiusOffset, Kind::Float, 0, 0, 0, true },

            { "pts_take",           GameAddress::kBonusResPointsTakeOffset,     Kind::Int,    0, 0, 0, true },
            { "pts_out",            GameAddress::kBonusResPointsOutOffset,      Kind::Int,    0, 0, 0, true },
            { "time",               GameAddress::kBonusResTimeOffset,           Kind::Float,  0, 0, 0, true },
            { "burn_time",          GameAddress::kBonusResBurnTimeOffset,       Kind::Float,  0, 0, 0, true },
            { "train_idx",          GameAddress::kBonusResTrainIndexOffset,     Kind::Int,    0, 0, 0, true },

            { "rocking_amp",        GameAddress::kBonusResRockingAmpOffset,     Kind::Float,  0, 0, 0, true },
            { "rocking_speed",      GameAddress::kBonusResRockingSpeedOffset,   Kind::Float,  0, 0, 0, true },
            { "rocking_axis",       GameAddress::kBonusResRockingAxisOffset,    Kind::Floats, 3, 0, 0, true },

            { "flags",              GameAddress::kBonusResFlagsOffset,          Kind::Int,    0, 0, 0, true },
            { "is_totem",           GameAddress::kBonusResFlagsOffset,          Kind::Flag,   0, GameAddress::kBonusFlagTotem,      0, true },
            { "is_torch",           GameAddress::kBonusResFlagsOffset,          Kind::Flag,   0, GameAddress::kBonusFlagTorch,      0, true },
            { "makes_train",        GameAddress::kBonusResFlagsOffset,          Kind::Flag,   0, GameAddress::kBonusFlagMakesTrain, 0, true },
            { "timer_is_mine",      GameAddress::kBonusResFlagsOffset,          Kind::Flag,   0, GameAddress::kBonusFlagSuppressed, 0, true },
        };

        int __cdecl ApiTypePropertyCount(AmodMod mod)
        {
            return Caller(mod) != nullptr ? static_cast<int>(sizeof(kTypeFields) / sizeof(Field))
                                          : 0;
        }

        const char* __cdecl ApiTypePropertyNameAt(AmodMod mod, int index)
        {
            const int total = ApiTypePropertyCount(mod);
            return (index >= 0 && index < total) ? kTypeFields[index].key : "";
        }

        int __cdecl ApiHasTypeProperty(AmodMod mod, const char* resourceId, const char* key)
        {
            const uint8_t* const type = Caller(mod) != nullptr ? BonusesApi::FindType(resourceId)
                                                               : nullptr;
            return (type != nullptr &&
                    ApiProperty::Find(kTypeFields, sizeof(kTypeFields) / sizeof(Field), key) != nullptr)
                       ? 1
                       : 0;
        }

        int __cdecl ApiGetTypeProperty(AmodMod mod, const char* resourceId, const char* key,
                                       AmodValue* value)
        {
            const uint8_t* const type = Caller(mod) != nullptr ? BonusesApi::FindType(resourceId)
                                                               : nullptr;
            const Field* const field =
                ApiProperty::Find(kTypeFields, sizeof(kTypeFields) / sizeof(Field), key);

            return (type != nullptr && field != nullptr) ? ApiProperty::Get(type, *field, value) : 0;
        }

        int __cdecl ApiSetTypeProperty(AmodMod mod, const char* resourceId, const char* key,
                                       const AmodValue* value)
        {
            uint8_t* const type = OwnedType(Caller(mod), resourceId);
            const Field* const field =
                ApiProperty::Find(kTypeFields, sizeof(kTypeFields) / sizeof(Field), key);

            if (type == nullptr || field == nullptr || value == nullptr ||
                ApiProperty::Set(type, *field, *value) == 0)
            {
                return 0;
            }

            if (field->kind == Kind::Resource &&
                field->offset == GameAddress::kBonusResTextureOffset)
            {
                RefreshSpawnedTexture(type);
            }
            return 1;
        }

        int __cdecl ApiSetBonusIcon(AmodMod mod, const char* resourceId, const char* file)
        {
            uint8_t* const type = file != nullptr ? OwnedType(Caller(mod), resourceId) : nullptr;
            const uint32_t icon = type != nullptr
                                      ? ResolveResource(GameAddress::kResolveTextureSlot, file)
                                      : 0;
            if (icon == 0)
            {
                return 0;
            }

            memcpy(type + GameAddress::kBonusResIconOffset, &icon, sizeof(icon));
            return 1;
        }

        // --- scoring ------------------------------------------------------------------------

        typedef void (__thiscall* ScoreAddRoutine)(void* session, int points, const float* position);

        int __cdecl ApiAwardPoints(AmodMod mod, int points, const float* position);

        typedef const uint32_t* (__thiscall* ChainMessageRoutine)(void* messages, int length);
        typedef void (__cdecl* PlayEffectRoutine)(uint32_t effect, const float* position, int zero,
                                                  float volume, float speed);

        int __cdecl ApiTakeChainLink(AmodMod mod, int points, const float* position)
        {
            uint8_t* const session = Caller(mod) != nullptr ? WorldApi::Session() : nullptr;
            int* const length = static_cast<int*>(GameModule::Resolve(GameAddress::kChainLength));
            int* const total = static_cast<int*>(GameModule::Resolve(GameAddress::kChainPoints));
            float* const timer = static_cast<float*>(GameModule::Resolve(GameAddress::kChainTimer));
            const float* const window =
                static_cast<const float*>(GameModule::Resolve(GameAddress::kChainWindow));

            if (session == nullptr || length == nullptr || total == nullptr || timer == nullptr ||
                window == nullptr)
            {
                return 0;
            }

            int scored = points;
            if (*timer <= 0.0f)
            {
                *total = 0;
                *length = 1;
            }
            else
            {
                scored = *length + points;
                *length += 1;
            }

            *total += scored;
            *timer = *window;

            const ChainMessageRoutine message =
                reinterpret_cast<ChainMessageRoutine>(GameModule::Resolve(GameAddress::kChainMessage));
            void** const effectSlot =
                static_cast<void**>(GameModule::Resolve(GameAddress::kPlayEffectSlot));
            const PlayEffectRoutine play =
                effectSlot != nullptr ? reinterpret_cast<PlayEffectRoutine>(*effectSlot) : nullptr;

            void* const messages = GameModule::Resolve(GameAddress::kChainMessages);

            const uint8_t* const player = WorldApi::Player();
            const uint32_t* const said = (message != nullptr && messages != nullptr)
                                             ? message(messages, *length)
                                             : nullptr;
            const float* const where =
                position != nullptr
                    ? position
                    : (player != nullptr
                           ? reinterpret_cast<const float*>(player + GameAddress::kEntityPositionOffset)
                           : nullptr);

            if (said != nullptr && play != nullptr && where != nullptr)
            {
                float volume;
                memcpy(&volume, said + 1, sizeof(volume));
                play(said[0], where, 0, volume, 1.0f);
            }

            ApiAwardPoints(mod, scored, position);
            return scored;
        }

        int __cdecl ApiAwardPoints(AmodMod mod, int points, const float* position)
        {
            uint8_t* const session = Caller(mod) != nullptr ? WorldApi::Session() : nullptr;
            const ScoreAddRoutine score =
                reinterpret_cast<ScoreAddRoutine>(GameModule::Resolve(GameAddress::kScoreAdd));

            if (session == nullptr || score == nullptr ||
                Read<void*>(session, GameAddress::kSessionPlayerOffset) == nullptr)
            {
                return 0;
            }

            score(session, points, position);
            return 1;
        }

        // --- taking a bonus -----------------------------------------------------------------

        struct TakeHandler
        {
            uint32_t id;
            ModContext* owner;
            AmodBonusTakeHandler handler;
            void* user;
        };

        std::vector<TakeHandler> g_takeHandlers;
        uint32_t g_nextTakeHandlerId = 1;

        typedef void (__cdecl* BonusTouchRoutine)(void* bonus, void* toucher);

        InlineHook g_touchHook;
        BonusTouchRoutine g_originalTouch = nullptr;

        int FloorDiv(float value, float size)
        {
            const float cells = value / size;
            const int whole = static_cast<int>(cells);
            return (cells < 0.0f && static_cast<float>(whole) != cells) ? whole - 1 : whole;
        }

        void MarkTaken(uint8_t* bonus, uint8_t* record)
        {
            bonus[GameAddress::kEntityDeadOffset] = 1;
            record[GameAddress::kBonusSpawnLiveOffset] = 0;

            if (record[GameAddress::kBonusSpawnDecorOffset] != 0)
            {
                return;
            }

            const char* const name = ReadText(bonus, GameAddress::kEntityNameOffset);
            const uintptr_t counter = strcmp(name, "bonus") == 0
                                          ? GameAddress::kBonusesLeft
                                          : (strcmp(name, "mega_bonus") == 0
                                                 ? GameAddress::kMegaBonusesLeft
                                                 : GameAddress::kOtherBonusesLeft);

            int* const left = static_cast<int*>(GameModule::Resolve(counter));
            if (left != nullptr)
            {
                *left -= 1;
            }
        }

        AmodBonusTakeResult AskHandlers(const uint8_t* bonus, const uint8_t* record)
        {
            const uint8_t* const type = Read<const uint8_t*>(record, GameAddress::kBonusSpawnTypeOffset);
            if (type == nullptr || !Readable(type, GameAddress::kBonusResStride))
            {
                return AMOD_BONUS_TAKE_DEFAULT;
            }

            AmodBonusTakeInfo info = {};
            info.structSize = sizeof(info);
            info.name = NameAt(type, GameAddress::kBonusResNameOffset)
                            ? ReadText(type, GameAddress::kBonusResNameOffset)
                            : "";
            info.resourceId = NameAt(type, GameAddress::kBonusResIdOffset)
                                  ? ReadText(type, GameAddress::kBonusResIdOffset)
                                  : "";
            info.tag = NameAt(record, GameAddress::kBonusSpawnTagOffset)
                           ? ReadText(record, GameAddress::kBonusSpawnTagOffset)
                           : "";

            for (int axis = 0; axis < 3; ++axis)
            {
                info.position[axis] = Read<float>(bonus, GameAddress::kEntityPositionOffset +
                                                             static_cast<uintptr_t>(axis) * sizeof(float));
            }

            info.column = FloorDiv(info.position[0] - GameAddress::kCellOriginX,
                                   GameAddress::kCellToWorld);
            info.row = FloorDiv(GameAddress::kCellOriginY - info.position[1],
                                GameAddress::kCellToWorld);

            info.pointsOnTake = Read<int>(type, GameAddress::kBonusResPointsTakeOffset);
            info.isDecor = record[GameAddress::kBonusSpawnDecorOffset] != 0 ? 1 : 0;

            const std::vector<TakeHandler> snapshot = g_takeHandlers;
            for (const TakeHandler& entry : snapshot)
            {
                if (entry.handler == nullptr)
                {
                    continue;
                }

                const AmodBonusTakeResult verdict = entry.handler(&info, entry.user);
                if (verdict != AMOD_BONUS_TAKE_DEFAULT)
                {
                    return verdict;
                }
            }
            return AMOD_BONUS_TAKE_DEFAULT;
        }

        bool IsPlayer(const uint8_t* entity)
        {
            return entity != nullptr && Readable(entity, GameAddress::kEntitySize) &&
                   Read<int>(entity, GameAddress::kEntityKindOffset) == GameAddress::kEntityKindPlayer;
        }

        // Called as (self, other, side); cdecl, so leaving the third off costs nothing.
        void __cdecl TouchDetour(void* bonus, void* toucher)
        {
            uint8_t* const entity = static_cast<uint8_t*>(bonus);
            uint8_t* const record = entity != nullptr && Readable(entity, GameAddress::kEntitySize)
                                        ? Read<uint8_t*>(entity, GameAddress::kEntitySpawnOffset)
                                        : nullptr;

            const bool ours = !g_takeHandlers.empty() &&
                              IsPlayer(static_cast<const uint8_t*>(toucher)) && record != nullptr &&
                              Readable(record, GameAddress::kBonusSpawnSize) &&
                              Read<int>(record, GameAddress::kSpawnKindOffset) == GameAddress::kSpawnKindBonus;

            const AmodBonusTakeResult verdict = ours ? AskHandlers(entity, record)
                                                     : AMOD_BONUS_TAKE_DEFAULT;

            switch (verdict)
            {
            case AMOD_BONUS_TAKE_REFUSE:
                return;

            case AMOD_BONUS_TAKE_HANDLED:
                MarkTaken(entity, record);
                return;

            default:
                g_originalTouch(bonus, toucher);
                return;
            }
        }

        bool InstallTouchHook()
        {
            void* const target = GameSymbols::Address(GameSymbol::BonusOnTouch);
            if (target == nullptr)
            {
                return false;
            }

            std::string mismatch;
            if (!GameModule::VerifyBytes(target, kBonusOnTouchPrologue, sizeof(kBonusOnTouchPrologue),
                                         mismatch))
            {
                FORGE_LOG_ERROR("Bonus_OnTouch does not open as expected (%s), so a mod cannot be "
                                "told about a bonus being taken", mismatch.c_str());
                return false;
            }

            std::string error;
            if (!g_touchHook.Install(target, reinterpret_cast<void*>(&TouchDetour),
                                     kBonusOnTouchPrologue, sizeof(kBonusOnTouchPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Bonus_OnTouch: %s", error.c_str());
                return false;
            }

            g_originalTouch = reinterpret_cast<BonusTouchRoutine>(g_touchHook.Trampoline());
            return true;
        }

        void ReleaseTakeHandler(void* payload)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload));
            for (auto it = g_takeHandlers.begin(); it != g_takeHandlers.end(); ++it)
            {
                if (it->id == id)
                {
                    g_takeHandlers.erase(it);
                    return;
                }
            }
        }

        AmodToken __cdecl ApiAddTakeHandler(AmodMod mod, AmodBonusTakeHandler handler, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || handler == nullptr || g_originalTouch == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }

            TakeHandler entry;
            entry.id = g_nextTakeHandlerId++;
            entry.owner = context;
            entry.handler = handler;
            entry.user = user;
            g_takeHandlers.push_back(entry);

            return context->owned.Add(&ReleaseTakeHandler,
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(entry.id)));
        }

        void __cdecl ApiRemoveTakeHandler(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Caller(mod);
            if (context != nullptr)
            {
                context->owned.Remove(token);
            }
        }
    }

    bool BonusesApi::Install()
    {
        g_bonusTable.structSize = sizeof(AmodBonusesApi);
        g_bonusTable.GetTypeCount = &ApiGetBonusTypeCount;
        g_bonusTable.GetType = &ApiGetBonusType;
        g_bonusTable.FindType = &ApiFindBonusType;
        g_bonusTable.Place = &ApiPlaceBonus;
        g_bonusTable.DefineType = &ApiDefineBonusType;
        g_bonusTable.PlaceEx = &ApiPlaceBonusEx;
        g_bonusTable.AwardPoints = &ApiAwardPoints;
        g_bonusTable.TakeChainLink = &ApiTakeChainLink;
        g_bonusTable.SetTexture = &ApiSetBonusTexture;
        g_bonusTable.SetIcon = &ApiSetBonusIcon;
        g_bonusTable.GetTypeProperty = &ApiGetTypeProperty;
        g_bonusTable.SetTypeProperty = &ApiSetTypeProperty;
        g_bonusTable.HasTypeProperty = &ApiHasTypeProperty;
        g_bonusTable.TypePropertyCount = &ApiTypePropertyCount;
        g_bonusTable.TypePropertyNameAt = &ApiTypePropertyNameAt;

        // Definitions a mod asked for before the game had a table to put them in.
        FrameApi::AddInternalTick(&ApplyPendingTypes);

        if (InstallTouchHook())
        {
            g_bonusTable.AddTakeHandler = &ApiAddTakeHandler;
            g_bonusTable.RemoveTakeHandler = &ApiRemoveTakeHandler;
        }

        return true;
    }

    void BonusesApi::Uninstall()
    {
        g_freeBonusResourcesHook.Uninstall();
        g_touchHook.Uninstall();

        g_originalFreeBonusResources = nullptr;
        g_originalTouch = nullptr;

        g_takeHandlers.clear();
        g_pendingTypes.clear();
        g_definedTypes.clear();
        g_bonusTable = AmodBonusesApi{};
    }

    const AmodBonusesApi* BonusesApi::Table()
    {
        return g_bonusTable.structSize != 0 ? &g_bonusTable : nullptr;
    }

    const uint8_t* BonusesApi::FindType(const char* resourceId)
    {
        const int index = resourceId != nullptr ? BonusRecordIndexOf(resourceId) : -1;
        return index >= 0 ? BonusRecord(index) : nullptr;
    }

    void BonusesApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_takeHandlers.begin(); it != g_takeHandlers.end();)
        {
            it = it->owner == owner ? g_takeHandlers.erase(it) : ++it;
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
