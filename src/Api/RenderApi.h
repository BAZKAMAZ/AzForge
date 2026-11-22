#pragma once

#include <RenderApi.h>

namespace Forge
{
    struct ModContext;

    class RenderApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        // Whether the two conditions its own 2D needs are both true: the slot the game fills with
        // its sprite draw, and a scene with sprite states in it. False early in a run, which is
        // exactly when a mod wants to draw a loading screen.
        static bool                                                 CanDraw2D();

        static const AmodRenderApi*                                 Table();

        static void                                                 ForgetMod(ModContext* owner);

        static void                                                 AddInternalPresent(void(__cdecl* draw)());

        // Run once the frame is on screen and no scene is open, where the game may put up its own.
        static void                                                 AddAfterPresent(void(__cdecl* tick)());
        static bool                                                 IsPresentHooked();
    };
}
