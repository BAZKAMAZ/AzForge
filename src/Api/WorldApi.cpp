#include <windows.h>

#include "src/Api/WorldApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/FrameApi.h"
#include "src/Api/MmdApi.h"
#include "src/Api/ModContext.h"
#include "src/Api/TextureApi.h"
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
        typedef char(__fastcall* LevelLoadRoutine)(void* level, void* unused, const char* script);
        typedef void(__fastcall* LevelFreeRoutine)(void* level, void* unused);
        typedef void* (__fastcall* LevelActivateRoomRoutine)(void* level, void* unused, int dx, int dy);
        typedef void(__fastcall* SceneSortObjectsRoutine)(void* scene, void* unused);
        typedef void(__fastcall* SceneAddLightRoutine)(void* scene, void* unused, const float* position, const float* colour, float range);

        InlineHook g_loadHook;
        InlineHook g_freeHook;
        InlineHook g_activateHook;
        InlineHook g_sortHook;
        LevelLoadRoutine g_originalLoad = nullptr;
        LevelFreeRoutine g_originalFree = nullptr;
        LevelActivateRoomRoutine g_originalActivate = nullptr;
        SceneSortObjectsRoutine g_originalSort = nullptr;
        SceneAddLightRoutine g_addLight = nullptr;

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
            const void* const current = IsLevelLoaded() ? Read<void*>(g_level, GameAddress::kLevelCurrentRoomOffset) : nullptr;
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

        bool LooksLikeSpawnRecord(const uint8_t* record)
        {
            if (!Readable(record, sizeof(int)))
            {
                return false;
            }
            const int kind = Read<int>(record, GameAddress::kSpawnKindOffset);
            return kind > 0 && kind < 0x100;
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
            const uint8_t* const record = reinterpret_cast<const uint8_t*>(Read<uintptr_t>(entity, GameAddress::kEntitySpawnOffset));
            return LooksLikeSpawnRecord(record) &&
                LayoutOf(Read<int>(record, GameAddress::kSpawnKindOffset)) != nullptr
                ? record
                : nullptr;
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
            const uint8_t* const maze = reinterpret_cast<const uint8_t*>(Read<uintptr_t>(g_level, GameAddress::kLevelMazeOffset));
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
            const int* const ids = reinterpret_cast<const int*>(Read<uintptr_t>(row, GameAddress::kMazeRowIdsOffset));
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
            const int previous = IndexOfEntry(Read<void*>(g_level, GameAddress::kLevelPreviousRoomOffset));
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
            const uint8_t* const session = static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kGameSession));
            if (session != nullptr && Readable(session, GameAddress::kSessionPlayerOffset + sizeof(void*)))
            {
                const uint8_t* const named = Read<const uint8_t*>(session, GameAddress::kSessionPlayerOffset);
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
            const uint8_t* const boxes = reinterpret_cast<const uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomCollisionBoxesOffset));
            if (boxes == nullptr)
            {
                return 0;
            }
            const uint8_t* const box = boxes + static_cast<size_t>(boxIndex) * GameAddress::kRoomCollisionBoxStride;
            out->min[0] = Read<float>(box, GameAddress::kBoxMinXOffset);
            out->min[1] = Read<float>(box, GameAddress::kBoxMinYOffset);
            out->min[2] = Read<float>(box, GameAddress::kBoxMinZOffset);
            out->max[0] = Read<float>(box, GameAddress::kBoxMaxXOffset);
            out->max[1] = Read<float>(box, GameAddress::kBoxMaxYOffset);
            out->max[2] = Read<float>(box, GameAddress::kBoxMaxZOffset);
            return 1;
        }

        int RoomOrCurrent(int roomIndex)
        {
            return roomIndex < 0 ? CurrentRoomIndex() : roomIndex;
        }

        // Persistent room lights.
        uint8_t* LightAt(int roomIndex, int index)
        {
            const uint8_t* const room = Room(RoomOrCurrent(roomIndex));
            if (room == nullptr || index < 0 ||
                index >= Read<int>(room, GameAddress::kRoomLightCountOffset))
            {
                return nullptr;
            }

            uint8_t* const lights = reinterpret_cast<uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomLightsOffset));

            return lights != nullptr && Readable(lights, static_cast<size_t>(index + 1) * GameAddress::kRoomLightStride)
                ? lights + static_cast<size_t>(index) * GameAddress::kRoomLightStride
                : nullptr;
        }

        uint8_t* SceneLightAt(int roomIndex, int index)
        {
            const uint8_t* const room = Room(RoomOrCurrent(roomIndex));
            if (room != nullptr &&
                Read<int>(room, GameAddress::kRoomLightCountOffset) > static_cast<int>(GameAddress::kSceneLightCapacity))
            {
                return nullptr;
            }

            uint8_t* const scene = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kScene));
            if (scene == nullptr || RoomOrCurrent(roomIndex) != CurrentRoomIndex() || index < 0 ||
                index >= static_cast<int>(GameAddress::kSceneLightCapacity) ||
                !Readable(scene + GameAddress::kSceneLightCountOffset, sizeof(int)) ||
                index >= Read<int>(scene, GameAddress::kSceneLightCountOffset))
            {
                return nullptr;
            }
            return scene + GameAddress::kSceneLightsOffset + static_cast<size_t>(index) * GameAddress::kRoomLightStride;
        }

        struct LightSnapshot
        {
            ModContext* owner;
            int roomIndex;
            int index;
            bool added;         // the mod's own: put out rather than put back
            uint8_t bytes[GameAddress::kRoomLightStride];
        };

        std::vector<LightSnapshot> g_lightSnapshots;

        void KeepLight(ModContext* owner, int roomIndex, int index, const uint8_t* light)
        {
            for (const LightSnapshot& kept : g_lightSnapshots)
            {
                if (kept.roomIndex == roomIndex && kept.index == index)
                {
                    return;
                }
            }

            LightSnapshot kept;
            kept.owner = owner;
            kept.roomIndex = roomIndex;
            kept.index = index;
            kept.added = false;
            memcpy(kept.bytes, light, sizeof(kept.bytes));
            g_lightSnapshots.push_back(kept);
        }

        void ReadLight(const uint8_t* light, AmodWorldLight& out)
        {
            memcpy(out.colour, light + GameAddress::kLightColourOffset, sizeof(out.colour));
            out.alpha = Read<float>(light, GameAddress::kLightAlphaOffset);
            out.range = Read<float>(light, GameAddress::kLightRangeOffset);
            memcpy(out.position, light + GameAddress::kLightPositionOffset, sizeof(out.position));
        }

        void WriteLight(uint8_t* light, const AmodWorldLight& from)
        {
            memcpy(light + GameAddress::kLightColourOffset, from.colour, sizeof(from.colour));
            memcpy(light + GameAddress::kLightAlphaOffset, &from.alpha, sizeof(float));
            memcpy(light + GameAddress::kLightRangeOffset, &from.range, sizeof(float));
            memcpy(light + GameAddress::kLightPositionOffset, from.position, sizeof(from.position));
        }

        int __cdecl ApiGetRoomLightCount(AmodMod mod, int roomIndex)
        {
            const uint8_t* const room = ApiRead::Caller(mod) != nullptr ? Room(RoomOrCurrent(roomIndex)) : nullptr;
            const int count = room != nullptr ? Read<int>(room, GameAddress::kRoomLightCountOffset) : 0;
            return (count > 0 && count < 0x1000) ? count : 0;
        }

        int __cdecl ApiGetRoomLight(AmodMod mod, int roomIndex, int index, AmodWorldLight* out)
        {
            const uint8_t* const light = ApiRead::Caller(mod) != nullptr ? LightAt(roomIndex, index) : nullptr;
            if (light == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no light %d", roomIndex, index);
            }
            if (out == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetRoomLight was given nowhere to put it");
            }

            AmodWorldLight filled = {};
            filled.structSize = sizeof(filled);
            ReadLight(light, filled);
            return ApiRead::CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetRoomLight(AmodMod mod, int roomIndex, int index, const AmodWorldLight* light)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            uint8_t* const target = caller != nullptr ? LightAt(roomIndex, index) : nullptr;

            if (target == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no light %d", roomIndex, index);
            }
            if (light == nullptr || light->structSize < sizeof(AmodWorldLight))
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetRoomLight was given no light to write");
            }

            KeepLight(caller, roomIndex, index, target);
            WriteLight(target, *light);

            uint8_t* const showing = SceneLightAt(roomIndex, index);
            if (showing != nullptr)
            {
                WriteLight(showing, *light);
            }
            return 1;
        }

        struct PendingLight
        {
            ModContext* owner;
            float position[3];
            float colour[3];
            float range;
        };

        std::vector<PendingLight> g_pending;

        void SubmitPendingLights()
        {
            uint8_t* const scene = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kScene));

            if (scene == nullptr || g_addLight == nullptr ||
                Read<uintptr_t>(scene, GameAddress::kSceneRoomOffset) == 0 ||
                Read<uintptr_t>(scene, GameAddress::kSceneLightVerticesOffset) == 0)
            {
                g_pending.clear();
                return;
            }

            for (const PendingLight& light : g_pending)
            {
                g_addLight(scene, nullptr, light.position, light.colour, light.range);
            }
            g_pending.clear();
        }

        void RefillSceneLights()
        {
            uint8_t* const scene = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kScene));
            const uint8_t* const room = scene != nullptr ? Room(CurrentRoomIndex()) : nullptr;
            const int count = room != nullptr ? Read<int>(room, GameAddress::kRoomLightCountOffset) : 0;

            // Scene light slots full; only replace a weaker one.
            if (count <= static_cast<int>(GameAddress::kSceneLightCapacity) || count > 0x1000)
            {
                return;
            }

            const uint8_t* const lights = reinterpret_cast<const uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomLightsOffset));

            if (lights == nullptr ||
                !Readable(lights, static_cast<size_t>(count) * GameAddress::kRoomLightStride))
            {
                return;
            }

            const uint8_t* const player = FindPlayer();
            const float atX = player != nullptr ? Read<float>(player, GameAddress::kEntityPositionOffset) : 0.0f;
            const float atY = player != nullptr ? Read<float>(player, GameAddress::kEntityPositionOffset + 4) : 0.0f;

            // Distance to light reach; <0 means player is inside.
            int chosen[GameAddress::kSceneLightCapacity] = {};
            float reach[GameAddress::kSceneLightCapacity] = {};
            int taken = 0;

            for (int at = 0; at < count; ++at)
            {
                const uint8_t* const light = lights + static_cast<size_t>(at) * GameAddress::kRoomLightStride;
                const float dx = Read<float>(light, GameAddress::kLightPositionOffset) - atX;
                const float dy = Read<float>(light, GameAddress::kLightPositionOffset + 4) - atY;
                const float away = sqrtf(dx * dx + dy * dy) - Read<float>(light, GameAddress::kLightRangeOffset);

                int put = taken;
                if (taken == static_cast<int>(GameAddress::kSceneLightCapacity))
                {
                    // Full list: keep only if stronger than the weakest.
                    int worst = 0;
                    for (int seen = 1; seen < taken; ++seen)
                    {
                        if (reach[seen] > reach[worst])
                        {
                            worst = seen;
                        }
                    }
                    if (reach[worst] <= away)
                    {
                        continue;
                    }
                    put = worst;
                }
                else
                {
                    ++taken;
                }

                chosen[put] = at;
                reach[put] = away;
            }

            for (int at = 0; at < taken; ++at)
            {
                memcpy(scene + GameAddress::kSceneLightsOffset + static_cast<size_t>(at) * GameAddress::kRoomLightStride, lights + 
                    static_cast<size_t>(chosen[at]) * GameAddress::kRoomLightStride, GameAddress::kRoomLightStride);
            }
            memcpy(scene + GameAddress::kSceneLightCountOffset, &taken, sizeof(taken));
        }

        void __fastcall SortObjectsDetour(void* scene, void* unused)
        {
            g_originalSort(scene, unused);
            RefillSceneLights();
            SubmitPendingLights();
        }

        int __cdecl ApiAddLight(AmodMod mod, const AmodWorldLight* light)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            if (caller == nullptr)
            {
                return 0;
            }
            if (light == nullptr || light->structSize < sizeof(AmodWorldLight))
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "AddLight was given no light to put up");
            }
            if (g_pending.size() >= GameAddress::kSceneDynamicLightCapacity)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_REFUSED, "the frame holds %d lights of this kind "
                    "and that many are already asked for", static_cast<int>(GameAddress::kSceneDynamicLightCapacity));
            }

            PendingLight put = {};
            put.owner = caller;
            memcpy(put.position, light->position, sizeof(put.position));
            memcpy(put.colour, light->colour, sizeof(put.colour));
            put.range = light->range;
            g_pending.push_back(put);
            return 1;
        }

        // Entity-attached light (not frame-queued).
        struct EntityLightSnapshot
        {
            ModContext* owner;
            int roomIndex;
            int index;
            uint32_t flags;
            uint8_t bytes[GameAddress::kEntityLightBlockSize];
        };

        std::vector<EntityLightSnapshot> g_entityLights;

        void KeepEntityLight(ModContext* owner, int roomIndex, int index, const uint8_t* entity)
        {
            for (const EntityLightSnapshot& kept : g_entityLights)
            {
                if (kept.roomIndex == roomIndex && kept.index == index)
                {
                    return;
                }
            }

            EntityLightSnapshot kept;
            kept.owner = owner;
            kept.roomIndex = roomIndex;
            kept.index = index;
            kept.flags = Read<uint32_t>(entity, GameAddress::kEntityFlagsOffset);
            memcpy(kept.bytes, entity + GameAddress::kEntityLightOffsetOffset, sizeof(kept.bytes));
            g_entityLights.push_back(kept);
        }

        uint8_t* LitEntityAt(int roomIndex, int index)
        {
            uint8_t* const entity = EntityAt(RoomOrCurrent(roomIndex), index);
            return (entity != nullptr &&
                Readable(entity + GameAddress::kEntityLightOffsetOffset,
                    GameAddress::kEntityLightBlockSize))
                ? entity
                : nullptr;
        }

        int __cdecl ApiGetPlayerEntity(AmodMod mod)
        {
            const uint8_t* const player = ApiRead::Caller(mod) != nullptr ? FindPlayer() : nullptr;
            if (player == nullptr)
            {
                return -1;
            }

            int at = 0;
            for (uint8_t* entity = FirstEntity(CurrentRoomIndex());
                entity != nullptr && Readable(entity, GameAddress::kEntitySize) &&
                at < static_cast<int>(GameAddress::kEntityPoolCount);
                entity = NextEntity(entity), ++at)
            {
                if (entity == player)
                {
                    return at;
                }
            }
            return -1;
        }

        AmodTextureId __cdecl ApiGetRoomLightmap(AmodMod mod, int roomIndex)
        {
            const uint8_t* const room = ApiRead::Caller(mod) != nullptr ? Room(RoomOrCurrent(roomIndex)) : nullptr;
            if (room == nullptr)
            {
                return AMOD_INVALID_TEXTURE;
            }

            const AmodTextureId lightmap = TextureApi::IdFor(
                reinterpret_cast<const void*>(Read<uintptr_t>(room, GameAddress::kRoomLightmapTextureOffset)));

            if (lightmap == AMOD_INVALID_TEXTURE)
            {
                ApiRead::Fail(mod, AMOD_FAIL_NOT_HERE, "room %d carries no lightmap", roomIndex);
            }
            return lightmap;
        }

        // Room static decoration pieces.

        typedef void(__fastcall* RoomAddStaticRoutine)(void* room, void* unused, const void* record,
            char translucent, char lit);

        constexpr int kStaticWalkLimit = 4096;

        uint8_t* FirstStatic(int roomIndex)
        {
            const uint8_t* const room = Room(RoomOrCurrent(roomIndex));
            return room != nullptr
                ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomStaticsOffset))
                : nullptr;
        }

        uint8_t* NextStatic(const uint8_t* piece)
        {
            return reinterpret_cast<uint8_t*>(Read<uintptr_t>(piece, GameAddress::kRoomStaticNextOffset));
        }

        bool StaticReadable(const uint8_t* piece)
        {
            return piece != nullptr && Readable(piece, GameAddress::kRoomStaticSize);
        }

        int StaticCount(int roomIndex)
        {
            int count = 0;
            for (uint8_t* piece = FirstStatic(roomIndex);
                StaticReadable(piece) && count < kStaticWalkLimit;
                piece = NextStatic(piece))
            {
                ++count;
            }
            return count;
        }

        uint8_t* StaticAt(int roomIndex, int index)
        {
            if (index < 0)
            {
                return nullptr;
            }
            int at = 0;
            for (uint8_t* piece = FirstStatic(roomIndex);
                StaticReadable(piece) && at < kStaticWalkLimit;
                piece = NextStatic(piece), ++at)
            {
                if (at == index)
                {
                    return piece;
                }
            }
            return nullptr;
        }

        struct StaticSnapshot
        {
            ModContext* owner;
            int roomIndex;
            int index;
            bool added;         // the mod's own, which is hidden rather than put back
            uint8_t bytes[GameAddress::kRoomStaticSize];
        };

        std::vector<StaticSnapshot> g_statics;

        void KeepStatic(ModContext* owner, int roomIndex, int index, const uint8_t* piece)
        {
            for (const StaticSnapshot& kept : g_statics)
            {
                if (kept.roomIndex == roomIndex && kept.index == index)
                {
                    return;
                }
            }

            StaticSnapshot kept;
            kept.owner = owner;
            kept.roomIndex = roomIndex;
            kept.index = index;
            kept.added = false;
            memcpy(kept.bytes, piece, sizeof(kept.bytes));
            g_statics.push_back(kept);
        }

        void ReadStatic(const uint8_t* piece, AmodRoomStatic& out)
        {
            const uint32_t flags = Read<uint32_t>(piece, GameAddress::kRoomStaticFlagsOffset);

            out.isShown = (flags & GameAddress::kRoomStaticHiddenFlag) == 0 ? 1 : 0;
            out.isLit = (flags & GameAddress::kRoomStaticLitFlag) != 0 ? 1 : 0;
            out.isTranslucent = (flags & GameAddress::kRoomStaticTranslucentFlag) != 0 ? 1 : 0;
            out.blend = Read<uint8_t>(piece, GameAddress::kRoomStaticBlendOffset);
            out.model = MmdApi::IdFor(reinterpret_cast<void*>(Read<uintptr_t>(piece, GameAddress::kRoomStaticModelOffset)));
            out.texture = TextureApi::IdFor(reinterpret_cast<const void*>(Read<uintptr_t>(piece, GameAddress::kRoomStaticTextureOffset)));
            memcpy(out.matrix, piece + GameAddress::kRoomStaticMatrixOffset, sizeof(out.matrix));
        }

        void WriteStatic(uint8_t* piece, const AmodRoomStatic& from)
        {
            uint32_t flags = 0;
            if (from.isShown == 0) { flags |= GameAddress::kRoomStaticHiddenFlag; }
            if (from.isLit != 0) { flags |= GameAddress::kRoomStaticLitFlag; }
            if (from.isTranslucent != 0) { flags |= GameAddress::kRoomStaticTranslucentFlag; }

            const uint8_t blend = static_cast<uint8_t>(from.blend);
            memcpy(piece + GameAddress::kRoomStaticFlagsOffset, &flags, sizeof(flags));
            memcpy(piece + GameAddress::kRoomStaticBlendOffset, &blend, sizeof(blend));
            memcpy(piece + GameAddress::kRoomStaticMatrixOffset, from.matrix, sizeof(from.matrix));

            void* const model = MmdApi::ModelFor(from.model);
            void* const texture = TextureApi::TextureFor(from.texture);

            if (model != nullptr)
            {
                memcpy(piece + GameAddress::kRoomStaticModelOffset, &model, sizeof(model));
            }
            if (texture != nullptr)
            {
                memcpy(piece + GameAddress::kRoomStaticTextureOffset, &texture, sizeof(texture));
            }
        }

        int __cdecl ApiGetStaticCount(AmodMod mod, int roomIndex)
        {
            return ApiRead::Caller(mod) != nullptr ? StaticCount(roomIndex) : 0;
        }

        int __cdecl ApiGetStatic(AmodMod mod, int roomIndex, int index, AmodRoomStatic* out)
        {
            const uint8_t* const piece = ApiRead::Caller(mod) != nullptr ? StaticAt(roomIndex, index) : nullptr;
            if (piece == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no static %d", roomIndex, index);
            }
            if (out == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetStatic was given nowhere to put it");
            }

            AmodRoomStatic filled = {};
            filled.structSize = sizeof(filled);
            ReadStatic(piece, filled);
            return ApiRead::CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetStatic(AmodMod mod, int roomIndex, int index, const AmodRoomStatic* what)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            uint8_t* const piece = caller != nullptr ? StaticAt(roomIndex, index) : nullptr;

            if (piece == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no static %d", roomIndex, index);
            }
            if (what == nullptr || what->structSize < sizeof(AmodRoomStatic))
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetStatic was given nothing to write");
            }

            KeepStatic(caller, RoomOrCurrent(roomIndex), index, piece);
            WriteStatic(piece, *what);
            return 1;
        }

        int __cdecl ApiAddStatic(AmodMod mod, int roomIndex, const AmodRoomStatic* what)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            uint8_t* const room = caller != nullptr ? Room(RoomOrCurrent(roomIndex)) : nullptr;
            const RoomAddStaticRoutine add = reinterpret_cast<RoomAddStaticRoutine>(GameModule::Resolve(GameAddress::kRoomAddStatic));

            if (room == nullptr || add == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NOT_HERE, "there is no room %d to put a static in", roomIndex);
            }
            if (what == nullptr || what->structSize < sizeof(AmodRoomStatic) ||
                MmdApi::ModelFor(what->model) == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a static is a model standing somewhere, and no model was named");
            }

            uint8_t record[GameAddress::kRoomStaticSize] = {};
            WriteStatic(record, *what);
            memset(record + GameAddress::kRoomStaticNextOffset, 0, sizeof(uintptr_t));

            add(room, nullptr, record, what->isTranslucent != 0 ? 1 : 0, what->isLit != 0 ? 1 : 0);

            uint8_t* const put = FirstStatic(roomIndex);     // prepended, so it is the head
            if (put == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_REFUSED, "the room would not take another static");
            }

            // Hidden flag set after Room_AddStatic (API cannot pass it).
            if (what->isShown == 0)
            {
                uint32_t flags = Read<uint32_t>(put, GameAddress::kRoomStaticFlagsOffset);
                flags |= GameAddress::kRoomStaticHiddenFlag;
                memcpy(put + GameAddress::kRoomStaticFlagsOffset, &flags, sizeof(flags));
            }

            // Everything already there moved down one, so what was kept about them moves too.
            const int where = RoomOrCurrent(roomIndex);
            for (StaticSnapshot& kept : g_statics)
            {
                if (kept.roomIndex == where)
                {
                    ++kept.index;
                }
            }

            StaticSnapshot mine;
            mine.owner = caller;
            mine.roomIndex = where;
            mine.index = 0;
            mine.added = true;
            memcpy(mine.bytes, put, sizeof(mine.bytes));
            g_statics.push_back(mine);
            return 0;
        }

        int __cdecl ApiGetEntityLight(AmodMod mod, int roomIndex, int index, AmodEntityLight* out)
        {
            const uint8_t* const entity = ApiRead::Caller(mod) != nullptr ? LitEntityAt(roomIndex, index) : nullptr;
            if (entity == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no entity %d", roomIndex, index);
            }
            if (out == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetEntityLight was given nowhere to put it");
            }

            AmodEntityLight filled = {};
            filled.structSize = sizeof(filled);
            filled.isLit =
                (Read<uint32_t>(entity, GameAddress::kEntityFlagsOffset) & GameAddress::kEntityLightFlag) != 0 ? 1 : 0;
            memcpy(filled.offset, entity + GameAddress::kEntityLightOffsetOffset, sizeof(filled.offset));
            memcpy(filled.colour, entity + GameAddress::kEntityLightColourOffset, sizeof(filled.colour));
            filled.alpha = Read<float>(entity, GameAddress::kEntityLightAlphaOffset);
            filled.range = Read<float>(entity, GameAddress::kEntityLightRangeOffset);
            return ApiRead::CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetEntityLight(AmodMod mod, int roomIndex, int index, const AmodEntityLight* light)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            uint8_t* const entity = caller != nullptr ? LitEntityAt(roomIndex, index) : nullptr;

            if (entity == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no entity %d", roomIndex, index);
            }
            if (light == nullptr || light->structSize < sizeof(AmodEntityLight))
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetEntityLight was given no light to put on");
            }

            KeepEntityLight(caller, RoomOrCurrent(roomIndex), index, entity);

            uint32_t flags = Read<uint32_t>(entity, GameAddress::kEntityFlagsOffset);
            if (light->isLit == 0)
            {
                flags &= ~GameAddress::kEntityLightFlag;
                memcpy(entity + GameAddress::kEntityFlagsOffset, &flags, sizeof(flags));
                return 1;
            }

            memcpy(entity + GameAddress::kEntityLightOffsetOffset, light->offset, sizeof(light->offset));
            memcpy(entity + GameAddress::kEntityLightColourOffset, light->colour, sizeof(light->colour));
            memcpy(entity + GameAddress::kEntityLightAlphaOffset, &light->alpha, sizeof(float));
            memcpy(entity + GameAddress::kEntityLightRangeOffset, &light->range, sizeof(float));

            flags |= GameAddress::kEntityLightFlag;
            memcpy(entity + GameAddress::kEntityFlagsOffset, &flags, sizeof(flags));
            return 1;
        }

        // Grow room light array by one; return new record.
        typedef uint8_t* (__stdcall* RoomLightsAppendRoutine)(void* array, int* count);

        int __cdecl ApiAddRoomLight(AmodMod mod, int roomIndex, const AmodWorldLight* light)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            uint8_t* const room = caller != nullptr ? Room(RoomOrCurrent(roomIndex)) : nullptr;
            const RoomLightsAppendRoutine append = reinterpret_cast<RoomLightsAppendRoutine>(GameModule::Resolve(GameAddress::kRoomLightsAppend));

            if (room == nullptr || append == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NOT_HERE, "there is no room %d to light", roomIndex);
            }
            if (light == nullptr || light->structSize < sizeof(AmodWorldLight))
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "AddRoomLight was given no light to add");
            }

            uint8_t* const put = append(room + GameAddress::kRoomLightsOffset,
                reinterpret_cast<int*>(room + GameAddress::kRoomLightCountOffset));

            if (put == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_REFUSED, "the room would not take another light");
            }

            WriteLight(put, *light);
            const int index = Read<int>(room, GameAddress::kRoomLightCountOffset) - 1;

            uint8_t* const scene = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kScene));
            const bool showing = scene != nullptr && RoomOrCurrent(roomIndex) == CurrentRoomIndex() &&
                Readable(scene + GameAddress::kSceneLightCountOffset, sizeof(int));
            const int shown = showing ? Read<int>(scene, GameAddress::kSceneLightCountOffset) : 0;

            if (showing && shown == index && shown < static_cast<int>(GameAddress::kSceneLightCapacity))
            {
                const int grown = shown + 1;
                WriteLight(scene + GameAddress::kSceneLightsOffset +
                    static_cast<size_t>(shown) * GameAddress::kRoomLightStride, *light);
                memcpy(scene + GameAddress::kSceneLightCountOffset, &grown, sizeof(grown));
            }

            LightSnapshot mine;
            mine.owner = caller;
            mine.roomIndex = RoomOrCurrent(roomIndex);
            mine.index = index;
            mine.added = true;
            memcpy(mine.bytes, put, sizeof(mine.bytes));
            g_lightSnapshots.push_back(mine);
            return index;
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
            const uint8_t* const* const records = reinterpret_cast<const uint8_t* const*>(Read<uintptr_t>(room, GameAddress::kRoomSpawnListOffset));
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

        // Room collision boxes.

        typedef void* (__cdecl* ReallocTrackedRoutine)(void* block, uint32_t size,
            const char* sourceFile, int sourceLine);

        // Collision boxes that stop a body.
        struct BoxSnapshot
        {
            ModContext* owner;
            int roomIndex;
            int count;
            std::vector<uint8_t> bytes;
        };

        std::vector<BoxSnapshot> g_boxes;

        void KeepBoxes(ModContext* owner, int roomIndex, const uint8_t* room)
        {
            for (const BoxSnapshot& kept : g_boxes)
            {
                if (kept.roomIndex == roomIndex && kept.owner == owner)
                {
                    return;
                }
            }

            const int count = Read<int>(room, GameAddress::kRoomCollisionBoxCountOffset);
            const uint8_t* const boxes = reinterpret_cast<const uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomCollisionBoxesOffset));

            BoxSnapshot kept;
            kept.owner = owner;
            kept.roomIndex = roomIndex;
            kept.count = count;

            if (boxes != nullptr && count > 0 &&
                Readable(boxes, static_cast<size_t>(count) * GameAddress::kRoomCollisionBoxStride))
            {
                kept.bytes.assign(boxes, boxes + static_cast<size_t>(count) * GameAddress::kRoomCollisionBoxStride);
            }
            g_boxes.push_back(kept);
        }

        void WriteBox(uint8_t* box, const AmodRoomBox& from)
        {
            const int kind = from.kind;
            memcpy(box + GameAddress::kRoomBoxKindOffset, &kind, sizeof(kind));
            memcpy(box + GameAddress::kBoxMinXOffset, from.min, sizeof(from.min));
            memcpy(box + GameAddress::kBoxMaxXOffset, from.max, sizeof(from.max));
        }

        uint8_t* BoxAt(int roomIndex, int index)
        {
            const uint8_t* const room = Room(RoomOrCurrent(roomIndex));
            if (room == nullptr || index < 0 ||
                index >= Read<int>(room, GameAddress::kRoomCollisionBoxCountOffset))
            {
                return nullptr;
            }

            uint8_t* const boxes = reinterpret_cast<uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomCollisionBoxesOffset));

            return boxes != nullptr &&
                Readable(boxes, static_cast<size_t>(index + 1) * GameAddress::kRoomCollisionBoxStride)
                ? boxes + static_cast<size_t>(index) * GameAddress::kRoomCollisionBoxStride
                : nullptr;
        }

        int __cdecl ApiGetRoomBox(AmodMod mod, int roomIndex, int boxIndex, AmodRoomBox* out)
        {
            const uint8_t* const box = ApiRead::Caller(mod) != nullptr ? BoxAt(roomIndex, boxIndex) : nullptr;
            if (box == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no box %d", roomIndex, boxIndex);
            }
            if (out == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetRoomBox was given nowhere to put it");
            }

            AmodRoomBox filled = {};
            filled.structSize = sizeof(filled);
            filled.kind = Read<int>(box, GameAddress::kRoomBoxKindOffset);
            filled.min[0] = Read<float>(box, GameAddress::kBoxMinXOffset);
            filled.min[1] = Read<float>(box, GameAddress::kBoxMinYOffset);
            filled.min[2] = Read<float>(box, GameAddress::kBoxMinZOffset);
            filled.max[0] = Read<float>(box, GameAddress::kBoxMaxXOffset);
            filled.max[1] = Read<float>(box, GameAddress::kBoxMaxYOffset);
            filled.max[2] = Read<float>(box, GameAddress::kBoxMaxZOffset);
            return ApiRead::CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetRoomBox(AmodMod mod, int roomIndex, int boxIndex, const AmodRoomBox* box)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            uint8_t* const room = caller != nullptr ? Room(RoomOrCurrent(roomIndex)) : nullptr;
            uint8_t* const target = room != nullptr ? BoxAt(roomIndex, boxIndex) : nullptr;

            if (target == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no box %d", roomIndex, boxIndex);
            }
            if (box == nullptr || box->structSize < sizeof(AmodRoomBox))
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetRoomBox was given no box to write");
            }

            KeepBoxes(caller, RoomOrCurrent(roomIndex), room);
            WriteBox(target, *box);
            return 1;
        }

        int __cdecl ApiAddRoomBox(AmodMod mod, int roomIndex, const AmodRoomBox* box)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            uint8_t* const room = caller != nullptr ? Room(RoomOrCurrent(roomIndex)) : nullptr;
            const ReallocTrackedRoutine grow = reinterpret_cast<ReallocTrackedRoutine>(GameSymbols::Address(GameSymbol::MemReallocTracked));

            if (room == nullptr || grow == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NOT_HERE, "there is no room %d to put a box in", roomIndex);
            }
            if (box == nullptr || box->structSize < sizeof(AmodRoomBox))
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "AddRoomBox was given no box to add");
            }

            // Snapshot before grow so ForgetMod can restore.
            KeepBoxes(caller, RoomOrCurrent(roomIndex), room);

            const int count = Read<int>(room, GameAddress::kRoomCollisionBoxCountOffset);
            void* const was = reinterpret_cast<void*>(Read<uintptr_t>(room, GameAddress::kRoomCollisionBoxesOffset));

            if (count < 0 || count > 0x4000 || was == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_REFUSED, "room %d keeps its boxes somewhere this cannot grow", roomIndex);
            }

            // Use room allocator so Room_Free releases it.
            uint8_t* const grown = static_cast<uint8_t*>(grow(was,
                static_cast<uint32_t>((count + 1) * GameAddress::kRoomCollisionBoxStride),
                ".\\Room.cpp", 0x15E));

            if (grown == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_REFUSED, "the room would not take another box");
            }

            WriteBox(grown + static_cast<size_t>(count) * GameAddress::kRoomCollisionBoxStride, *box);

            const int raised = count + 1;
            memcpy(room + GameAddress::kRoomCollisionBoxesOffset, &grown, sizeof(grown));
            memcpy(room + GameAddress::kRoomCollisionBoxCountOffset, &raised, sizeof(raised));
            return count;
        }

        // Light triangles (surfaces a light may fall on).

        struct TriangleSnapshot
        {
            ModContext* owner;
            int roomIndex;
            int count;
            std::vector<uint8_t> bytes;
        };

        std::vector<TriangleSnapshot> g_triangles;

        int TriangleCount(int roomIndex)
        {
            const uint8_t* const room = Room(RoomOrCurrent(roomIndex));
            const int count = room != nullptr
                ? Read<int>(room, GameAddress::kRoomLightTriangleCountOffset)
                : 0;
            return (count > 0 && count < 0x10000) ? count : 0;
        }

        uint8_t* TriangleAt(int roomIndex, int index)
        {
            const uint8_t* const room = Room(RoomOrCurrent(roomIndex));
            if (room == nullptr || index < 0 || index >= TriangleCount(roomIndex))
            {
                return nullptr;
            }

            uint8_t* const triangles = reinterpret_cast<uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomLightTrianglesOffset));

            return triangles != nullptr &&
                Readable(triangles, static_cast<size_t>(index + 1) * GameAddress::kRoomLightTriangleStride)
                ? triangles + static_cast<size_t>(index) * GameAddress::kRoomLightTriangleStride
                : nullptr;
        }

        void KeepTriangles(ModContext* owner, int roomIndex, const uint8_t* room)
        {
            for (const TriangleSnapshot& kept : g_triangles)
            {
                if (kept.roomIndex == roomIndex && kept.owner == owner)
                {
                    return;
                }
            }

            const int count = Read<int>(room, GameAddress::kRoomLightTriangleCountOffset);
            const uint8_t* const triangles = reinterpret_cast<const uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomLightTrianglesOffset));

            TriangleSnapshot kept;
            kept.owner = owner;
            kept.roomIndex = roomIndex;
            kept.count = count;

            if (triangles != nullptr && count > 0 &&
                Readable(triangles, static_cast<size_t>(count) * GameAddress::kRoomLightTriangleStride))
            {
                kept.bytes.assign(triangles,
                    triangles + static_cast<size_t>(count) * GameAddress::kRoomLightTriangleStride);
            }
            g_triangles.push_back(kept);
        }

        int __cdecl ApiGetLightTriangleCount(AmodMod mod, int roomIndex)
        {
            return ApiRead::Caller(mod) != nullptr ? TriangleCount(roomIndex) : 0;
        }

        int __cdecl ApiGetLightTriangle(AmodMod mod, int roomIndex, int index, AmodRoomTriangle* out)
        {
            const uint8_t* const triangle = ApiRead::Caller(mod) != nullptr ? TriangleAt(roomIndex, index) : nullptr;
            if (triangle == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no light triangle %d", roomIndex, index);
            }
            if (out == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetLightTriangle was given nowhere to put it");
            }

            AmodRoomTriangle filled = {};
            filled.structSize = sizeof(filled);
            memcpy(filled.points, triangle, sizeof(filled.points));
            return ApiRead::CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetLightTriangle(AmodMod mod, int roomIndex, int index, const AmodRoomTriangle* triangle)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            uint8_t* const room = caller != nullptr ? Room(RoomOrCurrent(roomIndex)) : nullptr;
            uint8_t* const target = room != nullptr ? TriangleAt(roomIndex, index) : nullptr;

            if (target == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no light triangle %d", roomIndex, index);
            }
            if (triangle == nullptr || triangle->structSize < sizeof(AmodRoomTriangle))
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetLightTriangle was given nothing to write");
            }

            KeepTriangles(caller, RoomOrCurrent(roomIndex), room);
            memcpy(target, triangle->points, sizeof(triangle->points));
            return 1;
        }

        int __cdecl ApiAddLightTriangle(AmodMod mod, int roomIndex, const AmodRoomTriangle* triangle)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            uint8_t* const room = caller != nullptr ? Room(RoomOrCurrent(roomIndex)) : nullptr;
            const ReallocTrackedRoutine grow = reinterpret_cast<ReallocTrackedRoutine>(GameSymbols::Address(GameSymbol::MemReallocTracked));

            if (room == nullptr || grow == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NOT_HERE, "there is no room %d to light", roomIndex);
            }
            if (triangle == nullptr || triangle->structSize < sizeof(AmodRoomTriangle))
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "AddLightTriangle was given nothing to add");
            }

            KeepTriangles(caller, RoomOrCurrent(roomIndex), room);

            const int count = Read<int>(room, GameAddress::kRoomLightTriangleCountOffset);
            void* const was = reinterpret_cast<void*>(Read<uintptr_t>(room, GameAddress::kRoomLightTrianglesOffset));

            if (count < 0 || count > 0x4000 || was == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_REFUSED, "room %d keeps its light triangles somewhere this cannot grow", roomIndex);
            }

            uint8_t* const grown = static_cast<uint8_t*>(grow(was,
                static_cast<uint32_t>((count + 1) * GameAddress::kRoomLightTriangleStride),
                ".\\Room.cpp", 0x15E));

            if (grown == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_REFUSED, "the room would not take another light triangle");
            }

            memcpy(grown + static_cast<size_t>(count) * GameAddress::kRoomLightTriangleStride, triangle->points, sizeof(triangle->points));

            const int raised = count + 1;
            memcpy(room + GameAddress::kRoomLightTrianglesOffset, &grown, sizeof(grown));
            memcpy(room + GameAddress::kRoomLightTriangleCountOffset, &raised, sizeof(raised));
            return count;
        }

        // SetRoomCell: write type only; leave flags/padding.
        struct GridSnapshot
        {
            ModContext* owner;
            int roomIndex;
            std::vector<uint8_t> bytes;
        };

        std::vector<GridSnapshot> g_grids;

        void KeepGrid(ModContext* owner, int roomIndex, const uint8_t* room)
        {
            for (const GridSnapshot& kept : g_grids)
            {
                if (kept.roomIndex == roomIndex && kept.owner == owner)
                {
                    return;
                }
            }

            GridSnapshot kept;
            kept.owner = owner;
            kept.roomIndex = roomIndex;
            kept.bytes.assign(room + GameAddress::kRoomCellsOffset,
                room + GameAddress::kRoomCellsOffset + GameAddress::kRoomGridSize);
            g_grids.push_back(kept);
        }

        int __cdecl ApiSetRoomCell(AmodMod mod, int roomIndex, int column, int row, char type)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            uint8_t* const room = caller != nullptr ? Room(RoomOrCurrent(roomIndex)) : nullptr;

            if (room == nullptr ||
                column < 0 || column >= static_cast<int>(GameAddress::kRoomGridColumns) ||
                row < 0 || row >= static_cast<int>(GameAddress::kRoomGridRows))
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NO_SUCH, "room %d has no cell %d,%d", roomIndex, column, row);
            }
            if (type == '\0')
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a cell is a character; ' ' is the empty one");
            }

            KeepGrid(caller, RoomOrCurrent(roomIndex), room);

            const size_t at = (static_cast<size_t>(row) * GameAddress::kRoomGridColumns +
                static_cast<size_t>(column)) * GameAddress::kRoomCellStride;
            room[GameAddress::kRoomCellsOffset + at + GameAddress::kRoomCellTypeOffset] =
                static_cast<uint8_t>(type);
            return 1;
        }

        int __cdecl ApiSetRoomCells(AmodMod mod, int roomIndex, const char* cells, int count)
        {
            ModContext* const caller = ApiRead::Caller(mod);
            uint8_t* const room = caller != nullptr ? Room(RoomOrCurrent(roomIndex)) : nullptr;
            const int wanted = static_cast<int>(GameAddress::kRoomGridColumns * GameAddress::kRoomGridRows);

            if (room == nullptr)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_NOT_HERE, "there is no room %d to write", roomIndex);
            }
            if (cells == nullptr || count != wanted)
            {
                return ApiRead::Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a room grid is %d cells and %d were given",
                    wanted, count);
            }

            KeepGrid(caller, RoomOrCurrent(roomIndex), room);

            for (int at = 0; at < wanted; ++at)
            {
                if (cells[at] == '\0')
                {
                    continue;       // left as it stands, so a mod can change part of a room
                }
                room[GameAddress::kRoomCellsOffset + static_cast<size_t>(at) * GameAddress::kRoomCellStride + 
                    GameAddress::kRoomCellTypeOffset] = static_cast<uint8_t>(cells[at]);
            }
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
        g_originalActivate = reinterpret_cast<LevelActivateRoomRoutine>(g_activateHook.Trampoline());

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
        g_table.GetRoomLightCount = &ApiGetRoomLightCount;
        g_table.GetRoomLight = &ApiGetRoomLight;
        g_table.SetRoomLight = &ApiSetRoomLight;

        g_addLight = reinterpret_cast<SceneAddLightRoutine>(GameSymbols::Address(GameSymbol::SceneAddLight));
        if (g_addLight != nullptr &&
            InstallHook(g_sortHook, GameSymbol::SceneSortObjects, reinterpret_cast<void*>(&SortObjectsDetour),
                kSceneSortObjectsPrologue, sizeof(kSceneSortObjectsPrologue), "Scene_SortObjects"))
        {
            g_originalSort = reinterpret_cast<SceneSortObjectsRoutine>(g_sortHook.Trampoline());
            g_table.AddLight = &ApiAddLight;
        }
        else
        {
            g_addLight = nullptr;
            FORGE_LOG_WARN("A light for one frame is not offered: the scene could not be reached");
        }

        g_table.GetEntityLight = &ApiGetEntityLight;
        g_table.SetEntityLight = &ApiSetEntityLight;
        g_table.GetPlayerEntity = &ApiGetPlayerEntity;
        g_table.GetRoomLightmap = &ApiGetRoomLightmap;
        g_table.GetStaticCount = &ApiGetStaticCount;
        g_table.GetStatic = &ApiGetStatic;
        g_table.SetStatic = &ApiSetStatic;
        g_table.AddStatic = &ApiAddStatic;
        g_table.AddRoomLight = &ApiAddRoomLight;
        g_table.SetRoomCell = &ApiSetRoomCell;
        g_table.SetRoomCells = &ApiSetRoomCells;
        g_table.GetRoomBox = &ApiGetRoomBox;
        g_table.SetRoomBox = &ApiSetRoomBox;
        g_table.AddRoomBox = &ApiAddRoomBox;
        g_table.GetLightTriangleCount = &ApiGetLightTriangleCount;
        g_table.GetLightTriangle = &ApiGetLightTriangle;
        g_table.SetLightTriangle = &ApiSetLightTriangle;
        g_table.AddLightTriangle = &ApiAddLightTriangle;

        FORGE_LOG_INFO("World interface installed");
        return true;
    }

    void WorldApi::Uninstall()
    {
        g_sortHook.Uninstall();
        g_activateHook.Uninstall();
        g_freeHook.Uninstall();
        g_loadHook.Uninstall();
        g_originalSort = nullptr;
        g_addLight = nullptr;
        g_originalActivate = nullptr;
        g_originalFree = nullptr;
        g_originalLoad = nullptr;
        g_level = nullptr;
        g_subscriptions.clear();
        g_pending.clear();
        g_entityLights.clear();
        g_statics.clear();
        g_grids.clear();
        g_boxes.clear();
        g_triangles.clear();
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

        // Pending one-frame lights go with the mod.
        for (auto it = g_pending.begin(); it != g_pending.end();)
        {
            it = it->owner == owner ? g_pending.erase(it) : ++it;
        }

        // Restore light triangle bytes + count on unload.
        for (auto it = g_triangles.begin(); it != g_triangles.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            uint8_t* const room = Room(it->roomIndex);
            uint8_t* const triangles = room != nullptr
                ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomLightTrianglesOffset))
                : nullptr;

            if (room != nullptr && triangles != nullptr && !it->bytes.empty() &&
                Readable(triangles, it->bytes.size()))
            {
                memcpy(triangles, it->bytes.data(), it->bytes.size());
            }
            if (room != nullptr)
            {
                memcpy(room + GameAddress::kRoomLightTriangleCountOffset, &it->count, sizeof(it->count));
            }
            it = g_triangles.erase(it);
        }

        for (auto it = g_boxes.begin(); it != g_boxes.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            uint8_t* const room = Room(it->roomIndex);
            uint8_t* const boxes = room != nullptr
                ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(room, GameAddress::kRoomCollisionBoxesOffset))
                : nullptr;

            if (room != nullptr && boxes != nullptr && !it->bytes.empty() &&
                Readable(boxes, it->bytes.size()))
            {
                memcpy(boxes, it->bytes.data(), it->bytes.size());
            }
            if (room != nullptr)
            {
                memcpy(room + GameAddress::kRoomCollisionBoxCountOffset, &it->count, sizeof(it->count));
            }
            it = g_boxes.erase(it);
        }

        // Restore full grid snapshot on unload.
        for (auto it = g_grids.begin(); it != g_grids.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            uint8_t* const room = Room(it->roomIndex);
            if (room != nullptr && it->bytes.size() == GameAddress::kRoomGridSize)
            {
                memcpy(room + GameAddress::kRoomCellsOffset, it->bytes.data(), it->bytes.size());
            }
            it = g_grids.erase(it);
        }

        // Restore statics; added ones are hidden (cannot unlink).
        for (auto it = g_statics.begin(); it != g_statics.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            uint8_t* const piece = StaticAt(it->roomIndex, it->index);
            if (piece != nullptr && it->added)
            {
                uint32_t flags = Read<uint32_t>(piece, GameAddress::kRoomStaticFlagsOffset);
                flags |= GameAddress::kRoomStaticHiddenFlag;
                memcpy(piece + GameAddress::kRoomStaticFlagsOffset, &flags, sizeof(flags));
            }
            else if (piece != nullptr)
            {
                // Restore static bytes except the list link.
                memcpy(piece, it->bytes, GameAddress::kRoomStaticNextOffset);
            }
            it = g_statics.erase(it);
        }

        // Restore entity lights on whatever is still there.
        for (auto it = g_entityLights.begin(); it != g_entityLights.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            uint8_t* const entity = LitEntityAt(it->roomIndex, it->index);
            if (entity != nullptr)
            {
                memcpy(entity + GameAddress::kEntityLightOffsetOffset, it->bytes, sizeof(it->bytes));

                uint32_t flags = Read<uint32_t>(entity, GameAddress::kEntityFlagsOffset);
                flags = (flags & ~GameAddress::kEntityLightFlag) |
                    (it->flags & GameAddress::kEntityLightFlag);
                memcpy(entity + GameAddress::kEntityFlagsOffset, &flags, sizeof(flags));
            }
            it = g_entityLights.erase(it);
        }

        for (auto it = g_lightSnapshots.begin(); it != g_lightSnapshots.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            uint8_t* const light = LightAt(it->roomIndex, it->index);
            uint8_t* const showing = SceneLightAt(it->roomIndex, it->index);

            const float out = 0.0f;

            if (light != nullptr)
            {
                if (it->added) { memcpy(light + GameAddress::kLightRangeOffset, &out, sizeof(out)); }
                else { memcpy(light, it->bytes, sizeof(it->bytes)); }
            }
            if (showing != nullptr)
            {
                if (it->added) { memcpy(showing + GameAddress::kLightRangeOffset, &out, sizeof(out)); }
                else { memcpy(showing, it->bytes, sizeof(it->bytes)); }
            }
            it = g_lightSnapshots.erase(it);
        }
    }
}