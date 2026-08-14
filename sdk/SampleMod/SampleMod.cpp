#include <ForgeApi.h>

#include <stdlib.h>
#include <string.h>

using namespace Amod;

void __cdecl CellByteCommand(int argc, const char** argv, void*)
{
    if (argc < 3)
    {
        Console::Printf("cellbyte: <column> <row> [which 0..%d] [value 0..255]\n",
            AMOD_CELL_FREE_COUNT - 1);
        return;
    }

    const int column = atoi(argv[1]);
    const int row = atoi(argv[2]);
    const int which = argc > 3 ? atoi(argv[3]) : 0;

    if (argc < 5)
    {
        unsigned char record[AMOD_CELL_BYTES] = {};

        if (!World::CellRecord(-1, column, row, record))
        {
            Console::Printf("cellbyte: there is no cell %d,%d\n", column, row);
            return;
        }

        // The game's three first, then the five that are ours, so the two halves are told apart
        Console::Printf("cell %d,%d: type '%c' %02x %02x | free %02x %02x %02x %02x %02x\n", column,
            row, record[AMOD_CELL_TYPE], record[1], record[2], record[3], record[4], record[5],
            record[6], record[7]);
        return;
    }

    if (which < 0 || which >= AMOD_CELL_FREE_COUNT)
    {
        Console::Printf("cellbyte: there are %d free bytes, numbered 0 to %d\n",
            AMOD_CELL_FREE_COUNT, AMOD_CELL_FREE_COUNT - 1);
        return;
    }

    const unsigned char value = static_cast<unsigned char>(atoi(argv[4]));

    if (!World::SetCellByte(column, row, which, value))
    {
        Console::Printf("cellbyte: there is no cell %d,%d in this room\n", column, row);
        return;
    }

    const unsigned char back = World::CellByte(column, row, which);

    // Said as both numbers, because the dump above counts all eight and this one counts the free
    Console::Printf(back == value
        ? "cellbyte: %d,%d free byte %d (of the eight, number %d) holds %u, type still '%c'\n"
        : "cellbyte: %d,%d free byte %d (of the eight, number %d) went in %u, came back wrong '%c'\n",
        column, row, which, AMOD_CELL_FREE_FIRST + which, back,
        World::Cell(World::CurrentRoom(), column, row));
}

// Every spare byte of the room at once, to see them survive a walk out of the room and back
void __cdecl CellSweepCommand(int argc, const char** argv, void*)
{
    const unsigned char value = argc > 1 ? static_cast<unsigned char>(atoi(argv[1])) : 0;

    int written = 0;
    int wrong = 0;

    for (int row = 0; row < AMOD_ROOM_ROWS; ++row)
    {
        for (int column = 0; column < AMOD_ROOM_COLUMNS; ++column)
        {
            if (!World::SetCellByte(column, row, 0, value))
            {
                continue;
            }

            ++written;
            wrong += World::CellByte(column, row, 0) == value ? 0 : 1;
        }
    }

    Console::Printf("cellsweep: %d cell(s) marked with %u, %d did not hold it\n", written, value, wrong);
}

void __cdecl CellCheckCommand(int argc, const char** argv, void*)
{
    const unsigned char value = argc > 1 ? static_cast<unsigned char>(atoi(argv[1])) : 0;

    int held = 0;

    for (int row = 0; row < AMOD_ROOM_ROWS; ++row)
    {
        for (int column = 0; column < AMOD_ROOM_COLUMNS; ++column)
        {
            held += World::CellByte(column, row, 0) == value ? 1 : 0;
        }
    }

    Console::Printf("cellcheck: %d of %d cell(s) still hold %u\n", held,
        AMOD_ROOM_COLUMNS * AMOD_ROOM_ROWS, value);
}

// The console's words are the game's own memory, so anything kept has to be kept here first
void Keep(char* out, int capacity, const char* text)
{
    int at = 0;

    while (text != nullptr && text[at] != '\0' && at < capacity - 1)
    {
        out[at] = text[at];
        ++at;
    }

    out[at] = '\0';
}

// Reads a file the way the game reads one - out of data.pak if that is where it lives
void __cdecl FileReadCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Console::Print("fileread: <path> - how many bytes the game would get, and the first of them\n");
        return;
    }

    char path[260] = {};
    Keep(path, sizeof(path), argv[1]);

    const int size = Files::Read(path);

    if (size <= 0)
    {
        Console::Printf("fileread: the game has no '%s'\n", path);
        return;
    }

    unsigned char head[16] = {};
    Files::Read(path, head, sizeof(head));

    Console::Printf("fileread: '%s' is %d byte(s), starting %02x %02x %02x %02x %02x %02x %02x %02x\n",
        path, size, head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7]);
}

void __cdecl FileServeCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Console::Printf("fileserve: <path> [text] - %d path(s) answered now\n", Files::ServedCount());

        for (int at = 0; at < Files::ServedCount(); ++at)
        {
            Console::Printf("  %s\n", Files::ServedAt(at));
        }
        return;
    }

    // Copied out of the console's own words before anything is asked of the game
    char path[260] = {};
    Keep(path, sizeof(path), argv[1]);

    if (argc < 3)
    {
        Console::Printf(Files::Clear(path)
            ? "fileserve: '%s' is the game's own again\n"
            : "fileserve: nothing was being answered for '%s'\n", path);
        return;
    }

    char text[260] = {};
    Keep(text, sizeof(text), argv[2]);

    const int size = static_cast<int>(strlen(text));

    if (!Files::Serve(path, text, size))
    {
        Console::Printf("fileserve: '%s' would not take an answer\n", path);
        return;
    }

    const int back = Files::Read(path);

    Console::Printf("fileserve: '%s' now answers with %d byte(s), and reads back as %d\n", path, size, back);
}

void __cdecl RoomTexCommand(int argc, const char** argv, void*)
{
    const int room = argc > 1 ? atoi(argv[1]) : -1;

    const AmodTextureId wall = World::WallTexture(room);
    const AmodTextureId floor = World::FloorTexture(room);

    AmodTexture about = {};

    Console::Printf(Textures::Get(wall, about)
        ? "roomtex: $wall_tex is %u '%s', %dx%d\n" : "roomtex: this room names no $wall_tex\n",
        wall, about.name, about.width, about.height);

    about = AmodTexture{};

    Console::Printf(Textures::Get(floor, about)
        ? "roomtex: $floor_tex is %u '%s', %dx%d\n" : "roomtex: this room names no $floor_tex\n",
        floor, about.name, about.width, about.height);
}

void __cdecl TexHandleCommand(int argc, const char** argv, void*)
{
    const int index = argc > 1 ? atoi(argv[1]) : 0;
    const AmodTextureId texture = Textures::At(index);

    AmodTexture about = {};

    if (!Textures::Get(texture, about))
    {
        Console::Printf("texhandle: there is no texture %d of %d\n", index, Textures::Count());
        return;
    }

    const uint32_t handle = Textures::ToGame(texture);
    const AmodTextureId back = Textures::FromGame(handle);

    Console::Printf("texhandle: %d '%s' is id %u, the game calls it 0x%08X, and back is %u - %s\n",
        index, about.name, texture, handle, back, back == texture ? "the same" : "NOT the same");
}

bool g_panel = false;

void __cdecl DrawPanel(const AmodFrame*, void*)
{
    if (!g_panel)
    {
        return;
    }

    // Half-transparent black behind, so the alpha byte is proved and not assumed
    Render::Rect(20.0f, 20.0f, 260.0f, 96.0f, 0x80000000);
    Render::Rect(20.0f, 20.0f, 260.0f, 2.0f, 0xFFFFD060);

    Render::Text(nullptr, 30.0f, 44.0f, "2D: rect, sprite, aligned text", 1.0f, 0xFFEAEAEA,
        AMOD_TEXT_ALIGN_DEFAULT);

    // The room's own wall, put on the screen as a swatch
    const AmodTextureId wall = World::WallTexture();

    if (wall != AMOD_INVALID_TEXTURE)
    {
        Render::Sprite(wall, 30.0f, 60.0f, 48.0f, 48.0f);
        Render::Sprite(wall, 88.0f, 60.0f, 48.0f, 48.0f, 0x80FF8080);
    }
}

void __cdecl PanelCommand(int, const char**, void*)
{
    g_panel = !g_panel;

    Console::Printf("panel: %s - a rect, the room's $wall_tex twice, and a line of text\n",
        g_panel ? "on" : "off");
}

void __cdecl MusicCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Console::Printf("music: playing '%s'; 'mus <path> [cut]' or 'mus stop'\n",
            Sound::CurrentMusic());
        return;
    }

    // The console's words are the game's own scratch, and asking the game anything overwrites them
    char path[260] = {};
    Keep(path, sizeof(path), argv[1]);

    if (strcmp(path, "stop") == 0)
    {
        Console::Printf(Sound::StopMusic() ? "music: stopped\n" : "music: would not stop\n");
        return;
    }

    const bool fade = argc < 3 || strcmp(argv[2], "cut") != 0;

    Console::Printf(Sound::PlayMusic(path, fade)
        ? "music: '%s', %s\n" : "music: '%s' would not play (%s)\n",
        path, fade ? "faded in" : "cut in");
}

void PrintBlock(Data::Block block, int depth)
{
    char pad[33] = {};
    const int wide = depth * 2 < 32 ? depth * 2 : 32;

    for (int at = 0; at < wide; ++at)
    {
        pad[at] = ' ';
    }

    for (int at = 0; at < block.ExprCount(); ++at)
    {
        const char* const key = block.ExprNameAt(at);
        AmodValue value = {};

        if (!block[key].Read(value))
        {
            continue;
        }

        if (value.kind == AMOD_VALUE_STRING)
        {
            Console::Printf("%s%s \"%s\"\n", pad, key, value.text != nullptr ? value.text : "");
        }
        else if (value.kind == AMOD_VALUE_FLOAT)
        {
            Console::Printf("%s%s %.3f\n", pad, key, value.count > 0 ? value.numbers[0] : 0.0f);
        }
        else
        {
            Console::Printf("%s%s %d\n", pad, key, value.integer);
        }
    }

    for (int at = 0; at < block.ChildCount(); ++at)
    {
        const Data::Block child = block.ChildAt(at);

        Console::Printf("%s%s\n%s{\n", pad, child.Name(), pad);
        PrintBlock(child, depth + 1);
        Console::Printf("%s}\n", pad);
    }
}

void __cdecl BlockCommand(int argc, const char** argv, void*)
{
    char path[260] = {};

    if (argc > 1)
    {
        Keep(path, sizeof(path), argv[1]);
    }
    else
    {
        AmodWorldLevel level = {};

        if (!World::GetLevel(level) || level.script == nullptr)
        {
            Console::Print("block: no level is loaded, so say which file\n");
            return;
        }

        Keep(path, sizeof(path), level.script);
    }

    const Data::Block root = Data::ParseFile(path);

    if (!root.IsValid())
    {
        Console::Printf("block: '%s' would not read\n", path);
        return;
    }

    Console::Printf("block: %s\n", path);
    PrintBlock(root, 0);
    root.Free();
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("block", &BlockCommand);
    Console::AddCommand("mus", &MusicCommand);
    Render::OnFrame(&DrawPanel);
    Console::AddCommand("panel", &PanelCommand);
    Console::AddCommand("texhandle", &TexHandleCommand);
    Console::AddCommand("roomtex", &RoomTexCommand);
    Console::AddCommand("cellbyte", &CellByteCommand);
    Console::AddCommand("cellsweep", &CellSweepCommand);
    Console::AddCommand("cellcheck", &CellCheckCommand);
    Console::AddCommand("fileread", &FileReadCommand);
    Console::AddCommand("fileserve", &FileServeCommand);
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
