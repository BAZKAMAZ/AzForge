#include <d3d8.h>        // from the DirectX 8 SDK headers in inc_d3d8; include before ForgeApi.h
#include <ForgeApi.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// A console command that creates menu items and spins them:
//   object create <x> <y> <id>   - a $item in the current menu at (x, y), error if the id is taken
//   object angle  <id> <speed>   - spin that item, +speed clockwise, -speed anti-clockwise
//
// The engine draws an item's text and plank straight - it has no angle of its own - so the spin
// is done here, by rotating the world transform around the item's centre for the length of its
// own draw. The real item rotates, not a stand-in.

struct SpinObject
{
    std::string id;
    float speed;        // degrees per second, sign is the direction
    AmodToken token;
};

static std::vector<SpinObject*> g_objects;
static double g_elapsed = 0.0;

static void __cdecl TrackTime(const AmodFrame* frame, void*)
{
    g_elapsed = frame->elapsed;
}

// Fires around the draw of the item: BEFORE sets a rotation, AFTER puts it back.
static void __cdecl SpinItem(AmodMenuItem item, AmodElementPhase phase, void* user)
{
    IDirect3DDevice8* const device = Amod::Render::Device();
    if (device == nullptr)
    {
        return;
    }

    if (phase == AMOD_ELEMENT_BEFORE)
    {
        const SpinObject* const object = static_cast<const SpinObject*>(user);
        Amod::Menu::Element element(item);
        const float cx = element["centre"].Number(0);
        const float cy = element["centre"].Number(1);

        const float a = static_cast<float>(g_elapsed) * object->speed * 0.017453293f;
        const float c = cosf(a), s = sinf(a);

        // Rotate around (cx, cy) in the menu's logical space: T(-c) * Rz * T(c).
        D3DMATRIX world =
        {
            c,                    s,                    0.0f, 0.0f,
            -s,                   c,                    0.0f, 0.0f,
            0.0f,                 0.0f,                 1.0f, 0.0f,
            cx * (1.0f - c) + cy * s, cy * (1.0f - c) - cx * s, 0.0f, 1.0f
        };
        device->SetTransform(D3DTS_WORLD, &world);
    }
    else
    {
        const D3DMATRIX identity =
        {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        device->SetTransform(D3DTS_WORLD, &identity);
    }
}

static void __cdecl ObjectCommand(int argc, const char** argv, void*)
{
    if (argc >= 2 && strcmp(argv[1], "create") == 0)
    {
        if (argc < 5)
        {
            Amod::Console::Print("usage: object create <x> <y> <id>");
            return;
        }
        const float x = static_cast<float>(atof(argv[2]));
        const float y = static_cast<float>(atof(argv[3]));
        const char* const id = argv[4];

        const char* const menu = Amod::Box::Current();
        if (menu[0] == '\0')
        {
            Amod::Console::Print("object: no menu is open");
            return;
        }
        if (Amod::Menu::Find(menu, id).IsValid())
        {
            Amod::Console::Printf("object: id '%s' already exists", id);
            return;
        }

        // $plank_width draws the wooden plank; $font_id "default" (multy_e) has lower-case
        // glyphs, so the id shows whatever its case - the menuitems/bigitems fonts are caps only.
        char fragment[400];
        snprintf(fragment, sizeof(fragment),
                 "$menu_items { $item { $v_align \"center\" $h_align \"center\" "
                 "$id \"%s\" $text \"%s\" $font_id \"default\" $pos %g %g "
                 "$rect_size 200 45 $plank_width 9 } }",
                 id, id, static_cast<double>(x), static_cast<double>(y));
        if (!Amod::Menu::Add(menu, fragment).IsValid())
        {
            Amod::Console::Printf("object: could not create '%s'", id);
            return;
        }
        Amod::Console::Printf("object: created '%s' at %g %g in %s", id,
                              static_cast<double>(x), static_cast<double>(y), menu);
        return;
    }

    if (argc >= 2 && strcmp(argv[1], "angle") == 0)
    {
        if (argc < 4)
        {
            Amod::Console::Print("usage: object angle <id> <speed>");
            return;
        }
        const char* const id = argv[2];
        const float speed = static_cast<float>(atof(argv[3]));

        for (SpinObject* const existing : g_objects)
        {
            if (existing->id == id)
            {
                existing->speed = speed;
                Amod::Console::Printf("object: '%s' now spinning at %g", id, static_cast<double>(speed));
                return;
            }
        }

        SpinObject* const object = new SpinObject{ id, speed, AMOD_INVALID_TOKEN };
        object->token = Amod::Render::OnElementDraw(
            id, AMOD_ELEMENT_BEFORE | AMOD_ELEMENT_AFTER, &SpinItem, object);
        g_objects.push_back(object);
        Amod::Console::Printf("object: '%s' spinning at %g", id, static_cast<double>(speed));
        return;
    }

    Amod::Console::Print("usage: object create <x> <y> <id> | object angle <id> <speed>");
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Amod::Render::OnFrame(&TrackTime);
    Amod::Console::AddCommand("object", &ObjectCommand);
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame* frame)
{
    (void)frame;
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
