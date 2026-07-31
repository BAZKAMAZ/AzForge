#include <d3d8.h>
#include <ForgeApi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//   mon   mtypes   mkill <n|all>   mplace <name> <col> <row> [col2 row2]
//   mmove <n|all> <movement>   mspeed <factor>

static const char* const kWasp = "wasp";
static const char* const kGhost = "ghost";

static float g_ghostPhase = 0.0f;

static int g_seen = 0;

static const char* StateName(int state)
{
    return state == AMOD_MONSTER_STATE_SIT ? "sit" : "move";
}

static AmodMonsterKillResult __cdecl OnMonsterKilled(const AmodMonsterKillInfo* info, void*)
{
    g_seen += info->isDecor != 0 ? 0 : 1;
    return AMOD_MONSTER_KILL_DEFAULT;
}

// Its own rule for the meeting: a ghost goes through the player and neither is any the worse.
static AmodMonsterTouchResult __cdecl OnMonsterTouch(const AmodMonsterTouchInfo* info, void*)
{
    return (info->isPlayer != 0 && strcmp(info->name, kGhost) == 0)
               ? AMOD_MONSTER_TOUCH_REFUSE
               : AMOD_MONSTER_TOUCH_DEFAULT;
}

static void __cdecl MonstersCommand(int, const char**, void*)
{
    const int count = Amod::Monsters::Count();
    Amod::Console::Printf("%d monster(s) here, %d killed this level, %d seen off by this mod\n",
                          count, Amod::Monsters::KilledCount(), g_seen);

    for (int index = 0; index < count; ++index)
    {
        AmodMonster monster;
        if (!Amod::Monsters::Get(index, monster))
        {
            continue;
        }

        char state[24] = "-";
        if (monster.hasState)
        {
            sprintf_s(state, sizeof(state), "%s %.2fs", StateName(monster.state), monster.timeLeft);
        }

        Amod::Console::Printf("%2d  %-10s moves %-9s %-12s speed %.2f  facing %+d%s%s  %s\n", index,
                              monster.name, monster.moves, state, monster.speed, monster.facing,
                              monster.isHard ? "  hard" : "", monster.isDecor ? "  decor" : "",
                              monster.tag);
    }
}

// Nothing the game has: it drifts the way it is facing and rides a wave while doing it.
static void __cdecl GhostThink(const AmodMonsterThinkInfo* info, void*)
{
    g_ghostPhase += info->delta;

    const Amod::Monsters::MonsterRef ghost = Amod::Monsters::At(info->index);

    // The game's own step, so walls stop it; the bob is written straight, because nothing is
    // supposed to stop that.
    if (Amod::Monsters::Move(info->index, info->facing) == 1)
    {
        ghost["facing"].Set(-info->facing);
    }
    ghost["y"].Set(info->position[1] + sinf(g_ghostPhase * 4.0f) * 0.004f);
}

static void __cdecl TypesCommand(int, const char**, void*)
{
    const int count = Amod::Monsters::TypeCount();
    Amod::Console::Printf("%d kind(s) of monster\n", count);

    for (int index = 0; index < count; ++index)
    {
        AmodMonsterType type;
        if (Amod::Monsters::GetType(index, type))
        {
            Amod::Console::Printf("%2d  %-12s speed %.2f  up %.2f  vert %.2f\n", index, type.name,
                                  type.speed, type.speedUp, type.vertSpeed);
        }
    }
}

static void __cdecl KillCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Amod::Console::Printf("mkill <index|all>\n");
        return;
    }

    if (strcmp(argv[1], "all") == 0)
    {
        int killed = 0;
        // Backwards: killing one takes it out of the list the rest are numbered in.
        for (int index = Amod::Monsters::Count() - 1; index >= 0; --index)
        {
            killed += Amod::Monsters::Kill(index) ? 1 : 0;
        }
        Amod::Console::Printf("killed %d\n", killed);
        return;
    }

    // Read before killing, so what is reported is what actually died.
    const int index = atoi(argv[1]);
    AmodMonster monster;

    if (!Amod::Monsters::Get(index, monster))
    {
        Amod::Console::Printf("there is no monster %d\n", index);
        return;
    }

    const char* const name = monster.name;
    const char* const moves = monster.moves;

    Amod::Console::Printf(Amod::Monsters::Kill(index) ? "killed %d: %s, moves %s\n"
                                                      : "could not kill %d: %s, moves %s\n",
                          index, name, moves);
}

// mplace <name> <col> <row> [col2 row2]
static void __cdecl PlaceCommand(int argc, const char** argv, void*)
{
    if (argc < 4)
    {
        Amod::Console::Printf("mplace <name> <col> <row> [col2 row2]\n");
        return;
    }

    Amod::Monsters::PlacementBuilder where(argv[1], atoi(argv[2]), atoi(argv[3]));
    if (argc >= 6)
    {
        where.Span(atoi(argv[4]), atoi(argv[5]));
    }

    Amod::Console::Printf(where.Put() ? "put a '%s' on cell %s,%s\n"
                                      : "could not put a '%s' on cell %s,%s\n",
                          argv[1], argv[2], argv[3]);
}

// mmove <n|all> <movement>
static void __cdecl MovementCommand(int argc, const char** argv, void*)
{
    if (argc < 3)
    {
        Amod::Console::Printf("mmove <n|all> %s\n",
                              "skull|snake|snake2|spider|scorpion|j_skull|bat|mummy|mummy2");
        return;
    }

    if (strcmp(argv[1], "all") == 0)
    {
        int moved = 0;
        for (int index = Amod::Monsters::Count() - 1; index >= 0; --index)
        {
            moved += Amod::Monsters::SetMovement(index, argv[2]) ? 1 : 0;
        }
        Amod::Console::Printf("%d monster(s) now move like a %s\n", moved, argv[2]);
        return;
    }

    const int index = atoi(argv[1]);
    Amod::Console::Printf(Amod::Monsters::SetMovement(index, argv[2])
                              ? "%d now moves like a %s\n"
                              : "could not make %d move like a %s\n",
                          index, argv[2]);
}

static void __cdecl SpeedCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Amod::Console::Printf("mspeed <factor>\n");
        return;
    }

    const float factor = static_cast<float>(atof(argv[1]));
    const int count = Amod::Monsters::Count();

    for (int index = 0; index < count; ++index)
    {
        AmodMonster monster;
        if (Amod::Monsters::Get(index, monster))
        {
            Amod::Monsters::At(index)["speed"].Set(monster.speed * factor);
        }
    }
    Amod::Console::Printf("%d monster(s) now move %.2f times as fast\n", count, factor);
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Amod::Console::AddCommand("mon", &MonstersCommand);
    Amod::Console::AddCommand("mtypes", &TypesCommand);
    Amod::Console::AddCommand("mkill", &KillCommand);
    Amod::Console::AddCommand("mplace", &PlaceCommand);
    Amod::Console::AddCommand("mmove", &MovementCommand);
    Amod::Console::AddCommand("mspeed", &SpeedCommand);
    Amod::Monsters::OnKill(&OnMonsterKilled);
    Amod::Monsters::OnTouch(&OnMonsterTouch);

    // The definition alone would never appear: the spawner matches a placement's $name against a
    // closed table of ten, so the name is taught to it too, and told which of the ten it moves like.
    AmodMonsterType known;

    if (!Amod::Monsters::FindType(kWasp, known))
    {
        Amod::Monsters::NewType(kWasp, AMOD_MONSTER_MOVES_BAT)
            .Speed(0.9f)
            .Define();
    }
    Amod::Monsters::DefineKind(kWasp, AMOD_MONSTER_MOVES_BAT);

    // The same again, but this one keeps only the bat's shape and runs the mod's own think.
    if (!Amod::Monsters::FindType(kGhost, known))
    {
        Amod::Monsters::NewType(kGhost, AMOD_MONSTER_MOVES_BAT).Define();
    }
    Amod::Monsters::DefineKind(kGhost, AMOD_MONSTER_MOVES_BAT);
    Amod::Monsters::DefineBehaviour(kGhost, &GhostThink);

    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
