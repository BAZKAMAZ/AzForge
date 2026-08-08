#ifndef AZFORGE_WORLD_API_H
#define AZFORGE_WORLD_API_H

#include "ForgeTypes.h"
#include "MmdApi.h" /* a room's statics hold a model */
#include "TextureApi.h" /* a room's lightmap is handed over as a texture */

#ifdef __cplusplus
extern "C" {
#endif

#define AMOD_ROOM_COLUMNS                                           40
#define AMOD_ROOM_ROWS                                              29
#define AMOD_ROOM_CELL_SIZE                                         0.1f
#define AMOD_ROOM_ORIGIN_X                                          (-2.0f)
#define AMOD_ROOM_ORIGIN_Y                                          1.45f

#define AMOD_CELL_EMPTY                                             ' '
#define AMOD_CELL_WALL                                              'W'

/* Level fields. Strings valid until unload; missing keys are "". */
typedef struct AmodWorldLevel
{
    uint32_t structSize; /* sizeof(AmodWorldLevel) as the running loader built it */

    const char* script; /* the level script this was loaded from */
    const char* name; /* $level_name */
    const char* nextLevel; /* $next_level */
    const char* background; /* $background */
    const char* music; /* $music */
    const char* startTitle; /* $start_title */
    const char* endTitle; /* $end_title */
    const char* menuStat; /* $menu_stat */
    const char* castle; /* $enable_castle */

    int number; /* $level_number, never 0 */
    float time; /* $level_time */
    float musicStart; /* $music_start */

    /* Timed diamond run: no monsters/bonuses from room scripts. */
    int isBonus; /* $bonus */
    float bonusTime; /* $bonus_time */
    int bonusCount; /* $bonus_count */

    int isDemoLevel; /* $demo_level */
    int useForDemo; /* $use_for_demo */

    int roomCount;
    int mazeRows; /* rows of the maze; each row has its own width */

    /* Player start on maze and in-room cell. */
    int startMazeX;
    int startMazeY;
    float startCellX;
    float startCellY;

} AmodWorldLevel;

AMOD_LAST_FIELD(AmodWorldLevel, startCellY, 0x5C);

/* One room. Strings valid until unload. */
typedef struct AmodWorldRoom
{
    uint32_t structSize; /* sizeof(AmodWorldRoom) as the running loader built it */

    int index; /* what names this room in this interface */
    int id; /* $id, what the maze and the level script hold */

    const char* file; /* the .room the geometry came from */
    const char* music; /* $music of the room, "" when it keeps the level's */

    /* Maze cell, or -1 if unreachable. */
    int mazeX;
    int mazeY;

    /* Geometry loaded successfully. */
    int isLoaded;

    int hasBackWall;
    int collisionBoxCount;
    int lightTriangleCount; /* the baked triangles the light halos are drawn over */

} AmodWorldRoom;

AMOD_LAST_FIELD(AmodWorldRoom, lightTriangleCount, 0x28);

/* Collision box corners. */
typedef struct AmodWorldBox
{
    float min[3];
    float max[3];
} AmodWorldBox;

AMOD_LAST_FIELD(AmodWorldBox, max, 0x0C);

/* What a box does when a body meets it. */
#define AMOD_BOX_PUSHES                                             0
#define AMOD_BOX_REPORTS_ONLY                                       1 /* a trigger, not a wall */
#define AMOD_BOX_PUSHES_WHEN_ASKED                                  2
#define AMOD_BOX_PUSHES_TOO                                         3

// A collision box with the one thing AmodWorldBox has no room for: its kind
typedef struct AmodRoomBox
{
    uint32_t structSize;

    int kind; /* AMOD_BOX_* */
    float min[3];
    float max[3];

} AmodRoomBox;

AMOD_LAST_FIELD(AmodRoomBox, max, 0x14);

// One surface a dynamic light is allowed to fall on: three corners, x y z each
typedef struct AmodRoomTriangle
{
    uint32_t structSize;

    /* Three corners, nine floats: x y z, x y z, x y z. */
    float points[9];

} AmodRoomTriangle;

AMOD_LAST_FIELD(AmodRoomTriangle, points, 0x04);

// One light of a room
typedef struct AmodWorldLight
{
    uint32_t structSize;

    float colour[3];
    float alpha; /* the game writes one here and never reads it back */
    float range;
    float position[3]; /* x and y; the game writes zero for the third and lights a plane */

} AmodWorldLight;

AMOD_LAST_FIELD(AmodWorldLight, position, 0x18);

typedef struct AmodEntityLight
{
    uint32_t structSize;

    int isLit; /* whether it carries one at all; clear it to put the light out */

    float colour[3];
    float alpha; /* carried beside the colour and never read by the lighting */
    float range; /* reach and brightness at once; the game's flicker is added to it */

    /* From the entity's own position, not a place in the room. */
    float offset[3];

} AmodEntityLight;

AMOD_LAST_FIELD(AmodEntityLight, offset, 0x1C);

/* How a static is put over what is already drawn. */
#define AMOD_STATIC_BLEND_OPAQUE                                    0
#define AMOD_STATIC_BLEND_ADD                                       1
#define AMOD_STATIC_BLEND_MULTIPLY                                  2

// One piece of a room's decoration
typedef struct AmodRoomStatic
{
    uint32_t structSize;

    /* Clear it and the room simply does not draw this one */
    int isShown;
    int isLit; /* taken into the lit pass rather than drawn flat */
    int isTranslucent;
    int blend; /* AMOD_STATIC_BLEND_*, and only when it is translucent */

    AmodModelId model;
    AmodTextureId texture;

    /* Where it stands, as a 4x4 in row order - the same the scene hands to the card. */
    float matrix[16];

} AmodRoomStatic;

AMOD_LAST_FIELD(AmodRoomStatic, matrix, 0x1C);

#define AMOD_ENTITY_MONSTER                                         10
#define AMOD_ENTITY_BONUS                                           9
#define AMOD_ENTITY_DOOR                                            2

/* Snapshot for this call only; entity is recycled when dead. */
typedef struct AmodWorldEntity
{
    uint32_t structSize;

    /* AMOD_ENTITY_* or 0 if no record. */
    int kind;

    /* Spawn $name, or "". */
    const char* name;

    float position[3];
    float previousPosition[3]; /* where it was last frame, which is its direction of travel */

    int state; /* what it is doing, and the sub-state under it */
    int subState;
    uint32_t flags;

    /* Put on free list at end of frame. */
    int isDead;
    int isLive;

} AmodWorldEntity;

AMOD_LAST_FIELD(AmodWorldEntity, isLive, 0x34);

typedef enum AmodWorldEvent
{
    AMOD_WORLD_EVENT_LEVEL_LOADED = 0, /* level parsed; no room current yet */
    AMOD_WORLD_EVENT_LEVEL_UNLOADED = 1, /* still readable during the call */
    AMOD_WORLD_EVENT_ROOM_CHANGED = 2, /* also fires for the first room */
    AMOD_WORLD_EVENT_TIME_UP = 3 /* the bonus level's countdown reached nothing */
} AmodWorldEvent;

typedef struct AmodWorldEventInfo
{
    uint32_t structSize;
    AmodWorldEvent event;

    /* Filled for ROOM_CHANGED; -1 otherwise. */
    int roomIndex;
    int roomId;
    int previousRoomIndex;

    int mazeX; /* -1 outside a room change */
    int mazeY;
} AmodWorldEventInfo;

AMOD_LAST_FIELD(AmodWorldEventInfo, mazeY, 0x18);

/* Info valid only for the call. */
typedef void(__cdecl* AmodWorldEventHandler)(const AmodWorldEventInfo* info, void* user);

typedef struct AmodWorldApi
{
    uint32_t structSize;

    /* False while no level is loaded. */
    int(__cdecl* IsLevelLoaded)(AmodMod mod);

    /* Caller sets out->structSize first. */
    int(__cdecl* GetLevel)(AmodMod mod, AmodWorldLevel* out);

    int(__cdecl* GetRoomCount)(AmodMod mod);

    /* Caller sets out->structSize first. */
    int(__cdecl* GetRoom)(AmodMod mod, int index, AmodWorldRoom* out);

    /* Index of room with this $id, or -1. */
    int(__cdecl* FindRoomById)(AmodMod mod, int id);

    /* Current room index, or -1. */
    int(__cdecl* GetCurrentRoom)(AmodMod mod);

    /* Previous room index, or -1. */
    int(__cdecl* GetPreviousRoom)(AmodMod mod);

    /* Maze cursor; either pointer may be null. */
    void(__cdecl* GetMazeCursor)(AmodMod mod, int* x, int* y);

    /* Width of maze row y, or 0. */
    int(__cdecl* GetMazeRowWidth)(AmodMod mod, int y);

    /* Room $id at maze cell, or 0. Pass through FindRoomById. */
    int(__cdecl* GetMazeRoomId)(AmodMod mod, int x, int y);

    char(__cdecl* GetRoomCell)(AmodMod mod, int index, int column, int row);

    int(__cdecl* GetRoomCells)(AmodMod mod, int index, char* buffer, int capacity);

    /* One collision box. */
    int(__cdecl* GetRoomCollisionBox)(AmodMod mod, int index, int boxIndex, AmodWorldBox* out);

    /* Subscribe; 0 if event unknown. */
    AmodToken(__cdecl* AddEventHandler)(AmodMod mod, AmodWorldEvent event, AmodWorldEventHandler handler, void* user);
    void(__cdecl* RemoveEventHandler)(AmodMod mod, AmodToken token);

    int(__cdecl* GetEntityCount)(AmodMod mod, int roomIndex);
    int(__cdecl* GetEntity)(AmodMod mod, int roomIndex, int index, AmodWorldEntity* out);

    int(__cdecl* GetRoomCellRecord)(AmodMod mod, int index, int column, int row, unsigned char* out, int capacity);

    int(__cdecl* GetSpawnCount)(AmodMod mod, int roomIndex);
    int(__cdecl* GetSpawn)(AmodMod mod, int roomIndex, int index, AmodWorldEntity* out);

    // The lights a room carries
    int(__cdecl* GetRoomLightCount)(AmodMod mod, int roomIndex);
    int(__cdecl* GetRoomLight)(AmodMod mod, int roomIndex, int index, AmodWorldLight* out);
    int(__cdecl* SetRoomLight)(AmodMod mod, int roomIndex, int index, const AmodWorldLight* light);
    int(__cdecl* AddLight)(AmodMod mod, const AmodWorldLight* light);

    /* The light an entity carries */
    int(__cdecl* GetEntityLight)(AmodMod mod, int roomIndex, int index, AmodEntityLight* out);
    int(__cdecl* SetEntityLight)(AmodMod mod, int roomIndex, int index, const AmodEntityLight* light);

    /* Player entity index in the room, or -1. (PlayerApi owns the player object.) */
    int(__cdecl* GetPlayerEntity)(AmodMod mod);

    AmodTextureId(__cdecl* GetRoomLightmap)(AmodMod mod, int roomIndex);

    /* The room's decoration */
    int(__cdecl* GetStaticCount)(AmodMod mod, int roomIndex);
    int(__cdecl* GetStatic)(AmodMod mod, int roomIndex, int index, AmodRoomStatic* out);
    int(__cdecl* SetStatic)(AmodMod mod, int roomIndex, int index, const AmodRoomStatic* what);

    /* One of the mod's own, drawn from this frame on. Its number, or -1. */
    int(__cdecl* AddStatic)(AmodMod mod, int roomIndex, const AmodRoomStatic* what);

    int(__cdecl* AddRoomLight)(AmodMod mod, int roomIndex, const AmodWorldLight* light);

    /* The grid the room is walked on */
    int(__cdecl* SetRoomCell)(AmodMod mod, int roomIndex, int column, int row, char type);
    int(__cdecl* SetRoomCells)(AmodMod mod, int roomIndex, const char* cells, int count);

    int(__cdecl* GetRoomBox)(AmodMod mod, int roomIndex, int boxIndex, AmodRoomBox* out);
    int(__cdecl* SetRoomBox)(AmodMod mod, int roomIndex, int boxIndex, const AmodRoomBox* box);

    /* One of the mod's own, met from this frame on */
    int(__cdecl* AddRoomBox)(AmodMod mod, int roomIndex, const AmodRoomBox* box);

    /* The surfaces a dynamic light may fall on */
    int(__cdecl* GetLightTriangleCount)(AmodMod mod, int roomIndex);
    int(__cdecl* GetLightTriangle)(AmodMod mod, int roomIndex, int index, AmodRoomTriangle* out);
    int(__cdecl* SetLightTriangle)(AmodMod mod, int roomIndex, int index, const AmodRoomTriangle* triangle);

    /* One of the mod's own */
    int(__cdecl* AddLightTriangle)(AmodMod mod, int roomIndex, const AmodRoomTriangle* triangle);

    /* Bonus-level countdown; only when GetLevel().isBonus. */
    float(__cdecl* GetTimeLeft)(AmodMod mod);
    int(__cdecl* SetTimeLeft)(AmodMod mod, float seconds);

    /* Stopwatch (HUD + results "Your Time"); counts up on every level. */
    float(__cdecl* GetElapsed)(AmodMod mod);
    int(__cdecl* SetElapsed)(AmodMod mod, float seconds);

    /* Enter room on that side (Player::Step result); places player at far edge. */
    int(__cdecl* CrossRoom)(AmodMod mod, int side);

    /* Remove a static decoration (ladder/fire models); indices after it shift. Final for the level. */
    int(__cdecl* RemoveStatic)(AmodMod mod, int roomIndex, int index);

} AmodWorldApi;

AMOD_LAST_FIELD(AmodWorldApi, RemoveStatic, 0xC0);

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

        inline int                                                  LightCount(int roomIndex = -1)
        {
            return g_world != nullptr && g_world->GetRoomLightCount != nullptr
                ? g_world->GetRoomLightCount(g_self, roomIndex)
                : 0;
        }

        inline bool                                                 GetLight(int roomIndex, int index, AmodWorldLight& out)
        {
            out = AmodWorldLight{};
            out.structSize = sizeof(out);
            return g_world != nullptr && g_world->GetRoomLight != nullptr &&
                g_world->GetRoomLight(g_self, roomIndex, index, &out) != 0;
        }

        inline bool                                                 SetLight(int roomIndex, int index, const AmodWorldLight& light)
        {
            return g_world != nullptr && g_world->SetRoomLight != nullptr &&
                g_world->SetRoomLight(g_self, roomIndex, index, &light) != 0;
        }


        inline int                                                  AddRoomLight(const AmodWorldLight& light, int roomIndex = -1)
        {
            return g_world != nullptr && g_world->AddRoomLight != nullptr
                ? g_world->AddRoomLight(g_self, roomIndex, &light)
                : -1;
        }

        /* A light for the coming frame only - ask again every frame for as long as it should burn. */
        inline bool                                                 AddLight(const AmodWorldLight& light)
        {
            return g_world != nullptr && g_world->AddLight != nullptr &&
                g_world->AddLight(g_self, &light) != 0;
        }

        inline bool                                                 AddLight(float x, float y, float r, float g, float b, float range)
        {
            AmodWorldLight light = {};
            light.structSize = sizeof(light);
            light.colour[0] = r;
            light.colour[1] = g;
            light.colour[2] = b;
            light.range = range;
            light.position[0] = x;
            light.position[1] = y;
            return AddLight(light);
        }

        /* The room's baked lightmap, to be read and painted through the texture interface. */
        inline AmodTextureId                                        Lightmap(int roomIndex = -1)
        {
            return g_world != nullptr && g_world->GetRoomLightmap != nullptr
                ? g_world->GetRoomLightmap(g_self, roomIndex)
                : AMOD_INVALID_TEXTURE;
        }

        /* The room's decoration. */
        inline int                                                  StaticCount(int roomIndex = -1)
        {
            return g_world != nullptr && g_world->GetStaticCount != nullptr
                ? g_world->GetStaticCount(g_self, roomIndex)
                : 0;
        }

        inline bool                                                 GetStatic(int index, AmodRoomStatic& out, int roomIndex = -1)
        {
            out = AmodRoomStatic{};
            out.structSize = sizeof(out);
            return g_world != nullptr && g_world->GetStatic != nullptr && g_world->GetStatic(g_self, roomIndex, index, &out) != 0;
        }

        inline bool                                                 SetStatic(int index, const AmodRoomStatic& what, int roomIndex = -1)
        {
            return g_world != nullptr && g_world->SetStatic != nullptr && g_world->SetStatic(g_self, roomIndex, index, &what) != 0;
        }

        /* Its number, or -1. */
        inline int                                                  AddStatic(const AmodRoomStatic& what, int roomIndex = -1)
        {
            return g_world != nullptr && g_world->AddStatic != nullptr
                ? g_world->AddStatic(g_self, roomIndex, &what)
                : -1;
        }

        /* Out of the room for good, models and all. Everything after it moves up one number. */
        inline bool                                                 RemoveStatic(int index, int roomIndex = -1)
        {
            return g_world != nullptr && g_world->RemoveStatic != nullptr &&
                g_world->RemoveStatic(g_self, roomIndex, index) != 0;
        }

        /* Whether the room draws it at all - the cheapest thing to do to a static. */
        inline bool                                                 ShowStatic(int index, bool shown)
        {
            AmodRoomStatic what;
            if (!GetStatic(index, what))
            {
                return false;
            }
            what.isShown = shown ? 1 : 0;
            return SetStatic(index, what);
        }

        /* Which of the room's entities is the player, or -1. */
        inline int                                                  PlayerEntity()
        {
            return g_world != nullptr && g_world->GetPlayerEntity != nullptr
                ? g_world->GetPlayerEntity(g_self)
                : -1;
        }

        inline bool                                                 GetEntityLight(int roomIndex, int index, AmodEntityLight& out)
        {
            out = AmodEntityLight{};
            out.structSize = sizeof(out);
            return g_world != nullptr && g_world->GetEntityLight != nullptr && g_world->GetEntityLight(g_self, roomIndex, index, &out) != 0;
        }

        inline bool                                                 SetEntityLight(int roomIndex, int index, const AmodEntityLight& light)
        {
            return g_world != nullptr && g_world->SetEntityLight != nullptr && g_world->SetEntityLight(g_self, roomIndex, index, &light) != 0;
        }

        /* Hand an entity a light of its own, which it then carries by itself. */
        inline bool                                                 LightEntity(int index, float r, float g, float b, float range)
        {
            AmodEntityLight light = {};
            light.structSize = sizeof(light);
            light.isLit = 1;
            light.colour[0] = r;
            light.colour[1] = g;
            light.colour[2] = b;
            light.range = range;
            return SetEntityLight(-1, index, light);
        }

        inline bool                                                 UnlightEntity(int index)
        {
            AmodEntityLight light = {};
            light.structSize = sizeof(light);
            return SetEntityLight(-1, index, light);
        }

        /* The character a cell carries; ' ' is empty and 'W' is wall. Changes on the spot. */
        inline bool                                                 SetCell(int column, int row, char type, int roomIndex = -1)
        {
            return g_world != nullptr && g_world->SetRoomCell != nullptr && g_world->SetRoomCell(g_self, roomIndex, column, row, type) != 0;
        }

        /* The boxes: what stops a body, as against the grid, which says what the room is made of. */
        inline bool                                                 GetBox(int boxIndex, AmodRoomBox& out, int roomIndex = -1)
        {
            out = AmodRoomBox{};
            out.structSize = sizeof(out);
            return g_world != nullptr && g_world->GetRoomBox != nullptr && g_world->GetRoomBox(g_self, roomIndex, boxIndex, &out) != 0;
        }

        inline bool                                                 SetBox(int boxIndex, const AmodRoomBox& box, int roomIndex = -1)
        {
            return g_world != nullptr && g_world->SetRoomBox != nullptr &&
                g_world->SetRoomBox(g_self, roomIndex, boxIndex, &box) != 0;
        }

        /* Its number, or -1. */
        inline int                                                  AddBox(const AmodRoomBox& box, int roomIndex = -1)
        {
            return g_world != nullptr && g_world->AddRoomBox != nullptr
                ? g_world->AddRoomBox(g_self, roomIndex, &box)
                : -1;
        }

        /* Seconds left of the bonus level's countdown; nothing on any other kind. */
        inline float                                                TimeLeft()
        {
            return g_world != nullptr && g_world->GetTimeLeft != nullptr ? g_world->GetTimeLeft(g_self) : 0.0f;
        }

        inline bool                                                 SetTimeLeft(float seconds)
        {
            return g_world != nullptr && g_world->SetTimeLeft != nullptr && g_world->SetTimeLeft(g_self, seconds) != 0;
        }

        /* Seconds since the level began, on every kind of level. */
        inline float                                                Elapsed()
        {
            return g_world != nullptr && g_world->GetElapsed != nullptr ? g_world->GetElapsed(g_self) : 0.0f;
        }

        inline bool                                                 SetElapsed(float seconds)
        {
            return g_world != nullptr && g_world->SetElapsed != nullptr && g_world->SetElapsed(g_self, seconds) != 0;
        }

        /* Side is what Player::Step answered when he left the room. */
        inline bool                                                 CrossRoom(int side)
        {
            return g_world != nullptr && g_world->CrossRoom != nullptr && g_world->CrossRoom(g_self, side) != 0;
        }

        /* The surfaces a dynamic light may fall on. */
        inline int                                                  LightTriangleCount(int roomIndex = -1)
        {
            return g_world != nullptr && g_world->GetLightTriangleCount != nullptr
                ? g_world->GetLightTriangleCount(g_self, roomIndex)
                : 0;
        }

        inline bool                                                 GetLightTriangle(int index, AmodRoomTriangle& out, int roomIndex = -1)
        {
            out = AmodRoomTriangle{};
            out.structSize = sizeof(out);
            return g_world != nullptr && g_world->GetLightTriangle != nullptr && g_world->GetLightTriangle(g_self, roomIndex, index, &out) != 0;
        }

        inline bool                                                 SetLightTriangle(int index, const AmodRoomTriangle& triangle, int roomIndex = -1)
        {
            return g_world != nullptr && g_world->SetLightTriangle != nullptr &&
                g_world->SetLightTriangle(g_self, roomIndex, index, &triangle) != 0;
        }

        inline int                                                  AddLightTriangle(const AmodRoomTriangle& triangle, int roomIndex = -1)
        {
            return g_world != nullptr && g_world->AddLightTriangle != nullptr
                ? g_world->AddLightTriangle(g_self, roomIndex, &triangle)
                : -1;
        }

        /* A whole grid, as GetRoomCells gives one back. A cell left '\0' is left alone. */
        inline bool                                                 SetCells(const char* cells, int count, int roomIndex = -1)
        {
            return g_world != nullptr && g_world->SetRoomCells != nullptr && g_world->SetRoomCells(g_self, roomIndex, cells, count) != 0;
        }

        inline bool                                                 CollisionBox(int index, int boxIndex, AmodWorldBox& out)
        {
            out = AmodWorldBox{};
            return g_world != nullptr && g_world->GetRoomCollisionBox(g_self, index, boxIndex, &out) != 0;
        }

        inline AmodToken                                            OnEvent(AmodWorldEvent event, AmodWorldEventHandler handler, void* user = nullptr)
        {
            return g_world != nullptr ? g_world->AddEventHandler(g_self, event, handler, user) : AMOD_INVALID_TOKEN;
        }

        inline void                                                 RemoveEvent(AmodToken token)
        {
            if (g_world != nullptr)
            {
                g_world->RemoveEventHandler(g_self, token);
            }
        }

        inline int                                                  EntityCount(int roomIndex)
        {
            return (g_world != nullptr && g_world->structSize >= sizeof(AmodWorldApi))
                ? g_world->GetEntityCount(g_self, roomIndex)
                : 0;
        }

        inline bool                                                 GetEntity(int roomIndex, int index, AmodWorldEntity& out)
        {
            out = AmodWorldEntity{};
            out.structSize = sizeof(AmodWorldEntity);
            return g_world != nullptr && g_world->structSize >= sizeof(AmodWorldApi) && g_world->GetEntity(g_self, roomIndex, index, &out) != 0;
        }

        /* Spawns in the current room. */
        inline int                                                  CurrentSpawnCount()
        {
            return (g_world != nullptr && g_world->structSize >= sizeof(AmodWorldApi))
                ? g_world->GetSpawnCount(g_self, CurrentRoom())
                : 0;
        }

        inline bool                                                 GetSpawn(int index, AmodWorldEntity& out)
        {
            out = AmodWorldEntity{};
            out.structSize = sizeof(AmodWorldEntity);
            return g_world != nullptr && g_world->structSize >= sizeof(AmodWorldApi) && g_world->GetSpawn(g_self, CurrentRoom(), index, &out) != 0;
        }

        /* 8-byte cell record. */
        inline bool                                                 CellRecord(int roomIndex, int column, int row, unsigned char out[8])
        {
            return g_world != nullptr && g_world->structSize >= sizeof(AmodWorldApi) && g_world->GetRoomCellRecord(g_self, roomIndex, column, row, out, 8) != 0;
        }

        class Entity
        {
        public:
            Entity() { m_data = AmodWorldEntity{}; }
            explicit Entity(const AmodWorldEntity& data) : m_data(data) {}

            bool                                                    IsValid() const { return m_data.structSize != 0; }

            int                                                     Kind() const { return m_data.kind; }
            const char* Name() const { return m_data.name != nullptr ? m_data.name : ""; }
            bool                                                    Is(const char* name) const;

            float                                                   GetX() const { return m_data.position[0]; }
            float                                                   GetY() const { return m_data.position[1]; }
            float                                                   GetZ() const { return m_data.position[2]; }

            /* Previous-frame position. */
            float                                                   GetPreviousX() const { return m_data.previousPosition[0]; }
            float                                                   GetPreviousY() const { return m_data.previousPosition[1]; }
            float                                                   GetPreviousZ() const { return m_data.previousPosition[2]; }

            /* Grid cell under the entity. */
            int                                                     GetColumn() const;
            int                                                     GetRow() const;

            int                                                     GetState() const { return m_data.state; }
            int                                                     GetSubState() const { return m_data.subState; }
            uint32_t                                                GetFlags() const { return m_data.flags; }
            bool                                                    IsDead() const { return m_data.isDead != 0; }

            /* Placement live byte. */
            int                                                     LiveByte() const { return m_data.isLive; }

            const AmodWorldEntity& Raw() const { return m_data; }

        private:
            AmodWorldEntity m_data;
        };

        /* Entities of the current room. */
        inline int                                                  CurrentEntityCount()
        {
            return EntityCount(CurrentRoom());
        }

        inline Entity                                               CurrentEntity(int index)
        {
            AmodWorldEntity data;
            return GetEntity(CurrentRoom(), index, data) ? Entity(data) : Entity();
        }

        inline float                                                ColumnLeft(int column)
        {
            return AMOD_ROOM_ORIGIN_X + column * AMOD_ROOM_CELL_SIZE;
        }

        inline float                                                RowTop(int row)
        {
            return AMOD_ROOM_ORIGIN_Y - row * AMOD_ROOM_CELL_SIZE;
        }

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


        inline int                                                  Entity::GetColumn() const
        {
            return ColumnAt(GetX());
        }

        inline int                                                  Entity::GetRow() const
        {
            return RowAt(GetY());
        }

        inline bool                                                 Entity::Is(const char* name) const
        {
            const char* mine = Name();
            if (name == nullptr)
            {
                return false;
            }

            while (*mine != '\0' && *name != '\0' && *mine == *name)
            {
                ++mine;
                ++name;
            }
            return *mine == *name;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_WORLD_API_H */
