#include <ForgeApi.h>

#include <windows.h>

#include <stdlib.h>
#include <string.h>

using namespace Amod;

const char* BindName(int bind)
{
    switch (bind)
    {
    case AMOD_KEY_CVAR:    return "cvar";
    case AMOD_KEY_COMMAND: return "command";
    }
    return "nothing";
}

void ShowKey(int key)
{
    AmodKeyInfo info = {};

    if (!Input::GetKey(key, &info))
    {
        Console::Printf("keys: there is no key %d\n", key);
        return;
    }

    Console::Printf("keys: %d '%s'%s%s\n", key, info.name != nullptr ? info.name : "?",
        info.isJoystick != 0 ? "  joystick" : "", Input::IsDown(key) ? "  down" : "");

    if (info.bind == AMOD_KEY_UNBOUND)
    {
        Console::Print("      bound to nothing\n");
        return;
    }

    Console::Printf("      %s '%s'%s\n", BindName(info.bind),
        info.target != nullptr ? info.target : "?", info.held != 0 ? ", while held" : "");

    for (int i = 0; i < info.paramCount; ++i)
    {
        const char* const param = Input::KeyParam(key, i);
        Console::Printf("      [%d] '%s'\n", i, param != nullptr ? param : "");
    }
}

void ListKeys(bool all)
{
    const int count = Input::KeyCount();
    int named = 0;
    int bound = 0;

    for (int key = 0; key < count; ++key)
    {
        AmodKeyInfo info = {};

        if (!Input::GetKey(key, &info) || info.name == nullptr)
        {
            continue;
        }

        ++named;

        if (info.bind != AMOD_KEY_UNBOUND)
        {
            ++bound;
        }
        else if (!all)
        {
            continue;
        }

        Console::Printf("  %3d %-12s %-8s %s%s\n", key, info.name, BindName(info.bind),
            info.target != nullptr ? info.target : "", info.held != 0 ? "  (held)" : "");
    }

    Console::Printf("keys: %d of %d named, %d bound, joystick %s\n", named, count, bound,
        Input::HasJoystick() ? "captured" : "absent");
}

void BindCommandTo(int argc, const char** argv)
{
    if (argc < 4)
    {
        Console::Print("keys bind <key> <command> [held] [parameters...]\n");
        return;
    }

    const int key = Input::FindKey(argv[2]);

    if (key < 0)
    {
        Console::Printf("keys: the game knows no key called '%s'\n", argv[2]);
        return;
    }

    const bool held = argc > 4 && atoi(argv[4]) != 0;
    const int extra = argc > 5 ? argc - 5 : 0;

    if (Input::BindCommand(key, argv[3], held, extra, extra != 0 ? argv + 5 : nullptr))
    {
        ShowKey(key);
    }
    else
    {
        Console::Printf("keys: '%s' is no command of the game's\n", argv[3]);
    }
}

void BindCvarTo(int argc, const char** argv)
{
    if (argc < 4)
    {
        Console::Print("keys cvar <key> <cvar> [held]\n");
        return;
    }

    const int key = Input::FindKey(argv[2]);

    if (key < 0)
    {
        Console::Printf("keys: the game knows no key called '%s'\n", argv[2]);
        return;
    }

    if (Input::BindCvar(key, argv[3], argc > 4 && atoi(argv[4]) != 0))
    {
        ShowKey(key);
    }
    else
    {
        Console::Printf("keys: '%s' is no variable of the game's\n", argv[3]);
    }
}

int g_holdKey = -1;
int g_holdFrames = 0;

void __cdecl KeysCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        ListKeys(false);
        return;
    }

    const char* const what = argv[1];

    if (strcmp(what, "all") == 0)
    {
        ListKeys(true);
    }
    else if (strcmp(what, "bind") == 0)
    {
        BindCommandTo(argc, argv);
    }
    else if (strcmp(what, "cvar") == 0)
    {
        BindCvarTo(argc, argv);
    }
    else if (strcmp(what, "keep") == 0 && argc > 2)
    {
        const int key = Input::FindKey(argv[2]);

        if (key < 0)
        {
            Console::Printf("keys: the game knows no key called '%s'\n", argv[2]);
            return;
        }

        const bool keep = argc > 3 ? atoi(argv[3]) != 0 : true;
        const bool seal = argc > 4 && atoi(argv[4]) != 0;

        const char* const how = !Input::Keep(key, keep, seal) ? "refused"
                              : !keep ? "is not kept any more"
                              : seal ? "is kept for next time, sealed"
                              : "is kept for next time";

        Console::Printf("keys: '%s' %s\n", argv[2], how);
    }
    else if ((strcmp(what, "press") == 0 || strcmp(what, "hold") == 0) && argc > 2)
    {
        const int key = Input::FindKey(argv[2]);

        if (key < 0)
        {
            Console::Printf("keys: the game knows no key called '%s'\n", argv[2]);
            return;
        }

        if (strcmp(what, "press") == 0)
        {
            Console::Printf("keys: '%s' %s\n", argv[2],
                Input::Press(key) ? "pressed once" : "refused");
            return;
        }

        // Held for a while by default, so a hold cannot be left on by accident.
        const int frames = argc > 3 ? atoi(argv[3]) : 60;
        const bool alone = argc > 4 && atoi(argv[4]) != 0;

        if (frames == 0)
        {
            g_holdKey = -1;
            Console::Printf("keys: '%s' %s\n", argv[2],
                Input::Hold(key, false) ? "let go" : "refused");
            return;
        }

        if (!Input::Hold(key, true, alone))
        {
            Console::Printf("keys: '%s' refused\n", argv[2]);
            return;
        }

        g_holdKey = key;
        g_holdFrames = frames;
        Console::Printf("keys: '%s' held down for %d frame(s)%s\n", argv[2], frames,
            alone ? ", and the player's own keys are ignored" : "");
    }
    else if (strcmp(what, "unbind") == 0 && argc > 2)
    {
        const int key = Input::FindKey(argv[2]);

        if (key < 0)
        {
            Console::Printf("keys: the game knows no key called '%s'\n", argv[2]);
            return;
        }

        Console::Printf("keys: '%s' %s\n", argv[2],
            Input::Unbind(key) ? "bound to nothing now" : "refused");
    }
    else
    {
        const int key = Input::FindKey(what);

        if (key >= 0)
        {
            ShowKey(key);
            return;
        }

        Console::Print("keys, 'keys all', 'keys <name>', 'keys unbind <name>',\n");
        Console::Print("      'keys keep <name> [0|1] [seal]',\n");
        Console::Print("      'keys press <name>', 'keys hold <name> [frames] [alone]',\n");
        Console::Print("      'keys bind <name> <command> [held] [parameters...]',\n");
        Console::Print("      'keys cvar <name> <cvar> [held]'\n");
    }
}

void __cdecl FreezeCommand(int argc, const char** argv, void*)
{
    const bool wanted = argc > 1 ? atoi(argv[1]) != 0 : !World::IsPaused();

    if (!World::SetPaused(wanted))
    {
        Console::Print("freeze: refused\n");
        return;
    }

    Console::Printf("freeze: the world is %s - a key press starts it again\n",
        World::IsPaused() ? "stopped" : "running");
}

void ShowMaterials(AmodModelId model, const char* name)
{
    const int count = Mmd::MaterialCount(model);

    if (count == 0)
    {
        Console::Printf("mtl: '%s' has no materials, so the game draws it whole\n", name);
        return;
    }

    Console::Printf("mtl: '%s' has %d material(s)\n", name, count);

    for (int i = 0; i < count; ++i)
    {
        AmodModelMaterial material = {};

        if (Mmd::GetMaterial(model, i, &material))
        {
            AmodTexture texture = {};
            const bool known = Textures::Get(material.texture, texture);

            Console::Printf("  %d  from index %d, %d triangle(s), texture %s\n", i,
                material.firstIndex, material.primitiveCount,
                known && texture.name != nullptr ? texture.name : "(none)");
        }
    }
}

void ListModels()
{
    const int count = Mmd::Count();

    if (count == 0)
    {
        Console::Print("mtl: no models are loaded - start a level first\n");
        return;
    }

    Console::Printf("mtl: %d model(s) loaded\n", count);

    for (int i = 0; i < count; ++i)
    {
        const AmodModelId model = Mmd::At(i);
        AmodModel info = {};

        if (!Mmd::Get(model, info))
        {
            continue;
        }

        Console::Printf("  %-28s %d frame(s)  %d verts  %d prims  %d material(s)\n",
            info.name != nullptr ? info.name : "?", info.frameCount, info.vertexCount,
            info.primitiveCount, Mmd::MaterialCount(model));
    }

    Console::Print("mtl: 'mtl split <model> <texture> [texture]' cuts one in two\n");
}

void SplitModel(int argc, const char** argv)
{
    if (argc < 4)
    {
        Console::Print("mtl split <model> <textureA> [textureB]\n");
        return;
    }

    const AmodModelId model = Mmd::Find(argv[2]);
    AmodModel info = {};

    if (model == AMOD_INVALID_MODEL || !Mmd::Get(model, info))
    {
        Console::Printf("mtl: no model called '%s' is loaded\n", argv[2]);
        return;
    }

    const int primitives = info.primitiveCount > 0 ? info.primitiveCount : info.indexCount / 3;

    if (primitives < 2)
    {
        Console::Printf("mtl: '%s' has %d triangle(s), too few to cut\n", argv[2], primitives);
        return;
    }

    const uint32_t first = Mmd::Texture(argv[3]);
    const uint32_t second = argc > 4 ? Mmd::Texture(argv[4]) : 0;

    if (first == 0)
    {
        Console::Printf("mtl: the game has no texture called '%s'\n", argv[3]);
        return;
    }
    if (argc > 4 && second == 0)
    {
        Console::Printf("mtl: the game has no texture called '%s'\n", argv[4]);
        return;
    }

    const int half = primitives / 2;

    Mmd::ClearMaterials(model);
    Mmd::AddMaterial(model, 0, half, first);
    Mmd::AddMaterial(model, half * 3, primitives - half, second != 0 ? second : first);

    ShowMaterials(model, argv[2]);
}

void AddOneMaterial(int argc, const char** argv)
{
    if (argc < 6)
    {
        Console::Print("mtl add <model> <firstIndex> <triangles> <texture>\n");
        return;
    }

    const AmodModelId model = Mmd::Find(argv[2]);

    if (model == AMOD_INVALID_MODEL)
    {
        Console::Printf("mtl: no model called '%s' is loaded\n", argv[2]);
        return;
    }

    const uint32_t texture = Mmd::Texture(argv[5]);

    if (texture == 0)
    {
        Console::Printf("mtl: the game has no texture called '%s'\n", argv[5]);
        return;
    }

    const int added = Mmd::AddMaterial(model, atoi(argv[3]), atoi(argv[4]), texture);

    Console::Printf("mtl: material %d %s\n", added, added >= 0 ? "added" : "refused");
    ShowMaterials(model, argv[2]);
}

void __cdecl MaterialCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        ListModels();
        return;
    }

    const char* const what = argv[1];

    if (strcmp(what, "split") == 0)
    {
        SplitModel(argc, argv);
    }
    else if (strcmp(what, "add") == 0)
    {
        AddOneMaterial(argc, argv);
    }
    else if (strcmp(what, "clear") == 0 && argc > 2)
    {
        const AmodModelId model = Mmd::Find(argv[2]);

        Console::Printf("mtl: '%s' %s\n", argv[2],
            model != AMOD_INVALID_MODEL && Mmd::ClearMaterials(model)
                ? "draws whole again" : "was not found");
    }
    else if (strcmp(what, "save") == 0 && argc > 3)
    {
        const AmodModelId model = Mmd::Find(argv[2]);

        if (model == AMOD_INVALID_MODEL)
        {
            Console::Printf("mtl: no model called '%s' is loaded\n", argv[2]);
            return;
        }

        Console::Printf("mtl: '%s' -> '%s' %s, %d material(s) went with it\n", argv[2], argv[3],
            Mmd::Save(model, argv[3]) ? "written" : "refused", Mmd::MaterialCount(model));
    }
    else if (strcmp(what, "load") == 0 && argc > 2)
    {
        const AmodModelId model = Mmd::Load(argv[2]);

        if (model == AMOD_INVALID_MODEL)
        {
            Console::Printf("mtl: '%s' would not load\n", argv[2]);
            return;
        }

        ShowMaterials(model, argv[2]);
    }
    else
    {
        const AmodModelId model = Mmd::Find(what);

        if (model != AMOD_INVALID_MODEL)
        {
            ShowMaterials(model, what);
            return;
        }

        Console::Print("mtl, 'mtl <model>', 'mtl split <model> <texA> [texB]',\n");
        Console::Print("     'mtl add <model> <firstIndex> <triangles> <texture>',\n");
        Console::Print("     'mtl clear <model>', 'mtl save <model> <path>', 'mtl load <path>'\n");
    }
}

// An atlas with real letters on it, drawn by Windows: no art file, and any face on the machine.
const char* const kFontChars =
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
    "`abcdefghijklmnopqrstuvwxyz{|}~";

AmodFontId g_blockFont = AMOD_FONT_NONE;
AmodTextureId g_blockAtlas = AMOD_INVALID_TEXTURE;
AmodToken g_fontDraw = 0;
char g_fontText[64] = "AZFORGE 0123";

// White letters, alpha carrying the shape - so the draw colour tints them and the paper is gone.
bool MakeLetters(const char* face, int size)
{
    const int count = static_cast<int>(strlen(kFontChars));
    const int columns = 16;
    const int rows = (count + columns - 1) / columns;
    const int cell = size + size / 2 + 4;
    const int wide = columns * cell;
    const int tall = rows * cell;

    BITMAPINFO shape = {};
    shape.bmiHeader.biSize = sizeof(shape.bmiHeader);
    shape.bmiHeader.biWidth = wide;
    shape.bmiHeader.biHeight = -tall;   // top down, the way a texture is read
    shape.bmiHeader.biPlanes = 1;
    shape.bmiHeader.biBitCount = 32;
    shape.bmiHeader.biCompression = BI_RGB;

    void* drawn = nullptr;
    const HDC paper = CreateCompatibleDC(nullptr);
    const HBITMAP bitmap = CreateDIBSection(paper, &shape, DIB_RGB_COLORS, &drawn, nullptr, 0);
    const HFONT font = CreateFontA(-size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);

    if (paper == nullptr || bitmap == nullptr || font == nullptr || drawn == nullptr)
    {
        Console::Print("font: Windows would not make the bitmap\n");
        return false;
    }

    SelectObject(paper, bitmap);
    SelectObject(paper, font);
    SetBkMode(paper, TRANSPARENT);
    SetTextColor(paper, RGB(255, 255, 255));

    unsigned char* const pixels = static_cast<unsigned char*>(malloc(static_cast<size_t>(wide) * tall * 4));

    if (pixels == nullptr)
    {
        DeleteObject(bitmap);
        DeleteObject(font);
        DeleteDC(paper);
        return false;
    }

    for (int i = 0; i < count; ++i)
    {
        TextOutA(paper, (i % columns) * cell, (i / columns) * cell, kFontChars + i, 1);
    }

    GdiFlush();

    // GDI wrote grey on black; the texture wants R, G, B and the fourth byte as the alpha.
    const unsigned char* const source = static_cast<const unsigned char*>(drawn);

    for (int at = 0; at < wide * tall; ++at)
    {
        const unsigned char ink = source[at * 4 + 2];

        pixels[at * 4 + 0] = 255;
        pixels[at * 4 + 1] = 255;
        pixels[at * 4 + 2] = 255;
        pixels[at * 4 + 3] = ink;
    }

    g_blockAtlas = Textures::Create(wide, tall, pixels, false, true);
    g_blockFont = SpriteFont::Create(face);

    if (g_blockAtlas != AMOD_INVALID_TEXTURE && g_blockFont != AMOD_FONT_NONE)
    {
        // Each letter keeps the width the face gives it, so the text is not laid out on a grid.
        for (int i = 0; i < count; ++i)
        {
            SIZE step = {};
            GetTextExtentPoint32A(paper, kFontChars + i, 1, &step);

            SpriteFont::SetGlyph(g_blockFont, static_cast<unsigned char>(kFontChars[i]), g_blockAtlas,
                static_cast<float>((i % columns) * cell), static_cast<float>((i / columns) * cell),
                static_cast<float>(step.cx), static_cast<float>(cell), 1.0f);
        }
    }

    free(pixels);
    DeleteObject(bitmap);
    DeleteObject(font);
    DeleteDC(paper);

    if (g_blockAtlas == AMOD_INVALID_TEXTURE || g_blockFont == AMOD_FONT_NONE)
    {
        Console::Print("font: the atlas or the font was refused\n");
        return false;
    }

    return true;
}

bool MakeBlockFont()
{
    return g_blockFont != AMOD_FONT_NONE || MakeLetters("Trebuchet MS", 18);
}

void __cdecl DrawFontSample(const AmodFrame*, void*)
{
    SpriteFont::Draw(g_blockFont, AMOD_MENU_WIDE * 0.5f, 80.0f, g_fontText, 1.0f, 0xFFFFFFFF,
        AMOD_FONT_CENTRE);
}

void __cdecl FontCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        const int count = SpriteFont::GameFontCount();

        for (int i = 0; i < count; ++i)
        {
            Console::Printf("  %s\n", SpriteFont::GameFontName(i));
        }

        Console::Printf("font: %d font(s) the game draws with\n", count);
        Console::Print("font, 'font show <text>', 'font hide', 'font measure <text>',\n");
        Console::Print("      'font swap <gamefont>', 'font restore <gamefont>',\n");
        Console::Print("      'font save <path.png>', 'font file <path> [cell]'\n");
        return;
    }

    if (strcmp(argv[1], "show") == 0 && argc > 2)
    {
        if (!MakeBlockFont())
        {
            return;
        }

        snprintf(g_fontText, sizeof(g_fontText), "%s", argv[2]);

        if (g_fontDraw == 0)
        {
            g_fontDraw = Render::OnFrame(&DrawFontSample);
        }

        Console::Printf("font: '%s' is drawn at the top, %g wide\n", g_fontText,
            SpriteFont::Measure(g_blockFont, g_fontText));
    }
    else if (strcmp(argv[1], "hide") == 0)
    {
        Render::Cancel(g_fontDraw);
        g_fontDraw = 0;
        Console::Print("font: not drawn any more\n");
    }
    else if (strcmp(argv[1], "measure") == 0 && argc > 2)
    {
        const AmodFontId game = SpriteFont::FromGame(argc > 3 ? argv[3] : "");

        Console::Printf("font: '%s' is %g wide, the line is %g tall\n", argv[2],
            SpriteFont::Measure(game, argv[2]), SpriteFont::LineHeight(game));
    }
    else if (strcmp(argv[1], "swap") == 0 && argc > 2)
    {
        if (!MakeBlockFont())
        {
            return;
        }

        Console::Printf("font: '%s' %s\n", argv[2],
            SpriteFont::Replace(argv[2], g_blockFont) ? "is drawn in blocks now" : "was refused");
    }
    else if (strcmp(argv[1], "file") == 0 && argc > 2)
    {
        // Nothing about a font cares where its atlas came from: this one is read off the disk.
        const float cell = argc > 3 ? static_cast<float>(atof(argv[3])) : 31.0f;
        const AmodTextureId atlas = Textures::Load(argv[2], false, true);

        if (atlas == AMOD_INVALID_TEXTURE)
        {
            Console::Printf("font: '%s' would not read\n", argv[2]);
            return;
        }

        const AmodFontId read = SpriteFont::Create(argv[2]);
        const int placed = SpriteFont::SetGrid(read, atlas, kFontChars, cell, cell, 1.0f);

        if (placed <= 0)
        {
            Console::Printf("font: '%s' gave no glyphs at %g\n", argv[2], cell);
            return;
        }

        g_blockFont = read;
        Console::Printf("font: '%s' read, %d glyph(s) of %g\n", argv[2], placed, cell);
    }
    else if (strcmp(argv[1], "save") == 0 && argc > 2)
    {
        if (!MakeBlockFont())
        {
            return;
        }

        Console::Printf("font: the atlas %s '%s'\n",
            Textures::Save(g_blockAtlas, argv[2]) ? "was written to" : "would not be written to",
            argv[2]);
    }
    else if (strcmp(argv[1], "restore") == 0 && argc > 2)
    {
        Console::Printf("font: '%s' %s\n", argv[2],
            SpriteFont::Restore(argv[2]) ? "is the game's own again" : "was refused");
    }
    else
    {
        Console::Print("font, 'font show <text>', 'font hide', 'font measure <text> [gamefont]',\n");
        Console::Print("      'font swap <gamefont>', 'font restore <gamefont>',\n");
        Console::Print("      'font save <path.png>', 'font file <path> [cell]'\n");
    }
}

void __cdecl CvarCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Console::Print("cfg <name> [value] [save] [seal], 'cfg keep <name> [0|1] [seal]'\n");
        return;
    }

    if (strcmp(argv[1], "keep") == 0 && argc > 2)
    {
        const bool keep = argc > 3 ? atoi(argv[3]) != 0 : true;
        const bool seal = argc > 4 && atoi(argv[4]) != 0;

        const char* const how = !Config::Keep(argv[2], keep, seal) ? "refused"
                              : !keep ? "is not kept any more"
                              : seal ? "is kept for next time, sealed"
                              : "is kept for next time";

        Console::Printf("cfg: '%s' %s\n", argv[2], how);
        return;
    }

    if (argc < 3)
    {
        Console::Printf("cfg: '%s' is %.4f\n", argv[1], Config::Number(argv[1]));
        return;
    }

    const bool save = argc > 3 && atoi(argv[3]) != 0;
    const bool seal = argc > 4 && atoi(argv[4]) != 0;

    if (!Config::Set(argv[1], argv[2], save, seal))
    {
        Console::Printf("cfg: '%s' was refused\n", argv[1]);
        return;
    }

    Console::Printf("cfg: '%s' is now '%s'%s\n", argv[1], argv[2],
        !save ? "" : seal ? ", and kept sealed for next time" : ", and kept for next time");
}

void ListScores()
{
    const int count = Scores::Count();

    if (count == 0)
    {
        Console::Print("score: the table is empty\n");
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        AmodScoreEntry entry = {};

        if (Scores::Get(i, entry))
        {
            Console::Printf("  %3d  %-16s %-28s %8.2f  %d\n", i,
                entry.name != nullptr ? entry.name : "", entry.level != nullptr ? entry.level : "",
                entry.time, entry.score);
        }
    }

    Console::Printf("score: %d entr(ies) across %d castle(s)\n", count, Scores::CastleCount());
}

void __cdecl ScoreCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        ListScores();
        return;
    }

    if (strcmp(argv[1], "add") == 0 && argc > 5)
    {
        const bool save = argc > 6 && atoi(argv[6]) != 0;
        const bool seal = argc > 7 && atoi(argv[7]) != 0;

        const int at = Scores::Add(argv[2], argv[3], static_cast<float>(atof(argv[4])),
            atoi(argv[5]), save, seal);

        if (at < 0)
        {
            Console::Printf("score: '%s' would not take it\n", argv[2]);
            return;
        }

        Console::Printf("score: entry %d added%s\n", at,
            !save ? "" : seal ? ", and kept sealed for next time" : ", and kept for next time");
    }
    else if (strcmp(argv[1], "keep") == 0 && argc > 2)
    {
        const int at = atoi(argv[2]);
        const bool keep = argc > 3 ? atoi(argv[3]) != 0 : true;
        const bool seal = argc > 4 && atoi(argv[4]) != 0;

        const char* const how = !Scores::Keep(at, keep, seal) ? "was refused"
                              : !keep ? "is not kept any more"
                              : seal ? "is kept for next time, sealed"
                              : "is kept for next time";

        Console::Printf("score: entry %d %s\n", at, how);
    }
    else if (strcmp(argv[1], "remove") == 0 && argc > 2)
    {
        const int at = atoi(argv[2]);

        Console::Printf("score: entry %d %s\n", at,
            Scores::Remove(at) ? "removed" : "was refused");
    }
    else
    {
        Console::Print("score, 'score add <level> <name> <time> <points> [save] [seal]',\n");
        Console::Print("       'score keep <index> [0|1] [seal]', 'score remove <index>'\n");
    }
}

void __cdecl PackCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        const int count = Sav::Count();

        for (int i = 0; i < count; ++i)
        {
            Console::Printf("  %s\n", Sav::NameAt(i));
        }

        Console::Printf("pack: %d level(s) open\n", count);
        return;
    }

    if (strcmp(argv[1], "open") == 0 && argc > 2)
    {
        const bool save = argc > 3 && atoi(argv[3]) != 0;
        const bool seal = argc > 4 && atoi(argv[4]) != 0;

        Console::Printf("pack: '%s' %s%s\n", argv[2],
            Sav::Open(argv[2], save, seal) >= 0 ? "open" : "was refused",
            !save ? "" : seal ? ", and kept sealed for next time" : ", and kept for next time");
    }
    else if (strcmp(argv[1], "close") == 0 && argc > 2)
    {
        const bool save = argc > 3 && atoi(argv[3]) != 0;
        const bool seal = argc > 4 && atoi(argv[4]) != 0;

        Console::Printf("pack: '%s' %s%s\n", argv[2],
            Sav::Close(argv[2], save, seal) ? "closed" : "was refused",
            !save ? "" : seal ? ", and kept sealed for next time" : ", and kept for next time");
    }
    else if (strcmp(argv[1], "keep") == 0 && argc > 2)
    {
        const bool keep = argc > 3 ? atoi(argv[3]) != 0 : true;
        const bool seal = argc > 4 && atoi(argv[4]) != 0;

        const char* const how = !Sav::Keep(argv[2], keep, seal) ? "was refused"
                              : !keep ? "is not kept any more"
                              : seal ? "is kept for next time, sealed"
                              : "is kept for next time";

        Console::Printf("pack: '%s' %s\n", argv[2], how);
    }
    else
    {
        Console::Print("pack, 'pack open <level> [save] [seal]', 'pack close <level> [save] [seal]',\n");
        Console::Print("      'pack keep <level> [0|1] [seal]'\n");
    }
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("keys", &KeysCommand);
    Console::AddCommand("freeze", &FreezeCommand);
    Console::AddCommand("mtl", &MaterialCommand);
    Console::AddCommand("cfg", &CvarCommand);
    Console::AddCommand("score", &ScoreCommand);
    Console::AddCommand("pack", &PackCommand);
    Console::AddCommand("font", &FontCommand);
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
    if (g_holdFrames > 0 && --g_holdFrames == 0 && g_holdKey >= 0)
    {
        Input::Hold(g_holdKey, false);
        g_holdKey = -1;
    }
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
