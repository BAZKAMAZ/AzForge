#pragma once
#include <stdint.h>

namespace Forge
{
    namespace GameAddress
    {
        constexpr uintptr_t kEdEnableCvar = 0x00517770; // node pointer; the value is a float at +0x0C
        constexpr uintptr_t kEdDefWallCvar = 0x00517774;
        constexpr uintptr_t kEdDefFloorCvar = 0x00517778;
        constexpr uintptr_t kCvarTextOffset = 0x14; // ed_def_wall / ed_def_floor keep a string here

        constexpr uintptr_t kLevelEditor = 0x0051777C; // CLevelEditor, static
        constexpr uintptr_t kLevelEditorVtable = 0x004CB2BC;
        constexpr uintptr_t kEditorEditOffset = 0x04; // CMazeEdit
        constexpr uintptr_t kEditorModeOffset = 0x08; // 0 room, 1 maze
        constexpr uintptr_t kEditorZoomOffset = 0x0C; // maze screen only; + and - walk it
        constexpr float kEditorZoomMin = 0.1f;
        constexpr float kEditorZoomMax = 2.0f;
        constexpr uintptr_t kEditorCursorTexOffset = 0x10; // textures/editor/cursor.png
        constexpr uintptr_t kEditorNoRoomTexOffset = 0x14; // textures/editor/no_room.png

        // The frame is either the editor's or the game's, never both. LevelEditor_Frame is a stub.
        constexpr uintptr_t kLevelEditorFrameGate = 0x0041D540; // app vtable slot at 0x004CBBCC
        constexpr uintptr_t kLevelEditorFrame = 0x00419FA0;
        constexpr uintptr_t kGameFrameSlot = 0x005176F4;

        constexpr uintptr_t kLevelEditorDraw = 0x0041A5A0; // __thiscall, one call site at 0x0040C22E
        constexpr uintptr_t kLevelEditorOnKey = 0x0041B170; // __thiscall (editor, key, arg); arg is never read
        constexpr uintptr_t kLevelEditorOnKeySlot = 0x004CB2C0; // vtable slot [1], reached only through it
        constexpr uintptr_t kLevelEditorDrawCursor = 0x0041A070; // __thiscall (editor)

        // Not the live room: this is what the editor paints and what RoomEdit_SaveRoomFile writes.
        constexpr uintptr_t kLevelEditorActiveEdit = 0x0041A100; // __thiscall (editor) -> CRoomEdit
        constexpr uintptr_t kRoomEditGridOffset = 0x004;
        constexpr size_t kRoomEditLayerStride = 0x488;
        constexpr int kRoomEditColumns = 0x28;
        constexpr int kRoomEditRows = 0x1D;
        constexpr uintptr_t kRoomEditXOffset = 0x918; // where in the maze this room sits
        constexpr uintptr_t kRoomEditYOffset = 0x91C;
        constexpr size_t kRoomEditSize = 0x920;
        constexpr uintptr_t kRoomEditInit = 0x0041FBE0; // __thiscall; every room is made here
        constexpr uintptr_t kRoomEditClear = 0x0041FAB0; // __fastcall (room); both grids, then a wall border
        constexpr uintptr_t kRoomEditSetPlace = 0x0041FAF0; // __thiscall (room, x, y)
        constexpr size_t kRoomEditDeleteSlot = 6; // deleting destructor, __thiscall (flags), RET 4

        constexpr uintptr_t kMazeRowsOffset = 0x04; // rows[y][x] is a CRoomEdit* or null
        constexpr uintptr_t kMazeWidthOffset = 0x08;
        constexpr uintptr_t kMazeHeightOffset = 0x0C;
        constexpr uintptr_t kMazeCursorXOffset = 0x10;
        constexpr uintptr_t kMazeCursorYOffset = 0x14;

        // Bounds-checked; makes the room when the cell is empty and moves the cursor either way.
        constexpr uintptr_t kMazeEditGoTo = 0x0041DA10; // __thiscall (maze, x, y), RET 8
        // Frees whatever room is there through vtable slot [6].
        constexpr uintptr_t kMazeEditRoomFromFile = 0x0041DB10; // __thiscall (maze, x, y, path, 0, 0, 0), RET 0x18
        constexpr uintptr_t kMazeEditInsertRow = 0x0041DC40; // __thiscall (maze, at), RET 4
        constexpr uintptr_t kMazeEditInsertColumn = 0x0041DCC0;
        // Adds count to the height but allocates only the LAST row; anything above one leaves nulls
        // that MazeEdit_RestampPlaces then walks into.
        constexpr uintptr_t kMazeEditGrowRows = 0x0041D860; // __thiscall (maze, count), RET 4
        constexpr uintptr_t kMazeEditRestampPlaces = 0x0041D9A0;

        constexpr uintptr_t kLevelEditorSaveLevel = 0x0041A860; // __stdcall (path, overwrite, run), RET 0xC
        constexpr uintptr_t kRoomEditSaveRoomFile = 0x0041FF30; // __thiscall (room, path, compile)
        // The compile half reads the .txt back off disk, right after NameBuffer_Assign(path).
        constexpr uintptr_t kRoomEditLoadRoomFile = 0x004202B0; // __thiscall (room, path), RET 4
        constexpr uintptr_t kRoomEditWritePlacements = 0x0041F880; // __thiscall (room, kind, FILE*)
        // Walks collector[group] and nothing else, so a group of a mod's own is invisible to both
        // unless it is showing.
        constexpr uintptr_t kRoomEditWriteLayer = 0x0041F310; // __thiscall (collector, cells, group, kind, file, x, y), RET 0x18
        constexpr uintptr_t kRoomEditReadLayer = 0x0041F380;
        constexpr int kPlacementKindLevel = 4; // written into the level file, not a sidecar

        // A FILE* belongs to the runtime that opened it, and the game has its own CRT.
        constexpr uintptr_t kGameFopen = 0x00461D4A; // __cdecl (path, mode) -> FILE*
        constexpr uintptr_t kGameFclose = 0x004602BC; // __cdecl (FILE*)
        constexpr uintptr_t kGameFprintf = 0x00461AD9; // __cdecl (FILE*, format, ...)

        // Two groups because the room editor holds two grids.
        constexpr uintptr_t kBrushCollector = 0x00679800; // pointer to {group0, group1, currentGroup}
        constexpr size_t kBrushGroupCount = 2;
        constexpr uintptr_t kBrushCurrentGroupOffset = 0x08; // 'M' walks it, wrapping at 2
        constexpr uintptr_t kBrushGroupNameOffset = 0x000; // char[0x100]
        constexpr uintptr_t kBrushGroupIndexOffset = 0x100;
        constexpr uintptr_t kBrushGroupListOffset = 0x104; // Brush**
        constexpr uintptr_t kBrushGroupCountOffset = 0x108;
        constexpr uintptr_t kBrushGroupCurrentOffset = 0x10C; // 'B' walks it, wrapping at the count

        // CBrushBase's constructor 0x00415C10 writes every one of these.
        constexpr size_t kBrushSize = 0x128;
        constexpr uintptr_t kBrushKindOffset = 0x004; // the placement file the layer walks pick on
        constexpr int kBrushKindNone = 0;
        constexpr int kBrushKindCtrl = 1;
        constexpr int kBrushKindBonus = 2;
        constexpr int kBrushKindMonster = 3;
        constexpr uintptr_t kBrushVariantsOffset = 0x008; // a MASK - Brush_FindByCell ANDs it
        constexpr uintptr_t kBrushKeyOffset = 0x00C;
        constexpr uintptr_t kBrushLabelOffset = 0x010; // char[0x100]
        constexpr size_t kBrushLabelSize = 0x100;
        constexpr uintptr_t kBrushCellOffset = 0x110;
        constexpr uintptr_t kBrushTextureOffset = 0x114;
        constexpr uintptr_t kBrushColourOffset = 0x118; // four floats, all 1.0

        // Slot [0] is the cell draw; Erase overrides it to nothing.
        constexpr size_t kBrushVtableSlots = 9;
        constexpr size_t kBrushKeySlot = 2;
        constexpr size_t kBrushLabelSlot = 3;
        constexpr uintptr_t kBrushCurrent = 0x0041F250; // __thiscall (collector) -> brush
        constexpr uintptr_t kBrushFindByCell = 0x0041F270; // __thiscall (collector, layer, cell, variant)
        constexpr uintptr_t kBrushGroupFind = 0x0041F0B0; // __thiscall (group, cell, variant)
        constexpr uintptr_t kBrushGroupNext = 0x0041F2D0; // __thiscall (collector), wraps at 2
        constexpr uintptr_t kBrushGroupCurrentName = 0x0041F2F0; // __thiscall (collector) -> group, and its name
        constexpr uintptr_t kBrushDrawPalette = 0x0041F2B0; // __thiscall (collector, x, y)

        // The room screen is one call: the grid, then the HUD. Every place inside is a constant.
        constexpr uintptr_t kRoomEditDraw = 0x0041FEC0; // __thiscall (room)
        constexpr uintptr_t kRoomEditDrawGrid = 0x0041FC20; // __thiscall (room, x, y, cell, lines)
        constexpr uintptr_t kRoomEditDrawHud = 0x0041F9D0;
        constexpr float kEditorGridX = 20.0f;
        constexpr float kEditorGridY = 10.0f;
        constexpr float kEditorGridCell = 12.0f;

        // This space measures y from the BOTTOM, where Render and the menus measure from the top.
        constexpr float kEditorScreenHeight = 480.0f;

        constexpr uintptr_t kEditorDrawText = 0x0040CDC0; // __cdecl (x, y, colour, format, ...); hardcodes the scale

        // __thiscall (font, text, pos[2], scale[2], colour[4], length, fixedWidth), RET 0x18.
        constexpr uintptr_t kEditorTextDraw = 0x004253F0;
        constexpr float kEditorTextScaleX = 0.5f; // the y half of the pair is 1.0

        constexpr float kEditorStatusX = 5.0f;
        constexpr float kEditorStatusY = 460.0f;
        constexpr float kEditorStatusStep = 15.0f;
        constexpr uintptr_t kEditorStatusBrush = 0x004CC11C; // "Current Brush (B): '%s'"
        constexpr uintptr_t kEditorStatusMode = 0x004CC0F4; // "Current Brush Mode (M): '%s'"
        constexpr uintptr_t kEditorStatusClear = 0x004CC0E0; // "Clear Room (C)"
        constexpr uintptr_t kEditorStatusKeys = 0x004CC0AC; // "[F3] load temp level, ..."

        // BrushGroup_DrawPalette 0x0041F150, __thiscall (group, x, y); the row step is inside it.
        constexpr float kEditorPaletteX = 530.0f;
        constexpr float kEditorPaletteY = 460.0f;
        constexpr float kEditorPaletteStep = 15.0f;
        constexpr float kEditorPaletteKeyStep = 30.0f; // the "[K]" sits this far left of the label
        constexpr uintptr_t kEditorKeyFormat = 0x004CBFC0; // "[%c]"

        constexpr uintptr_t kSceneRenderWorld = 0x0040BE50; // __thiscall (scene)
        constexpr uintptr_t kAppUpdateFrame = 0x0041D450; // __stdcall (seconds), RET 4
        constexpr uintptr_t kEntitiesScene = 0x00517754; // what it calls Entities_Update on
        constexpr uintptr_t kDrawWorldCvar = 0x00503544; // r_draw_world
    }
}
