#include <ForgeApi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//   mmd   mmdinfo <n>   mmdmake   mmdover <path>   mmdroom [n]   mmdrestore <n>
//   mmdscale <n> <k>   mmdmove <n> <dx> <dy> <dz>   mmdpart <n> <minY> <maxY> <dy>
//   mmdvert <n> <frame> <index>   mmdbounds <n> [frame]   mmdshow [texture]
//   mmdknife [vertex] [scale] [degrees] - bare toggles, a dash takes it off
//   mmdanim [n]   mmdstate <entity>   mmdplay <entity> <frameset|->
//   mmdtex [rows]   mmdpaint [size] [path to stand in for]   mmdsave <n> <path>
//   mmdcolour <n> <aarrggbb|rainbow|off>   mmdcolourbox <n> <minY> <maxY> <aarrggbb>


static const char* const kPyramid = "forge/pyramid.mmd";

static AmodModelId g_pyramid = AMOD_INVALID_MODEL;

static uint32_t g_showTexture = 0;
static bool g_show = false;
static float g_spin = 0.0f;

static const char* KindName(int kind)
{
    switch (kind)
    {
    case AMOD_MODEL_KIND_MESH:     return "mesh";
    case AMOD_MODEL_KIND_STATIC:   return "static";
    case AMOD_MODEL_KIND_ANIMATED: return "anim";
    }
    return "?";
}

static AmodModelId Chosen(int argc, const char** argv, int at)
{
    return argc > at ? Amod::Mmd::At(atoi(argv[at])) : AMOD_INVALID_MODEL;
}

static void __cdecl ListCommand(int, const char**, void*)
{
    const int count = Amod::Mmd::Count();
    Amod::Console::Printf("%d model(s) loaded\n", count);

    for (int index = 0; index < count; ++index)
    {
        AmodModel model;
        if (!Amod::Mmd::Get(Amod::Mmd::At(index), model))
        {
            continue;
        }

        Amod::Console::Printf("%3d  %-6s frames %-3d verts %-6d prims %-6d refs %-3d %s%s\n", index,
                              KindName(model.kind), model.frameCount, model.vertexCount,
                              model.primitiveCount, model.references,
                              model.edited ? "* " : "", model.name);
    }
}

static void __cdecl InfoCommand(int argc, const char** argv, void*)
{
    AmodModel model;
    if (!Amod::Mmd::Get(Chosen(argc, argv, 1), model))
    {
        Amod::Console::Printf("mmdinfo <n>\n");
        return;
    }

    Amod::Console::Printf("%s\n  kind %s, %d frame(s), %d verts, %d indices, %d prims, %d refs\n",
                          model.name, KindName(model.kind), model.frameCount, model.vertexCount,
                          model.indexCount, model.primitiveCount, model.references);
    Amod::Console::Printf("  centre %.2f %.2f %.2f   extents %.2f %.2f %.2f%s\n", model.centre[0],
                          model.centre[1], model.centre[2], model.extents[0], model.extents[1],
                          model.extents[2], model.edited ? "   edited" : "");
}

// Four sides and a floor, built out of nothing the game had.
static AmodModelId BuildPyramid()
{
    const float half = 40.0f;
    const float top = 90.0f;
    const float corners[4][2] = { { -half, -half }, { half, -half },
                                 { half, half },    { -half, half } };

    AmodVertex vertices[5] = {};
    for (int at = 0; at < 4; ++at)
    {
        vertices[at].position[0] = corners[at][0];
        vertices[at].position[2] = corners[at][1];
        vertices[at].normal[1] = -1.0f;
        vertices[at].uv[0] = (at == 1 || at == 2) ? 1.0f : 0.0f;
        vertices[at].uv[1] = at >= 2 ? 1.0f : 0.0f;
    }

    vertices[4].position[1] = top;
    vertices[4].normal[1] = 1.0f;
    vertices[4].uv[0] = 0.5f;
    vertices[4].uv[1] = 0.5f;

    // Every face twice, the second one wound the other way, so no cull state can hide it.
    static const uint16_t indices[36] =
    {
        0, 1, 4,  1, 2, 4,  2, 3, 4,  3, 0, 4,  0, 2, 1,  0, 3, 2,
        4, 1, 0,  4, 2, 1,  4, 3, 2,  4, 0, 3,  1, 2, 0,  2, 3, 0,
    };

    const AmodMeshData mesh = Amod::Mmd::Mesh(vertices, 5, indices, 36);
    return Amod::Mmd::Create(kPyramid, mesh);
}

static void __cdecl MakeCommand(int, const char**, void*)
{
    g_pyramid = BuildPyramid();

    AmodModel model;
    if (!Amod::Mmd::Get(g_pyramid, model))
    {
        Amod::Console::Printf("the pyramid would not build\n");
        return;
    }
    Amod::Console::Printf("'%s' built: %d verts, %d prims\n", model.name, model.vertexCount,
                          model.primitiveCount);
}

// Whatever asks for that file from now on is given the pyramid instead.
static void __cdecl OverrideCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Amod::Console::Printf("mmdover <path>, e.g. models/ctrls/door.mmd\n");
        return;
    }

    g_pyramid = Amod::Mmd::IsValid(g_pyramid) ? g_pyramid : BuildPyramid();
    Amod::Console::Printf(Amod::Mmd::Override(argv[1], g_pyramid) ? "'%s' is the pyramid now\n"
                                                                 : "'%s' refused\n",
                          argv[1]);
}

static void __cdecl RoomCommand(int argc, const char** argv, void*)
{
    const int room = argc > 1 ? atoi(argv[1]) : -1;

    g_pyramid = Amod::Mmd::IsValid(g_pyramid) ? g_pyramid : BuildPyramid();
    Amod::Console::Printf(Amod::Mmd::SetRoomModel(room, AMOD_ROOM_MODEL_GEOMETRY, g_pyramid)
                              ? "room %d is a pyramid\n"
                              : "room %d refused\n",
                          room);
}

// True min and max on each axis, which is the thing you need before aiming a filter.
static bool ModelBounds(AmodModelId id, int frame, float* low, float* high)
{
    AmodModel model;
    if (!Amod::Mmd::Get(id, model) || model.vertexCount <= 0)
    {
        return false;
    }

    for (int axis = 0; axis < 3; ++axis)
    {
        low[axis] = 1.0e30f;
        high[axis] = -1.0e30f;
    }

    for (int at = 0; at < model.vertexCount; ++at)
    {
        AmodVertex vertex;
        if (!Amod::Mmd::GetVertex(id, frame, at, vertex))
        {
            return false;
        }

        for (int axis = 0; axis < 3; ++axis)
        {
            low[axis] = vertex.position[axis] < low[axis] ? vertex.position[axis] : low[axis];
            high[axis] = vertex.position[axis] > high[axis] ? vertex.position[axis] : high[axis];
        }
    }
    return true;
}

static void __cdecl BoundsCommand(int argc, const char** argv, void*)
{
    AmodModel model;
    float low[3];
    float high[3];

    if (!Amod::Mmd::Get(Chosen(argc, argv, 1), model) ||
        !ModelBounds(Amod::Mmd::At(atoi(argv[1])), argc > 2 ? atoi(argv[2]) : 0, low, high))
    {
        Amod::Console::Printf("mmdbounds <n> [frame]\n");
        return;
    }

    Amod::Console::Printf("%s, %d vert(s):\n", model.name, model.vertexCount);
    for (int axis = 0; axis < 3; ++axis)
    {
        Amod::Console::Printf("  %c  %+9.4f .. %+9.4f   span %.4f\n", "XYZ"[axis], low[axis],
                              high[axis], high[axis] - low[axis]);
    }
}

static void __cdecl ScaleCommand(int argc, const char** argv, void*)
{
    AmodModel model;
    if (argc < 3 || !Amod::Mmd::Get(Chosen(argc, argv, 1), model))
    {
        Amod::Console::Printf("mmdscale <n> <factor>, 1 puts it back\n");
        return;
    }

    const AmodModelId id = Amod::Mmd::At(atoi(argv[1]));
    const float factor = static_cast<float>(atof(argv[2]));

    Amod::Mmd::Restore(id);
    const bool done = factor == 1.0f || Amod::Mmd::Transform(id, Amod::Mmd::Matrix::Resize(factor));

    Amod::Console::Printf(done ? "%s is %.2f times its own size\n" : "%s refused\n", model.name,
                          factor);
}

static void __cdecl MoveCommand(int argc, const char** argv, void*)
{
    AmodModel model;
    if (argc < 5 || !Amod::Mmd::Get(Chosen(argc, argv, 1), model))
    {
        Amod::Console::Printf("mmdmove <n> <dx> <dy> <dz>\n");
        return;
    }

    const Amod::Mmd::Matrix by = Amod::Mmd::Matrix::Move(static_cast<float>(atof(argv[2])),
                                                         static_cast<float>(atof(argv[3])),
                                                         static_cast<float>(atof(argv[4])));

    Amod::Console::Printf(Amod::Mmd::Transform(Amod::Mmd::At(atoi(argv[1])), by) ? "%s moved\n"
                                                                                : "%s refused\n",
                          model.name);
}

// A slab of the model picked out by height, and only that slab moves.
static void __cdecl PartCommand(int argc, const char** argv, void*)
{
    AmodModel model;
    if (argc < 5 || !Amod::Mmd::Get(Chosen(argc, argv, 1), model))
    {
        Amod::Console::Printf("mmdpart <n> <minY> <maxY> <dy>\n");
        return;
    }

    const AmodModelId id = Amod::Mmd::At(atoi(argv[1]));
    const float low = static_cast<float>(atof(argv[2]));
    const float high = static_cast<float>(atof(argv[3]));
    const float lift = static_cast<float>(atof(argv[4]));

    const float wide = 1.0e9f;
    const AmodVertexFilter slab = Amod::Mmd::InBox(-wide, low, -wide, wide, high, wide);
    const int picked = Amod::Mmd::SelectVertices(id, 0, slab, nullptr, 0);

    float modelLow[3];
    float modelHigh[3];
    if (picked == 0 && ModelBounds(id, 0, modelLow, modelHigh))
    {
        Amod::Console::Printf("nothing in y %.4f..%.4f; this model runs %.4f..%.4f\n", low, high,
                              modelLow[1], modelHigh[1]);
        return;
    }

    Amod::Console::Printf("%d of %d vert(s) picked, %s\n", picked, model.vertexCount,
                          Amod::Mmd::Transform(id, -1, slab, Amod::Mmd::Matrix::Move(0.0f, lift, 0.0f))
                              ? "moved"
                              : "refused");
}

// The primitive for hanging one model off another: where a vertex is, on the frame being shown.
static void __cdecl VertexCommand(int argc, const char** argv, void*)
{
    AmodVertex vertex;
    if (argc < 4 || !Amod::Mmd::GetVertex(Chosen(argc, argv, 1), atoi(argv[2]), atoi(argv[3]),
                                          vertex))
    {
        Amod::Console::Printf("mmdvert <n> <frame> <index>\n");
        return;
    }

    Amod::Console::Printf("vertex %s at %.3f %.3f %.3f  normal %.2f %.2f %.2f  uv %.2f %.2f\n",
                          argv[3], vertex.position[0], vertex.position[1], vertex.position[2],
                          vertex.normal[0], vertex.normal[1], vertex.normal[2], vertex.uv[0],
                          vertex.uv[1]);
}

static AmodModelId g_knifeModel = AMOD_INVALID_MODEL;
static uint32_t g_knifeTexture = 0;
static AmodToken g_knifeHandler = AMOD_INVALID_TOKEN;
static int g_knifeHand = 207;
static float g_knifeScale = 0.35f;
static float g_knifeTurn = 90.0f;

static void __cdecl OnPlayerDrawn(const AmodModelDrawInfo* info, void*)
{
    AmodVertex a;
    AmodVertex b;
    if (!Amod::Mmd::GetVertex(info->model, info->frameA < 0 ? 0 : info->frameA, g_knifeHand, a))
    {
        return;
    }

    // Between the two frames, exactly where the game has the hand this instant.
    float grip[3] = { a.position[0], a.position[1], a.position[2] };
    if (info->frameB >= 0 && Amod::Mmd::GetVertex(info->model, info->frameB, g_knifeHand, b))
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            grip[axis] += (b.position[axis] - a.position[axis]) * info->blend;
        }
    }

    const float turn = g_knifeTurn * 3.14159265f / 180.0f;
    const float turnCos = cosf(turn);
    const float turnSin = sinf(turn);

    Amod::Mmd::Matrix place = Amod::Mmd::Matrix::Identity();
    place.m[0] = g_knifeScale * turnCos;
    place.m[1] = g_knifeScale * turnSin;
    place.m[4] = -g_knifeScale * turnSin;
    place.m[5] = g_knifeScale * turnCos;
    place.m[10] = g_knifeScale;
    place.m[12] = grip[0];
    place.m[13] = grip[1];
    place.m[14] = grip[2];

    Amod::Mmd::DrawWith(g_knifeModel, g_knifeTexture, place);
}

static void __cdecl KnifeCommand(int argc, const char** argv, void*)
{
    const AmodModelId player = Amod::Mmd::Find("models/player.mmd");

    AmodModel p;
    if (!Amod::Mmd::Get(player, p))
    {
        Amod::Console::Printf("the player model is not loaded yet\n");
        return;
    }

    // Bare command toggles; a dash always takes it off.
    if ((argc == 1 && g_knifeHandler != AMOD_INVALID_TOKEN) ||
        (argc > 1 && strcmp(argv[1], "-") == 0))
    {
        Amod::Mmd::RemoveOnDraw(g_knifeHandler);
        g_knifeHandler = AMOD_INVALID_TOKEN;
        Amod::Console::Printf("the knife is gone\n");
        return;
    }

    g_knifeHand = argc > 1 ? atoi(argv[1]) : 207;
    g_knifeScale = argc > 2 ? static_cast<float>(atof(argv[2])) : 0.35f;
    g_knifeTurn = argc > 3 ? static_cast<float>(atof(argv[3])) : 90.0f;

    if (g_knifeHand < 0 || g_knifeHand >= p.vertexCount)
    {
        Amod::Console::Printf("vertex %d is not one of the player's %d\n", g_knifeHand,
                              p.vertexCount);
        return;
    }

    g_knifeModel = Amod::Mmd::Find("models/bonus/knife.mmd");
    g_knifeModel = Amod::Mmd::IsValid(g_knifeModel) ? g_knifeModel
                                                    : Amod::Mmd::Load("models/bonus/knife.mmd");
    g_knifeTexture = Amod::Mmd::Texture("textures/bonus/knife.jpg");

    if (!Amod::Mmd::IsValid(g_knifeModel))
    {
        Amod::Console::Printf("the knife model would not load\n");
        return;
    }

    Amod::Mmd::RemoveOnDraw(g_knifeHandler);
    g_knifeHandler = Amod::Mmd::OnDraw(player, &OnPlayerDrawn);

    Amod::Console::Printf(g_knifeHandler != AMOD_INVALID_TOKEN
                              ? "knife on vertex %d, scale %.2f, %.0f degrees, texture %s\n"
                              : "refused\n",
                          g_knifeHand, g_knifeScale, g_knifeTurn,
                          g_knifeTexture != 0 ? "ok" : "MISSING");
}

static void __cdecl StateCommand(int argc, const char** argv, void*)
{
    const int index = argc > 1 ? atoi(argv[1]) : 0;

    AmodAnimState state;
    if (!Amod::Mmd::GetEntityAnim(index, state))
    {
        Amod::Console::Printf("mmdstate <entity>\n");
        return;
    }

    Amod::Console::Printf("entity %d: '%s' frames %d..%d blend %.2f speed %.2f%s%s%s\n", index,
                          state.frameset, state.frameA, state.frameB, state.blend, state.speed,
                          state.backwards ? "  backwards" : "", state.stopped ? "  stopped" : "",
                          state.finished ? "  finished" : "");
}

static void __cdecl PlayCommand(int argc, const char** argv, void*)
{
    if (argc < 3)
    {
        Amod::Console::Printf("mmdplay <entity> <frameset>, or mmdplay <entity> - to stop\n");
        return;
    }

    const int index = atoi(argv[1]);
    const bool stop = strcmp(argv[2], "-") == 0;
    const bool done = stop ? Amod::Mmd::StopEntityAnim(index)
                           : Amod::Mmd::PlayEntityAnim(index, argv[2]);

    Amod::Console::Printf(done ? "entity %d: %s\n" : "entity %d: %s refused\n", index, argv[2]);
}

static void __cdecl RestoreCommand(int argc, const char** argv, void*)
{
    AmodModel model;
    if (!Amod::Mmd::Get(Chosen(argc, argv, 1), model))
    {
        Amod::Console::Printf("mmdrestore <n>\n");
        return;
    }

    Amod::Console::Printf(Amod::Mmd::Restore(Amod::Mmd::At(atoi(argv[1])))
                              ? "%s is as it was\n"
                              : "%s had nothing to put back\n",
                          model.name);
}

static void __cdecl ShowCommand(int argc, const char** argv, void*)
{
    g_show = !g_show;
    if (!g_show)
    {
        Amod::Console::Printf("the pyramid is out of the way\n");
        return;
    }

    g_pyramid = Amod::Mmd::IsValid(g_pyramid) ? g_pyramid : BuildPyramid();
    if (argc > 1)
    {
        g_showTexture = Amod::Textures::Load(argv[1]);
    }

    AmodModelDraw draw = Amod::Mmd::Placement(g_pyramid, 0.0f, -0.4f, -3.0f, 0.01f);
    draw.texture = g_showTexture;

    Amod::Console::Printf("model %s, %d slot(s) free, queued %s%s\n",
                          Amod::Mmd::IsValid(g_pyramid) ? "ok" : "MISSING - run mmdmake",
                          Amod::Mmd::QueueRoom(), Amod::Mmd::Enqueue(draw) ? "ok" : "REFUSED",
                          g_showTexture == 0 ? ", no texture" : "");
}

static void __cdecl AnimCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        const int count = Amod::Mmd::AnimationCount();
        Amod::Console::Printf("%d animation(s) loaded\n", count);

        for (int index = 0; index < count; ++index)
        {
            AmodAnimation animation;
            if (Amod::Mmd::GetAnimation(Amod::Mmd::AnimationAt(index), animation))
            {
                Amod::Console::Printf("%3d  framesets %-3d refs %-3d %s\n", index,
                                      animation.framesetCount, animation.references,
                                      animation.name);
            }
        }
        return;
    }

    const AmodAnimationId id = Amod::Mmd::AnimationAt(atoi(argv[1]));
    AmodAnimation animation;
    if (!Amod::Mmd::GetAnimation(id, animation))
    {
        Amod::Console::Printf("mmdanim [n]\n");
        return;
    }

    Amod::Console::Printf("%s\n", animation.name);
    for (int at = 0; at < animation.framesetCount; ++at)
    {
        AmodFrameset frameset;
        if (Amod::Mmd::GetFrameset(id, at, frameset))
        {
            Amod::Console::Printf("  %-20s %6.2f fps  frames %d..%d start %d%s%s  %s\n",
                                  frameset.name, frameset.fps, frameset.from, frameset.to,
                                  frameset.startFrame, frameset.loop ? "  loop" : "",
                                  frameset.backwards ? "  backwards" : "", frameset.follow);
        }
    }
}


static AmodTextureId g_drawn = AMOD_INVALID_TEXTURE;

static void __cdecl TexturesCommand(int argc, const char** argv, void*)
{
    const int count = Amod::Textures::Count();
    Amod::Console::Printf("%d texture(s) loaded\n", count);

    const int shown = argc > 1 ? atoi(argv[1]) : 24;
    for (int index = 0; index < count && index < shown; ++index)
    {
        AmodTexture texture;
        if (Amod::Textures::Get(Amod::Textures::At(index), texture))
        {
            Amod::Console::Printf("%3d  %4dx%-4d %s%s refs %-3d %s\n", index, texture.width,
                                  texture.height, texture.mipmapped ? "mip " : "    ",
                                  texture.hasAlpha ? "alpha" : "     ", texture.references,
                                  texture.madeHere ? "<made here>" : texture.name);
        }
    }
}

// A texture drawn in code, with no file behind it: checkers with a coloured border.
static void __cdecl PaintCommand(int argc, const char** argv, void*)
{
    const int size = argc > 1 ? atoi(argv[1]) : 64;
    if (size < 4 || size > 512)
    {
        Amod::Console::Printf("mmdpaint [size] [path to stand in for]\n");
        return;
    }

    unsigned char* const pixels = static_cast<unsigned char*>(malloc(size * size * 4));
    if (pixels == nullptr)
    {
        return;
    }

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            unsigned char* const at = pixels + (y * size + x) * 4;
            const bool edge = x < 2 || y < 2 || x >= size - 2 || y >= size - 2;
            const bool light = ((x >> 3) + (y >> 3)) % 2 == 0;

            at[0] = edge ? 255 : (light ? 210 : 60);
            at[1] = edge ? 40 : (light ? 170 : 40);
            at[2] = edge ? 40 : (light ? 90 : 30);
            at[3] = 255;
        }
    }

    g_drawn = Amod::Textures::Create(size, size, pixels);
    unsigned char* const pixelsKept = pixels;

    if (!Amod::Textures::IsValid(g_drawn))
    {
        Amod::Console::Printf("the texture would not be made\n");
        return;
    }

    if (argc > 2)
    {
        const AmodTextureId loaded = Amod::Textures::Find(argv[2]);
        const bool painted = Amod::Textures::IsValid(loaded) &&
                             Amod::Textures::Replace(loaded, size, size, pixelsKept);
        const bool standing = Amod::Textures::Override(argv[2], g_drawn);

        Amod::Console::Printf("%dx%d painted; '%s' %s, future loads %s\n", size, size, argv[2],
                              painted ? "repainted now" : "was not loaded",
                              standing ? "will get it" : "refused");
        free(pixelsKept);
        return;
    }

    free(pixelsKept);
    g_showTexture = g_drawn;
    Amod::Console::Printf("%dx%d painted; mmdshow will wear it\n", size, size);
}

// Writes a model back out as a .mmd, so geometry the mod generated outlives the process.
static void __cdecl SaveCommand(int argc, const char** argv, void*)
{
    AmodModel model;
    if (argc < 3 || !Amod::Mmd::Get(Chosen(argc, argv, 1), model))
    {
        Amod::Console::Printf("mmdsave <n> <path>, e.g. mmdsave 0 data/models/mine.mmd\n");
        return;
    }

    Amod::Console::Printf(Amod::Mmd::Save(Amod::Mmd::At(atoi(argv[1])), argv[2])
                              ? "%s written to '%s'\n"
                              : "%s would not go to '%s'\n",
                          model.name, argv[2]);
}

// Colour with no texture at all: the diffuse the game always leaves white.
static void __cdecl ColourCommand(int argc, const char** argv, void*)
{
    AmodModel model;
    if (argc < 3 || !Amod::Mmd::Get(Chosen(argc, argv, 1), model))
    {
        Amod::Console::Printf("mmdcolour <n> <aarrggbb|rainbow|off>\n");
        return;
    }

    const AmodModelId id = Amod::Mmd::At(atoi(argv[1]));

    if (strcmp(argv[2], "off") == 0)
    {
        Amod::Console::Printf(Amod::Mmd::ClearColours(id) ? "%s is white again\n"
                                                          : "%s refused\n", model.name);
        return;
    }

    if (strcmp(argv[2], "rainbow") != 0)
    {
        const uint32_t colour = static_cast<uint32_t>(strtoul(argv[2], nullptr, 16));
        Amod::Console::Printf(Amod::Mmd::SetColour(id, colour) ? "%s is now %08x\n"
                                                               : "%s refused\n",
                              model.name, colour);
        return;
    }

    // One colour per vertex, so the shading is visibly not a flat tint.
    uint32_t* const colours =
        static_cast<uint32_t*>(malloc(sizeof(uint32_t) * model.vertexCount));
    if (colours == nullptr)
    {
        return;
    }

    for (int at = 0; at < model.vertexCount; ++at)
    {
        const int step = model.vertexCount > 1 ? at * 255 / (model.vertexCount - 1) : 0;
        colours[at] = 0xFF000000u | (static_cast<uint32_t>(step) << 16) |
                      (static_cast<uint32_t>(255 - step) << 8) | 0x80u;
    }

    const bool done = Amod::Mmd::SetColours(id, colours, model.vertexCount);
    free(colours);

    Amod::Console::Printf(done ? "%s: %d vert(s) coloured\n" : "%s refused\n",
                          model.name, model.vertexCount);
}

static void __cdecl ColourBoxCommand(int argc, const char** argv, void*)
{
    AmodModel model;
    if (argc < 5 || !Amod::Mmd::Get(Chosen(argc, argv, 1), model) || model.vertexCount <= 0)
    {
        Amod::Console::Printf("mmdcolourbox <n> <minY> <maxY> <aarrggbb>\n");
        return;
    }

    const AmodModelId id = Amod::Mmd::At(atoi(argv[1]));
    const float wide = 1.0e9f;
    const AmodVertexFilter slab = Amod::Mmd::InBox(-wide, static_cast<float>(atof(argv[2])), -wide,
                                                   wide, static_cast<float>(atof(argv[3])), wide);
    const uint32_t colour = static_cast<uint32_t>(strtoul(argv[4], nullptr, 16));

    uint16_t* const picked =
        static_cast<uint16_t*>(malloc(sizeof(uint16_t) * model.vertexCount));
    uint32_t* const colours =
        static_cast<uint32_t*>(malloc(sizeof(uint32_t) * model.vertexCount));

    if (picked == nullptr || colours == nullptr)
    {
        free(picked);
        free(colours);
        return;
    }

    const int found = Amod::Mmd::SelectVertices(id, 0, slab, picked, model.vertexCount);

    float low[3];
    float high[3];
    if (found == 0 && ModelBounds(id, 0, low, high))
    {
        Amod::Console::Printf("nothing in y %g..%g; %s runs %.4f..%.4f\n", atof(argv[2]),
                              atof(argv[3]), model.name, low[1], high[1]);
        free(picked);
        free(colours);
        return;
    }

    for (int at = 0; at < model.vertexCount; ++at)
    {
        colours[at] = 0xFFFFFFFFu;
    }
    for (int at = 0; at < found && at < model.vertexCount; ++at)
    {
        colours[picked[at]] = colour;
    }

    const bool done = Amod::Mmd::SetColours(id, colours, model.vertexCount);
    free(picked);
    free(colours);

    Amod::Console::Printf(done ? "%s: %d of %d vert(s) are %08x\n" : "%s refused\n",
                          model.name, found, model.vertexCount, colour);
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Amod::Console::AddCommand("mmd", &ListCommand);
    Amod::Console::AddCommand("mmdinfo", &InfoCommand);
    Amod::Console::AddCommand("mmdmake", &MakeCommand);
    Amod::Console::AddCommand("mmdover", &OverrideCommand);
    Amod::Console::AddCommand("mmdroom", &RoomCommand);
    Amod::Console::AddCommand("mmdscale", &ScaleCommand);
    Amod::Console::AddCommand("mmdrestore", &RestoreCommand);
    Amod::Console::AddCommand("mmdshow", &ShowCommand);
    Amod::Console::AddCommand("mmdanim", &AnimCommand);
    Amod::Console::AddCommand("mmdmove", &MoveCommand);
    Amod::Console::AddCommand("mmdpart", &PartCommand);
    Amod::Console::AddCommand("mmdvert", &VertexCommand);
    Amod::Console::AddCommand("mmdstate", &StateCommand);
    Amod::Console::AddCommand("mmdplay", &PlayCommand);
    Amod::Console::AddCommand("mmdbounds", &BoundsCommand);
    Amod::Console::AddCommand("mmdknife", &KnifeCommand);
    Amod::Console::AddCommand("mmdtex", &TexturesCommand);
    Amod::Console::AddCommand("mmdpaint", &PaintCommand);
    Amod::Console::AddCommand("mmdsave", &SaveCommand);
    Amod::Console::AddCommand("mmdcolour", &ColourCommand);
    Amod::Console::AddCommand("mmdcolourbox", &ColourBoxCommand);

    return AMOD_OK;
}

// The list is emptied every frame, so anything wanted on screen is put back every frame.
AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame* frame)
{
    if (!g_show || !Amod::Mmd::IsValid(g_pyramid))
    {
        return;
    }

    g_spin += frame->delta;

    AmodModelDraw draw = Amod::Mmd::Placement(g_pyramid, 0.0f, -0.4f, -3.0f, 0.01f);
    draw.texture = g_showTexture;
    draw.rotation[1] = g_spin;
    Amod::Mmd::Enqueue(draw);
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
