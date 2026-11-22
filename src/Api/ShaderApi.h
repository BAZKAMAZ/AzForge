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

        static int                                                  DrawMesh(AmodModelId model, const float* world, AmodTextureId texture, uint32_t tint);
        static int                                                  DrawMeshes(const AmodShadedDraw* draws, int count);

        static void                                                 ForgetMod(ModContext* owner);

        static void                                                 OnDeviceEvent(DeviceEvent event, void* device);
    };
}
