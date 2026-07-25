#pragma once

#include <RenderApi.h>

namespace Forge
{
    struct ModContext;

    // Loader side of the render overlay.
    //
    // A single detour on Render_Present, the last render call each frame, drives every
    // registered mod callback while the scene is still open, then lets the game end the scene
    // and present. The device the mod draws through is the game's own, read out of the render
    // manager. Available only when Render_Present was located.
    class RenderApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodRenderApi*                                 Table();

        static void                                                 ForgetMod(ModContext* owner);
    };
}
