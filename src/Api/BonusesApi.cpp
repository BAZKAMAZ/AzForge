#include "src/Api/BonusesApi.h"
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
#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;

        const uint8_t* BonusRecord(int index)
        {
            const uint8_t* const table = static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kBonusResTable));
            const int* const count = static_cast<const int*>(GameModule::Resolve(GameAddress::kBonusResCount));

            if (table == nullptr || count == nullptr || index < 0)
            {
                return nullptr;
            }

            const uint8_t* const records = *reinterpret_cast<const uint8_t* const*>(table);
            if (records == nullptr || index >= *count)
            {
                return nullptr;
            }

            const uint8_t* const record = records + static_cast<size_t>(index) * GameAddress::kBonusResStride;
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

            void* const model = reinterpret_cast<void*>(Read<uint32_t>(record, GameAddress::kBonusResModelOffset));
            void* const texture = reinterpret_cast<void*>(Read<uint32_t>(record, GameAddress::kBonusResTextureOffset));
            void* const icon = reinterpret_cast<void*>(Read<uint32_t>(record, GameAddress::kBonusResIconOffset));

            out.modelId = model != nullptr ? MmdApi::IdFor(model) : 0;
            out.textureId = texture != nullptr ? TextureApi::IdFor(texture) : 0;
            out.iconId = icon != nullptr ? TextureApi::IdFor(icon) : 0;

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
            const int* const count = static_cast<const int*>(GameModule::Resolve(GameAddress::kBonusResCount));
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

            for (const uintptr_t field : { GameAddress::kBonusResNameOffset, GameAddress::kBonusResIdOffset })
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
        typedef void(__cdecl* AppendRoutine)(void* room, void* record);
        typedef void(__cdecl* SpawnRoutine)(void* room, void* record);

        SpawnRoutine PlacementSpawner()
        {
            void** const slot =
                static_cast<void**>(GameModule::Resolve(GameAddress::kSpawnPlacementSlot));
            return slot != nullptr ? reinterpret_cast<SpawnRoutine>(*slot) : nullptr;
        }

        uint32_t ResolveResource(uintptr_t slot, const char* name);

        int BonusKindByName(const char* name);
        bool PlaceKindRecord(int kind, int roomIndex, const AmodBonusPlacement& where);
        uint8_t* RoomFor(int roomIndex);

        bool PlaceBonus(int roomIndex, const AmodBonusPlacement& where)
        {
            if (where.name == nullptr ||
                where.column < 0 || where.column >= static_cast<int>(GameAddress::kRoomGridColumns) ||
                where.row < 0 || where.row >= static_cast<int>(GameAddress::kRoomGridRows))
            {
                return false;
            }

            uint8_t* const room = RoomFor(roomIndex);

            const CloneRoutine clone = reinterpret_cast<CloneRoutine>(GameModule::Resolve(GameAddress::kMemClone));
            const AppendRoutine append = reinterpret_cast<AppendRoutine>(GameModule::Resolve(GameAddress::kRoomSpawnsAppend));
            const SpawnRoutine spawn = PlacementSpawner();

            if (room == nullptr || clone == nullptr || append == nullptr || spawn == nullptr)
            {
                return false;
            }

            typedef void* (__cdecl* FindTypeRoutine)(const char* name);
            const FindTypeRoutine findType = reinterpret_cast<FindTypeRoutine>(GameModule::Resolve(GameAddress::kBonusResFind));

            void* const typeRecord = findType != nullptr ? findType(where.name) : nullptr;
            if (typeRecord == nullptr)
            {
                const int kind = BonusKindByName(where.name);
                return kind != 0 ? PlaceKindRecord(kind, roomIndex, where) : false;
            }

            uint8_t record[GameAddress::kBonusSpawnSize] = {};
            const int kind = GameAddress::kSpawnKindBonus;
            memcpy(record + GameAddress::kSpawnKindOffset, &kind, sizeof(kind));
            record[GameAddress::kBonusSpawnLiveOffset] = 1;

            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kBonusSpawnNameOffset), GameAddress::kSpawnNameLength, 
                static_cast<const char*>(typeRecord) + GameAddress::kBonusResNameOffset, _TRUNCATE);

            memcpy(record + GameAddress::kBonusSpawnTypeOffset, &typeRecord, sizeof(typeRecord));

            if (where.tag != nullptr)
            {
                strncpy_s(reinterpret_cast<char*>(record + GameAddress::kBonusSpawnTagOffset), GameAddress::kSpawnNameLength, where.tag, _TRUNCATE);
            }
            if (where.time > 0.0f)
            {
                memcpy(record + GameAddress::kBonusSpawnTimeOffset, &where.time, sizeof(float));
            }
            record[GameAddress::kBonusSpawnDecorOffset] = where.decor != 0 ? 1 : 0;

            // $own_model: placement model/texture instead of type's.
            const bool wantsModel = where.model != nullptr && where.model[0] != '\0';
            const bool wantsTexture = where.texture != nullptr && where.texture[0] != '\0';

            if (wantsModel || wantsTexture)
            {
                const uint8_t* const type = static_cast<const uint8_t*>(typeRecord);
                const uint32_t model = ResolveResource(GameAddress::kResolveModelSlot, where.model);
                const uint32_t texture = ResolveResource(GameAddress::kResolveTextureSlot, where.texture);

                if (wantsModel && model == 0)
                {
                    FORGE_LOG_WARN("Bonuses: no model is called '%s', so that placement keeps its type's", where.model);
                }
                if (wantsTexture && texture == 0)
                {
                    FORGE_LOG_WARN("Bonuses: no texture is called '%s', so that placement keeps its type's", where.texture);
                }

                const uint32_t useModel = model != 0 ? model : Read<uint32_t>(type, GameAddress::kBonusResModelOffset);
                const uint32_t useTexture = texture != 0 ? texture : Read<uint32_t>(type, GameAddress::kBonusResTextureOffset);

                record[GameAddress::kBonusSpawnOwnModelOffset] = 1;
                memcpy(record + GameAddress::kBonusSpawnModelOffset, &useModel, sizeof(useModel));
                memcpy(record + GameAddress::kBonusSpawnTextureOffset, &useTexture, sizeof(useTexture));
            }

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

            // Spawn now only in current room; others wait for enter.
            if (room == WorldApi::CurrentRoom())
            {
                spawn(room, placed);
            }
            return true;
        }

        int __cdecl ApiPlaceBonus(AmodMod mod, const char* name, int column, int row)
        {
            AmodBonusPlacement where = {};
            where.structSize = sizeof(where);
            where.name = name;
            where.column = column;
            where.row = row;

            return (Caller(mod) != nullptr && PlaceBonus(-1, where)) ? 1 : 0;
        }

        int __cdecl ApiPlaceBonusIn(AmodMod mod, int roomIndex, const AmodBonusPlacement* placement)
        {
            if (Caller(mod) == nullptr || placement == nullptr ||
                placement->structSize < sizeof(uint32_t))
            {
                return 0;
            }

            AmodBonusPlacement where = {};
            memcpy(&where, placement, placement->structSize < sizeof(where) ? placement->structSize : sizeof(where));

            return PlaceBonus(roomIndex, where) ? 1 : 0;
        }

        int __cdecl ApiPlaceBonusEx(AmodMod mod, const AmodBonusPlacement* placement)
        {
            return ApiPlaceBonusIn(mod, -1, placement);
        }

        // Define new bonus types.

        typedef uint32_t(__cdecl* ResolveResourceRoutine)(const char* name);
        typedef uint8_t* (__stdcall* BonusResAppendRoutine)(uint8_t** table, int* count);

        uint32_t ResolveResource(uintptr_t slot, const char* name)
        {
            void** const at = static_cast<void**>(GameModule::Resolve(slot));
            const ResolveResourceRoutine routine = at != nullptr ? reinterpret_cast<ResolveResourceRoutine>(*at) : nullptr;

            return (routine != nullptr && name != nullptr) ? routine(name) : 0;
        }

        int BonusTypeTotal()
        {
            const int* const count = static_cast<const int*>(GameModule::Resolve(GameAddress::kBonusResCount));
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

        bool WriteResource(uint8_t* record, uintptr_t offset, uintptr_t slot, const char* name, const char* what, const char* id)
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

        // Free rows from unloaded defs; indices stay stable.
        std::vector<int> g_freeRows;

        // Type record this mod defined and still owns, or null.
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

        typedef void(__cdecl* FreeBonusResourcesRoutine)();

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
                uint8_t* const record = index >= 0 ? const_cast<uint8_t*>(BonusRecord(index)) : nullptr;
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
            if (target == nullptr || !GameModule::VerifyBytes(target, kBonusesFreeResourcesPrologue, sizeof(kBonusesFreeResourcesPrologue), mismatch))
            {
                FORGE_LOG_ERROR("Bonuses_FreeResources does not open as expected, so a type cannot "
                    "be added without the game freeing its resources twice");
                return false;
            }

            std::string error;
            if (!g_freeBonusResourcesHook.Install(target, reinterpret_cast<void*>(&FreeBonusResourcesDetour), kBonusesFreeResourcesPrologue, 
                sizeof(kBonusesFreeResourcesPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Bonuses_FreeResources: %s", error.c_str());
                return false;
            }

            g_originalFreeBonusResources = reinterpret_cast<FreeBonusResourcesRoutine>(g_freeBonusResourcesHook.Trampoline());
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

            const bool fromScratch = wanted.basedOn == nullptr || wanted.basedOn[0] == '\0';
            const int base = fromScratch ? -1 : BonusRecordIndexOf(wanted.basedOn);
            const uint8_t* const source = base >= 0 ? BonusRecord(base) : nullptr;

            if (!fromScratch && source == nullptr)
            {
                FORGE_LOG_ERROR("Bonus '%s' is based on '%s', which is not one of the %d types the "
                    "game knows", wanted.resourceId, wanted.basedOn, BonusTypeTotal());
                return false;
            }

            if (fromScratch
                && (wanted.model == nullptr || wanted.model[0] == '\0')
                && (wanted.texture == nullptr || wanted.texture[0] == '\0'))
            {
                FORGE_LOG_ERROR("Bonus '%s' is based on nothing, so it needs a model or a texture "
                    "of its own or it is drawn as nothing", wanted.resourceId);
                return false;
            }

            uint8_t** const table = static_cast<uint8_t**>(GameModule::Resolve(GameAddress::kBonusResTable));
            int* const count = static_cast<int*>(GameModule::Resolve(GameAddress::kBonusResCount));
            const BonusResAppendRoutine appendType = reinterpret_cast<BonusResAppendRoutine>(GameModule::Resolve(GameAddress::kBonusResAppend));

            if (table == nullptr || count == nullptr || appendType == nullptr)
            {
                return false;
            }

            uint8_t record[GameAddress::kBonusResStride] = {};

            if (source != nullptr)
            {
                memcpy(record, source, sizeof(record));
            }
            else
            {
                const uint8_t* const shape = BonusRecord(0);
                if (shape == nullptr)
                {
                    FORGE_LOG_ERROR("Bonus '%s' has no type to take a touch box from", wanted.resourceId);
                    return false;
                }

                memcpy(record + GameAddress::kBonusResBoxOffset, shape + GameAddress::kBonusResBoxOffset, GameAddress::kBonusResSoundTakeOffset - GameAddress::kBonusResBoxOffset);
            }

            const char* const behaviour = wanted.behavesAs != nullptr && wanted.behavesAs[0] != '\0'
                ? wanted.behavesAs
                : wanted.resourceId;

            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kBonusResNameOffset), GameAddress::kSpawnNameLength, behaviour, _TRUNCATE);
            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kBonusResIdOffset), GameAddress::kSpawnNameLength, wanted.resourceId, _TRUNCATE);

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
                memcpy(record + GameAddress::kBonusResPointsTakeOffset, &wanted.pointsOnTake, sizeof(int));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_POINTS_OUT) != 0)
            {
                memcpy(record + GameAddress::kBonusResPointsOutOffset, &wanted.pointsOnRunOut, sizeof(int));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_TIME) != 0)
            {
                memcpy(record + GameAddress::kBonusResTimeOffset, &wanted.time, sizeof(float));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_GLOW) != 0)
            {
                memcpy(record + GameAddress::kBonusResGlowOffset, wanted.glowColor, sizeof(wanted.glowColor));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_GLOW2) != 0)
            {
                memcpy(record + GameAddress::kBonusResGlow2Offset, wanted.glowColor2, sizeof(wanted.glowColor2));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_ROCKING) != 0)
            {
                memcpy(record + GameAddress::kBonusResRockingAmpOffset, &wanted.rockingAmp, sizeof(float));
                memcpy(record + GameAddress::kBonusResRockingSpeedOffset, &wanted.rockingSpeed, sizeof(float));
                memcpy(record + GameAddress::kBonusResRockingAxisOffset, wanted.rockingAxis, sizeof(wanted.rockingAxis));
            }
            if ((wanted.fields & AMOD_BONUS_FIELD_LIGHT) != 0)
            {
                memcpy(record + GameAddress::kBonusResLightColorOffset, wanted.lightColor, sizeof(wanted.lightColor));
                memcpy(record + GameAddress::kBonusResLightRadiusOffset, &wanted.lightRadius, sizeof(float));
            }

            uint32_t flags = Read<uint32_t>(record, GameAddress::kBonusResFlagsOffset);
            if ((wanted.fields & AMOD_BONUS_FIELD_TRAIN) != 0)
            {
                flags = wanted.makesTrain != 0 ? (flags | GameAddress::kBonusFlagMakesTrain)
                    : (flags & ~GameAddress::kBonusFlagMakesTrain);
                memcpy(record + GameAddress::kBonusResTrainIndexOffset, &wanted.trainIndex, sizeof(int));
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

            uint8_t* into = nullptr;
            if (!g_freeRows.empty())
            {
                into = const_cast<uint8_t*>(BonusRecord(g_freeRows.back()));
                g_freeRows.pop_back();
            }

            if (into == nullptr)
            {
                into = appendType(table, count);
            }

            if (into == nullptr)
            {
                FORGE_LOG_ERROR("The bonus table would not grow for '%s'", wanted.resourceId);
                return false;
            }

            memcpy(into, record, sizeof(record));
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

            if (wanted.resourceId == nullptr || wanted.resourceId[0] == '\0')
            {
                FORGE_LOG_ERROR("A bonus type needs an id of its own");
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

            for (uint8_t* entity = WorldApi::FirstLiveEntity(); entity != nullptr && Readable(entity, GameAddress::kEntitySize); 
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

        constexpr Field kPlacementFields[] =
        {
            // Name is copied onto entity and used by level count; read-only here.
            { "name",       GameAddress::kBonusSpawnNameOffset,     Kind::Text,   0,                              0, 0, false },
            { "tag",        GameAddress::kBonusSpawnTagOffset,      Kind::Text,   GameAddress::kSpawnNameLength,  0, 0, true },

            { "pos",        GameAddress::kBonusSpawnPositionOffset, Kind::Floats, 3, 0, 0, true },
            { "phase",      GameAddress::kBonusSpawnPhaseOffset,    Kind::Float,  0, 0, 0, true },

            // Overrides type $time for this placement (like a room file).
            { "time",       GameAddress::kBonusSpawnTimeOffset,     Kind::Float,  0, 0, 0, true },

            // Decor: does not count toward level wait.
            { "decor",      GameAddress::kBonusSpawnDecorOffset,    Kind::Byte,   0, 0, 0, true },

            // Clear live byte = spent, same as take.
            { "is_live",    GameAddress::kBonusSpawnLiveOffset,     Kind::Byte,   0, 0, 0, true },

            // Placement $own_model look.
            { "own_look",   GameAddress::kBonusSpawnOwnModelOffset, Kind::Byte,   0, 0, 0, true },
            { "model",      GameAddress::kBonusSpawnModelOffset,    Kind::Resource, 0, 0, GameAddress::kResolveModelSlot,   true },
            { "texture",    GameAddress::kBonusSpawnTextureOffset,  Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true },
        };

        int __cdecl ApiPlacementPropertyCount(AmodMod mod)
        {
            return Caller(mod) != nullptr
                ? static_cast<int>(sizeof(kPlacementFields) / sizeof(Field)) : 0;
        }

        const char* __cdecl ApiPlacementPropertyNameAt(AmodMod mod, int index)
        {
            const int total = ApiPlacementPropertyCount(mod);
            return (index >= 0 && index < total) ? kPlacementFields[index].key : nullptr;
        }

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

        int __cdecl ApiGetTypeProperty(AmodMod mod, const char* resourceId, const char* key, AmodValue* value)
        {
            const uint8_t* const type = Caller(mod) != nullptr ? BonusesApi::FindType(resourceId)
                : nullptr;
            const Field* const field =
                ApiProperty::Find(kTypeFields, sizeof(kTypeFields) / sizeof(Field), key);

            return (type != nullptr && field != nullptr) ? ApiProperty::Get(type, *field, value) : 0;
        }

        int __cdecl ApiSetTypeProperty(AmodMod mod, const char* resourceId, const char* key, const AmodValue* value)
        {
            uint8_t* const type = OwnedType(Caller(mod), resourceId);
            const Field* const field =
                ApiProperty::Find(kTypeFields, sizeof(kTypeFields) / sizeof(Field), key);

            if (type == nullptr || field == nullptr || value == nullptr || ApiProperty::Set(type, *field, *value) == 0)
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

        // Scoring.

        typedef void(__thiscall* ScoreAddRoutine)(void* session, int points, const float* position);

        int __cdecl ApiAwardPoints(AmodMod mod, int points, const float* position);

        typedef const uint32_t* (__thiscall* ChainMessageRoutine)(void* messages, int length);
        typedef void(__cdecl* PlayEffectRoutine)(uint32_t effect, const float* position, int zero,
            float volume, float speed);

        int __cdecl ApiTakeChainLink(AmodMod mod, int points, const float* position)
        {
            uint8_t* const session = Caller(mod) != nullptr ? WorldApi::Session() : nullptr;
            int* const length = static_cast<int*>(GameModule::Resolve(GameAddress::kChainLength));
            int* const total = static_cast<int*>(GameModule::Resolve(GameAddress::kChainPoints));
            float* const timer = static_cast<float*>(GameModule::Resolve(GameAddress::kChainTimer));
            const float* const window = static_cast<const float*>(GameModule::Resolve(GameAddress::kChainWindow));

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

            const ChainMessageRoutine message = reinterpret_cast<ChainMessageRoutine>(GameModule::Resolve(GameAddress::kChainMessage));
            void** const effectSlot = static_cast<void**>(GameModule::Resolve(GameAddress::kPlayEffectSlot));
            const PlayEffectRoutine play = effectSlot != nullptr ? reinterpret_cast<PlayEffectRoutine>(*effectSlot) : nullptr;

            void* const messages = GameModule::Resolve(GameAddress::kChainMessages);

            const uint8_t* const player = WorldApi::Player();
            const uint32_t* const said = (message != nullptr && messages != nullptr)
                ? message(messages, *length)
                : nullptr;
            const float* const where = position != nullptr ? position : (player != nullptr
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

        // Taking a bonus.

        struct TakeHandler
        {
            uint32_t id;
            ModContext* owner;
            AmodBonusTakeHandler handler;
            void* user;
        };

        std::vector<TakeHandler> g_takeHandlers;
        uint32_t g_nextTakeHandlerId = 1;

        typedef void(__cdecl* BonusTouchRoutine)(void* bonus, void* toucher);

        InlineHook g_touchHook;
        BonusTouchRoutine g_originalTouch = nullptr;

        int FloorDiv(float value, float size)
        {
            const float cells = value / size;
            const int whole = static_cast<int>(cells);
            return (cells < 0.0f && static_cast<float>(whole) != cells) ? whole - 1 : whole;
        }

        void SpendRecord(uint8_t* record)
        {
            record[GameAddress::kBonusSpawnLiveOffset] = 0;

            if (record[GameAddress::kBonusSpawnDecorOffset] != 0)
            {
                return;
            }

            const char* const name = ReadText(record, GameAddress::kBonusSpawnNameOffset);
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

        void MarkTaken(uint8_t* bonus, uint8_t* record)
        {
            bonus[GameAddress::kEntityDeadOffset] = 1;
            SpendRecord(record);
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

            info.column = FloorDiv(info.position[0] - GameAddress::kCellOriginX, GameAddress::kCellToWorld);
            info.row = FloorDiv(GameAddress::kCellOriginY - info.position[1], GameAddress::kCellToWorld);

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

        // Touch: __cdecl (self, other, side); third arg optional.
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

        bool IsBonusKind(int kind);
        const char* BonusKindName(int kind);
        int BonusKindByName(const char* name);
        int KindByStandIn(const void* type);
        void BuildBonusKind(void* room, void* record, int kind, void* user);

        uint8_t* RoomFor(int roomIndex)
        {
            return roomIndex < 0 ? WorldApi::CurrentRoom() : WorldApi::RoomAt(roomIndex);
        }

        uint8_t* PlacementIn(int roomIndex, int index)
        {
            uint8_t* const room = RoomFor(roomIndex);
            if (room == nullptr || index < 0)
            {
                return nullptr;
            }

            const int count = Read<int>(room, GameAddress::kRoomSpawnCountOffset);
            if (index >= count)
            {
                return nullptr;
            }

            uint8_t* const* const records =
                reinterpret_cast<uint8_t* const*>(Read<uintptr_t>(room, GameAddress::kRoomSpawnListOffset));

            if (records == nullptr || !Readable(records, sizeof(void*) * static_cast<size_t>(index + 1)))
            {
                return nullptr;
            }

            uint8_t* const record = records[index];
            if (record == nullptr || !Readable(record, GameAddress::kBonusSpawnSize))
            {
                return nullptr;
            }

            const int head = Read<int>(record, GameAddress::kSpawnKindOffset);
            return (head == GameAddress::kPlacementKindBonus || IsBonusKind(head)) ? record : nullptr;
        }

        uint8_t* PlacementOf(int index)
        {
            return PlacementIn(-1, index);
        }

        int PlacementTotal(int roomIndex)
        {
            uint8_t* const room = RoomFor(roomIndex);
            return room != nullptr ? Read<int>(room, GameAddress::kRoomSpawnCountOffset) : 0;
        }

        uint8_t* EntityFor(const uint8_t* record)
        {
            for (uint8_t* entity = WorldApi::FirstLiveEntity(); entity != nullptr;
                entity = WorldApi::NextLiveEntity(entity))
            {
                if (!Readable(entity, GameAddress::kEntitySize))
                {
                    break;
                }
                if (Read<const uint8_t*>(entity, GameAddress::kEntitySpawnOffset) == record)
                {
                    return entity;
                }
            }
            return nullptr;
        }

        int __cdecl ApiGetPlacementIn(AmodMod mod, int roomIndex, int index, AmodBonusSpawnInfo* out)
        {
            if (Caller(mod) == nullptr || out == nullptr || out->structSize < sizeof(uint32_t))
            {
                return 0;
            }

            const uint8_t* const record = PlacementIn(roomIndex, index);
            if (record == nullptr)
            {
                return 0;
            }

            const int head = Read<int>(record, GameAddress::kSpawnKindOffset);
            const bool isKind = head != GameAddress::kPlacementKindBonus;

            const uint8_t* const type = Read<const uint8_t*>(record, GameAddress::kBonusSpawnTypeOffset);
            const bool hasType = type != nullptr && Readable(type, GameAddress::kBonusResStride);

            AmodBonusSpawnInfo filled = {};
            filled.structSize = sizeof(filled);
            filled.name = ReadText(record, GameAddress::kBonusSpawnNameOffset);
            filled.tag = ReadText(record, GameAddress::kBonusSpawnTagOffset);
            filled.kind = isKind ? head : 0;

            filled.resourceId = isKind ? BonusKindName(head) : (hasType ? ReadText(type, GameAddress::kBonusResIdOffset) : "");
            filled.hasType = (isKind || hasType) ? 1 : 0;
            filled.isLive = record[GameAddress::kBonusSpawnLiveOffset] != 0 ? 1 : 0;
            memcpy(filled.position, record + GameAddress::kBonusSpawnPositionOffset, sizeof(filled.position));

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiGetPlacement(AmodMod mod, int index, AmodBonusSpawnInfo* out)
        {
            return ApiGetPlacementIn(mod, -1, index, out);
        }

        int __cdecl ApiPlacementCount(AmodMod mod, int roomIndex)
        {
            return Caller(mod) != nullptr ? PlacementTotal(roomIndex) : 0;
        }

        int __cdecl ApiRemovePlacementIn(AmodMod mod, int roomIndex, int index)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            uint8_t* const record = PlacementIn(roomIndex, index);
            if (record == nullptr || record[GameAddress::kBonusSpawnLiveOffset] == 0)
            {
                return 0;
            }

            uint8_t* const entity = EntityFor(record);
            if (entity != nullptr)
            {
                entity[GameAddress::kEntityDeadOffset] = 1;
            }

            // Same bookkeeping as take so level count stays honest.
            SpendRecord(record);
            return 1;
        }

        int __cdecl ApiRemovePlacement(AmodMod mod, int index)
        {
            return ApiRemovePlacementIn(mod, -1, index);
        }

        int __cdecl ApiGetPlacementProperty(AmodMod mod, int roomIndex, int index, const char* key, AmodValue* value)
        {
            const uint8_t* const record = Caller(mod) != nullptr ? PlacementIn(roomIndex, index)
                : nullptr;
            const Field* const field =
                ApiProperty::Find(kPlacementFields, sizeof(kPlacementFields) / sizeof(Field), key);

            return (record != nullptr && field != nullptr) ? ApiProperty::Get(record, *field, value) : 0;
        }

        int __cdecl ApiSetPlacementProperty(AmodMod mod, int roomIndex, int index, const char* key, const AmodValue* value)
        {
            uint8_t* const record = Caller(mod) != nullptr ? PlacementIn(roomIndex, index) : nullptr;
            const Field* const field = ApiProperty::Find(kPlacementFields, sizeof(kPlacementFields) / sizeof(Field), key);

            if (record == nullptr || field == nullptr || value == nullptr)
            {
                return 0;
            }

            return ApiProperty::Set(record, *field, *value);
        }

        // Placement becoming a live bonus.

        struct SpawnHandler
        {
            uint32_t id;
            ModContext* owner;
            AmodBonusSpawnHandler handler;
            void* user;
        };

        std::vector<SpawnHandler> g_spawnHandlers;
        uint32_t g_nextSpawnHandlerId = 1;

        typedef void(__cdecl* BonusSpawnRoutine)(void* record);

        InlineHook g_spawnHook;
        BonusSpawnRoutine g_originalSpawn = nullptr;

        void ComplainAboutName(const char* name)
        {
            static std::vector<std::string> said;

            for (const std::string& seen : said)
            {
                if (seen == name)
                {
                    return;
                }
            }

            said.push_back(name);
            FORGE_LOG_WARN("Bonuses: nothing in the bonus table is named '%s', so every placement asking for it is skipped", name);
        }

        void __cdecl SpawnDetour(void* record)
        {
            uint8_t* const at = static_cast<uint8_t*>(record);
            const bool isBonus = at != nullptr && Readable(at, GameAddress::kBonusSpawnSize) && Read<int>(at, 0) == GameAddress::kPlacementKindBonus;

            if (isBonus)
            {
                const uint8_t* const type = Read<const uint8_t*>(at, GameAddress::kBonusSpawnTypeOffset);
                const bool hasType = type != nullptr && Readable(type, GameAddress::kBonusResStride);

                AmodBonusSpawnInfo info = {};
                info.structSize = sizeof(info);
                info.name = ReadText(at, GameAddress::kBonusSpawnNameOffset);
                info.resourceId = hasType ? ReadText(type, GameAddress::kBonusResIdOffset) : "";
                info.tag = ReadText(at, GameAddress::kBonusSpawnTagOffset);
                info.hasType = hasType ? 1 : 0;
                info.isLive = at[GameAddress::kBonusSpawnLiveOffset] != 0 ? 1 : 0;
                memcpy(info.position, at + GameAddress::kBonusSpawnPositionOffset, sizeof(info.position));

                const std::vector<SpawnHandler> snapshot = g_spawnHandlers;
                for (const SpawnHandler& entry : snapshot)
                {
                    if (entry.handler != nullptr && entry.handler(&info, entry.user) == 0)
                    {
                        return;
                    }
                }

                // Parser stand-in or mod-placed kind name.
                int adopted = info.isLive != 0 ? KindByStandIn(type) : 0;
                if (adopted == 0 && !hasType && info.isLive != 0)
                {
                    adopted = BonusKindByName(info.name);
                }
                if (adopted != 0)
                {
                    memcpy(at + GameAddress::kSpawnKindOffset, &adopted, sizeof(adopted));
                    BuildBonusKind(nullptr, at, adopted, nullptr);
                    return;
                }

                if (!hasType && info.isLive != 0)
                {
                    ComplainAboutName(info.name);
                }
            }

            if (g_originalSpawn != nullptr)
            {
                g_originalSpawn(record);
            }
        }

        // Spawn hook always installed; silent drops still reported.
        bool InstallSpawnHook()
        {
            if (g_spawnHook.IsInstalled())
            {
                return true;
            }

            void* const target = GameModule::Resolve(GameAddress::kBonusSpawn);
            if (target == nullptr)
            {
                return false;
            }

            std::string error;
            if (!g_spawnHook.Install(target, reinterpret_cast<void*>(&SpawnDetour), kBonusSpawnPrologue, sizeof(kBonusSpawnPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Bonus_Spawn: %s", error.c_str());
                return false;
            }

            g_originalSpawn = reinterpret_cast<BonusSpawnRoutine>(g_spawnHook.Trampoline());
            return true;
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

        AmodToken __cdecl ApiAddSpawnHandler(AmodMod mod, AmodBonusSpawnHandler handler, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || handler == nullptr || !InstallSpawnHook())
            {
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

        struct RegisteredKind
        {
            ModContext* owner;
            int kind;
            std::string name;
            AmodBonusKindHandler handler;
            void* user;

            uint8_t* standIn;
        };

        // Stand-in records outlive the kinds that asked for them.
        std::vector<std::vector<uint8_t>> g_standIns;

        std::vector<RegisteredKind> g_kinds;

        struct Resolved
        {
            uint32_t id;
            void* pointer;
        };

        constexpr int kResolvedKept = 16;

        Resolved g_resolvedModels[kResolvedKept] = {};
        Resolved g_resolvedTextures[kResolvedKept] = {};
        int g_resolvedModelNext = 0;
        int g_resolvedTextureNext = 0;
        uint32_t g_resolvedGeneration = 0;

        void CheckResolvedLevel()
        {
            const uint32_t generation = WorldApi::LevelGeneration();
            if (generation == g_resolvedGeneration)
            {
                return;
            }

            g_resolvedGeneration = generation;
            memset(g_resolvedModels, 0, sizeof(g_resolvedModels));
            memset(g_resolvedTextures, 0, sizeof(g_resolvedTextures));
            g_resolvedModelNext = 0;
            g_resolvedTextureNext = 0;
        }

        void* ResolveModelCached(AmodModelId id)
        {
            CheckResolvedLevel();

            for (const Resolved& known : g_resolvedModels)
            {
                if (known.id == id && id != 0)
                {
                    return known.pointer;
                }
            }

            void* const model = MmdApi::ModelFor(id);
            g_resolvedModels[g_resolvedModelNext] = { id, model };
            g_resolvedModelNext = (g_resolvedModelNext + 1) % kResolvedKept;
            return model;
        }

        void* ResolveTextureCached(AmodTextureId id)
        {
            CheckResolvedLevel();

            for (const Resolved& known : g_resolvedTextures)
            {
                if (known.id == id && id != 0)
                {
                    return known.pointer;
                }
            }

            void* const texture = TextureApi::TextureFor(id);

            g_resolvedTextures[g_resolvedTextureNext] = { id, texture };
            g_resolvedTextureNext = (g_resolvedTextureNext + 1) % kResolvedKept;
            return texture;
        }

        typedef void(__cdecl* KindThinkRoutine)(void* entity);
        typedef void(__cdecl* KindTouchRoutine)(void* entity, void* other, int side);

        // Standing state the record does not hold.
        struct Standing
        {
            int kind;
            const void* record;
            float rockingAmp;
            float rockingSpeed;
            int pointsOnTake;
            uint32_t generation;

            AmodModelId modelId;
            AmodTextureId textureId;

            // true when level-placed bonus was borrowed by a mod.
            bool borrowed;
            KindThinkRoutine gameThink;
            KindTouchRoutine gameTouch;
        };

        std::vector<Standing> g_standing;

        const RegisteredKind* KindOf(int kind)
        {
            for (const RegisteredKind& known : g_kinds)
            {
                if (known.kind == kind)
                {
                    return &known;
                }
            }
            return nullptr;
        }

        bool IsBonusKind(int kind)
        {
            return KindOf(kind) != nullptr;
        }

        const char* BonusKindName(int kind)
        {
            const RegisteredKind* const known = KindOf(kind);
            return known != nullptr ? known->name.c_str() : "";
        }

        int BonusKindByName(const char* name)
        {
            for (const RegisteredKind& known : g_kinds)
            {
                if (name != nullptr && known.name == name)
                {
                    return known.kind;
                }
            }
            return 0;
        }

        // Kind from the parser stand-in on the record.
        int KindByStandIn(const void* type)
        {
            for (const RegisteredKind& known : g_kinds)
            {
                if (type != nullptr && known.standIn == type)
                {
                    return known.kind;
                }
            }
            return 0;
        }

        typedef uint8_t* (__cdecl* BonusResFindRoutine)(const char* resourceId);

        InlineHook g_findHook;
        BonusResFindRoutine g_originalFind = nullptr;

        uint8_t* __cdecl FindDetour(const char* resourceId)
        {
            for (RegisteredKind& known : g_kinds)
            {
                if (resourceId != nullptr && known.name == resourceId && known.standIn != nullptr)
                {
                    return known.standIn;
                }
            }

            return g_originalFind != nullptr ? g_originalFind(resourceId) : nullptr;
        }

        bool InstallFindHook()
        {
            if (g_findHook.IsInstalled())
            {
                return true;
            }

            void* const target = GameModule::Resolve(GameAddress::kBonusResFind);
            std::string mismatch;

            if (target == nullptr || !GameModule::VerifyBytes(target, kBonusResFindPrologue, sizeof(kBonusResFindPrologue), mismatch))
            {
                FORGE_LOG_ERROR("BonusRes_FindByResId does not open as expected, so a room file "
                    "cannot name a kind of a mod's own");
                return false;
            }

            std::string error;
            if (!g_findHook.Install(target, reinterpret_cast<void*>(&FindDetour),
                kBonusResFindPrologue, sizeof(kBonusResFindPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook BonusRes_FindByResId: %s", error.c_str());
                return false;
            }

            g_originalFind = reinterpret_cast<BonusResFindRoutine>(g_findHook.Trampoline());
            return true;
        }

        Standing* StandingOn(const void* record)
        {
            for (Standing& one : g_standing)
            {
                if (one.record == record)
                {
                    return &one;
                }
            }
            return nullptr;
        }

        // Placement record an entity of ours came from.
        uint8_t* KindRecordOf(void* entity)
        {
            uint8_t* const self = static_cast<uint8_t*>(entity);
            if (self == nullptr || !Readable(self, GameAddress::kEntitySize))
            {
                return nullptr;
            }

            uint8_t* const record = Read<uint8_t*>(self, GameAddress::kEntitySpawnOffset);
            return (record != nullptr && Readable(record, GameAddress::kBonusSpawnSize) &&
                StandingOn(record) != nullptr)
                ? record
                : nullptr;
        }

        void ReadBody(const uint8_t* entity, AmodBonusBody& body)
        {
            memcpy(body.position, entity + GameAddress::kEntityPositionOffset, sizeof(body.position));
            memcpy(body.glowColor, entity + GameAddress::kEntityGlowOffset, sizeof(body.glowColor));
            memcpy(body.glowColor2, entity + GameAddress::kEntityGlow2Offset, sizeof(body.glowColor2));

            const uint32_t flags = Read<uint32_t>(entity, GameAddress::kEntityFlagsOffset);
            body.isHidden = (flags & GameAddress::kEntityHiddenFlag) != 0 ? 1 : 0;
        }

        // Fields UPDATE may change.
        void WriteBody(uint8_t* entity, const AmodBonusBody& body)
        {

            for (int axis = 0; axis < 3; ++axis)
            {
                const uintptr_t at = static_cast<uintptr_t>(axis) * sizeof(float);
                memcpy(entity + GameAddress::kEntityPositionOffset + at, &body.position[axis], sizeof(float));
            }

            memcpy(entity + GameAddress::kEntityGlowOffset, body.glowColor, sizeof(body.glowColor));
            memcpy(entity + GameAddress::kEntityGlow2Offset, body.glowColor2, sizeof(body.glowColor2));

            uint32_t flags = Read<uint32_t>(entity, GameAddress::kEntityFlagsOffset);
            flags = body.isHidden != 0 ? (flags | GameAddress::kEntityHiddenFlag) : (flags & ~GameAddress::kEntityHiddenFlag);

            memcpy(entity + GameAddress::kEntityFlagsOffset, &flags, sizeof(flags));
        }

        typedef void(__cdecl* KindThinkRoutine)(void* entity);
        typedef void(__cdecl* KindTouchRoutine)(void* entity, void* other, int side);

        void __cdecl KindThinkThunk(void* entity)
        {
            uint8_t* const self = static_cast<uint8_t*>(entity);
            uint8_t* const record = KindRecordOf(entity);
            Standing* const standing = record != nullptr ? StandingOn(record) : nullptr;

            if (standing == nullptr)
            {
                return;
            }

            const bool borrowed = standing->borrowed;
            const KindThinkRoutine gameThink = standing->gameThink;

            if (!borrowed && standing->rockingAmp != 0.0f)
            {
                const float delta = Read<float>(self, GameAddress::kEntityDeltaOffset);
                float phase = Read<float>(record, GameAddress::kBonusSpawnPhaseOffset) + standing->rockingSpeed * delta;

                phase = phase >= 6.2831855f ? phase - 6.2831855f : (phase < 0.0f ? phase + 6.2831855f : phase);
                memcpy(record + GameAddress::kBonusSpawnPhaseOffset, &phase, sizeof(phase));

                const float swing = sinf(phase) * standing->rockingAmp;
                memcpy(self + GameAddress::kEntityRockingOffset, &swing, sizeof(swing));
            }

            const RegisteredKind* const mine = KindOf(standing->kind);
            if (mine == nullptr || mine->handler == nullptr)
            {
                // No handler left; borrowed keeps game tick.
                if (borrowed && gameThink != nullptr)
                {
                    gameThink(entity);
                }
                return;
            }

            AmodBonusBody body = {};
            body.structSize = sizeof(body);
            body.name = ReadText(self, GameAddress::kEntityNameOffset);
            body.tag = ReadText(record, GameAddress::kBonusSpawnTagOffset);
            body.rockingAmp = standing->rockingAmp;
            body.rockingSpeed = standing->rockingSpeed;
            body.pointsOnTake = standing->pointsOnTake;
            body.modelId = standing->modelId;
            body.textureId = standing->textureId;
            ReadBody(self, body);

            AmodBonusKindInfo asking = {};
            asking.structSize = sizeof(asking);
            asking.event = AMOD_BONUS_EVENT_UPDATE;
            asking.kind = standing->kind;
            asking.record = record;
            asking.body = &body;
            asking.deltaTime = Read<float>(self, GameAddress::kEntityDeltaOffset);

            const int verdict = mine->handler(&asking, mine->user);

            // Borrowed: handler return decides if game tick still runs.
            if (borrowed && verdict == 0 && gameThink != nullptr)
            {
                gameThink(entity);
            }

            Standing* const still = StandingOn(record);
            if (still != nullptr)
            {
                still->rockingAmp = body.rockingAmp;
                still->rockingSpeed = body.rockingSpeed;
                still->pointsOnTake = body.pointsOnTake;

                // Apply model/texture only when handler swapped one.
                if (body.modelId != still->modelId)
                {
                    void* const model = ResolveModelCached(body.modelId);
                    memcpy(self + GameAddress::kEntityModelOffset, &model, sizeof(model));
                    still->modelId = body.modelId;
                }
                if (body.textureId != still->textureId)
                {
                    void* const texture = ResolveTextureCached(body.textureId);
                    memcpy(self + GameAddress::kEntityTextureOffset, &texture, sizeof(texture));
                    still->textureId = body.textureId;
                }
            }

            WriteBody(self, body);
        }

        void __cdecl KindTouchThunk(void* entity, void* other, int side)
        {
            uint8_t* const self = static_cast<uint8_t*>(entity);
            uint8_t* const record = KindRecordOf(entity);
            Standing* const standing = record != nullptr ? StandingOn(record) : nullptr;

            if (standing == nullptr || !IsPlayer(static_cast<const uint8_t*>(other)) ||
                record[GameAddress::kBonusSpawnLiveOffset] == 0)
            {
                return;
            }

            const RegisteredKind* const mine = KindOf(standing->kind);
            const int points = standing->pointsOnTake;
            const bool borrowed = standing->borrowed;
            const KindTouchRoutine gameTouch = standing->gameTouch;

            AmodBonusTakeInfo taking = {};
            taking.structSize = sizeof(taking);
            taking.name = ReadText(self, GameAddress::kEntityNameOffset);
            taking.resourceId = mine != nullptr ? mine->name.c_str() : "";
            taking.tag = ReadText(record, GameAddress::kBonusSpawnTagOffset);
            taking.pointsOnTake = points;
            taking.isDecor = record[GameAddress::kBonusSpawnDecorOffset] != 0 ? 1 : 0;
            memcpy(taking.position, self + GameAddress::kEntityPositionOffset, sizeof(taking.position));

            taking.column = FloorDiv(taking.position[0] - GameAddress::kCellOriginX, GameAddress::kCellToWorld);
            taking.row = FloorDiv(GameAddress::kCellOriginY - taking.position[1], GameAddress::kCellToWorld);

            AmodBonusKindInfo asking = {};
            asking.structSize = sizeof(asking);
            asking.event = AMOD_BONUS_EVENT_TAKE;
            asking.kind = standing->kind;
            asking.record = record;
            asking.take = &taking;

            const int verdict = (mine != nullptr && mine->handler != nullptr)
                ? mine->handler(&asking, mine->user)
                : AMOD_BONUS_TAKE_DEFAULT;

            if (verdict == AMOD_BONUS_TAKE_REFUSE)
            {
                return;
            }

            if (borrowed)
            {
                if (verdict == AMOD_BONUS_TAKE_DEFAULT && gameTouch != nullptr)
                {
                    gameTouch(entity, other, side);
                }
                else if (verdict == AMOD_BONUS_TAKE_HANDLED)
                {
                    MarkTaken(self, record);
                }
                return;
            }

            uint8_t* const session = WorldApi::Session();
            const ScoreAddRoutine score =
                reinterpret_cast<ScoreAddRoutine>(GameModule::Resolve(GameAddress::kScoreAdd));

            if (verdict == AMOD_BONUS_TAKE_DEFAULT && points != 0 && session != nullptr && score != nullptr)
            {
                score(session, points, taking.position);
            }

            MarkTaken(self, record);
        }

        void WriteGlowDefaults(uint8_t* entity)
        {
            const uintptr_t sources[] = { GameAddress::kBonusGlowState0, GameAddress::kBonusGlowState1, GameAddress::kBonusGlowState2 };

            for (int at = 0; at < 3; ++at)
            {
                const void* const from = GameModule::Resolve(sources[at]);
                if (from != nullptr)
                {
                    memcpy(entity + GameAddress::kEntityGlowStateOffset + static_cast<uintptr_t>(at) * sizeof(uint32_t), from, sizeof(uint32_t));
                }
            }

            const void* const sprite = GameModule::Resolve(GameAddress::kBonusGlowSprite);
            if (sprite != nullptr)
            {
                memcpy(entity + GameAddress::kEntityGlowSpriteOffset, sprite, sizeof(uint32_t));
            }
        }

        uint8_t* BuildKindEntity(const AmodBonusBody& body, uint8_t* record)
        {
            typedef uint8_t* (__cdecl* EntityAllocRoutine)();
            const EntityAllocRoutine alloc = reinterpret_cast<EntityAllocRoutine>(GameModule::Resolve(GameAddress::kEntitiesAlloc));

            uint8_t* const entity = alloc != nullptr ? alloc() : nullptr;
            if (entity == nullptr)
            {
                return nullptr;
            }

            const int kind = GameAddress::kEntityKindBonus;
            memcpy(entity + GameAddress::kEntityKindOffset, &kind, sizeof(kind));

            strncpy_s(reinterpret_cast<char*>(entity + GameAddress::kEntityNameOffset), GameAddress::kSpawnNameLength, body.name != nullptr ? body.name : "", _TRUNCATE);
            strncpy_s(reinterpret_cast<char*>(entity + GameAddress::kEntityTagOffset), GameAddress::kEntityTagSize, body.tag != nullptr ? body.tag : "", _TRUNCATE);

            for (int axis = 0; axis < 3; ++axis)
            {
                const uintptr_t at = static_cast<uintptr_t>(axis) * sizeof(float);
                memcpy(entity + GameAddress::kEntityPositionOffset + at, &body.position[axis], sizeof(float));
                memcpy(entity + GameAddress::kEntityPreviousPositionOffset + at, &body.position[axis], sizeof(float));
            }

            // Touch box.
            uint8_t* const box = entity + GameAddress::kEntityFrameBoxOffset;
            const int reports = 1;
            memcpy(box + GameAddress::kRoomBoxKindOffset, &reports, sizeof(reports));
            memcpy(box + GameAddress::kBoxMinXOffset, body.box.min, sizeof(body.box.min));
            memcpy(box + GameAddress::kBoxMaxXOffset, body.box.max, sizeof(body.box.max));

            void* const model = ResolveModelCached(body.modelId);
            void* const texture = ResolveTextureCached(body.textureId);
            memcpy(entity + GameAddress::kEntityModelOffset, &model, sizeof(model));
            memcpy(entity + GameAddress::kEntityTextureOffset, &texture, sizeof(texture));

            uint32_t flags = model != 0 ? 0x204u : GameAddress::kEntityHiddenFlag;
            if (body.isHidden != 0)
            {
                flags |= GameAddress::kEntityHiddenFlag;
            }

            if (body.lightRadius > 0.0f)
            {
                flags |= GameAddress::kEntityLightFlag;
                memcpy(entity + GameAddress::kEntityLightColourOffset, body.lightColor, sizeof(body.lightColor));
                memcpy(entity + GameAddress::kEntityLightRangeOffset, &body.lightRadius, sizeof(float));
            }
            memcpy(entity + GameAddress::kEntityFlagsOffset, &flags, sizeof(flags));

            WriteGlowDefaults(entity);
            memcpy(entity + GameAddress::kEntityGlowOffset, body.glowColor, sizeof(body.glowColor));
            memcpy(entity + GameAddress::kEntityGlow2Offset, body.glowColor2, sizeof(body.glowColor2));
            memcpy(entity + GameAddress::kEntityRockingAxisOffset, body.rockingAxis, sizeof(body.rockingAxis));

            const uint8_t touchable = 1;
            memcpy(entity + GameAddress::kEntityTouchableOffset, &touchable, sizeof(touchable));
            memcpy(entity + GameAddress::kEntitySpawnOffset, &record, sizeof(record));

            const KindThinkRoutine think = &KindThinkThunk;
            const KindTouchRoutine touch = &KindTouchThunk;
            memcpy(entity + GameAddress::kEntityThinkOffset, &think, sizeof(think));
            memcpy(entity + GameAddress::kEntityTouchHandlerOffset, &touch, sizeof(touch));
            return entity;
        }

        // Placement build callback for a registered kind.
        void BuildBonusKind(void* room, void* record, int kind, void*)
        {
            (void)room;

            const RegisteredKind* const mine = KindOf(kind);
            uint8_t* const at = static_cast<uint8_t*>(record);

            if (mine == nullptr || mine->handler == nullptr || at == nullptr ||
                !Readable(at, GameAddress::kBonusSpawnSize) ||
                at[GameAddress::kBonusSpawnLiveOffset] == 0)
            {
                return;
            }

            AmodBonusBody body = {};
            body.structSize = sizeof(body);
            body.name = ReadText(at, GameAddress::kBonusSpawnNameOffset);
            body.tag = ReadText(at, GameAddress::kBonusSpawnTagOffset);
            body.box.structSize = sizeof(body.box);
            memcpy(body.position, at + GameAddress::kBonusSpawnPositionOffset, sizeof(body.position));

            const uint8_t* const shape = BonusRecord(0);
            if (shape != nullptr)
            {
                memcpy(body.box.min, shape + GameAddress::kBonusResBoxOffset + GameAddress::kBoxMinXOffset, sizeof(body.box.min));
                memcpy(body.box.max, shape + GameAddress::kBonusResBoxOffset + GameAddress::kBoxMaxXOffset, sizeof(body.box.max));
            }

            AmodBonusKindInfo asking = {};
            asking.structSize = sizeof(asking);
            asking.event = AMOD_BONUS_EVENT_BUILD;
            asking.kind = kind;
            asking.record = at;
            asking.body = &body;

            if (mine->handler(&asking, mine->user) == 0)
            {
                return;
            }

            // Resolve model/texture names once here if ids not set.
            if (body.modelId == 0 && body.model != nullptr && body.model[0] != '\0')
            {
                body.modelId = MmdApi::IdFor(reinterpret_cast<void*>(
                    ResolveResource(GameAddress::kResolveModelSlot, body.model)));
            }
            if (body.textureId == 0 && body.texture != nullptr && body.texture[0] != '\0')
            {
                body.textureId = TextureApi::IdFor(reinterpret_cast<void*>(
                    ResolveResource(GameAddress::kResolveTextureSlot, body.texture)));
            }

            if (BuildKindEntity(body, at) == nullptr)
            {
                FORGE_LOG_ERROR("Bonuses: the entity pool would not give kind %d one to stand on", kind);
                return;
            }

            Standing* found = StandingOn(at);
            if (found == nullptr)
            {
                g_standing.push_back(Standing{});
                found = &g_standing.back();
            }

            found->kind = kind;
            found->record = at;
            found->rockingAmp = body.rockingAmp;
            found->rockingSpeed = body.rockingSpeed;
            found->pointsOnTake = body.pointsOnTake;
            found->modelId = body.modelId;
            found->textureId = body.textureId;
            found->generation = WorldApi::LevelGeneration();
        }

        bool ClaimStanding(uint8_t* entity, uint8_t* record, int kind)
        {
            const KindThinkRoutine think = Read<KindThinkRoutine>(entity, GameAddress::kEntityThinkOffset);
            const KindTouchRoutine touch = Read<KindTouchRoutine>(entity, GameAddress::kEntityTouchHandlerOffset);

            if (think == &KindThinkThunk || touch == &KindTouchThunk)
            {
                return false;
            }

            Standing* found = StandingOn(record);
            if (found == nullptr)
            {
                g_standing.push_back(Standing{});
                found = &g_standing.back();
            }

            found->kind = kind;
            found->record = record;
            found->borrowed = true;
            found->gameThink = think;
            found->gameTouch = touch;
            found->generation = WorldApi::LevelGeneration();

            // Capture model/texture ids once, not each tick.
            found->modelId = MmdApi::IdFor(Read<void*>(entity, GameAddress::kEntityModelOffset));
            found->textureId = TextureApi::IdFor(Read<void*>(entity, GameAddress::kEntityTextureOffset));

            // pointsOnTake from type; handler need not look it up.
            const uint8_t* const type = Read<const uint8_t*>(record, GameAddress::kBonusSpawnTypeOffset);
            found->pointsOnTake = (type != nullptr && Readable(type, GameAddress::kBonusResStride))
                ? Read<int>(type, GameAddress::kBonusResPointsTakeOffset)
                : 0;

            const KindThinkRoutine mineThink = &KindThinkThunk;
            const KindTouchRoutine mineTouch = &KindTouchThunk;
            memcpy(entity + GameAddress::kEntityThinkOffset, &mineThink, sizeof(mineThink));
            memcpy(entity + GameAddress::kEntityTouchHandlerOffset, &mineTouch, sizeof(mineTouch));
            return true;
        }

        void ClaimBonusBorrowed(void* room, const void* record, int kind, void*)
        {
            (void)room;

            uint8_t* const at = const_cast<uint8_t*>(static_cast<const uint8_t*>(record));
            uint8_t* const entity = at != nullptr ? EntityFor(at) : nullptr;

            if (entity == nullptr)
            {
                FORGE_LOG_ERROR("Bonuses: kind %d found nothing standing on record %p to take",
                    kind, record);
                return;
            }

            ClaimStanding(entity, at, kind);
        }

        // Drop standing from previous level generation; forget borrows.
        void DropDeadStanding()
        {
            const uint32_t generation = WorldApi::LevelGeneration();
            for (auto it = g_standing.begin(); it != g_standing.end();)
            {
                if (it->generation == generation)
                {
                    ++it;
                    continue;
                }

                if (it->borrowed)
                {
                    Placement::ForgetBorrowed(it->record);
                }
                it = g_standing.erase(it);
            }
        }

        int __cdecl ApiRegisterBonusKind(AmodMod mod, const char* name, AmodBonusKindHandler handler, void* user)
        {
            ModContext* const context = Caller(mod);
            if (context == nullptr || name == nullptr || name[0] == '\0' || handler == nullptr)
            {
                return 0;
            }

            const int kind =
                Placement::RegisterKind(context, name, &BuildBonusKind, &ClaimBonusBorrowed, nullptr);
            if (kind == 0)
            {
                FORGE_LOG_ERROR("Bonuses: '%s' is a kind something already registered", name);
                return 0;
            }

            RegisteredKind fresh;
            fresh.owner = context;
            fresh.kind = kind;
            fresh.name = name;
            fresh.handler = handler;
            fresh.user = user;

            // Parser stand-in record (name + id).
            g_standIns.push_back(std::vector<uint8_t>(GameAddress::kBonusResStride, 0));
            fresh.standIn = g_standIns.back().data();

            strncpy_s(reinterpret_cast<char*>(fresh.standIn), GameAddress::kSpawnNameLength, name, _TRUNCATE);
            strncpy_s(reinterpret_cast<char*>(fresh.standIn + GameAddress::kBonusResIdOffset), GameAddress::kSpawnNameLength, name, _TRUNCATE);

            g_kinds.push_back(fresh);
            InstallFindHook();

            return kind;
        }

        bool PlaceKindRecord(int kind, int roomIndex, const AmodBonusPlacement& where)
        {
            const RegisteredKind* const mine = KindOf(kind);
            if (mine == nullptr || where.column < 0 || where.column >= static_cast<int>(GameAddress::kRoomGridColumns) || 
                where.row < 0 || where.row >= static_cast<int>(GameAddress::kRoomGridRows))
            {
                return false;
            }

            uint8_t* const room = RoomFor(roomIndex);
            const CloneRoutine clone = reinterpret_cast<CloneRoutine>(GameModule::Resolve(GameAddress::kMemClone));
            const AppendRoutine append = reinterpret_cast<AppendRoutine>(GameModule::Resolve(GameAddress::kRoomSpawnsAppend));

            if (room == nullptr || clone == nullptr || append == nullptr)
            {
                return false;
            }

            uint8_t record[GameAddress::kBonusSpawnSize] = {};
            memcpy(record + GameAddress::kSpawnKindOffset, &kind, sizeof(kind));
            record[GameAddress::kBonusSpawnLiveOffset] = 1;

            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kBonusSpawnNameOffset), GameAddress::kSpawnNameLength, mine->name.c_str(), _TRUNCATE);

            if (where.tag != nullptr)
            {
                strncpy_s(reinterpret_cast<char*>(record + GameAddress::kBonusSpawnTagOffset),
                    GameAddress::kSpawnNameLength, where.tag, _TRUNCATE);
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

            // Spawn only in current room.
            if (room == WorldApi::CurrentRoom())
            {
                Placement::Stand(room, placed);
            }
            return true;
        }

        int __cdecl ApiPlaceKindIn(AmodMod mod, int kind, int roomIndex, const AmodBonusPlacement* placement)
        {
            ModContext* const context = Caller(mod);
            const RegisteredKind* const mine = KindOf(kind);

            if (context == nullptr || placement == nullptr ||
                placement->structSize < sizeof(uint32_t) || mine == nullptr || mine->owner != context)
            {
                return 0;
            }

            AmodBonusPlacement where = {};
            memcpy(&where, placement, placement->structSize < sizeof(where) ? placement->structSize : sizeof(where));

            return PlaceKindRecord(kind, roomIndex, where) ? 1 : 0;
        }

        int __cdecl ApiPlaceKind(AmodMod mod, int kind, const AmodBonusPlacement* placement)
        {
            return ApiPlaceKindIn(mod, kind, -1, placement);
        }

        // Restore think/touch and type glow on release.
        void ReleaseStanding(const Standing& one)
        {
            uint8_t* const record = const_cast<uint8_t*>(static_cast<const uint8_t*>(one.record));
            uint8_t* const entity = record != nullptr ? EntityFor(record) : nullptr;

            if (entity != nullptr)
            {
                memcpy(entity + GameAddress::kEntityThinkOffset, &one.gameThink, sizeof(one.gameThink));
                memcpy(entity + GameAddress::kEntityTouchHandlerOffset, &one.gameTouch, sizeof(one.gameTouch));

                const uint8_t* const type = Read<const uint8_t*>(record, GameAddress::kBonusSpawnTypeOffset);

                if (type != nullptr && Readable(type, GameAddress::kBonusResStride))
                {
                    memcpy(entity + GameAddress::kEntityGlowOffset, type + GameAddress::kBonusResGlowOffset, sizeof(float) * 4);
                    memcpy(entity + GameAddress::kEntityGlow2Offset, type + GameAddress::kBonusResGlow2Offset, sizeof(float) * 4);
                }
            }

            Placement::ForgetBorrowed(one.record);
        }

        int __cdecl ApiReturnPlacement(AmodMod mod, int index)
        {
            ModContext* const context = Caller(mod);
            const uint8_t* const record = context != nullptr ? PlacementOf(index) : nullptr;
            Standing* const standing = record != nullptr ? StandingOn(record) : nullptr;

            if (standing == nullptr || !standing->borrowed)
            {
                return 0;
            }

            const RegisteredKind* const mine = KindOf(standing->kind);
            if (mine == nullptr || mine->owner != context)
            {
                return 0;
            }

            ReleaseStanding(*standing);

            for (auto it = g_standing.begin(); it != g_standing.end(); ++it)
            {
                if (it->record == record)
                {
                    g_standing.erase(it);
                    break;
                }
            }
            return 1;
        }

        int __cdecl ApiBorrowPlacementIn(AmodMod mod, int kind, int roomIndex, int index)
        {
            ModContext* const context = Caller(mod);
            const RegisteredKind* const mine = KindOf(kind);

            if (context == nullptr || mine == nullptr || mine->owner != context)
            {
                return 0;
            }

            uint8_t* const record = PlacementIn(roomIndex, index);
            if (record == nullptr || record[GameAddress::kBonusSpawnLiveOffset] == 0)
            {
                return 0;
            }

            // Only borrow game kind 9; mod kinds already owned.
            if (Read<int>(record, GameAddress::kSpawnKindOffset) != GameAddress::kPlacementKindBonus)
            {
                return 0;
            }

            Placement::Borrow(record, kind);

            uint8_t* const entity = EntityFor(record);
            if (entity != nullptr)
            {
                ClaimStanding(entity, record, kind);
            }
            return 1;
        }

        int __cdecl ApiBorrowPlacement(AmodMod mod, int kind, int index)
        {
            return ApiBorrowPlacementIn(mod, kind, -1, index);
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
        g_bonusTable.AddSpawnHandler = &ApiAddSpawnHandler;
        g_bonusTable.RemoveSpawnHandler = &ApiRemoveSpawnHandler;
        g_bonusTable.GetPlacement = &ApiGetPlacement;
        g_bonusTable.RemovePlacement = &ApiRemovePlacement;
        g_bonusTable.PlacementCount = &ApiPlacementCount;
        g_bonusTable.GetPlacementIn = &ApiGetPlacementIn;
        g_bonusTable.RemovePlacementIn = &ApiRemovePlacementIn;
        g_bonusTable.PlaceIn = &ApiPlaceBonusIn;
        g_bonusTable.GetPlacementProperty = &ApiGetPlacementProperty;
        g_bonusTable.SetPlacementProperty = &ApiSetPlacementProperty;
        g_bonusTable.PlacementPropertyCount = &ApiPlacementPropertyCount;
        g_bonusTable.PlacementPropertyNameAt = &ApiPlacementPropertyNameAt;
        g_bonusTable.HasTypeProperty = &ApiHasTypeProperty;
        g_bonusTable.TypePropertyCount = &ApiTypePropertyCount;
        g_bonusTable.TypePropertyNameAt = &ApiTypePropertyNameAt;

        // Mod kinds go through Placement-owned spawn slot.
        if (Placement::Install())
        {
            g_bonusTable.RegisterKind = &ApiRegisterBonusKind;
            g_bonusTable.PlaceKind = &ApiPlaceKind;
            g_bonusTable.PlaceKindIn = &ApiPlaceKindIn;
            g_bonusTable.BorrowPlacementIn = &ApiBorrowPlacementIn;
            g_bonusTable.BorrowPlacement = &ApiBorrowPlacement;
            g_bonusTable.ReturnPlacement = &ApiReturnPlacement;
            FrameApi::AddInternalTick(&DropDeadStanding);
        }

        // Apply type defs deferred until table exists.
        FrameApi::AddInternalTick(&ApplyPendingTypes);

        if (InstallTouchHook())
        {
            g_bonusTable.AddTakeHandler = &ApiAddTakeHandler;
            g_bonusTable.RemoveTakeHandler = &ApiRemoveTakeHandler;
        }

        // Spawn hook early so silent drops are still reported.
        InstallSpawnHook();

        return true;
    }

    void BonusesApi::Uninstall()
    {
        g_freeBonusResourcesHook.Uninstall();
        g_touchHook.Uninstall();
        g_spawnHook.Uninstall();
        g_findHook.Uninstall();
        g_originalFind = nullptr;
        g_standIns.clear();

        g_originalFreeBonusResources = nullptr;
        g_originalTouch = nullptr;
        g_originalSpawn = nullptr;

        g_spawnHandlers.clear();
        g_takeHandlers.clear();
        g_pendingTypes.clear();
        g_definedTypes.clear();
        g_freeRows.clear();
        g_kinds.clear();
        g_standing.clear();
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
        for (auto it = g_standing.begin(); it != g_standing.end();)
        {
            const RegisteredKind* const mine = KindOf(it->kind);
            if (mine == nullptr || mine->owner != owner)
            {
                ++it;
                continue;
            }

            uint8_t* const record = const_cast<uint8_t*>(static_cast<const uint8_t*>(it->record));
            uint8_t* const entity = record != nullptr ? EntityFor(record) : nullptr;

            if (it->borrowed)
            {
                ReleaseStanding(*it);
            }
            else if (record != nullptr && record[GameAddress::kBonusSpawnLiveOffset] != 0)
            {
                if (entity != nullptr)
                {
                    MarkTaken(entity, record);
                }
                else
                {
                    record[GameAddress::kBonusSpawnLiveOffset] = 0;
                }
            }

            it = g_standing.erase(it);
        }

        for (auto it = g_kinds.begin(); it != g_kinds.end();)
        {
            it = it->owner == owner ? g_kinds.erase(it) : it + 1;
        }

        Placement::ForgetMod(owner);

        for (auto it = g_takeHandlers.begin(); it != g_takeHandlers.end();)
        {
            it = it->owner == owner ? g_takeHandlers.erase(it) : ++it;
        }
        for (auto it = g_pendingTypes.begin(); it != g_pendingTypes.end();)
        {
            it = it->owner == owner ? g_pendingTypes.erase(it) : ++it;
        }

        // Blank defined rows (indices must not shift); zero shared handles.
        for (auto it = g_definedTypes.begin(); it != g_definedTypes.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            const int index = BonusRecordIndexOf(it->id.c_str());
            uint8_t* const record = index >= 0 ? const_cast<uint8_t*>(BonusRecord(index)) : nullptr;

            if (record != nullptr)
            {
                memset(record, 0, GameAddress::kBonusResStride);
                g_freeRows.push_back(index);
            }

            it = g_definedTypes.erase(it);
        }
    }
}
