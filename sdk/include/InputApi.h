/*
 * InputApi.h - the keyboard and the mouse, as the game sees them.
 *
 * This is deliberately not a second input path. A mod calling GetAsyncKeyState for itself
 * would answer questions the game is not asking: it would see keys while the console is open,
 * while the window is not focused, and while the game is deliberately swallowing a key that
 * was already held down when a screen appeared. A mod acting on those disagrees with the game
 * in front of the same player.
 *
 * So this reads the table the game fills each frame in Input_Update, and the cursor through
 * the game's own conversion into the 640x480 space every menu coordinate is in - which is also
 * what keeps a mod's hit-testing lined up with the menus at any window size.
 *
 * The mouse buttons are keys. The game stores them in the same table under VK_LBUTTON and
 * VK_RBUTTON, so there is no separate mouse state here because there is no separate mouse
 * state in the game.
 *
 * Reached through AmodApi::input, null on a loader older than API version 20.
 */

#ifndef AZFORGE_INPUT_API_H
#define AZFORGE_INPUT_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The virtual-key codes worth naming. Any other Windows VK_ code works too - the table covers
 * 0 to 254 - these are just the ones a menu tends to want. */
#define AMOD_KEY_LBUTTON                                            0x01
#define AMOD_KEY_RBUTTON                                            0x02
#define AMOD_KEY_MBUTTON                                            0x04
#define AMOD_KEY_BACK                                               0x08
#define AMOD_KEY_TAB                                                0x09
#define AMOD_KEY_RETURN                                             0x0D
#define AMOD_KEY_SHIFT                                              0x10
#define AMOD_KEY_CONTROL                                            0x11
#define AMOD_KEY_ESCAPE                                             0x1B
#define AMOD_KEY_SPACE                                              0x20
#define AMOD_KEY_LEFT                                               0x25
#define AMOD_KEY_UP                                                 0x26
#define AMOD_KEY_RIGHT                                              0x27
#define AMOD_KEY_DOWN                                               0x28

typedef struct AmodInputApi
{
    uint32_t structSize;

    /* Whether the key is down right now, by virtual-key code. Mouse buttons included, under
     * AMOD_KEY_LBUTTON and the rest. */
    int (__cdecl* IsKeyDown)(AmodMod mod, int key);

    /* Whether it went down since the previous frame - the edge rather than the level, which
     * is what a button press is. Answered from the loader's own copy of the previous frame,
     * because the game keeps only the current state. */
    int (__cdecl* WasKeyPressed)(AmodMod mod, int key);
    int (__cdecl* WasKeyReleased)(AmodMod mod, int key);

    /* The cursor, in the 640x480 space menu coordinates use. Either pointer may be null. */
    void (__cdecl* GetCursor)(AmodMod mod, float* x, float* y);

    /* Puts the cursor somewhere, the way the game does when it clamps it back into the
     * window. Use sparingly: taking the pointer away from a player is rarely welcome. */
    void (__cdecl* SetCursor)(AmodMod mod, float x, float y);

    /* Whether the pointer is over the game's window at all. A drag should stop caring about
     * the cursor when this goes false rather than following it off the screen. */
    int (__cdecl* IsCursorInWindow)(AmodMod mod);

    /* Whether the game is currently ignoring input - a key held from before a screen opened,
     * or the console taking it. A mod that acts anyway is acting on something the player did
     * not mean for it. */
    int (__cdecl* IsSwallowed)(AmodMod mod);

    /* Notches the wheel turned during the last frame: positive away from the player, negative
     * towards them, zero most frames. The game itself has no idea the wheel exists - it polls
     * GetAsyncKeyState, and the wheel is not a key - so the loader reads it off the window's
     * messages and hands it on. */
    int (__cdecl* WheelDelta)(AmodMod mod);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodInputApi;

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Input
    {
        inline const AmodInputApi* g_input = nullptr;
        inline AmodMod g_self = nullptr;

        inline void                                                 Attach(const AmodInputApi* api, AmodMod self)
        {
            g_input = api;
            g_self = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_input != nullptr;
        }

        inline bool                                                 IsDown(int key)
        {
            return g_input != nullptr && g_input->IsKeyDown(g_self, key) != 0;
        }

        inline bool                                                 WasPressed(int key)
        {
            return g_input != nullptr && g_input->WasKeyPressed(g_self, key) != 0;
        }

        inline bool                                                 WasReleased(int key)
        {
            return g_input != nullptr && g_input->WasKeyReleased(g_self, key) != 0;
        }

        inline void                                                 Cursor(float& x, float& y)
        {
            x = 0.0f;
            y = 0.0f;
            if (g_input != nullptr)
            {
                g_input->GetCursor(g_self, &x, &y);
            }
        }

        inline void                                                 SetCursor(float x, float y)
        {
            if (g_input != nullptr)
            {
                g_input->SetCursor(g_self, x, y);
            }
        }

        inline bool                                                 IsCursorInWindow()
        {
            return g_input != nullptr && g_input->IsCursorInWindow(g_self) != 0;
        }

        inline bool                                                 IsSwallowed()
        {
            return g_input != nullptr && g_input->IsSwallowed(g_self) != 0;
        }

        /* Wheel notches turned during the last frame; zero most frames. */
        inline int                                                  Wheel()
        {
            return (g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi))
                       ? g_input->WheelDelta(g_self)
                       : 0;
        }

        /* Whether the cursor is inside a rectangle given the way the menus give one: a centre
         * and a size, in the same 640x480 space. */
        inline bool                                                 CursorIn(float cx, float cy, float width, float height)
        {
            float x = 0.0f;
            float y = 0.0f;
            Cursor(x, y);
            return x >= cx - width * 0.5f && x <= cx + width * 0.5f &&
                   y >= cy - height * 0.5f && y <= cy + height * 0.5f;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_INPUT_API_H */
