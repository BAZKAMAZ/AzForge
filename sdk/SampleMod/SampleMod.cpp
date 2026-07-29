#include <d3d8.h>        // from the DirectX 8 SDK headers in inc_d3d8; include before ForgeApi.h
#include <ForgeApi.h>

#include <stdio.h>
#include <string.h>

// A console command that reads the world out through the world interface:
//   world          - the level as a whole, and where the player is in it
//   world rooms    - every room of the level, with its maze cell
//   world maze     - the maze, as the room ids it holds
//   world cells    - the current room's cell grid, the one movement is expressed in
//   world boxes    - the current room's collision boxes
//
// It also subscribes to the three world events, so entering a level and walking between rooms
// reports itself in the console without anything being asked for.

static void PrintLevel()
{
    AmodWorldLevel level;
    if (!Amod::World::GetLevel(level))
    {
        Amod::Console::Printf("world: no level is loaded\n");
        return;
    }

    Amod::Console::Printf("level '%s' (#%d) from '%s'\n", level.name, level.number, level.script);
    Amod::Console::Printf("  rooms %d, maze %d rows, next '%s'\n",
                          level.roomCount, level.mazeRows, level.nextLevel);
    Amod::Console::Printf("  music '%s' from %.2f, background '%s'\n",
                          level.music, level.musicStart, level.background);
    Amod::Console::Printf("  time %.1f, bonus %d (time %.1f, count %d), demo %d/%d\n",
                          level.time, level.isBonus, level.bonusTime, level.bonusCount,
                          level.isDemoLevel, level.useForDemo);
    Amod::Console::Printf("  start at maze %d,%d cell %.2f,%.2f\n",
                          level.startMazeX, level.startMazeY, level.startCellX, level.startCellY);

    int x = -1;
    int y = -1;
    Amod::World::MazeCursor(x, y);

    const int current = Amod::World::CurrentRoom();
    AmodWorldRoom room;
    if (current >= 0 && Amod::World::GetRoom(current, room))
    {
        Amod::Console::Printf("  in room %d (id %d, '%s') at maze %d,%d\n",
                              room.index, room.id, room.file, x, y);
    }
    else
    {
        Amod::Console::Printf("  no room is current; maze cursor %d,%d\n", x, y);
    }
}

static void PrintRooms()
{
    const int count = Amod::World::RoomCount();
    if (count == 0)
    {
        Amod::Console::Printf("world: no level is loaded\n");
        return;
    }

    const int current = Amod::World::CurrentRoom();
    for (int index = 0; index < count; ++index)
    {
        AmodWorldRoom room;
        if (!Amod::World::GetRoom(index, room))
        {
            continue;
        }

        Amod::Console::Printf("%s%3d  id %-4d maze %2d,%-2d  boxes %-3d %s'%s'\n",
                              index == current ? "->" : "  ", room.index, room.id,
                              room.mazeX, room.mazeY, room.collisionBoxCount,
                              room.isLoaded ? "" : "(not loaded) ", room.file);
    }
}

static void PrintMaze()
{
    AmodWorldLevel level;
    if (!Amod::World::GetLevel(level))
    {
        Amod::Console::Printf("world: no level is loaded\n");
        return;
    }

    int cursorX = -1;
    int cursorY = -1;
    Amod::World::MazeCursor(cursorX, cursorY);

    // The maze is ragged, so each row is asked for its own width rather than the level for one.
    for (int y = 0; y < level.mazeRows; ++y)
    {
        char line[256];
        int used = 0;
        const int width = Amod::World::MazeRowWidth(y);

        for (int x = 0; x < width && used < static_cast<int>(sizeof(line)) - 8; ++x)
        {
            const int id = Amod::World::MazeRoomId(x, y);
            const size_t left = sizeof(line) - static_cast<size_t>(used);

            // A zero is a hole in the maze rather than a room; the brackets mark where the
            // player is standing.
            if (id == 0)
            {
                used += snprintf(line + used, left, "  .  ");
            }
            else
            {
                used += snprintf(line + used, left,
                                 (x == cursorX && y == cursorY) ? "[%3d]" : " %3d ", id);
            }
        }

        line[used] = '\0';
        Amod::Console::Printf("%2d %s\n", y, line);
    }
}

static void PrintCells()
{
    const int current = Amod::World::CurrentRoom();
    char cells[AMOD_ROOM_COLUMNS * AMOD_ROOM_ROWS];
    if (current < 0 || Amod::World::Cells(current, cells, static_cast<int>(sizeof(cells))) == 0)
    {
        Amod::Console::Printf("world: no room is current\n");
        return;
    }

    // A row at a time, exactly as the game indexes it: row 0 is the top of the room.
    for (int row = 0; row < AMOD_ROOM_ROWS; ++row)
    {
        char line[AMOD_ROOM_COLUMNS + 1];
        for (int column = 0; column < AMOD_ROOM_COLUMNS; ++column)
        {
            const char cell = cells[row * AMOD_ROOM_COLUMNS + column];
            line[column] = cell == '\0' ? ' ' : cell;
        }
        line[AMOD_ROOM_COLUMNS] = '\0';
        Amod::Console::Printf("%2d |%s|\n", row, line);
    }
}

static void PrintBoxes()
{
    const int current = Amod::World::CurrentRoom();
    AmodWorldRoom room;
    if (current < 0 || !Amod::World::GetRoom(current, room))
    {
        Amod::Console::Printf("world: no room is current\n");
        return;
    }

    Amod::Console::Printf("room %d has %d collision boxes\n", room.index, room.collisionBoxCount);
    for (int index = 0; index < room.collisionBoxCount; ++index)
    {
        AmodWorldBox box;
        if (Amod::World::CollisionBox(current, index, box))
        {
            Amod::Console::Printf("%3d  %7.3f %7.3f %7.3f  ->  %7.3f %7.3f %7.3f\n", index,
                                  box.min[0], box.min[1], box.min[2],
                                  box.max[0], box.max[1], box.max[2]);
        }
    }
}

static void __cdecl WorldCommand(int argc, const char** argv, void*)
{
    const char* const what = argc > 1 ? argv[1] : "";

    if (strcmp(what, "rooms") == 0)      { PrintRooms(); }
    else if (strcmp(what, "maze") == 0)  { PrintMaze(); }
    else if (strcmp(what, "cells") == 0) { PrintCells(); }
    else if (strcmp(what, "boxes") == 0) { PrintBoxes(); }
    else if (what[0] == '\0')            { PrintLevel(); }
    else
    {
        Amod::Console::Printf("world: rooms | maze | cells | boxes\n");
    }
}

static void __cdecl WorldEvent(const AmodWorldEventInfo* info, void*)
{
    switch (info->event)
    {
    case AMOD_WORLD_EVENT_LEVEL_LOADED:
    {
        AmodWorldLevel level;
        if (Amod::World::GetLevel(level))
        {
            Amod::Console::Printf("[world] level '%s' loaded, %d rooms\n",
                                  level.name, level.roomCount);
        }
        break;
    }

    case AMOD_WORLD_EVENT_LEVEL_UNLOADED:
        // The level is still readable here, which is what makes it worth reporting by name.
        Amod::Console::Printf("[world] level unloaded (%d rooms)\n", Amod::World::RoomCount());
        break;

    case AMOD_WORLD_EVENT_ROOM_CHANGED:
    {
        AmodWorldRoom room;
        if (Amod::World::GetRoom(info->roomIndex, room))
        {
            Amod::Console::Printf("[world] room %d (id %d, '%s') at maze %d,%d, from room %d\n",
                                  room.index, room.id, room.file, info->mazeX, info->mazeY,
                                  info->previousRoomIndex);
        }
        break;
    }
    }
}

// Everything below reaches the game directly, without the loader knowing anything about what
// is being reached. Room_Load is a routine AzForge exposes through no interface at all: the
// mod finds it by its own byte signature, detours it, and reports every .room the game opens.
//
//   Room_Load  __thiscall (room, file) -> bool
//     55              PUSH EBP
//     8D 6C 24 CC     LEA EBP,[ESP-0x34]
//     83 EC 34        SUB ESP,0x34
//
// Eight bytes, three whole instructions, none of them position-dependent and none of them a
// jump target from inside the routine - which is what the loader has to be told, because only
// the code that reversed it knows.
static const unsigned char kRoomLoadPrologue[] = { 0x55, 0x8D, 0x6C, 0x24, 0xCC, 0x83, 0xEC, 0x34 };

static const char kRoomLoadPattern[] =
    "55 8D 6C 24 CC 83 EC 34 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 83 EC 64 "
    "53 56 57 A1 ?? ?? ?? ?? 33 C5 50 8D 45 F4";

// __thiscall is __fastcall with the second register argument unused.
typedef char(__fastcall* RoomLoadRoutine)(void* room, void* unused, const char* file);
static RoomLoadRoutine g_originalRoomLoad = nullptr;

static char __fastcall RoomLoadDetour(void* room, void* unused, const char* file)
{
    const char result = g_originalRoomLoad(room, unused, file);
    Amod::Console::Printf("[interop] Room_Load('%s') -> %d\n", file, result);
    return result;
}

static void HookRoomLoad()
{
    if (!Amod::Interop::IsAvailable())
    {
        Amod::Log(AMOD_LOG_WARNING, "the direct interface is missing, Room_Load is not hooked");
        return;
    }

    AmodFindResult why = AMOD_FIND_NOT_FOUND;
    const uintptr_t target = Amod::Interop::Find(kRoomLoadPattern, &why);
    if (target == 0)
    {
        Amod::Log(AMOD_LOG_WARNING, "Room_Load was not found (result %d)", why);
        return;
    }

    void* trampoline = nullptr;
    const AmodToken token = Amod::Interop::Hook(target, &RoomLoadDetour, kRoomLoadPrologue,
                                                sizeof(kRoomLoadPrologue), &trampoline);
    if (token == AMOD_INVALID_TOKEN)
    {
        Amod::Log(AMOD_LOG_WARNING, "Room_Load at 0x%08X could not be hooked",
                  static_cast<unsigned>(target));
        return;
    }

    g_originalRoomLoad = reinterpret_cast<RoomLoadRoutine>(trampoline);
    Amod::Log(AMOD_LOG_INFO, "Room_Load found at 0x%08X and hooked by the mod itself",
              static_cast<unsigned>(target));
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Amod::Console::AddCommand("world", &WorldCommand);

    Amod::World::OnEvent(AMOD_WORLD_EVENT_LEVEL_LOADED, &WorldEvent);
    Amod::World::OnEvent(AMOD_WORLD_EVENT_LEVEL_UNLOADED, &WorldEvent);
    Amod::World::OnEvent(AMOD_WORLD_EVENT_ROOM_CHANGED, &WorldEvent);

    HookRoomLoad();

    Amod::Log(AMOD_LOG_INFO, "world interface %s", Amod::World::IsAvailable() ? "ready" : "missing");
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame* frame)
{
    (void)frame;
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
