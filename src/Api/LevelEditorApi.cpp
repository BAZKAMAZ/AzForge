#include <windows.h>

#include "src/Api/LevelEditorApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/FrameApi.h"
#include "src/Api/RenderApi.h"
#include "src/Api/TextureApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Hook/InlineHook.h"

#include <stdio.h>
#include <string.h>
#include <filesystem>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;

        AmodLevelEditorApi g_table = {};

        typedef void(__fastcall* EntitiesUpdateRoutine)(void* scene, void* unused, float seconds);
        typedef void(__fastcall* RenderWorldRoutine)(void* scene, void* unused);
        typedef void(__fastcall* EditorDrawRoutine)(void* editor, void* unused);
        typedef void(__fastcall* EditorOnKeyRoutine)(void* editor, void* unused, int key, int arg);
        typedef void(__fastcall* EditorCursorRoutine)(void* editor, void* unused);
        typedef void(__fastcall* DrawObjectsRoutine)(void* scene, void* unused);
        typedef void(__fastcall* GroupNextRoutine)(void* collector, void* unused);
        typedef void*(__fastcall* FindByCellRoutine)(void* collector, void* unused, int layer, int cell, int variant);
        typedef void*(__fastcall* GroupFindRoutine)(void* group, void* unused, char cell, uint32_t variant);
        typedef void*(__fastcall* RoomEditInitRoutine)(void* room, void* unused);
        typedef void(__fastcall* RoomEditSetPlaceRoutine)(void* room, void* unused, int x, int y);
        typedef char(__fastcall* LoadRoomFileRoutine)(void* room, void* unused, const char* path);
        typedef void(__fastcall* LayerRoutine)(void* collector, void* unused, void* cells, int group, int kind, void* file, int x, int y);
        typedef void(__fastcall* PlacementSlotRoutine)(void* brush, void* unused, void* cells, void* file, int x, int y);

        // The game's own, because the FILE* belongs to whichever runtime opened it
        typedef void*(__cdecl* GameFopenRoutine)(const char* path, const char* mode);
        typedef int(__cdecl* GameFcloseRoutine)(void* file);
        typedef int(__cdecl* GameFprintfRoutine)(void* file, const char* format, ...);
        typedef char(__stdcall* SaveLevelRoutine)(const char* path, char overwrite, char compile);
        typedef void(__cdecl* LevelStartRoutine)(const char* path, int flag);
        typedef void(__fastcall* SaveRoomFileRoutine)(void* room, void* unused, const char* path, char compile);
        typedef void(__fastcall* WritePlacementsRoutine)(void* room, void* unused, int kind, FILE* file);

        const float* g_delta = nullptr;
        uint8_t* g_input = nullptr;

        InlineHook g_updateHook;
        EntitiesUpdateRoutine g_originalUpdate = nullptr;

        InlineHook g_renderHook;
        RenderWorldRoutine g_originalRender = nullptr;

        InlineHook g_editorDrawHook;
        EditorDrawRoutine g_originalEditorDraw = nullptr;

        InlineHook g_objectsHook;
        DrawObjectsRoutine g_originalObjects = nullptr;

        InlineHook g_groupNextHook;
        GroupNextRoutine g_originalGroupNext = nullptr;

        InlineHook g_findCellHook;
        FindByCellRoutine g_originalFindCell = nullptr;
        GroupFindRoutine g_groupFind = nullptr;

        InlineHook g_roomInitHook;
        RoomEditInitRoutine g_originalRoomInit = nullptr;

        InlineHook g_roomPlaceHook;
        RoomEditSetPlaceRoutine g_originalRoomPlace = nullptr;

        InlineHook g_saveLevelHook;
        SaveLevelRoutine g_originalSaveLevel = nullptr;

        InlineHook g_loadRoomHook;
        LoadRoomFileRoutine g_originalLoadRoom = nullptr;

        typedef void(__fastcall* RoomDrawRoutine)(void* room, void* unused);
        typedef void(__fastcall* DrawGridRoutine)(void* room, void* unused, float x, float y, float cell, int lines);
        typedef void(__fastcall* EditorTextRoutine)(void* font, void* unused, const char* text, const float* position, const float* scale, const float* colour, 
            int length, float fixedWidth);
        typedef void*(__fastcall* CollectorPickRoutine)(void* collector, void* unused);
        typedef const char*(__fastcall* BrushTextRoutine)(void* brush, void* unused);
        typedef char(__fastcall* BrushKeyRoutine)(void* brush, void* unused);
        typedef char*(__fastcall* NameAssignRoutine)(char* buffer, void* unused, const char* text);
        typedef char(__fastcall* GrowRowsRoutine)(void* maze, void* unused, int count);
        typedef void(__fastcall* RoomClearRoutine)(void* room, void* unused);

        InlineHook g_growRowsHook;
        GrowRowsRoutine g_originalGrowRows = nullptr;

        InlineHook g_nameAssignHook;
        NameAssignRoutine g_originalNameAssign = nullptr;

        InlineHook g_saveRoomHook;

        InlineHook g_roomDrawHook;
        DrawGridRoutine g_drawGrid = nullptr;
        EditorTextRoutine g_editorText = nullptr;
        CollectorPickRoutine g_brushCurrent = nullptr;
        CollectorPickRoutine g_groupCurrent = nullptr;

        struct EditorPart
        {
            int shown;
            float x;
            float y;
            float size;
            float scale;
        };

        constexpr size_t kEditorPartCount = AMOD_EDITOR_PART_MAZE + 1;

        EditorPart g_parts[kEditorPartCount] = {};
        ModContext* g_partsOwner = nullptr;

        // Empty until a mod says otherwise, and then it stands in for the game's own line
        std::string g_statusLines[AMOD_EDITOR_STATUS_LINES];
        bool g_statusMine[AMOD_EDITOR_STATUS_LINES] = {};

        struct BrushSnapshot
        {
            ModContext* owner;
            void* object;
            std::string label;
            float colour[4];
            bool labelTaken;
            bool colourTaken;
        };

        std::vector<BrushSnapshot> g_brushEdits;

        // The editor's two sprites, likewise
        void* g_iconWas[2] = {};
        bool g_iconTaken[2] = {};
        ModContext* g_iconOwner = nullptr;

        struct KeySub
        {
            ModContext* owner;
            AmodToken token;
            AmodEditorKeyHandler handler;
            void* user;
        };

        std::vector<KeySub> g_keySubs;

        struct RoomWriteSub
        {
            ModContext* owner;
            AmodToken token;
            AmodRoomWriteHandler handler;
            void* user;
        };

        std::vector<RoomWriteSub> g_roomWriteSubs;

        std::string g_roomWritePath;
        void* g_roomWriteRoom = nullptr;

        InlineHook g_writeLayerHook;
        InlineHook g_readLayerHook;
        LayerRoutine g_originalWriteLayer = nullptr;
        LayerRoutine g_originalReadLayer = nullptr;

        GameFopenRoutine g_fopen = nullptr;
        GameFcloseRoutine g_fclose = nullptr;
        GameFprintfRoutine g_fprintf = nullptr;
        SaveRoomFileRoutine g_saveRoomFile = nullptr;
        SaveRoomFileRoutine g_originalSaveRoomFile = nullptr;
        WritePlacementsRoutine g_writePlacements = nullptr;

        void* g_newRoom = nullptr;
        uint8_t* g_roomOverride = nullptr;
        uint8_t* g_cellsOverride = nullptr;
        int g_cellsLayer = -1;

        void** g_onKeySlot = nullptr;
        EditorOnKeyRoutine g_originalOnKey = nullptr;
        EditorCursorRoutine g_editorCursor = nullptr;

        struct FrameSub
        {
            ModContext* owner;
            AmodToken token;
            AmodEditorFrameHandler handler;
            void* user;
        };

        struct DrawSub
        {
            ModContext* owner;
            AmodToken token;
            int phase;
            AmodEditorDrawHandler handler;
            void* user;
        };

        struct RoomSub
        {
            ModContext* owner;
            AmodToken token;
            AmodRoomCreatedHandler handler;
            void* user;
        };

        struct LoadedSub
        {
            ModContext* owner;
            AmodToken token;
            AmodRoomLoadedHandler handler;
            void* user;
        };

        struct WriteSub
        {
            ModContext* owner;
            AmodToken token;
            AmodLevelWriteHandler handler;
            void* user;
        };

        std::vector<FrameSub> g_frameSubs;
        std::vector<DrawSub> g_drawSubs;
        std::vector<RoomSub> g_roomSubs;
        std::vector<LoadedSub> g_loadedSubs;
        std::vector<WriteSub> g_writeSubs;
        AmodToken g_nextToken = 1;

        ModContext* g_owner = nullptr;
        AmodEditorMode g_mode = {};

        float* CvarValue(uintptr_t address)
        {
            uint8_t* const node = static_cast<uint8_t*>(GameModule::Resolve(address));
            uint8_t* const held = node != nullptr && Readable(node, sizeof(uintptr_t))
                ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(node, 0))
                : nullptr;

            return (held != nullptr && Readable(held, GameAddress::kCvarValueOffset + sizeof(float)))
                ? reinterpret_cast<float*>(held + GameAddress::kCvarValueOffset)
                : nullptr;
        }

        bool IsActive()
        {
            return g_owner != nullptr;
        }

        void FireFrame(float seconds)
        {
            const std::vector<FrameSub> snapshot = g_frameSubs;

            for (const FrameSub& held : snapshot)
            {
                if (held.owner == g_owner && held.handler != nullptr)
                {
                    held.handler(seconds, held.user);
                }
            }
        }

        void FireDraw(int phase)
        {
            const std::vector<DrawSub> snapshot = g_drawSubs;
            const bool needsHolder = phase != AMOD_EDITOR_DRAW_BACKDROP;

            for (const DrawSub& held : snapshot)
            {
                if ((held.owner == g_owner || !needsHolder) && held.phase == phase && held.handler != nullptr)
                {
                    held.handler(held.user);
                }
            }
        }

        void HoldKeys()
        {
            if (g_input == nullptr || g_input[GameAddress::kInputSwallowingOffset] == 0)
            {
                return;
            }

            for (int key = 0; key < static_cast<int>(GameAddress::kInputKeyCount); ++key)
            {
                g_input[GameAddress::kInputKeyStateOffset + key] =
                    (GetAsyncKeyState(key) & 0x8000) != 0 ? 1 : 0;
            }
        }

        void HoldSwallow()
        {
            if (g_input != nullptr && g_mode.stopInput != 0)
            {
                g_input[GameAddress::kInputSwallowingOffset] = 1;
            }
        }

        void __cdecl EditorTick()
        {
            if (IsActive())
            {
                HoldSwallow();
                HoldKeys();
                FireFrame(g_delta != nullptr ? *g_delta : 0.0f);
            }
        }

        struct ModBrush
        {
            ModContext* owner;
            int handle;
            int group;
            std::vector<uint8_t> object;    // the game sees this, so it must not move
            void* slots[GameAddress::kBrushVtableSlots];
            std::string label;
            AmodBrushDrawHandler draw;
            AmodBrushPaintHandler paint;
            AmodBrushSaveHandler save;
            AmodTextureId texture;
            void* user;
        };

        std::vector<ModBrush*> g_brushes;
        int g_nextBrush = 1;

        struct GroupList
        {
            void* original;                 // the game's own array, put back on the way out
            int originalCount;
            std::vector<void*> held;
        };

        GroupList g_groups[GameAddress::kBrushGroupCount] = {};
        char g_brushText[GameAddress::kBrushLabelSize] = {};

        struct ModGroup
        {
            ModContext* owner;
            int tab;                        // 2 and up; 0 and 1 are the game's
            int layer;                      // which grid its brushes paint
            std::string name;
            std::vector<uint8_t> object;    // the 0x110 the game will read
            std::vector<void*> brushes;
        };

        std::vector<ModGroup*> g_modGroups;
        int g_tab = 0;
        void* g_swappedOut[GameAddress::kBrushGroupCount] = {};

        uint8_t* Collector()
        {
            void** const slot = static_cast<void**>(GameModule::Resolve(GameAddress::kBrushCollector));
            uint8_t* const collector = (slot != nullptr && Readable(slot, sizeof(void*)))
                ? static_cast<uint8_t*>(*slot)
                : nullptr;

            return (collector != nullptr && Readable(collector, 0x0C)) ? collector : nullptr;
        }

        ModGroup* ModGroupAt(int tab)
        {
            for (ModGroup* const held : g_modGroups)
            {
                if (held->tab == tab)
                {
                    return held;
                }
            }

            return nullptr;
        }

        int TabCount()
        {
            return static_cast<int>(GameAddress::kBrushGroupCount) + static_cast<int>(g_modGroups.size());
        }

        // Tabs 0 and 1 are the game's groups, wherever they currently sit
        uint8_t* Group(int tab)
        {
            ModGroup* const mine = ModGroupAt(tab);

            if (mine != nullptr)
            {
                return mine->object.data();
            }

            uint8_t* const collector = Collector();

            if (collector == nullptr || tab < 0 || tab >= static_cast<int>(GameAddress::kBrushGroupCount))
            {
                return nullptr;
            }
            if (g_swappedOut[tab] != nullptr)
            {
                return static_cast<uint8_t*>(g_swappedOut[tab]);
            }

            uint8_t* const group = reinterpret_cast<uint8_t*>(
                Read<uintptr_t>(collector, static_cast<uintptr_t>(tab) * sizeof(void*)));

            return (group != nullptr && Readable(group, 0x110)) ? group : nullptr;
        }

        void ApplyTab()
        {
            uint8_t* const collector = Collector();

            if (collector == nullptr)
            {
                return;
            }

            for (int layer = 0; layer < static_cast<int>(GameAddress::kBrushGroupCount); ++layer)
            {
                if (g_swappedOut[layer] != nullptr)
                {
                    memcpy(collector + static_cast<uintptr_t>(layer) * sizeof(void*), &g_swappedOut[layer], sizeof(void*)); 
                    g_swappedOut[layer] = nullptr;
                }
            }

            ModGroup* const mine = ModGroupAt(g_tab);
            const int which = mine != nullptr ? mine->layer : g_tab;

            if (mine != nullptr)
            {
                void* const held = reinterpret_cast<void*>(Read<uintptr_t>(collector, static_cast<uintptr_t>(mine->layer) * sizeof(void*)));
                void* const ours = mine->object.data();

                g_swappedOut[mine->layer] = held;
                memcpy(collector + static_cast<uintptr_t>(mine->layer) * sizeof(void*), &ours, sizeof(ours));
            }

            memcpy(collector + GameAddress::kBrushCurrentGroupOffset, &which, sizeof(which));
        }

        void** GroupList_(const uint8_t* group)
        {
            return group != nullptr
                ? reinterpret_cast<void**>(Read<uintptr_t>(group, GameAddress::kBrushGroupListOffset))
                : nullptr;
        }

        int GroupCount(const uint8_t* group)
        {
            const int count = group != nullptr ? Read<int>(group, GameAddress::kBrushGroupCountOffset) : 0;
            return (count > 0 && count < 0x400) ? count : 0;
        }

        ModBrush* BrushOf(void* object)
        {
            for (ModBrush* const brush : g_brushes)
            {
                if (object != nullptr && brush->object.data() == object)
                {
                    return brush;
                }
            }

            return nullptr;
        }

        void __fastcall BrushDrawThunk(void* self, void*, float x, float y, float size)
        {
            ModBrush* const brush = BrushOf(self);

            if (brush == nullptr)
            {
                return;
            }
            if (brush->draw != nullptr)
            {
                brush->draw(x, y, size, brush->user);
                return;
            }

            const AmodRenderApi* const render = RenderApi::Table();

            if (brush->texture != AMOD_INVALID_TEXTURE && render != nullptr && render->DrawSprite != nullptr)
            {
                render->DrawSprite(brush->owner, brush->texture, x - size * 1.5f, y + size * 3.0f, size * 4.0f, size * 4.0f, 0xFFFFFFFF);
            }
        }

        char __fastcall BrushCellThunk(void* self, void*)
        {
            return static_cast<uint8_t*>(self)[GameAddress::kBrushCellOffset];
        }

        char __fastcall BrushKeyThunk(void* self, void*)
        {
            return static_cast<uint8_t*>(self)[GameAddress::kBrushKeyOffset];
        }

        const char* __fastcall BrushLabelThunk(void* self, void*)
        {
            return reinterpret_cast<const char*>(static_cast<uint8_t*>(self) + GameAddress::kBrushLabelOffset);
        }

        // Slot [4] is not the texture - Brush_FindByCell ANDs what it returns against the variant
        uint32_t __fastcall BrushVariantsThunk(void* self, void*)
        {
            return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(self) + GameAddress::kBrushVariantsOffset);
        }

        bool CellInGrid(int column, int row)
        {
            return column >= 0 && column < GameAddress::kRoomEditColumns && row >= 0 && row < GameAddress::kRoomEditRows;
        }

        void __fastcall BrushPaintThunk(void* self, void*, int column, int row, uint8_t* cells)
        {
            ModBrush* const brush = BrushOf(self);
            const bool write = brush == nullptr || brush->paint == nullptr ||
                brush->paint(column, row, brush->user) != 0;

            if (write && cells != nullptr && CellInGrid(column, row))
            {
                cells[row * GameAddress::kRoomEditColumns + column] = static_cast<uint8_t*>(self)[GameAddress::kBrushCellOffset];
            }
        }

        void* __fastcall BrushDeleteThunk(void* self, void*, uint8_t)
        {
            return self;
        }

        // Slot [8], the read. It does nothing - a mod reads through OnRoomLoaded - but it is
        // RET 0x10, and a thunk that pops nothing unbalances the stack on every level load.
        void __fastcall BrushLoadThunk(void*, void*, uint8_t*, void*, int, int)
        {
        }

        // Slot [7], likewise RET 0x10: the room is being saved and this brush's kind picked the file
        void __fastcall BrushSaveThunk(void* self, void*, uint8_t* cells, FILE* file, int x, int y)
        {
            ModBrush* const brush = BrushOf(self);

            if (brush == nullptr || brush->save == nullptr)
            {
                return;
            }

            ModGroup* const mine = ModGroupAt(brush->group);
            const int layer = mine != nullptr ? mine->layer : brush->group;

            g_cellsOverride = cells;
            g_cellsLayer = layer;

            const char* const text = brush->save(x, y, brush->user);

            g_cellsOverride = nullptr;
            g_cellsLayer = -1;

            // The game opened this handle, so the game's fprintf writes to it
            if (text != nullptr && file != nullptr && g_fprintf != nullptr)
            {
                g_fprintf(file, "%s", text);
            }
        }

        typedef void*(__fastcall* ActiveEditRoutine)(void* editor, void* unused);

        uint8_t* Grid(int layer)
        {
            if (g_cellsOverride != nullptr && layer == g_cellsLayer)
            {
                return g_cellsOverride;
            }

            const ActiveEditRoutine active = reinterpret_cast<ActiveEditRoutine>(GameModule::Resolve(GameAddress::kLevelEditorActiveEdit));
            void* const editor = GameModule::Resolve(GameAddress::kLevelEditor);

            if (active == nullptr || editor == nullptr || layer < 0 ||
                layer >= static_cast<int>(GameAddress::kBrushGroupCount))
            {
                return nullptr;
            }

            uint8_t* const room = g_roomOverride != nullptr
                ? g_roomOverride
                : static_cast<uint8_t*>(active(editor, nullptr));

            if (room == nullptr || !Readable(room, GameAddress::kRoomEditGridOffset + GameAddress::kBrushGroupCount * GameAddress::kRoomEditLayerStride))
            {
                return nullptr;
            }

            return room + GameAddress::kRoomEditGridOffset + static_cast<size_t>(layer) * GameAddress::kRoomEditLayerStride;
        }

        int __cdecl ApiGetGridCell(AmodMod mod, int layer, int column, int row)
        {
            const uint8_t* const grid = Caller(mod) != nullptr ? Grid(layer) : nullptr;

            return (grid != nullptr && CellInGrid(column, row))
                ? grid[row * GameAddress::kRoomEditColumns + column]
                : 0;
        }

        int __cdecl ApiSetGridCell(AmodMod mod, int layer, int column, int row, int cell)
        {
            uint8_t* const grid = Caller(mod) != nullptr ? Grid(layer) : nullptr;

            if (grid == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "no room is being edited");
            }
            if (!CellInGrid(column, row))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d,%d is outside the grid", column, row);
            }

            grid[row * GameAddress::kRoomEditColumns + column] = static_cast<uint8_t>(cell);
            return 1;
        }

        int __cdecl ApiGridSize(AmodMod mod, int* columns, int* rows)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (columns != nullptr)
            {
                *columns = GameAddress::kRoomEditColumns;
            }
            if (rows != nullptr)
            {
                *rows = GameAddress::kRoomEditRows;
            }

            return 1;
        }

        uint8_t* Maze()
        {
            uint8_t* const editor = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kLevelEditor));
            uint8_t* const maze = editor != nullptr && Readable(editor, 0x18)
                ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(editor, GameAddress::kEditorEditOffset))
                : nullptr;

            return (maze != nullptr && Readable(maze, 0x10)) ? maze : nullptr;
        }

        // The cell itself, so removing a room can null it
        void** RoomSlot(int x, int y)
        {
            uint8_t* const maze = Maze();

            if (maze == nullptr || x < 0 || y < 0 || x >= Read<int>(maze, GameAddress::kMazeWidthOffset) || y >= Read<int>(maze, GameAddress::kMazeHeightOffset))
            {
                return nullptr;
            }

            void* const* const rows = reinterpret_cast<void* const*>(Read<uintptr_t>(maze, GameAddress::kMazeRowsOffset));

            if (rows == nullptr || !Readable(rows, static_cast<size_t>(y + 1) * sizeof(void*)))
            {
                return nullptr;
            }

            void** const row = static_cast<void**>(rows[y]);
            return (row != nullptr && Readable(row, static_cast<size_t>(x + 1) * sizeof(void*)))
                ? row + x
                : nullptr;
        }

        uint8_t* RoomAt(int x, int y)
        {
            void** const slot = RoomSlot(x, y);
            return slot != nullptr ? static_cast<uint8_t*>(*slot) : nullptr;
        }

        typedef void*(__fastcall* MazeGoToRoutine)(void* maze, void* unused, int x, int y);
        typedef char(__fastcall* MazeInsertRoutine)(void* maze, void* unused, int at);
        typedef char(__fastcall* MazeFromFileRoutine)(void* maze, void* unused, int x, int y, const char* path, int spare1, int spare2, int spare3);
        typedef void*(__fastcall* RoomDeleteRoutine)(void* room, void* unused, uint8_t flags);

        int g_loadX = -1;
        int g_loadY = -1;

        // Both RET 4, so both are __stdcall - the same pair FilesApi opens the game's files with
        typedef void*(__stdcall* PackageOpenRoutine)(const char* path);
        typedef void(__stdcall* PackageFreeRoutine)(void* file);

        bool FileIsThere(const char* path)
        {
            const PackageOpenRoutine open = reinterpret_cast<PackageOpenRoutine>(GameModule::Resolve(GameAddress::kPackageOpenFile));
            const PackageFreeRoutine release = reinterpret_cast<PackageFreeRoutine>(GameModule::Resolve(GameAddress::kPackageFree));

            if (open == nullptr || release == nullptr)
            {
                return true;
            }

            void* const file = open(path);

            if (file == nullptr)
            {
                return false;
            }

            release(file);
            return true;
        }

        char __fastcall GrowRowsDetour(void* maze, void* unused, int count)
        {
            char result = 1;

            if (g_originalGrowRows == nullptr)
            {
                return 0;
            }

            for (int at = 0; at < count && result != 0; ++at)
            {
                result = g_originalGrowRows(maze, unused, 1);
            }

            return count > 0 ? result : g_originalGrowRows(maze, unused, count);
        }

        bool SetCursor(int x, int y)
        {
            uint8_t* const maze = Maze();

            if (maze == nullptr || x < 0 || y < 0 ||
                x >= Read<int>(maze, GameAddress::kMazeWidthOffset) ||
                y >= Read<int>(maze, GameAddress::kMazeHeightOffset))
            {
                return false;
            }

            *reinterpret_cast<int*>(maze + GameAddress::kMazeCursorXOffset) = x;
            *reinterpret_cast<int*>(maze + GameAddress::kMazeCursorYOffset) = y;
            return true;
        }

        bool ReachCell(int x, int y)
        {
            const MazeInsertRoutine insertRow = reinterpret_cast<MazeInsertRoutine>(GameModule::Resolve(GameAddress::kMazeEditInsertRow));
            const MazeInsertRoutine insertColumn = reinterpret_cast<MazeInsertRoutine>(GameModule::Resolve(GameAddress::kMazeEditInsertColumn));
            uint8_t* const maze = Maze();

            if (maze == nullptr || insertRow == nullptr || insertColumn == nullptr)
            {
                return false;
            }

            while (Read<int>(maze, GameAddress::kMazeHeightOffset) <= y)
            {
                insertRow(maze, nullptr, Read<int>(maze, GameAddress::kMazeHeightOffset));
            }
            while (Read<int>(maze, GameAddress::kMazeWidthOffset) <= x)
            {
                insertColumn(maze, nullptr, Read<int>(maze, GameAddress::kMazeWidthOffset));
            }

            return true;
        }

        int __cdecl ApiMazeSize(AmodMod mod, int* columns, int* rows)
        {
            uint8_t* const maze = Caller(mod) != nullptr ? Maze() : nullptr;

            if (maze == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the editor is holding no maze");
            }
            if (columns != nullptr)
            {
                *columns = Read<int>(maze, GameAddress::kMazeWidthOffset);
            }
            if (rows != nullptr)
            {
                *rows = Read<int>(maze, GameAddress::kMazeHeightOffset);
            }

            return 1;
        }

        int __cdecl ApiGetMazeCursor(AmodMod mod, int* x, int* y)
        {
            uint8_t* const maze = Caller(mod) != nullptr ? Maze() : nullptr;

            if (maze == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the editor is holding no maze");
            }
            if (x != nullptr)
            {
                *x = Read<int>(maze, GameAddress::kMazeCursorXOffset);
            }
            if (y != nullptr)
            {
                *y = Read<int>(maze, GameAddress::kMazeCursorYOffset);
            }

            return 1;
        }

        int __cdecl ApiSetMazeCursor(AmodMod mod, int x, int y)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            return SetCursor(x, y)
                ? 1
                : Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d,%d is outside the maze", x, y);
        }

        int __cdecl ApiHasRoom(AmodMod mod, int x, int y)
        {
            return (Caller(mod) != nullptr && RoomAt(x, y) != nullptr) ? 1 : 0;
        }

        int __cdecl ApiCreateRoom(AmodMod mod, int x, int y)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (x < 0 || y < 0)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d,%d is not a maze cell", x, y);
            }
            if (!ReachCell(x, y))
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the editor is holding no maze");
            }

            const MazeGoToRoutine goTo = reinterpret_cast<MazeGoToRoutine>(
                GameModule::Resolve(GameAddress::kMazeEditGoTo));
            uint8_t* const maze = Maze();

            // One call: it makes the room when the cell is empty, then moves the cursor onto it
            return (goTo != nullptr && maze != nullptr && goTo(maze, nullptr, x, y) != nullptr)
                ? 1
                : Fail(mod, AMOD_FAIL_REFUSED, "the maze would not make a room at %d,%d", x, y);
        }

        int __cdecl ApiRemoveRoom(AmodMod mod, int x, int y)
        {
            void** const slot = Caller(mod) != nullptr ? RoomSlot(x, y) : nullptr;

            if (slot == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d,%d is outside the maze", x, y);
            }
            if (*slot == nullptr)
            {
                return 1;
            }

            void* const room = *slot;
            void* const* const vtable = Readable(room, sizeof(void*))
                ? reinterpret_cast<void* const*>(*static_cast<void**>(room))
                : nullptr;

            if (vtable == nullptr || !Readable(vtable, (GameAddress::kRoomEditDeleteSlot + 1) * sizeof(void*)))
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "the room at %d,%d has no destructor", x, y);
            }

            // Out of the maze before it is freed, so nothing can reach the dead pointer
            *slot = nullptr;

            if (g_newRoom == room)
            {
                g_newRoom = nullptr;
            }

            reinterpret_cast<RoomDeleteRoutine>(vtable[GameAddress::kRoomEditDeleteSlot])(room, nullptr, 1);
            return 1;
        }

        int __cdecl ApiInsertRow(AmodMod mod, int at)
        {
            const MazeInsertRoutine insert = Caller(mod) != nullptr
                ? reinterpret_cast<MazeInsertRoutine>(GameModule::Resolve(GameAddress::kMazeEditInsertRow))
                : nullptr;
            uint8_t* const maze = Maze();

            if (insert == nullptr || maze == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the editor is holding no maze");
            }

            return insert(maze, nullptr, at) != 0 ? 1 : Fail(mod, AMOD_FAIL_REFUSED, "the maze would not grow");
        }

        int __cdecl ApiInsertColumn(AmodMod mod, int at)
        {
            const MazeInsertRoutine insert = Caller(mod) != nullptr
                ? reinterpret_cast<MazeInsertRoutine>(GameModule::Resolve(GameAddress::kMazeEditInsertColumn))
                : nullptr;
            uint8_t* const maze = Maze();

            if (insert == nullptr || maze == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the editor is holding no maze");
            }

            return insert(maze, nullptr, at) != 0 ? 1 : Fail(mod, AMOD_FAIL_REFUSED, "the maze would not grow");
        }

        int __cdecl ApiLoadRoom(AmodMod mod, int x, int y, const char* path)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (path == nullptr || x < 0 || y < 0)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d,%d is not a maze cell", x, y);
            }

            if (!FileIsThere(path))
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no '%s'", path);
            }
            if (!ReachCell(x, y))
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the editor is holding no maze");
            }

            const MazeFromFileRoutine fromFile = reinterpret_cast<MazeFromFileRoutine>(
                GameModule::Resolve(GameAddress::kMazeEditRoomFromFile));
            uint8_t* const maze = Maze();

            if (fromFile == nullptr || maze == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "this build cannot read a room file");
            }

            g_loadX = x;
            g_loadY = y;

            const char result = fromFile(maze, nullptr, x, y, path, 0, 0, 0);

            g_loadX = -1;
            g_loadY = -1;

            if (result == 0)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "'%s' is not a room file", path);
            }

            uint8_t* const room = RoomAt(x, y);

            // It is loaded, not made: the coordinates are all that is left to stamp
            g_newRoom = nullptr;

            if (room != nullptr && g_originalRoomPlace != nullptr)
            {
                g_originalRoomPlace(room, nullptr, x, y);
            }

            SetCursor(x, y);
            return 1;
        }

        int __cdecl ApiSaveRoom(AmodMod mod, int x, int y, const char* path, int compile)
        {
            uint8_t* const room = Caller(mod) != nullptr ? RoomAt(x, y) : nullptr;

            if (path == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a room needs a path to be written to");
            }
            if (room == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no room at %d,%d", x, y);
            }
            if (g_saveRoomFile == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "this build cannot write a room file");
            }

            std::error_code sink;

            std::filesystem::create_directories(std::filesystem::path(path).parent_path(), sink);
            g_saveRoomFile(room, nullptr, path, compile != 0 ? 1 : 0);
            return 1;
        }

        const char* CvarText(uintptr_t address)
        {
            uint8_t* const node = static_cast<uint8_t*>(GameModule::Resolve(address));
            uint8_t* const held = node != nullptr && Readable(node, sizeof(uintptr_t))
                ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(node, 0))
                : nullptr;
            const char* const text = held != nullptr && Readable(held, GameAddress::kCvarTextOffset + sizeof(void*))
                ? reinterpret_cast<const char*>(Read<uintptr_t>(held, GameAddress::kCvarTextOffset))
                : nullptr;

            return (text != nullptr && Readable(text, 1)) ? text : "";
        }

        const char* FirePart(int part, int id, int x, int y, const char* rooms)
        {
            AmodLevelPart asked = {};
            asked.structSize = sizeof(asked);
            asked.part = part;
            asked.id = id;
            asked.x = x;
            asked.y = y;
            asked.rooms = rooms;

            for (const WriteSub& held : g_writeSubs)
            {
                const char* const text = held.handler != nullptr ? held.handler(&asked, held.user) : nullptr;

                if (text != nullptr)
                {
                    return text;
                }
            }

            return nullptr;
        }

        const char* FireRoomPart(int part, int x, int y, const char* path)
        {
            AmodRoomPart asked = {};

            asked.structSize = sizeof(asked);
            asked.part = part;
            asked.x = x;
            asked.y = y;
            asked.path = path;

            for (const RoomWriteSub& held : g_roomWriteSubs)
            {
                const char* const text = held.handler != nullptr ? held.handler(&asked, held.user) : nullptr;

                if (text != nullptr)
                {
                    return text;
                }
            }

            return nullptr;
        }

        void WriteRoomText(void* room, const char* path)
        {
            uint8_t* const at = static_cast<uint8_t*>(room);

            if (at == nullptr || path == nullptr || g_fopen == nullptr || g_fclose == nullptr ||
                g_fprintf == nullptr)
            {
                return;
            }

            const int x = Read<int>(at, GameAddress::kRoomEditXOffset);
            const int y = Read<int>(at, GameAddress::kRoomEditYOffset);
            const char* const header = FireRoomPart(AMOD_ROOM_HEADER, x, y, path);
            const char* const statics = FireRoomPart(AMOD_ROOM_STATICS, x, y, path);
            const char* const deform = FireRoomPart(AMOD_ROOM_DEFORM, x, y, path);
            void* const file = g_fopen(path, "w");

            if (file == nullptr)
            {
                return;
            }

            g_fprintf(file, "%s", header != nullptr ? header
                : "$back_wall 1\n$light_ambient 0.3 0.3 0.3\n$tex_wall_scale 1\n$tex_floor_scale 1\n\n"
                  "$ladder_cut 1\n$ladder_cross 1\n$chain_cut 1\n$chain_cross 1\n\n");

            g_fprintf(file, "%s", "$geometry\n{\n");

            const uint8_t* const cells = at + GameAddress::kRoomEditGridOffset;

            for (int row = 0; row < GameAddress::kRoomEditRows; ++row)
            {
                char line[GameAddress::kRoomEditColumns + 1] = {};

                memcpy(line, cells + row * GameAddress::kRoomEditColumns, GameAddress::kRoomEditColumns);
                g_fprintf(file, "\t$row \"%s\"\n", line);
            }

            g_fprintf(file, "%s", "}\n\n$statics\n{\n");
            g_fprintf(file, "%s", statics != nullptr ? statics : "");
            g_fprintf(file, "%s", "}\n\n$y_deform\n{\n");
            g_fprintf(file, "%s", deform != nullptr ? deform : "");
            g_fprintf(file, "%s", "}\n\n");

            g_fclose(file);
        }

        void FlushRoomText()
        {
            if (g_roomWriteRoom != nullptr && !g_roomWritePath.empty())
            {
                WriteRoomText(g_roomWriteRoom, g_roomWritePath.c_str());
            }

            g_roomWriteRoom = nullptr;
            g_roomWritePath.clear();
        }

        char* __fastcall NameAssignDetour(char* buffer, void* unused, const char* text)
        {
            if (g_roomWriteRoom != nullptr && text != nullptr && g_roomWritePath == text)
            {
                FlushRoomText();
            }

            return g_originalNameAssign != nullptr ? g_originalNameAssign(buffer, unused, text) : buffer;
        }

        void __fastcall SaveRoomFileDetour(void* room, void* unused, const char* path, char compile)
        {
            if (g_originalSaveRoomFile == nullptr)
            {
                return;
            }
            if (g_roomWriteSubs.empty() || room == nullptr || path == nullptr)
            {
                g_originalSaveRoomFile(room, unused, path, compile);
                return;
            }

            g_roomWriteRoom = room;
            g_roomWritePath = path;

            g_originalSaveRoomFile(room, unused, path, compile);
            FlushRoomText();
        }

        bool WriteLevel(const char* path, bool overwrite, bool compile)
        {
            uint8_t* const maze = Maze();

            if (path == nullptr || maze == nullptr || g_saveRoomFile == nullptr ||
                g_fopen == nullptr || g_fclose == nullptr || g_fprintf == nullptr)
            {
                return false;
            }

            const std::filesystem::path full(path);
            const std::filesystem::path rooms =
                full.parent_path() / ("rooms_" + full.stem().string());

            std::error_code sink;

            if (!overwrite && std::filesystem::exists(full, sink))
            {
                return false;
            }

            std::filesystem::create_directories(rooms, sink);

            void* const file = g_fopen != nullptr ? g_fopen(path, "w") : nullptr;

            if (file == nullptr)
            {
                return false;
            }

            const char* const header = FirePart(AMOD_LEVEL_HEADER, 0, 0, 0, nullptr);

            g_fprintf(file, "%s", "$header\n{\n");
            g_fprintf(file, "%s", header != nullptr ? header
                : "\t$next_level \"\"\n\t$music \"\"\n\t$bonus 0\n\t$bonus_time 0\n"
                  "\t$bonus_count 0\n\t$quest_list\n\t{\n\t}\n");
            g_fprintf(file, "%s", "}\n\n");

            const int width = Read<int>(maze, GameAddress::kMazeWidthOffset);
            const int height = Read<int>(maze, GameAddress::kMazeHeightOffset);
            const std::string stem = rooms.string();
            int id = 1;

            g_fprintf(file, "%s", "$rooms\n{\n");

            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    uint8_t* const room = RoomAt(x, y);

                    if (room == nullptr)
                    {
                        continue;
                    }

                    char text[512] = {};

                    snprintf(text, sizeof(text), "%s/%03i.txt", stem.c_str(), id);
                    g_saveRoomFile(room, nullptr, text, compile ? 1 : 0);

                    const char* const block = FirePart(AMOD_LEVEL_ROOM, id, x, y, stem.c_str());

                    g_fprintf(file, "\t$room\n\t{\n\t\t$id %i\n", id);
                    g_fprintf(file, "\t\t$room_file \"%s/%03i.room\"\n", stem.c_str(), id);

                    if (block != nullptr)
                    {
                        g_fprintf(file, "%s", block);
                    }
                    else
                    {
                        g_fprintf(file, "\t\t$floor_tex \"%s\"\n", CvarText(GameAddress::kEdDefFloorCvar));
                        g_fprintf(file, "\t\t$wall_tex \"%s\"\n\n", CvarText(GameAddress::kEdDefWallCvar));
                        g_fprintf(file, "\t\t$active_items \"%s/%03i_ctrls.txt\"\n\n", stem.c_str(), id);
                        g_fprintf(file, "\t\t$monsters \"%s/%03i_monsters.txt\"\n", stem.c_str(), id);
                        g_fprintf(file, "\t\t$bonuses \"%s/%03i_bonuses.txt\"\n\n", stem.c_str(), id);
                        g_fprintf(file, "%s", "\t\t$type \"light\"\n\t\t$demo \"\"\n\t\t$music \"\"\n");
                    }

                    g_fprintf(file, "%s", "\t}\n\n");
                    ++id;
                }
            }

            g_fprintf(file, "%s", "}\n\n$maze\n{\n");

            int numbered = 1;

            for (int y = 0; y < height; ++y)
            {
                g_fprintf(file, "%s", "\t$row \"");

                for (int x = 0; x < width; ++x)
                {
                    if (x != 0)
                    {
                        g_fprintf(file, "%s", ",");
                    }

                    if (RoomAt(x, y) == nullptr)
                    {
                        g_fprintf(file, "%s", "##");
                    }
                    else
                    {
                        g_fprintf(file, "%02i", numbered++);
                    }
                }

                g_fprintf(file, "%s", "\"\n");
            }

            g_fprintf(file, "%s", "}\n\n");

            // The fourth placement kind goes into the level file rather than a sidecar
            for (int y = 0; y < height && g_writePlacements != nullptr; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    uint8_t* const room = RoomAt(x, y);

                    if (room != nullptr)
                    {
                        g_writePlacements(room, nullptr, GameAddress::kPlacementKindLevel, static_cast<FILE*>(file));
                    }
                }
            }

            const char* const tail = FirePart(AMOD_LEVEL_TAIL, 0, 0, 0, stem.c_str());

            if (tail != nullptr)
            {
                g_fprintf(file, "%s", tail);
            }

            g_fclose(file);
            return true;
        }

        char __stdcall SaveLevelDetour(const char* path, char overwrite, char compile)
        {
            if (g_writeSubs.empty())
            {
                return g_originalSaveLevel != nullptr ? g_originalSaveLevel(path, overwrite, compile) : 0;
            }

            if (!WriteLevel(path, overwrite != 0, compile != 0))
            {
                return 0;
            }

            // F2 does not only save: the same flag plays the level and leaves the editor, and
            // taking that away would be a change nobody asked for
            const LevelStartRoutine start = reinterpret_cast<LevelStartRoutine>(GameModule::Resolve(GameAddress::kLevelStart));

            if (compile != 0 && start != nullptr)
            {
                float* const enabled = CvarValue(GameAddress::kEdEnableCvar);

                start(path, 0);

                if (enabled != nullptr)
                {
                    *enabled = 0.0f;
                }
            }

            return 1;
        }

        void PublishModGroup(ModGroup* group)
        {
            uint8_t* const at = group->object.data();
            void* const list = group->brushes.empty() ? nullptr : static_cast<void*>(group->brushes.data());
            const int count = static_cast<int>(group->brushes.size());
            const int current = Read<int>(at, GameAddress::kBrushGroupCurrentOffset);

            memcpy(at + GameAddress::kBrushGroupListOffset, &list, sizeof(list));
            memcpy(at + GameAddress::kBrushGroupCountOffset, &count, sizeof(count));

            if (current >= count)
            {
                const int reset = 0;
                memcpy(at + GameAddress::kBrushGroupCurrentOffset, &reset, sizeof(reset));
            }
        }

        void* __fastcall RoomEditInitDetour(void* room, void* unused)
        {
            void* const made = g_originalRoomInit != nullptr
                ? g_originalRoomInit(room, unused)
                : room;

            g_newRoom = room;
            return made;
        }

        void __fastcall RoomEditSetPlaceDetour(void* room, void* unused, int x, int y)
        {
            if (g_originalRoomPlace != nullptr)
            {
                g_originalRoomPlace(room, unused, x, y);
            }

            if (room == nullptr || room != g_newRoom)
            {
                return;
            }

            g_newRoom = nullptr;

            const std::vector<RoomSub> snapshot = g_roomSubs;

            g_roomOverride = static_cast<uint8_t*>(room);

            for (const RoomSub& held : snapshot)
            {
                if (held.handler != nullptr)
                {
                    held.handler(x, y, held.user);
                }
            }

            g_roomOverride = nullptr;
        }

        char __fastcall LoadRoomFileDetour(void* room, void* unused, const char* path)
        {
            const char result = g_originalLoadRoom != nullptr
                ? g_originalLoadRoom(room, unused, path)
                : 0;

            if (result == 0 || room == nullptr || path == nullptr || g_loadedSubs.empty())
            {
                return result;
            }

            const std::filesystem::path full(path);
            const std::string stem = (full.parent_path() / full.stem()).string();
            const std::string monsters = stem + "_monsters.txt";
            const std::string bonuses = stem + "_bonuses.txt";

            AmodRoomFiles files = {};
            files.structSize = sizeof(files);
            files.x = g_loadX >= 0 ? g_loadX : Read<int>(static_cast<uint8_t*>(room), GameAddress::kRoomEditXOffset);
            files.y = g_loadY >= 0 ? g_loadY : Read<int>(static_cast<uint8_t*>(room), GameAddress::kRoomEditYOffset);
            files.room = path;
            files.monsters = monsters.c_str();
            files.bonuses = bonuses.c_str();

            const std::vector<LoadedSub> snapshot = g_loadedSubs;

            g_roomOverride = static_cast<uint8_t*>(room);

            for (const LoadedSub& held : snapshot)
            {
                if (held.handler != nullptr)
                {
                    held.handler(&files, held.user);
                }
            }

            g_roomOverride = nullptr;
            return result;
        }

        void WalkLayer(void* collector, void* unused, void* cells, int group, int kind, void* file, int x, int y, LayerRoutine original, int slot)
        {
            void* const displaced = (group >= 0 && group < static_cast<int>(GameAddress::kBrushGroupCount))
                ? g_swappedOut[group]
                : nullptr;
            uint8_t* const at = Collector();

            if (displaced != nullptr && at != nullptr)
            {
                void* const mine = reinterpret_cast<void*>(Read<uintptr_t>(at, static_cast<uintptr_t>(group) * sizeof(void*)));

                memcpy(at + static_cast<uintptr_t>(group) * sizeof(void*), &displaced, sizeof(void*));

                if (original != nullptr)
                {
                    original(collector, unused, cells, group, kind, file, x, y);
                }

                memcpy(at + static_cast<uintptr_t>(group) * sizeof(void*), &mine, sizeof(void*));
            }
            else if (original != nullptr)
            {
                original(collector, unused, cells, group, kind, file, x, y);
            }

            for (const ModGroup* const held : g_modGroups)
            {
                if (held->layer != group)
                {
                    continue;
                }

                for (void* const object : held->brushes)
                {
                    void** const slots = object != nullptr ? *static_cast<void***>(object) : nullptr;

                    if (slots == nullptr || Read<int>(static_cast<uint8_t*>(object), GameAddress::kBrushKindOffset) != kind)
                    {
                        continue;
                    }

                    reinterpret_cast<PlacementSlotRoutine>(slots[slot])(object, nullptr, cells, file, x, y);
                }
            }
        }

        void __fastcall WriteLayerDetour(void* collector, void* unused, void* cells, int group, int kind, void* file, int x, int y)
        {
            WalkLayer(collector, unused, cells, group, kind, file, x, y, g_originalWriteLayer, 7);
        }

        void __fastcall ReadLayerDetour(void* collector, void* unused, void* cells, int group, int kind, void* file, int x, int y)
        {
            WalkLayer(collector, unused, cells, group, kind, file, x, y, g_originalReadLayer, 8);
        }

        void __fastcall GroupNextDetour(void* collector, void* unused)
        {
            if (g_modGroups.empty())
            {
                if (g_originalGroupNext != nullptr)
                {
                    g_originalGroupNext(collector, unused);
                }

                g_tab = Collector() != nullptr
                    ? Read<int>(Collector(), GameAddress::kBrushCurrentGroupOffset)
                    : 0;
                return;
            }

            g_tab = (g_tab + 1) % TabCount();
            ApplyTab();
        }

        void* __fastcall FindByCellDetour(void* collector, void* unused, int layer, int cell, int variant)
        {
            void* found = g_originalFindCell != nullptr
                ? g_originalFindCell(collector, unused, layer, cell, variant)
                : nullptr;

            const ModBrush* const owned = BrushOf(found);

            if (owned != nullptr && owned->draw == nullptr && owned->texture == AMOD_INVALID_TEXTURE)
            {
                found = nullptr;
            }

            if (found != nullptr || g_groupFind == nullptr || layer < 0 ||
                layer >= static_cast<int>(GameAddress::kBrushGroupCount))
            {
                return found;
            }

            if (g_swappedOut[layer] != nullptr)
            {
                void* const held = g_groupFind(g_swappedOut[layer], nullptr,
                    static_cast<char>(cell), static_cast<uint32_t>(variant));

                if (held != nullptr)
                {
                    return held;
                }
            }

            for (ModGroup* const group : g_modGroups)
            {
                if (group->layer != layer)
                {
                    continue;
                }

                for (void* const object : group->brushes)
                {
                    ModBrush* const brush = BrushOf(object);
                    const uint32_t mask = *reinterpret_cast<uint32_t*>(
                        static_cast<uint8_t*>(object) + GameAddress::kBrushVariantsOffset);

                    // Nothing to draw with is what keeps a paint-only brush out of the lookup,
                    // and a texture counts as something
                    const bool draws = brush != nullptr && (brush->draw != nullptr || brush->texture != AMOD_INVALID_TEXTURE);

                    if (!draws || static_cast<uint8_t*>(object)[GameAddress::kBrushCellOffset] != static_cast<uint8_t>(cell))
                    {
                        continue;
                    }
                    if (variant != 0 && (static_cast<uint32_t>(variant) & mask) == 0)
                    {
                        continue;
                    }

                    return object;
                }
            }

            return nullptr;
        }

        bool GameIsSuppressed()
        {
            return IsActive() && g_mode.suppressGame != 0;
        }

        // Every place the game's editor draws at, turned over into the loader's space
        void ResetParts()
        {
            const float rows = static_cast<float>(GameAddress::kRoomEditRows);

            g_parts[AMOD_EDITOR_PART_GRID] = { 1, GameAddress::kEditorGridX,
                GameAddress::kEditorScreenHeight - GameAddress::kEditorGridY - rows * GameAddress::kEditorGridCell,
                GameAddress::kEditorGridCell, 1.0f };
            g_parts[AMOD_EDITOR_PART_LINES] = { 1, 0.0f, 0.0f, 0.0f, 1.0f };
            g_parts[AMOD_EDITOR_PART_STATUS] = { 1, GameAddress::kEditorStatusX,
                GameAddress::kEditorScreenHeight - GameAddress::kEditorStatusY,
                GameAddress::kEditorStatusStep, 1.0f };
            g_parts[AMOD_EDITOR_PART_PALETTE] = { 1, GameAddress::kEditorPaletteX,
                GameAddress::kEditorScreenHeight - GameAddress::kEditorPaletteY,
                GameAddress::kEditorPaletteStep, 1.0f };
            g_parts[AMOD_EDITOR_PART_MAZE] = { 1, 0.0f, 0.0f, 0.0f, 1.0f };

            for (int line = 0; line < AMOD_EDITOR_STATUS_LINES; ++line)
            {
                g_statusLines[line].clear();
                g_statusMine[line] = false;
            }

            g_partsOwner = nullptr;
        }

        float* EditorZoom()
        {
            uint8_t* const editor = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kLevelEditor));

            return (editor != nullptr && Readable(editor, GameAddress::kEditorZoomOffset + sizeof(float)))
                ? reinterpret_cast<float*>(editor + GameAddress::kEditorZoomOffset)
                : nullptr;
        }

        const char* GameText(uintptr_t address)
        {
            const char* const text = static_cast<const char*>(GameModule::Resolve(address));
            return (text != nullptr && Readable(text, 1)) ? text : "";
        }

        // The editor's own text, one level below Editor_DrawText, which hardcodes the scale
        void EditorText(float x, float y, float scale, const float* colour, const char* text)
        {
            void* const font = GameModule::Resolve(GameAddress::kSystemFont);
            const float position[2] = { x, y };
            const float scaling[2] = { GameAddress::kEditorTextScaleX * scale, scale };

            if (g_editorText == nullptr || font == nullptr || text == nullptr || text[0] == '\0')
            {
                return;
            }

            g_editorText(font, nullptr, text, position, scaling, colour, 0, 0.0f);
        }

        std::string Filled(const char* format, const char* value)
        {
            const std::string text = format != nullptr ? format : "";
            const size_t at = text.find("%s");

            return (at == std::string::npos || value == nullptr)
                ? text
                : text.substr(0, at) + value + text.substr(at + 2);
        }

        const char* StatusLine(int line)
        {
            static const uintptr_t game[AMOD_EDITOR_STATUS_LINES] = {
                GameAddress::kEditorStatusBrush, GameAddress::kEditorStatusMode,
                GameAddress::kEditorStatusClear, GameAddress::kEditorStatusKeys };

            return g_statusMine[line] ? g_statusLines[line].c_str() : GameText(game[line]);
        }

        const char* BrushLabel(void* object)
        {
            void* const* const vtable = (object != nullptr && Readable(object, sizeof(void*)))
                ? reinterpret_cast<void* const*>(*static_cast<void**>(object))
                : nullptr;

            return (vtable != nullptr && Readable(vtable, (GameAddress::kBrushLabelSlot + 1) * sizeof(void*)))
                ? reinterpret_cast<BrushTextRoutine>(vtable[GameAddress::kBrushLabelSlot])(object, nullptr)
                : nullptr;
        }

        void DrawHud()
        {
            uint8_t* const collector = Collector();
            const EditorPart& status = g_parts[AMOD_EDITOR_PART_STATUS];
            const EditorPart& palette = g_parts[AMOD_EDITOR_PART_PALETTE];
            const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            const float keyColour[4] = { 1.0f, 1.0f, 0.5f, 1.0f };
            const float heldColour[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

            if (collector == nullptr)
            {
                return;
            }

            void* const brush = g_brushCurrent != nullptr ? g_brushCurrent(collector, nullptr) : nullptr;
            uint8_t* const group = g_groupCurrent != nullptr
                ? static_cast<uint8_t*>(g_groupCurrent(collector, nullptr))
                : nullptr;

            if (status.shown != 0)
            {
                const float top = GameAddress::kEditorScreenHeight - status.y;

                if (brush != nullptr)
                {
                    EditorText(status.x, top, status.scale, white, Filled(StatusLine(0), BrushLabel(brush)).c_str());
                }

                // A group's first field is its name, so the pointer is both
                EditorText(status.x, top - status.size, status.scale, white, Filled(StatusLine(1), reinterpret_cast<const char*>(group)).c_str());
                EditorText(status.x, top - status.size * 2.0f, status.scale, white, Filled(StatusLine(2), nullptr).c_str());
                EditorText(status.x, top - status.size * 3.0f, status.scale, white, Filled(StatusLine(3), nullptr).c_str());
            }

            if (palette.shown == 0 || group == nullptr || !Readable(group, 0x110))
            {
                return;
            }

            void** const list = GroupList_(group);
            const int count = GroupCount(group);
            const int current = Read<int>(group, GameAddress::kBrushGroupCurrentOffset);
            float y = GameAddress::kEditorScreenHeight - palette.y;

            for (int at = 0; list != nullptr && at < count; ++at)
            {
                void* const object = list[at];
                void* const* const vtable = (object != nullptr && Readable(object, sizeof(void*)))
                    ? reinterpret_cast<void* const*>(*static_cast<void**>(object))
                    : nullptr;
                char keyText[16] = {};

                if (vtable == nullptr || !Readable(vtable, (GameAddress::kBrushLabelSlot + 1) * sizeof(void*)))
                {
                    continue;
                }

                const char key = reinterpret_cast<BrushKeyRoutine>(vtable[GameAddress::kBrushKeySlot])(object, nullptr);

                snprintf(keyText, sizeof(keyText), GameText(GameAddress::kEditorKeyFormat),
                    key != '\0' ? key : ' ');

                // The key column sits a fixed step left of the label, and it moves with the letters
                EditorText(palette.x - GameAddress::kEditorPaletteKeyStep * palette.scale, y, palette.scale, keyColour, keyText);
                EditorText(palette.x, y, palette.scale, at == current ? heldColour : white, BrushLabel(object));

                y -= palette.size;
            }
        }

        void __fastcall RoomDrawDetour(void* room, void* unused)
        {
            const EditorPart& grid = g_parts[AMOD_EDITOR_PART_GRID];

            (void)unused;

            if (grid.shown != 0 && g_drawGrid != nullptr && grid.size > 0.0f)
            {
                const float bottom = GameAddress::kEditorScreenHeight - grid.y - static_cast<float>(GameAddress::kRoomEditRows) * grid.size;

                g_drawGrid(room, nullptr, grid.x, bottom, grid.size, g_parts[AMOD_EDITOR_PART_LINES].shown != 0 ? 1 : 0);
            }

            DrawHud();
        }

        void __fastcall EditorDrawDetour(void* editor, void* unused)
        {
            FireDraw(AMOD_EDITOR_DRAW_BACKDROP);

            if (GameIsSuppressed())
            {
                return;
            }

            const int mode = (editor != nullptr && Readable(editor, GameAddress::kEditorModeOffset + sizeof(int)))
                ? Read<int>(static_cast<uint8_t*>(editor), GameAddress::kEditorModeOffset)
                : 0;

            if (mode == 1 && g_parts[AMOD_EDITOR_PART_MAZE].shown == 0)
            {
                if (g_editorCursor != nullptr)
                {
                    g_editorCursor(editor, nullptr);
                }

                return;
            }

            if (g_originalEditorDraw != nullptr)
            {
                g_originalEditorDraw(editor, unused);
            }
        }

        bool FireKey(int key, int arg)
        {
            const std::vector<KeySub> snapshot = g_keySubs;
            bool taken = false;

            for (const KeySub& held : snapshot)
            {
                if (held.handler != nullptr)
                {
                    taken = held.handler(key, arg, held.user) != 0 || taken;
                }
            }

            return taken;
        }

        void __fastcall EditorOnKeyThunk(void* editor, void* unused, int key, int arg)
        {
            if (FireKey(key, arg) || GameIsSuppressed())
            {
                return;
            }

            if (g_originalOnKey != nullptr)
            {
                g_originalOnKey(editor, unused, key, arg);
            }
        }

        void __fastcall UpdateDetour(void* scene, void* unused, float seconds)
        {
            if (IsActive() && g_mode.stopWorld != 0)
            {
                return;
            }

            if (g_originalUpdate != nullptr)
            {
                g_originalUpdate(scene, unused, seconds);
            }
        }

        void __fastcall RenderDetour(void* scene, void* unused)
        {
            float* const drawWorld = IsActive() && g_mode.stopRender != 0
                ? CvarValue(GameAddress::kDrawWorldCvar)
                : nullptr;
            const float held = drawWorld != nullptr ? *drawWorld : 0.0f;

            if (drawWorld != nullptr)
            {
                *drawWorld = 0.0f;
            }

            if (g_originalRender != nullptr)
            {
                g_originalRender(scene, unused);
            }

            if (drawWorld != nullptr)
            {
                *drawWorld = held;
            }

            if (IsActive())
            {
                FireDraw(AMOD_EDITOR_DRAW_OVERLAY);
            }
        }

        void __fastcall DrawObjectsDetour(void* scene, void* unused)
        {
            if (g_originalObjects != nullptr)
            {
                g_originalObjects(scene, unused);
            }

            if (IsActive())
            {
                FireDraw(AMOD_EDITOR_DRAW_WORLD);
            }
        }

        void TakeMode(const AmodEditorMode* mode)
        {
            g_mode = AmodEditorMode{};
            g_mode.structSize = sizeof(g_mode);

            if (mode != nullptr)
            {
                g_mode.stopWorld = mode->stopWorld;
                g_mode.stopRender = mode->stopRender;

                if (mode->structSize >= sizeof(AmodEditorMode))
                {
                    g_mode.stopInput = mode->stopInput;
                    g_mode.suppressGame = mode->suppressGame;
                }
            }
        }

        int __cdecl ApiEnter(AmodMod mod, const AmodEditorMode* mode)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Enter wants a mod");
            }
            if (g_owner != nullptr && g_owner != caller)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "another mod is holding the editor");
            }

            g_owner = caller;
            TakeMode(mode);
            return 1;
        }

        int __cdecl ApiLeave(AmodMod mod)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || g_owner != caller)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "this mod is not holding the editor");
            }

            g_owner = nullptr;
            g_mode = AmodEditorMode{};

            // Input_Update would clear it itself once no key is held, but not while one is
            if (g_input != nullptr)
            {
                g_input[GameAddress::kInputSwallowingOffset] = 0;
            }

            return 1;
        }

        int __cdecl ApiIsActive(AmodMod mod)
        {
            return (Caller(mod) != nullptr && g_owner == Caller(mod)) ? 1 : 0;
        }

        int __cdecl ApiGetMode(AmodMod mod, AmodEditorMode* out)
        {
            if (Caller(mod) == nullptr || out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetMode wants somewhere to answer");
            }

            AmodEditorMode filled = g_mode;
            filled.structSize = sizeof(filled);

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetMode(AmodMod mod, const AmodEditorMode* mode)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || g_owner != caller)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "this mod is not holding the editor");
            }
            if (mode == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetMode wants a mode");
            }

            TakeMode(mode);
            return 1;
        }

        AmodToken __cdecl ApiOnFrame(AmodMod mod, AmodEditorFrameHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || handler == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "OnFrame wants a handler");
                return AMOD_INVALID_TOKEN;
            }

            g_frameSubs.push_back(FrameSub{ caller, g_nextToken, handler, user });
            return g_nextToken++;
        }

        AmodToken __cdecl ApiOnDraw(AmodMod mod, AmodEditorDrawHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || handler == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "OnDraw wants a handler");
                return AMOD_INVALID_TOKEN;
            }

            g_drawSubs.push_back(DrawSub{ caller, g_nextToken, AMOD_EDITOR_DRAW_OVERLAY, handler, user });
            return g_nextToken++;
        }

        AmodToken __cdecl ApiOnLevelWrite(AmodMod mod, AmodLevelWriteHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || handler == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "OnLevelWrite wants a handler");
                return AMOD_INVALID_TOKEN;
            }

            g_writeSubs.push_back(WriteSub{ caller, g_nextToken, handler, user });
            return g_nextToken++;
        }

        int __cdecl ApiSaveLevel(AmodMod mod, const char* path, int overwrite, int compile)
        {
            if (Caller(mod) == nullptr || path == nullptr || path[0] == '\0')
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SaveLevel wants a path");
            }

            return WriteLevel(path, overwrite != 0, compile != 0)
                ? 1
                : Fail(mod, AMOD_FAIL_REFUSED, "'%s' would not be written", path);
        }

        AmodToken __cdecl ApiOnRoomLoaded(AmodMod mod, AmodRoomLoadedHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || handler == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "OnRoomLoaded wants a handler");
                return AMOD_INVALID_TOKEN;
            }

            g_loadedSubs.push_back(LoadedSub{ caller, g_nextToken, handler, user });
            return g_nextToken++;
        }

        AmodToken __cdecl ApiOnRoomCreated(AmodMod mod, AmodRoomCreatedHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || handler == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "OnRoomCreated wants a handler");
                return AMOD_INVALID_TOKEN;
            }

            g_roomSubs.push_back(RoomSub{ caller, g_nextToken, handler, user });
            return g_nextToken++;
        }

        AmodToken __cdecl ApiOnDrawPhase(AmodMod mod, int phase, AmodEditorDrawHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || handler == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "OnDraw wants a handler");
                return AMOD_INVALID_TOKEN;
            }
            if (phase != AMOD_EDITOR_DRAW_WORLD && phase != AMOD_EDITOR_DRAW_OVERLAY && phase != AMOD_EDITOR_DRAW_BACKDROP)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "there is no draw phase %d", phase);
                return AMOD_INVALID_TOKEN;
            }

            g_drawSubs.push_back(DrawSub{ caller, g_nextToken, phase, handler, user });
            return g_nextToken++;
        }

        void __cdecl ApiCancel(AmodMod mod, AmodToken token)
        {
            ModContext* const caller = Caller(mod);

            for (auto it = g_frameSubs.begin(); it != g_frameSubs.end(); ++it)
            {
                if (it->owner == caller && it->token == token)
                {
                    g_frameSubs.erase(it);
                    return;
                }
            }

            for (auto it = g_drawSubs.begin(); it != g_drawSubs.end(); ++it)
            {
                if (it->owner == caller && it->token == token)
                {
                    g_drawSubs.erase(it);
                    return;
                }
            }

            for (auto it = g_keySubs.begin(); it != g_keySubs.end(); ++it)
            {
                if (it->owner == caller && it->token == token)
                {
                    g_keySubs.erase(it);
                    return;
                }
            }

            for (auto it = g_roomSubs.begin(); it != g_roomSubs.end(); ++it)
            {
                if (it->owner == caller && it->token == token)
                {
                    g_roomSubs.erase(it);
                    return;
                }
            }

            for (auto it = g_loadedSubs.begin(); it != g_loadedSubs.end(); ++it)
            {
                if (it->owner == caller && it->token == token)
                {
                    g_loadedSubs.erase(it);
                    return;
                }
            }

            for (auto it = g_writeSubs.begin(); it != g_writeSubs.end(); ++it)
            {
                if (it->owner == caller && it->token == token)
                {
                    g_writeSubs.erase(it);
                    return;
                }
            }

            for (auto it = g_roomWriteSubs.begin(); it != g_roomWriteSubs.end(); ++it)
            {
                if (it->owner == caller && it->token == token)
                {
                    g_roomWriteSubs.erase(it);
                    return;
                }
            }
        }

        bool HoldGroupList(int group, uint8_t* at)
        {
            GroupList& held = g_groups[group];

            if (held.original == nullptr)
            {
                held.original = GroupList_(at);
                held.originalCount = GroupCount(at);

                void** const from = static_cast<void**>(held.original);

                for (int index = 0; index < held.originalCount; ++index)
                {
                    held.held.push_back(from[index]);
                }
            }

            return true;
        }

        void PublishGroup(int group, uint8_t* at)
        {
            GroupList& held = g_groups[group];
            void* const list = held.held.empty() ? nullptr : static_cast<void*>(held.held.data());
            const int count = static_cast<int>(held.held.size());

            memcpy(at + GameAddress::kBrushGroupListOffset, &list, sizeof(list));
            memcpy(at + GameAddress::kBrushGroupCountOffset, &count, sizeof(count));

            const int current = Read<int>(at, GameAddress::kBrushGroupCurrentOffset);

            if (current >= count)
            {
                const int reset = 0;
                memcpy(at + GameAddress::kBrushGroupCurrentOffset, &reset, sizeof(reset));
            }
        }

        void DropBrushFromGroup(ModBrush* brush)
        {
            ModGroup* const mine = ModGroupAt(brush->group);
            std::vector<void*>& list = mine != nullptr ? mine->brushes : g_groups[brush->group].held;

            for (auto held = list.begin(); held != list.end(); ++held)
            {
                if (*held == brush->object.data())
                {
                    list.erase(held);
                    break;
                }
            }

            if (mine != nullptr)
            {
                PublishModGroup(mine);
                return;
            }

            uint8_t* const group = Group(brush->group);

            if (group != nullptr)
            {
                PublishGroup(brush->group, group);
            }
        }

        int __cdecl ApiAddGroup(AmodMod mod, const char* name, int layer)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || name == nullptr || name[0] == '\0')
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "AddGroup wants a name");
            }
            if (layer < 0 || layer >= static_cast<int>(GameAddress::kBrushGroupCount))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT,
                    "a group paints one of the two grids, and %d is neither", layer);
            }

            ModGroup* const group = new ModGroup();
            group->owner = caller;
            group->tab = TabCount();
            group->layer = layer;
            group->name = name;
            group->object.assign(0x110, 0);

            uint8_t* const at = group->object.data();
            strncpy_s(reinterpret_cast<char*>(at + GameAddress::kBrushGroupNameOffset), GameAddress::kBrushLabelSize, group->name.c_str(), 
                GameAddress::kBrushLabelSize - 1);
            memcpy(at + GameAddress::kBrushGroupIndexOffset, &layer, sizeof(layer));

            g_modGroups.push_back(group);
            PublishModGroup(group);

            return group->tab;
        }

        int __cdecl ApiRemoveGroup(AmodMod mod, int tab)
        {
            ModContext* const caller = Caller(mod);

            for (auto it = g_modGroups.begin(); it != g_modGroups.end(); ++it)
            {
                ModGroup* const group = *it;

                if (group->owner != caller || group->tab != tab)
                {
                    continue;
                }

                // Its tab may be the one showing, and its brushes point into it
                if (g_tab == tab)
                {
                    g_tab = 0;
                    ApplyTab();
                }

                for (auto brush = g_brushes.begin(); brush != g_brushes.end();)
                {
                    if ((*brush)->group == tab)
                    {
                        delete *brush;
                        brush = g_brushes.erase(brush);
                    }
                    else
                    {
                        ++brush;
                    }
                }

                g_modGroups.erase(it);
                delete group;

                // The tabs after it close up, and so does anything pointing at them
                for (size_t at = 0; at < g_modGroups.size(); ++at)
                {
                    const int was = g_modGroups[at]->tab;
                    const int now = static_cast<int>(GameAddress::kBrushGroupCount) + static_cast<int>(at);

                    for (ModBrush* const held : g_brushes)
                    {
                        held->group = held->group == was ? now : held->group;
                    }

                    g_modGroups[at]->tab = now;
                }

                g_tab = g_tab < TabCount() ? g_tab : 0;
                ApplyTab();
                return 1;
            }

            return Fail(mod, AMOD_FAIL_NO_SUCH, "this mod added no group %d", tab);
        }

        int __cdecl ApiGroupCount(AmodMod mod)
        {
            return Caller(mod) != nullptr ? TabCount() : 0;
        }

        const char* __cdecl ApiGroupName(AmodMod mod, int tab)
        {
            const uint8_t* const group = Caller(mod) != nullptr ? Group(tab) : nullptr;

            g_brushText[0] = '\0';

            if (group != nullptr)
            {
                strncpy_s(g_brushText, sizeof(g_brushText), reinterpret_cast<const char*>(group + GameAddress::kBrushGroupNameOffset), sizeof(g_brushText) - 1);
            }

            return g_brushText;
        }

        int __cdecl ApiGroupLayer(AmodMod mod, int tab)
        {
            ModGroup* const mine = Caller(mod) != nullptr ? ModGroupAt(tab) : nullptr;

            if (mine != nullptr)
            {
                return mine->layer;
            }

            return (tab >= 0 && tab < static_cast<int>(GameAddress::kBrushGroupCount)) ? tab : -1;
        }

        int __cdecl ApiAddBrush(AmodMod mod, const AmodBrushDesc* desc)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || desc == nullptr || desc->label == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "AddBrush wants a label");
            }
            if (desc->group < 0 || desc->group >= TabCount())
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "there is no brush group %d", desc->group);
            }

            uint8_t* const group = Group(desc->group);
            ModGroup* const mine = ModGroupAt(desc->group);

            if (group == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the brush palette is not up yet");
            }
            if (mine == nullptr)
            {
                HoldGroupList(desc->group, group);
            }

            // Both do the same job and the handler wins, so a texture given beside one is dead
            if (desc->draw != nullptr && desc->texture != AMOD_INVALID_TEXTURE)
            {
                FORGE_LOG_WARN("Brush '%s' gave a draw handler and a texture; the handler draws and "
                    "the texture is ignored", desc->label);
            }

            ModBrush* const brush = new ModBrush();
            brush->owner = caller;
            brush->handle = g_nextBrush++;
            brush->group = desc->group;
            brush->label = desc->label;
            brush->draw = desc->draw;
            brush->paint = desc->paint;
            brush->save = desc->structSize >= sizeof(AmodBrushDesc) ? desc->save : nullptr;
            brush->texture = desc->texture;
            brush->user = desc->user;
            brush->object.assign(GameAddress::kBrushSize, 0);

            brush->slots[0] = reinterpret_cast<void*>(&BrushDrawThunk);
            brush->slots[1] = reinterpret_cast<void*>(&BrushCellThunk);
            brush->slots[2] = reinterpret_cast<void*>(&BrushKeyThunk);
            brush->slots[3] = reinterpret_cast<void*>(&BrushLabelThunk);
            brush->slots[4] = reinterpret_cast<void*>(&BrushVariantsThunk);
            brush->slots[5] = reinterpret_cast<void*>(&BrushPaintThunk);
            brush->slots[6] = reinterpret_cast<void*>(&BrushDeleteThunk);
            brush->slots[7] = reinterpret_cast<void*>(&BrushSaveThunk);
            brush->slots[8] = reinterpret_cast<void*>(&BrushLoadThunk);

            uint8_t* const object = brush->object.data();
            void* const table = brush->slots;

            memcpy(object, &table, sizeof(table));

            // The field holds what the game means by a texture, not what a mod means
            void* const skin = TextureApi::TextureFor(desc->texture);
            memcpy(object + GameAddress::kBrushTextureOffset, &skin, sizeof(skin));

            // +0x04 is what the save filters placements on: 0 keeps the brush out of every file
            const int kind = desc->structSize >= sizeof(AmodBrushDesc) ? desc->kind : 0;
            memcpy(object + GameAddress::kBrushKindOffset, &kind, sizeof(kind));

            // A variant of 0 is only ever found when the caller wants no particular tiling, and
            // the geometry grid always asks for one - so a brush with no mask never draws there
            const uint32_t variants = (desc->structSize >= sizeof(AmodBrushDesc) && desc->variants != 0)
                ? static_cast<uint32_t>(desc->variants)
                : 0xFFFFFFFFu;

            memcpy(object + GameAddress::kBrushVariantsOffset, &variants, sizeof(variants));

            const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            memcpy(object + GameAddress::kBrushColourOffset, white, sizeof(white));

            object[GameAddress::kBrushKeyOffset] = static_cast<uint8_t>(desc->key != 0 ? desc->key : desc->cell);
            object[GameAddress::kBrushCellOffset] = static_cast<uint8_t>(desc->cell);

            strncpy_s(reinterpret_cast<char*>(object + GameAddress::kBrushLabelOffset), GameAddress::kBrushLabelSize, brush->label.c_str(), 
                GameAddress::kBrushLabelSize - 1);

            g_brushes.push_back(brush);

            if (mine != nullptr)
            {
                mine->brushes.push_back(object);
                PublishModGroup(mine);
            }
            else
            {
                g_groups[desc->group].held.push_back(object);
                PublishGroup(desc->group, group);
            }

            return brush->handle;
        }

        int __cdecl ApiRemoveBrush(AmodMod mod, int handle)
        {
            ModContext* const caller = Caller(mod);

            for (auto it = g_brushes.begin(); it != g_brushes.end(); ++it)
            {
                ModBrush* const brush = *it;

                if (brush->owner != caller || brush->handle != handle)
                {
                    continue;
                }

                DropBrushFromGroup(brush);
                g_brushes.erase(it);
                delete brush;
                return 1;
            }

            return Fail(mod, AMOD_FAIL_NO_SUCH, "this mod added no brush %d", handle);
        }

        int __cdecl ApiBrushCount(AmodMod mod, int group)
        {
            return Caller(mod) != nullptr ? GroupCount(Group(group)) : 0;
        }

        int __cdecl ApiGetBrush(AmodMod mod, int group, int index, AmodBrushInfo* out)
        {
            uint8_t* const at = Caller(mod) != nullptr ? Group(group) : nullptr;
            void** const list = GroupList_(at);

            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetBrush wants somewhere to answer");
            }
            if (list == nullptr || index < 0 || index >= GroupCount(at))
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "group %d has no brush %d", group, index);
            }

            void* const object = list[index];
            void** const slots = object != nullptr ? *static_cast<void***>(object) : nullptr;

            if (slots == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "group %d has no brush %d", group, index);
            }

            // Through its own vtable, because the game's brushes and a mod's answer differently
            typedef char(__fastcall * ByteRoutine)(void*, void*);
            typedef const char*(__fastcall * TextRoutine)(void*, void*);

            const char* const label = reinterpret_cast<TextRoutine>(slots[3])(object, nullptr);

            g_brushText[0] = '\0';
            strncpy_s(g_brushText, sizeof(g_brushText), label != nullptr ? label : "", sizeof(g_brushText) - 1);

            AmodBrushInfo filled = {};
            filled.structSize = sizeof(filled);
            filled.label = g_brushText;
            filled.group = group;
            filled.cell = static_cast<unsigned char>(reinterpret_cast<ByteRoutine>(slots[1])(object, nullptr));
            filled.key = static_cast<unsigned char>(reinterpret_cast<ByteRoutine>(slots[2])(object, nullptr));
            filled.isMine = BrushOf(object) != nullptr && BrushOf(object)->owner == Caller(mod) ? 1 : 0;

            if (Readable(object, GameAddress::kBrushColourOffset + 16))
            {
                float rgba[4] = {};

                memcpy(rgba, static_cast<uint8_t*>(object) + GameAddress::kBrushColourOffset, sizeof(rgba));
                filled.colour =
                    (static_cast<uint32_t>(rgba[3] * 255.0f) << 24) |
                    (static_cast<uint32_t>(rgba[0] * 255.0f) << 16) |
                    (static_cast<uint32_t>(rgba[1] * 255.0f) << 8) |
                    static_cast<uint32_t>(rgba[2] * 255.0f);
            }

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        // Puts back whichever halves of a game brush a mod took
        void RestoreBrush(const BrushSnapshot& held)
        {
            if (held.object == nullptr || !Readable(held.object, GameAddress::kBrushColourOffset + 16))
            {
                return;
            }
            if (held.labelTaken)
            {
                strncpy_s(static_cast<char*>(held.object) + GameAddress::kBrushLabelOffset, GameAddress::kBrushLabelSize, held.label.c_str(), 
                    GameAddress::kBrushLabelSize - 1);
            }
            if (held.colourTaken)
            {
                memcpy(static_cast<uint8_t*>(held.object) + GameAddress::kBrushColourOffset, held.colour, sizeof(held.colour));
            }
        }

        // The one snapshot per brush, made the first time either half is taken
        BrushSnapshot* SnapshotOf(ModContext* owner, void* object)
        {
            for (BrushSnapshot& held : g_brushEdits)
            {
                if (held.object == object)
                {
                    held.owner = owner;
                    return &held;
                }
            }

            BrushSnapshot made = {};

            made.owner = owner;
            made.object = object;
            made.label = static_cast<const char*>(object) + GameAddress::kBrushLabelOffset;
            memcpy(made.colour, static_cast<uint8_t*>(object) + GameAddress::kBrushColourOffset, sizeof(made.colour));

            g_brushEdits.push_back(made);
            return &g_brushEdits.back();
        }

        void ForgetSnapshot(void* object)
        {
            for (auto it = g_brushEdits.begin(); it != g_brushEdits.end(); ++it)
            {
                if (it->object == object && !it->labelTaken && !it->colourTaken)
                {
                    g_brushEdits.erase(it);
                    return;
                }
            }
        }

        void* BrushAt(AmodMod mod, int group, int index)
        {
            uint8_t* const at = Caller(mod) != nullptr ? Group(group) : nullptr;
            void** const list = GroupList_(at);

            if (list == nullptr || index < 0 || index >= GroupCount(at))
            {
                return nullptr;
            }

            void* const object = list[index];
            return (object != nullptr && Readable(object, GameAddress::kBrushColourOffset + 16))
                ? object
                : nullptr;
        }

        int __cdecl ApiSetBrushLabel(AmodMod mod, int group, int index, const char* label)
        {
            ModContext* const caller = Caller(mod);
            void* const object = BrushAt(mod, group, index);

            if (caller == nullptr || object == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "group %d has no brush %d", group, index);
            }

            ModBrush* const mine = BrushOf(object);

            // A brush of the mod's own answers out of its own record, and the loader owns that
            if (mine != nullptr)
            {
                if (label == nullptr)
                {
                    return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a mod's own brush has no name to go back to");
                }

                mine->label = label;
                return 1;
            }

            BrushSnapshot* const held = SnapshotOf(caller, object);
            char* const buffer = static_cast<char*>(object) + GameAddress::kBrushLabelOffset;

            if (label == nullptr)
            {
                RestoreBrush(BrushSnapshot{ caller, object, held->label, {}, true, false });
                held->labelTaken = false;
                ForgetSnapshot(object);
                return 1;
            }

            held->labelTaken = true;
            strncpy_s(buffer, GameAddress::kBrushLabelSize, label, GameAddress::kBrushLabelSize - 1);

            void** const slots = *static_cast<void***>(object);
            const char* const now = reinterpret_cast<BrushTextRoutine>(slots[GameAddress::kBrushLabelSlot])(object, nullptr);

            return (now != nullptr && strcmp(now, label) == 0)
                ? 1
                : Fail(mod, AMOD_FAIL_REFUSED, "brush %d of group %d does not read its name from its record", index, group);
        }

        // Slot [0] sets the device colour from these four floats before it blits, so this reaches
        // the palette and the painted cells alike
        int __cdecl ApiSetBrushColour(AmodMod mod, int group, int index, uint32_t colour)
        {
            ModContext* const caller = Caller(mod);
            void* const object = BrushAt(mod, group, index);

            if (caller == nullptr || object == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "group %d has no brush %d", group, index);
            }

            BrushSnapshot* const held = SnapshotOf(caller, object);

            if (colour == 0)
            {
                BrushSnapshot back = *held;

                back.labelTaken = false;
                back.colourTaken = true;
                RestoreBrush(back);

                held->colourTaken = false;
                ForgetSnapshot(object);
                return 1;
            }

            const float rgba[4] =
            {
                ((colour >> 16) & 0xFF) / 255.0f,
                ((colour >> 8) & 0xFF) / 255.0f,
                (colour & 0xFF) / 255.0f,
                ((colour >> 24) & 0xFF) / 255.0f,
            };

            held->colourTaken = true;
            memcpy(static_cast<uint8_t*>(object) + GameAddress::kBrushColourOffset, rgba, sizeof(rgba));
            return 1;
        }

        void RestoreIcons()
        {
            uint8_t* const editor = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kLevelEditor));

            if (editor == nullptr || !Readable(editor, GameAddress::kEditorNoRoomTexOffset + sizeof(void*)))
            {
                return;
            }

            const uintptr_t offsets[2] = { GameAddress::kEditorCursorTexOffset, GameAddress::kEditorNoRoomTexOffset };

            for (int icon = 0; icon < 2; ++icon)
            {
                if (g_iconTaken[icon])
                {
                    *reinterpret_cast<void**>(editor + offsets[icon]) = g_iconWas[icon];
                    g_iconTaken[icon] = false;
                }
            }

            g_iconOwner = nullptr;
        }

        int __cdecl ApiSetIcon(AmodMod mod, int icon, AmodTextureId texture)
        {
            ModContext* const caller = Caller(mod);
            uint8_t* const editor = caller != nullptr
                ? static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kLevelEditor))
                : nullptr;

            if (editor == nullptr || !Readable(editor, GameAddress::kEditorNoRoomTexOffset + sizeof(void*)))
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the editor is not up yet");
            }
            if (icon != AMOD_EDITOR_ICON_CURSOR && icon != AMOD_EDITOR_ICON_NO_ROOM)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "the editor has no icon %d", icon);
            }

            const uintptr_t offset = icon == AMOD_EDITOR_ICON_CURSOR
                ? GameAddress::kEditorCursorTexOffset
                : GameAddress::kEditorNoRoomTexOffset;
            void** const slot = reinterpret_cast<void**>(editor + offset);

            if (!g_iconTaken[icon])
            {
                g_iconWas[icon] = *slot;
            }
            if (texture == AMOD_INVALID_TEXTURE)
            {
                *slot = g_iconWas[icon];
                g_iconTaken[icon] = false;
                return 1;
            }

            void* const skin = TextureApi::TextureFor(texture);

            if (skin == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no texture %d", static_cast<int>(texture));
            }

            *slot = skin;
            g_iconTaken[icon] = true;
            g_iconOwner = caller;
            return 1;
        }

        int __cdecl ApiClearRoom(AmodMod mod, int x, int y)
        {
            uint8_t* const room = Caller(mod) != nullptr ? RoomAt(x, y) : nullptr;
            const RoomClearRoutine clear = reinterpret_cast<RoomClearRoutine>(GameModule::Resolve(GameAddress::kRoomEditClear));

            if (room == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no room at %d,%d", x, y);
            }
            if (clear == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_OFFERED, "this build cannot empty a room");
            }

            clear(room, nullptr);
            return 1;
        }

        AmodToken __cdecl ApiOnRoomWrite(AmodMod mod, AmodRoomWriteHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || handler == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "OnRoomWrite wants a handler");
                return AMOD_INVALID_TOKEN;
            }

            g_roomWriteSubs.push_back(RoomWriteSub{ caller, g_nextToken, handler, user });
            return g_nextToken++;
        }

        AmodToken __cdecl ApiOnKey(AmodMod mod, AmodEditorKeyHandler handler, void* user)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || handler == nullptr)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "OnKey wants a handler");
                return AMOD_INVALID_TOKEN;
            }

            g_keySubs.push_back(KeySub{ caller, g_nextToken, handler, user });
            return g_nextToken++;
        }

        int __cdecl ApiGetScreen(AmodMod mod)
        {
            uint8_t* const editor = Caller(mod) != nullptr
                ? static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kLevelEditor))
                : nullptr;

            return (editor != nullptr && Readable(editor, GameAddress::kEditorModeOffset + sizeof(int)))
                ? Read<int>(editor, GameAddress::kEditorModeOffset)
                : -1;
        }

        int __cdecl ApiSetScreen(AmodMod mod, int screen)
        {
            uint8_t* const editor = Caller(mod) != nullptr
                ? static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kLevelEditor))
                : nullptr;

            if (editor == nullptr || !Readable(editor, GameAddress::kEditorModeOffset + sizeof(int)))
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the editor is not up yet");
            }
            if (screen != AMOD_EDITOR_SCREEN_ROOM && screen != AMOD_EDITOR_SCREEN_MAZE)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "there is no editor screen %d", screen);
            }

            // What TAB walks between; the editor reads it every frame and nothing else caches it
            memcpy(editor + GameAddress::kEditorModeOffset, &screen, sizeof(screen));
            return 1;
        }

        int __cdecl ApiGetCurrentBrush(AmodMod mod, int* group, int* index)
        {
            uint8_t* const collector = Caller(mod) != nullptr ? Collector() : nullptr;

            if (collector == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the brush palette is not up yet");
            }

            // g_tab is the answer, not the collector's index: a mod's tab shows there as the
            // layer it borrowed
            const int which = ModGroupAt(g_tab) != nullptr
                ? g_tab
                : Read<int>(collector, GameAddress::kBrushCurrentGroupOffset);
            uint8_t* const at = Group(which);

            if (group != nullptr)
            {
                *group = which;
            }
            if (index != nullptr)
            {
                *index = at != nullptr ? Read<int>(at, GameAddress::kBrushGroupCurrentOffset) : -1;
            }

            return at != nullptr ? 1 : 0;
        }

        int __cdecl ApiSetCurrentBrush(AmodMod mod, int group, int index)
        {
            uint8_t* const collector = Caller(mod) != nullptr ? Collector() : nullptr;
            uint8_t* const at = Group(group);

            if (collector == nullptr || at == nullptr || group < 0 || group >= TabCount())
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no brush group %d", group);
            }
            if (index < 0 || index >= GroupCount(at))
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "group %d has no brush %d", group, index);
            }

            g_tab = group;
            ApplyTab();

            memcpy(at + GameAddress::kBrushGroupCurrentOffset, &index, sizeof(index));
            return 1;
        }

        int __cdecl ApiFindBrushByCell(AmodMod mod, int group, int cell)
        {
            uint8_t* const at = Caller(mod) != nullptr ? Group(group) : nullptr;
            void** const list = GroupList_(at);
            const int count = GroupCount(at);

            typedef char(__fastcall * ByteRoutine)(void*, void*);

            for (int index = 0; index < count && list != nullptr; ++index)
            {
                void* const object = list[index];
                void** const slots = object != nullptr ? *static_cast<void***>(object) : nullptr;

                if (slots != nullptr && static_cast<unsigned char>(reinterpret_cast<ByteRoutine>(slots[1])(object, nullptr)) == static_cast<unsigned char>(cell))
                {
                    return index;
                }
            }

            return -1;
        }

        int __cdecl ApiGetPart(AmodMod mod, int part, AmodEditorPart* out)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }
            if (part < 0 || part >= static_cast<int>(kEditorPartCount) || out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d is not a part of the editor", part);
            }

            AmodEditorPart filled = {};

            filled.structSize = sizeof(filled);
            filled.shown = g_parts[part].shown;
            filled.x = g_parts[part].x;
            filled.y = g_parts[part].y;
            filled.size = g_parts[part].size;
            filled.scale = g_parts[part].scale;

            float* const zoom = part == AMOD_EDITOR_PART_MAZE ? EditorZoom() : nullptr;

            if (zoom != nullptr)
            {
                filled.size = *zoom;
            }

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetPart(AmodMod mod, int part, const AmodEditorPart* place)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (part < 0 || part >= static_cast<int>(kEditorPartCount) || place == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d is not a part of the editor", part);
            }
            if (place->structSize < sizeof(AmodEditorPart))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "the part was not filled in");
            }

            g_parts[part].shown = place->shown;
            g_parts[part].x = place->x;
            g_parts[part].y = place->y;

            // A size of zero keeps the one it has, so a mod that only wanted to move it does not
            // collapse the grid to nothing
            if (place->size > 0.0f)
            {
                g_parts[part].size = place->size;
            }
            if (place->scale > 0.0f)
            {
                g_parts[part].scale = place->scale;
            }

            float* const zoom = part == AMOD_EDITOR_PART_MAZE ? EditorZoom() : nullptr;

            if (zoom != nullptr && place->size > 0.0f)
            {
                *zoom = place->size < GameAddress::kEditorZoomMin ? GameAddress::kEditorZoomMin
                    : (place->size > GameAddress::kEditorZoomMax ? GameAddress::kEditorZoomMax : place->size);
            }

            g_partsOwner = caller;
            return 1;
        }

        const char* __cdecl ApiGetStatusLine(AmodMod mod, int line)
        {
            if (Caller(mod) == nullptr)
            {
                return "";
            }
            if (line < 0 || line >= AMOD_EDITOR_STATUS_LINES)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "the status has no line %d", line);
                return "";
            }

            return StatusLine(line);
        }

        int __cdecl ApiSetStatusLine(AmodMod mod, int line, const char* text)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr)
            {
                return 0;
            }
            if (line < 0 || line >= AMOD_EDITOR_STATUS_LINES)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "the status has no line %d", line);
            }

            g_statusMine[line] = text != nullptr;
            g_statusLines[line] = text != nullptr ? text : "";

            g_partsOwner = caller;
            return 1;
        }

        int __cdecl ApiIsGameEditorOn(AmodMod mod)
        {
            const float* const value = Caller(mod) != nullptr ? CvarValue(GameAddress::kEdEnableCvar) : nullptr;
            return (value != nullptr && *value != 0.0f) ? 1 : 0;
        }

        int __cdecl ApiSetGameEditor(AmodMod mod, int on)
        {
            float* const value = Caller(mod) != nullptr ? CvarValue(GameAddress::kEdEnableCvar) : nullptr;

            if (value == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "ed_enable is not registered yet");
            }

            *value = on != 0 ? 1.0f : 0.0f;
            return 1;
        }
    }

    bool LevelEditorApi::Install()
    {
        void* const updateTarget = GameModule::Resolve(GameAddress::kEntitiesUpdate);
        void* const renderTarget = GameModule::Resolve(GameAddress::kSceneRenderWorld);
        std::string mismatch;
        std::string error;

        if (updateTarget == nullptr || renderTarget == nullptr)
        {
            FORGE_LOG_WARN("The editor seams were not located, so the editor interface is not offered");
            return false;
        }

        g_delta = static_cast<const float*>(GameModule::Resolve(GameAddress::kFrameDelta));
        g_input = static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kInput));
        g_editorCursor = reinterpret_cast<EditorCursorRoutine>(GameModule::Resolve(GameAddress::kLevelEditorDrawCursor));
        g_groupFind = reinterpret_cast<GroupFindRoutine>(GameModule::Resolve(GameAddress::kBrushGroupFind));
        g_fopen = reinterpret_cast<GameFopenRoutine>(GameModule::Resolve(GameAddress::kGameFopen));
        g_fclose = reinterpret_cast<GameFcloseRoutine>(GameModule::Resolve(GameAddress::kGameFclose));
        g_fprintf = reinterpret_cast<GameFprintfRoutine>(GameModule::Resolve(GameAddress::kGameFprintf));
        g_drawGrid = reinterpret_cast<DrawGridRoutine>(GameModule::Resolve(GameAddress::kRoomEditDrawGrid));
        g_editorText = reinterpret_cast<EditorTextRoutine>(GameModule::Resolve(GameAddress::kFontDrawText));
        g_brushCurrent = reinterpret_cast<CollectorPickRoutine>(GameModule::Resolve(GameAddress::kBrushCurrent));
        g_groupCurrent = reinterpret_cast<CollectorPickRoutine>(GameModule::Resolve(GameAddress::kBrushGroupCurrentName));

        ResetParts();

        if (!GameModule::VerifyBytes(updateTarget, kEntitiesUpdatePrologue, sizeof(kEntitiesUpdatePrologue), mismatch) ||
            !g_updateHook.Install(updateTarget, reinterpret_cast<void*>(&UpdateDetour), kEntitiesUpdatePrologue, sizeof(kEntitiesUpdatePrologue), error))
        {
            FORGE_LOG_ERROR("Cannot hook Entities_Update: %s", error.empty() ? mismatch.c_str() : error.c_str());
            return false;
        }

        g_originalUpdate = reinterpret_cast<EntitiesUpdateRoutine>(g_updateHook.Trampoline());

        if (!GameModule::VerifyBytes(renderTarget, kRenderWorldPrologue, sizeof(kRenderWorldPrologue), mismatch) ||
            !g_renderHook.Install(renderTarget, reinterpret_cast<void*>(&RenderDetour), kRenderWorldPrologue, sizeof(kRenderWorldPrologue), error))
        {
            FORGE_LOG_ERROR("Cannot hook Scene_RenderWorld: %s", error.empty() ? mismatch.c_str() : error.c_str());
            g_updateHook.Uninstall();
            g_originalUpdate = nullptr;
            return false;
        }

        g_originalRender = reinterpret_cast<RenderWorldRoutine>(g_renderHook.Trampoline());

        // Both of these only carry suppressGame, so neither is worth refusing the interface over
        void* const editorDrawTarget = GameModule::Resolve(GameAddress::kLevelEditorDraw);

        if (editorDrawTarget != nullptr && GameModule::VerifyBytes(editorDrawTarget, kEditorDrawPrologue, sizeof(kEditorDrawPrologue), mismatch) &&
            g_editorDrawHook.Install(editorDrawTarget, reinterpret_cast<void*>(&EditorDrawDetour), kEditorDrawPrologue, sizeof(kEditorDrawPrologue), error))
        {
            g_originalEditorDraw = reinterpret_cast<EditorDrawRoutine>(g_editorDrawHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("LevelEditor_Draw is not hooked, so the game's own editor keeps drawing: %s", error.empty() ? mismatch.c_str() : error.c_str());
        }

        // The room's own file, and the name buffer the compile reads it back through
        void* const saveRoomTarget = GameModule::Resolve(GameAddress::kRoomEditSaveRoomFile);
        void* const nameAssignTarget = GameModule::Resolve(GameAddress::kNameBufferAssign);

        if (saveRoomTarget != nullptr && nameAssignTarget != nullptr &&
            GameModule::VerifyBytes(saveRoomTarget, kSaveRoomFilePrologue, sizeof(kSaveRoomFilePrologue), mismatch) &&
            GameModule::VerifyBytes(nameAssignTarget, kNameAssignPrologue, sizeof(kNameAssignPrologue), mismatch) &&

            g_saveRoomHook.Install(saveRoomTarget, reinterpret_cast<void*>(&SaveRoomFileDetour),
                kSaveRoomFilePrologue, sizeof(kSaveRoomFilePrologue), error) &&
            g_nameAssignHook.Install(nameAssignTarget, reinterpret_cast<void*>(&NameAssignDetour),
                kNameAssignPrologue, sizeof(kNameAssignPrologue), error))
        {
            g_originalSaveRoomFile = reinterpret_cast<SaveRoomFileRoutine>(g_saveRoomHook.Trampoline());
            g_originalNameAssign = reinterpret_cast<NameAssignRoutine>(g_nameAssignHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("RoomEdit_SaveRoomFile is not hooked, so a room keeps the game's own fields: %s", error.empty() ? mismatch.c_str() : error.c_str());
            g_saveRoomHook.Uninstall();
            g_nameAssignHook.Uninstall();
        }

        // Not a feature: the game's own F3 crashes without it, and so does anything else that grows
        // the maze by more than one row.
        void* const growRowsTarget = GameModule::Resolve(GameAddress::kMazeEditGrowRows);

        if (growRowsTarget != nullptr && GameModule::VerifyBytes(growRowsTarget, kGrowRowsPrologue, sizeof(kGrowRowsPrologue), mismatch) &&
            g_growRowsHook.Install(growRowsTarget, reinterpret_cast<void*>(&GrowRowsDetour), kGrowRowsPrologue, sizeof(kGrowRowsPrologue), error))
        {
            g_originalGrowRows = reinterpret_cast<GrowRowsRoutine>(g_growRowsHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("MazeEdit_GrowRows is not hooked, so loading a taller level still crashes: %s",
                error.empty() ? mismatch.c_str() : error.c_str());
        }

        // The room screen. Without it the game's editor draws its own way and no part can move.
        void* const roomDrawTarget = GameModule::Resolve(GameAddress::kRoomEditDraw);

        if (roomDrawTarget == nullptr || g_drawGrid == nullptr || g_editorText == nullptr ||
            !GameModule::VerifyBytes(roomDrawTarget, kRoomDrawPrologue, sizeof(kRoomDrawPrologue), mismatch) ||
            !g_roomDrawHook.Install(roomDrawTarget, reinterpret_cast<void*>(&RoomDrawDetour), kRoomDrawPrologue, sizeof(kRoomDrawPrologue), error))
        {
            FORGE_LOG_WARN("RoomEdit_Draw is not hooked, so the editor's own pieces cannot be moved: %s",
                error.empty() ? mismatch.c_str() : error.c_str());
        }

        void* const objectsTarget = GameModule::Resolve(GameAddress::kSceneDrawObjects);

        if (objectsTarget != nullptr && GameModule::VerifyBytes(objectsTarget, kDrawObjectsPrologue, sizeof(kDrawObjectsPrologue), mismatch) &&
            g_objectsHook.Install(objectsTarget, reinterpret_cast<void*>(&DrawObjectsDetour), kDrawObjectsPrologue, sizeof(kDrawObjectsPrologue), error))
        {
            g_originalObjects = reinterpret_cast<DrawObjectsRoutine>(g_objectsHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("Scene_DrawObjects is not hooked, so there is no world draw phase: %s",
                error.empty() ? mismatch.c_str() : error.c_str());
        }

        // Both carry groups of a mod's own; without them the palette simply stays the game's two
        void* const groupNextTarget = GameModule::Resolve(GameAddress::kBrushGroupNext);

        if (groupNextTarget != nullptr && GameModule::VerifyBytes(groupNextTarget, kGroupNextPrologue, sizeof(kGroupNextPrologue), mismatch) &&
            g_groupNextHook.Install(groupNextTarget, reinterpret_cast<void*>(&GroupNextDetour), kGroupNextPrologue, sizeof(kGroupNextPrologue), error))
        {
            g_originalGroupNext = reinterpret_cast<GroupNextRoutine>(g_groupNextHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("BrushGroup_Next is not hooked, so a mod cannot add a brush group: %s",
                error.empty() ? mismatch.c_str() : error.c_str());
        }

        void* const findCellTarget = GameModule::Resolve(GameAddress::kBrushFindByCell);

        if (findCellTarget != nullptr && GameModule::VerifyBytes(findCellTarget, kFindByCellPrologue, sizeof(kFindByCellPrologue), mismatch) &&
            g_findCellHook.Install(findCellTarget, reinterpret_cast<void*>(&FindByCellDetour), kFindByCellPrologue, sizeof(kFindByCellPrologue), error))
        {
            g_originalFindCell = reinterpret_cast<FindByCellRoutine>(g_findCellHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("Brush_FindByCell is not hooked, so cells a mod's group painted will not draw: %s",
                error.empty() ? mismatch.c_str() : error.c_str());
        }

        g_saveRoomFile = reinterpret_cast<SaveRoomFileRoutine>(GameModule::Resolve(GameAddress::kRoomEditSaveRoomFile));
        g_writePlacements = reinterpret_cast<WritePlacementsRoutine>(GameModule::Resolve(GameAddress::kRoomEditWritePlacements));

        void* const writeLayerTarget = GameModule::Resolve(GameAddress::kRoomEditWriteLayer);
        void* const readLayerTarget = GameModule::Resolve(GameAddress::kRoomEditReadLayer);

        if (writeLayerTarget != nullptr && GameModule::VerifyBytes(writeLayerTarget, kLayerPrologue, sizeof(kLayerPrologue), mismatch) &&
            g_writeLayerHook.Install(writeLayerTarget, reinterpret_cast<void*>(&WriteLayerDetour), kLayerPrologue, sizeof(kLayerPrologue), error))
        {
            g_originalWriteLayer = reinterpret_cast<LayerRoutine>(g_writeLayerHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("RoomEdit_WriteLayer is not hooked, so a mod's group writes no blocks: %s",
                error.empty() ? mismatch.c_str() : error.c_str());
        }

        if (readLayerTarget != nullptr && GameModule::VerifyBytes(readLayerTarget, kLayerPrologue, sizeof(kLayerPrologue), mismatch) &&
            g_readLayerHook.Install(readLayerTarget, reinterpret_cast<void*>(&ReadLayerDetour), kLayerPrologue, sizeof(kLayerPrologue), error))
        {
            g_originalReadLayer = reinterpret_cast<LayerRoutine>(g_readLayerHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("RoomEdit_ReadLayer is not hooked, so a mod's group reads no blocks: %s",
                error.empty() ? mismatch.c_str() : error.c_str());
        }

        void* const loadRoomTarget = GameModule::Resolve(GameAddress::kRoomEditLoadRoomFile);

        if (loadRoomTarget != nullptr && GameModule::VerifyBytes(loadRoomTarget, kLoadRoomFilePrologue, sizeof(kLoadRoomFilePrologue), mismatch) &&
            g_loadRoomHook.Install(loadRoomTarget, reinterpret_cast<void*>(&LoadRoomFileDetour), kLoadRoomFilePrologue, sizeof(kLoadRoomFilePrologue), error))
        {
            g_originalLoadRoom = reinterpret_cast<LoadRoomFileRoutine>(g_loadRoomHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("RoomEdit_LoadRoomFile is not hooked, so no mod hears a room being read: %s",
                error.empty() ? mismatch.c_str() : error.c_str());
        }

        void* const saveLevelTarget = GameModule::Resolve(GameAddress::kLevelEditorSaveLevel);

        if (saveLevelTarget != nullptr && GameModule::VerifyBytes(saveLevelTarget, kSaveLevelPrologue, sizeof(kSaveLevelPrologue), mismatch) &&
            g_saveLevelHook.Install(saveLevelTarget, reinterpret_cast<void*>(&SaveLevelDetour), kSaveLevelPrologue, sizeof(kSaveLevelPrologue), error))
        {
            g_originalSaveLevel = reinterpret_cast<SaveLevelRoutine>(g_saveLevelHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("LevelEditor_SaveLevelFile is not hooked, so F2 keeps the game's own writer: %s",
                error.empty() ? mismatch.c_str() : error.c_str());
        }

        void* const roomPlaceTarget = GameModule::Resolve(GameAddress::kRoomEditSetPlace);

        if (roomPlaceTarget != nullptr && GameModule::VerifyBytes(roomPlaceTarget, kRoomEditSetPlacePrologue, sizeof(kRoomEditSetPlacePrologue), mismatch) &&
            g_roomPlaceHook.Install(roomPlaceTarget, reinterpret_cast<void*>(&RoomEditSetPlaceDetour), kRoomEditSetPlacePrologue, sizeof(kRoomEditSetPlacePrologue), error))
        {
            g_originalRoomPlace = reinterpret_cast<RoomEditSetPlaceRoutine>(g_roomPlaceHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("A room's placing is not hooked, so no mod hears a room being made: %s",
                error.empty() ? mismatch.c_str() : error.c_str());
        }

        void* const roomInitTarget = GameModule::Resolve(GameAddress::kRoomEditInit);

        if (roomInitTarget != nullptr && GameModule::VerifyBytes(roomInitTarget, kRoomEditInitPrologue, sizeof(kRoomEditInitPrologue), mismatch) &&
            g_roomInitHook.Install(roomInitTarget, reinterpret_cast<void*>(&RoomEditInitDetour), kRoomEditInitPrologue, sizeof(kRoomEditInitPrologue), error))
        {
            g_originalRoomInit = reinterpret_cast<RoomEditInitRoutine>(g_roomInitHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("CRoomEdit's constructor is not hooked, so no mod hears a room being made: %s",
                error.empty() ? mismatch.c_str() : error.c_str());
        }

        void** const keySlot = static_cast<void**>(GameModule::Resolve(GameAddress::kLevelEditorOnKeySlot));
        DWORD guard = 0;

        if (keySlot != nullptr && VirtualProtect(keySlot, sizeof(void*), PAGE_READWRITE, &guard))
        {
            g_originalOnKey = reinterpret_cast<EditorOnKeyRoutine>(*keySlot);
            *keySlot = reinterpret_cast<void*>(&EditorOnKeyThunk);
            VirtualProtect(keySlot, sizeof(void*), guard, &guard);
            g_onKeySlot = keySlot;
        }
        else
        {
            FORGE_LOG_WARN("The editor's key slot is not patched, so the game's own editor keeps its keys");
        }

        g_table = AmodLevelEditorApi{};
        g_table.structSize = sizeof(g_table);
        g_table.Enter = &ApiEnter;
        g_table.Leave = &ApiLeave;
        g_table.IsActive = &ApiIsActive;
        g_table.GetMode = &ApiGetMode;
        g_table.SetMode = &ApiSetMode;
        g_table.OnFrame = &ApiOnFrame;
        g_table.OnDraw = &ApiOnDraw;
        g_table.Cancel = &ApiCancel;
        g_table.IsGameEditorOn = &ApiIsGameEditorOn;
        g_table.SetGameEditor = &ApiSetGameEditor;
        g_table.AddBrush = &ApiAddBrush;
        g_table.RemoveBrush = &ApiRemoveBrush;
        g_table.BrushCount = &ApiBrushCount;
        g_table.GetBrush = &ApiGetBrush;
        g_table.GetCurrentBrush = &ApiGetCurrentBrush;
        g_table.SetCurrentBrush = &ApiSetCurrentBrush;
        g_table.FindBrushByCell = &ApiFindBrushByCell;
        g_table.OnDrawPhase = &ApiOnDrawPhase;
        g_table.AddGroup = &ApiAddGroup;
        g_table.RemoveGroup = &ApiRemoveGroup;
        g_table.GroupCount = &ApiGroupCount;
        g_table.GroupName = &ApiGroupName;
        g_table.GroupLayer = &ApiGroupLayer;
        g_table.GetGridCell = &ApiGetGridCell;
        g_table.SetGridCell = &ApiSetGridCell;
        g_table.GridSize = &ApiGridSize;
        g_table.OnRoomCreated = &ApiOnRoomCreated;
        g_table.OnRoomLoaded = &ApiOnRoomLoaded;
        g_table.OnLevelWrite = &ApiOnLevelWrite;
        g_table.SaveLevel = &ApiSaveLevel;
        g_table.MazeSize = &ApiMazeSize;
        g_table.GetMazeCursor = &ApiGetMazeCursor;
        g_table.SetMazeCursor = &ApiSetMazeCursor;
        g_table.HasRoom = &ApiHasRoom;
        g_table.CreateRoom = &ApiCreateRoom;
        g_table.RemoveRoom = &ApiRemoveRoom;
        g_table.InsertRow = &ApiInsertRow;
        g_table.InsertColumn = &ApiInsertColumn;
        g_table.LoadRoom = &ApiLoadRoom;
        g_table.SaveRoom = &ApiSaveRoom;
        g_table.GetPart = &ApiGetPart;
        g_table.SetPart = &ApiSetPart;
        g_table.GetStatusLine = &ApiGetStatusLine;
        g_table.SetStatusLine = &ApiSetStatusLine;
        g_table.SetBrushLabel = &ApiSetBrushLabel;
        g_table.GetScreen = &ApiGetScreen;
        g_table.SetScreen = &ApiSetScreen;
        g_table.OnKey = &ApiOnKey;
        g_table.SetBrushColour = &ApiSetBrushColour;
        g_table.SetIcon = &ApiSetIcon;
        g_table.ClearRoom = &ApiClearRoom;
        g_table.OnRoomWrite = &ApiOnRoomWrite;

        FrameApi::AddInternalTick(&EditorTick);

        FORGE_LOG_INFO("Level editor interface installed");
        return true;
    }

    void LevelEditorApi::Uninstall()
    {
        if (g_updateHook.IsInstalled())
        {
            g_updateHook.Uninstall();
        }
        if (g_renderHook.IsInstalled())
        {
            g_renderHook.Uninstall();
        }
        if (g_editorDrawHook.IsInstalled())
        {
            g_editorDrawHook.Uninstall();
        }
        if (g_roomDrawHook.IsInstalled())
        {
            g_roomDrawHook.Uninstall();
        }

        for (const BrushSnapshot& held : g_brushEdits)
        {
            RestoreBrush(held);
        }

        g_brushEdits.clear();
        g_keySubs.clear();
        g_roomWriteSubs.clear();
        g_roomWriteRoom = nullptr;
        g_roomWritePath.clear();
        RestoreIcons();

        if (g_growRowsHook.IsInstalled())
        {
            g_growRowsHook.Uninstall();
        }
        if (g_saveRoomHook.IsInstalled())
        {
            g_saveRoomHook.Uninstall();
        }
        if (g_nameAssignHook.IsInstalled())
        {
            g_nameAssignHook.Uninstall();
        }

        g_originalGrowRows = nullptr;
        g_originalSaveRoomFile = nullptr;
        g_originalNameAssign = nullptr;

        ResetParts();
        g_drawGrid = nullptr;
        g_editorText = nullptr;
        g_brushCurrent = nullptr;
        g_groupCurrent = nullptr;

        if (g_objectsHook.IsInstalled())
        {
            g_objectsHook.Uninstall();
        }

        g_tab = 0;
        ApplyTab();

        if (g_groupNextHook.IsInstalled())
        {
            g_groupNextHook.Uninstall();
        }
        if (g_findCellHook.IsInstalled())
        {
            g_findCellHook.Uninstall();
        }
        if (g_roomInitHook.IsInstalled())
        {
            g_roomInitHook.Uninstall();
        }
        if (g_roomPlaceHook.IsInstalled())
        {
            g_roomPlaceHook.Uninstall();
        }

        if (g_saveLevelHook.IsInstalled())
        {
            g_saveLevelHook.Uninstall();
        }
        if (g_loadRoomHook.IsInstalled())
        {
            g_loadRoomHook.Uninstall();
        }
        if (g_writeLayerHook.IsInstalled())
        {
            g_writeLayerHook.Uninstall();
        }
        if (g_readLayerHook.IsInstalled())
        {
            g_readLayerHook.Uninstall();
        }

        g_originalWriteLayer = nullptr;
        g_originalReadLayer = nullptr;
        g_originalLoadRoom = nullptr;
        g_loadedSubs.clear();

        g_originalRoomInit = nullptr;
        g_originalRoomPlace = nullptr;
        g_originalSaveLevel = nullptr;
        g_saveRoomFile = nullptr;
        g_writePlacements = nullptr;
        g_newRoom = nullptr;
        g_cellsOverride = nullptr;
        g_cellsLayer = -1;
        g_writeSubs.clear();
        g_roomOverride = nullptr;
        g_roomSubs.clear();

        for (ModGroup* const group : g_modGroups)
        {
            delete group;
        }

        g_modGroups.clear();
        g_originalGroupNext = nullptr;
        g_originalFindCell = nullptr;
        g_groupFind = nullptr;

        // The thunk goes with the DLL, so the slot cannot be left pointing at it
        if (g_onKeySlot != nullptr && g_originalOnKey != nullptr)
        {
            DWORD guard = 0;

            if (VirtualProtect(g_onKeySlot, sizeof(void*), PAGE_READWRITE, &guard))
            {
                *g_onKeySlot = reinterpret_cast<void*>(g_originalOnKey);
                VirtualProtect(g_onKeySlot, sizeof(void*), guard, &guard);
            }
        }

        for (int which = 0; which < static_cast<int>(GameAddress::kBrushGroupCount); ++which)
        {
            uint8_t* const group = Group(which);

            if (group != nullptr && g_groups[which].original != nullptr)
            {
                memcpy(group + GameAddress::kBrushGroupListOffset, &g_groups[which].original, sizeof(void*));
                memcpy(group + GameAddress::kBrushGroupCountOffset, &g_groups[which].originalCount, sizeof(int));

                const int reset = 0;
                memcpy(group + GameAddress::kBrushGroupCurrentOffset, &reset, sizeof(reset));
            }

            g_groups[which] = GroupList{};
        }

        for (ModBrush* const brush : g_brushes)
        {
            delete brush;
        }

        g_brushes.clear();

        g_onKeySlot = nullptr;
        g_originalOnKey = nullptr;
        g_originalEditorDraw = nullptr;
        g_originalObjects = nullptr;
        g_editorCursor = nullptr;

        g_originalUpdate = nullptr;
        g_originalRender = nullptr;
        g_delta = nullptr;
        g_input = nullptr;
        g_owner = nullptr;
        g_mode = AmodEditorMode{};
        g_frameSubs.clear();
        g_drawSubs.clear();
        g_table = AmodLevelEditorApi{};
    }

    void LevelEditorApi::ForgetMod(ModContext* owner)
    {
        // A mod that goes while holding the editor would leave the world stopped for good
        if (g_owner == owner)
        {
            g_owner = nullptr;
            g_mode = AmodEditorMode{};

            if (g_input != nullptr)
            {
                g_input[GameAddress::kInputSwallowingOffset] = 0;
            }
        }

        // Its pieces are the game's editor, not the mod's, so they go back where the game had them
        if (g_partsOwner == owner)
        {
            ResetParts();
        }

        for (auto it = g_brushEdits.begin(); it != g_brushEdits.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            RestoreBrush(*it);
            it = g_brushEdits.erase(it);
        }

        for (auto it = g_keySubs.begin(); it != g_keySubs.end();)
        {
            it = it->owner == owner ? g_keySubs.erase(it) : ++it;
        }

        for (auto it = g_roomWriteSubs.begin(); it != g_roomWriteSubs.end();)
        {
            it = it->owner == owner ? g_roomWriteSubs.erase(it) : ++it;
        }

        if (g_iconOwner == owner)
        {
            RestoreIcons();
        }

        for (auto it = g_frameSubs.begin(); it != g_frameSubs.end();)
        {
            it = it->owner == owner ? g_frameSubs.erase(it) : ++it;
        }

        for (auto it = g_drawSubs.begin(); it != g_drawSubs.end();)
        {
            it = it->owner == owner ? g_drawSubs.erase(it) : ++it;
        }

        for (auto it = g_roomSubs.begin(); it != g_roomSubs.end();)
        {
            it = it->owner == owner ? g_roomSubs.erase(it) : ++it;
        }

        for (auto it = g_loadedSubs.begin(); it != g_loadedSubs.end();)
        {
            it = it->owner == owner ? g_loadedSubs.erase(it) : ++it;
        }

        for (auto it = g_writeSubs.begin(); it != g_writeSubs.end();)
        {
            it = it->owner == owner ? g_writeSubs.erase(it) : ++it;
        }

        // Its brushes point at callbacks in a DLL about to go
        for (auto it = g_brushes.begin(); it != g_brushes.end();)
        {
            ModBrush* const brush = *it;

            if (brush->owner != owner)
            {
                ++it;
                continue;
            }

            DropBrushFromGroup(brush);
            it = g_brushes.erase(it);
            delete brush;
        }

        // And its groups, which the tabs after them close up over
        for (auto it = g_modGroups.begin(); it != g_modGroups.end();)
        {
            if ((*it)->owner != owner)
            {
                ++it;
                continue;
            }

            delete *it;
            it = g_modGroups.erase(it);
        }

        for (size_t at = 0; at < g_modGroups.size(); ++at)
        {
            const int was = g_modGroups[at]->tab;
            const int now = static_cast<int>(GameAddress::kBrushGroupCount) + static_cast<int>(at);

            for (ModBrush* const held : g_brushes)
            {
                held->group = held->group == was ? now : held->group;
            }

            g_modGroups[at]->tab = now;
        }

        g_tab = g_tab < TabCount() ? g_tab : 0;
        ApplyTab();
    }

    const AmodLevelEditorApi* LevelEditorApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
