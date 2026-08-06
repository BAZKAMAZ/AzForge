#include <ForgeApi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//   light                             every light of the room the player is in
//   light <n> <r> <g> <b> [range]     colour, and the range if it is given
//   light <n> move <x> <y>            where it stands
//   light add <x> <y> [r g b] [range] one the room did not have, kept by the room

//   dlight                             what is burning, and what there is to ride
//   dlight off [n]                     put them all out, or just that one
//   dlight <x> <y> [r g b] [range]     one standing there
//   dlight follow <n> [r g b] [range]  one riding entity n, so it travels

//   elight                             who carries one in this room
//   elight <n> [r g b] [range]         hand entity n a light of its own
//   elight <n> off                     take it away
//   elight me [r g b] [range]          the player, which is what the torch does

//   lmap                        what the room was baked with
//   lmap <scale>                all of it brighter or darker: 0.5 half, 2 twice
//   lmap tint <r> <g> <b>       each channel by its own amount
//   lmap spot <u> <v> <r> <w>   a round patch painted into the bake, 0..1 across the map

//   wall                        the three models the room is drawn from
//   wall bulge <amount>         every wall vertex pushed out along its own normal
//   wall colour <aarrggbb>      the walls painted
//   wall reset                  the geometry put back as it was read
//   ... floor | ... back        any of them against one of the other two

//   deco                              what the room is decorated with
//   deco <n> hide | show
//   deco <n> move <x> <y> <z>         where it stands
//   deco <n> scale <s>                how big, keeping where
//   deco <n> model <path>             a different model on the same spot
//   deco add <path> <x> <y> <z> [s]   one of the mod's own

//   lseed [seconds]             drop a light of its own colour where the player stands, over and
//   lseed off                   over, so a room comes to carry far more than the scene can hold

//   cell                        the grid the room is walked on, row by row
//   cell <col> <row> <char>     one cell; 'W' walls it, ' ' opens it
//   cell wall                   a wall across the row the player is standing on
//   cell clear                  every wall of the room taken away

//   box                                    what stops a body in this room
//   box <n> kind <k>                       0 and 3 push, 1 only reports, 2 pushes when asked
//   box add <minx> <miny> <maxx> <maxy>    one of the mod's own
//   box here                               a ledge under the player - the other half of 'cell wall'

//   ltri                        the surfaces a dynamic light is allowed to fall on
//   ltri here                   a patch of floor under the player added to them
//   ltri off                    every one of them flattened to nothing

struct Burning
{
    int room;               // the room it belongs to; a light of this kind is tied to none by itself
    int rides;              // the entity it follows, or below zero for a fixed spot
    float at[2];
    float colour[3];
    float range;
};

static Burning g_lights[8];
static int g_lightCount = 0;

static void __cdecl LightCommand(int argc, const char** argv, void*)
{
    const int count = Amod::World::LightCount();
    const int index = argc > 1 ? atoi(argv[1]) : -1;

    if (argc < 2)
    {
        //A room with no lights of its own is lit by what was baked into it, so the counts beside
        //them say whether that is the case or whether the reading is at fault.
        AmodWorldRoom room;
        if (Amod::World::GetRoom(Amod::World::CurrentRoom(), room))
        {
            Amod::Console::Printf("room %d (%s): %d box(es), %d light triangle(s)\n", room.id, room.file, room.collisionBoxCount, room.lightTriangleCount);
        }

        Amod::Console::Printf("%d light(s) here%s\n", count,
                              count > 16 ? "; the scene holds 16 at a time, refilled every frame "
                                           "with the ones that reach you" : "");

        for (int at = 0; at < count; ++at)
        {
            AmodWorldLight light;
            if (Amod::World::GetLight(-1, at, light))
            {
                Amod::Console::Printf("%2d  rgb %.2f %.2f %.2f  range %.3f  at %+.3f %+.3f\n", at, light.colour[0], light.colour[1], light.colour[2],
                                      light.range, light.position[0], light.position[1]);
            }
        }
        return;
    }

    if (strcmp(argv[1], "add") == 0)
    {
        if (argc < 4)
        {
            Amod::Console::Printf("light add <x> <y> [r g b] [range]\n");
            return;
        }

        AmodWorldLight fresh = {};
        fresh.structSize = sizeof(fresh);
        fresh.alpha = 1.0f;
        fresh.colour[0] = argc > 6 ? static_cast<float>(atof(argv[4])) : 1.0f;
        fresh.colour[1] = argc > 6 ? static_cast<float>(atof(argv[5])) : 1.0f;
        fresh.colour[2] = argc > 6 ? static_cast<float>(atof(argv[6])) : 1.0f;
        fresh.range = argc > 7 ? static_cast<float>(atof(argv[7])) : 2.0f;
        fresh.position[0] = static_cast<float>(atof(argv[2]));
        fresh.position[1] = static_cast<float>(atof(argv[3]));

        const int put = Amod::World::AddRoomLight(fresh);
        if (put < 0)
        {
            Amod::Console::Printf("light: %s\n", Amod::LastError());
            return;
        }
        Amod::Console::Printf("light %d is the room's own now, and will be here on the next visit\n", put);
        return;
    }

    AmodWorldLight light;
    if (!Amod::World::GetLight(-1, index, light))
    {
        Amod::Console::Printf("light: %s\n", Amod::LastError());
        return;
    }

    if (argc > 4 && strcmp(argv[2], "move") == 0)
    {
        light.position[0] = static_cast<float>(atof(argv[3]));
        light.position[1] = static_cast<float>(atof(argv[4]));
    }
    else if (argc > 4)
    {
        light.colour[0] = static_cast<float>(atof(argv[2]));
        light.colour[1] = static_cast<float>(atof(argv[3]));
        light.colour[2] = static_cast<float>(atof(argv[4]));

        //The range is the reach and the brightness at once - there is no separate strength.
        if (argc > 5)
        {
            light.range = static_cast<float>(atof(argv[5]));
        }
    }

    if (argc > 4 && !Amod::World::SetLight(-1, index, light))
    {
        Amod::Console::Printf("light: %s\n", Amod::LastError());
        return;
    }

    Amod::Console::Printf("%2d  rgb %.2f %.2f %.2f  range %.3f  at %+.3f %+.3f\n", index, light.colour[0], light.colour[1], light.colour[2], light.range,
                          light.position[0], light.position[1]);
}

static void __cdecl DynamicLightCommand(int argc, const char** argv, void*)
{
    if (argc > 1 && strcmp(argv[1], "off") == 0)
    {
        const int which = argc > 2 ? atoi(argv[2]) : -1;

        if (which < 0)
        {
            g_lightCount = 0;
            Amod::Console::Printf("all out\n");
            return;
        }
        if (which >= g_lightCount)
        {
            Amod::Console::Printf("there is no light %d burning\n", which);
            return;
        }
        g_lights[which] = g_lights[--g_lightCount];
        Amod::Console::Printf("light %d is out, %d left\n", which, g_lightCount);
        return;
    }

    if (argc < 3)
    {
        Amod::Console::Printf("dlight [off [n] | <x> <y> | follow <n>] [r g b] [range]\n");
        Amod::Console::Printf("%d burning:\n", g_lightCount);

        for (int at = 0; at < g_lightCount; ++at)
        {
            const Burning& light = g_lights[at];
            Amod::Console::Printf("%2d  room %-3d %-14s rgb %.2f %.2f %.2f  range %.2f\n", at,
                                  light.room, light.rides >= 0 ? "on an entity" : "where it was put",
                                  light.colour[0], light.colour[1], light.colour[2], light.range);
        }

        for (int at = 0; at < Amod::World::CurrentEntityCount(); ++at)
        {
            const Amod::World::Entity what = Amod::World::CurrentEntity(at);
            if (what.IsValid())
            {
                Amod::Console::Printf("%2d  %-16s at %+.3f %+.3f\n", at, what.Name()[0] != '\0' ? what.Name() : "(nameless)", what.GetX(), what.GetY());
            }
        }
        return;
    }

    if (g_lightCount == static_cast<int>(sizeof(g_lights) / sizeof(g_lights[0])))
    {
        Amod::Console::Printf("this mod keeps room for %d and holds that many\n", g_lightCount);
        return;
    }

    Burning& light = g_lights[g_lightCount];
    const bool rides = strcmp(argv[1], "follow") == 0;
    int next = 2;

    light.room = Amod::World::CurrentRoom();
    light.rides = -1;
    light.colour[0] = 1.0f;
    light.colour[1] = 0.6f;
    light.colour[2] = 0.2f;
    light.range = 2.0f;

    if (rides)
    {
        light.rides = atoi(argv[2]);
        next = 3;
    }
    else
    {
        light.at[0] = static_cast<float>(atof(argv[1]));
        light.at[1] = static_cast<float>(atof(argv[2]));
    }

    if (argc > next + 2)
    {
        light.colour[0] = static_cast<float>(atof(argv[next]));
        light.colour[1] = static_cast<float>(atof(argv[next + 1]));
        light.colour[2] = static_cast<float>(atof(argv[next + 2]));
        next += 3;
    }
    if (argc > next)
    {
        light.range = static_cast<float>(atof(argv[next]));
    }

    ++g_lightCount;
    Amod::Console::Printf("light %d burning in room %d, rgb %.2f %.2f %.2f, range %.2f\n", g_lightCount - 1, light.room, light.colour[0], light.colour[1],
        light.colour[2], light.range);
}

static void __cdecl EntityLightCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        const int player = Amod::World::PlayerEntity();
        Amod::Console::Printf("elight <n|me> [off | r g b] [range]; the player is entity %d\n", player);

        for (int at = 0; at < Amod::World::CurrentEntityCount(); ++at)
        {
            const Amod::World::Entity what = Amod::World::CurrentEntity(at);
            AmodEntityLight light;

            if (!what.IsValid() || !Amod::World::GetEntityLight(-1, at, light) || light.isLit == 0)
            {
                continue;
            }
            Amod::Console::Printf("%2d  %-16s rgb %.2f %.2f %.2f  range %.3f%s\n", at, what.Name()[0] != '\0' ? what.Name() : "(nameless)",
                                  light.colour[0], light.colour[1], light.colour[2], light.range, at == player ? "   <- the torch" : "");
        }
        return;
    }

    const int which = strcmp(argv[1], "me") == 0 ? Amod::World::PlayerEntity() : atoi(argv[1]);

    if (which < 0)
    {
        Amod::Console::Printf("elight: there is no player here\n");
        return;
    }

    if (argc > 2 && strcmp(argv[2], "off") == 0)
    {
        if (Amod::World::UnlightEntity(which))
        {
            Amod::Console::Printf("entity %d is dark again\n", which);
            return;
        }
        Amod::Console::Printf("elight: %s\n", Amod::LastError());
        return;
    }

    //Read first, so what is not given keeps whatever it already had.
    AmodEntityLight light;
    if (!Amod::World::GetEntityLight(-1, which, light))
    {
        Amod::Console::Printf("elight: %s\n", Amod::LastError());
        return;
    }

    if (argc > 4)
    {
        light.colour[0] = static_cast<float>(atof(argv[2]));
        light.colour[1] = static_cast<float>(atof(argv[3]));
        light.colour[2] = static_cast<float>(atof(argv[4]));
    }
    if (argc > 5)
    {
        light.range = static_cast<float>(atof(argv[5]));
    }
    if (light.range <= 0.0f)
    {
        light.range = 2.0f;
    }

    light.isLit = 1;
    if (!Amod::World::SetEntityLight(-1, which, light))
    {
        Amod::Console::Printf("elight: %s\n", Amod::LastError());
        return;
    }

    //Nothing is asked for again: the game carries this one by itself, every frame, wherever the
    //entity goes.
    Amod::Console::Printf("entity %d carries rgb %.2f %.2f %.2f, range %.2f\n", which,  light.colour[0], light.colour[1], light.colour[2], light.range);
}

//The baked light is 256x256 and the mod works on a copy of it, so there is one to hold.
static unsigned char g_bake[256 * 256 * 4];

static unsigned char Clamped(float value)
{
    if (value < 0.0f) { return 0; }
    return value > 255.0f ? 255 : static_cast<unsigned char>(value);
}

static void __cdecl LightmapCommand(int argc, const char** argv, void*)
{
    const AmodTextureId lightmap = Amod::World::Lightmap();
    AmodTexture about;

    if (lightmap == AMOD_INVALID_TEXTURE || !Amod::Textures::Get(lightmap, about))
    {
        Amod::Console::Printf("lmap: %s\n", Amod::LastError());
        return;
    }

    const int bytes = Amod::Textures::ReadPixels(lightmap);
    if (bytes <= 0 || bytes > static_cast<int>(sizeof(g_bake)) ||
        Amod::Textures::ReadPixels(lightmap, g_bake, bytes) != bytes)
    {
        Amod::Console::Printf("lmap: the lightmap keeps no pixels to work on\n");
        return;
    }

    if (argc < 2)
    {
        //An average is enough to say whether the room is baked bright or dark at all.
        long long sum = 0;
        for (int at = 0; at < bytes; at += 4)
        {
            sum += g_bake[at] + g_bake[at + 1] + g_bake[at + 2];
        }
        Amod::Console::Printf("lmap [scale | tint r g b | spot u v r w]\n");
        Amod::Console::Printf("%dx%d, %d bytes, average %.1f of 255\n", about.width, about.height, bytes, static_cast<double>(sum) / (bytes / 4 * 3));
        return;
    }

    const int texels = bytes / 4;
    const int width = about.width;

    if (strcmp(argv[1], "spot") == 0 && argc > 5)
    {
        //Painted into the bake itself, so it is there for good and costs nothing to draw - the
        //other half of what a light for one frame does.
        const float u = static_cast<float>(atof(argv[2])) * width;
        const float v = static_cast<float>(atof(argv[3])) * about.height;
        const float radius = static_cast<float>(atof(argv[4])) * width;
        const float weight = static_cast<float>(atof(argv[5]));

        for (int at = 0; at < texels; ++at)
        {
            const float dx = static_cast<float>(at % width) - u;
            const float dy = static_cast<float>(at / width) - v;
            const float away = dx * dx + dy * dy;

            if (radius <= 0.0f || away > radius * radius)
            {
                continue;
            }

            //One at the middle, nothing at the edge, the same falloff the game lights a point by.
            const float much = weight * (1.0f - (away / (radius * radius)));
            for (int channel = 0; channel < 3; ++channel)
            {
                g_bake[at * 4 + channel] = Clamped(g_bake[at * 4 + channel] + much * 255.0f);
            }
        }
    }
    else
    {
        float scale[3] = { 1.0f, 1.0f, 1.0f };

        if (strcmp(argv[1], "tint") == 0 && argc > 4)
        {
            scale[0] = static_cast<float>(atof(argv[2]));
            scale[1] = static_cast<float>(atof(argv[3]));
            scale[2] = static_cast<float>(atof(argv[4]));
        }
        else
        {
            scale[0] = scale[1] = scale[2] = static_cast<float>(atof(argv[1]));
        }

        for (int at = 0; at < texels; ++at)
        {
            for (int channel = 0; channel < 3; ++channel)
            {
                g_bake[at * 4 + channel] = Clamped(g_bake[at * 4 + channel] * scale[channel]);
            }
        }
    }

    if (!Amod::Textures::Replace(lightmap, about.width, about.height, g_bake))
    {
        Amod::Console::Printf("lmap: the lightmap would not take the new pixels\n");
        return;
    }
    Amod::Console::Printf("the room is baked anew; it stays until the level is left\n");
}

static AmodVertex g_verts[8192];

static AmodModelId RoomPart(int argc, const char** argv, const char** named)
{
    for (int at = 1; at < argc; ++at)
    {
        if (strcmp(argv[at], "floor") == 0)
        {
            *named = "floor";
            return Amod::Mmd::RoomModel(-1, AMOD_ROOM_MODEL_LIGHTMAP);
        }
        if (strcmp(argv[at], "back") == 0)
        {
            *named = "back wall";
            return Amod::Mmd::RoomModel(-1, AMOD_ROOM_MODEL_BACKWALL);
        }
    }
    *named = "walls";
    return Amod::Mmd::RoomModel(-1, AMOD_ROOM_MODEL_GEOMETRY);
}

static void __cdecl WallCommand(int argc, const char** argv, void*)
{
    static const char* const kSlots[] = { "walls", "floor", "back wall" };
    static const int kWhich[] = { AMOD_ROOM_MODEL_GEOMETRY, AMOD_ROOM_MODEL_LIGHTMAP, AMOD_ROOM_MODEL_BACKWALL };

    if (argc < 2)
    {
        Amod::Console::Printf("wall [bulge <amount> | colour <aarrggbb> | reset] [floor|back]\n");

        for (int at = 0; at < 3; ++at)
        {
            const AmodModelId model = Amod::Mmd::RoomModel(-1, kWhich[at]);
            AmodModel about;

            if (model == AMOD_INVALID_MODEL || !Amod::Mmd::Get(model, about))
            {
                Amod::Console::Printf("  %-10s none\n", kSlots[at]);
                continue;
            }
            //'edited' is whether the loader is keeping what this was, which is the whole of
            //whether 'wall reset' has anything to give back.
            Amod::Console::Printf("  %-10s model %u, kind %d, %d vert(s), %d frame(s)%s\n", kSlots[at],  model, about.kind, about.vertexCount, about.frameCount,
                                  about.edited ? ", edited" : "");
        }
        return;
    }

    const char* named = "";
    const AmodModelId model = RoomPart(argc, argv, &named);
    AmodModel about;

    if (model == AMOD_INVALID_MODEL || !Amod::Mmd::Get(model, about))
    {
        Amod::Console::Printf("wall: this room is drawn from nothing the loader can reach\n");
        return;
    }

    if (strcmp(argv[1], "reset") == 0)
    {
        if (Amod::Mmd::Restore(model))
        {
            Amod::Console::Printf("the %s are as they were read\n", named);
            return;
        }
        Amod::Console::Printf("wall: the %s would not go back - %s\n", named,
                              about.edited ? "the snapshot would not build"
                                           : "nothing was written to them to undo");
        return;
    }

    if (strcmp(argv[1], "colour") == 0 && argc > 2)
    {
        const uint32_t colour = static_cast<uint32_t>(strtoul(argv[2], nullptr, 16));
        Amod::Console::Printf(Amod::Mmd::SetColour(model, colour) ? "the %s are painted\n"
                                                                  : "wall: the %s refused paint\n", named);
        return;
    }

    if (strcmp(argv[1], "bulge") != 0 || argc < 3)
    {
        Amod::Console::Printf("wall [bulge <amount> | colour <aarrggbb> | reset] [floor|back]\n");
        return;
    }

    //Writing takes the whole frame or nothing, so a mesh bigger than there is room for here is
    //said so plainly rather than half written.
    const int room = static_cast<int>(sizeof(g_verts) / sizeof(g_verts[0]));
    if (about.vertexCount > room)
    {
        Amod::Console::Printf("wall: the %s have %d vertices and this mod holds %d\n", named, about.vertexCount, room);
        return;
    }

    const int count = Amod::Mmd::ReadVertices(model, 0, g_verts, room);
    if (count <= 0)
    {
        Amod::Console::Printf("wall: the %s gave up no vertices\n", named);
        return;
    }

    int normalled = 0;
    for (int at = 0; at < count; ++at)
    {
        const AmodVertex& vertex = g_verts[at];
        if (vertex.normal[0] != 0.0f || vertex.normal[1] != 0.0f || vertex.normal[2] != 0.0f)
        {
            ++normalled;
        }
    }

    const float much = static_cast<float>(atof(argv[2]));
    const bool byNormal = normalled > count / 2;

    for (int at = 0; at < count; ++at)
    {
        float way[3];
        float length = 0.0f;

        for (int axis = 0; axis < 3; ++axis)
        {
            way[axis] = byNormal ? g_verts[at].normal[axis]
                                 : g_verts[at].position[axis] - about.centre[axis];
            length += way[axis] * way[axis];
        }

        if (length <= 0.0f)
        {
            continue;
        }

        length = sqrtf(length);
        for (int axis = 0; axis < 3; ++axis)
        {
            g_verts[at].position[axis] += way[axis] / length * much;
        }
    }

    if (!Amod::Mmd::WriteVertices(model, 0, g_verts, count))
    {
        Amod::Console::Printf("wall: the %s would not take the new vertices\n", named);
        return;
    }
    Amod::Console::Printf("%d vertices of the %s moved %.3f %s; %d of them carry a normal\n",
                          count, named, much, byNormal ? "along their normals" : "out from the middle",
                          normalled);
}

//The translation of a 4x4 in the order the card is handed it.
static const int kX = 12;
static const int kY = 13;
static const int kZ = 14;

static void __cdecl DecoCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        const int count = Amod::World::StaticCount();
        Amod::Console::Printf("deco [<n> hide|show|move|scale|model | add <path> <x> <y> <z>]\n");
        Amod::Console::Printf("%d static(s) here:\n", count);

        for (int at = 0; at < count; ++at)
        {
            AmodRoomStatic piece;
            AmodModel about;

            if (!Amod::World::GetStatic(at, piece))
            {
                continue;
            }

            const bool named = Amod::Mmd::Get(piece.model, about);
            Amod::Console::Printf("%2d  %-28s at %+.3f %+.3f %+.3f%s%s\n", at, named && about.name[0] != '\0' ? about.name : "(from the room)",
                                  piece.matrix[kX], piece.matrix[kY], piece.matrix[kZ], piece.isShown ? "" 
                                    : "  hidden", piece.isTranslucent ? "  translucent" : "");
        }
        return;
    }

    if (strcmp(argv[1], "add") == 0)
    {
        if (argc < 6)
        {
            Amod::Console::Printf("deco add <model path> <x> <y> <z> [scale]\n");
            return;
        }

        const AmodModelId model = Amod::Mmd::Load(argv[2]);
        if (model == AMOD_INVALID_MODEL)
        {
            Amod::Console::Printf("deco: '%s' would not load\n", argv[2]);
            return;
        }

        const float size = argc > 6 ? static_cast<float>(atof(argv[6])) : 1.0f;

        AmodRoomStatic piece = {};
        piece.structSize = sizeof(piece);
        piece.isShown = 1;
        piece.isLit = 1;
        piece.model = model;
        piece.matrix[0] = piece.matrix[5] = piece.matrix[10] = size;
        piece.matrix[15] = 1.0f;
        piece.matrix[kX] = static_cast<float>(atof(argv[3]));
        piece.matrix[kY] = static_cast<float>(atof(argv[4]));
        piece.matrix[kZ] = static_cast<float>(atof(argv[5]));

        const int put = Amod::World::AddStatic(piece);
        if (put < 0)
        {
            Amod::Console::Printf("deco: %s\n", Amod::LastError());
            return;
        }
        Amod::Console::Printf("'%s' stands in the room as static %d\n", argv[2], put);
        return;
    }

    const int which = atoi(argv[1]);
    AmodRoomStatic piece;

    if (!Amod::World::GetStatic(which, piece))
    {
        Amod::Console::Printf("deco: %s\n", Amod::LastError());
        return;
    }

    if (argc > 2 && (strcmp(argv[2], "hide") == 0 || strcmp(argv[2], "show") == 0))
    {
        piece.isShown = strcmp(argv[2], "show") == 0 ? 1 : 0;
    }
    else if (argc > 5 && strcmp(argv[2], "move") == 0)
    {
        piece.matrix[kX] = static_cast<float>(atof(argv[3]));
        piece.matrix[kY] = static_cast<float>(atof(argv[4]));
        piece.matrix[kZ] = static_cast<float>(atof(argv[5]));
    }
    else if (argc > 3 && strcmp(argv[2], "scale") == 0)
    {
        //Only the three that carry the size, so whatever turn it was given is kept.
        const float size = static_cast<float>(atof(argv[3]));
        piece.matrix[0] = piece.matrix[5] = piece.matrix[10] = size;
    }
    else if (argc > 3 && strcmp(argv[2], "model") == 0)
    {
        const AmodModelId model = Amod::Mmd::Load(argv[3]);
        if (model == AMOD_INVALID_MODEL)
        {
            Amod::Console::Printf("deco: '%s' would not load\n", argv[3]);
            return;
        }
        piece.model = model;
    }
    else
    {
        Amod::Console::Printf("deco <n> hide|show|move <x> <y> <z>|scale <s>|model <path>\n");
        return;
    }

    if (!Amod::World::SetStatic(which, piece))
    {
        Amod::Console::Printf("deco: %s\n", Amod::LastError());
        return;
    }
    Amod::Console::Printf("static %d taken; it changes on the spot, no need to leave the room\n", which);
}

static bool g_seeding = false;
static float g_every = 5.0f;
static float g_since = 0.0f;
static int g_seeded = 0;

static void __cdecl SeedCommand(int argc, const char** argv, void*)
{
    if (argc > 1 && strcmp(argv[1], "off") == 0)
    {
        g_seeding = false;
        Amod::Console::Printf("done; %d light(s) were left in the room\n", g_seeded);
        return;
    }

    if (argc > 1)
    {
        const float every = static_cast<float>(atof(argv[1]));
        g_every = every > 0.1f ? every : 5.0f;
    }

    g_seeding = true;
    g_since = g_every;
    g_seeded = 0;
    Amod::Console::Printf("a light where you stand every %.1f second(s); 'lseed off' stops\n", g_every);
}

//A colour of its own, settled once and never touched again
static void SeedOne(void)
{
    const int which = Amod::World::PlayerEntity();
    const Amod::World::Entity player = which >= 0 ? Amod::World::CurrentEntity(which)
                                                  : Amod::World::Entity();
    if (!player.IsValid())
    {
        return;
    }

    float colour[3];
    float brightest = 0.0f;

    for (int at = 0; at < 3; ++at)
    {
        colour[at] = 0.15f + static_cast<float>(rand() % 1000) / 1000.0f;
        brightest = colour[at] > brightest ? colour[at] : brightest;
    }

    AmodWorldLight light = {};
    light.structSize = sizeof(light);
    light.alpha = 1.0f;
    light.range = 1.5f;
    light.position[0] = player.GetX();
    light.position[1] = player.GetY();

    //Scaled so the brightest channel is full: a saturated colour rather than a washed-out one.
    for (int at = 0; at < 3; ++at)
    {
        light.colour[at] = colour[at] / brightest;
    }

    const int put = Amod::World::AddRoomLight(light);
    if (put < 0)
    {
        Amod::Console::Printf("lseed: %s\n", Amod::LastError());
        g_seeding = false;
        return;
    }

    ++g_seeded;
    Amod::Console::Printf("light %d at %+.3f %+.3f, rgb %.2f %.2f %.2f (%d of mine here)\n", put, light.position[0], light.position[1], light.colour[0], 
                            light.colour[1], light.colour[2], g_seeded);
}

static char g_cells[AMOD_ROOM_COLUMNS * AMOD_ROOM_ROWS];

static void __cdecl CellCommand(int argc, const char** argv, void*)
{
    const int cells = static_cast<int>(sizeof(g_cells));

    if (argc < 2)
    {
        if (Amod::World::Cells(Amod::World::CurrentRoom(), g_cells, cells) != cells)
        {
            Amod::Console::Printf("cell: the room gave up no grid\n");
            return;
        }

        Amod::Console::Printf("cell <col> <row> <char> | wall | clear\n");
        for (int row = 0; row < AMOD_ROOM_ROWS; ++row)
        {
            char line[AMOD_ROOM_COLUMNS + 1];
            for (int column = 0; column < AMOD_ROOM_COLUMNS; ++column)
            {
                line[column] = g_cells[row * AMOD_ROOM_COLUMNS + column];
            }
            line[AMOD_ROOM_COLUMNS] = '\0';
            Amod::Console::Printf("%2d %s\n", row, line);
        }
        return;
    }

    if (strcmp(argv[1], "clear") == 0 || strcmp(argv[1], "wall") == 0)
    {
        const bool clearing = strcmp(argv[1], "clear") == 0;

        if (Amod::World::Cells(Amod::World::CurrentRoom(), g_cells, cells) != cells)
        {
            Amod::Console::Printf("cell: the room gave up no grid\n");
            return;
        }

        if (clearing)
        {
            //Every wall taken away at once, which is what the whole-grid write is for.
            for (int at = 0; at < cells; ++at)
            {
                g_cells[at] = g_cells[at] == 'W' ? ' ' : '\0';
            }
        }
        else
        {
            //A wall across the row the player stands on, and nothing else touched.
            const int which = Amod::World::PlayerEntity();
            const Amod::World::Entity player = which >= 0 ? Amod::World::CurrentEntity(which)
                                                          : Amod::World::Entity();
            if (!player.IsValid())
            {
                Amod::Console::Printf("cell: there is no player to stand anywhere\n");
                return;
            }

            //Under the feet, not through them: rows run downwards, so the one below is where a
            //floor belongs. Walling the player's own row builds a wall inside the player.
            const int row = player.GetRow() + 1;
            int laid = 0;
            int left = 0;

            if (row < 0 || row >= AMOD_ROOM_ROWS)
            {
                Amod::Console::Printf("cell: there is no row under the player to build on\n");
                return;
            }

            char was[AMOD_ROOM_COLUMNS];
            memcpy(was, g_cells + row * AMOD_ROOM_COLUMNS, sizeof(was));
            memset(g_cells, 0, sizeof(g_cells));    // '\0' leaves a cell alone, so only this row is written

            for (int column = 0; column < AMOD_ROOM_COLUMNS; ++column)
            {
                //Only what is empty. A chain, a ladder, anything the room already put there is
                //left alone - writing 'W' over an 'I' is how the chain stopped being a chain.
                if (was[column] == ' ')
                {
                    g_cells[row * AMOD_ROOM_COLUMNS + column] = 'W';
                    ++laid;
                }
                else
                {
                    ++left;
                }
            }
            Amod::Console::Printf("row %d: %d cell(s) floored, %d left as the room had them\n",
                                  row, laid, left);
        }

        if (!Amod::World::SetCells(g_cells, cells))
        {
            Amod::Console::Printf("cell: %s\n", Amod::LastError());
        }
        return;
    }

    if (argc < 4)
    {
        Amod::Console::Printf("cell <col> <row> <char>\n");
        return;
    }

    if (!Amod::World::SetCell(atoi(argv[1]), atoi(argv[2]), argv[3][0]))
    {
        Amod::Console::Printf("cell: %s\n", Amod::LastError());
        return;
    }
    Amod::Console::Printf("cell %s,%s is '%c' now, and the room is walked on it at once\n", argv[1], argv[2], argv[3][0]);
}

static void __cdecl BoxCommand(int argc, const char** argv, void*)
{
    AmodWorldRoom room;
    if (!Amod::World::GetRoom(Amod::World::CurrentRoom(), room))
    {
        Amod::Console::Printf("box: there is no room here\n");
        return;
    }

    static const char* const kKinds[] = { "pushes", "reports only", "pushes when asked", "pushes" };

    if (argc < 2)
    {
        Amod::Console::Printf("box [<n> kind <k> | add <minx> <miny> <maxx> <maxy> | here]\n");
        Amod::Console::Printf("%d box(es) here; these are what a body meets, not the grid\n", room.collisionBoxCount);

        for (int at = 0; at < room.collisionBoxCount; ++at)
        {
            AmodRoomBox box;
            if (Amod::World::GetBox(at, box))
            {
                Amod::Console::Printf("%2d  %-18s x %+.3f..%+.3f  y %+.3f..%+.3f\n", at, box.kind >= 0 && box.kind < 4 ? kKinds[box.kind] : "?",
                                      box.min[0], box.max[0], box.min[1], box.max[1]);
            }
        }
        return;
    }

    //A new box takes its z from one the room was built with, so it stands in the same plane everything else does rather than in one guessed at.
    AmodRoomBox pattern;
    const bool haveZ = room.collisionBoxCount > 0 && Amod::World::GetBox(0, pattern);

    if (strcmp(argv[1], "add") == 0 || strcmp(argv[1], "here") == 0)
    {
        AmodRoomBox box = {};
        box.structSize = sizeof(box);
        box.kind = AMOD_BOX_PUSHES;
        box.min[2] = haveZ ? pattern.min[2] : -1.0f;
        box.max[2] = haveZ ? pattern.max[2] : 1.0f;

        if (strcmp(argv[1], "here") == 0)
        {
            const int which = Amod::World::PlayerEntity();
            const Amod::World::Entity player = which >= 0 ? Amod::World::CurrentEntity(which)
                                                          : Amod::World::Entity();
            if (!player.IsValid())
            {
                Amod::Console::Printf("box: there is no player to stand anywhere\n");
                return;
            }

            box.min[0] = player.GetX() - AMOD_ROOM_CELL_SIZE * 2.0f;
            box.max[0] = player.GetX() + AMOD_ROOM_CELL_SIZE * 2.0f;
            box.max[1] = player.GetY() - AMOD_ROOM_CELL_SIZE * 2.0f;
            box.min[1] = box.max[1] - AMOD_ROOM_CELL_SIZE;
        }
        else
        {
            if (argc < 6)
            {
                Amod::Console::Printf("box add <minx> <miny> <maxx> <maxy> [kind]\n");
                return;
            }
            box.min[0] = static_cast<float>(atof(argv[2]));
            box.min[1] = static_cast<float>(atof(argv[3]));
            box.max[0] = static_cast<float>(atof(argv[4]));
            box.max[1] = static_cast<float>(atof(argv[5]));
            box.kind = argc > 6 ? atoi(argv[6]) : AMOD_BOX_PUSHES;
        }

        const int put = Amod::World::AddBox(box);
        if (put < 0)
        {
            Amod::Console::Printf("box: %s\n", Amod::LastError());
            return;
        }

        Amod::Console::Printf("box %d stands at x %+.3f..%+.3f y %+.3f..%+.3f and stops a body now\n", put, box.min[0], box.max[0], box.min[1], box.max[1]);
        return;
    }

    if (argc < 4 || strcmp(argv[2], "kind") != 0)
    {
        Amod::Console::Printf("box <n> kind <0..3>\n");
        return;
    }

    const int which = atoi(argv[1]);
    AmodRoomBox box;

    if (!Amod::World::GetBox(which, box))
    {
        Amod::Console::Printf("box: %s\n", Amod::LastError());
        return;
    }

    box.kind = atoi(argv[3]);
    if (!Amod::World::SetBox(which, box))
    {
        Amod::Console::Printf("box: %s\n", Amod::LastError());
        return;
    }
    Amod::Console::Printf("box %d %s now\n", which, box.kind >= 0 && box.kind < 4 ? kKinds[box.kind] : "does something unknown");
}

static void __cdecl LightTriangleCommand(int argc, const char** argv, void*)
{
    const int count = Amod::World::LightTriangleCount();

    if (argc < 2)
    {
        Amod::Console::Printf("ltri [here | off]; %d surface(s) take a dynamic light here\n", count);

        for (int at = 0; at < count && at < 8; ++at)
        {
            AmodRoomTriangle triangle;
            if (Amod::World::GetLightTriangle(at, triangle))
            {
                Amod::Console::Printf("%2d  %+.2f %+.2f %+.2f | %+.2f %+.2f %+.2f | %+.2f %+.2f %+.2f\n", at, triangle.points[0], triangle.points[1], triangle.points[2],
                                      triangle.points[3], triangle.points[4], triangle.points[5], triangle.points[6], triangle.points[7], triangle.points[8]);
            }
        }
        if (count > 8)
        {
            Amod::Console::Printf("   ... and %d more\n", count - 8);
        }
        Amod::Console::Printf("put a 'dlight' on them to see which ones a light lands on\n");
        return;
    }

    if (strcmp(argv[1], "off") == 0)
    {
        AmodRoomTriangle flat = {};
        flat.structSize = sizeof(flat);

        int done = 0;
        for (int at = 0; at < count; ++at)
        {
            done += Amod::World::SetLightTriangle(at, flat) ? 1 : 0;
        }
        Amod::Console::Printf("%d surface(s) flattened; a dlight will light things and not the room\n", done);
        return;
    }

    const int which = Amod::World::PlayerEntity();
    const Amod::World::Entity player = which >= 0 ? Amod::World::CurrentEntity(which) : Amod::World::Entity();
    if (!player.IsValid())
    {
        Amod::Console::Printf("ltri: there is no player to stand anywhere\n");
        return;
    }

    //Two triangles make the patch, laid flat at the player's feet.
    const float x = player.GetX();
    const float y = player.GetY() - AMOD_ROOM_CELL_SIZE * 2.0f;
    const float z = player.GetZ();
    const float half = AMOD_ROOM_CELL_SIZE * 3.0f;

    const float corners[2][9] =
    {
        { x - half, y, z,  x + half, y, z,  x + half, y + half * 2.0f, z },
        { x - half, y, z,  x + half, y + half * 2.0f, z,  x - half, y + half * 2.0f, z },
    };

    for (int at = 0; at < 2; ++at)
    {
        AmodRoomTriangle triangle = {};
        triangle.structSize = sizeof(triangle);
        memcpy(triangle.points, corners[at], sizeof(triangle.points));

        const int put = Amod::World::AddLightTriangle(triangle);
        if (put < 0)
        {
            Amod::Console::Printf("ltri: %s\n", Amod::LastError());
            return;
        }
    }
    Amod::Console::Printf("a patch at your feet takes light now; 'dlight %.2f %.2f 1 0.6 0.2 2' to see it\n", x, player.GetY());
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Amod::Console::AddCommand("light", &LightCommand);
    Amod::Console::AddCommand("dlight", &DynamicLightCommand);
    Amod::Console::AddCommand("elight", &EntityLightCommand);
    Amod::Console::AddCommand("lmap", &LightmapCommand);
    Amod::Console::AddCommand("wall", &WallCommand);
    Amod::Console::AddCommand("deco", &DecoCommand);
    Amod::Console::AddCommand("lseed", &SeedCommand);
    Amod::Console::AddCommand("cell", &CellCommand);
    Amod::Console::AddCommand("box", &BoxCommand);
    Amod::Console::AddCommand("ltri", &LightTriangleCommand);
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame* frame)
{
    if (g_seeding && frame != nullptr)
    {
        g_since += frame->delta;
        if (g_since >= g_every)
        {
            g_since = 0.0f;
            SeedOne();
        }
    }

    const int here = Amod::World::CurrentRoom();

    for (int at = 0; at < g_lightCount; ++at)
    {
        const Burning& light = g_lights[at];

        if (light.room != here)
        {
            continue;
        }

        float x = light.at[0];
        float y = light.at[1];

        if (light.rides >= 0)
        {
            const Amod::World::Entity what = Amod::World::CurrentEntity(light.rides);
            if (!what.IsValid())
            {
                continue;
            }
            x = what.GetX();
            y = what.GetY();
        }

        Amod::World::AddLight(x, y, light.colour[0], light.colour[1], light.colour[2], light.range);
    }
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
