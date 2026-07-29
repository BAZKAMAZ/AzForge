#include "pch.h"

#include "src/Api/WorldApi.h"

#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

#include <stddef.h>
#include <string.h>
#include <vector>

namespace Forge
{
    namespace
    {
        // Level_Load and Level_Free take nothing but their level; Level_ActivateRoom takes the
        // step to make through the maze. All three are __thiscall, which is __fastcall with the
        // second register argument unused.
        typedef char(__fastcall* LevelLoadRoutine)(void* level, void* unused, const char* script);
        typedef void(__fastcall* LevelFreeRoutine)(void* level, void* unused);
        typedef void*(__fastcall* LevelActivateRoomRoutine)(void* level, void* unused, int dx, int dy);

        InlineHook g_loadHook;
        InlineHook g_freeHook;
        InlineHook g_activateHook;

        LevelLoadRoutine g_originalLoad = nullptr;
        LevelFreeRoutine g_originalFree = nullptr;
        LevelActivateRoomRoutine g_originalActivate = nullptr;

        uint8_t* g_level = nullptr;

        AmodWorldApi g_table = {};

        struct Subscription
        {
            uint32_t id;
            ModContext* owner;
            AmodWorldEvent event;
            AmodWorldEventHandler handler;
            void* user;
        };

        std::vector<Subscription> g_subscriptions;
        uint32_t g_nextSubscriptionId = 1;

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        // --- reading the level ------------------------------------------------------------

        template <typename T>
        T Read(const uint8_t* base, uintptr_t offset)
        {
            T value;
            memcpy(&value, base + offset, sizeof(T));
            return value;
        }

        const char* ReadText(const uint8_t* base, uintptr_t offset)
        {
            return reinterpret_cast<const char*>(base + offset);
        }

        int RoomCount()
        {
            return g_level != nullptr ? Read<int>(g_level, GameAddress::kLevelRoomCountOffset) : 0;
        }

        // A level is loaded exactly when it has rooms. Level_Free clears the level rather than
        // releasing it - the object is static - and a Level_Load that gave up before the $rooms
        // block never appended any, so the count is the game's own answer to the question and
        // needs no flag of AzForge's own kept alongside it.
        bool IsLevelLoaded()
        {
            return RoomCount() > 0;
        }

        uint8_t* Rooms()
        {
            return g_level != nullptr
                       ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(g_level, GameAddress::kLevelRoomsOffset))
                       : nullptr;
        }

        uint8_t* RoomEntry(int index)
        {
            uint8_t* const rooms = Rooms();
            if (rooms == nullptr || index < 0 || index >= RoomCount())
            {
                return nullptr;
            }
            return rooms + static_cast<size_t>(index) * GameAddress::kRoomEntryStride;
        }

        // The room itself is embedded in the entry rather than pointed at.
        uint8_t* Room(int index)
        {
            uint8_t* const entry = RoomEntry(index);
            return entry != nullptr ? entry + GameAddress::kRoomEntryRoomOffset : nullptr;
        }

        // Turns the entry pointer the game keeps for the current room back into an index.
        int IndexOfEntry(const void* entry)
        {
            const uint8_t* const rooms = Rooms();
            if (entry == nullptr || rooms == nullptr)
            {
                return -1;
            }

            const ptrdiff_t stride = static_cast<ptrdiff_t>(GameAddress::kRoomEntryStride);
            const ptrdiff_t distance = static_cast<const uint8_t*>(entry) - rooms;
            if (distance < 0 || distance % stride != 0)
            {
                return -1;
            }

            const int index = static_cast<int>(distance / stride);
            return index < RoomCount() ? index : -1;
        }

        int FindRoomById(int id)
        {
            const int count = RoomCount();
            for (int index = 0; index < count; ++index)
            {
                if (Read<int>(RoomEntry(index), GameAddress::kRoomEntryIdOffset) == id)
                {
                    return index;
                }
            }
            return -1;
        }

        // A maze row is { int* roomIds; int width } and rows are ragged, so a row carries the
        // width of its own $maze line rather than the maze having one.
        const uint8_t* MazeRow(int y)
        {
            if (g_level == nullptr || y < 0 ||
                y >= Read<int>(g_level, GameAddress::kLevelMazeHeightOffset))
            {
                return nullptr;
            }

            const uint8_t* const maze =
                reinterpret_cast<const uint8_t*>(Read<uintptr_t>(g_level, GameAddress::kLevelMazeOffset));
            return maze != nullptr ? maze + static_cast<size_t>(y) * GameAddress::kMazeRowStride
                                   : nullptr;
        }

        int MazeRowWidth(int y)
        {
            const uint8_t* const row = MazeRow(y);
            return row != nullptr ? Read<int>(row, GameAddress::kMazeRowWidthOffset) : 0;
        }

        int MazeRoomId(int x, int y)
        {
            const uint8_t* const row = MazeRow(y);
            if (row == nullptr || x < 0 || x >= Read<int>(row, GameAddress::kMazeRowWidthOffset))
            {
                return 0;
            }

            const int* const ids =
                reinterpret_cast<const int*>(Read<uintptr_t>(row, GameAddress::kMazeRowIdsOffset));
            return ids != nullptr ? ids[x] : 0;
        }

        // Where a room sits on the maze, the same search Level_FindRoomPos @0x0040d670 makes.
        // A room the level carries but no maze cell names is not on it at all.
        void FindMazePos(int id, int& x, int& y)
        {
            x = -1;
            y = -1;
            if (g_level == nullptr)
            {
                return;
            }

            const int rows = Read<int>(g_level, GameAddress::kLevelMazeHeightOffset);
            for (int row = 0; row < rows; ++row)
            {
                const int width = MazeRowWidth(row);
                for (int column = 0; column < width; ++column)
                {
                    if (MazeRoomId(column, row) == id)
                    {
                        x = column;
                        y = row;
                        return;
                    }
                }
            }
        }

        // Copies as much of a filled structure as the caller has room for. A mod built against
        // a later version of the interface gets the fields this loader knows and keeps the
        // zeroes it started with in the rest; one built against an earlier version gets the
        // prefix it asked for. Either way structSize comes back as what was actually written.
        int CopyOut(void* out, uint32_t requested, const void* filled, size_t produced)
        {
            if (out == nullptr || requested < sizeof(uint32_t))
            {
                return 0;
            }

            const uint32_t copied = requested < produced ? requested
                                                        : static_cast<uint32_t>(produced);
            memcpy(out, filled, copied);
            memcpy(out, &copied, sizeof(copied));
            return 1;
        }

        // --- events -----------------------------------------------------------------------

        void Dispatch(AmodWorldEvent event, int roomIndex, int roomId, int previousRoomIndex,
                      int mazeX, int mazeY)
        {
            if (g_subscriptions.empty())
            {
                return;
            }

            AmodWorldEventInfo info = {};
            info.structSize = sizeof(AmodWorldEventInfo);
            info.event = event;
            info.roomIndex = roomIndex;
            info.roomId = roomId;
            info.previousRoomIndex = previousRoomIndex;
            info.mazeX = mazeX;
            info.mazeY = mazeY;

            // A copy, so a mod dropping its handler from inside the handler cannot break
            // iteration.
            const std::vector<Subscription> snapshot = g_subscriptions;
            for (const Subscription& subscription : snapshot)
            {
                if (subscription.event == event && subscription.handler != nullptr)
                {
                    subscription.handler(&info, subscription.user);
                }
            }
        }

        void ReleaseSubscription(void* payload)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload));
            for (auto it = g_subscriptions.begin(); it != g_subscriptions.end(); ++it)
            {
                if (it->id == id)
                {
                    g_subscriptions.erase(it);
                    return;
                }
            }
        }

        // --- the detours ------------------------------------------------------------------

        char __fastcall LoadDetour(void* level, void* unused, const char* script)
        {
            const char result = g_originalLoad(level, unused, script);

            // Level_Load opens by freeing whatever was loaded, so the unload event has already
            // gone out by the time this returns. Reporting the level only when rooms came out
            // of it keeps a script that failed to parse from announcing a level that is not
            // there - the same test IsLevelLoaded makes.
            if (IsLevelLoaded())
            {
                Dispatch(AMOD_WORLD_EVENT_LEVEL_LOADED, -1, 0, -1, -1, -1);
            }

            return result;
        }

        void __fastcall FreeDetour(void* level, void* unused)
        {
            // Before the game clears it, so a mod can still read what is going away. Level_Load
            // and Level_Start both call this whether or not a level is up, which is why an
            // empty level is not announced as unloading.
            if (IsLevelLoaded())
            {
                Dispatch(AMOD_WORLD_EVENT_LEVEL_UNLOADED, -1, 0, -1, -1, -1);
            }

            g_originalFree(level, unused);
        }

        void* __fastcall ActivateRoomDetour(void* level, void* unused, int dx, int dy)
        {
            void* const room = g_originalActivate(level, unused, dx, dy);

            // A null means the step led nowhere - off the maze, an empty cell, or an id with no
            // room behind it - and the current room did not change.
            if (room == nullptr || g_level == nullptr)
            {
                return room;
            }

            const int index = IndexOfEntry(Read<void*>(g_level, GameAddress::kLevelCurrentRoomOffset));
            const int previous =
                IndexOfEntry(Read<void*>(g_level, GameAddress::kLevelPreviousRoomOffset));

            Dispatch(AMOD_WORLD_EVENT_ROOM_CHANGED, index,
                     Read<int>(g_level, GameAddress::kLevelCurrentRoomIdOffset), previous,
                     Read<int>(g_level, GameAddress::kLevelMazeXOffset),
                     Read<int>(g_level, GameAddress::kLevelMazeYOffset));

            return room;
        }

        // --- the table the mods call through ------------------------------------------------

        int __cdecl ApiIsLevelLoaded(AmodMod mod)
        {
            return (Resolve(mod) != nullptr && IsLevelLoaded()) ? 1 : 0;
        }

        int __cdecl ApiGetLevel(AmodMod mod, AmodWorldLevel* out)
        {
            if (Resolve(mod) == nullptr || out == nullptr || !IsLevelLoaded())
            {
                return 0;
            }

            AmodWorldLevel level = {};
            level.structSize = sizeof(AmodWorldLevel);

            level.script = ReadText(g_level, GameAddress::kLevelScriptOffset);
            level.name = ReadText(g_level, GameAddress::kLevelNameOffset);
            level.nextLevel = ReadText(g_level, GameAddress::kLevelNextLevelOffset);
            level.background = ReadText(g_level, GameAddress::kLevelBackgroundOffset);
            level.music = ReadText(g_level, GameAddress::kLevelMusicOffset);
            level.startTitle = ReadText(g_level, GameAddress::kLevelStartTitleOffset);
            level.endTitle = ReadText(g_level, GameAddress::kLevelEndTitleOffset);
            level.menuStat = ReadText(g_level, GameAddress::kLevelMenuStatOffset);
            level.castle = ReadText(g_level, GameAddress::kLevelEnableCastleOffset);

            level.number = Read<int>(g_level, GameAddress::kLevelNumberOffset);
            level.time = Read<float>(g_level, GameAddress::kLevelTimeOffset);
            level.musicStart = Read<float>(g_level, GameAddress::kLevelMusicStartOffset);

            level.isBonus = Read<uint8_t>(g_level, GameAddress::kLevelBonusOffset) != 0 ? 1 : 0;
            level.bonusTime = Read<float>(g_level, GameAddress::kLevelBonusTimeOffset);
            level.bonusCount = Read<int>(g_level, GameAddress::kLevelBonusCountOffset);

            level.isDemoLevel = Read<uint8_t>(g_level, GameAddress::kLevelDemoLevelOffset) != 0 ? 1 : 0;
            level.useForDemo = Read<uint8_t>(g_level, GameAddress::kLevelUseForDemoOffset) != 0 ? 1 : 0;

            level.roomCount = RoomCount();
            level.mazeRows = Read<int>(g_level, GameAddress::kLevelMazeHeightOffset);

            level.startMazeX = Read<int>(g_level, GameAddress::kLevelStartRoomXOffset);
            level.startMazeY = Read<int>(g_level, GameAddress::kLevelStartRoomYOffset);
            level.startCellX = Read<float>(g_level, GameAddress::kLevelStartCellOffset);
            level.startCellY = Read<float>(g_level, GameAddress::kLevelStartCellOffset + sizeof(float));

            return CopyOut(out, out->structSize, &level, sizeof(level));
        }

        int __cdecl ApiGetRoomCount(AmodMod mod)
        {
            return Resolve(mod) != nullptr ? RoomCount() : 0;
        }

        int __cdecl ApiGetRoom(AmodMod mod, int index, AmodWorldRoom* out)
        {
            const uint8_t* const entry = RoomEntry(index);
            if (Resolve(mod) == nullptr || out == nullptr || entry == nullptr)
            {
                return 0;
            }

            const uint8_t* const room = entry + GameAddress::kRoomEntryRoomOffset;

            AmodWorldRoom filled = {};
            filled.structSize = sizeof(AmodWorldRoom);
            filled.index = index;
            filled.id = Read<int>(entry, GameAddress::kRoomEntryIdOffset);
            filled.file = ReadText(room, GameAddress::kRoomFileOffset);
            filled.music = ReadText(entry, GameAddress::kRoomEntryMusicOffset);

            FindMazePos(filled.id, filled.mazeX, filled.mazeY);

            filled.isLoaded = Read<uint8_t>(room, GameAddress::kRoomLoadedOffset) != 0 ? 1 : 0;
            filled.hasBackWall = Read<int>(room, GameAddress::kRoomHasBackWallOffset) != 0 ? 1 : 0;
            filled.collisionBoxCount = Read<int>(room, GameAddress::kRoomCollisionBoxCountOffset);
            filled.lightTriangleCount = Read<int>(room, GameAddress::kRoomLightTriangleCountOffset);

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiFindRoomById(AmodMod mod, int id)
        {
            return Resolve(mod) != nullptr ? FindRoomById(id) : -1;
        }

        int __cdecl ApiGetCurrentRoom(AmodMod mod)
        {
            if (Resolve(mod) == nullptr || !IsLevelLoaded())
            {
                return -1;
            }
            return IndexOfEntry(Read<void*>(g_level, GameAddress::kLevelCurrentRoomOffset));
        }

        int __cdecl ApiGetPreviousRoom(AmodMod mod)
        {
            if (Resolve(mod) == nullptr || !IsLevelLoaded())
            {
                return -1;
            }
            return IndexOfEntry(Read<void*>(g_level, GameAddress::kLevelPreviousRoomOffset));
        }

        void __cdecl ApiGetMazeCursor(AmodMod mod, int* x, int* y)
        {
            if (Resolve(mod) == nullptr || !IsLevelLoaded())
            {
                return;
            }

            if (x != nullptr)
            {
                *x = Read<int>(g_level, GameAddress::kLevelMazeXOffset);
            }
            if (y != nullptr)
            {
                *y = Read<int>(g_level, GameAddress::kLevelMazeYOffset);
            }
        }

        int __cdecl ApiGetMazeRowWidth(AmodMod mod, int y)
        {
            return Resolve(mod) != nullptr ? MazeRowWidth(y) : 0;
        }

        int __cdecl ApiGetMazeRoomId(AmodMod mod, int x, int y)
        {
            return Resolve(mod) != nullptr ? MazeRoomId(x, y) : 0;
        }

        // A cell is a record of eight bytes and the type character is the first of them; the
        // rest is what the room's geometry made of that cell and is not read here.
        const uint8_t* Cell(int index, int column, int row)
        {
            const uint8_t* const room = Room(index);
            if (room == nullptr ||
                column < 0 || column >= static_cast<int>(GameAddress::kRoomGridColumns) ||
                row < 0 || row >= static_cast<int>(GameAddress::kRoomGridRows))
            {
                return nullptr;
            }

            const size_t cell = static_cast<size_t>(row) * GameAddress::kRoomGridColumns + column;
            return room + GameAddress::kRoomCellsOffset + cell * GameAddress::kRoomCellStride;
        }

        char __cdecl ApiGetRoomCell(AmodMod mod, int index, int column, int row)
        {
            if (Resolve(mod) == nullptr)
            {
                return '\0';
            }

            const uint8_t* const cell = Cell(index, column, row);
            return cell != nullptr ? static_cast<char>(*cell) : '\0';
        }

        int __cdecl ApiGetRoomCells(AmodMod mod, int index, char* buffer, int capacity)
        {
            const int cells = static_cast<int>(GameAddress::kRoomGridColumns *
                                               GameAddress::kRoomGridRows);
            if (Resolve(mod) == nullptr || buffer == nullptr || capacity < cells ||
                Room(index) == nullptr)
            {
                return 0;
            }

            for (int row = 0; row < static_cast<int>(GameAddress::kRoomGridRows); ++row)
            {
                for (int column = 0; column < static_cast<int>(GameAddress::kRoomGridColumns);
                     ++column)
                {
                    buffer[row * static_cast<int>(GameAddress::kRoomGridColumns) + column] =
                        static_cast<char>(*Cell(index, column, row));
                }
            }

            return cells;
        }

        int __cdecl ApiGetRoomCollisionBox(AmodMod mod, int index, int boxIndex, AmodWorldBox* out)
        {
            const uint8_t* const room = Room(index);
            if (Resolve(mod) == nullptr || out == nullptr || room == nullptr || boxIndex < 0 ||
                boxIndex >= Read<int>(room, GameAddress::kRoomCollisionBoxCountOffset))
            {
                return 0;
            }

            const uint8_t* const boxes =
                reinterpret_cast<const uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomCollisionBoxesOffset));
            if (boxes == nullptr)
            {
                return 0;
            }

            // The record does not hold two corners in order: the near z comes first, then the
            // near x and y, a gap, and then the far x, y and z.
            const uint8_t* const box =
                boxes + static_cast<size_t>(boxIndex) * GameAddress::kRoomCollisionBoxStride;

            out->min[0] = Read<float>(box, GameAddress::kRoomBoxMinXOffset);
            out->min[1] = Read<float>(box, GameAddress::kRoomBoxMinYOffset);
            out->min[2] = Read<float>(box, GameAddress::kRoomBoxMinZOffset);
            out->max[0] = Read<float>(box, GameAddress::kRoomBoxMaxXOffset);
            out->max[1] = Read<float>(box, GameAddress::kRoomBoxMaxYOffset);
            out->max[2] = Read<float>(box, GameAddress::kRoomBoxMaxZOffset);
            return 1;
        }

        AmodToken __cdecl ApiAddEventHandler(AmodMod mod, AmodWorldEvent event,
                                             AmodWorldEventHandler handler, void* user)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || handler == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }

            if (event != AMOD_WORLD_EVENT_LEVEL_LOADED &&
                event != AMOD_WORLD_EVENT_LEVEL_UNLOADED &&
                event != AMOD_WORLD_EVENT_ROOM_CHANGED)
            {
                return AMOD_INVALID_TOKEN;
            }

            Subscription item;
            item.id = g_nextSubscriptionId++;
            item.owner = context;
            item.event = event;
            item.handler = handler;
            item.user = user;
            g_subscriptions.push_back(item);

            return context->owned.Add(&ReleaseSubscription,
                                      reinterpret_cast<void*>(static_cast<uintptr_t>(item.id)));
        }

        void __cdecl ApiRemoveEventHandler(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Resolve(mod);
            if (context != nullptr)
            {
                context->owned.Remove(token);
            }
        }

        // Installs one detour, refusing when the routine does not open the way the reverse says
        // it does. All three have to hold for the interface to be offered at all: a mod told a
        // level is loaded but never told it changed would be worse than no interface.
        bool InstallHook(InlineHook& hook, GameSymbol symbol, void* detour,
                         const uint8_t* prologue, size_t prologueLength, const char* name)
        {
            void* const target = GameSymbols::Address(symbol);
            if (target == nullptr)
            {
                FORGE_LOG_WARN("%s was not located, so the world interface is not offered", name);
                return false;
            }

            std::string mismatch;
            if (!GameModule::VerifyBytes(target, prologue, prologueLength, mismatch))
            {
                FORGE_LOG_ERROR("%s does not open as expected (%s)", name, mismatch.c_str());
                return false;
            }

            std::string error;
            if (!hook.Install(target, detour, prologue, prologueLength, error))
            {
                FORGE_LOG_ERROR("Cannot hook %s: %s", name, error.c_str());
                return false;
            }

            return true;
        }
    }

    bool WorldApi::Install()
    {
        g_level = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kLevel));
        if (g_level == nullptr)
        {
            FORGE_LOG_WARN("The level was not located, so the world interface is not offered");
            return false;
        }

        if (!InstallHook(g_loadHook, GameSymbol::LevelLoad, reinterpret_cast<void*>(&LoadDetour),
                         kLevelLoadPrologue, sizeof(kLevelLoadPrologue), "Level_Load") ||
            !InstallHook(g_freeHook, GameSymbol::LevelFree, reinterpret_cast<void*>(&FreeDetour),
                         kLevelFreePrologue, sizeof(kLevelFreePrologue), "Level_Free") ||
            !InstallHook(g_activateHook, GameSymbol::LevelActivateRoom,
                         reinterpret_cast<void*>(&ActivateRoomDetour), kLevelActivateRoomPrologue,
                         sizeof(kLevelActivateRoomPrologue), "Level_ActivateRoom"))
        {
            Uninstall();
            return false;
        }

        g_originalLoad = reinterpret_cast<LevelLoadRoutine>(g_loadHook.Trampoline());
        g_originalFree = reinterpret_cast<LevelFreeRoutine>(g_freeHook.Trampoline());
        g_originalActivate =
            reinterpret_cast<LevelActivateRoomRoutine>(g_activateHook.Trampoline());

        g_table.structSize = sizeof(AmodWorldApi);
        g_table.IsLevelLoaded = &ApiIsLevelLoaded;
        g_table.GetLevel = &ApiGetLevel;
        g_table.GetRoomCount = &ApiGetRoomCount;
        g_table.GetRoom = &ApiGetRoom;
        g_table.FindRoomById = &ApiFindRoomById;
        g_table.GetCurrentRoom = &ApiGetCurrentRoom;
        g_table.GetPreviousRoom = &ApiGetPreviousRoom;
        g_table.GetMazeCursor = &ApiGetMazeCursor;
        g_table.GetMazeRowWidth = &ApiGetMazeRowWidth;
        g_table.GetMazeRoomId = &ApiGetMazeRoomId;
        g_table.GetRoomCell = &ApiGetRoomCell;
        g_table.GetRoomCells = &ApiGetRoomCells;
        g_table.GetRoomCollisionBox = &ApiGetRoomCollisionBox;
        g_table.AddEventHandler = &ApiAddEventHandler;
        g_table.RemoveEventHandler = &ApiRemoveEventHandler;

        FORGE_LOG_INFO("World interface installed");
        return true;
    }

    void WorldApi::Uninstall()
    {
        g_activateHook.Uninstall();
        g_freeHook.Uninstall();
        g_loadHook.Uninstall();

        g_originalActivate = nullptr;
        g_originalFree = nullptr;
        g_originalLoad = nullptr;
        g_level = nullptr;

        g_subscriptions.clear();
        g_table = AmodWorldApi{};
    }

    const AmodWorldApi* WorldApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void WorldApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_subscriptions.begin(); it != g_subscriptions.end();)
        {
            it = it->owner == owner ? g_subscriptions.erase(it) : ++it;
        }
    }
}
