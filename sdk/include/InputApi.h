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

/* The eight joystick keys carry on from the keyboard's 256. */
#define AMOD_KEY_JOYSTICK                                           0x100
#define AMOD_KEY_JOY_LEFT                                           0x100
#define AMOD_KEY_JOY_RIGHT                                          0x101
#define AMOD_KEY_JOY_DOWN                                           0x102
#define AMOD_KEY_JOY_UP                                             0x103
#define AMOD_KEY_JOY1                                               0x104

/* What a key does when it is pressed. */
typedef enum AmodKeyBind
{
    AMOD_KEY_UNBOUND = 0,
    AMOD_KEY_CVAR    = 1,
    AMOD_KEY_COMMAND = 2
} AmodKeyBind;

typedef struct AmodKeyInfo
{
    uint32_t structSize;

    const char* name;       /* what the game calls it, or null for a key it never named */
    int isJoystick;

    int bind;               /* AmodKeyBind */
    const char* target;     /* the cvar or the command, null when nothing is bound */
    int held;               /* it acts while the key is down */
    int paramCount;         /* parameters the command was bound with; GetKeyParam reads them */

} AmodKeyInfo;

AMOD_LAST_FIELD(AmodKeyInfo, paramCount, 0x18);

typedef struct AmodInputApi
{
    uint32_t structSize;

    /* Key currently down (VK code; mouse buttons included). */
    int (__cdecl* IsKeyDown)(AmodMod mod, int key);

    /* Edge: went down / up since previous frame. */
    int (__cdecl* WasKeyPressed)(AmodMod mod, int key);
    int (__cdecl* WasKeyReleased)(AmodMod mod, int key);

    /* Cursor in 640x480 menu space, y from the top. */
    void (__cdecl* GetCursor)(AmodMod mod, float* x, float* y);
    void (__cdecl* SetCursor)(AmodMod mod, float x, float y);

    /* Pointer over the game window. */
    int (__cdecl* IsCursorInWindow)(AmodMod mod);

    /* Game is ignoring input (console open, key held from before a screen, etc.). */
    int (__cdecl* IsSwallowed)(AmodMod mod);

    /* Wheel notches last frame: + away, − toward, 0 most frames. */
    int (__cdecl* WheelDelta)(AmodMod mod);

    /* All game keys: keyboard, then joystick. */
    int(__cdecl* KeyCount)(AmodMod mod);
    int(__cdecl* GetKey)(AmodMod mod, int key, AmodKeyInfo* out);

    /* Finds a key by name; -1 if unknown. */
    int(__cdecl* FindKey)(AmodMod mod, const char* name);

    /* Returns a bound command parameter, or null. */
    const char* (__cdecl* GetKeyParam)(AmodMod mod, int key, int param);

    /* held keeps the binding active while pressed. */
    int(__cdecl* BindCommand)(AmodMod mod, int key, const char* command, int argc, const char** argv, int held);
    int(__cdecl* BindCvar)(AmodMod mod, int key, const char* cvar, int held);
    int(__cdecl* Unbind)(AmodMod mod, int key);

    /* Whether the joystick was captured at startup. */
    int(__cdecl* HasJoystick)(AmodMod mod);

    /* Keeps the key binding between runs. */
    int(__cdecl* Keep)(AmodMod mod, int key, int keep, int protect);

    /* Forces keyboard input. */
    int(__cdecl* PressKey)(AmodMod mod, int key);
    int(__cdecl* HoldKey)(AmodMod mod, int key, int down, int exclusive);
    int(__cdecl* IsKeyForced)(AmodMod mod, int key);

    /* Windows cursor. */
    int(__cdecl* ShowSystemCursor)(AmodMod mod, int show);
} AmodInputApi;

AMOD_LAST_FIELD(AmodInputApi, ShowSystemCursor, 0x54);

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

        inline int                                                  KeyCount()
        {
            return (g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi))
                       ? g_input->KeyCount(g_self)
                       : 0;
        }

        inline bool                                                 GetKey(int key, AmodKeyInfo* out)
        {
            if (g_input == nullptr || g_input->structSize < sizeof(AmodInputApi) || out == nullptr)
            {
                return false;
            }
            out->structSize = sizeof(AmodKeyInfo);
            return g_input->GetKey(g_self, key, out) != 0;
        }

        inline int                                                  FindKey(const char* name)
        {
            return (g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi))
                       ? g_input->FindKey(g_self, name)
                       : -1;
        }

        inline const char*                                          KeyParam(int key, int param)
        {
            return (g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi))
                       ? g_input->GetKeyParam(g_self, key, param)
                       : nullptr;
        }

        inline bool                                                 Keep(int key, bool keep = true, bool protect = false)
        {
            return g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi) &&
                   g_input->Keep(g_self, key, keep ? 1 : 0, protect ? 1 : 0) != 0;
        }

        inline bool                                                 BindCommand(int key, const char* command, bool held = false, int argc = 0, 
            const char** argv = nullptr, bool save = false, bool protect = false)
        {
            if (g_input == nullptr || g_input->structSize < sizeof(AmodInputApi) ||
                g_input->BindCommand(g_self, key, command, argc, argv, held ? 1 : 0) == 0)
            {
                return false;
            }
            return !save || Keep(key, true, protect);
        }

        inline bool                                                 BindCvar(int key, const char* cvar, bool held = false, bool save = false, bool protect = false)
        {
            if (g_input == nullptr || g_input->structSize < sizeof(AmodInputApi) ||
                g_input->BindCvar(g_self, key, cvar, held ? 1 : 0) == 0)
            {
                return false;
            }
            return !save || Keep(key, true, protect);
        }

        inline bool                                                 Unbind(int key)
        {
            return g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi) &&
                   g_input->Unbind(g_self, key) != 0;
        }

        inline bool                                                 HasJoystick()
        {
            return g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi) &&
                   g_input->HasJoystick(g_self) != 0;
        }

        /* One press on the player's behalf, let go as soon as the game has seen it. */
        inline bool                                                 Press(int key)
        {
            return g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi) &&
                   g_input->PressKey(g_self, key) != 0;
        }

        inline bool                                                 Hold(int key, bool down = true, bool exclusive = false)
        {
            return g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi) &&
                   g_input->HoldKey(g_self, key, down ? 1 : 0, exclusive ? 1 : 0) != 0;
        }

        /* 1 shown, 0 hidden, -1 the game's own again. */
        inline bool                                                 ShowSystemCursor(int show)
        {
            return g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi) &&
                   g_input->ShowSystemCursor(g_self, show) != 0;
        }

        inline bool                                                 IsForced(int key)
        {
            return g_input != nullptr && g_input->structSize >= sizeof(AmodInputApi) &&
                   g_input->IsKeyForced(g_self, key) != 0;
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
