#pragma once

#include <ShaderApi.h>

#include "src/Game/D3d8Hook.h"

namespace Forge
{
    struct ModContext;

    class ShaderApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodShaderApi*                                 Table();

        static void                                                 ForgetMod(ModContext* owner);

        static void                                                 OnDeviceEvent(DeviceEvent event, void* device);
    };
}
