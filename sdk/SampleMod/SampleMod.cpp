#include <ForgeApi.h>

#include <stdlib.h>
#include <string.h>

using namespace Amod;

namespace
{
    bool Same(const char* a, const char* b)
    {
        return a != nullptr && b != nullptr && strcmp(a, b) == 0;
    }

    AmodToken g_placeWatch = AMOD_INVALID_TOKEN;
    bool g_refusing = false;
    const char* g_driven = "";

    int g_kind = 0;
    bool g_harmless = false;
    bool g_quietTouch = false;
    bool g_stiffBox = false;

    void __cdecl MonsterCommand(int argc, const char** argv, void*)
    {
        const char* const what = argc > 1 ? argv[1] : "list";

        // Spending the placements, so nothing is ever built from them. Not a death: the level's
        // tally of the killed does not move.
        if (Same(what, "clear"))
        {
            const int room = argc > 2 ? atoi(argv[2]) : -1;
            Console::Printf("monster: %d placement(s) spent in %s\n",
                Monsters::RemoveAllPlaced(room), room < 0 ? "this room" : "that room");
            return;
        }

        // The same thing said as a verdict, which reaches rooms nobody has been in yet
        if (Same(what, "none"))
        {
            g_refusing = !Same(argc > 2 ? argv[2] : "", "off");

            if (g_placeWatch == AMOD_INVALID_TOKEN)
            {
                g_placeWatch = Monsters::OnPlace([](const AmodMonsterPlaceInfo* info)
                {
                    if (g_refusing)
                    {
                        Console::Printf("  refused '%s' in room %d\n", info->name, info->roomIndex);
                    }
                    return g_refusing ? 0 : 1;
                });
            }

            Console::Print(g_placeWatch == AMOD_INVALID_TOKEN
                ? "monster: this loader is not asked before a monster is built\n"
                : (g_refusing ? "monster: none may appear - walk into a room to find it empty\n"
                              : "monster: allowed again\n"));
            return;
        }

        // Driving one of the game's own, which DefineBehaviour could never do
        if (Same(what, "drive"))
        {
            const char* const name = argc > 2 ? argv[2] : "spider";

            if (Same(name, "off"))
            {
                Console::Printf(Monsters::ClearBehaviour(g_driven)
                    ? "monster: '%s' handed back to the game\n"
                    : "monster: '%s' was not being driven\n", g_driven);
                return;
            }

            static bool instead = false;
            instead = argc > 3 && Same(argv[3], "instead");
            g_driven = name;

            const bool took = Monsters::SetBehaviour(name, [](const AmodMonsterThinkInfo* info)
            {
                // Rising every frame, so it is plain who is doing the moving
                Monsters::At(info->index)["y"].Set(info->position[1] + info->delta * 0.04f);

                return instead ? AMOD_MONSTER_INSTEAD_OF_GAME : AMOD_MONSTER_ALSO_GAME;
            });

            Console::Printf(took
                ? "monster: every '%s' is ours - it rises, and %s\n"
                : "monster: '%s' would not take\n", name,
                instead ? "the game's own tick no longer runs" : "the game still walks it");
            return;
        }

        // One monster of the room, driven on its own rather than its whole name. The number is the
        // one `monster live` prints, which is not the one `monster list` does.
        if (Same(what, "grab"))
        {
            const bool giving = Same(argc > 2 ? argv[2] : "", "off");
            const int index = giving ? (argc > 3 ? atoi(argv[3]) : 0)
                                     : (argc > 2 ? atoi(argv[2]) : 0);

            if (giving)
            {
                Console::Printf(Monsters::Return(index) ? "monster: %d given back\n"
                                                        : "monster: %d was not ours\n", index);
                return;
            }

            const bool took = Monsters::Borrow(index, [](const AmodMonsterThinkInfo* info)
            {
                Monsters::At(info->index)["y"].Set(info->position[1] + info->delta * 0.04f);
                return AMOD_MONSTER_INSTEAD_OF_GAME;
            });

            Console::Printf(took ? "monster: %d is ours alone - it rises and stops walking\n"
                                 : "monster: there is no monster %d\n", index);
            return;
        }

        // A monster of the mod's own making: the game's table has never heard of it
        if (Same(what, "mine"))
        {
            g_harmless = Same(argc > 2 ? argv[2] : "", "totem");
            g_quietTouch = Same(argc > 2 ? argv[2] : "", "quiet");
            g_stiffBox = Same(argc > 2 ? argv[2] : "", "stiff");

            const bool named = g_harmless || g_quietTouch || g_stiffBox;
            const int column = argc > (named ? 3 : 2) ? atoi(argv[named ? 3 : 2]) : 20;
            const int row = argc > (named ? 4 : 3) ? atoi(argv[named ? 4 : 3]) : 14;

            Console::Printf(g_kind != 0 && Monsters::PlaceKind(g_kind, column, row, "mine")
                ? "monster: one of ours put at %d %d - the game's table never heard of it\n"
                : "monster: it would not go at %d %d\n", column, row);
            return;
        }

        // Putting one where the player is not, and taking one out there
        if (Same(what, "far"))
        {
            const int room = argc > 2 ? atoi(argv[2]) : 0;
            const char* const name = argc > 3 ? argv[3] : "spider";
            const int column = argc > 4 ? atoi(argv[4]) : 20;
            const int row = argc > 5 ? atoi(argv[5]) : 14;

            Console::Printf(Monsters::PlaceIn(room, name, column, row, "far")
                ? "monster: '%s' put in room %d at %d %d - walk there and it is standing\n"
                : "monster: room %d would not take '%s' at %d %d\n", name, room, column, row);
            return;
        }

        if (Same(what, "killfar"))
        {
            const int room = argc > 2 ? atoi(argv[2]) : 0;
            const int index = argc > 3 ? atoi(argv[3]) : 0;

            Console::Printf(Monsters::KillIn(room, index)
                ? "monster: %d of room %d spent - the level counts it killed\n"
                : "monster: room %d has no %d to spend\n", index, room);
            return;
        }

        // What actually moves a monster, as against what only says it was doubled
        if (Same(what, "speed"))
        {
            const int index = argc > 2 ? atoi(argv[2]) : 0;

            if (argc > 3)
            {
                const float wanted = static_cast<float>(atof(argv[3]));
                Monsters::At(index)["speed"].Set(wanted);
                Monsters::At(index)["speed_min"].Set(wanted);
                Monsters::At(index)["speed_max"].Set(wanted);

                Monsters::At(index)["spawn_speed"].Set(wanted);

                Console::Printf("monster: %d now walks at %.3f, and still will after you leave\n",
                    index, wanted);
                return;
            }

            Console::Printf("  %d  speed %.3f  between %.3f and %.3f  hard says %.1f\n", index,
                Monsters::At(index)["speed"].Number(), Monsters::At(index)["speed_min"].Number(),
                Monsters::At(index)["speed_max"].Number(), Monsters::At(index)["hard"].Number());
            return;
        }

        // Frameset by name, on a monster the mod built itself
        if (Same(what, "anim"))
        {
            const int index = argc > 2 ? atoi(argv[2]) : 0;
            const char* const frameset = argc > 3 ? argv[3] : "move";

            Console::Printf(Monsters::PlayAnim(index, frameset)
                ? "monster: %d plays '%s'\n" : "monster: %d would not play '%s'\n", index, frameset);
            return;
        }

        // The numbers grab and drive act on: living monsters, which is a different list from the
        // room's placements and in a different order.
        if (Same(what, "live"))
        {
            const int alive = Monsters::Count();
            for (int at = 0; at < alive; ++at)
            {
                AmodMonster one = {};
                if (Monsters::Get(at, one))
                {
                    Console::Printf("  live %d  placed %d  %-14s tag '%s'\n",
                        one.index, one.placed, one.name, one.tag);
                }
            }

            Console::Printf("monster: %d standing here. Use the LIVE number for grab, speed, anim\n",
                alive);
            return;
        }

        const int held = Monsters::PlacedCount(-1);
        for (int at = 0; at < held; ++at)
        {
            AmodMonster one = {};
            if (Monsters::GetPlaced(-1, at, one))
            {
                Console::Printf("  placed %d  live %d  %-14s tag '%s'\n",
                    one.placed, one.index, one.name, one.tag);
            }
        }

        Console::Printf("monster: %d placed here. PLACED is for killfar and clear, LIVE for the rest\n",
            held);
        Console::Print("monster: list | clear [room] | none [off] | drive [name] [instead] | drive off\n");
        Console::Print("         live | grab [n] | grab off [n] | mine [totem|quiet|stiff] [col] [row]\n");
        Console::Print("         far <room> [name] [col] [row] | killfar <room> [n]\n");
        Console::Print("         speed <n> [value] | anim <n> [frameset]\n");
    }
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("monster", &MonsterCommand);

    g_kind = Monsters::RegisterKind("azforge.watcher", [](const AmodMonsterKindInfo* info)
    {
        if (info->event == AMOD_MONSTER_EVENT_BUILD)
        {
            info->body->box.min[0] = -0.05f;
            info->body->box.min[1] = -0.05f;
            info->body->box.min[2] = -0.05f;
            info->body->box.max[0] = 0.05f;
            info->body->box.max[1] = 0.05f;
            info->body->box.max[2] = 0.05f;

            info->body->actsLike = g_harmless ? nullptr : "spider";

            // 'mine stiff' keeps the box above instead of letting the animation stretch it
            info->body->keepHitBox = g_stiffBox ? 1 : 0;

            // And its animation, so it is not a still model. A totem names it on its own, since
            // it borrows no contact to take one from.
            info->body->animatedLike = "spider";
            return 1;
        }

        if (info->event == AMOD_MONSTER_EVENT_TOUCH)
        {
            Console::Printf("  something walked into it: %s, from side %d\n", info->touch->isPlayer != 0 ? "the player" : "not the player", info->touch->side);

            return g_quietTouch ? 1 : 0;
        }

        // Nothing of the game runs behind a kind, so this is the whole of its life
        Monsters::At(info->think->index)["facing"].Set(info->think->position[0] > 0.0f ? -1 : 1);
        return 0;
    });

    Log(AMOD_LOG_INFO, "%s is up", ModId());
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
