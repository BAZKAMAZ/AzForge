#pragma once

#include <ParticlesApi.h>

namespace Forge
{
    struct ModContext;

    class ParticlesApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static void                                                 ForgetMod(ModContext* owner);

        static const AmodParticlesApi*                              Table();
    };
}
