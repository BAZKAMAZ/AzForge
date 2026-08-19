#ifndef AZFORGE_LEVEL_EDITOR_API_H
#define AZFORGE_LEVEL_EDITOR_API_H

#include "ForgeTypes.h"
#include "TextureApi.h"         /* AmodTextureId, for what a brush's cells are drawn with */

#ifdef __cplusplus
extern "C" {
#endif

/* What stops while a mod's editor is up. Nothing stops unless the mod asks. */
typedef struct AmodEditorMode
{
    uint32_t structSize;

    int stopWorld;          /* entities do not update, so nothing moves */
    int stopRender;         /* the 3D world is not drawn */
    int stopInput;          /* the game stops acting on keys; the mod still reads them through Input */
    int suppressGame;       /* the game's own editor neither draws nor takes keys */

} AmodEditorMode;

AMOD_LAST_FIELD(AmodEditorMode, suppressGame, 0x10);

/* Once a frame while the mod's editor is up, where the world would have updated. */
typedef void(__cdecl* AmodEditorFrameHandler)(float seconds, void* user);

/* Where the world would have been drawn. 2D is the 640x480 menu space, as in Render. */
typedef void(__cdecl* AmodEditorDrawHandler)(void* user);

#define AMOD_EDITOR_DRAW_WORLD                                      0 /* in the world render; stopRender takes it away */
#define AMOD_EDITOR_DRAW_OVERLAY                                    1 /* after everything, in 640x480 */
#define AMOD_EDITOR_DRAW_BACKDROP                                   2 /* under the game's editor, so only with ed_enable on */

/* The pieces the game's editor draws. Unloading the mod that moved one puts it back. */
#define AMOD_EDITOR_PART_GRID                                       0 /* the room's painted cells, 40 by 29 */
#define AMOD_EDITOR_PART_LINES                                      1 /* the blue grid and the numbers; only `shown` */
#define AMOD_EDITOR_PART_STATUS                                     2 /* the four lines of text at the top left */
#define AMOD_EDITOR_PART_PALETTE                                    3 /* the brush list down the right */
#define AMOD_EDITOR_PART_MAZE                                       4 /* the maze screen's rooms; its cursor stays */

/* Where a piece is drawn. y is measured from the TOP, as Render measures it. */
typedef struct AmodEditorPart
{
    uint32_t structSize;

    int shown;
    float x;                /* GRID its top left corner; STATUS and PALETTE their first line */
    float y;
    float size;             /* GRID one cell, 12; STATUS and PALETTE the step to the next line, 15;
                               MAZE the screen's zoom, 0.1 to 2 */
    float scale;            /* STATUS and PALETTE: the letters, 1 as the game draws them */

} AmodEditorPart;

AMOD_LAST_FIELD(AmodEditorPart, scale, 0x14);

/* Top to bottom: the held brush, the group, "Clear Room (C)", the F2/F3 keys. */
#define AMOD_EDITOR_STATUS_LINES                                    4

/* What TAB walks between. */
#define AMOD_EDITOR_SCREEN_ROOM                                     0
#define AMOD_EDITOR_SCREEN_MAZE                                     1

#define AMOD_EDITOR_ICON_CURSOR                                     0 /* the mouse, on both screens */
#define AMOD_EDITOR_ICON_NO_ROOM                                    1 /* an empty maze cell; maze screen only */

/* A key reached the game's editor, before it acts. Return non-zero to take it. `arg` is the
   editor's second parameter and nothing reads it. */
typedef int(__cdecl* AmodEditorKeyHandler)(int key, int arg, void* user);

/* The two grids the room editor paints, and the palette group that fills each. */
#define AMOD_BRUSH_GEOMETRY                                         0
#define AMOD_BRUSH_ENTITIES                                         1

/* Where a cell of this brush is drawn, in the grid's own space. Null leaves it to `texture`. */
typedef void(__cdecl* AmodBrushDrawHandler)(float x, float y, float size, void* user);

/* The cell was painted. Return zero to leave the grid alone and do the placing yourself. */
typedef int(__cdecl* AmodBrushPaintHandler)(int column, int row, void* user);

/* Which of the room's three placement files a brush belongs to. */
#define AMOD_BRUSH_KIND_NONE                                        0 /* in none of them; only paints cells */
#define AMOD_BRUSH_KIND_CTRL                                        1
#define AMOD_BRUSH_KIND_BONUS                                       2
#define AMOD_BRUSH_KIND_MONSTER                                     3

/* That file is being written. Return this brush's blocks, or null for nothing; read the grid to
   decide what to emit. */
typedef const char* (__cdecl* AmodBrushSaveHandler)(int roomX, int roomY, void* user);

/* A room was made in the maze and is empty. The grid calls answer for it while the handler runs. */
typedef void(__cdecl* AmodRoomCreatedHandler)(int x, int y, void* user);

/* A room read back from disk. Its geometry is already in the grid; monsters and bonuses are not. */
typedef struct AmodRoomFiles
{
    uint32_t structSize;

    int x;
    int y;

    const char* room;       /* the .txt just read */
    const char* monsters;
    const char* bonuses;

} AmodRoomFiles;

AMOD_LAST_FIELD(AmodRoomFiles, bonuses, 0x14);

/* Parse the file with Data::ParseFile and put the cells back with SetGridCell - the grid answers
   for the room being loaded while the handler runs. */
typedef void(__cdecl* AmodRoomLoadedHandler)(const AmodRoomFiles* files, void* user);

#define AMOD_LEVEL_HEADER                                           0 /* inside $header */
#define AMOD_LEVEL_ROOM                                             1 /* inside one $room */
#define AMOD_LEVEL_TAIL                                             2 /* after $maze */

typedef struct AmodLevelPart
{
    uint32_t structSize;

    int part;               /* AMOD_LEVEL_* */
    int id;                 /* on AMOD_LEVEL_ROOM: which room, and where it sits */
    int x;
    int y;

    const char* rooms;      /* the directory its files went to; what the sidecar paths must name */

} AmodLevelPart;

AMOD_LAST_FIELD(AmodLevelPart, rooms, 0x14);

/* Return the text of that piece, or null to leave the loader's own. $id and $room_file are written
   either way. AMOD_LEVEL_TAIL is where $player_start goes; the parser refuses a level without one
   and the loader will not invent it. */
typedef const char* (__cdecl* AmodLevelWriteHandler)(const AmodLevelPart* part, void* user);

#define AMOD_ROOM_HEADER                                            0 /* before $geometry */
#define AMOD_ROOM_STATICS                                           1 /* inside $statics */
#define AMOD_ROOM_DEFORM                                            2 /* inside $y_deform */

typedef struct AmodRoomPart
{
    uint32_t structSize;

    int part;               /* AMOD_ROOM_* */
    int x;
    int y;

    const char* path;       /* the file being written */

} AmodRoomPart;

AMOD_LAST_FIELD(AmodRoomPart, path, 0x10);

/* Return the text of that piece, or null to leave the game's own. The header holds $back_wall,
   $light_ambient, the two texture scales and the ladder and chain flags. The geometry is the grid
   and the loader writes it. */
typedef const char* (__cdecl* AmodRoomWriteHandler)(const AmodRoomPart* part, void* user);

typedef struct AmodBrushDesc
{
    uint32_t structSize;

    const char* label;      /* what the palette shows, "Erase" and the like */

    /* What a painted cell looks like; the palette is text and has no icons. `draw` does the same
       job and wins when both are given. */
    AmodTextureId texture;

    int cell;               /* the character it writes into the room grid */
    int key;                /* the letter the editor sorts it under */
    int group;              /* AMOD_BRUSH_GEOMETRY, AMOD_BRUSH_ENTITIES, or one from AddGroup */

    AmodBrushDrawHandler draw;
    AmodBrushPaintHandler paint;
    void* user;

    /* Which tilings this brush answers for. The geometry grid asks by a cell's neighbours, so a
       brush with no bits set is never drawn there. Zero means all of them. */
    int variants;

    int kind;               /* AMOD_BRUSH_KIND_* */
    AmodBrushSaveHandler save;

} AmodBrushDesc;

AMOD_LAST_FIELD(AmodBrushDesc, save, 0x2C);

/* What a brush is, the game's own included. */
typedef struct AmodBrushInfo
{
    uint32_t structSize;

    const char* label;      /* valid until the next call */
    int group;
    int cell;
    int key;
    int isMine;             /* whether this mod added it */
    uint32_t colour;        /* 0xAARRGGBB; white unless someone changed it */

} AmodBrushInfo;

AMOD_LAST_FIELD(AmodBrushInfo, colour, 0x18);

typedef struct AmodLevelEditorApi
{
    uint32_t structSize;

    /* An editor of the mod's own. Not ed_enable: the game's editor stays off and what stops is
       whatever the mode asks for. One mod holds it at a time. */
    int(__cdecl* Enter)(AmodMod mod, const AmodEditorMode* mode);
    int(__cdecl* Leave)(AmodMod mod);
    int(__cdecl* IsActive)(AmodMod mod);

    int(__cdecl* GetMode)(AmodMod mod, AmodEditorMode* out);
    int(__cdecl* SetMode)(AmodMod mod, const AmodEditorMode* mode);

    /* These two stand in for the game's frame and its drawing, so they come only to the mod holding
       the editor. Every other handler here comes either way. */
    AmodToken(__cdecl* OnFrame)(AmodMod mod, AmodEditorFrameHandler handler, void* user);
    AmodToken(__cdecl* OnDraw)(AmodMod mod, AmodEditorDrawHandler handler, void* user);
    void(__cdecl* Cancel)(AmodMod mod, AmodToken token);

    /* The game's own editor: its two screens, its keys and its ed_* commands, all at once. */
    int(__cdecl* IsGameEditorOn)(AmodMod mod);
    int(__cdecl* SetGameEditor)(AmodMod mod, int on);

    /* A brush of the mod's own stands in the palette beside the game's and is picked the same way. */
    int(__cdecl* AddBrush)(AmodMod mod, const AmodBrushDesc* desc);
    int(__cdecl* RemoveBrush)(AmodMod mod, int brush);

    int(__cdecl* BrushCount)(AmodMod mod, int group);
    int(__cdecl* GetBrush)(AmodMod mod, int group, int index, AmodBrushInfo* out);

    int(__cdecl* GetCurrentBrush)(AmodMod mod, int* group, int* index);
    int(__cdecl* SetCurrentBrush)(AmodMod mod, int group, int index);

    /* What owns the character already sitting in a cell. */
    int(__cdecl* FindBrushByCell)(AmodMod mod, int group, int cell);

    /* OnDraw is the overlay phase; this one picks. */
    AmodToken(__cdecl* OnDrawPhase)(AmodMod mod, int phase, AmodEditorDrawHandler handler, void* user);

    /* A group of the mod's own, standing after the game's two in the same M cycle. It paints one of
       the two grids, and returns the number AddBrush and the rest take. */
    int(__cdecl* AddGroup)(AmodMod mod, const char* name, int layer);
    int(__cdecl* RemoveGroup)(AmodMod mod, int group);

    int(__cdecl* GroupCount)(AmodMod mod);
    const char* (__cdecl* GroupName)(AmodMod mod, int group);
    int(__cdecl* GroupLayer)(AmodMod mod, int group);

    /* The grid the editor is painting - the working copy, not the live room. */
    int(__cdecl* GetGridCell)(AmodMod mod, int layer, int column, int row);
    int(__cdecl* SetGridCell)(AmodMod mod, int layer, int column, int row, int cell);
    int(__cdecl* GridSize)(AmodMod mod, int* columns, int* rows);

    AmodToken(__cdecl* OnRoomCreated)(AmodMod mod, AmodRoomCreatedHandler handler, void* user);
    AmodToken(__cdecl* OnRoomLoaded)(AmodMod mod, AmodRoomLoadedHandler handler, void* user);

    /* Registering this also sends the editor's own F2 through the loader's writer. */
    AmodToken(__cdecl* OnLevelWrite)(AmodMod mod, AmodLevelWriteHandler handler, void* user);

    /* `compile` builds the .room files the level points at; without them it will not load. */
    int(__cdecl* SaveLevel)(AmodMod mod, const char* path, int overwrite, int compile);

    /* The maze: a grid of rooms, most of its cells empty. The cursor says which room every grid and
       brush call above answers for. */
    int(__cdecl* MazeSize)(AmodMod mod, int* columns, int* rows);
    int(__cdecl* GetMazeCursor)(AmodMod mod, int* x, int* y);
    int(__cdecl* SetMazeCursor)(AmodMod mod, int x, int y);

    int(__cdecl* HasRoom)(AmodMod mod, int x, int y);

    /* An empty room, the maze grown to reach and the cursor left on it. A cell that already holds
       one keeps it, so this is also how to go there. */
    int(__cdecl* CreateRoom)(AmodMod mod, int x, int y);
    int(__cdecl* RemoveRoom)(AmodMod mod, int x, int y);

    /* Before that row or column; past the end appends one. */
    int(__cdecl* InsertRow)(AmodMod mod, int at);
    int(__cdecl* InsertColumn)(AmodMod mod, int at);

    /* One room, to and from a constructor file. Saving writes the .txt and the three placement
       files beside it; loading replaces whatever room is in that cell. */
    int(__cdecl* LoadRoom)(AmodMod mod, int x, int y, const char* path);
    int(__cdecl* SaveRoom)(AmodMod mod, int x, int y, const char* path, int compile);

    int(__cdecl* GetPart)(AmodMod mod, int part, AmodEditorPart* out);
    int(__cdecl* SetPart)(AmodMod mod, int part, const AmodEditorPart* place);

    /* Null puts the game's line back, an empty string leaves it out. The first %s is filled with
       the held brush on line 0 and the group on line 1; printf never sees the rest. */
    const char* (__cdecl* GetStatusLine)(AmodMod mod, int line);
    int(__cdecl* SetStatusLine)(AmodMod mod, int line, const char* text);

    /* A game brush keeps its name and its colour in its own record, so null and zero put them back,
       and unloading the mod does it anyway. */
    int(__cdecl* SetBrushLabel)(AmodMod mod, int group, int index, const char* label);

    int(__cdecl* GetScreen)(AmodMod mod);
    int(__cdecl* SetScreen)(AmodMod mod, int screen);

    AmodToken(__cdecl* OnKey)(AmodMod mod, AmodEditorKeyHandler handler, void* user);

    int(__cdecl* SetBrushColour)(AmodMod mod, int group, int index, uint32_t colour);

    /* AMOD_EDITOR_ICON_*. AMOD_INVALID_TEXTURE puts the game's own sprite back. */
    int(__cdecl* SetIcon)(AmodMod mod, int icon, AmodTextureId texture);

    /* Both grids emptied and a wall border put back - what C does. */
    int(__cdecl* ClearRoom)(AmodMod mod, int x, int y);

    /* The room's own file, the counterpart of OnLevelWrite. */
    AmodToken(__cdecl* OnRoomWrite)(AmodMod mod, AmodRoomWriteHandler handler, void* user);

} AmodLevelEditorApi;

AMOD_LAST_FIELD(AmodLevelEditorApi, OnRoomWrite, 0xD0);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace LevelEditor
    {
        inline const AmodLevelEditorApi* g_editor = nullptr;
        inline AmodMod g_editorSelf = nullptr;

        inline void                                                 Attach(const AmodLevelEditorApi* api, AmodMod self)
        {
            g_editor = api;
            g_editorSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_editor != nullptr;
        }

        inline bool                                                 Enter(const AmodEditorMode& mode)
        {
            return g_editor != nullptr && g_editor->Enter(g_editorSelf, &mode) != 0;
        }

        inline bool                                                 Enter(bool stopWorld = true, bool stopRender = true, bool stopInput = true,
                                                                        bool suppressGame = true)
        {
            AmodEditorMode mode = {};
            mode.structSize = sizeof(mode);
            mode.stopWorld = stopWorld ? 1 : 0;
            mode.stopRender = stopRender ? 1 : 0;
            mode.stopInput = stopInput ? 1 : 0;
            mode.suppressGame = suppressGame ? 1 : 0;

            return Enter(mode);
        }

        inline bool                                                 Leave()
        {
            return g_editor != nullptr && g_editor->Leave(g_editorSelf) != 0;
        }

        inline bool                                                 IsActive()
        {
            return g_editor != nullptr && g_editor->IsActive(g_editorSelf) != 0;
        }

        inline bool                                                 GetMode(AmodEditorMode& out)
        {
            out = AmodEditorMode{};
            out.structSize = sizeof(out);
            return g_editor != nullptr && g_editor->GetMode(g_editorSelf, &out) != 0;
        }

        inline bool                                                 SetMode(const AmodEditorMode& mode)
        {
            return g_editor != nullptr && g_editor->SetMode(g_editorSelf, &mode) != 0;
        }

        inline AmodToken                                            OnFrame(AmodEditorFrameHandler handler, void* user = nullptr)
        {
            return g_editor != nullptr
                ? g_editor->OnFrame(g_editorSelf, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline AmodToken                                            OnDraw(AmodEditorDrawHandler handler, void* user = nullptr)
        {
            return g_editor != nullptr
                ? g_editor->OnDraw(g_editorSelf, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline AmodToken                                            OnDraw(int phase, AmodEditorDrawHandler handler, void* user = nullptr)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->OnDrawPhase != nullptr)
                ? g_editor->OnDrawPhase(g_editorSelf, phase, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline void                                                 Cancel(AmodToken token)
        {
            if (g_editor != nullptr)
            {
                g_editor->Cancel(g_editorSelf, token);
            }
        }

        inline int                                                  AddBrush(const AmodBrushDesc& desc)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->AddBrush != nullptr)
                ? g_editor->AddBrush(g_editorSelf, &desc)
                : -1;
        }

        /* `cell` is written into the grid; `key` picks the brush. Different things. */
        inline int                                                  AddBrush(const char* label, char cell, char key,
                                                                             int group = AMOD_BRUSH_GEOMETRY,
                                                                             AmodBrushPaintHandler paint = nullptr, void* user = nullptr)
        {
            AmodBrushDesc desc = {};
            desc.structSize = sizeof(desc);
            desc.label = label;
            desc.cell = cell;
            desc.key = key;
            desc.group = group;
            desc.paint = paint;
            desc.user = user;

            return AddBrush(desc);
        }

        inline bool                                                 RemoveBrush(int brush)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->RemoveBrush != nullptr &&
                g_editor->RemoveBrush(g_editorSelf, brush) != 0;
        }

        inline int                                                  BrushCount(int group = AMOD_BRUSH_GEOMETRY)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->BrushCount != nullptr)
                ? g_editor->BrushCount(g_editorSelf, group)
                : 0;
        }

        inline bool                                                 GetBrush(int group, int index, AmodBrushInfo& out)
        {
            out = AmodBrushInfo{};
            out.structSize = sizeof(out);
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->GetBrush != nullptr &&
                g_editor->GetBrush(g_editorSelf, group, index, &out) != 0;
        }

        inline bool                                                 GetCurrentBrush(int& group, int& index)
        {
            group = -1;
            index = -1;
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->GetCurrentBrush != nullptr &&
                g_editor->GetCurrentBrush(g_editorSelf, &group, &index) != 0;
        }

        inline bool                                                 SetCurrentBrush(int group, int index)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->SetCurrentBrush != nullptr &&
                g_editor->SetCurrentBrush(g_editorSelf, group, index) != 0;
        }

        inline int                                                  AddGroup(const char* name, int layer = AMOD_BRUSH_GEOMETRY)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->AddGroup != nullptr)
                ? g_editor->AddGroup(g_editorSelf, name, layer)
                : -1;
        }

        inline bool                                                 RemoveGroup(int group)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->RemoveGroup != nullptr &&
                g_editor->RemoveGroup(g_editorSelf, group) != 0;
        }

        inline int                                                  GroupCount()
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->GroupCount != nullptr)
                ? g_editor->GroupCount(g_editorSelf)
                : 0;
        }

        inline const char*                                          GroupName(int group)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->GroupName != nullptr)
                ? g_editor->GroupName(g_editorSelf, group)
                : "";
        }

        inline int                                                  GroupLayer(int group)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->GroupLayer != nullptr)
                ? g_editor->GroupLayer(g_editorSelf, group)
                : -1;
        }

        inline AmodToken                                            OnRoomLoaded(AmodRoomLoadedHandler handler, void* user = nullptr)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->OnRoomLoaded != nullptr)
                ? g_editor->OnRoomLoaded(g_editorSelf, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline AmodToken                                            OnRoomCreated(AmodRoomCreatedHandler handler, void* user = nullptr)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->OnRoomCreated != nullptr)
                ? g_editor->OnRoomCreated(g_editorSelf, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline AmodToken                                            OnRoomWrite(AmodRoomWriteHandler handler, void* user = nullptr)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->OnRoomWrite != nullptr)
                ? g_editor->OnRoomWrite(g_editorSelf, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline AmodToken                                            OnLevelWrite(AmodLevelWriteHandler handler, void* user = nullptr)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->OnLevelWrite != nullptr)
                ? g_editor->OnLevelWrite(g_editorSelf, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline bool                                                 SaveLevel(const char* path, bool overwrite = true, bool compile = true)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->SaveLevel != nullptr &&
                g_editor->SaveLevel(g_editorSelf, path, overwrite ? 1 : 0, compile ? 1 : 0) != 0;
        }

        inline int                                                  GetGridCell(int column, int row, int layer = AMOD_BRUSH_GEOMETRY)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->GetGridCell != nullptr)
                ? g_editor->GetGridCell(g_editorSelf, layer, column, row)
                : 0;
        }

        inline bool                                                 SetGridCell(int column, int row, int cell, int layer = AMOD_BRUSH_GEOMETRY)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->SetGridCell != nullptr &&
                g_editor->SetGridCell(g_editorSelf, layer, column, row, cell) != 0;
        }

        inline bool                                                 GridSize(int& columns, int& rows)
        {
            columns = 0;
            rows = 0;
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->GridSize != nullptr &&
                g_editor->GridSize(g_editorSelf, &columns, &rows) != 0;
        }

        inline int                                                  FindBrushByCell(int group, int cell)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->FindBrushByCell != nullptr)
                ? g_editor->FindBrushByCell(g_editorSelf, group, cell)
                : -1;
        }

        inline bool                                                 MazeSize(int& columns, int& rows)
        {
            columns = 0;
            rows = 0;
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->MazeSize != nullptr &&
                g_editor->MazeSize(g_editorSelf, &columns, &rows) != 0;
        }

        inline bool                                                 GetMazeCursor(int& x, int& y)
        {
            x = -1;
            y = -1;
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->GetMazeCursor != nullptr &&
                g_editor->GetMazeCursor(g_editorSelf, &x, &y) != 0;
        }

        inline bool                                                 SetMazeCursor(int x, int y)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->SetMazeCursor != nullptr &&
                g_editor->SetMazeCursor(g_editorSelf, x, y) != 0;
        }

        inline bool                                                 HasRoom(int x, int y)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->HasRoom != nullptr &&
                g_editor->HasRoom(g_editorSelf, x, y) != 0;
        }

        inline bool                                                 CreateRoom(int x, int y)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->CreateRoom != nullptr &&
                g_editor->CreateRoom(g_editorSelf, x, y) != 0;
        }

        inline bool                                                 RemoveRoom(int x, int y)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->RemoveRoom != nullptr &&
                g_editor->RemoveRoom(g_editorSelf, x, y) != 0;
        }

        inline bool                                                 InsertRow(int at)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->InsertRow != nullptr &&
                g_editor->InsertRow(g_editorSelf, at) != 0;
        }

        inline bool                                                 InsertColumn(int at)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->InsertColumn != nullptr &&
                g_editor->InsertColumn(g_editorSelf, at) != 0;
        }

        inline bool                                                 LoadRoom(int x, int y, const char* path)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->LoadRoom != nullptr &&
                g_editor->LoadRoom(g_editorSelf, x, y, path) != 0;
        }

        inline bool                                                 SaveRoom(int x, int y, const char* path, bool compile = false)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->SaveRoom != nullptr &&
                g_editor->SaveRoom(g_editorSelf, x, y, path, compile ? 1 : 0) != 0;
        }

        inline bool                                                 GetPart(int part, AmodEditorPart& out)
        {
            out = AmodEditorPart{};
            out.structSize = sizeof(out);
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->GetPart != nullptr &&
                g_editor->GetPart(g_editorSelf, part, &out) != 0;
        }

        inline bool                                                 SetPart(int part, const AmodEditorPart& place)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->SetPart != nullptr &&
                g_editor->SetPart(g_editorSelf, part, &place) != 0;
        }

        /* Hide one, or bring it back, without moving it. */
        inline bool                                                 ShowPart(int part, bool shown)
        {
            AmodEditorPart place = {};

            if (!GetPart(part, place))
            {
                return false;
            }

            place.shown = shown ? 1 : 0;
            return SetPart(part, place);
        }

        /* A size of zero keeps the one it has. */
        inline bool                                                 MovePart(int part, float x, float y, float size = 0.0f)
        {
            AmodEditorPart place = {};

            if (!GetPart(part, place))
            {
                return false;
            }

            place.x = x;
            place.y = y;
            place.size = size != 0.0f ? size : place.size;
            return SetPart(part, place);
        }

        inline bool                                                 ScalePart(int part, float scale)
        {
            AmodEditorPart place = {};

            if (!GetPart(part, place))
            {
                return false;
            }

            place.scale = scale;
            return SetPart(part, place);
        }

        inline bool                                                 SetStatusLine(int line, const char* text)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->SetStatusLine != nullptr &&
                g_editor->SetStatusLine(g_editorSelf, line, text) != 0;
        }

        inline bool                                                 SetBrushLabel(int group, int index, const char* label)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->SetBrushLabel != nullptr &&
                g_editor->SetBrushLabel(g_editorSelf, group, index, label) != 0;
        }

        inline bool                                                 SetBrushColour(int group, int index, uint32_t colour)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->SetBrushColour != nullptr &&
                g_editor->SetBrushColour(g_editorSelf, group, index, colour) != 0;
        }

        inline bool                                                 SetIcon(int icon, AmodTextureId texture)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->SetIcon != nullptr &&
                g_editor->SetIcon(g_editorSelf, icon, texture) != 0;
        }

        inline bool                                                 ClearRoom(int x, int y)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->ClearRoom != nullptr &&
                g_editor->ClearRoom(g_editorSelf, x, y) != 0;
        }

        inline AmodToken                                            OnKey(AmodEditorKeyHandler handler, void* user = nullptr)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->OnKey != nullptr)
                ? g_editor->OnKey(g_editorSelf, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline int                                                  GetScreen()
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->GetScreen != nullptr)
                ? g_editor->GetScreen(g_editorSelf)
                : -1;
        }

        inline bool                                                 SetScreen(int screen)
        {
            return g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->SetScreen != nullptr &&
                g_editor->SetScreen(g_editorSelf, screen) != 0;
        }

        inline const char*                                          GetStatusLine(int line)
        {
            return (g_editor != nullptr && g_editor->structSize >= sizeof(AmodLevelEditorApi) && g_editor->GetStatusLine != nullptr)
                ? g_editor->GetStatusLine(g_editorSelf, line)
                : "";
        }

        inline bool                                                 IsGameEditorOn()
        {
            return g_editor != nullptr && g_editor->IsGameEditorOn(g_editorSelf) != 0;
        }

        inline bool                                                 SetGameEditor(bool on)
        {
            return g_editor != nullptr && g_editor->SetGameEditor(g_editorSelf, on ? 1 : 0) != 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_LEVEL_EDITOR_API_H */
