#include <ForgeApi.h>
#include <stdlib.h>
#include <string.h>

static const char* StateName(int state)
{
    switch (state)
    {
    case AMOD_PLAYER_STATE_ONFOOT: return "on foot";
    case AMOD_PLAYER_STATE_LADDER: return "ladder";
    case AMOD_PLAYER_STATE_CHAIN: return "chain";
    case AMOD_PLAYER_STATE_PIPE: return "pipe";
    }
    return "?";
}

static const char* SubName(int sub)
{
    switch (sub)
    {
    case AMOD_PLAYER_SUB_STILL: return "still";
    case AMOD_PLAYER_SUB_FALLING: return "falling";
    case AMOD_PLAYER_SUB_WALK_LEFT: return "walk left";
    case AMOD_PLAYER_SUB_WALK_RIGHT: return "walk right";
    case AMOD_PLAYER_SUB_CLIMB_UP: return "climb up";
    case AMOD_PLAYER_SUB_CLIMB_DOWN: return "climb down";
    case AMOD_PLAYER_SUB_DROPPED: return "dropped";
    }
    return "?";
}

// pl | pl move <x> <y> <z> | pl face <a> | pl kill [crash] | pl live
static void __cdecl PlayerCommand(int argc, const char** argv, void*)
{
    AmodPlayer player;
    if (!Amod::Player::Get(player))
    {
        Amod::Console::Printf("pl: %s\n", Amod::LastError());
        return;
    }

    if (argc < 2)
    {
        Amod::Console::Printf("entity %d  %+.3f %+.3f %+.3f  face %.2f  fall %.3f\n",
            player.entity, player.position[0], player.position[1], player.position[2],
            player.facing, player.fallSpeed);
        Amod::Console::Printf("%s / %s%s%s%s%s\n",
            StateName(player.state), SubName(player.subState),
            player.isDead ? " dead" : "",
            player.isCrashed ? " crashed" : "",
            player.isFading ? " fading" : "",
            player.isBurning ? " burning" : "");
        return;
    }

    if (strcmp(argv[1], "move") == 0 && argc > 4)
    {
        const float x = static_cast<float>(atof(argv[2]));
        const float y = static_cast<float>(atof(argv[3]));
        const float z = static_cast<float>(atof(argv[4]));
        if (!Amod::Player::SetPosition(x, y, z))
        {
            Amod::Console::Printf("pl: %s\n", Amod::LastError());
            return;
        }
        Amod::Console::Printf("moved to %+.3f %+.3f %+.3f\n", x, y, z);
        return;
    }

    if (strcmp(argv[1], "face") == 0 && argc > 2)
    {
        const float facing = static_cast<float>(atof(argv[2]));
        Amod::Console::Printf(Amod::Player::SetFacing(facing) ? "facing %.2f\n" : "pl: refused\n", facing);
        return;
    }

    if (strcmp(argv[1], "kill") == 0)
    {
        const bool crashed = argc > 2 && strcmp(argv[2], "crash") == 0;
        if (!Amod::Player::Kill(crashed))
        {
            Amod::Console::Printf("pl: %s\n", Amod::LastError());
            return;
        }
        Amod::Console::Printf("dead%s\n", crashed ? " (crashed)" : "");
        return;
    }

    if (strcmp(argv[1], "live") == 0)
    {
        if (!Amod::Player::Revive())
        {
            Amod::Console::Printf("pl: %s\n", Amod::LastError());
            return;
        }
        Amod::Console::Printf("alive\n");
        return;
    }

    Amod::Console::Printf("pl [move <x> <y> <z> | face <a> | kill [crash] | live]\n");
}

static bool g_watching = false;
static int g_wasState = -1;
static int g_wasSub = -1;
static int g_wasDead = -1;

// plwatch -- toggle state/death watch
static void __cdecl WatchCommand(int, const char**, void*)
{
    g_watching = !g_watching;
    g_wasState = -1;
    g_wasSub = -1;
    g_wasDead = -1;
    Amod::Console::Printf(g_watching ? "watching\n" : "not watching\n");
}

// life [n]
static void __cdecl LifeCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Amod::Console::Printf("%d live(s)\n", Amod::Player::Lives());
        return;
    }
    const int lives = atoi(argv[1]);
    Amod::Console::Printf(Amod::Player::SetLives(lives) ? "%d live(s)\n" : "life: %s\n", lives);
}

// score [add <n> | set <n>]  -- add uses Score_Add; set is raw write
static void __cdecl ScoreCommand(int argc, const char** argv, void*)
{
    if (argc < 3)
    {
        Amod::Console::Printf("%d\n", Amod::Player::Score());
        return;
    }

    const int points = atoi(argv[2]);
    if (strcmp(argv[1], "add") == 0)
    {
        if (!Amod::Player::AddScore(points))
        {
            Amod::Console::Printf("score: %s\n", Amod::LastError());
            return;
        }
        Amod::Console::Printf("%+d\n", points);
        return;
    }

    if (strcmp(argv[1], "set") == 0)
    {
        if (!Amod::Player::SetScore(points))
        {
            Amod::Console::Printf("score: %s\n", Amod::LastError());
            return;
        }
        Amod::Console::Printf("%d\n", points);
        return;
    }
    Amod::Console::Printf("score [add <n> | set <n>]\n");
}

// time [seconds]  -- stopwatch
static void __cdecl TimeCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Amod::Console::Printf("%.2f\n", Amod::World::Elapsed());
        return;
    }
    const float seconds = static_cast<float>(atof(argv[1]));
    Amod::Console::Printf(Amod::World::SetElapsed(seconds) ? "%.2f\n" : "time: refused\n", seconds);
}

// btime [seconds]  -- bonus countdown
static void __cdecl BonusTimeCommand(int argc, const char** argv, void*)
{
    AmodWorldLevel level;
    if (!Amod::World::GetLevel(level) || level.isBonus == 0)
    {
        Amod::Console::Printf("not a bonus level\n");
        return;
    }
    if (argc < 2)
    {
        Amod::Console::Printf("%.2f left\n", Amod::World::TimeLeft());
        return;
    }
    const float seconds = static_cast<float>(atof(argv[1]));
    Amod::Console::Printf(Amod::World::SetTimeLeft(seconds) ? "%.2f left\n" : "btime: %s\n", seconds);
}

static void __cdecl OnWorldEvent(const AmodWorldEventInfo* info, void*)
{
    if (info->event == AMOD_WORLD_EVENT_TIME_UP)
    {
        Amod::Console::Printf("* time up\n");
    }
}

static void __cdecl OnPlayerEvent(const AmodPlayerEventInfo* info, void*)
{
    switch (info->event)
    {
    case AMOD_PLAYER_EVENT_DIED:
        Amod::Console::Printf("* died%s, %d live(s)\n", info->crashed ? " (fall)" : "", info->lives);
        break;
    case AMOD_PLAYER_EVENT_REVIVED:
        Amod::Console::Printf("* revived\n");
        break;
    case AMOD_PLAYER_EVENT_LIVES_CHANGED:
        Amod::Console::Printf("* lives %d -> %d\n", info->wasLives, info->lives);
        break;
    case AMOD_PLAYER_EVENT_SCORE_CHANGED:
        Amod::Console::Printf("* score %d -> %d\n", info->wasScore, info->score);
        break;
    }
}

static const char* DriverName(AmodPlayerDriver driver)
{
    switch (driver)
    {
    case AMOD_PLAYER_DRIVER_GAME: return "game";
    case AMOD_PLAYER_DRIVER_MOD: return "mod";
    case AMOD_PLAYER_DRIVER_BOTH: return "both";
    case AMOD_PLAYER_DRIVER_NOBODY: return "nobody";
    }
    return "?";
}

static int g_wasSide = 0;
static const char* g_playing = "";

// Frameset for current state; null on idle climb (sub-state keeps last dir).
static const char* FramesetFor(const AmodPlayerFrame* frame, int dirY, bool rising)
{
    if (frame->state == AMOD_PLAYER_STATE_LADDER)
    {
        return dirY == 0 ? nullptr : (dirY > 0 ? "ladder_up" : "ladder_down");
    }
    if (frame->state == AMOD_PLAYER_STATE_CHAIN)
    {
        return dirY == 0 ? nullptr : (dirY > 0 ? "chain_up" : "chain_down");
    }
    if (frame->state == AMOD_PLAYER_STATE_PIPE)
    {
        return "pipe_move";
    }
    switch (frame->subState)
    {
        // rising -> silent "jump"; "fall" has the land sound
    case AMOD_PLAYER_SUB_FALLING:
    case AMOD_PLAYER_SUB_DROPPED: return rising ? "jump" : "fall";
    case AMOD_PLAYER_SUB_WALK_LEFT:
    case AMOD_PLAYER_SUB_WALK_RIGHT: return "move";
    }
    return "stay";
}

// Anim_Play restarts every call; only fire on change.
static void PlayAnim(const AmodPlayerFrame* frame, int dirY)
{
    static float wasY = 0.0f;
    AmodPlayer player;
    if (!Amod::Player::Get(player) || player.entity < 0)
    {
        return;
    }

    // frame->previousPosition equals position here; keep own wasY for rise/fall.
    const bool rising = player.position[1] > wasY;
    wasY = player.position[1];

    const char* const frameset = FramesetFor(frame, dirY, rising);
    if (frameset == nullptr)
    {
        if (g_playing[0] != '\0')
        {
            Amod::Mmd::StopEntityAnim(player.entity);
            g_playing = "";
        }
        return;
    }

    if (strcmp(frameset, g_playing) != 0 && Amod::Mmd::PlayEntityAnim(player.entity, frameset))
    {
        g_playing = frameset;
    }
}

static int g_wantDirX = 1;

// Face movement dir; on climbable face camera (0).
static void TurnTowards(const AmodPlayerFrame* frame, int dirX, float delta)
{
    if (dirX != 0)
    {
        g_wantDirX = dirX;
    }
    const bool climbing = frame->state == AMOD_PLAYER_STATE_LADDER || frame->state == AMOD_PLAYER_STATE_CHAIN;
    const float want = climbing ? 0.0f : (g_wantDirX > 0 ? AMOD_PLAYER_FACING_RIGHT : AMOD_PLAYER_FACING_LEFT);
    float gap = want - frame->facing;

    while (gap > 3.14159265f) { gap -= 6.28318531f; }
    while (gap < -3.14159265f) { gap += 6.28318531f; }

    if (gap > -0.01f && gap < 0.01f)
    {
        Amod::Player::SetFacing(want);
        return;
    }

    const float step = 12.0f * delta;
    const float room = gap > 0.0f ? gap : -gap;
    Amod::Player::SetFacing(frame->facing + (gap > 0.0f ? 1.0f : -1.0f) * (step < room ? step : room));
}

static void __cdecl DriveHandler(const AmodPlayerFrame* frame, void*)
{
    int dirX = 0;
    if (Amod::Input::IsDown(AMOD_KEY_RIGHT))
    {
        dirX = 1;
    }
    else if (Amod::Input::IsDown(AMOD_KEY_LEFT))
    {
        dirX = -1;
    }

    int dirY = 0;
    if (Amod::Input::IsDown(AMOD_KEY_UP))
    {
        dirY = 1;
    }
    else if (Amod::Input::IsDown(AMOD_KEY_DOWN))
    {
        dirY = -1;
    }

    const int side = Amod::Player::Step(dirX, dirY, Amod::Input::IsDown(AMOD_KEY_SPACE), frame->delta);
    PlayAnim(frame, dirY);
    TurnTowards(frame, dirX, frame->delta);
    if (side != 0)
    {
        Amod::World::CrossRoom(side);
    }
    if (side != g_wasSide)
    {
        g_wasSide = side;
    }
}

// drive [mod | both | nobody | game]
static void __cdecl DriveCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Amod::Console::Printf("driver: %s\n", DriverName(Amod::Player::Driver()));
        return;
    }

    g_wasSide = 0;
    bool took = false;
    if (strcmp(argv[1], "mod") == 0)
    {
        took = Amod::Player::Drive(&DriveHandler);
    }
    else if (strcmp(argv[1], "both") == 0)
    {
        took = Amod::Player::DriveAlongside(&DriveHandler);
    }
    else if (strcmp(argv[1], "nobody") == 0)
    {
        took = Amod::Player::Freeze();
    }
    else if (strcmp(argv[1], "game") == 0)
    {
        took = Amod::Player::Release();
    }
    else
    {
        Amod::Console::Printf("drive [mod | both | nobody | game]\n");
        return;
    }

    if (!took)
    {
        Amod::Console::Printf("drive: %s\n", Amod::LastError());
        return;
    }

    Amod::Console::Printf("driver: %s\n", DriverName(Amod::Player::Driver()));
}

static char Printable(char cell)
{
    if (cell == ' ')
    {
        return ' ';
    }
    return (cell > 32 && cell < 127) ? cell : '?';
}

static void WidePlace(const AmodPlayerPlace& place, int room)
{
    for (int row = 0; row < place.rows; ++row)
    {
        char line[64] = {};
        int at = 0;
        for (int column = -6; column < place.columns + 6; ++column)
        {
            if (column == 0 || column == place.columns)
            {
                line[at++] = '|';
            }
            const bool him = column == place.atColumn && row == place.atRow;
            line[at++] = him ? '[' : ' ';
            line[at++] = Printable(Amod::World::Cell(room, place.column + column, place.row + row));
            line[at++] = him ? ']' : ' ';
        }
        Amod::Console::Printf("%2d %s\n", place.row + row, line);
    }
}

// place | place cell | place wide
static void __cdecl PlaceCommand(int argc, const char** argv, void*)
{
    AmodPlayerPlace place;
    if (!Amod::Player::GetPlace(place))
    {
        Amod::Console::Printf("place: %s\n", Amod::LastError());
        return;
    }

    const bool byCell = argc > 1 && strcmp(argv[1], "cell") == 0;
    if (argc > 1 && strcmp(argv[1], "wide") == 0)
    {
        WidePlace(place, Amod::World::CurrentRoom());
        return;
    }

    const int room = Amod::World::CurrentRoom();
    Amod::Console::Printf("col %d row %d  at %d,%d\n", place.column, place.row, place.atColumn, place.atRow);

    for (int row = 0; row < place.rows; ++row)
    {
        char line[32] = {};
        int at = 0;
        for (int column = 0; column < place.columns; ++column)
        {
            const char mine = byCell
                ? Amod::World::Cell(room, place.column + column, place.row + row)
                : place.cells[row * place.columns + column];
            const bool him = column == place.atColumn && row == place.atRow;
            line[at++] = him ? '[' : ' ';
            line[at++] = Printable(mine);
            line[at++] = him ? ']' : ' ';
        }
        Amod::Console::Printf("%2d %s\n", place.row + row, line);
    }

    if (byCell)
    {
        return;
    }

    Amod::Console::Printf("L %d  R %d  climb %d  foot %d  pipe %d\n",
        Amod::Player::Ask(AMOD_PLAYER_ASK_LEFT),
        Amod::Player::Ask(AMOD_PLAYER_ASK_RIGHT),
        Amod::Player::Ask(AMOD_PLAYER_ASK_CLIMB),
        Amod::Player::Ask(AMOD_PLAYER_ASK_FOOTING),
        Amod::Player::Ask(AMOD_PLAYER_ASK_PIPE));
}

static int __cdecl LadderHandler(const AmodPlayerFrame* frame, void*)
{
    const int dirY = Amod::Input::IsDown(AMOD_KEY_UP) ? 1 : (Amod::Input::IsDown(AMOD_KEY_DOWN) ? -1 : 0);
    if (dirY != 0)
    {
        Amod::Player::SetPosition(frame->position[0],
            frame->position[1] + dirY * 0.6f * frame->delta,
            frame->position[2]);
    }
    PlayAnim(frame, dirY);
    TurnTowards(frame, 0, frame->delta);
    return 1;
}

static int g_refused = 0;

static int __cdecl DeathHandler(const AmodPlayerDeathInfo* info, void*)
{
    ++g_refused;
    Amod::Console::Printf("* refuse death %d (%d)\n", info->kind, g_refused);
    return 1;
}

static bool g_standing = false;

// nodie -- toggle death refuse
static void __cdecl NoDieCommand(int, const char**, void*)
{
    if (g_standing)
    {
        Amod::Player::ClearDeathHandler();
        g_standing = false;
        Amod::Console::Printf("deaths on; refused %d\n", g_refused);
        return;
    }
    if (!Amod::Player::SetDeathHandler(&DeathHandler))
    {
        Amod::Console::Printf("nodie: %s\n", Amod::LastError());
        return;
    }
    g_standing = true;
    g_refused = 0;
    Amod::Console::Printf("deaths refused\n");
}

static int g_stateTaken = -1;

// state [ladder | game]
static void __cdecl StateCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Amod::Console::Printf("%s\n",
            g_stateTaken < 0 ? "all game" : "ladder = mod");
        return;
    }
    if (strcmp(argv[1], "ladder") == 0)
    {
        if (!Amod::Player::SetStateHandler(AMOD_PLAYER_STATE_LADDER, &LadderHandler))
        {
            Amod::Console::Printf("state: %s\n", Amod::LastError());
            return;
        }
        g_stateTaken = AMOD_PLAYER_STATE_LADDER;
        Amod::Console::Printf("ladder = mod\n");
        return;
    }
    if (strcmp(argv[1], "game") == 0)
    {
        Amod::Player::ClearStateHandler(AMOD_PLAYER_STATE_LADDER);
        g_stateTaken = -1;
        Amod::Console::Printf("ladder = game\n");
        return;
    }
    Amod::Console::Printf("state [ladder | game]\n");
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Amod::Console::AddCommand("pl", &PlayerCommand);
    Amod::Console::AddCommand("plwatch", &WatchCommand);
    Amod::Console::AddCommand("life", &LifeCommand);
    Amod::Console::AddCommand("score", &ScoreCommand);
    Amod::Console::AddCommand("time", &TimeCommand);
    Amod::Console::AddCommand("btime", &BonusTimeCommand);
    Amod::Console::AddCommand("drive", &DriveCommand);
    Amod::Console::AddCommand("place", &PlaceCommand);
    Amod::Console::AddCommand("state", &StateCommand);
    Amod::Console::AddCommand("nodie", &NoDieCommand);

    Amod::World::OnEvent(AMOD_WORLD_EVENT_TIME_UP, &OnWorldEvent);
    Amod::Player::On(AMOD_PLAYER_EVENT_DIED, &OnPlayerEvent);
    Amod::Player::On(AMOD_PLAYER_EVENT_REVIVED, &OnPlayerEvent);
    Amod::Player::On(AMOD_PLAYER_EVENT_LIVES_CHANGED, &OnPlayerEvent);
    Amod::Player::On(AMOD_PLAYER_EVENT_SCORE_CHANGED, &OnPlayerEvent);
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
    AmodPlayer player;
    if (!g_watching || !Amod::Player::Get(player))
    {
        return;
    }
    if (player.state == g_wasState && player.subState == g_wasSub && player.isDead == g_wasDead)
    {
        return;
    }
    Amod::Console::Printf("%s / %s%s  %+.3f %+.3f  face %+.2f\n",
        StateName(player.state), SubName(player.subState),
        player.isDead ? " DEAD" : "",
        player.position[0], player.position[1], player.facing);
    g_wasState = player.state;
    g_wasSub = player.subState;
    g_wasDead = player.isDead;
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}