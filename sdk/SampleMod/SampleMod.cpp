#include <ForgeApi.h>

#include <stdlib.h>
#include <string.h>

using namespace Amod;

void Keep(char* out, int capacity, const char* text)
{
    int at = 0;

    while (text != nullptr && text[at] != '\0' && at < capacity - 1)
    {
        out[at] = text[at];
        ++at;
    }

    out[at] = '\0';
}

void __cdecl ConfigCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Console::Printf("cfg: %d key(s); 'cfg <name>' reads one, 'cfg <name> <value>' writes it\n",
            Config::Count());
        return;
    }

    // The console's words are the game's own scratch, and asking the game anything overwrites them
    char name[128] = {};
    Keep(name, sizeof(name), argv[1]);

    if (strcmp(name, "list") == 0)
    {
        for (int at = 0; at < Config::Count(); ++at)
        {
            const char* const key = Config::NameAt(at);
            Console::Printf("  %-24s \"%s\"\n", key, Config::Text(key));
        }
        return;
    }

    if (argc > 2)
    {
        char value[260] = {};
        Keep(value, sizeof(value), argv[2]);

        // Added rather than refused when it is new, which is the whole point of the command
        if (!Config::Has(name))
        {
            Config::Add(name, value, AMOD_CONFIG_SAVED);
            Console::Printf("cfg: '%s' is new, added as \"%s\" and will be saved\n", name, value);
            return;
        }

        Console::Printf(Config::Set(name, value)
            ? "cfg: '%s' is now \"%s\"\n" : "cfg: '%s' would not take \"%s\"\n", name, value);
        return;
    }

    AmodConfigKey key = {};

    if (!Config::Get(name, key))
    {
        Console::Printf("cfg: there is no '%s'\n", name);
        return;
    }

    Console::Printf("cfg: %s = \"%s\" (%.3f), default \"%s\", flags 0x%02X%s\n", key.name, key.value,
        key.number, key.fallback, key.flags,
        (key.flags & AMOD_CONFIG_SAVED) != 0 ? " - written to the .cfg" : " - this run only");
}

// Proves the round trip: the file on disk is what Config_Write printed, not a copy of anything
void __cdecl ConfigSaveCommand(int, const char**, void*)
{
    Console::Printf(Config::Save()
        ? "cfgsave: written - look in Azangara.cfg\n" : "cfgsave: the config object is not up\n");
}

// scores.dat, grouped by castle rather than kept in one run
void __cdecl ScoresCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Console::Printf("scores: %d entr(ies) in %d castle(s)\n", Scores::Count(),
            Scores::CastleCount());

        int shown = 0;

        for (int at = 0; at < Scores::Count() && shown < 16; ++at)
        {
            AmodScoreEntry entry = {};

            if (!Scores::Get(at, entry) || entry.inCastle != 0)
            {
                continue;
            }

            Console::Printf("  castle %d: %d entr(ies), level \"%s\"\n", entry.castle,
                Scores::CastleEntryCount(entry.castle), entry.level);
            ++shown;
        }

        Console::Print("scores: list | add <level> <name> <points> | save\n");
        return;
    }

    char what[64] = {};
    Keep(what, sizeof(what), argv[1]);

    if (strcmp(what, "list") == 0)
    {
        for (int at = 0; at < Scores::Count(); ++at)
        {
            AmodScoreEntry entry = {};

            if (Scores::Get(at, entry))
            {
                // 'kept' or 'DROPPED': an entry never written on is thrown away by the next save
                Console::Printf("  %2d  castle %d.%-2d  %-16s %-24s %8d  %.1fs  %s\n", at,
                    entry.castle, entry.inCastle, entry.name, entry.level, entry.score, entry.time,
                    entry.filled != 0 ? "kept" : "DROPPED on save");
            }
        }
        return;
    }

    if (strcmp(what, "save") == 0)
    {
        Console::Printf(Scores::Save()
            ? "scores: written to scores.dat\n" : "scores: the table is not up\n");
        return;
    }

    if (strcmp(what, "add") == 0 && argc > 4)
    {
        char level[64] = {};
        char name[64] = {};
        Keep(level, sizeof(level), argv[2]);
        Keep(name, sizeof(name), argv[3]);

        const int points = atoi(argv[4]);
        const int castle = Scores::CastleOfLevel(level);
        const int at = Scores::Add(level, name, 0.0f, points);

        Console::Printf(at >= 0
            ? "scores: '%s' went in at %d, castle %d, with %d\n"
            : "scores: refused - '%s' (%d), castle %d, %d beat nothing there\n",
            name, at, castle, points);

        // Said with both strings side by side, so a mismatch is read rather than guessed at
        if (at < 0)
        {
            Console::Printf("scores: %s\n", LastError());
            Console::Printf("scores: you asked for \"%s\"\n", level);

            AmodScoreEntry first = {};

            if (Scores::Get(0, first))
            {
                Console::Printf("scores: the table holds e.g. \"%s\" (%d points there)\n",
                    first.level, first.score);
            }
        }
        return;
    }

    if (strcmp(what, "test") == 0)
    {
        if (Scores::Count() <= 0)
        {
            Console::Print("scores: the table is empty, so there is nothing to write over\n");
            return;
        }

        AmodScoreEntry before = {};
        Scores::Get(0, before);

        char keptName[64] = {};
        Keep(keptName, sizeof(keptName), before.name);

        const int keptPoints = before.score;

        const bool named = Scores::SetName(0, "AzForgeTest");
        const bool sameName = strcmp(Scores::GetName(0), "AzForgeTest") == 0;

        const bool pointed = Scores::SetPoint(0, 12345);
        const bool samePoint = Scores::GetPoint(0) == 12345;

        Console::Printf("scores: SetName %s, GetName %s\n", named ? "ok" : "REFUSED",
            sameName ? "matches" : "DOES NOT MATCH");
        Console::Printf("scores: SetPoint %s, GetPoint %s (%d)\n", pointed ? "ok" : "REFUSED",
            samePoint ? "matches" : "DOES NOT MATCH", Scores::GetPoint(0));

        // Put back what was there, so the test costs the player nothing
        Scores::SetName(0, keptName);
        Scores::SetPoint(0, keptPoints);

        Console::Printf("scores: put back \"%s\" %d; Remove is not tested here because it drops a "
            "real entry - use 'scores remove <n>'\n", keptName, keptPoints);
        return;
    }

    if (strcmp(what, "remove") == 0 && argc > 2)
    {
        const int index = atoi(argv[2]);
        const int was = Scores::Count();

        Console::Printf(Scores::Remove(index)
            ? "scores: %d dropped, %d left (was %d)\n" : "scores: %d would not drop (%d, was %d)\n",
            index, Scores::Count(), was);
        return;
    }

    Console::Print("scores: list | add <level> <name> <points> | test | remove <n> | save\n");
}

// packs.sav, the levels the player has opened
void __cdecl ProgressCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Console::Printf("prog: %d level(s) open, pack %d of %d; 'prog list', 'prog open <id>', "
            "'prog close <id>', 'prog save'\n",
            Progress::Count(), Progress::Current(), Progress::PackCount());
        return;
    }

    char what[64] = {};
    Keep(what, sizeof(what), argv[1]);

    if (strcmp(what, "list") == 0)
    {
        for (int at = 0; at < Progress::Count(); ++at)
        {
            Console::Printf("  %2d  %s\n", at, Progress::NameAt(at));
        }
        return;
    }

    if (strcmp(what, "save") == 0)
    {
        Console::Printf(Progress::Save()
            ? "prog: written to packs.sav\n" : "prog: the pack list is not up\n");
        return;
    }

    if (argc > 2)
    {
        char id[260] = {};
        Keep(id, sizeof(id), argv[2]);

        if (strcmp(what, "open") == 0)
        {
            Console::Printf("prog: '%s' is open, at %d\n", id, Progress::Open(id));
            return;
        }
        if (strcmp(what, "close") == 0)
        {
            Console::Printf(Progress::Close(id)
                ? "prog: '%s' is closed again\n" : "prog: '%s' was not open\n", id);
            return;
        }
    }

    if (strcmp(what, "test") == 0)
    {
        const int was = Progress::Count();

        // Add and Remove and SetId and GetId, each read back rather than believed
        const int at = Progress::Add("AZFORGE_TEST_LEVEL");
        const bool added = at >= 0 && Progress::Count() == was + 1;
        const bool found = Progress::IsOpen("AZFORGE_TEST_LEVEL");

        Console::Printf("prog: Add %s at %d, count %d -> %d\n", added ? "ok" : "FAILED", at, was,
            Progress::Count());
        Console::Printf("prog: IsOpen %s, GetId(%d) = \"%s\"\n", found ? "yes" : "NO", at,
            Progress::GetId(at));

        const bool renamed = at >= 0 && Progress::SetId(at, "AZFORGE_TEST_RENAMED");
        const bool sameId = at >= 0 && strcmp(Progress::GetId(at), "AZFORGE_TEST_RENAMED") == 0;

        Console::Printf("prog: SetId %s, GetId %s\n", renamed ? "ok" : "REFUSED",
            sameId ? "matches" : "DOES NOT MATCH");

        const bool dropped = at >= 0 && Progress::Remove(at);

        Console::Printf("prog: Remove %s, count back to %d (was %d)%s\n", dropped ? "ok" : "FAILED",
            Progress::Count(), was, Progress::Count() == was ? "" : " - MISMATCH");

        // Remove blocks the id it dropped, and the test's id must not stay blocked
        Progress::Block("AZFORGE_TEST_RENAMED", false);
        Progress::Block("AZFORGE_TEST_LEVEL", false);

        Console::Print("prog: nothing of the game's was touched - the test used ids of its own\n");
        return;
    }

    Console::Print("prog: list | open <id> | close <id> | test | save\n");
}

void __cdecl SlotCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        for (int slot = 0; slot < 10; ++slot)
        {
            if (Progress::SlotExists(slot))
            {
                // The name is copied before the level is asked for: both come out of one buffer
                char name[80] = {};
                Keep(name, sizeof(name), Progress::SlotName(slot));

                Console::Printf("  %d  \"%s\"  on %s\n", slot, name, Progress::SlotLevel(slot));
            }
        }

        Console::Print("slot: save <n> <name> | load <n> | drop <n>\n");
        return;
    }

    char what[32] = {};
    Keep(what, sizeof(what), argv[1]);

    const int slot = argc > 2 ? atoi(argv[2]) : 0;

    if (strcmp(what, "save") == 0)
    {
        char name[80] = {};
        Keep(name, sizeof(name), argc > 3 ? argv[3] : "mod");

        Console::Printf(Progress::SaveSlot(slot, name)
            ? "slot: %d written as \"%s\"\n" : "slot: %d would not be written (\"%s\")\n", slot, name);
        return;
    }

    if (strcmp(what, "load") == 0)
    {
        Console::Printf(Progress::LoadSlot(slot)
            ? "slot: %d loaded\n" : "slot: %d would not load\n", slot);
        return;
    }

    if (strcmp(what, "drop") == 0)
    {
        Console::Printf(Progress::DeleteSlot(slot)
            ? "slot: %d deleted\n" : "slot: %d holds nothing\n", slot);
        return;
    }

    Console::Print("slot: save <n> <name> | load <n> | drop <n>\n");
}

char g_questNote[128] = "nothing yet";
int g_questStep = 0;

void __cdecl OnSlotSave(int slot, void*)
{
    char kept[160] = {};
    const int size = snprintf(kept, sizeof(kept), "%d|%s", g_questStep, g_questNote);

    Console::Printf("quest: slot %d takes step %d, \"%s\"\n", slot, g_questStep, g_questNote);
    Progress::WriteSlotData(kept, size + 1);
}

void __cdecl OnSlotLoad(int slot, const void* bytes, int size, void*)
{
    // Told even when there is nothing: a save made before this mod existed reads exactly like that
    if (bytes == nullptr || size <= 0)
    {
        Console::Printf("quest: slot %d holds nothing of this mod's\n", slot);
        return;
    }

    char kept[160] = {};
    Keep(kept, sizeof(kept), static_cast<const char*>(bytes));

    const char* const bar = strchr(kept, '|');

    g_questStep = atoi(kept);
    Keep(g_questNote, sizeof(g_questNote), bar != nullptr ? bar + 1 : "");

    Console::Printf("quest: slot %d gave back step %d, \"%s\"\n", slot, g_questStep, g_questNote);
}

void __cdecl LevelChangeCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Console::Printf("chg: %d remembered\n", Progress::LevelChangeCount());

        for (int at = 0; at < Progress::LevelChangeCount(); ++at)
        {
            AmodLevelChange held = {};

            if (Progress::LevelChangeAt(at, held))
            {
                Console::Printf("  %-28s %-16s = %d\n", held.level, held.name, held.value);
            }
        }

        Console::Print("chg: <name> <value> sets it on the level being played\n");
        return;
    }

    AmodWorldLevel level = {};

    if (!World::GetLevel(level) || level.script == nullptr || level.script[0] == '\0')
    {
        Console::Print("chg: no level is being played, so there is nothing to remember it against\n");
        return;
    }

    // Copied before anything else is asked of the game, which would write over the console's words
    char script[260] = {};
    char name[64] = {};
    Keep(script, sizeof(script), level.script);
    Keep(name, sizeof(name), argv[1]);

    if (argc < 3)
    {
        Console::Printf("chg: %s in %s is %d\n", name, script,
            Progress::GetLevelChange(script, name, -1));
        return;
    }

    const int value = atoi(argv[2]);

    Console::Printf(Progress::SetLevelChange(script, name, value)
        ? "chg: %s in %s set to %d - 'slot save 1 x' keeps it\n"
        : "chg: %s in %s would not take %d\n", name, script, value);
}

void __cdecl ThingCommand(int argc, const char** argv, void*)
{
    const int room = World::CurrentRoom();

    if (argc > 2)
    {
        const int index = atoi(argv[1]);
        const int value = atoi(argv[2]);

        AmodRoomThing thing = {};

        if (!World::GetRoomThing(index, thing))
        {
            Console::Printf("thing: this room has no %d\n", index);
            return;
        }

        thing.values[0] = value;

        Console::Printf(World::SetRoomThing(index, thing)
            ? "thing: %d set to %d - save the slot and it comes back\n"
            : "thing: %d would not take %d\n", index, value);
        return;
    }

    Console::Printf("thing: room %d keeps %d, and takes %d byte(s) in a save\n", room,
        World::RoomThingCount(), World::RoomSavedSize());

    for (int at = 0; at < World::RoomThingCount(); ++at)
    {
        AmodRoomThing thing = {};

        if (!World::GetRoomThing(at, thing))
        {
            continue;
        }

        if (thing.tag == 2)
        {
            Console::Printf("  %2d  tag %-3d %2d byte(s)  %d %d %d\n", at, thing.tag, thing.saved,
                thing.values[0], thing.values[1], thing.values[2]);
        }
        else
        {
            Console::Printf("  %2d  tag %-3d %2d byte(s)  %d\n", at, thing.tag, thing.saved,
                thing.values[0]);
        }
    }

    AmodRoomMemory memory = {};

    if (World::GetRoomMemory(memory))
    {
        Console::Printf("  room line: %s, state %d, extra %s (%d)\n",
            memory.isSaved != 0 ? "saved" : "NOT saved", memory.state,
            memory.hasExtra != 0 ? "yes" : "no", memory.extra);
    }

    // The whole level, so the total can be checked against the file
    int total = 0;

    for (int at = 0; at < World::RoomCount(); ++at)
    {
        total += World::RoomSavedSize(at);
    }

    Console::Printf("thing: the level section takes %d byte(s) of things and lines, over %d room(s)\n",
        total, World::RoomCount());
}

void __cdecl QuestCommand(int argc, const char** argv, void*)
{
    if (argc > 1)
    {
        char said[128] = {};
        Keep(said, sizeof(said), argv[1]);

        Keep(g_questNote, sizeof(g_questNote), said);
        ++g_questStep;
    }

    Console::Printf("quest: step %d, \"%s\" - 'slot save 1 x' keeps it, 'slot load 1' brings it back\n",
        g_questStep, g_questNote);
}

void __cdecl StatsCommand(int argc, const char** argv, void*)
{
    AmodLevelStats stats = {};

    if (!World::GetLevelStats(stats))
    {
        Console::Print("stats: no level is being played\n");
        return;
    }

    if (argc > 2)
    {
        // 'stats monsters 3' - to see the end-of-level screen and the save follow
        char which[32] = {};
        Keep(which, sizeof(which), argv[1]);

        const int value = atoi(argv[2]);

        if (strcmp(which, "crystals") == 0) { stats.crystalsTaken = value; }
        else if (strcmp(which, "bonuses") == 0) { stats.bonusesTaken = value; }
        else if (strcmp(which, "super") == 0) { stats.superTaken = value; }
        else if (strcmp(which, "monsters") == 0) { stats.monstersKilled = value; }
        else if (strcmp(which, "light") == 0) { stats.lightRoomsSeen = value; }
        else if (strcmp(which, "dark") == 0) { stats.darkRoomsSeen = value; }
        else if (strcmp(which, "secret") == 0) { stats.secretRoomsSeen = value; }
        else if (strcmp(which, "eighth") == 0) { stats.eighthTaken = value; }
        else
        {
            Console::Print("stats: crystals | bonuses | super | monsters | light | dark | secret | eighth\n");
            return;
        }

        World::SetLevelStats(stats);
    }

    Console::Printf("stats: crystals %d/%d  bonuses %d/%d  super %d/%d  monsters %d/%d\n",
        stats.crystalsTaken, stats.crystalsTotal, stats.bonusesTaken, stats.bonusesTotal,
        stats.superTaken, stats.superTotal, stats.monstersKilled, stats.monstersTotal);

    Console::Printf("stats: light %d/%d  dark %d/%d  secret %d/%d  eighth %d/%d\n",
        stats.lightRoomsSeen, stats.lightRoomsTotal, stats.darkRoomsSeen, stats.darkRoomsTotal,
        stats.secretRoomsSeen, stats.secretRoomsTotal, stats.eighthTaken, stats.eighthTotal);
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("stats", &StatsCommand);

    Progress::OnSlotSave(&OnSlotSave);
    Progress::OnSlotLoad(&OnSlotLoad);

    Console::AddCommand("thing", &ThingCommand);
    Console::AddCommand("chg", &LevelChangeCommand);
    Console::AddCommand("quest", &QuestCommand);
    Console::AddCommand("scores", &ScoresCommand);
    Console::AddCommand("prog", &ProgressCommand);
    Console::AddCommand("slot", &SlotCommand);
    Console::AddCommand("cfg", &ConfigCommand);
    Console::AddCommand("cfgsave", &ConfigSaveCommand);
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
