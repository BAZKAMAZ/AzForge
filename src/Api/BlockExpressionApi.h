#pragma once

#include <BlockExpressionApi.h>

namespace Forge
{
    struct ModContext;

    // Loader side of the block/expression interface.
    //
    // Pure mod-owned data: it hooks nothing in the game and needs no game symbols. Each mod's
    // trees live in a store keyed by its context, so one mod cannot reach another's, and
    // everything a mod made is released when it unloads.
    class BlockExpressionApi
    {
    public:
        static const AmodBlockExpressionApi*                        Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
