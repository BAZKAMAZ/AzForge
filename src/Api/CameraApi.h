#pragma once

#include <CameraApi.h>

namespace Forge
{
    struct ModContext;

    class CameraApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static void                                                 ForgetMod(ModContext* owner);

        static const AmodCameraApi*                                 Table();
    };
}
