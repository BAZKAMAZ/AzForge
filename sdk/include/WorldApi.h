/*
 * WorldApi.h - the level the game has loaded and the rooms it is made of.
 *
 * A level is a maze of rooms. The maze is a ragged grid of room ids: the player stands on one
 * of its cells, and walking off the edge of a room steps the cursor to the neighbour. Each room
 * carries its own geometry, its own 40x29 grid of cells - the grid movement and collision are
 * expressed in - and the collision boxes the .room was built with.
 *
 * This is a reading interface. Nothing here moves the player, spawns anything or changes the
 * world; it answers what the world is and tells a mod when it changed. The entities living in
 * the rooms - monsters, bonuses, ctrl objects, the player - are their own subsystems.
 *
 * A room is named by its index, 0 to Level.roomCount-1, which is stable for as long as the
 * level is loaded and means nothing once it is not. A room also carries an $id, which is what
 * the maze holds and what a level script refers to; FindRoomById turns one into the other.
 *
 * Reached through AmodApi::world, null on a loader older than API version 17.
 */

#ifndef AZFORGE_WORLD_API_H
#define AZFORGE_WORLD_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The cell grid every room has, and where it sits in the world. A cell is a tenth of a world
 * unit square; column 0 begins at AMOD_ROOM_ORIGIN_X and row 0 at AMOD_ROOM_ORIGIN_Y, with rows
 * running downwards. */
#define AMOD_ROOM_COLUMNS                                           40
#define AMOD_ROOM_ROWS                                              29
#define AMOD_ROOM_CELL_SIZE                                         0.1f
#define AMOD_ROOM_ORIGIN_X                                          (-2.0f)
#define AMOD_ROOM_ORIGIN_Y                                          1.45f

/* What a cell holds. The grid is a character per cell, and these are the ones the game's own
 * movement tests for; a room may carry others, so a mod should treat an unlisted character as
 * scenery rather than assume it is empty. */
#define AMOD_CELL_EMPTY                                             ' '
#define AMOD_CELL_WALL                                              'W'

/* The level as a whole. Every string points into the game and stays valid until the level is
 * unloaded; a key the script left out is an empty string, never null. */
typedef struct AmodWorldLevel
{
    uint32_t structSize;    /* sizeof(AmodWorldLevel) as the running loader built it */

    const char* script;     /* the level script this was loaded from */
    const char* name;       /* $level_name */
    const char* nextLevel;  /* $next_level */
    const char* background; /* $background */
    const char* music;      /* $music */
    const char* startTitle; /* $start_title */
    const char* endTitle;   /* $end_title */
    const char* menuStat;   /* $menu_stat */
    const char* castle;     /* $enable_castle */

    int number;             /* $level_number, never 0 */
    float time;             /* $level_time */
    float musicStart;       /* $music_start */

    /* A bonus level is the timed diamond run: the game loads neither monsters nor bonuses
     * from the room scripts, so a mod that finds a room empty on one is seeing the truth. */
    int isBonus;            /* $bonus */
    float bonusTime;        /* $bonus_time */
    int bonusCount;         /* $bonus_count */

    int isDemoLevel;        /* $demo_level */
    int useForDemo;         /* $use_for_demo */

    int roomCount;
    int mazeRows;           /* rows of the maze; each row has its own width */

    /* Where $player_start put the player: the maze cell, and the cell within that room. */
    int startMazeX;
    int startMazeY;
    float startCellX;
    float startCellY;

    /* New fields are appended, never inserted. */
} AmodWorldLevel;

/* One room of the level. The strings point into the game and stay valid until the level is
 * unloaded. */
typedef struct AmodWorldRoom
{
    uint32_t structSize;    /* sizeof(AmodWorldRoom) as the running loader built it */

    int index;              /* what names this room in this interface */
    int id;                 /* $id, what the maze and the level script hold */

    const char* file;       /* the .room the geometry came from */
    const char* music;      /* $music of the room, "" when it keeps the level's */

    /* Where the room sits on the maze, or -1 for both when no maze cell names it - a room
     * the level carries but never reaches. */
    int mazeX;
    int mazeY;

    /* A room entry whose .room failed to load is still an entry, so a mod checking anything
     * about the geometry should check this first. */
    int isLoaded;

    int hasBackWall;
    int collisionBoxCount;
    int lightTriangleCount; /* the baked triangles the light halos are drawn over */

    /* New fields are appended, never inserted. */
} AmodWorldRoom;

/* One of the room's collision boxes, as two corners in world space. */
typedef struct AmodWorldBox
{
    float min[3];
    float max[3];
} AmodWorldBox;

typedef enum AmodWorldEvent
{
    /* The level has been parsed and its rooms loaded. No room is current yet. */
    AMOD_WORLD_EVENT_LEVEL_LOADED = 0,

    /* The level is about to be freed. It is still readable for the length of the call, and
     * every index and pointer a mod is holding stops meaning anything once it returns. */
    AMOD_WORLD_EVENT_LEVEL_UNLOADED = 1,

    /* A room became the current one. This fires for the first room of a level as well as for
     * every step through the maze afterwards. */
    AMOD_WORLD_EVENT_ROOM_CHANGED = 2
} AmodWorldEvent;

typedef struct AmodWorldEventInfo
{
    uint32_t structSize;    /* sizeof(AmodWorldEventInfo) as the running loader built it */
    AmodWorldEvent event;

    /* The room now current, and the one before it, or -1 where there is none. Filled in for
     * AMOD_WORLD_EVENT_ROOM_CHANGED; -1 for the other two. */
    int roomIndex;
    int roomId;
    int previousRoomIndex;

    /* Where the cursor now sits on the maze, or -1 for both outside a room change. */
    int mazeX;
    int mazeY;

    /* New fields are appended, never inserted. */
} AmodWorldEventInfo;

/* The info pointer is valid only for the call. */
typedef void (__cdecl* AmodWorldEventHandler)(const AmodWorldEventInfo* info, void* user);

typedef struct AmodWorldApi
{
    uint32_t structSize;

    /* Whether a level is loaded. Everything below answers nothing while one is not: the game
     * keeps a single level object and empties it rather than freeing it. */
    int (__cdecl* IsLevelLoaded)(AmodMod mod);

    /* Fills in the level. Non-zero on success. The caller sets out->structSize first, and the
     * loader fills what fits, so a mod built against a later version still works here. */
    int (__cdecl* GetLevel)(AmodMod mod, AmodWorldLevel* out);

    int (__cdecl* GetRoomCount)(AmodMod mod);

    /* Fills in one room by index. Non-zero on success; zero when the index names no room.
     * The caller sets out->structSize first, as for GetLevel. */
    int (__cdecl* GetRoom)(AmodMod mod, int index, AmodWorldRoom* out);

    /* The index of the room with this $id, or -1. */
    int (__cdecl* FindRoomById)(AmodMod mod, int id);

    /* The room the player is in, or -1 before the first one is activated. */
    int (__cdecl* GetCurrentRoom)(AmodMod mod);

    /* The room the player was in before that, or -1. */
    int (__cdecl* GetPreviousRoom)(AmodMod mod);

    /* Where the player is on the maze. Either pointer may be null; both are left alone when
     * no level is loaded. */
    void (__cdecl* GetMazeCursor)(AmodMod mod, int* x, int* y);

    /* The maze is ragged: every row carries its own width, so a row is asked for its own.
     * Zero for a row that is not there. */
    int (__cdecl* GetMazeRowWidth)(AmodMod mod, int y);

    /* The $id of the room at this maze cell, or 0 for a hole in the maze or a cell that is
     * not there. Note this is an id, not an index - pass it through FindRoomById. */
    int (__cdecl* GetMazeRoomId)(AmodMod mod, int x, int y);

    /* The character at one cell of a room's grid, or 0 when the room or the cell is not
     * there. Columns run 0..AMOD_ROOM_COLUMNS-1 left to right and rows 0..AMOD_ROOM_ROWS-1
     * downwards, the same way the game indexes them. */
    char (__cdecl* GetRoomCell)(AmodMod mod, int index, int column, int row);

    /* The whole grid at once, row by row, for a mod that walks it every frame. Writes
     * AMOD_ROOM_COLUMNS * AMOD_ROOM_ROWS characters and returns how many it wrote, or 0 when
     * the room is not there or the buffer is too small. */
    int (__cdecl* GetRoomCells)(AmodMod mod, int index, char* buffer, int capacity);

    /* One of the room's collision boxes. Non-zero on success. */
    int (__cdecl* GetRoomCollisionBox)(AmodMod mod, int index, int boxIndex, AmodWorldBox* out);

    /* Calls the handler whenever this happens, until the mod drops it or is unloaded. Zero
     * when the event is not one this loader knows. */
    AmodToken (__cdecl* AddEventHandler)(AmodMod mod, AmodWorldEvent event,
                                         AmodWorldEventHandler handler, void* user);
    void (__cdecl* RemoveEventHandler)(AmodMod mod, AmodToken token);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodWorldApi;

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace World
    {
        inline const AmodWorldApi* g_world = nullptr;
        inline AmodMod g_self = nullptr;

        inline void                                                 Attach(const AmodWorldApi* api, AmodMod self)
        {
            g_world = api;
            g_self = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_world != nullptr;
        }

        inline bool                                                 IsLevelLoaded()
        {
            return g_world != nullptr && g_world->IsLevelLoaded(g_self) != 0;
        }

        inline bool                                                 GetLevel(AmodWorldLevel& out)
        {
            out = AmodWorldLevel{};
            out.structSize = sizeof(AmodWorldLevel);
            return g_world != nullptr && g_world->GetLevel(g_self, &out) != 0;
        }

        inline int                                                  RoomCount()
        {
            return g_world != nullptr ? g_world->GetRoomCount(g_self) : 0;
        }

        inline bool                                                 GetRoom(int index, AmodWorldRoom& out)
        {
            out = AmodWorldRoom{};
            out.structSize = sizeof(AmodWorldRoom);
            return g_world != nullptr && g_world->GetRoom(g_self, index, &out) != 0;
        }

        inline int                                                  FindRoomById(int id)
        {
            return g_world != nullptr ? g_world->FindRoomById(g_self, id) : -1;
        }

        inline int                                                  CurrentRoom()
        {
            return g_world != nullptr ? g_world->GetCurrentRoom(g_self) : -1;
        }

        inline int                                                  PreviousRoom()
        {
            return g_world != nullptr ? g_world->GetPreviousRoom(g_self) : -1;
        }

        inline void                                                 MazeCursor(int& x, int& y)
        {
            x = -1;
            y = -1;
            if (g_world != nullptr)
            {
                g_world->GetMazeCursor(g_self, &x, &y);
            }
        }

        inline int                                                  MazeRowWidth(int y)
        {
            return g_world != nullptr ? g_world->GetMazeRowWidth(g_self, y) : 0;
        }

        inline int                                                  MazeRoomId(int x, int y)
        {
            return g_world != nullptr ? g_world->GetMazeRoomId(g_self, x, y) : 0;
        }

        inline char                                                 Cell(int index, int column, int row)
        {
            return g_world != nullptr ? g_world->GetRoomCell(g_self, index, column, row) : '\0';
        }

        inline int                                                  Cells(int index, char* buffer, int capacity)
        {
            return g_world != nullptr ? g_world->GetRoomCells(g_self, index, buffer, capacity) : 0;
        }

        inline bool                                                 CollisionBox(int index, int boxIndex, AmodWorldBox& out)
        {
            out = AmodWorldBox{};
            return g_world != nullptr && g_world->GetRoomCollisionBox(g_self, index, boxIndex, &out) != 0;
        }

        inline AmodToken                                            OnEvent(AmodWorldEvent event, AmodWorldEventHandler handler, void* user = nullptr)
        {
            return g_world != nullptr ? g_world->AddEventHandler(g_self, event, handler, user)
                                      : AMOD_INVALID_TOKEN;
        }

        inline void                                                 RemoveEvent(AmodToken token)
        {
            if (g_world != nullptr)
            {
                g_world->RemoveEventHandler(g_self, token);
            }
        }

        /* Where a cell sits in the world. The x is the left edge of the column and the y the
         * top edge of the row, so a cell spans to those plus AMOD_ROOM_CELL_SIZE, downwards
         * for y. The game's own movement works in these coordinates. */
        inline float                                                ColumnLeft(int column)
        {
            return AMOD_ROOM_ORIGIN_X + column * AMOD_ROOM_CELL_SIZE;
        }

        inline float                                                RowTop(int row)
        {
            return AMOD_ROOM_ORIGIN_Y - row * AMOD_ROOM_CELL_SIZE;
        }

        /* And the other way. The result may fall outside the grid, which is what a caller
         * checks for rather than being given a clamped answer that reads as a real cell.
         * Rounding is towards the lower cell, so a point on a boundary belongs to the cell
         * that begins there and a point left of or above the grid gives a negative index
         * rather than folding back onto cell zero. */
        inline int                                                  FloorToInt(float value)
        {
            const int truncated = static_cast<int>(value);
            return (value < 0.0f && static_cast<float>(truncated) != value) ? truncated - 1
                                                                            : truncated;
        }

        inline int                                                  ColumnAt(float x)
        {
            return FloorToInt((x - AMOD_ROOM_ORIGIN_X) / AMOD_ROOM_CELL_SIZE);
        }

        inline int                                                  RowAt(float y)
        {
            return FloorToInt((AMOD_ROOM_ORIGIN_Y - y) / AMOD_ROOM_CELL_SIZE);
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_WORLD_API_H */
