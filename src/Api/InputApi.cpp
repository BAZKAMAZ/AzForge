#include "pch.h"

#include "src/Api/InputApi.h"

#include "src/Api/FrameApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"

#include <string.h>

namespace Forge
{
    namespace
    {
        using namespace GameAddress;

        // Both end in RET 8, so they clean their own arguments: __stdcall, not __cdecl.
        // Calling them as __cdecl let the caller pop the arguments a second time, which walked
        // the stack pointer eight bytes past the frame and handed garbage back to whoever had
        // called through the mod - a crash with no bad pointer anywhere near it.
        typedef void(__stdcall* GetCursorRoutine)(float* x, float* y);
        typedef void(__stdcall* SetCursorRoutine)(float x, float y);

        AmodInputApi g_table = {};

        uint8_t* g_input = nullptr;
        GetCursorRoutine g_getCursor = nullptr;
        SetCursorRoutine g_setCursor = nullptr;

        // The game keeps only what is down now, so the edge has to be worked out here. Copied
        // once a frame, after the game's own update, which is what makes "pressed" mean
        // "between the previous frame and this one" rather than "since somebody last asked".
        uint8_t g_previous[kInputKeyCount] = {};

        // The wheel, which the game never sees. g_wheel gathers notches as the messages arrive;
        // g_wheelFrame is what a mod reads, held still for the length of a frame so every mod
        // sees the same turn.
        HWND g_window = nullptr;
        WNDPROC g_originalProc = nullptr;
        int g_wheel = 0;
        int g_wheelFrame = 0;

        LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        {
            if (message == WM_MOUSEWHEEL)
            {
                g_wheel += GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
            }
            return CallWindowProcA(g_originalProc, window, message, wparam, lparam);
        }

        // The window does not exist when the loader starts - the launcher has not run yet - so
        // it is picked up on the first frame that has one.
        void AttachToWindow()
        {
            HWND* const slot = static_cast<HWND*>(GameModule::Resolve(kGameWindow));
            HWND const window = slot != nullptr ? *slot : nullptr;

            if (window == nullptr || window == g_window)
            {
                return;
            }

            const LONG_PTR previous = SetWindowLongPtrA(window, GWLP_WNDPROC,
                                                        reinterpret_cast<LONG_PTR>(&WindowProc));
            if (previous == 0)
            {
                return;
            }

            g_originalProc = reinterpret_cast<WNDPROC>(previous);
            g_window = window;
            FORGE_LOG_INFO("The game's window is watched for the mouse wheel, which the game "
                           "itself does not read");
        }

        void DetachFromWindow()
        {
            if (g_window != nullptr && g_originalProc != nullptr)
            {
                SetWindowLongPtrA(g_window, GWLP_WNDPROC,
                                  reinterpret_cast<LONG_PTR>(g_originalProc));
            }

            g_window = nullptr;
            g_originalProc = nullptr;
        }

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        bool KeyInRange(int key)
        {
            return key >= 0 && key < static_cast<int>(kInputKeyCount);
        }

        bool DownNow(int key)
        {
            return g_input != nullptr && KeyInRange(key) &&
                   *(g_input + kInputKeyStateOffset + key) != 0;
        }

        void __cdecl RememberFrame()
        {
            AttachToWindow();

            g_wheelFrame = g_wheel;
            g_wheel = 0;

            if (g_input != nullptr)
            {
                memcpy(g_previous, g_input + kInputKeyStateOffset, kInputKeyCount);
            }
        }

        int __cdecl ApiIsKeyDown(AmodMod mod, int key)
        {
            return (Resolve(mod) != nullptr && DownNow(key)) ? 1 : 0;
        }

        int __cdecl ApiWasKeyPressed(AmodMod mod, int key)
        {
            if (Resolve(mod) == nullptr || !KeyInRange(key))
            {
                return 0;
            }
            return (DownNow(key) && g_previous[key] == 0) ? 1 : 0;
        }

        int __cdecl ApiWasKeyReleased(AmodMod mod, int key)
        {
            if (Resolve(mod) == nullptr || !KeyInRange(key))
            {
                return 0;
            }
            return (!DownNow(key) && g_previous[key] != 0) ? 1 : 0;
        }

        void __cdecl ApiGetCursor(AmodMod mod, float* x, float* y)
        {
            if (Resolve(mod) == nullptr || g_getCursor == nullptr)
            {
                return;
            }

            // Asked for both even when the caller wants one, because the game's routine writes
            // through both pointers and has no way to be told otherwise.
            float cursorX = 0.0f;
            float cursorY = 0.0f;
            g_getCursor(&cursorX, &cursorY);

            if (x != nullptr)
            {
                *x = cursorX;
            }
            if (y != nullptr)
            {
                *y = cursorY;
            }
        }

        void __cdecl ApiSetCursor(AmodMod mod, float x, float y)
        {
            if (Resolve(mod) != nullptr && g_setCursor != nullptr)
            {
                g_setCursor(x, y);
            }
        }

        int __cdecl ApiIsCursorInWindow(AmodMod mod)
        {
            return (Resolve(mod) != nullptr && g_input != nullptr &&
                    *(g_input + kInputCursorInWindowOffset) != 0) ? 1 : 0;
        }

        int __cdecl ApiIsSwallowed(AmodMod mod)
        {
            return (Resolve(mod) != nullptr && g_input != nullptr &&
                    *(g_input + kInputSwallowingOffset) != 0) ? 1 : 0;
        }

        int __cdecl ApiWheelDelta(AmodMod mod)
        {
            return Resolve(mod) != nullptr ? g_wheelFrame : 0;
        }
    }

    bool InputApi::Install()
    {
        g_input = static_cast<uint8_t*>(GameModule::Resolve(kInput));
        g_getCursor = reinterpret_cast<GetCursorRoutine>(GameModule::Resolve(kInputGetCursor));
        g_setCursor = reinterpret_cast<SetCursorRoutine>(GameModule::Resolve(kInputSetCursor));

        if (g_input == nullptr || g_getCursor == nullptr || g_setCursor == nullptr)
        {
            FORGE_LOG_WARN("The game's input was not located, so the input interface is not "
                           "offered");
            return false;
        }

        // After the mods, not before them: the copy this takes is what the next frame's edges
        // are measured against, and taking it first would answer every WasKeyPressed with false.
        FrameApi::AddInternalTick(&RememberFrame, true);

        g_table.structSize = sizeof(AmodInputApi);
        g_table.IsKeyDown = &ApiIsKeyDown;
        g_table.WasKeyPressed = &ApiWasKeyPressed;
        g_table.WasKeyReleased = &ApiWasKeyReleased;
        g_table.GetCursor = &ApiGetCursor;
        g_table.SetCursor = &ApiSetCursor;
        g_table.IsCursorInWindow = &ApiIsCursorInWindow;
        g_table.IsSwallowed = &ApiIsSwallowed;
        g_table.WheelDelta = &ApiWheelDelta;

        FORGE_LOG_INFO("Input interface installed");
        return true;
    }

    void InputApi::Uninstall()
    {
        // Before anything else: the window procedure points into this DLL.
        DetachFromWindow();
        g_wheel = 0;
        g_wheelFrame = 0;

        g_input = nullptr;
        g_getCursor = nullptr;
        g_setCursor = nullptr;
        memset(g_previous, 0, sizeof(g_previous));
        g_table = AmodInputApi{};
    }

    const AmodInputApi* InputApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }
}
