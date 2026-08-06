#include <ForgeApi.h>
#include <stdlib.h>
#include <string.h>

static const char* const kDefaultSound = "sounds/door_open.ogg";
static const char* const kDefaultLoop = "sounds/ffield_loop.ogg";

static AmodSoundId g_sound = AMOD_INVALID_SOUND;
static AmodVoice g_voice = AMOD_INVALID_VOICE;
static char g_name[128] = "";
static bool g_riding = false;

//   sload [name]           what the game calls that sound, and its id
static void __cdecl LoadCommand(int argc, const char** argv, void*)
{
    const char* const name = argc > 1 ? argv[1] : kDefaultSound;
    const AmodSoundId sound = Amod::Sound::Load(name);

    if (sound == AMOD_INVALID_SOUND)
    {
        Amod::Console::Printf("sload: %s\n", Amod::LastError());
        return;
    }

    g_sound = sound;
    strncpy_s(g_name, sizeof(g_name), name, _TRUNCATE);
    Amod::Console::Printf("'%s' is sound %u; splay and sat now use it\n", name, sound);
}

// The same id asked for twice is the same sound - the game finds rather than loads again.
static bool Ready(void)
{
    if (g_sound == AMOD_INVALID_SOUND)
    {
        g_sound = Amod::Sound::Load(kDefaultSound);
        strncpy_s(g_name, sizeof(g_name), kDefaultSound, _TRUNCATE);
    }
    return g_sound != AMOD_INVALID_SOUND;
}

//   splay                  heard the same wherever the player stands
static void __cdecl PlayCommand(int, const char**, void*)
{
    if (!Ready())
    {
        Amod::Console::Printf("splay: %s\n", Amod::LastError());
        return;
    }

    Amod::Console::Printf(Amod::Sound::Play(g_sound) ? "'%s' played flat\n" : "splay refused '%s'\n", g_name);
}

//   sat [x y z] [loop] [pitch]  heard from a place; keeps the voice for smove and sstop
static void __cdecl PlayAtCommand(int argc, const char** argv, void*)
{
    if (!Ready())
    {
        Amod::Console::Printf("sat: %s\n", Amod::LastError());
        return;
    }

    const int which = Amod::World::PlayerEntity();
    const Amod::World::Entity player = which >= 0 ? Amod::World::CurrentEntity(which) : Amod::World::Entity();

    float x = player.IsValid() ? player.GetX() : 0.0f;
    float y = player.IsValid() ? player.GetY() : 0.0f;
    float z = player.IsValid() ? player.GetZ() : 0.0f;

    if (argc > 3)
    {
        x = static_cast<float>(atof(argv[1]));
        y = static_cast<float>(atof(argv[2]));
        z = static_cast<float>(atof(argv[3]));
    }

    const bool loop = argc > 4 && atoi(argv[4]) != 0;
    const float pitch = argc > 5 ? static_cast<float>(atof(argv[5])) : 1.0f;

    if (g_voice != AMOD_INVALID_VOICE)
    {
        Amod::Sound::Stop(g_voice);
    }

    g_voice = Amod::Sound::PlayAt(g_sound, x, y, z, loop, pitch);
    if (g_voice == AMOD_INVALID_VOICE)
    {
        Amod::Console::Printf("sat: %s\n", Amod::LastError());
        return;
    }

    g_riding = false;
    Amod::Console::Printf("'%s' at %+.2f %+.2f %+.2f as voice %u, pitch %.2f%s\n",
                          g_name, x, y, z, g_voice, pitch, loop ? ", looping" : "");
}

//   sride [pitch]      looping on the player, so Move can be heard while walking
static void __cdecl RideCommand(int argc, const char** argv, void*)
{
    if (!Ready())
    {
        Amod::Console::Printf("sride: %s\n", Amod::LastError());
        return;
    }

    const int which = Amod::World::PlayerEntity();
    const Amod::World::Entity player = which >= 0 ? Amod::World::CurrentEntity(which) : Amod::World::Entity();

    if (!player.IsValid())
    {
        Amod::Console::Printf("sride: there is no player to sit on\n");
        return;
    }

    if (g_voice != AMOD_INVALID_VOICE)
    {
        Amod::Sound::Stop(g_voice);
    }

    const float pitch = argc > 1 ? static_cast<float>(atof(argv[1])) : 1.0f;

    g_voice = Amod::Sound::PlayAt(g_sound, player.GetX(), player.GetY(), player.GetZ(), true, pitch);
    if (g_voice == AMOD_INVALID_VOICE)
    {
        Amod::Console::Printf("sride: %s\n", Amod::LastError());
        return;
    }

    g_riding = true;
    Amod::Console::Printf("'%s' rides you at pitch %.2f; smove off leaves it where you stood\n",
                          g_name, pitch);
}

//   smove [x y z | ride | off]  where that voice is heard from
static void __cdecl MoveCommand(int argc, const char** argv, void*)
{
    if (g_voice == AMOD_INVALID_VOICE)
    {
        Amod::Console::Printf("smove: nothing is playing - run sat first\n");
        return;
    }

    if (argc > 1 && strcmp(argv[1], "ride") == 0)
    {
        g_riding = true;
        Amod::Console::Printf("voice %u rides the player; walk and listen\n", g_voice);
        return;
    }

    if (argc > 1 && strcmp(argv[1], "off") == 0)
    {
        g_riding = false;
        Amod::Console::Printf("voice %u stays where it is\n", g_voice);
        return;
    }

    if (argc < 4)
    {
        Amod::Console::Printf("smove <x> <y> <z> | ride | off\n");
        return;
    }

    g_riding = false;
    const float x = static_cast<float>(atof(argv[1]));
    const float y = static_cast<float>(atof(argv[2]));
    const float z = static_cast<float>(atof(argv[3]));

    Amod::Console::Printf(Amod::Sound::Move(g_voice, x, y, z) ? "voice %u is heard from %+.2f %+.2f %+.2f\n"
                                                              : "smove refused voice %u\n",
                          g_voice, x, y, z);
}

//  sstop       the voice, and nothing else
static void __cdecl StopCommand(int, const char**, void*)
{
    if (g_voice == AMOD_INVALID_VOICE)
    {
        Amod::Console::Printf("sstop: nothing is playing\n");
        return;
    }

    const bool stopped = Amod::Sound::Stop(g_voice);
    Amod::Console::Printf(stopped ? "voice %u stopped\n" : "sstop refused voice %u\n", g_voice);

    g_riding = false;
    g_voice = AMOD_INVALID_VOICE;
}

//   sroom                       the loops this room keeps
//   sroom add [name]            one of the mod's own, heard where the player stands
//   sroom <n> <x> <y> <z>       move one the room already had
static void __cdecl RoomCommand(int argc, const char** argv, void*)
{
    const int count = Amod::Sound::RoomSoundCount();

    if (argc < 2)
    {
        Amod::Console::Printf("sroom [add [name] | <n> <x> <y> <z>]; %d loop(s) here\n", count);

        for (int at = 0; at < count; ++at)
        {
            AmodRoomSound sound;
            if (Amod::Sound::GetRoomSound(at, sound))
            {
                Amod::Console::Printf("%2d  sound %-6u at %+.3f %+.3f %+.3f\n", at, sound.sound, sound.position[0], sound.position[1], sound.position[2]);
            }
        }
        Amod::Console::Printf("a room's loops start when it is walked into; leave and come back\n");
        return;
    }

    if (strcmp(argv[1], "add") == 0)
    {
        const char* const name = argc > 2 ? argv[2] : kDefaultLoop;
        const AmodSoundId sound = Amod::Sound::Load(name);

        if (sound == AMOD_INVALID_SOUND)
        {
            Amod::Console::Printf("sroom: %s\n", Amod::LastError());
            return;
        }

        const int which = Amod::World::PlayerEntity();
        const Amod::World::Entity player = which >= 0 ? Amod::World::CurrentEntity(which) : Amod::World::Entity();

        AmodRoomSound fresh = {};
        fresh.structSize = sizeof(fresh);
        fresh.sound = sound;
        fresh.position[0] = player.IsValid() ? player.GetX() : 0.0f;
        fresh.position[1] = player.IsValid() ? player.GetY() : 0.0f;
        fresh.position[2] = player.IsValid() ? player.GetZ() : 0.0f;

        const int put = Amod::Sound::AddRoomSound(fresh);
        if (put < 0)
        {
            Amod::Console::Printf("sroom: %s\n", Amod::LastError());
            return;
        }
        Amod::Console::Printf("'%s' is loop %d of this room; walk out and back to hear it\n", name, put);
        return;
    }

    if (argc < 5)
    {
        Amod::Console::Printf("sroom <n> <x> <y> <z>\n");
        return;
    }

    const int index = atoi(argv[1]);
    AmodRoomSound sound;

    if (!Amod::Sound::GetRoomSound(index, sound))
    {
        Amod::Console::Printf("sroom: %s\n", Amod::LastError());
        return;
    }

    sound.position[0] = static_cast<float>(atof(argv[2]));
    sound.position[1] = static_cast<float>(atof(argv[3]));
    sound.position[2] = static_cast<float>(atof(argv[4]));

    if (!Amod::Sound::SetRoomSound(index, sound))
    {
        Amod::Console::Printf("sroom: %s\n", Amod::LastError());
        return;
    }
    Amod::Console::Printf("loop %d moved; it is heard from there on the next visit\n", index);
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Amod::Console::AddCommand("sload", &LoadCommand);
    Amod::Console::AddCommand("splay", &PlayCommand);
    Amod::Console::AddCommand("sat", &PlayAtCommand);
    Amod::Console::AddCommand("sride", &RideCommand);
    Amod::Console::AddCommand("smove", &MoveCommand);
    Amod::Console::AddCommand("sstop", &StopCommand);
    Amod::Console::AddCommand("sroom", &RoomCommand);
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
    if (!g_riding || g_voice == AMOD_INVALID_VOICE)
    {
        return;
    }

    const int which = Amod::World::PlayerEntity();
    const Amod::World::Entity player = which >= 0 ? Amod::World::CurrentEntity(which) : Amod::World::Entity();

    if (player.IsValid())
    {
        Amod::Sound::Move(g_voice, player.GetX(), player.GetY(), player.GetZ());
    }
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
