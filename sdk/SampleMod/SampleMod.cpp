#include <ForgeApi.h>

#include <stdlib.h>
#include <string.h>

using namespace Amod;

float g_cursorX = 320.0f;
float g_cursorY = 240.0f;

int g_stopWorld = 1;
int g_stopRender = 1;
int g_stopInput = 1;
int g_suppressGame = 1;

int g_wanted = 0;
int g_gameEditorWas = 0;
int g_worldPhaseSeen = 0;

void __cdecl EditFrame(float seconds, void*)
{
    const float step = seconds * 240.0f;

    g_cursorX += Input::IsDown(0x27) ? step : 0.0f;
    g_cursorX -= Input::IsDown(0x25) ? step : 0.0f;
    g_cursorY += Input::IsDown(0x28) ? step : 0.0f;
    g_cursorY -= Input::IsDown(0x26) ? step : 0.0f;
}

void __cdecl EditDrawWorld(void*)
{
    g_worldPhaseSeen = 1;
}

void __cdecl EditDraw(void*)
{
    char line[128] = {};

    if (g_suppressGame != 0)
    {
        Render::Rect(0.0f, 0.0f, 640.0f, 480.0f, 0xC0101018);

        for (float x = 0.0f; x <= 640.0f; x += 40.0f)
        {
            Render::Rect(x, 0.0f, 1.0f, 480.0f, 0x40FFFFFF);
        }

        for (float y = 0.0f; y <= 480.0f; y += 40.0f)
        {
            Render::Rect(0.0f, y, 640.0f, 1.0f, 0x40FFFFFF);
        }

        Render::Rect(g_cursorX - 6.0f, g_cursorY - 6.0f, 12.0f, 12.0f, 0xFFFFCC00);
        Render::Text(350.0f, 56.0f, "arrows move the marker", 0.6f, 0xFF9999AA);
    }

    snprintf(line, sizeof(line), "edit: world %s, render %s, input %s, game editor %s",
        g_stopWorld != 0 ? "stopped" : "running",
        g_stopRender != 0 ? "stopped" : "drawn",
        g_stopInput != 0 ? "stopped" : "acting",
        g_suppressGame != 0 ? "silent" : "showing");

    Render::Text(16.0f, 16.0f, line, 0.6f, 0xFFFFFFFF);
    Render::Text(16.0f, 36.0f, g_worldPhaseSeen != 0
        ? "world phase reached" : "world phase silent - the render is stopped", 0.6f, 0xFF9999AA);

    g_worldPhaseSeen = 0;
}

void __cdecl EditCommand(int argc, const char** argv, void*)
{
    if (argc > 1)
    {
        if (strcmp(argv[1], "world") == 0) { g_stopWorld = g_stopWorld != 0 ? 0 : 1; }
        else if (strcmp(argv[1], "render") == 0) { g_stopRender = g_stopRender != 0 ? 0 : 1; }
        else if (strcmp(argv[1], "input") == 0) { g_stopInput = g_stopInput != 0 ? 0 : 1; }
        else if (strcmp(argv[1], "game") == 0) { g_suppressGame = g_suppressGame != 0 ? 0 : 1; }
        else
        {
            Console::Print("edit: world | render | input | game, each one a toggle\n");
            return;
        }

        AmodEditorMode mode = {};
        mode.structSize = sizeof(mode);
        mode.stopWorld = g_stopWorld;
        mode.stopRender = g_stopRender;
        mode.stopInput = g_stopInput;
        mode.suppressGame = g_suppressGame;

        LevelEditor::SetMode(mode);

        Console::Printf("edit: world %s, render %s, input %s, game editor %s\n",
            g_stopWorld != 0 ? "stops" : "runs",
            g_stopRender != 0 ? "stops" : "draws",
            g_stopInput != 0 ? "stops" : "acts",
            g_suppressGame != 0 ? "silent" : "shows");
        return;
    }

    g_wanted = g_wanted != 0 ? 0 : 1;

    Console::Printf(g_wanted != 0
        ? "edit: entered\n"
        : "edit: left - the backdrop and the key capture keep going\n");
}

int g_groupSample = -1;
int g_brushWall = -1;
int g_brushCross = -1;
int g_brushPickup = -1;

int __cdecl PaintWall(int column, int row, void*)
{
    Console::Printf("brush: painted %d,%d\n", column, row);
    return 1;
}

int __cdecl PaintCross(int column, int row, void*)
{
    for (int y = row - 1; y <= row + 1; ++y)
    {
        for (int x = column - 1; x <= column + 1; ++x)
        {
            LevelEditor::SetGridCell(x, y, 'W');
        }
    }

    return 0;
}

void __cdecl DrawPickup(float x, float y, float size, void*)
{
    Render::Rect(x + size * 0.5f, y + size * 0.5f, size * 3.0f, size * 3.0f, 0xFF33CC66);
}

const char* __cdecl SaveWall(int roomX, int roomY, void*)
{
    static char block[256] = {};

    snprintf(block, sizeof(block),
        "$sample_block \n{\n\t$room %d %d\n\t$note \"written by SampleMod\"\n}\n\n", roomX, roomY);

    return block;
}

const char* __cdecl SavePickup(int roomX, int roomY, void*)
{
    static char blocks[4096] = {};

    int columns = 0;
    int rows = 0;
    int at = 0;

    blocks[0] = '\0';

    if (!LevelEditor::GridSize(columns, rows))
    {
        return nullptr;
    }

    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            // snprintf answers with what it wanted to write, so the room left is checked first and
            // never derived from its return
            const int left = static_cast<int>(sizeof(blocks)) - at;

            if (LevelEditor::GetGridCell(column, row, AMOD_BRUSH_ENTITIES) != 'X' || left <= 128)
            {
                continue;
            }

            at += snprintf(blocks + at, left,
                "$bonus\n{\n\t$room_pos %i %i\n\t$pos %i %i\n\t$name \"sample\"\n}\n\n",
                roomX, roomY, column, row);
        }
    }

    return at != 0 ? blocks : nullptr;
}

AmodTextureId g_brushTexture = AMOD_INVALID_TEXTURE;

void __cdecl BrushCommand(int argc, const char** argv, void*)
{
    if (argc > 1 && strcmp(argv[1], "add") == 0)
    {
        if (g_brushWall >= 0)
        {
            Console::Print("brush: already added\n");
            return;
        }

        g_groupSample = LevelEditor::AddGroup("Sample", AMOD_BRUSH_GEOMETRY);

        const int group = g_groupSample >= 0 ? g_groupSample : AMOD_BRUSH_GEOMETRY;

        AmodBrushDesc wall = {};
        wall.structSize = sizeof(wall);
        wall.label = "Sample Wall";
        wall.cell = 'W';
        wall.key = '1';
        wall.group = group;
        wall.texture = g_brushTexture;
        wall.paint = &PaintWall;
        wall.kind = AMOD_BRUSH_KIND_CTRL;
        wall.save = &SaveWall;

        g_brushWall = LevelEditor::AddBrush(wall);
        g_brushCross = LevelEditor::AddBrush("Wall 3x3", 'W', '2', group, &PaintCross);

        AmodBrushDesc pickup = {};
        pickup.structSize = sizeof(pickup);
        pickup.label = "Sample Pickup";
        pickup.cell = 'X';
        pickup.key = '5';
        pickup.group = AMOD_BRUSH_ENTITIES;
        pickup.kind = AMOD_BRUSH_KIND_BONUS;
        pickup.draw = &DrawPickup;
        pickup.save = &SavePickup;

        g_brushPickup = LevelEditor::AddBrush(pickup);

        Console::Printf(g_brushWall >= 0
            ? "brush: group %d added - M walks past the game's two, B picks\n"
            : "brush: would not be added (group %d)\n", g_groupSample);
        return;
    }

    if (argc > 1 && strcmp(argv[1], "remove") == 0)
    {
        LevelEditor::RemoveBrush(g_brushWall);
        LevelEditor::RemoveBrush(g_brushCross);
        LevelEditor::RemoveBrush(g_brushPickup);
        LevelEditor::RemoveGroup(g_groupSample);

        g_brushWall = -1;
        g_brushCross = -1;
        g_brushPickup = -1;
        g_groupSample = -1;

        Console::Print("brush: removed\n");
        return;
    }

    int currentGroup = -1;
    int currentIndex = -1;

    LevelEditor::GetCurrentBrush(currentGroup, currentIndex);

    for (int group = 0; group < LevelEditor::GroupCount(); ++group)
    {
        Console::Printf("brush: group %d '%s' paints grid %d, holds %d\n", group,
            LevelEditor::GroupName(group), LevelEditor::GroupLayer(group),
            LevelEditor::BrushCount(group));

        for (int at = 0; at < LevelEditor::BrushCount(group); ++at)
        {
            AmodBrushInfo info = {};

            if (!LevelEditor::GetBrush(group, at, info))
            {
                continue;
            }

            Console::Printf("  %s%2d  cell '%c'  key '%c'  %08X  %-20s%s\n",
                (group == currentGroup && at == currentIndex) ? "->" : "  ",
                at, info.cell != 0 ? info.cell : ' ', info.key != 0 ? info.key : ' ',
                info.colour, info.label, info.isMine != 0 ? "  (mine)" : "");
        }
    }

    Console::Print("brush: 'brush add' and 'brush remove'\n");
}

void __cdecl RoomCreated(int x, int y, void*)
{
    int columns = 0;
    int rows = 0;

    if (!LevelEditor::GridSize(columns, rows))
    {
        return;
    }

    for (int column = 0; column < columns; ++column)
    {
        const int row = column * rows / columns;

        LevelEditor::SetGridCell(column, row, 'W');
        LevelEditor::SetGridCell(column, rows - 1 - row, 'W');
    }

    Console::Printf("maze: room %d,%d made and crossed\n", x, y);
}

void __cdecl RoomLoaded(const AmodRoomFiles* files, void*)
{
    const Data::Block root = Data::ParseFile(files->bonuses);
    int restored = 0;

    if (!root.IsValid())
    {
        return;
    }

    for (int at = 0; at < root.ChildCount(); ++at)
    {
        const Data::Block block = root.ChildAt(at);

        if (strcmp(block.Name(), "bonus") != 0 || strcmp(block["name"].Text(), "sample") != 0)
        {
            continue;
        }

        LevelEditor::SetGridCell(static_cast<int>(block["pos"].Number(0)),
            static_cast<int>(block["pos"].Number(1)), 'X', AMOD_BRUSH_ENTITIES);

        ++restored;
    }

    root.Free();
    Console::Printf("maze: room %d,%d loaded, %d pickup(s) put back\n", files->x, files->y, restored);
}

void __cdecl MazeCommand(int argc, const char** argv, void*)
{
    const char* const what = argc > 1 ? argv[1] : "";
    const int x = argc > 2 ? atoi(argv[2]) : 0;
    const int y = argc > 3 ? atoi(argv[3]) : 0;
    const char* const path = argc > 4 ? argv[4] : "rooms/sample.txt";

    if (strcmp(what, "goto") == 0)
    {
        if (LevelEditor::SetMazeCursor(x, y)) { Console::Printf("maze: on %d,%d\n", x, y); }
        else { Console::Printf("maze: goto %d,%d - %s\n", x, y, LastError()); }
        return;
    }

    // Grows the maze to reach, and leaves the cursor on the room whether it existed or not
    if (strcmp(what, "new") == 0)
    {
        if (LevelEditor::CreateRoom(x, y)) { Console::Printf("maze: cursor on %d,%d\n", x, y); }
        else { Console::Printf("maze: new %d,%d - %s\n", x, y, LastError()); }
        return;
    }

    if (strcmp(what, "del") == 0)
    {
        if (LevelEditor::RemoveRoom(x, y)) { Console::Printf("maze: %d,%d is empty\n", x, y); }
        else { Console::Printf("maze: del %d,%d - %s\n", x, y, LastError()); }
        return;
    }

    if (strcmp(what, "clear") == 0)
    {
        if (LevelEditor::ClearRoom(x, y)) { Console::Printf("maze: %d,%d emptied\n", x, y); }
        else { Console::Printf("maze: clear %d,%d - %s\n", x, y, LastError()); }
        return;
    }

    if (strcmp(what, "row") == 0 || strcmp(what, "col") == 0)
    {
        const char* const which = what[0] == 'r' ? "row" : "column";

        if (what[0] == 'r' ? LevelEditor::InsertRow(x) : LevelEditor::InsertColumn(x))
        {
            Console::Printf("maze: %s inserted before %d\n", which, x);
        }
        else
        {
            Console::Printf("maze: %s at %d - %s\n", which, x, LastError());
        }
        return;
    }

    // The .txt plus its _ctrls, _monsters and _bonuses - the brushes are asked for their blocks
    if (strcmp(what, "save") == 0)
    {
        if (LevelEditor::SaveRoom(x, y, path)) { Console::Printf("maze: %d,%d written to '%s'\n", x, y, path); }
        else { Console::Printf("maze: save %d,%d - %s\n", x, y, LastError()); }
        return;
    }

    if (strcmp(what, "load") == 0)
    {
        if (LevelEditor::LoadRoom(x, y, path)) { Console::Printf("maze: '%s' is now %d,%d\n", path, x, y); }
        else { Console::Printf("maze: load '%s' - %s\n", path, LastError()); }
        return;
    }

    int columns = 0;
    int rows = 0;
    int cursorX = -1;
    int cursorY = -1;

    if (!LevelEditor::MazeSize(columns, rows))
    {
        Console::Printf("maze: %s\n", LastError());
        return;
    }

    LevelEditor::GetMazeCursor(cursorX, cursorY);
    Console::Printf("maze: %dx%d, cursor on %d,%d\n", columns, rows, cursorX, cursorY);

    // The editor allocates its maze at startup but leaves it 0x0 until a level is loaded into it
    if (columns <= 0 || rows <= 0)
    {
        Console::Print("maze: empty - 'maze new 0 0' puts the first room in\n");
    }

    for (int row = 0; row < rows; ++row)
    {
        char line[80] = {};
        int at = 0;

        for (int column = 0; column < columns && at < static_cast<int>(sizeof(line)) - 1; ++column)
        {
            line[at++] = (column == cursorX && row == cursorY) ? '@'
                : (LevelEditor::HasRoom(column, row) ? '#' : '.');
        }

        Console::Printf("  %s\n", line);
    }

    Console::Print("maze: goto|new|del|clear <x> <y>, row|col <at>, save|load <x> <y> [path]\n");
}

const char* __cdecl LevelWrite(const AmodLevelPart* part, void*)
{
    static char block[512] = {};

    if (part->part == AMOD_LEVEL_HEADER)
    {
        return "\t$next_level \"\"\n\t$music \"music/light1.ogg\"\n\t$bonus 0\n"
            "\t$bonus_time 0\n\t$bonus_count 0\n\t$quest_list\n\t{\n\t}\n";
    }

    // The parser refuses a level with no $player_start, and the loader will not invent one
    if (part->part == AMOD_LEVEL_TAIL)
    {
        return "$player_start\n{\n\t$room_pos 0 0\n\t$pos 20 14\n}\n\n";
    }

    // Per room, which the game's own writer cannot do: it gives every room the same two textures
    snprintf(block, sizeof(block),
        "\t\t$floor_tex \"textures/rooms/3_02_floor.jpg\"\n"
        "\t\t$wall_tex \"textures/rooms/3_01_wall.jpg\"\n\n"
        "\t\t$active_items \"%s/%03i_ctrls.txt\"\n\n"
        "\t\t$monsters \"%s/%03i_monsters.txt\"\n"
        "\t\t$bonuses \"%s/%03i_bonuses.txt\"\n\n"
        "\t\t$type \"%s\"\n\t\t$demo \"\"\n\t\t$music \"\"\n",
        part->rooms, part->id, part->rooms, part->id, part->rooms, part->id,
        ((part->x + part->y) % 2) != 0 ? "dark" : "light");

    return block;
}

const char* __cdecl RoomWrite(const AmodRoomPart* part, void*)
{
    static char header[256] = {};

    if (part->part != AMOD_ROOM_HEADER)
    {
        return nullptr;
    }

    const float light = ((part->x + part->y) % 2) != 0 ? 0.15f : 0.35f;

    snprintf(header, sizeof(header),
        "$back_wall 1\n$light_ambient %.2f %.2f %.2f\n$tex_wall_scale 1\n$tex_floor_scale 1\n\n"
        "$ladder_cut 1\n$ladder_cross 1\n$chain_cut 1\n$chain_cross 1\n\n", light, light, light);

    return header;
}

void __cdecl LevelCommand(int argc, const char** argv, void*)
{
    const char* const path = argc > 1 ? argv[1] : "levels/sample.txt";

    if (LevelEditor::SaveLevel(path)) { Console::Printf("savelevel: '%s' written\n", path); }
    else { Console::Printf("savelevel: '%s' - %s\n", path, LastError()); }
}


int g_backdrop = 0;

void __cdecl EditBackdrop(void*)
{
    if (g_backdrop == 0)
    {
        return;
    }

    Render::Rect(0.0f, 0.0f, 640.0f, 480.0f, 0xFF101828);

    for (float y = 0.0f; y < 480.0f; y += 8.0f)
    {
        Render::Rect(0.0f, y, 640.0f, 1.0f, 0xFF16203A);
    }
}

int __cdecl EditKey(int key, int, void*)
{
    if (key != 0x71)
    {
        return 0;
    }

    Console::Print("part: F2 taken from the editor\n");
    return 1;
}

const char* const g_partNames[] = { "grid", "lines", "status", "palette", "maze" };

void __cdecl PartCommand(int argc, const char** argv, void*)
{
    const char* const what = argc > 1 ? argv[1] : "";

    if (strcmp(what, "back") == 0)
    {
        if (argc > 2) { g_backdrop = strcmp(argv[2], "off") == 0 ? 0 : 1; }
        else { g_backdrop = g_backdrop != 0 ? 0 : 1; }

        Console::Printf("part: backdrop %s\n", g_backdrop != 0 ? "on" : "off");
        return;
    }

    if (strcmp(what, "say") == 0 && argc > 2)
    {
        const int line = atoi(argv[2]);

        if (LevelEditor::SetStatusLine(line, argc > 3 ? argv[3] : nullptr))
        {
            Console::Printf("part: line %d is now '%s'\n", line, LevelEditor::GetStatusLine(line));
        }
        else
        {
            Console::Printf("part: line %d - %s\n", line, LastError());
        }
        return;
    }

    // A game brush keeps its name and its colour in its own record; nothing gives them back
    if ((strcmp(what, "name") == 0 || strcmp(what, "colour") == 0) && argc > 3)
    {
        const int group = atoi(argv[2]);
        const int index = atoi(argv[3]);
        AmodBrushInfo info = {};

        const bool done = what[0] == 'n'
            ? LevelEditor::SetBrushLabel(group, index, argc > 4 ? argv[4] : nullptr)
            : LevelEditor::SetBrushColour(group, index, argc > 4 ? strtoul(argv[4], nullptr, 16) : 0);

        if (done && LevelEditor::GetBrush(group, index, info))
        {
            Console::Printf("part: brush %d of group %d is '%s' in %08X\n", index, group,
                info.label, info.colour);
        }
        else
        {
            Console::Printf("part: brush %d of group %d - %s\n", index, group, LastError());
        }
        return;
    }

    if (strcmp(what, "icon") == 0 && argc > 2)
    {
        const int icon = atoi(argv[2]);
        const AmodTextureId texture = argc > 3
            ? Textures::Load(argv[3])
            : AMOD_INVALID_TEXTURE;

        if (LevelEditor::SetIcon(icon, texture)) { Console::Printf("part: icon %d set\n", icon); }
        else { Console::Printf("part: icon %d - %s\n", icon, LastError()); }
        return;
    }

    if (strcmp(what, "screen") == 0)
    {
        if (argc > 2) { LevelEditor::SetScreen(atoi(argv[2])); }

        Console::Printf("part: screen %d, the %s one\n", LevelEditor::GetScreen(),
            LevelEditor::GetScreen() == AMOD_EDITOR_SCREEN_MAZE ? "maze" : "room");
        return;
    }

    for (int at = 0; at < 5; ++at)
    {
        AmodEditorPart place = {};

        if (strcmp(what, g_partNames[at]) != 0 || !LevelEditor::GetPart(at, place))
        {
            continue;
        }

        if (argc > 2 && strcmp(argv[2], "off") == 0) { place.shown = 0; }
        else if (argc > 2 && strcmp(argv[2], "on") == 0) { place.shown = 1; }
        else if (argc > 3)
        {
            place.x = static_cast<float>(atof(argv[2]));
            place.y = static_cast<float>(atof(argv[3]));
            place.size = argc > 4 ? static_cast<float>(atof(argv[4])) : place.size;
            place.scale = argc > 5 ? static_cast<float>(atof(argv[5])) : place.scale;
        }
        else
        {
            Console::Printf("part: %s off|on, or %s <x> <y> [size] [scale]\n", what, what);
            return;
        }

        if (LevelEditor::SetPart(at, place))
        {
            Console::Printf("part: %s %s at %.0f,%.0f size %.0f scale %.2f\n", g_partNames[at],
                place.shown != 0 ? "shown" : "hidden", place.x, place.y, place.size, place.scale);
        }
        else
        {
            Console::Printf("part: %s - %s\n", g_partNames[at], LastError());
        }
        return;
    }

    for (int at = 0; at < 5; ++at)
    {
        AmodEditorPart place = {};

        if (LevelEditor::GetPart(at, place))
        {
            Console::Printf("part: %-8s %s  %.0f,%.0f  size %.0f  scale %.2f\n", g_partNames[at],
                place.shown != 0 ? "shown " : "hidden", place.x, place.y, place.size, place.scale);
        }
    }

    for (int line = 0; line < AMOD_EDITOR_STATUS_LINES; ++line)
    {
        Console::Printf("part: line %d  \"%s\"\n", line, LevelEditor::GetStatusLine(line));
    }

    Console::Print("part: <name> off|on, <name> <x> <y> [size] [scale], 'part say <line> [text]',\n");
    Console::Print("      'part name|colour <group> <index> [text|AARRGGBB]', 'part screen [0|1]',\n");
    Console::Print("      'part icon 0|1 [path]', 'part back [on|off]'\n");
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("edit", &EditCommand);
    Console::AddCommand("brush", &BrushCommand);
    Console::AddCommand("maze", &MazeCommand);
    Console::AddCommand("part", &PartCommand);
    Console::AddCommand("savelevel", &LevelCommand);

    g_brushTexture = Textures::Load("textures/editor/cursor.png");

    LevelEditor::OnFrame(&EditFrame);
    LevelEditor::OnDraw(&EditDraw);
    LevelEditor::OnDraw(AMOD_EDITOR_DRAW_WORLD, &EditDrawWorld);

    // These come whether or not a mod is holding the editor
    LevelEditor::OnDraw(AMOD_EDITOR_DRAW_BACKDROP, &EditBackdrop);
    LevelEditor::OnKey(&EditKey); //Uncomment it if you want to suppress F2 keystrokes.
    LevelEditor::OnRoomCreated(&RoomCreated);
    LevelEditor::OnRoomLoaded(&RoomLoaded);
    LevelEditor::OnLevelWrite(&LevelWrite);
    LevelEditor::OnRoomWrite(&RoomWrite);

    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
    const int on = LevelEditor::IsGameEditorOn() ? 1 : 0;

    if (on != g_gameEditorWas)
    {
        g_gameEditorWas = on;
        g_wanted = on;
    }

    if (g_wanted != 0 && !LevelEditor::IsActive())
    {
        LevelEditor::Enter(g_stopWorld != 0, g_stopRender != 0, g_stopInput != 0, g_suppressGame != 0);
    }
    else if (g_wanted == 0 && LevelEditor::IsActive())
    {
        LevelEditor::Leave();
    }
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
