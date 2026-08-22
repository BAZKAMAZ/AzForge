#include <ForgeApi.h>

#include <windows.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace Amod;

float g_seconds = 0.0f;
int g_frames = 0;
bool g_withWorld = false;
AmodToken g_overlay = 0;

AmodModelId g_model = AMOD_INVALID_MODEL;
AmodTextureId g_sheet = AMOD_INVALID_TEXTURE;   // the same checks, before they were handed over
int g_quads = 0;                                // how many the batch draws, 0 for none
AmodToken g_quadsToken = 0;
AmodAzmId g_car = AMOD_INVALID_AZM;             // a model with parts, out of a .azm
int g_wheels[8] = {};
int g_wheelCount = 0;
int g_models = 0;                               // how many the 3D batch draws
int g_drawn = 0;
float g_spent = 0.0f;                           // seconds counted since the count was last set
int g_counted = 0;
float g_slowest = 0.0f;
float g_reach = 0.5f;   // how far back the camera sits, from the model's own extents
uint32_t g_texture = 0;
bool g_holdsInput = false;
bool g_holdsShell = false;
bool g_holdsLoad = false;

void __cdecl StageUpdate(float seconds, void*)
{
    g_seconds += seconds;
    ++g_frames;

    g_spent += seconds;
    ++g_counted;
    g_slowest = seconds > g_slowest ? seconds : g_slowest;

    if (g_withWorld)
    {
        Stage::RunGameUpdate(seconds);
        return;
    }

    Stage::RunMusic();
}

void MakeAWorld()
{
    if (g_model != AMOD_INVALID_MODEL)
    {
        return;
    }

    static unsigned int checks[64 * 64];

    for (int y = 0; y < 64; ++y)
    {
        for (int x = 0; x < 64; ++x)
        {
            const bool light = ((x >> 3) + (y >> 3)) % 2 == 0;
            checks[y * 64 + x] = light ? Textures::Pixel(0x44, 0x88, 0xCC) : Textures::Pixel(0x22, 0x33, 0x44);
        }
    }

    g_sheet = Textures::Create(64, 64, checks, false, false);
    g_texture = Textures::ToGame(g_sheet);

    const float half = 0.5f;
    AmodVertex corners[24] = {};
    uint16_t faces[36] = {};

    const float normals[6][3] =
    {
        {  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f },
        {  1.0f,  0.0f,  0.0f }, { -1.0f,  0.0f,  0.0f },
        {  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f },
    };

    for (int face = 0; face < 6; ++face)
    {
        const float* const normal = normals[face];

        float across[3] = { normal[1], normal[2], normal[0] };
        float down[3] =
        {
            normal[1] * across[2] - normal[2] * across[1],
            normal[2] * across[0] - normal[0] * across[2],
            normal[0] * across[1] - normal[1] * across[0],
        };

        for (int corner = 0; corner < 4; ++corner)
        {
            const float u = (corner == 1 || corner == 2) ? 1.0f : 0.0f;
            const float v = (corner >= 2) ? 1.0f : 0.0f;
            AmodVertex& vertex = corners[face * 4 + corner];

            for (int axis = 0; axis < 3; ++axis)
            {
                vertex.position[axis] = (normal[axis] + across[axis] * (u * 2.0f - 1.0f) +
                                         down[axis] * (v * 2.0f - 1.0f)) * half;
                vertex.normal[axis] = normal[axis];
            }

            vertex.uv[0] = u;
            vertex.uv[1] = v;
        }

        const uint16_t first = static_cast<uint16_t>(face * 4);
        const uint16_t order[6] = { 0, 1, 2, 0, 2, 3 };

        for (int at = 0; at < 6; ++at)
        {
            faces[face * 6 + at] = static_cast<uint16_t>(first + order[at]);
        }
    }

    AmodMeshData mesh = {};
    mesh.structSize = sizeof(mesh);
    mesh.frameCount = 1;
    mesh.vertexCount = 24;
    mesh.indexCount = 36;
    mesh.primitiveCount = 12;
    mesh.vertices = corners;
    mesh.indices = faces;

    g_model = Mmd::Create("sample/cube", mesh);
    g_reach = 2.0f;

    Console::Printf("stage: a world of its own - model %u, texture %u\n", g_model, g_texture);
}

void DrawField()
{
    static AmodModelDraw* field = nullptr;
    static int held = 0;

    if (held < g_models)
    {
        free(field);
        field = static_cast<AmodModelDraw*>(malloc(sizeof(AmodModelDraw) * g_models));
        held = field != nullptr ? g_models : 0;
    }

    if (field == nullptr)
    {
        return;
    }

    int side = 1;

    while (side * side < g_models)
    {
        ++side;
    }

    for (int at = 0; at < g_models; ++at)
    {
        const float across = static_cast<float>(at % side) - (side - 1) * 0.5f;
        const float along = static_cast<float>(at / side) - (side - 1) * 0.5f;

        field[at] = Mmd::Placement(g_model, across * 1.6f, sinf(g_seconds + at * 0.3f) * 0.3f,
            along * 1.6f);
        field[at].texture = g_texture;
        field[at].rotation[1] = g_seconds * 0.7f + at;

        field[at].blend = (at % 8) == 7 ? AMOD_DRAW_BLEND_ADD : AMOD_DRAW_BLEND_NONE;
    }

    g_drawn = Mmd::DrawMany(field, g_models);
}

void __cdecl StageRender(void*)
{
    if (g_withWorld)
    {
        Stage::RunGameRender();
        return;
    }

    Stage::Clear(0xFF101828);

    MakeAWorld();

    if (g_model == AMOD_INVALID_MODEL)
    {
        return;
    }

    const float eye[3] = { 0.0f, g_reach * 0.4f, g_reach };
    const float at[3] = { 0.0f, g_reach * 0.15f, 0.0f };
    const float up[3] = { 0.0f, 1.0f, 0.0f };

    Stage::SetCamera(eye, at, up, 60.0f, g_reach * 0.05f, g_reach * 20.0f);

    if (g_car != AMOD_INVALID_AZM)
    {
        for (int at = 0; at < g_wheelCount; ++at)
        {
            Azm::Turn(g_car, g_wheels[at], 1.0f, 0.0f, 0.0f, g_seconds * 4.0f);
        }

        Mmd::Matrix stand = Mmd::Matrix::Identity();
        const float turn = cosf(g_seconds * 0.4f);
        const float lean = sinf(g_seconds * 0.4f);

        stand.m[0] = turn;
        stand.m[2] = -lean;
        stand.m[8] = lean;
        stand.m[10] = turn;
        stand.m[13] = -0.7f;

        Azm::Draw(g_car, stand.m);
        return;
    }

    if (g_models > 0)
    {
        DrawField();
        return;
    }

    // Turned about y on the mod's own seconds. Row vectors, like the game's own matrices.
    const float turn = cosf(g_seconds);
    const float lean = sinf(g_seconds);

    Mmd::Matrix spin = Mmd::Matrix::Identity();
    spin.m[0] = turn;
    spin.m[2] = -lean;
    spin.m[8] = lean;
    spin.m[10] = turn;

    Mmd::DrawWith(g_model, g_texture, spin);
}

int __cdecl StageShell(void*);
void __cdecl StageOverlay(const AmodFrame*, void*);

// The boot step is held to show that it can be, and handed straight on: the game loads as itself.
// Nothing is taken over until it is asked for, and 'stage take' is the asking.
int __cdecl StageLoad(void*)
{
    return Stage::RunGameLoad();
}

int __cdecl StageShell(void*)
{
    return 0;
}

void __cdecl StageOverlay(const AmodFrame*, void*)
{
    const float across = g_seconds * 120.0f;
    const float x = across - AMOD_MENU_WIDE * (int)(across / AMOD_MENU_WIDE);

    Render::Rect(0.0f, 40.0f, AMOD_MENU_WIDE, 24.0f, 0x80000000);
    Render::Rect(x, 40.0f, 40.0f, 24.0f, 0xFFFFC800);
}

void __cdecl WindowCommand(int argc, const char** argv, void*)
{
    HWND const window = static_cast<HWND>(Render::Window());
    const char* const what = argc > 1 ? argv[1] : "";

    if (window == nullptr)
    {
        Console::Print("window: the game has no window yet\n");
        return;
    }

    if (strcmp(what, "full") == 0)
    {
        const int wide = GetSystemMetrics(SM_CXSCREEN);
        const int tall = GetSystemMetrics(SM_CYSCREEN);

        SetWindowLongPtrA(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(window, HWND_TOP, 0, 0, wide, tall, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        Console::Printf("window: borderless over %dx%d\n", wide, tall);
    }
    else if (strcmp(what, "windowed") == 0)
    {
        const int wide = argc > 2 ? atoi(argv[2]) : 800;
        const int tall = argc > 3 ? atoi(argv[3]) : 600;

        SetWindowLongPtrA(window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(window, HWND_TOP, 60, 60, wide, tall, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        Console::Printf("window: %dx%d with a frame, and it can be dragged by its edges\n",
            wide, tall);
    }
    else if (strcmp(what, "cursor") == 0)
    {
        const int show = argc > 2 ? atoi(argv[2]) : 1;

        Console::Printf("window: the system cursor is %s\n", !Input::ShowSystemCursor(show)
            ? "refused" : show < 0 ? "the game's again" : show != 0 ? "shown" : "hidden");
    }
    else if (strcmp(what, "sound") == 0 && argc > 2)
    {
        const AmodSoundId sound = Sound::Load(argv[2]);

        Console::Printf("window: '%s' %s\n", argv[2],
            sound == AMOD_INVALID_SOUND ? "would not load"
                : Sound::Play(sound) ? "is playing" : "loaded but would not play");
    }
    else
    {
        RECT box = {};
        GetWindowRect(window, &box);

        Console::Printf("window: %ld x %ld at %ld,%ld\n", box.right - box.left,
            box.bottom - box.top, box.left, box.top);
        Console::Print("window, 'window full', 'window windowed [w] [h]',\n");
        Console::Print("        'window cursor [1|0|-1]', 'window sound <path.wav>'\n");
    }
}

void __cdecl StageCommand(int argc, const char** argv, void*)
{
    const char* const what = argc > 1 ? argv[1] : "";

    if (strcmp(what, "give") == 0)
    {
        Stage::GiveBack(AMOD_STAGE_ALL);
        Stage::TakeInput(false);
        g_holdsShell = false;
        Render::CancelFrame(g_overlay);
        g_overlay = 0;

        Console::Printf("stage: the game has its frame back after %.1fs and %d frame(s)\n",
            g_seconds, g_frames);
        return;
    }

    if (strcmp(what, "take") == 0 || strcmp(what, "over") == 0 || strcmp(what, "all") == 0)
    {
        g_withWorld = strcmp(what, "over") == 0;
        g_seconds = 0.0f;
        g_frames = 0;

        if (!Stage::TakeUpdate(&StageUpdate) || !Stage::TakeRender(&StageRender))
        {
            Console::Print("stage: refused - something else holds the frame\n");
            Stage::GiveBack();
            return;
        }

        if (g_overlay == 0)
        {
            g_overlay = Render::OnFrame(&StageOverlay);
        }

        g_holdsInput = Stage::TakeInput(!g_withWorld);

        g_holdsShell = strcmp(what, "all") == 0 && Stage::TakeShell(&StageShell);

        Console::Printf("stage: the mod runs the world%s%s\n",
            g_withWorld ? ", and the game runs under it" : " and draws its own",
            g_holdsShell ? ", and the shell with it" : "");
        return;
    }

    AmodModel info = {};
    const bool known = g_model != AMOD_INVALID_MODEL && Mmd::Get(g_model, info);

    Console::Printf("stage: the frame is %s, %.1fs and %d frame(s) counted\n",
        Stage::IsTaken() ? "held by a mod" : "the game's", g_seconds, g_frames);
    Console::Printf("       input %s, shell %s\n",
        g_holdsInput ? "held by the mod" : "the game's",
        g_holdsShell ? "held by the mod" : "the game's");
    Console::Printf("       boot step %s\n",
        g_holdsLoad ? "held by the mod" : "the game's");
    Console::Printf("       drawing '%s' of %d model(s), texture %u of %d\n",
        known && info.name != nullptr ? info.name : "nothing yet", Mmd::Count(),
        g_texture, Textures::Count());
    Console::Print("stage, 'stage take' the mod's own world, 'stage over' the game under it,\n");
    Console::Print("       'stage all' the shell too - no menus and no game states,\n");
    Console::Print("       'stage give' hands the frame back\n");
    Console::Print("stage: the game boots as itself - nothing is taken over until it is\n");
    Console::Print("       asked for, and nothing stays taken after 'stage give'\n");
}

void __cdecl QuadField(const AmodFrame*, void*)
{
    static AmodQuad* field = nullptr;
    static int held = 0;

    if (g_quads <= 0)
    {
        return;
    }

    if (held < g_quads)
    {
        free(field);
        field = static_cast<AmodQuad*>(malloc(sizeof(AmodQuad) * g_quads));
        held = field != nullptr ? g_quads : 0;
    }

    if (field == nullptr)
    {
        return;
    }

    for (int at = 0; at < g_quads; ++at)
    {
        const float drift = g_seconds * (20.0f + (at % 5) * 9.0f) + at * 37.0f;

        field[at].x = fmodf(drift, AMOD_MENU_WIDE);
        field[at].y = fmodf(at * 53.0f + g_seconds * 8.0f, AMOD_MENU_TALL);
        field[at].w = 18.0f;
        field[at].h = 18.0f;
        field[at].angle = g_seconds * (0.6f + (at % 7) * 0.2f) + at;
        field[at].u0 = 0.0f;
        field[at].v0 = 0.0f;
        field[at].u1 = 1.0f;
        field[at].v1 = 1.0f;
        field[at].colour = 0xC0FFFFFF - static_cast<uint32_t>((at % 5) * 0x00201008);
    }

    Render::Quads(g_sheet, field, g_quads);
}

void __cdecl QuadsCommand(int argc, const char** argv, void*)
{
    if (argc > 1)
    {
        g_quads = strcmp(argv[1], "off") == 0 ? 0 : atoi(argv[1]);
        g_quads = g_quads < 0 ? 0 : g_quads;
    }

    if (g_quads > 0 && g_quadsToken == 0)
    {
        g_quadsToken = Render::OnFrame(&QuadField);
    }

    Console::Printf("quads: %d a frame, in one draw, off texture %u\n", g_quads, g_sheet);
    Console::Print("quads: 'quads <count>' to ask for that many, 'quads off' to stop\n");

    if (g_sheet == AMOD_INVALID_TEXTURE)
    {
        Console::Print("quads: no texture yet - they are drawn in plain colour until 'stage take'\n");
    }
}

void __cdecl ModelsCommand(int argc, const char** argv, void*)
{
    if (argc > 1)
    {
        int side = 1;

        g_models = strcmp(argv[1], "off") == 0 ? 0 : atoi(argv[1]);
        g_models = g_models < 0 ? 0 : g_models;

        while (side * side < g_models)
        {
            ++side;
        }

        // Far enough back that the whole field is in front of the camera.
        g_reach = side * 2.0f + 2.0f;
        g_spent = 0.0f;
        g_counted = 0;
        g_slowest = 0.0f;
        g_drawn = 0;
    }

    const float average = g_counted > 0 ? g_spent / g_counted : 0.0f;

    Console::Printf("models: %d asked for, %d drawn in one list\n", g_models, g_drawn);
    Console::Printf("        %.2f ms a frame over %d frame(s), worst %.2f ms\n",
        average * 1000.0f, g_counted, g_slowest * 1000.0f);
    Console::Print("models: 'models <count>' draws that many cubes, 'models off' stops.\n");
    Console::Print("        The frame has to be the mod's: 'stage take' first, or nothing counts\n");
}

void __cdecl AzmCommand(int argc, const char** argv, void*)
{
    const char* const what = argc > 1 ? argv[1] : "";

    if (strcmp(what, "off") == 0)
    {
        Azm::Release(g_car);
        g_car = AMOD_INVALID_AZM;
        g_wheelCount = 0;
        Console::Print("azm: put down\n");
        return;
    }

    if (strcmp(what, "tint") == 0)
    {
        const uint32_t colour = argc > 2 ? (0xFF000000u | strtoul(argv[2], nullptr, 16))
                                         : 0xFFFFFFFFu;

        if (!Azm::SetTint(g_car, colour))
        {
            Console::Print("azm: nothing loaded to tint\n");
            return;
        }

        Console::Printf("azm: tinted %06X - the model's own colours show through it\n",
            colour & 0xFFFFFF);
        Console::Print("azm: 'azm tint' with no colour puts it back\n");
        return;
    }

    if (strcmp(what, "paint") == 0 && argc > 2)
    {
        AmodAzmModel model = {};

        if (!Azm::Get(g_car, model))
        {
            Console::Print("azm: nothing loaded to paint\n");
            return;
        }

        const uint32_t colour = 0xFF000000 | strtoul(argv[2], nullptr, 16);

        for (int at = 0; at < model.materialCount; ++at)
        {
            AmodAzmMaterial material = {};

            if (Azm::GetMaterial(g_car, at, material) && (material.flags & AZM_MATERIAL_PAINTABLE))
            {
                Console::Printf("azm: '%s' repainted %06X\n", material.name, colour & 0xFFFFFF);
                Azm::Paint(g_car, at, colour);
            }
        }
        return;
    }

    if (what[0] != '\0' && strcmp(what, "info") != 0)
    {
        Azm::Release(g_car);
        g_wheelCount = 0;
        g_car = Azm::Load(what);

        if (g_car == AMOD_INVALID_AZM)
        {
            Console::Printf("azm: '%s' would not load - forge.log says why\n", what);
            return;
        }

        AmodAzmModel model = {};
        Azm::Get(g_car, model);

        // Whatever the file calls a wheel turns; nothing else does.
        for (int at = 0; at < model.partCount && g_wheelCount < 8; ++at)
        {
            AmodAzmPart part = {};

            if (Azm::GetPart(g_car, at, part) && strncmp(part.name, "wheel", 5) == 0)
            {
                g_wheels[g_wheelCount++] = at;
            }
        }

        // The camera has to stand back far enough to see the whole of it.
        const float across = model.boundsMax[0] - model.boundsMin[0];
        const float tall = model.boundsMax[1] - model.boundsMin[1];
        const float along = model.boundsMax[2] - model.boundsMin[2];
        const float widest = across > tall ? (across > along ? across : along) : (tall > along ? tall : along);

        g_reach = widest * 1.8f + 1.0f;
        g_models = 0;
    }

    AmodAzmModel model = {};

    if (!Azm::Get(g_car, model))
    {
        Console::Print("azm: nothing loaded\n");
        Console::Print("azm: 'azm <path.azm>' loads and draws one, 'azm off' puts it down,\n");
        Console::Print("     'azm paint <rrggbb>' recolours every paintable material.\n");
        Console::Print("     The frame has to be the mod's: 'stage take' first\n");
        return;
    }

    Console::Printf("azm: '%s' from %s\n", model.name, model.path);
    Console::Printf("     %d part(s) in %d mesh(es), %d material(s), %d triangle(s)\n",
        model.partCount, model.meshCount, model.materialCount, model.triangleCount);
    Console::Printf("     %d point(s), %d box(es), %g unit(s) a metre\n", model.pointCount,
        model.boxCount, model.unitsPerMetre);
    Console::Printf("     bounds %.2f %.2f %.2f .. %.2f %.2f %.2f\n", model.boundsMin[0],
        model.boundsMin[1], model.boundsMin[2], model.boundsMax[0], model.boundsMax[1],
        model.boundsMax[2]);

    for (int at = 0; at < model.partCount; ++at)
    {
        AmodAzmPart part = {};

        if (Azm::GetPart(g_car, at, part))
        {
            Console::Printf("     part %d '%s' parent %d pivot %.2f %.2f %.2f\n", at, part.name,
                part.parent, part.pivot[0], part.pivot[1], part.pivot[2]);
        }
    }

    Console::Printf("     %d wheel(s) turning\n", g_wheelCount);
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("stage", &StageCommand);
    Console::AddCommand("window", &WindowCommand);
    Console::AddCommand("quads", &QuadsCommand);
    Console::AddCommand("models", &ModelsCommand);
    Console::AddCommand("azm", &AzmCommand);

    Launcher::Skip(true);
    g_holdsLoad = Stage::TakeLoad(&StageLoad);

    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
