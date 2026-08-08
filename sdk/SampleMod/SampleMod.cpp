#include <ForgeApi.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

using namespace Amod;

enum class Drives
{
    None,
    Orbit,
    Chase
};

static Drives g_drives = Drives::None;
static float g_angle = 0.0f;

enum class Views
{
    None,
    Flip,
    Shoulder
};

static Views g_view = Views::None;
static bool g_holdsPlace = false;

static int __cdecl OnCameraEvent(const AmodCameraEventInfo* info, void*)
{
    Console::Printf("the game wants the camera at %+.3f %+.3f %+.3f%s\n",
        info->point[0], info->point[1], info->point[2], g_holdsPlace ? " - refused" : "");

    return g_holdsPlace ? 1 : 0;
}

static void Say(bool done, const char* said)
{
    Console::Printf(done ? "%s\n" : "cam: %s\n", done ? said : LastError());
}

static void ReadTriple(int argc, const char** argv, int from, float* out)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        out[axis] = from + axis < argc ? static_cast<float>(atof(argv[from + axis])) : 0.0f;
    }
}

static void __cdecl OnCamera(const AmodCameraFrame* frame, void*)
{
    if (g_drives == Drives::None)
    {
        return;
    }

    float focus[3] = { frame->followed[0], frame->followed[1], frame->followed[2] };
    Camera::GetFocus(focus);

    if (g_drives == Drives::Orbit)
    {
        g_angle = g_angle + frame->delta;

        const float radius = 3.0f;
        const float eye[3] = { focus[0] + sinf(g_angle) * radius, focus[1] + 1.0f, focus[2] + cosf(g_angle) * radius };

        Camera::SetEye(eye);
        Camera::SetTarget(focus);
        return;
    }

    // Chase: hangs back and catches up by hand, so the lag is the mod's, not $lag_dist
    AmodCamera now;
    if (!Camera::Get(now))
    {
        return;
    }

    const float wanted[3] = { focus[0], focus[1] + 0.5f, focus[2] + 4.0f };
    float eye[3];

    for (int axis = 0; axis < 3; ++axis)
    {
        eye[axis] = now.eye[axis] + (wanted[axis] - now.eye[axis]) * frame->delta * 2.0f;
    }

    Camera::SetEye(eye);
    Camera::SetTarget(focus);
}

// The last word on what is drawn, after whatever moved the camera
static int __cdecl OnView(AmodCameraView* view, void*)
{
    if (g_view == Views::Flip)
    {
        view->up[0] = -view->up[0];
        view->up[1] = -view->up[1];
        view->up[2] = -view->up[2];
        return 1;
    }

    if (g_view == Views::Shoulder)
    {
        view->eye[0] = view->eye[0] + 1.5f;
        return 1;
    }

    return 0;
}

static void ShowState()
{
    AmodCamera camera;
    if (!Camera::Get(camera))
    {
        Console::Printf("cam: %s\n", LastError());
        return;
    }

    static const char* const kEffects[] = { "none", "zoom", "rocking", "rotate" };
    static const char* const kDrivers[] = { "game", "mod", "both", "nobody" };

    Console::Printf("eye   %+.3f %+.3f %+.3f\n", camera.eye[0], camera.eye[1], camera.eye[2]);
    Console::Printf("at    %+.3f %+.3f %+.3f\n", camera.target[0], camera.target[1], camera.target[2]);
    Console::Printf("up    %+.3f %+.3f %+.3f  roll %+.3f\n", camera.up[0], camera.up[1], camera.up[2], camera.roll);
    Console::Printf("holds %+.3f %+.3f %+.3f\n", camera.followed[0], camera.followed[1], camera.followed[2]);
    Console::Printf("fx %s%s, follows %s%s, driven by %s\n",
        kEffects[camera.effect >= 0 && camera.effect <= 3 ? camera.effect : 0],
        camera.isShaking ? " + shake" : "",
        camera.followsEye ? "eye " : "",
        camera.followsTarget ? "target" : "",
        kDrivers[Camera::GetDriver()]);

    float focus[3];
    if (Camera::GetFocus(focus))
    {
        Console::Printf("focus %+.3f %+.3f %+.3f\n", focus[0], focus[1], focus[2]);
    }

    AmodCameraView view;
    if (Camera::GetView(view))
    {
        Console::Printf("drawn from %+.3f %+.3f %+.3f\n", view.eye[0], view.eye[1], view.eye[2]);
    }

    AmodCameraLens lens;
    if (Camera::GetLens(lens))
    {
        Console::Printf("lens fov %.1f deg, aspect %.3f, planes %.3f .. %.1f\n",
            lens.fov * 180.0f / 3.1415927f, lens.aspect, lens.nearPlane, lens.farPlane);
    }

    const int free = Camera::GetFreeMode();
    if (free != 0)
    {
        Console::Printf("cam_free is %d, so nothing here moves the camera\n", free);
    }
}

static void ShowProperties()
{
    const int total = Camera::PropertyCount();
    Console::Printf("%d camera setting(s)\n", total);

    for (int at = 0; at < total; ++at)
    {
        const char* const key = Camera::PropertyNameAt(at);
        AmodValue value = {};

        if (!Camera::GetProperty(key, value))
        {
            continue;
        }
        if (value.kind == AMOD_VALUE_INT)
        {
            Console::Printf("  %-14s %d\n", key, value.integer);
        }
        else
        {
            Console::Printf("  %-14s %.3f\n", key, value.numbers[0]);
        }
    }
}

static bool TakeDriver(Drives drives)
{
    g_drives = drives;
    g_angle = 0.0f;

    if (drives == Drives::None)
    {
        Camera::SetUpdateHandler(nullptr);
        return Camera::SetDriver(AMOD_CAMERA_DRIVER_GAME);
    }

    return Camera::SetUpdateHandler(&OnCamera) && Camera::SetDriver(AMOD_CAMERA_DRIVER_MOD);
}

static void __cdecl CamCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        ShowState();
        return;
    }

    const char* const what = argv[1];

    if (strcmp(what, "props") == 0)
    {
        ShowProperties();
        return;
    }

    if (strcmp(what, "reload") == 0)
    {
        Say(Camera::Reload(), "camera.txt read again");
        return;
    }

    if (strcmp(what, "here") == 0)
    {
        float focus[3];
        Say(Camera::GetFocus(focus) && Camera::Place(focus), "put where the player is");
        return;
    }

    if (strcmp(what, "eye") == 0 && argc > 4)
    {
        float point[3];
        ReadTriple(argc, argv, 2, point);
        Say(Camera::SetEye(point), "eye moved");
        return;
    }

    if (strcmp(what, "at") == 0 && argc > 4)
    {
        float point[3];
        ReadTriple(argc, argv, 2, point);
        Say(Camera::SetTarget(point), "looking there");
        return;
    }

    if (strcmp(what, "up") == 0 && argc > 4)
    {
        float point[3];
        ReadTriple(argc, argv, 2, point);
        Say(Camera::SetUp(point), "up is that way");
        return;
    }

    if (strcmp(what, "roll") == 0 && argc > 2)
    {
        Say(Camera::SetRoll(static_cast<float>(atof(argv[2]))), "rolled");
        return;
    }

    if (strcmp(what, "follow") == 0 && argc > 4)
    {
        float point[3];
        ReadTriple(argc, argv, 2, point);
        Say(Camera::Follow(point), "catching up to it");
        return;
    }

    if (strcmp(what, "place") == 0 && argc > 4)
    {
        float point[3];
        ReadTriple(argc, argv, 2, point);
        Say(Camera::Place(point), "put there");
        return;
    }

    if (strcmp(what, "lock") == 0 && argc > 3)
    {
        const bool follows = atoi(argv[3]) != 0;

        if (strcmp(argv[2], "eye") == 0)
        {
            Say(Camera::SetFollowEye(follows), follows ? "the eye follows" : "the eye stands");
            return;
        }
        if (strcmp(argv[2], "at") == 0)
        {
            Say(Camera::SetFollowTarget(follows), follows ? "the target follows" : "the target stands");
            return;
        }

        Console::Printf("cam lock eye|at 0|1\n");
        return;
    }

    if (strcmp(what, "fx") == 0 && argc > 2)
    {
        if (strcmp(argv[2], "off") == 0)
        {
            Say(Camera::StopEffect(), "effect off");
            return;
        }

        const int effect = strcmp(argv[2], "zoom") == 0 ? AMOD_CAMERA_EFFECT_ZOOM :
            strcmp(argv[2], "rock") == 0 ? AMOD_CAMERA_EFFECT_ROCKING :
            strcmp(argv[2], "rotate") == 0 ? AMOD_CAMERA_EFFECT_ROTATE : 0;

        if (effect == 0)
        {
            Console::Printf("cam fx zoom|rock|rotate|off\n");
            return;
        }

        Say(Camera::StartEffect(effect), argv[2]);
        return;
    }

    if (strcmp(what, "shake") == 0 && argc > 2)
    {
        const bool on = strcmp(argv[2], "on") == 0;
        Say(on ? Camera::StartShake() : Camera::StopShake(), on ? "shaking" : "still");
        return;
    }

    if (strcmp(what, "drive") == 0 && argc > 2)
    {
        if (strcmp(argv[2], "orbit") == 0)
        {
            Say(TakeDriver(Drives::Orbit), "orbiting");
            return;
        }
        if (strcmp(argv[2], "chase") == 0)
        {
            Say(TakeDriver(Drives::Chase), "chasing");
            return;
        }
        if (strcmp(argv[2], "game") == 0)
        {
            Say(TakeDriver(Drives::None), "the game has it back");
            return;
        }
        if (strcmp(argv[2], "both") == 0)
        {
            g_drives = Drives::Orbit;
            Say(Camera::SetUpdateHandler(&OnCamera) && Camera::SetDriver(AMOD_CAMERA_DRIVER_BOTH), "both");
            return;
        }
        if (strcmp(argv[2], "nobody") == 0)
        {
            Say(Camera::SetDriver(AMOD_CAMERA_DRIVER_NOBODY), "frozen where it stands");
            return;
        }

        Console::Printf("cam drive orbit|chase|both|nobody|game\n");
        return;
    }

    if (strcmp(what, "lens") == 0 && argc > 2)
    {
        if (strcmp(argv[2], "off") == 0)
        {
            Say(Camera::ClearLens(), "the lens is the game's");
            return;
        }
        if (argc < 4)
        {
            Console::Printf("cam lens fov|near|far <number>, or off\n");
            return;
        }

        const float number = static_cast<float>(atof(argv[3]));
        AmodCameraLens lens = {};
        lens.structSize = sizeof(lens);

        if (strcmp(argv[2], "fov") == 0)
        {
            lens.fov = number * 3.1415927f / 180.0f;
        }
        else if (strcmp(argv[2], "near") == 0)
        {
            lens.nearPlane = number;
        }
        else if (strcmp(argv[2], "far") == 0)
        {
            lens.farPlane = number;
        }
        else
        {
            Console::Printf("cam lens fov|near|far <number>, or off\n");
            return;
        }

        Say(Camera::SetLens(lens), argv[2]);
        return;
    }

    if (strcmp(what, "snap") == 0 && argc > 2)
    {
        g_holdsPlace = strcmp(argv[2], "refuse") == 0;
        Say(Camera::SetEventHandler(&OnCameraEvent),
            g_holdsPlace ? "the snap will be refused" : "the snap is only reported");
        return;
    }

    if (strcmp(what, "view") == 0 && argc > 2)
    {
        if (strcmp(argv[2], "off") == 0)
        {
            g_view = Views::None;
            Say(Camera::SetViewHandler(nullptr), "the view is the game's");
            return;
        }

        g_view = strcmp(argv[2], "flip") == 0 ? Views::Flip :
            strcmp(argv[2], "shoulder") == 0 ? Views::Shoulder : Views::None;

        if (g_view == Views::None)
        {
            Console::Printf("cam view flip|shoulder|off\n");
            return;
        }

        Say(Camera::SetViewHandler(&OnView), argv[2]);
        return;
    }

    if (argc < 3)
    {
        Console::Printf("cam [props|reload|here|eye|at|up|roll|follow|place|lock|fx|shake|drive|view|lens|snap]\n");
        Console::Printf("cam <setting> <number>\n");
        return;
    }

    // An int setting refuses a float value, so the kind it reads back as is the kind it is sent
    AmodValue was = {};
    const bool known = Camera::GetProperty(what, was);
    const AmodValue value = known && was.kind == AMOD_VALUE_INT
        ? Value(atoi(argv[2]))
        : Value(static_cast<float>(atof(argv[2])));

    Say(known && Camera::SetProperty(what, value), what);
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("cam", &CamCommand);
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
