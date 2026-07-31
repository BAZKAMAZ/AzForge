#include <windows.h>

#include "src/Api/InventoryApi.h"

#include "src/Api/ApiProperty.h"
#include "src/Api/ApiRead.h"
#include "src/Api/BonusesApi.h"
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

        // The same resolver the parsers go through, for an icon named by a mod.
        uint32_t ResolveResource(uintptr_t slot, const char* name)
        {
            void** const at = static_cast<void**>(GameModule::Resolve(slot));
            const auto routine = at != nullptr
                                     ? reinterpret_cast<uint32_t (__cdecl*)(const char*)>(*at)
                                     : nullptr;

            return (routine != nullptr && name != nullptr) ? routine(name) : 0;
        }

        // --- the player and what it carries ------------------------------------------------
        //
        // The player is an entity like any other and its kind is its first field, which is what
        // Bonus_OnTouch tests before letting anything be picked up. So it is found by walking the
        // live list rather than through a global of its own.

        AmodInventoryApi g_inventoryTable = {};

        // What the game is currently walking. Five until a mod says otherwise.
        int g_slotCount = static_cast<int>(GameAddress::kInventorySlotCount);

        const uint8_t* SlotBase();
        size_t SlotsReachable();

        const uint8_t* InventorySlot(int slot)
        {
            const uint8_t* const base = SlotBase();
            if (base == nullptr || slot < 0 || slot >= static_cast<int>(SlotsReachable()))
            {
                return nullptr;
            }

            return base + static_cast<size_t>(slot) * GameAddress::kInventorySlotStride;
        }

        int __cdecl ApiInventoryAvailable(AmodMod mod)
        {
            return (Caller(mod) != nullptr && WorldApi::Player() != nullptr) ? 1 : 0;
        }

        int __cdecl ApiGetInventoryItem(AmodMod mod, int slot, AmodInventoryItem* out)
        {
            const uint8_t* const record = Caller(mod) != nullptr ? InventorySlot(slot) : nullptr;
            if (record == nullptr || out == nullptr || out->structSize < sizeof(uint32_t))
            {
                return 0;
            }

            const uint32_t flags = Read<uint32_t>(record, GameAddress::kInventoryFlagsOffset);

            AmodInventoryItem filled = {};
            filled.structSize = sizeof(AmodInventoryItem);
            filled.name = NameAt(record, GameAddress::kInventoryNameOffset)
                              ? ReadText(record, GameAddress::kInventoryNameOffset)
                              : "";
            filled.timeLeft = Read<float>(record, GameAddress::kInventoryTimeLeftOffset);
            filled.timeTotal = Read<float>(record, GameAddress::kInventoryTimeTotalOffset);
            filled.isTotem = (flags & GameAddress::kBonusFlagTotem) != 0 ? 1 : 0;
            filled.isTorch = (flags & GameAddress::kBonusFlagTorch) != 0 ? 1 : 0;
            filled.pointsOnRunOut = Read<int>(record, GameAddress::kInventoryPointsOutOffset);
            filled.burnTime = Read<float>(record, GameAddress::kInventoryBurnTimeOffset);

            const uint32_t copied = out->structSize < sizeof(filled)
                                        ? out->structSize
                                        : static_cast<uint32_t>(sizeof(filled));
            memcpy(out, &filled, copied);
            memcpy(out, &copied, sizeof(copied));
            return 1;
        }

        int __cdecl ApiGetInventoryUsed(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            int used = 0;
            for (int slot = 0; slot < g_slotCount; ++slot)
            {
                const uint8_t* const record = InventorySlot(slot);
                used += (record != nullptr &&
                         ReadText(record, GameAddress::kInventoryNameOffset)[0] != '\0') ? 1 : 0;
            }
            return used;
        }

        template <typename T>
        T Global(uintptr_t address, T fallback)
        {
            const void* const at = GameModule::Resolve(address);
            return at != nullptr ? *static_cast<const T*>(at) : fallback;
        }

        int __cdecl ApiGetChain(AmodMod mod, AmodBonusChain* out)
        {
            if (Caller(mod) == nullptr || out == nullptr || out->structSize < sizeof(uint32_t))
            {
                return 0;
            }

            AmodBonusChain filled = {};
            filled.structSize = sizeof(AmodBonusChain);
            filled.length = Global<int>(GameAddress::kChainLength, 0);
            filled.points = Global<int>(GameAddress::kChainPoints, 0);
            filled.timeLeft = Global<float>(GameAddress::kChainTimer, 0.0f);
            filled.window = Global<float>(GameAddress::kChainWindow, 0.0f);

            const uint32_t copied = out->structSize < sizeof(filled)
                                        ? out->structSize
                                        : static_cast<uint32_t>(sizeof(filled));
            memcpy(out, &filled, copied);
            memcpy(out, &copied, sizeof(copied));
            return 1;
        }

        int __cdecl ApiGetBonusesLeft(AmodMod mod)
        {
            return Caller(mod) != nullptr ? Global<int>(GameAddress::kBonusesLeft, 0) : 0;
        }

        int __cdecl ApiGetMegaBonusesLeft(AmodMod mod)
        {
            return Caller(mod) != nullptr ? Global<int>(GameAddress::kMegaBonusesLeft, 0) : 0;
        }

        int __cdecl ApiGetOtherBonusesLeft(AmodMod mod)
        {
            return Caller(mod) != nullptr ? Global<int>(GameAddress::kOtherBonusesLeft, 0) : 0;
        }

        // --- writing the carried slots ------------------------------------------------------

        typedef void (__cdecl* ClearSlotRoutine)(void* player, void* slot);

        uint32_t SlotTimerFlags(uint32_t flags, AmodBonusTimer timer)
        {
            flags &= ~(GameAddress::kBonusFlagTotem | GameAddress::kBonusFlagSuppressed);
            if (timer != AMOD_BONUS_TIMER_NONE)
            {
                flags |= GameAddress::kBonusFlagTotem;
            }
            if (timer == AMOD_BONUS_TIMER_MOD)
            {
                flags |= GameAddress::kBonusFlagSuppressed;
            }
            return flags;
        }

        int __cdecl ApiFindInventorySlot(AmodMod mod, const char* name)
        {
            if (Caller(mod) == nullptr || name == nullptr)
            {
                return -1;
            }

            for (int slot = 0; slot < g_slotCount; ++slot)
            {
                const uint8_t* const record = InventorySlot(slot);
                if (record != nullptr &&
                    strcmp(ReadText(record, GameAddress::kInventoryNameOffset), name) == 0)
                {
                    return slot;
                }
            }
            return -1;
        }

        int __cdecl ApiFindFreeInventorySlot(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return -1;
            }

            for (int slot = 0; slot < g_slotCount; ++slot)
            {
                const uint8_t* const record = InventorySlot(slot);
                if (record != nullptr &&
                    ReadText(record, GameAddress::kInventoryNameOffset)[0] == '\0')
                {
                    return slot;
                }
            }
            return -1;
        }

        int __cdecl ApiPutInventoryItem(AmodMod mod, int slot, const char* typeName, float time,
                                        AmodBonusTimer timer)
        {
            uint8_t* const record = Caller(mod) != nullptr
                                        ? const_cast<uint8_t*>(InventorySlot(slot))
                                        : nullptr;
            if (record == nullptr || typeName == nullptr)
            {
                return 0;
            }

            const uint8_t* const type = BonusesApi::FindType(typeName);
            if (type == nullptr)
            {
                return 0;
            }

            const uint32_t flags =
                SlotTimerFlags(Read<uint32_t>(type, GameAddress::kBonusResFlagsOffset), timer);
            const float lasts = time > 0.0f ? time
                                            : Read<float>(type, GameAddress::kBonusResTimeOffset);

            const uint32_t icon = Read<uint32_t>(type, GameAddress::kBonusResIconOffset);
            const int pointsOut = Read<int>(type, GameAddress::kBonusResPointsOutOffset);
            const uint32_t soundUse = Read<uint32_t>(type, GameAddress::kBonusResSoundUseOffset);
            const float burn = (flags & GameAddress::kBonusFlagTorch) != 0
                                   ? Read<float>(type, GameAddress::kBonusResBurnTimeOffset)
                                   : 0.0f;

            memcpy(record + GameAddress::kInventoryIconOffset, &icon, sizeof(icon));
            memcpy(record + GameAddress::kInventoryPointsOutOffset, &pointsOut, sizeof(pointsOut));
            memcpy(record + GameAddress::kInventorySoundUseOffset, &soundUse, sizeof(soundUse));
            memcpy(record + GameAddress::kInventoryTypeOffset, &type, sizeof(type));
            memcpy(record + GameAddress::kInventoryFlagsOffset, &flags, sizeof(flags));
            memcpy(record + GameAddress::kInventoryTimeLeftOffset, &lasts, sizeof(lasts));
            memcpy(record + GameAddress::kInventoryTimeTotalOffset, &lasts, sizeof(lasts));
            memcpy(record + GameAddress::kInventoryBurnTimeOffset, &burn, sizeof(burn));

            strncpy_s(reinterpret_cast<char*>(record + GameAddress::kInventoryNameOffset),
                      GameAddress::kSpawnNameLength,
                      ReadText(type, GameAddress::kBonusResNameOffset), _TRUNCATE);
            return 1;
        }

        int __cdecl ApiClearInventoryItem(AmodMod mod, int slot)
        {
            uint8_t* const record = Caller(mod) != nullptr
                                        ? const_cast<uint8_t*>(InventorySlot(slot))
                                        : nullptr;
            uint8_t* const player = record != nullptr ? const_cast<uint8_t*>(WorldApi::Player()) : nullptr;

            const ClearSlotRoutine clear =
                reinterpret_cast<ClearSlotRoutine>(GameModule::Resolve(GameAddress::kInventoryClearSlot));

            if (record == nullptr || ReadText(record, GameAddress::kInventoryNameOffset)[0] == '\0')
            {
                return 0;
            }

            if (player != nullptr && clear != nullptr)
            {
                clear(player, record);
            }
            else
            {
                record[GameAddress::kInventoryNameOffset] = '\0';
            }
            return 1;
        }

        // The icon one carried item shows, without touching the type it came from.
        int __cdecl ApiSetInventoryIcon(AmodMod mod, int slot, const char* file)
        {
            uint8_t* const record = Caller(mod) != nullptr && file != nullptr
                                        ? const_cast<uint8_t*>(InventorySlot(slot))
                                        : nullptr;
            if (record == nullptr || ReadText(record, GameAddress::kInventoryNameOffset)[0] == '\0')
            {
                return 0;
            }

            const uint32_t icon = ResolveResource(GameAddress::kResolveTextureSlot, file);
            if (icon == 0)
            {
                return 0;
            }

            memcpy(record + GameAddress::kInventoryIconOffset, &icon, sizeof(icon));
            return 1;
        }

        // --- how many slots there are ---------------------------------------------------------
        //
        // Not a number the game keeps anywhere: six routines each carry their own copy as an
        // immediate, so changing it means writing six bytes. Every one is checked against the
        // opcode in front of it first, so a byte is only written where the reverse says it is.

        int __cdecl ApiGetSlotCount(AmodMod mod)
        {
            return Caller(mod) != nullptr ? g_slotCount : 0;
        }

        alignas(16) uint8_t g_slots[GameAddress::kInventorySlotsMax *
                                    GameAddress::kInventorySlotStride] = {};

        bool g_moved = false;

        ModContext* g_slotOwner = nullptr;

        const uint8_t* SlotBase()
        {
            if (g_moved)
            {
                return g_slots;
            }

            const uint8_t* const player = WorldApi::Player();
            return player != nullptr ? player + GameAddress::kPlayerInventoryOffset : nullptr;
        }

        size_t SlotsReachable()
        {
            return g_moved ? GameAddress::kInventorySlotsMax : GameAddress::kInventorySlotCount;
        }

        uint8_t g_originalBase[sizeof(GameAddress::kSlotBaseSites) /
                               sizeof(GameAddress::kSlotBaseSites[0])][7] = {};

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

        bool MoveSlotsOut()
        {
            if (g_moved)
            {
                return true;
            }

            size_t index = 0;
            for (const GameAddress::SlotBaseSite& site : GameAddress::kSlotBaseSites)
            {
                const uint8_t* const at = static_cast<const uint8_t*>(GameModule::Resolve(site.address));
                if (at == nullptr || memcmp(at, site.original, site.length) != 0)
                {
                    FORGE_LOG_ERROR("The slots cannot be moved: 0x%08X does not hold what it was "
                                    "reversed as", static_cast<unsigned>(site.address));
                    return false;
                }
                memcpy(g_originalBase[index++], at, site.length);
            }

            const uint8_t* const player = WorldApi::Player();
            if (player != nullptr)
            {
                memcpy(g_slots, player + GameAddress::kPlayerInventoryOffset,
                       GameAddress::kInventorySlotCount * GameAddress::kInventorySlotStride);
            }

            const uint32_t buffer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_slots));

            for (const GameAddress::SlotBaseSite& site : GameAddress::kSlotBaseSites)
            {
                const uint32_t target = buffer + site.addend;

                uint8_t patch[7];
                memset(patch, 0x90, sizeof(patch));                 // NOP the tail
                memcpy(patch, site.prefix, site.prefixLength);
                memcpy(patch + site.prefixLength, &target, sizeof(target));

                if (!WriteCode(static_cast<uint8_t*>(GameModule::Resolve(site.address)), patch,
                               site.length))
                {
                    return false;
                }
            }

            g_moved = true;
            return true;
        }

        void PutSlotsBack()
        {
            if (!g_moved)
            {
                return;
            }

            size_t index = 0;
            for (const GameAddress::SlotBaseSite& site : GameAddress::kSlotBaseSites)
            {
                WriteCode(static_cast<uint8_t*>(GameModule::Resolve(site.address)),
                          g_originalBase[index++], site.length);
            }
            g_moved = false;
        }

        bool WriteSlotCount(int count)
        {
            for (const GameAddress::SlotCountSite& site : GameAddress::kSlotCountSites)
            {
                uint8_t* const at = static_cast<uint8_t*>(GameModule::Resolve(site.address));
                const uint8_t written = static_cast<uint8_t>(count);

                if (at == nullptr || !WriteCode(at, &written, 1))
                {
                    return false;
                }
            }
            return true;
        }

        int __cdecl ApiSetSlotCount(AmodMod mod, int count)
        {
            if (Caller(mod) == nullptr || count < 1 ||
                count > static_cast<int>(GameAddress::kInventorySlotsMax))
            {
                FORGE_LOG_ERROR("An inventory of %d is refused; 1 to %d is what there is room for",
                                count, static_cast<int>(GameAddress::kInventorySlotsMax));
                return 0;
            }

            if (count > static_cast<int>(GameAddress::kInventorySlotCount) && !MoveSlotsOut())
            {
                return 0;
            }

            for (const GameAddress::SlotCountSite& site : GameAddress::kSlotCountSites)
            {
                const uint8_t* const opcode =
                    static_cast<const uint8_t*>(GameModule::Resolve(site.opcodeAt));
                if (opcode == nullptr || *opcode != site.opcode)
                {
                    FORGE_LOG_ERROR("The inventory size at 0x%08X is not where it was reversed",
                                    static_cast<unsigned>(site.address));
                    return 0;
                }
            }

            if (!WriteSlotCount(count))
            {
                return 0;
            }

            const int reachable = g_moved ? static_cast<int>(GameAddress::kInventorySlotsMax)
                                          : static_cast<int>(GameAddress::kInventorySlotCount);

            for (int slot = count; slot < reachable; ++slot)
            {
                uint8_t* const record = const_cast<uint8_t*>(InventorySlot(slot));
                if (record != nullptr)
                {
                    memset(record, 0, GameAddress::kInventorySlotStride);
                }
            }

            g_slotCount = count;
            g_slotOwner = Caller(mod);
            return 1;
        }

        // --- the keys one carried item carries -----------------------------------------------

        using ApiProperty::Field;
        using ApiProperty::Kind;

        constexpr Field kItemFields[] =
        {
            { "name",       GameAddress::kInventoryNameOffset,      Kind::Text,     0, 0, 0, false },
            { "icon",       GameAddress::kInventoryIconOffset,      Kind::Resource, 0, 0, GameAddress::kResolveTextureSlot, true },
            { "sound_use",  GameAddress::kInventorySoundUseOffset,  Kind::Resource, 0, 0, GameAddress::kResolveSoundSlot,   true },

            { "pts_out",    GameAddress::kInventoryPointsOutOffset, Kind::Int,      0, 0, 0, true },
            { "time_left",  GameAddress::kInventoryTimeLeftOffset,  Kind::Float,    0, 0, 0, true },
            { "time_total", GameAddress::kInventoryTimeTotalOffset, Kind::Float,    0, 0, 0, true },
            { "burn_time",  GameAddress::kInventoryBurnTimeOffset,  Kind::Float,    0, 0, 0, true },

            { "flags",         GameAddress::kInventoryFlagsOffset,  Kind::Int,      0, 0, 0, true },
            { "has_timer",     GameAddress::kInventoryFlagsOffset,  Kind::Flag,     0, GameAddress::kBonusFlagTotem,      0, true },
            { "timer_is_mine", GameAddress::kInventoryFlagsOffset,  Kind::Flag,     0, GameAddress::kBonusFlagSuppressed, 0, true },
        };

        int __cdecl ApiItemPropertyCount(AmodMod mod)
        {
            return Caller(mod) != nullptr ? static_cast<int>(sizeof(kItemFields) / sizeof(Field))
                                          : 0;
        }

        const char* __cdecl ApiItemPropertyNameAt(AmodMod mod, int index)
        {
            const int total = ApiItemPropertyCount(mod);
            return (index >= 0 && index < total) ? kItemFields[index].key : "";
        }

        // A slot that holds nothing carries no keys: what is at those offsets belongs to whatever
        // was there last.
        uint8_t* FilledSlot(AmodMod mod, int slot)
        {
            uint8_t* const record = Caller(mod) != nullptr
                                        ? const_cast<uint8_t*>(InventorySlot(slot))
                                        : nullptr;

            return (record != nullptr &&
                    ReadText(record, GameAddress::kInventoryNameOffset)[0] != '\0')
                       ? record
                       : nullptr;
        }

        int __cdecl ApiHasItemProperty(AmodMod mod, int slot, const char* key)
        {
            return (FilledSlot(mod, slot) != nullptr &&
                    ApiProperty::Find(kItemFields, sizeof(kItemFields) / sizeof(Field), key) != nullptr)
                       ? 1
                       : 0;
        }

        int __cdecl ApiGetItemProperty(AmodMod mod, int slot, const char* key, AmodValue* value)
        {
            const uint8_t* const record = FilledSlot(mod, slot);
            const Field* const field =
                ApiProperty::Find(kItemFields, sizeof(kItemFields) / sizeof(Field), key);

            return (record != nullptr && field != nullptr) ? ApiProperty::Get(record, *field, value)
                                                           : 0;
        }

        int __cdecl ApiSetItemProperty(AmodMod mod, int slot, const char* key,
                                       const AmodValue* value)
        {
            uint8_t* const record = FilledSlot(mod, slot);
            const Field* const field =
                ApiProperty::Find(kItemFields, sizeof(kItemFields) / sizeof(Field), key);

            return (record != nullptr && field != nullptr && value != nullptr)
                       ? ApiProperty::Set(record, *field, *value)
                       : 0;
        }

        int __cdecl ApiSetInventoryTimeLeft(AmodMod mod, int slot, float seconds)
        {
            uint8_t* const record = Caller(mod) != nullptr
                                        ? const_cast<uint8_t*>(InventorySlot(slot))
                                        : nullptr;
            if (record == nullptr || ReadText(record, GameAddress::kInventoryNameOffset)[0] == '\0')
            {
                return 0;
            }

            const float total = Read<float>(record, GameAddress::kInventoryTimeTotalOffset);
            const float clamped = seconds < 0.0f ? 0.0f : (seconds > total ? total : seconds);

            memcpy(record + GameAddress::kInventoryTimeLeftOffset, &clamped, sizeof(clamped));
            return 1;
        }
    }

    bool InventoryApi::Install()
    {
        g_inventoryTable.structSize = sizeof(AmodInventoryApi);
        g_inventoryTable.IsAvailable = &ApiInventoryAvailable;
        g_inventoryTable.GetItem = &ApiGetInventoryItem;
        g_inventoryTable.GetUsedCount = &ApiGetInventoryUsed;
        g_inventoryTable.GetChain = &ApiGetChain;
        g_inventoryTable.GetBonusesLeft = &ApiGetBonusesLeft;
        g_inventoryTable.GetMegaBonusesLeft = &ApiGetMegaBonusesLeft;
        g_inventoryTable.GetOtherBonusesLeft = &ApiGetOtherBonusesLeft;
        g_inventoryTable.FindSlot = &ApiFindInventorySlot;
        g_inventoryTable.FindFreeSlot = &ApiFindFreeInventorySlot;
        g_inventoryTable.PutItem = &ApiPutInventoryItem;
        g_inventoryTable.ClearItem = &ApiClearInventoryItem;
        g_inventoryTable.SetTimeLeft = &ApiSetInventoryTimeLeft;
        g_inventoryTable.SetIcon = &ApiSetInventoryIcon;
        g_inventoryTable.GetItemProperty = &ApiGetItemProperty;
        g_inventoryTable.SetItemProperty = &ApiSetItemProperty;
        g_inventoryTable.HasItemProperty = &ApiHasItemProperty;
        g_inventoryTable.ItemPropertyCount = &ApiItemPropertyCount;
        g_inventoryTable.ItemPropertyNameAt = &ApiItemPropertyNameAt;
        g_inventoryTable.GetSlotCount = &ApiGetSlotCount;
        g_inventoryTable.SetSlotCount = &ApiSetSlotCount;

        return true;
    }

    void InventoryApi::ForgetMod(ModContext* owner)
    {
        if (owner == nullptr || owner != g_slotOwner)
        {
            return;
        }

        for (int slot = 0; slot < static_cast<int>(SlotsReachable()); ++slot)
        {
            uint8_t* const record = const_cast<uint8_t*>(InventorySlot(slot));
            if (record != nullptr)
            {
                memset(record, 0, GameAddress::kInventorySlotStride);
            }
        }

        PutSlotsBack();
        WriteSlotCount(static_cast<int>(GameAddress::kInventorySlotCount));

        g_slotCount = static_cast<int>(GameAddress::kInventorySlotCount);
        g_slotOwner = nullptr;
    }

    void InventoryApi::Uninstall()
    {
        PutSlotsBack();

        g_inventoryTable = AmodInventoryApi{};
    }

    const AmodInventoryApi* InventoryApi::Table()
    {
        return g_inventoryTable.structSize != 0 ? &g_inventoryTable : nullptr;
    }
}
