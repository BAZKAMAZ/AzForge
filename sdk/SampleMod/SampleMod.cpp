#include <d3d8.h>
#include <ForgeApi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//   mon   mtypes   mkill <n|all>   mplace <name> <col> <row> [col2 row2]
//   mmove <n|all> <movement>   mspeed <factor>
//   mbox <n>   mfat <n> <factor>   manim <n> <script>, all by the index mon prints
//   mabox <script> [factor], by the animation script itself
//   morbit <n> [radius] [phase]   mpace <n> [wait_low wait_high] [hard]
//   mtrack [n], which holds a monster by id rather than by number
//   mstep <n> [accel] [gravity], the body that does the walking
//   mplaced [room]   mfind <name_col_row> [level], a monster by what the level file calls it
//   mrule [name_col_row] [speed|-], as many as sixteen, kept across every rebuild of the room
//   mwho <n>   mcolour <n> [aarrggbb]   mskull [script] [factor]
//   mpaint <n|name_col_row> [aarrggbb|-], a key survives leaving the room, a number does not
//   mskin <n> blend|solid|normals|second <model> <texture>

static const char* const kWasp = "wasp";
static const char* const kGhost = "ghost";

static void Refused(const char* doing)
{
    Amod::Console::Printf("%s: %s\n", doing, Amod::LastError());
}

static float g_ghostPhase = 0.0f;

static int g_seen = 0;

struct Paint
{
    char key[64];
    AmodMonsterId id;
    uint32_t colour;
};

static Paint g_paints[16];
static int g_paintCount = 0;

// IdOfDraw distinguishes entities that share a model.
static AmodModelId g_watched = AMOD_INVALID_MODEL;
static int g_watchLeft = 0;

// Track by id — index shifts when others die.
static Amod::Monsters::Tracked g_kept;

// Watch each model once.
static AmodModelId g_watchedModels[16];
static int g_watchedCount = 0;

struct Rule
{
    char key[64];
    float speed;
};

static Rule g_rules[16];
static int g_ruleCount = 0;

static int RuleAt(const char* key)
{
    for (int at = 0; at < g_ruleCount; ++at)
    {
        if (strcmp(g_rules[at].key, key) == 0)
        {
            return at;
        }
    }
    return -1;
}

static int PaintOf(AmodMonsterId id)
{
    for (int at = 0; at < g_paintCount; ++at)
    {
        if (g_paints[at].id == id && id != AMOD_MONSTER_NONE)
        {
            return at;
        }
    }
    return -1;
}

static int PaintOfKey(const char* key)
{
    for (int at = 0; at < g_paintCount; ++at)
    {
        if (key != nullptr && strcmp(g_paints[at].key, key) == 0)
        {
            return at;
        }
    }
    return -1;
}

static const char* StateName(int state)
{
    return state == AMOD_MONSTER_STATE_SIT ? "sit" : "move";
}

static AmodMonsterKillResult __cdecl OnMonsterKilled(const AmodMonsterKillInfo* info, void*)
{
    g_seen += info->isDecor != 0 ? 0 : 1;
    return AMOD_MONSTER_KILL_DEFAULT;
}

// Ghost passes through the player.
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

// Custom think: drift facing + bob.
static void __cdecl GhostThink(const AmodMonsterThinkInfo* info, void*)
{
    g_ghostPhase += info->delta;

    const Amod::Monsters::MonsterRef ghost = Amod::Monsters::At(info->index);

    // Game step (walls stop it); bob is written straight.
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
        // Backwards: kill removes from the numbered list.
        for (int index = Amod::Monsters::Count() - 1; index >= 0; --index)
        {
            killed += Amod::Monsters::Kill(index) ? 1 : 0;
        }
        Amod::Console::Printf("killed %d\n", killed);
        return;
    }

    // Read before kill so the report is accurate.
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

static void __cdecl MovementCommand(int argc, const char** argv, void*)
{
    if (argc < 3)
    {
        Amod::Console::Printf("mmove <n|all> %s\n", "skull|snake|snake2|spider|scorpion|j_skull|bat|mummy|mummy2");
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

static const char* BoxName(int which)
{
    switch (which)
    {
    case AMOD_MONSTER_BOX_MAIN:   return "main";
    case AMOD_MONSTER_BOX_SECOND: return "second";
    case AMOD_MONSTER_BOX_OBJECT: return "object";
    }
    return "?";
}

// Index from mon; kind comes from the monster.
static const char* KindAt(int argc, const char** argv, int at, AmodMonster& monster)
{
    return (argc > at && Amod::Monsters::Get(atoi(argv[at]), monster)) ? monster.name : nullptr;
}

static void __cdecl BoxCommand(int argc, const char** argv, void*)
{
    AmodMonster monster;
    const char* const kind = KindAt(argc, argv, 1, monster);
    if (kind == nullptr)
    {
        Amod::Console::Printf("mbox <n>, the index mon prints\n");
        return;
    }

    for (int which = 0; which <= AMOD_MONSTER_BOX_OBJECT; ++which)
    {
        AmodBox box;
        if (Amod::Monsters::GetTypeBox(kind, which, box))
        {
            Amod::Console::Printf("%-8s %-6s min %+.3f %+.3f %+.3f  max %+.3f %+.3f %+.3f\n", kind, 
                BoxName(which), box.min[0], box.min[1], box.min[2], box.max[0], box.max[1], box.max[2]);
        }
    }

    AmodBox live;
    if (Amod::Monsters::GetBox(atoi(argv[1]), live))
    {
        Amod::Console::Printf("  live %-3d       min %+.3f %+.3f %+.3f  max %+.3f %+.3f %+.3f\n", atoi(argv[1]), 
            live.min[0], live.min[1], live.min[2], live.max[0], live.max[1], live.max[2]);
    }
}

// Grow X/Z both ways, Y upward only — feet stay on the floor.
static void Inflate(AmodBox& box, float factor)
{
    for (int axis = 0; axis < 3; axis += 2)
    {
        const float middle = (box.min[axis] + box.max[axis]) * 0.5f;
        const float half = (box.max[axis] - box.min[axis]) * 0.5f * factor;
        box.min[axis] = middle - half;
        box.max[axis] = middle + half;
    }
    box.max[1] = box.min[1] + (box.max[1] - box.min[1]) * factor;
}

static void __cdecl FatCommand(int argc, const char** argv, void*)
{
    AmodMonster monster;
    const char* const kind = KindAt(argc, argv, 1, monster);
    const float factor = argc > 2 ? static_cast<float>(atof(argv[2])) : 0.0f;

    if (kind == nullptr || factor <= 0.0f)
    {
        Amod::Console::Printf("mfat <n> <factor>, e.g. mfat 0 3\n");
        return;
    }

    char wanted[64];
    strncpy_s(wanted, sizeof(wanted), kind, _TRUNCATE);

    int done = 0;
    for (int which = 0; which <= AMOD_MONSTER_BOX_OBJECT; ++which)
    {
        AmodBox box;
        if (!Amod::Monsters::GetTypeBox(wanted, which, box))
        {
            continue;
        }
        Inflate(box, factor);
        done += Amod::Monsters::SetTypeBox(wanted, which, box) ? 1 : 0;
    }

    int live = 0;
    const int here = Amod::Monsters::Count();
    for (int at = 0; at < here; ++at)
    {
        AmodMonster each;
        AmodBox box;
        if (!Amod::Monsters::Get(at, each) || strcmp(each.name, wanted) != 0 || !Amod::Monsters::GetBox(at, box))
        {
            continue;
        }
        Inflate(box, factor);
        live += Amod::Monsters::SetBox(at, box) ? 1 : 0;
    }

    Amod::Console::Printf("%s: %d box(es) and %d live one(s) grown %.2f times\n", wanted, done, live, factor);
}

// Animation paths are scripts (e.g. scripts/anim_spider.txt).
static void __cdecl AnimateCommand(int argc, const char** argv, void*)
{
    AmodMonster monster;
    const char* const kind = KindAt(argc, argv, 1, monster);
    if (kind == nullptr || argc < 3)
    {
        Amod::Console::Printf("manim <n> <script>, e.g. manim 0 scripts/anim_bat.txt\n");
        return;
    }

    char wanted[64];
    strncpy_s(wanted, sizeof(wanted), kind, _TRUNCATE);

    if (Amod::Monsters::Type(wanted)["animation"].Set(argv[2]))
    {
        Amod::Console::Printf("%s animates from '%s' now\n", wanted, argv[2]);
        return;
    }
    Refused("manim");
}

// Box belongs to the animation script, not a monster index.
static void __cdecl AnimBoxCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Amod::Console::Printf("mabox <script> [factor], e.g. mabox scripts/anim_spider.txt 2\n");
        return;
    }

    // Load the script if it is not in memory yet.
    AmodAnimation about;
    AmodAnimationId animation = Amod::Mmd::FindAnimation(argv[1]);
    if (!Amod::Mmd::GetAnimation(animation, about))
    {
        animation = Amod::Mmd::LoadAnimation(argv[1]);
    }

    if (!Amod::Mmd::GetAnimation(animation, about))
    {
        Refused("mabox");
        return;
    }

    // Use SELF box if present, else ENTITY.
    Amod::Bounds box;
    const int which = Amod::Mmd::GetAnimBox(animation, AMOD_ANIM_BOX_SELF, -1, box) ? AMOD_ANIM_BOX_SELF : AMOD_ANIM_BOX_ENTITY;
    if (which == AMOD_ANIM_BOX_ENTITY && !Amod::Mmd::GetAnimBox(animation, which, -1, box))
    {
        Refused("mabox");
        return;
    }

    if (argc < 3)
    {
        Amod::Console::Printf("%s: min %+.3f %+.3f %+.3f  max %+.3f %+.3f %+.3f, %d frame(s)\n", argv[1], box.min[0], box.min[1], box.min[2], box.max[0], 
            box.max[1], box.max[2], Amod::Mmd::AnimBoxFrames(animation, which));
        return;
    }

    const float factor = static_cast<float>(atof(argv[2]));
    if (factor <= 0.0f)
    {
        Amod::Console::Printf("a factor above zero, please\n");
        return;
    }

    // Same as monster box: X/Z both ways, Y upward only.
    for (int axis = 0; axis < 3; axis += 2)
    {
        const float middle = (box.min[axis] + box.max[axis]) * 0.5f;
        const float half = (box.max[axis] - box.min[axis]) * 0.5f * factor;
        box.min[axis] = middle - half;
        box.max[axis] = middle + half;
    }
    box.max[1] = box.min[1] + (box.max[1] - box.min[1]) * factor;

    if (Amod::Mmd::SetAnimBox(animation, which, -1, box))
    {
        Amod::Console::Printf("%s grown %.2f times\n", argv[1], factor);
        return;
    }
    Refused("mabox");
}

static void __cdecl TrackCommand(int argc, const char** argv, void*)
{
    if (argc >= 2)
    {
        AmodMonster monster;
        if (!Amod::Monsters::Get(atoi(argv[1]), monster))
        {
            Refused("mtrack");
            return;
        }

        g_kept = Amod::Monsters::ById(monster.id);
        Amod::Console::Printf("holding %s, id %u, standing at %d right now\n", monster.name, monster.id, atoi(argv[1]));
        return;
    }

    if (g_kept.Id() == AMOD_MONSTER_NONE)
    {
        Amod::Console::Printf("mtrack <n> first, then mtrack alone to see where it went\n");
        return;
    }

    AmodMonster monster;
    if (!g_kept.Get(monster))
    {
        Amod::Console::Printf("id %u: %s\n", g_kept.Id(), Amod::LastError());
        return;
    }

    Amod::Console::Printf("id %u is the %s at %d now, %.3f %.3f %.3f\n", g_kept.Id(), monster.name, g_kept.Index(), monster.position[0], monster.position[1], monster.position[2]);
}

// Circle: radius, phase; bare prints, numbers set.
static void __cdecl OrbitCommand(int argc, const char** argv, void*)
{
    AmodMonster monster;
    if (argc < 2 || !Amod::Monsters::Get(atoi(argv[1]), monster))
    {
        Amod::Console::Printf("morbit <n> [radius] [phase], the index mon prints\n");
        return;
    }

    const int index = atoi(argv[1]);
    const Amod::Monsters::MonsterRef which = Amod::Monsters::At(index);

    if (argc > 2)
    {
        which["radius"].Set(static_cast<float>(atof(argv[2])));
    }
    if (argc > 3)
    {
        which["phase"].Set(static_cast<float>(atof(argv[3])));
    }

    Amod::Console::Printf("%d %-10s radius %.4f  phase %.4f  axis %.3f %.3f %.3f\n", index,
        monster.name, which["radius"].Number(), which["phase"].Number(),
        which["axis"].Number(0), which["axis"].Number(1), which["axis"].Number(2));
}

// $*_time is a range: pair in, pair out.
static void __cdecl PaceCommand(int argc, const char** argv, void*)
{
    AmodMonster monster;
    if (argc < 2 || !Amod::Monsters::Get(atoi(argv[1]), monster))
    {
        Amod::Console::Printf("mpace <n> [walk_wait_low walk_wait_high] [hard]\n");
        return;
    }

    const int index = atoi(argv[1]);
    const Amod::Monsters::MonsterRef which = Amod::Monsters::At(index);

    if (argc > 3)
    {
        which["walk_wait"].Set(Amod::Value(static_cast<float>(atof(argv[2])), static_cast<float>(atof(argv[3]))));
    }
    if (argc > 4)
    {
        which["hard"].Set(Amod::Value(static_cast<float>(atof(argv[4]))));
    }

    Amod::Console::Printf("%d %-10s walk %.2f..%.2f  wait between %.2f..%.2f  sit %.2f..%.2f  hard x%.2f\n",
        index, monster.name, which["walk_time"].Number(0), which["walk_time"].Number(1),
        which["walk_wait"].Number(0), which["walk_wait"].Number(1),
        which["sit_time"].Number(0), which["sit_time"].Number(1), which["hard"].Number());

    Amod::Console::Printf("   %s for %.2fs more, and inside that %s for %.2fs more\n",
        StateName(which["state"].Integer()), which["time_left"].Number(),
        which["walk_state"].Integer() == AMOD_MONSTER_WALK_STAY ? "stands still" : "walks",
        which["walk_left"].Number());
}

// Step body: accel / gravity.
static void __cdecl StepCommand(int argc, const char** argv, void*)
{
    AmodMonster monster;
    if (argc < 2 || !Amod::Monsters::Get(atoi(argv[1]), monster))
    {
        Refused("mstep");
        return;
    }

    const Amod::Monsters::MonsterRef which = Amod::Monsters::At(atoi(argv[1]));

    if (argc > 2)
    {
        which["accel"].Set(static_cast<float>(atof(argv[2])));
    }
    if (argc > 3)
    {
        which["gravity"].Set(static_cast<float>(atof(argv[3])));
    }

    Amod::Console::Printf("%d %-10s speed %.4f  accel %.4f  now %.4f  state %d\n", atoi(argv[1]),
        monster.name, monster.speed, which["accel"].Number(),
        which["velocity"].Number(), which["move_state"].Integer());
    Amod::Console::Printf("   %.4f above the floor, falling %.4f, gravity %.2f x %.4f\n",
        which["height"].Number(), which["vert_velocity"].Number(),
        which["gravity"].Number(), which["fall_speed"].Number());
}

// Placements in the room, each with its key.
static void __cdecl PlacedCommand(int argc, const char** argv, void*)
{
    const int room = argc > 1 ? atoi(argv[1]) : -1;
    const int count = Amod::Monsters::PlacedCount(room);

    Amod::Console::Printf("%d monster(s) written into room %d\n", count, room);

    for (int index = 0; index < count; ++index)
    {
        AmodMonster placed;
        if (!Amod::Monsters::GetPlaced(room, index, placed))
        {
            continue;
        }

        const int column = static_cast<int>((placed.position[0] - AMOD_ROOM_ORIGIN_X) / AMOD_ROOM_CELL_SIZE);
        const int row = static_cast<int>((AMOD_ROOM_ORIGIN_Y - placed.position[1]) / AMOD_ROOM_CELL_SIZE);

        Amod::Console::Printf("%2d  %s_%d_%d%s%s  level %d\n", index, placed.name, column, row, placed.tag[0] != '\0' ? "  tag " : "", placed.tag, placed.level);
    }
}

// Find by placement key (name_col_row).
static void __cdecl FindCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Amod::Console::Printf("mfind <name_col_row> [level], the key mplaced prints\n");
        return;
    }

    const Amod::Monsters::Tracked found = Amod::Monsters::Find(argv[1]);
    const int placed = Amod::Monsters::FindPlaced(argv[1]);

    if (found.Id() == AMOD_MONSTER_NONE && placed < 0)
    {
        Refused("mfind");
        return;
    }

    if (argc > 2 && placed >= 0)
    {
        Amod::Monsters::Placed(-1, placed).Set("level", atoi(argv[2]));
    }

    AmodMonster monster;
    if (found.Get(monster))
    {
        Amod::Console::Printf("%s is monster %d here, id %u, speed %.4f\n", argv[1], found.Index(),
            found.Id(), monster.speed);
    }
    else
    {
        Amod::Console::Printf("%s is not standing in this room right now\n", argv[1]);
    }

    AmodMonster written;
    if (placed >= 0 && Amod::Monsters::GetPlaced(-1, placed, written))
    {
        Amod::Console::Printf("   placement %d, level %d - leave the room and come back to see it applied\n",
            placed, written.level);
    }
}

static void __cdecl OnMonsterSpawned(const AmodMonsterSpawnInfo* info, void*)
{
    // Rebind paint key to the new id after rebuild.
    for (int at = 0; at < g_paintCount; ++at)
    {
        if (g_paints[at].key[0] != '\0' && Amod::Monsters::Find(g_paints[at].key).Id() == info->id)
        {
            g_paints[at].id = info->id;
        }
    }

    for (int at = 0; at < g_ruleCount; ++at)
    {
        if (Amod::Monsters::Find(g_rules[at].key).Id() == info->id)
        {
            Amod::Monsters::At(info->index)["speed"].Set(g_rules[at].speed);
            return;
        }
    }
}

// Speed only — not $level (hard texture / death colour).
static void __cdecl RuleCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Amod::Console::Printf("%d rule(s), mrule <name_col_row> <speed>, '-' for the speed drops "
            "one and 'mrule -' drops them all\n", g_ruleCount);
        for (int at = 0; at < g_ruleCount; ++at)
        {
            Amod::Console::Printf("   %-24s %.4f\n", g_rules[at].key, g_rules[at].speed);
        }
        return;
    }

    if (argv[1][0] == '-')
    {
        g_ruleCount = 0;
        Amod::Console::Printf("no rules now\n");
        return;
    }

    const int found = RuleAt(argv[1]);

    if (argc > 2 && argv[2][0] == '-')
    {
        if (found >= 0)
        {
            g_rules[found] = g_rules[--g_ruleCount];
        }
        Amod::Console::Printf(found >= 0 ? "%s walks as it was told to again\n" : "%s had no rule\n",
            argv[1]);
        return;
    }

    if (argc < 3)
    {
        Amod::Console::Printf("mrule <name_col_row> <speed>\n");
        return;
    }

    if (found < 0 && g_ruleCount == static_cast<int>(sizeof(g_rules) / sizeof(g_rules[0])))
    {
        Amod::Console::Printf("this mod keeps room for %d rules and holds that many already\n",
            g_ruleCount);
        return;
    }

    const int at = found >= 0 ? found : g_ruleCount++;
    strncpy_s(g_rules[at].key, sizeof(g_rules[at].key), argv[1], _TRUNCATE);
    g_rules[at].speed = static_cast<float>(atof(argv[2]));

    const Amod::Monsters::Tracked now = Amod::Monsters::Find(g_rules[at].key);
    if (now.Id() != AMOD_MONSTER_NONE)
    {
        Amod::Monsters::At(now.Index())["speed"].Set(g_rules[at].speed);
    }

    Amod::Console::Printf("%s walks at %.4f, now and every time the room is built again (%d rule(s))\n",
        g_rules[at].key, g_rules[at].speed, g_ruleCount);
}

static void __cdecl OnModelDrawn(const AmodModelDrawInfo* info, void*)
{
    if (g_watchLeft <= 0)
    {
        return;
    }
    --g_watchLeft;

    const AmodMonsterId id = Amod::Monsters::IdOfDraw(info->item);
    AmodMonster monster;

    if (id != AMOD_MONSTER_NONE && Amod::Monsters::ById(id).Get(monster))
    {
        Amod::Console::Printf("  draw %08X is monster %d, %s, id %u, at %.3f %.3f\n", info->item,
            Amod::Monsters::IndexOf(id), monster.name, id, info->position[0],
            info->position[1]);
        return;
    }
    Amod::Console::Printf("  draw %08X: %s\n", info->item, Amod::LastError());
}

// Watch that monster's model draws for one frame.
static void __cdecl WhoCommand(int argc, const char** argv, void*)
{
    AmodMonster monster;
    if (argc < 2 || !Amod::Monsters::Get(atoi(argv[1]), monster))
    {
        Refused("mwho");
        return;
    }

    AmodMonsterType kind;
    const AmodModelId model = Amod::Monsters::FindType(monster.name, kind) ? Amod::Mmd::Find(kind.model)
        : AMOD_INVALID_MODEL;
    if (model == AMOD_INVALID_MODEL)
    {
        Amod::Console::Printf("the model of '%s' is not loaded\n", monster.name);
        return;
    }

    if (model != g_watched)
    {
        Amod::Mmd::OnDraw(model, &OnModelDrawn);
        g_watched = model;
    }

    g_watchLeft = 16;
    Amod::Console::Printf("watching what '%s' is drawn from, the next %d draw(s):\n", monster.name,
        g_watchLeft);
}

// Per-draw colour via BeforeDraw; one handler for the kind.
static void __cdecl BeforeMonsterDrawn(const AmodModelDrawInfo* info, void*)
{
    const int at = PaintOf(Amod::Monsters::IdOfDraw(info->item));

    if (at >= 0)
    {
        Amod::Mmd::SetColour(info->model, g_paints[at].colour);
        return;
    }
    Amod::Mmd::ClearColours(info->model);
}

static bool WatchModel(AmodModelId model)
{
    for (int at = 0; at < g_watchedCount; ++at)
    {
        if (g_watchedModels[at] == model)
        {
            return true;
        }
    }

    if (g_watchedCount == static_cast<int>(sizeof(g_watchedModels) / sizeof(g_watchedModels[0])) ||
        Amod::Mmd::BeforeDraw(model, &BeforeMonsterDrawn) == AMOD_INVALID_TOKEN)
    {
        return false;
    }

    g_watchedModels[g_watchedCount++] = model;
    return true;
}

static void __cdecl PaintCommand(int argc, const char** argv, void*)
{
    AmodMonster monster;
    AmodMonsterType kind;

    if (argc < 2)
    {
        Amod::Console::Printf("mpaint <n|name_col_row> [aarrggbb|-], FFFF0000 is red. %d painted:\n",
            g_paintCount);
        for (int at = 0; at < g_paintCount; ++at)
        {
            Amod::Console::Printf("   %-24s id %-6u %08X\n",
                g_paints[at].key[0] != '\0' ? g_paints[at].key : "(by number)",
                g_paints[at].id, g_paints[at].colour);
        }
        return;
    }

    // Number = this visit; key = survives room rebuild.
    const bool byKey = strchr(argv[1], '_') != nullptr;
    const Amod::Monsters::Tracked found = byKey ? Amod::Monsters::Find(argv[1])
        : Amod::Monsters::Track(atoi(argv[1]));

    if (!found.Get(monster) || !Amod::Monsters::FindType(monster.name, kind))
    {
        Refused("mpaint");
        return;
    }

    const AmodModelId model = Amod::Mmd::Find(kind.model);
    if (model == AMOD_INVALID_MODEL)
    {
        Amod::Console::Printf("the model of '%s' is not loaded\n", monster.name);
        return;
    }

    const int already = byKey ? PaintOfKey(argv[1]) : PaintOf(monster.id);

    if (argc > 2 && argv[2][0] == '-')
    {
        if (already >= 0)
        {
            g_paints[already] = g_paints[--g_paintCount];
        }
        Amod::Mmd::ClearColours(model);
        Amod::Console::Printf(already >= 0 ? "%s is its own colour again\n" : "%s was not painted\n",
            monster.name);
        return;
    }

    if (already < 0 && g_paintCount == static_cast<int>(sizeof(g_paints) / sizeof(g_paints[0])))
    {
        Amod::Console::Printf("this mod keeps room for %d painted monsters and holds that many\n",
            g_paintCount);
        return;
    }

    if (!WatchModel(model))
    {
        Refused("mpaint could not watch the model being drawn");
        return;
    }

    Paint& paint = g_paints[already >= 0 ? already : g_paintCount++];
    paint.id = monster.id;
    paint.colour = argc > 2 ? static_cast<uint32_t>(strtoul(argv[2], nullptr, 16)) : 0xFFFF0000u;
    paint.key[0] = '\0';
    if (byKey)
    {
        strncpy_s(paint.key, sizeof(paint.key), argv[1], _TRUNCATE);
    }

    Amod::Console::Printf("%s, id %u, is painted %08X - %d painted in all%s\n", monster.name,
        paint.id, paint.colour, g_paintCount,
        byKey ? ", and this one across every visit" : ", this one until you leave the room");
}

static void __cdecl SkinCommand(int argc, const char** argv, void*)
{
    AmodMonster monster;
    AmodMonsterType kind;
    const int index = argc > 1 ? atoi(argv[1]) : -1;

    if (argc < 2 || !Amod::Monsters::Get(index, monster) ||
        !Amod::Monsters::FindType(monster.name, kind))
    {
        Amod::Console::Printf("mskin <n> blend|solid|normals|second <model> <texture>\n");
        return;
    }

    const Amod::Monsters::MonsterRef which = Amod::Monsters::At(index);
    const char* const what = argc > 2 ? argv[2] : "";

    if (strcmp(what, "blend") == 0 || strcmp(what, "solid") == 0)
    {
        const bool on = strcmp(what, "blend") == 0;
        which["blend"].Set(Amod::Value(on));
        Amod::Console::Printf("%s is drawn %s - a colour's alpha counts %s\n", monster.name,
            on ? "in the blending pass" : "solid", on ? "now" : "for nothing");
        return;
    }

    if (strcmp(what, "normals") == 0)
    {
        if (Amod::Mmd::RebuildNormals(Amod::Mmd::Find(kind.model)))
        {
            Amod::Console::Printf("%s has its normals worked out again\n", kind.model);
            return;
        }
        Refused("mskin");
        return;
    }

    if (strcmp(what, "second") == 0 && argc > 4)
    {
        const bool put = which["model2"].Set(argv[3]) && which["texture2"].Set(argv[4]);
        if (!put)
        {
            Refused("mskin");
            return;
        }
        Amod::Console::Printf("%s carries a second model, '%s' in '%s', drawn in the same place\n",
            monster.name, argv[3], argv[4]);
        return;
    }

    Amod::Console::Printf("mskin <n> blend|solid|normals|second <model> <texture>\n");
}

// Colour the whole kind at once (no per-draw handler).
static void __cdecl ColourCommand(int argc, const char** argv, void*)
{
    AmodMonster monster;
    AmodMonsterType kind;

    if (argc < 2 || !Amod::Monsters::Get(atoi(argv[1]), monster) ||
        !Amod::Monsters::FindType(monster.name, kind))
    {
        Amod::Console::Printf("mcolour <n> [aarrggbb], the whole kind at once\n");
        return;
    }

    const AmodModelId model = Amod::Mmd::Find(kind.model);
    AmodModel about;

    if (model == AMOD_INVALID_MODEL || !Amod::Mmd::Get(model, about))
    {
        Amod::Console::Printf("'%s' is drawn from '%s', which is not loaded\n", monster.name, kind.model);
        return;
    }

    const uint32_t colour = argc > 2 ? static_cast<uint32_t>(strtoul(argv[2], nullptr, 16)) : 0xFFFF0000u;
    const bool done = Amod::Mmd::SetColour(model, colour);

    Amod::Console::Printf("%s: kind %d, %d vert(s), %d frame(s) - SetColour %s\n", about.name,
        about.kind, about.vertexCount, about.frameCount, done ? "took" : "refused");
    if (!done)
    {
        Refused("mcolour");
    }
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
    Amod::Console::AddCommand("mbox", &BoxCommand);
    Amod::Console::AddCommand("mfat", &FatCommand);
    Amod::Console::AddCommand("manim", &AnimateCommand);
    Amod::Console::AddCommand("mabox", &AnimBoxCommand);
    Amod::Console::AddCommand("morbit", &OrbitCommand);
    Amod::Console::AddCommand("mpace", &PaceCommand);
    Amod::Console::AddCommand("mtrack", &TrackCommand);
    Amod::Console::AddCommand("mstep", &StepCommand);
    Amod::Console::AddCommand("mplaced", &PlacedCommand);
    Amod::Console::AddCommand("mfind", &FindCommand);
    Amod::Console::AddCommand("mrule", &RuleCommand);
    Amod::Console::AddCommand("mwho", &WhoCommand);
    Amod::Console::AddCommand("mpaint", &PaintCommand);
    Amod::Console::AddCommand("mcolour", &ColourCommand);
    Amod::Console::AddCommand("mskin", &SkinCommand);
    Amod::Monsters::OnSpawn(&OnMonsterSpawned);
    Amod::Monsters::OnKill(&OnMonsterKilled);
    Amod::Monsters::OnTouch(&OnMonsterTouch);

    // Teach the spawner the name and which of the ten it moves like.
    AmodMonsterType known;

    if (!Amod::Monsters::FindType(kWasp, known))
    {
        Amod::Monsters::NewType(kWasp, AMOD_MONSTER_MOVES_BAT)
            .Speed(0.9f)
            .Define();
    }
    Amod::Monsters::DefineKind(kWasp, AMOD_MONSTER_MOVES_BAT);

    // Ghost: bat shape, custom think.
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
