#include <windows.h>

#include "src/Api/WorldApi.h"
#include "src/Api/FrameApi.h"
#include "src/Api/ModContext.h"
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
        // All three are __thiscall → __fastcall with EDX unused.
        typedef char(__fastcall* LevelLoadRoutine)(void* level, void* unused, const char* script);
        typedef void(__fastcall* LevelFreeRoutine)(void* level, void* unused);
        typedef void* (__fastcall* LevelActivateRoomRoutine)(void* level, void* unused, int dx, int dy);

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

        // Level is static; rooms > 0 means loaded (Level_Free clears rather than releases).
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

        // Room is embedded in the entry, not pointed at.
        uint8_t* Room(int index)
        {
            uint8_t* const entry = RoomEntry(index);
            return entry != nullptr ? entry + GameAddress::kRoomEntryRoomOffset : nullptr;
        }

        bool Readable(const void* address, size_t size)
        {
            if (address == nullptr)
            {
                return false;
            }
            MEMORY_BASIC_INFORMATION region = {};
            if (VirtualQuery(address, &region, sizeof(region)) != sizeof(region) ||
                region.State != MEM_COMMIT)
            {
                return false;
            }
            const uintptr_t end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
            return reinterpret_cast<uintptr_t>(address) + size <= end;
        }

        // One global entity manager; live list is the current room only.
        uint8_t* Entities(int roomIndex)
        {
            const uint8_t* const entry = RoomEntry(roomIndex);
            const void* const current =
                IsLevelLoaded() ? Read<void*>(g_level, GameAddress::kLevelCurrentRoomOffset) : nullptr;
            if (entry == nullptr || entry != current)
            {
                return nullptr;
            }
            uint8_t* const container = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kEntities));
            return Readable(container, GameAddress::kEntitiesTimeScaleOffset + sizeof(float))
                ? container
                : nullptr;
        }

        uint8_t* FirstEntity(int roomIndex)
        {
            uint8_t* const container = Entities(roomIndex);
            return container != nullptr
                ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(container, GameAddress::kEntitiesLiveOffset))
                : nullptr;
        }

        uint8_t* NextEntity(const uint8_t* entity)
        {
            return reinterpret_cast<uint8_t*>(Read<uintptr_t>(entity, GameAddress::kEntityNextOffset));
        }

        // Cap at pool size so a corrupted ring ends the walk.
        int EntityCount(int roomIndex)
        {
            int count = 0;
            for (uint8_t* entity = FirstEntity(roomIndex);
                entity != nullptr && Readable(entity, GameAddress::kEntitySize) &&
                count < static_cast<int>(GameAddress::kEntityPoolCount);
                entity = NextEntity(entity))
            {
                ++count;
            }
            return count;
        }

        uint8_t* EntityAt(int roomIndex, int index)
        {
            if (index < 0)
            {
                return nullptr;
            }
            int at = 0;
            for (uint8_t* entity = FirstEntity(roomIndex);
                entity != nullptr && Readable(entity, GameAddress::kEntitySize) &&
                at < static_cast<int>(GameAddress::kEntityPoolCount);
                entity = NextEntity(entity), ++at)
            {
                if (at == index)
                {
                    return entity;
                }
            }
            return nullptr;
        }

        // Printable NUL-terminated name of ≥ 2 chars (1-letter hits are noise).
        bool NameAt(const uint8_t* record, uintptr_t offset)
        {
            if (!Readable(record + offset, GameAddress::kSpawnNameLength))
            {
                return false;
            }
            const char* const name = reinterpret_cast<const char*>(record + offset);
            for (size_t at = 0; at < GameAddress::kSpawnNameLength; ++at)
            {
                if (name[at] == '\0')
                {
                    return at >= 2;
                }
                if (static_cast<unsigned char>(name[at]) < 0x20 ||
                    static_cast<unsigned char>(name[at]) > 0x7E)
                {
                    return false;
                }
            }
            return false;
        }

        bool LooksLikeSpawnRecord(const uint8_t* record)
        {
            if (!Readable(record, sizeof(int)))
            {
                return false;
            }
            const int kind = Read<int>(record, GameAddress::kSpawnKindOffset);
            return kind > 0 && kind < 0x100;
        }

        const GameAddress::SpawnLayout* LayoutOf(int kind)
        {
            for (const GameAddress::SpawnLayout& layout : GameAddress::kSpawnLayouts)
            {
                if (layout.kind == kind)
                {
                    return &layout;
                }
            }
            return nullptr;
        }

        const char* SpawnNameOf(const uint8_t* record, const GameAddress::SpawnLayout* layout)
        {
            if (layout != nullptr)
            {
                return NameAt(record, layout->name)
                    ? reinterpret_cast<const char*>(record + layout->name)
                    : "";
            }
            // Byte-at-a-time: bonus name sits on an odd offset.
            for (uintptr_t offset = sizeof(int); offset <= 0x80; ++offset)
            {
                if (NameAt(record, offset))
                {
                    return reinterpret_cast<const char*>(record + offset);
                }
            }
            return "";
        }

        const uint8_t* SpawnRecord(const uint8_t* entity)
        {
            const uint8_t* const record =
                reinterpret_cast<const uint8_t*>(Read<uintptr_t>(entity, GameAddress::kEntitySpawnOffset));
            return LooksLikeSpawnRecord(record) ? record : nullptr;
        }

        const char* SpawnName(const uint8_t* record)
        {
            return SpawnNameOf(record, LayoutOf(Read<int>(record, GameAddress::kSpawnKindOffset)));
        }

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

        // Maze row: { int* roomIds; int width }, rows are ragged.
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

        // Copy min(requested, produced); write actual size back into structSize.
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

            // Snapshot so a handler may unsubscribe without breaking iteration.
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

        char __fastcall LoadDetour(void* level, void* unused, const char* script)
        {
            const char result = g_originalLoad(level, unused, script);
            if (IsLevelLoaded())
            {
                Dispatch(AMOD_WORLD_EVENT_LEVEL_LOADED, -1, 0, -1, -1, -1);
            }
            return result;
        }

        void __fastcall FreeDetour(void* level, void* unused)
        {
            if (IsLevelLoaded())
            {
                Dispatch(AMOD_WORLD_EVENT_LEVEL_UNLOADED, -1, 0, -1, -1, -1);
            }
            g_originalFree(level, unused);
        }

        void* __fastcall ActivateRoomDetour(void* level, void* unused, int dx, int dy)
        {
            void* const room = g_originalActivate(level, unused, dx, dy);
            // Null = step led nowhere; current room unchanged.
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

        int CurrentRoomIndex()
        {
            return RoomCount() > 0
                ? IndexOfEntry(Read<void*>(g_level, GameAddress::kLevelCurrentRoomOffset))
                : -1;
        }

        bool IsPlayer(const uint8_t* entity)
        {
            return entity != nullptr && Readable(entity, GameAddress::kEntitySize) &&
                Read<int>(entity, GameAddress::kEntityKindOffset) == GameAddress::kEntityKindPlayer;
        }

        // Prefer session pointer; fall back to walking the live list.
        const uint8_t* FindPlayer()
        {
            const uint8_t* const session =
                static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kGameSession));
            if (session != nullptr && Readable(session, GameAddress::kSessionPlayerOffset + sizeof(void*)))
            {
                const uint8_t* const named =
                    Read<const uint8_t*>(session, GameAddress::kSessionPlayerOffset);
                if (IsPlayer(named))
                {
                    return named;
                }
            }
            const int current = IsLevelLoaded()
                ? IndexOfEntry(Read<void*>(g_level, GameAddress::kLevelCurrentRoomOffset))
                : -1;
            for (uint8_t* entity = FirstEntity(current);
                entity != nullptr && Readable(entity, GameAddress::kEntitySize);
                entity = NextEntity(entity))
            {
                if (IsPlayer(entity))
                {
                    return entity;
                }
            }
            return nullptr;
        }

        uint8_t* FindSession()
        {
            uint8_t* const session = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kGameSession));
            return (session != nullptr &&
                Readable(session, GameAddress::kSessionScoreOffset + sizeof(int)))
                ? session
                : nullptr;
        }

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

        // Cell record is 8 bytes; type character is the first.
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
            //The same 0x1C as every box in the game: +0x00 says what kind it is, the two corners
            //follow at +0x04 and +0x10. The Z of the minimum used to be read from +0x00, so it
            //came back as that kind seen as a float - which is where the nonsense in it came from.
            const uint8_t* const box =
                boxes + static_cast<size_t>(boxIndex) * GameAddress::kRoomCollisionBoxStride;
            out->min[0] = Read<float>(box, GameAddress::kBoxMinXOffset);
            out->min[1] = Read<float>(box, GameAddress::kBoxMinYOffset);
            out->min[2] = Read<float>(box, GameAddress::kBoxMinZOffset);
            out->max[0] = Read<float>(box, GameAddress::kBoxMaxXOffset);
            out->max[1] = Read<float>(box, GameAddress::kBoxMaxYOffset);
            out->max[2] = Read<float>(box, GameAddress::kBoxMaxZOffset);
            return 1;
        }

        int __cdecl ApiGetSpawnCount(AmodMod mod, int roomIndex)
        {
            const uint8_t* const room = Room(roomIndex);
            if (Resolve(mod) == nullptr || room == nullptr)
            {
                return 0;
            }
            const int count = Read<int>(room, GameAddress::kRoomSpawnCountOffset);
            return (count > 0 && count < 0x4000) ? count : 0;
        }

        int __cdecl ApiGetSpawn(AmodMod mod, int roomIndex, int index, AmodWorldEntity* out)
        {
            const uint8_t* const room = Room(roomIndex);
            if (Resolve(mod) == nullptr || out == nullptr || room == nullptr ||
                index < 0 || index >= ApiGetSpawnCount(mod, roomIndex) ||
                out->structSize < sizeof(uint32_t))
            {
                return 0;
            }
            const uint8_t* const* const records = reinterpret_cast<const uint8_t* const*>(
                Read<uintptr_t>(room, GameAddress::kRoomSpawnListOffset));
            if (!Readable(records, sizeof(void*) * static_cast<size_t>(index + 1)))
            {
                return 0;
            }
            const uint8_t* const record = records[index];
            if (!LooksLikeSpawnRecord(record))
            {
                return 0;
            }
            AmodWorldEntity filled = {};
            filled.structSize = sizeof(AmodWorldEntity);
            filled.kind = Read<int>(record, GameAddress::kSpawnKindOffset);
            const GameAddress::SpawnLayout* const layout = LayoutOf(filled.kind);
            filled.name = SpawnNameOf(record, layout);
            filled.isLive = (layout != nullptr && layout->liveByte != 0)
                ? Read<uint8_t>(record, layout->liveByte)
                : 1;
            if (layout != nullptr && Readable(record + layout->position, 3 * sizeof(float)))
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    filled.position[axis] =
                        Read<float>(record, layout->position +
                            static_cast<uintptr_t>(axis) * sizeof(float));
                    filled.previousPosition[axis] = filled.position[axis];
                }
            }
            const uint32_t copied = out->structSize < sizeof(filled)
                ? out->structSize
                : static_cast<uint32_t>(sizeof(filled));
            memcpy(out, &filled, copied);
            memcpy(out, &copied, sizeof(copied));
            return 1;
        }

        int __cdecl ApiGetRoomCellRecord(AmodMod mod, int index, int column, int row,
            unsigned char* out, int capacity)
        {
            const uint8_t* const room = Room(index);
            if (Resolve(mod) == nullptr || room == nullptr || out == nullptr ||
                capacity < static_cast<int>(GameAddress::kRoomCellStride) ||
                column < 0 || column >= static_cast<int>(GameAddress::kRoomGridColumns) ||
                row < 0 || row >= static_cast<int>(GameAddress::kRoomGridRows))
            {
                return 0;
            }
            const size_t cell = (static_cast<size_t>(row) * GameAddress::kRoomGridColumns +
                static_cast<size_t>(column)) * GameAddress::kRoomCellStride;
            memcpy(out, room + GameAddress::kRoomCellsOffset + cell, GameAddress::kRoomCellStride);
            return 1;
        }

        int __cdecl ApiGetEntityCount(AmodMod mod, int roomIndex)
        {
            return Resolve(mod) != nullptr ? EntityCount(roomIndex) : 0;
        }

        int __cdecl ApiGetEntity(AmodMod mod, int roomIndex, int index, AmodWorldEntity* out)
        {
            const uint8_t* const entity = Resolve(mod) != nullptr && out != nullptr
                ? EntityAt(roomIndex, index)
                : nullptr;
            if (entity == nullptr || out->structSize < sizeof(uint32_t))
            {
                return 0;
            }
            AmodWorldEntity filled = {};
            filled.structSize = sizeof(AmodWorldEntity);
            const uint8_t* const record = SpawnRecord(entity);
            filled.kind = record != nullptr ? Read<int>(record, GameAddress::kSpawnKindOffset) : 0;
            filled.name = record != nullptr ? SpawnName(record) : "";
            for (int axis = 0; axis < 3; ++axis)
            {
                const uintptr_t offset = static_cast<uintptr_t>(axis) * sizeof(float);
                filled.position[axis] =
                    Read<float>(entity, GameAddress::kEntityPositionOffset + offset);
                filled.previousPosition[axis] =
                    Read<float>(entity, GameAddress::kEntityPreviousPositionOffset + offset);
            }
            filled.state = Read<int>(entity, GameAddress::kEntityStateOffset);
            filled.subState = Read<int>(entity, GameAddress::kEntitySubStateOffset);
            filled.flags = Read<uint32_t>(entity, GameAddress::kEntityFlagsOffset);
            filled.isDead = Read<uint8_t>(entity, GameAddress::kEntityDeadOffset) != 0 ? 1 : 0;
            const uint32_t copied = out->structSize < sizeof(filled)
                ? out->structSize
                : static_cast<uint32_t>(sizeof(filled));
            memcpy(out, &filled, copied);
            memcpy(out, &copied, sizeof(copied));
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

        // All three hooks must succeed or the interface is not offered.
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
        g_table.GetEntityCount = &ApiGetEntityCount;
        g_table.GetEntity = &ApiGetEntity;
        g_table.GetRoomCellRecord = &ApiGetRoomCellRecord;
        g_table.GetSpawnCount = &ApiGetSpawnCount;
        g_table.GetSpawn = &ApiGetSpawn;

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

    bool WorldApi::IsLevelLoaded()
    {
        return RoomCount() > 0;
    }

    uint8_t* WorldApi::CurrentRoom()
    {
        return Room(CurrentRoomIndex());
    }

    int WorldApi::RoomTotal()
    {
        return RoomCount();
    }

    uint8_t* WorldApi::RoomAt(int index)
    {
        return Room(index);
    }

    uint8_t* WorldApi::FirstLiveEntity()
    {
        return FirstEntity(CurrentRoomIndex());
    }

    uint8_t* WorldApi::NextLiveEntity(const uint8_t* entity)
    {
        return NextEntity(entity);
    }

    uint8_t* WorldApi::LiveEntityAt(int roomIndex, int index)
    {
        return EntityAt(roomIndex < 0 ? CurrentRoomIndex() : roomIndex, index);
    }

    const uint8_t* WorldApi::Player()
    {
        return FindPlayer();
    }

    uint8_t* WorldApi::Session()
    {
        return FindSession();
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