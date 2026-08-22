#pragma once

#include <MmdApi.h>

#include <stdint.h>

namespace Forge
{
    struct ModContext;

    class MmdApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodMmdApi*                                    Table();

        static int                                                  ReadAnimState(const uint8_t* state, AmodAnimState* out);

        static AmodModelId                                          IdFor(void* model);
        static void*                                                ModelFor(AmodModelId id);

        static void*                                                AnimationFor(AmodAnimationId id);

        static void                                                 EnterDraw();
        static void                                                 LeaveDraw();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
