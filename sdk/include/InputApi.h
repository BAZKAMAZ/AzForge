#ifndef AZFORGE_INPUT_API_H
#define AZFORGE_INPUT_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

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

    /* Key currently down (VK code; mouse buttons included). */
    int (__cdecl* IsKeyDown)(AmodMod mod, int key);

    /* Edge: went down / up since previous frame. */
    int (__cdecl* WasKeyPressed)(AmodMod mod, int key);
    int (__cdecl* WasKeyReleased)(AmodMod mod, int key);

    /* Cursor in 640x480 menu space. Either pointer may be null. */
    void (__cdecl* GetCursor)(AmodMod mod, float* x, float* y);
    void (__cdecl* SetCursor)(AmodMod mod, float x, float y);

    /* Pointer over the game window. */
    int (__cdecl* IsCursorInWindow)(AmodMod mod);

    /* Game is ignoring input (console open, key held from before a screen, etc.). */
    int (__cdecl* IsSwallowed)(AmodMod mod);

    /* Wheel notches last frame: + away, − toward, 0 most frames. */
    int (__cdecl* WheelDelta)(AmodMod mod);
} AmodInputApi;

#ifdef __cplusplus
}
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

        /* Wheel notches last frame; 0 most frames. */
        inline int                                                  Wheel()
        {
            return (g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi))
                       ? g_input->WheelDelta(g_self)
                       : 0;
        }

        /* Cursor inside a centre+size rectangle (640x480 space). */
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
