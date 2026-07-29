#pragma once

#include <WorldApi.h>

namespace Forge
{
    struct ModContext;

    // Loader side of the game world.
    //
    // The game keeps one level, a static object rather than a pointer, so reading it is a
    // matter of reaching a known address; what needs hooks is knowing when it changed.
    // Three detours give the three edges: Level_Load for a level arriving, Level_Free for one
    // going away, and Level_ActivateRoom for the player moving between rooms - the one route
    // by which a room becomes current, the first of a level included.
    //
    // Nothing here writes to the world. Available only when all three were located.
    class WorldApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodWorldApi*                                  Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
