#include <ForgeApi.h>

#include <stdlib.h>
#include <string.h>

using namespace Amod;

void __cdecl MonsterAnimCommand(int argc, const char** argv, void*)
{
    const int index = argc > 1 ? atoi(argv[1]) : 0;

    AmodAnimState state = {};
    if (!Monsters::GetAnim(index, state))
    {
        Console::Printf("monsteranim: there is no monster %d\n", index);
        return;
    }

    Console::Printf("monsteranim: %d plays '%s' frame %d to %d at %.2f, speed %.2f%s%s\n", index,
        state.frameset, state.frameA, state.frameB, state.blend, state.speed,
        state.stopped ? ", stopped" : "", state.finished ? ", finished" : "");
}

void __cdecl MonsterYawCommand(int argc, const char** argv, void*)
{
    const int index = argc > 1 ? atoi(argv[1]) : 0;

    if (argc < 3)
    {
        Console::Printf("monsteryaw: %d looks along %.2f\n", index, Monsters::At(index)["yaw"].Number());
        return;
    }

    const float yaw = static_cast<float>(atof(argv[2]));

    Console::Printf(Monsters::At(index)["yaw"].Set(yaw)
        ? "monsteryaw: %d turned to %.2f\n" : "monsteryaw: %d would not turn\n", index, yaw);
}

void __cdecl PlayerBurnCommand(int, const char**, void*)
{
    const int lives = Player::Lives();

    Console::Printf(Player::Burn()
        ? "playerburn: alight, and that was one of his %d\n"
        : "playerburn: he is already dead or already alight (%d left)\n", lives);
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("monsteranim", &MonsterAnimCommand);
    Console::AddCommand("monsteryaw", &MonsterYawCommand);
    Console::AddCommand("playerburn", &PlayerBurnCommand);
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
