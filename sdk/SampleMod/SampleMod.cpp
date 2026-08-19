#include <ForgeApi.h>

#include <stdlib.h>
#include <string.h>

using namespace Amod;

AmodToken g_demoToken = AMOD_INVALID_TOKEN;
AmodToken g_moveToken = AMOD_INVALID_TOKEN;
AmodToken g_inputToken = AMOD_INVALID_TOKEN;

float g_driveJump = 0.0f;

const char* EventName(AmodDemoEvent event)
{
    switch (event)
    {
    case AMOD_DEMO_STARTED: return "started";
    case AMOD_DEMO_ENDED:   return "ended";
    case AMOD_DEMO_BROKEN:  return "broken";
    }
    return "?";
}

void __cdecl DemoHappened(AmodDemoEvent event, int script, void*)
{
    Console::Printf("demo: script %d %s\n", script, EventName(event));
}

void __cdecl MoveReached(int script, int move, void*)
{
    AmodDemoMove step = {};

    if (!Demo::GetMove(script, move, &step))
    {
        Console::Printf("demo: script %d move %d\n", script, move);
        return;
    }

    if (step.pause > 0.0f)
    {
        Console::Printf("demo: %d.%d pause %.2f\n", script, move, step.pause);
        return;
    }

    Console::Printf("demo: %d.%d horz %d vert %d jump %d to %.2f\n", script, move,
        step.horizontal, step.vertical, step.jump, step.endCoord);
}

// Walks right and jumps twice a second, whatever the script was going to do.
int __cdecl DriveInstead(const float position[3], int* horizontal, int* vertical, int* jump, float seconds, void*)
{
    (void)position;
    (void)vertical;

    g_driveJump += seconds;

    *horizontal = 1;
    *jump = 0;

    if (g_driveJump >= 0.5f)
    {
        g_driveJump = 0.0f;
        *jump = 1;
    }

    return 1;
}

void ListScripts()
{
    const int count = Demo::Count();
    const int current = Demo::Current();

    if (count == 0)
    {
        Console::Print("demo: this level has no demo scripts\n");
        return;
    }

    Console::Printf("demo: %d script(s), %s\n", count,
        Demo::IsPlaying() ? "playing" : "idle");

    for (int i = 0; i < count; ++i)
    {
        AmodDemoPlace place = {};
        char where[64] = {};

        if (Demo::GetPlace(i, &place))
        {
            snprintf(where, sizeof(where), "player %d,%d room %d,%d", place.playerX, place.playerY,
                place.roomX, place.roomY);
        }
        else
        {
            snprintf(where, sizeof(where), "no $player_place");
        }

        Console::Printf("  %s %d  %d move(s)  break %.2f  %s\n", i == current ? "->" : "  ", i,
            Demo::MoveCount(i), Demo::BreakTime(i), where);
    }

    if (current >= 0)
    {
        Console::Printf("demo: on move %d of script %d, %.2fs in\n", Demo::MoveIndex(), current,
            Demo::Elapsed());
    }
}

void DumpScript(int script)
{
    const int moves = Demo::MoveCount(script);
    float position[3] = {};
    float view[3] = {};

    if (moves == 0)
    {
        Console::Printf("demo: there is no script %d\n", script);
        return;
    }

    if (Demo::GetCameraStart(script, position, view))
    {
        Console::Printf("demo: script %d starts the camera at %.2f %.2f %.2f looking %.2f %.2f %.2f\n",
            script, position[0], position[1], position[2], view[0], view[1], view[2]);
    }
    else
    {
        Console::Printf("demo: script %d has no $camera_start\n", script);
    }

    for (int i = 0; i < moves; ++i)
    {
        AmodDemoMove step = {};

        if (!Demo::GetMove(script, i, &step))
        {
            continue;
        }

        if (step.pause > 0.0f)
        {
            Console::Printf("  %2d  pause %.2f\n", i, step.pause);
        }
        else
        {
            Console::Printf("  %2d  horz %d vert %d jump %d to %.2f\n", i, step.horizontal,
                step.vertical, step.jump, step.endCoord);
        }

        for (int c = 0; c < step.camCount; ++c)
        {
            AmodDemoCamera camera = {};
            if (Demo::GetCamera(script, i, c, &camera))
            {
                Console::Printf("        cam  %.2f %.2f %.2f  %.2fs%s\n", camera.position[0],
                    camera.position[1], camera.position[2], camera.time,
                    camera.giveBack != 0 ? "  auto" : "");
            }
        }

        for (int v = 0; v < step.viewCount; ++v)
        {
            AmodDemoCamera camera = {};
            if (Demo::GetView(script, i, v, &camera))
            {
                Console::Printf("        view %.2f %.2f %.2f  %.2fs%s\n", camera.position[0],
                    camera.position[1], camera.position[2], camera.time,
                    camera.giveBack != 0 ? "  auto" : "");
            }
        }

        for (int t = 0; t < step.timeCount; ++t)
        {
            AmodDemoTime slice = {};
            if (Demo::GetTime(script, i, t, &slice))
            {
                Console::Printf("        time pause %.2f factor %.2f for %.2fs\n", slice.pause,
                    slice.factor, slice.time);
            }
        }
    }
}

void SetField(int argc, const char** argv)
{
    if (argc < 6)
    {
        Console::Print("demo set <script> <move> pause|horz|vert|jump|end <value>\n");
        return;
    }

    const int script = atoi(argv[2]);
    const int move = atoi(argv[3]);
    const char* const field = argv[4];
    const float value = static_cast<float>(atof(argv[5]));

    AmodDemoMove step = {};
    if (!Demo::GetMove(script, move, &step))
    {
        Console::Printf("demo: script %d has no move %d\n", script, move);
        return;
    }

    if (strcmp(field, "pause") == 0) { step.pause = value; }
    else if (strcmp(field, "horz") == 0) { step.horizontal = static_cast<int>(value); }
    else if (strcmp(field, "vert") == 0) { step.vertical = static_cast<int>(value); }
    else if (strcmp(field, "jump") == 0) { step.jump = static_cast<int>(value); }
    else if (strcmp(field, "end") == 0) { step.endCoord = value; }
    else
    {
        Console::Print("demo set: pause, horz, vert, jump or end\n");
        return;
    }

    if (Demo::SetMove(script, move, step))
    {
        Console::Printf("demo: %d.%d %s is now %.2f\n", script, move, field, value);
    }
    else
    {
        Console::Printf("demo: %d.%d %s was refused\n", script, move, field);
    }
}

void SetCameraCommand(int argc, const char** argv, bool view)
{
    if (argc < 8)
    {
        Console::Printf("demo %s <script> <move> <index> <x> <y> <z> [time] [auto]\n",
            view ? "view" : "cam");
        return;
    }

    const int script = atoi(argv[2]);
    const int move = atoi(argv[3]);
    const int index = atoi(argv[4]);

    AmodDemoCamera camera = {};
    const bool read = view ? Demo::GetView(script, move, index, &camera)
                           : Demo::GetCamera(script, move, index, &camera);

    if (!read)
    {
        AmodDemoMove step = {};
        Demo::GetMove(script, move, &step);

        Console::Printf("demo: %d.%d holds %d cam and %d view - 'demo add%s' makes one\n", script,
            move, step.camCount, step.viewCount, view ? "view" : "cam");
        return;
    }

    camera.position[0] = static_cast<float>(atof(argv[5]));
    camera.position[1] = static_cast<float>(atof(argv[6]));
    camera.position[2] = static_cast<float>(atof(argv[7]));

    if (argc > 8)
    {
        camera.time = static_cast<float>(atof(argv[8]));
    }
    if (argc > 9)
    {
        camera.giveBack = atoi(argv[9]);
    }

    const bool written = view ? Demo::SetView(script, move, index, camera)
                              : Demo::SetCamera(script, move, index, camera);

    Console::Printf("demo: %s %d.%d.%d %s\n", view ? "view" : "cam", script, move, index,
        written ? "written" : "refused");
}

void SetTimeCommand(int argc, const char** argv)
{
    if (argc < 8)
    {
        Console::Print("demo time <script> <move> <index> <pause> <factor> <seconds>\n");
        return;
    }

    AmodDemoTime slice = {};
    slice.structSize = sizeof(slice);
    slice.pause = static_cast<float>(atof(argv[5]));
    slice.factor = static_cast<float>(atof(argv[6]));
    slice.time = static_cast<float>(atof(argv[7]));

    const int script = atoi(argv[2]);
    const int move = atoi(argv[3]);
    const int index = atoi(argv[4]);

    Console::Printf("demo: time %d.%d.%d %s\n", script, move, index,
        Demo::SetTime(script, move, index, slice) ? "written" : "refused");
}

void StartCommand(int argc, const char** argv)
{
    if (argc < 9)
    {
        Console::Print("demo start <script> <x> <y> <z> <vx> <vy> <vz>\n");
        return;
    }

    float position[3] = {};
    float view[3] = {};

    position[0] = static_cast<float>(atof(argv[3]));
    position[1] = static_cast<float>(atof(argv[4]));
    position[2] = static_cast<float>(atof(argv[5]));
    view[0] = static_cast<float>(atof(argv[6]));
    view[1] = static_cast<float>(atof(argv[7]));
    view[2] = static_cast<float>(atof(argv[8]));

    Console::Printf("demo: camera start %s\n",
        Demo::SetCameraStart(atoi(argv[2]), position, view) ? "written" : "refused");
}

void MakeScript()
{
    AmodPlayer player = {};

    if (!Player::Get(player))
    {
        Console::Print("demo: there is no player to build a demo around\n");
        return;
    }

    const int script = Demo::Create();

    if (script < 0)
    {
        Console::Print("demo: an empty script was refused\n");
        return;
    }

    AmodDemoMove step = {};
    step.structSize = sizeof(step);
    step.pause = 2.0f;

    const int wait = Demo::AddMove(script, step);

    AmodDemoCamera camera = {};
    camera.structSize = sizeof(camera);
    camera.position[0] = player.position[0];
    camera.position[1] = player.position[1] + 0.4f;
    camera.position[2] = player.position[2] + 1.5f;
    camera.time = 2.0f;
    camera.giveBack = 1;

    AmodDemoCamera look = camera;
    look.position[0] = player.position[0];
    look.position[1] = player.position[1];
    look.position[2] = player.position[2];

    AmodDemoTime slice = {};
    slice.structSize = sizeof(slice);
    slice.factor = 0.5f;
    slice.time = 2.0f;

    Demo::AddCamera(script, wait, camera);
    Demo::AddView(script, wait, look);
    Demo::AddTime(script, wait, slice);

    // Then a third of a room to the right and back again, with a jump on the way
    step.pause = 0.0f;
    step.horizontal = 1;
    step.endCoord = player.position[0] + 0.3f;
    Demo::AddMove(script, step);

    step.horizontal = -1;
    step.jump = 1;
    step.endCoord = player.position[0];
    Demo::AddMove(script, step);

    Console::Printf("demo: script %d built, %d move(s) - 'demo play %d' runs it\n", script,
        Demo::MoveCount(script), script);
}

void AddMoveCommand(int argc, const char** argv)
{
    if (argc < 4)
    {
        Console::Print("demo addmove <script> <pause> [horz] [vert] [jump] [end]\n");
        return;
    }

    AmodDemoMove step = {};
    step.structSize = sizeof(step);
    step.pause = static_cast<float>(atof(argv[3]));
    step.horizontal = argc > 4 ? atoi(argv[4]) : 0;
    step.vertical = argc > 5 ? atoi(argv[5]) : 0;
    step.jump = argc > 6 ? atoi(argv[6]) : 0;
    step.endCoord = argc > 7 ? static_cast<float>(atof(argv[7])) : 0.0f;

    const int script = atoi(argv[2]);
    const int added = Demo::AddMove(script, step);

    Console::Printf("demo: move %d %s, script %d now holds %d\n", added,
        added >= 0 ? "added" : "refused", script, Demo::MoveCount(script));
}

void AddCameraCommand(int argc, const char** argv, bool view)
{
    if (argc < 7)
    {
        Console::Printf("demo add%s <script> <move> <x> <y> <z> [time] [auto]\n",
            view ? "view" : "cam");
        return;
    }

    AmodDemoCamera camera = {};
    camera.structSize = sizeof(camera);
    camera.position[0] = static_cast<float>(atof(argv[4]));
    camera.position[1] = static_cast<float>(atof(argv[5]));
    camera.position[2] = static_cast<float>(atof(argv[6]));
    camera.time = argc > 7 ? static_cast<float>(atof(argv[7])) : 1.0f;
    camera.giveBack = argc > 8 ? atoi(argv[8]) : 0;

    const int script = atoi(argv[2]);
    const int move = atoi(argv[3]);
    const int added = view ? Demo::AddView(script, move, camera)
                           : Demo::AddCamera(script, move, camera);

    AmodDemoMove step = {};
    Demo::GetMove(script, move, &step);

    Console::Printf("demo: %s %d %s, %d.%d now holds %d cam, %d view, %d time\n",
        view ? "view" : "cam", added, added >= 0 ? "added" : "refused", script, move,
        step.camCount, step.viewCount, step.timeCount);
}

void AddTimeCommand(int argc, const char** argv)
{
    if (argc < 7)
    {
        Console::Print("demo addtime <script> <move> <pause> <factor> <seconds>\n");
        return;
    }

    AmodDemoTime slice = {};
    slice.structSize = sizeof(slice);
    slice.pause = static_cast<float>(atof(argv[4]));
    slice.factor = static_cast<float>(atof(argv[5]));
    slice.time = static_cast<float>(atof(argv[6]));

    const int script = atoi(argv[2]);
    const int move = atoi(argv[3]);
    const int added = Demo::AddTime(script, move, slice);

    AmodDemoMove step = {};
    Demo::GetMove(script, move, &step);

    Console::Printf("demo: time %d %s, %d.%d now holds %d\n", added,
        added >= 0 ? "added" : "refused", script, move, step.timeCount);
}

void WatchCommand()
{
    if (g_demoToken != AMOD_INVALID_TOKEN)
    {
        Demo::Cancel(g_demoToken);
        Demo::Cancel(g_moveToken);
        g_demoToken = AMOD_INVALID_TOKEN;
        g_moveToken = AMOD_INVALID_TOKEN;
        Console::Print("demo: not watching\n");
        return;
    }

    g_demoToken = Demo::OnDemo(&DemoHappened);
    g_moveToken = Demo::OnMove(&MoveReached);

    Console::Print("demo: watching starts, ends and every move\n");
}

void DriveCommand()
{
    if (g_inputToken != AMOD_INVALID_TOKEN)
    {
        Demo::Cancel(g_inputToken);
        g_inputToken = AMOD_INVALID_TOKEN;
        Console::Print("demo: the script drives again\n");
        return;
    }

    g_driveJump = 0.0f;
    g_inputToken = Demo::OnInput(&DriveInstead);

    Console::Print("demo: the mod drives - right and a jump every half second\n");
}

void __cdecl DemoCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        ListScripts();
        return;
    }

    const char* const what = argv[1];

    if (strcmp(what, "play") == 0 && argc > 2)
    {
        const int script = atoi(argv[2]);
        Console::Printf("demo: script %d %s\n", script, Demo::Play(script) ? "playing" : "refused");
    }
    else if (strcmp(what, "stop") == 0)
    {
        Console::Printf("demo: %s\n", Demo::Stop() ? "stopped" : "nothing was playing");
    }
    else if (strcmp(what, "next") == 0)
    {
        Console::Printf("demo: %s\n", Demo::Next() ? "on the next script" : "that was the last");
    }
    else if (strcmp(what, "add") == 0 && argc > 2)
    {
        const int added = Demo::Add(argv[2]);
        Console::Printf("demo: '%s' %s\n", argv[2], added >= 0 ? "added" : "refused");
    }
    else if (strcmp(what, "remove") == 0 && argc > 2)
    {
        const int script = atoi(argv[2]);
        Console::Printf("demo: script %d %s\n", script,
            Demo::Remove(script) ? "removed" : "refused");
    }
    else if (strcmp(what, "suppress") == 0)
    {
        const int on = argc > 2 ? atoi(argv[2]) : 1;
        Demo::SuppressLevelDemos(on != 0);
        Console::Printf("demo: the level's own scripts are %s from the next level\n",
            on != 0 ? "dropped" : "kept");
    }
    else if (strcmp(what, "break") == 0 && argc > 3)
    {
        const int script = atoi(argv[2]);
        const float seconds = static_cast<float>(atof(argv[3]));
        Console::Printf("demo: break time %s\n",
            Demo::SetBreakTime(script, seconds) ? "written" : "refused");
    }
    else if (strcmp(what, "set") == 0)
    {
        SetField(argc, argv);
    }
    else if (strcmp(what, "cam") == 0)
    {
        SetCameraCommand(argc, argv, false);
    }
    else if (strcmp(what, "view") == 0)
    {
        SetCameraCommand(argc, argv, true);
    }
    else if (strcmp(what, "time") == 0)
    {
        SetTimeCommand(argc, argv);
    }
    else if (strcmp(what, "make") == 0)
    {
        MakeScript();
    }
    else if (strcmp(what, "addmove") == 0)
    {
        AddMoveCommand(argc, argv);
    }
    else if (strcmp(what, "addcam") == 0)
    {
        AddCameraCommand(argc, argv, false);
    }
    else if (strcmp(what, "addview") == 0)
    {
        AddCameraCommand(argc, argv, true);
    }
    else if (strcmp(what, "addtime") == 0)
    {
        AddTimeCommand(argc, argv);
    }
    else if (strcmp(what, "start") == 0)
    {
        StartCommand(argc, argv);
    }
    else if (strcmp(what, "watch") == 0)
    {
        WatchCommand();
    }
    else if (strcmp(what, "drive") == 0)
    {
        DriveCommand();
    }
    else if (argv[1][0] >= '0' && argv[1][0] <= '9')
    {
        DumpScript(atoi(argv[1]));
    }
    else
    {
        Console::Print("demo, demo <n>, 'demo play|remove <n>', 'demo stop|next|watch|drive',\n");
        Console::Print("      'demo add <path>', 'demo suppress [0|1]', 'demo break <n> <secs>',\n");
        Console::Print("      'demo set <n> <move> pause|horz|vert|jump|end <value>',\n");
        Console::Print("      'demo cam|view <n> <move> <i> <x> <y> <z> [time] [auto]',\n");
        Console::Print("      'demo time <n> <move> <i> <pause> <factor> <secs>',\n");
        Console::Print("      'demo start <n> <x> <y> <z> <vx> <vy> <vz>', 'demo make',\n");
        Console::Print("      'demo addmove <n> <pause> [horz] [vert] [jump] [end]',\n");
        Console::Print("      'demo addcam|addview <n> <move> <x> <y> <z> [time] [auto]',\n");
        Console::Print("      'demo addtime <n> <move> <pause> <factor> <secs>'\n");
    }
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("demo", &DemoCommand);
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
