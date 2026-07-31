#pragma once

#include <BlockExpressionApi.h>

namespace Forge
{
    struct ModContext;

    class BlockExpressionApi
    {
    public:
        static const AmodBlockExpressionApi*                        Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
