#include <windows.h>

#include <string.h>

#include "src/Api/StageApi.h"
#include "src/Mods/ModLoadScreen.h"
#include "src/Api/RenderApi.h"

#include "src/Api/ApiRead.h"
#include "src/Api/InputApi.h"
#include "src/Api/MmdApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"

namespace Forge
{
    namespace
    {
        using namespace GameAddress;
        using ApiRead::Caller;
        using ApiRead::Fail;

        typedef void(__cdecl* RenderRoutine)();
        typedef void(__stdcall* UpdateRoutine)(float seconds);
        typedef int(__cdecl* ShellRoutine)();
        typedef int(__cdecl* LoadRoutine)();
        typedef void(__fastcall* MusicRoutine)(void* music);
        typedef int(__cdecl* InitRoutine)();
        typedef int(__fastcall* InitGuiRoutine)(void* scene);
        typedef void*(__cdecl* StateRoutine)();

        // The manager's setters are __thiscall, and so are the two matrix builders.
        typedef void(__fastcall* ApplyStatesRoutine)(void* manager, void* unused, void* states);
        typedef void(__fastcall* SetTransformRoutine)(void* manager, void* unused, const float* matrix);
        typedef uint32_t(__stdcall* ClearRoutine)(void* device, uint32_t count, const void* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil);
        typedef void(__fastcall* LookAtRoutine)(float* matrix, void* unused, const float* eye, const float* target, const float* up);
        typedef void(__fastcall* PerspectiveRoutine)(float* matrix, void* unused, float fovRadians, float aspect, float nearZ, float farZ);

        // Opening and showing a frame, which the game does for itself every tick and a mod being
        // started has to do for itself, because the game is not ticking yet.
        typedef void(__fastcall* FrameRoutine)(void* manager, void* unused);

        // Straight off the device: BeginScene, EndScene and Present by their place in the vtable.
        typedef long(__stdcall* SceneRoutine)(void* device);
        typedef long(__stdcall* PresentRoutine)(void* device, const void* from, const void* to, void* window, void* dirty);

        constexpr size_t kDevicePresent = 15;
        constexpr size_t kDeviceBeginScene = 34;
        constexpr size_t kDeviceEndScene = 35;

        AmodStageApi g_table = {};

        void** g_vtable = nullptr;
        RenderRoutine g_gameRender = nullptr;
        UpdateRoutine g_gameUpdate = nullptr;
        ShellRoutine g_gameShell = nullptr;
        LoadRoutine g_gameLoad = nullptr;

        // Who holds each half, and what to call instead.
        struct Holder
        {
            ModContext* owner;
            void* callback;
            void* user;
        };

        Holder g_update = {};
        Holder g_render = {};
        Holder g_shell = {};
        Holder g_load = {};

        bool g_inUpdate = false;
        bool g_inRender = false;
        bool g_inShell = false;
        bool g_inLoad = false;
        bool g_loaded = false;

        void* Manager();
        void* Device();

        void Viewport(float& wide, float& tall)
        {
            const uint8_t* const manager = static_cast<const uint8_t*>(Manager());

            wide = AMOD_MENU_WIDE;
            tall = AMOD_MENU_TALL;

            if (manager == nullptr)
            {
                return;
            }

            const uint8_t* const info = manager + kRenderViewportInfoOffset;
            const int pixelsWide = ApiRead::Read<int>(info, kRenderViewportWidthOffset);
            const int pixelsTall = ApiRead::Read<int>(info, kRenderViewportHeightOffset);

            wide = pixelsWide > 0 ? static_cast<float>(pixelsWide) : AMOD_MENU_WIDE;
            tall = pixelsTall > 0 ? static_cast<float>(pixelsTall) : AMOD_MENU_TALL;
        }

        void ApplyWorldStates()
        {
            void* const manager = Manager();
            const uint8_t* const scene = static_cast<const uint8_t*>(GameModule::Resolve(kScene));

            if (manager == nullptr || scene == nullptr)
            {
                return;
            }

            void* states = nullptr;
            memcpy(&states, scene + kSceneModelStatesOffset, sizeof(states));

            void** const vtable = *static_cast<void***>(manager);
            const auto apply = reinterpret_cast<ApplyStatesRoutine>(vtable[kRenderApplyStates / sizeof(void*)]);
            apply(manager, nullptr, states);
        }

        void __stdcall UpdateDetour(float seconds)
        {
            if (g_update.callback == nullptr)
            {
                g_gameUpdate(seconds);
                return;
            }

            InputApi::FillKeysIfStalled();

            g_inUpdate = true;
            reinterpret_cast<AmodStageUpdate>(g_update.callback)(seconds, g_update.user);
            g_inUpdate = false;
        }

        void __cdecl RenderDetour()
        {
            if (g_render.callback == nullptr)
            {
                g_gameRender();
                return;
            }

            ApplyWorldStates();

            g_inRender = true;
            MmdApi::EnterDraw();
            reinterpret_cast<AmodStageRender>(g_render.callback)(g_render.user);
            MmdApi::LeaveDraw();
            g_inRender = false;
        }

        int __cdecl ShellDetour()
        {
            if (g_shell.callback == nullptr)
            {
                return g_gameShell();
            }

            g_inShell = true;
            const int swallowed = reinterpret_cast<AmodStageShell>(g_shell.callback)(g_shell.user);
            g_inShell = false;
            return swallowed;
        }

        void RunEngineInit()
        {
            const InitRoutine music = reinterpret_cast<InitRoutine>(GameModule::Resolve(kMusicInit));
            const InitGuiRoutine gui = reinterpret_cast<InitGuiRoutine>(GameModule::Resolve(kRenderInitGui));
            void* const scene = GameModule::Resolve(kScene);

            if (music != nullptr)
            {
                music();
            }
            if (gui != nullptr && scene != nullptr)
            {
                gui(scene);
            }
        }

        int __cdecl LoadDetour()
        {
            int carryOn = 0;

            if (g_load.callback == nullptr)
            {
                carryOn = g_gameLoad();
            }
            else
            {
                RunEngineInit();

                g_inLoad = true;
                carryOn = reinterpret_cast<AmodStageLoad>(g_load.callback)(g_load.user);
                g_inLoad = false;
            }

            g_loaded = true;
            return carryOn;
        }

        bool WriteSlot(uintptr_t slot, void* routine)
        {
            DWORD guard = 0;
            void** const at = g_vtable + slot / sizeof(void*);

            if (g_vtable == nullptr || VirtualProtect(at, sizeof(void*), PAGE_READWRITE, &guard) == 0)
            {
                return false;
            }

            *at = routine;

            DWORD ignored = 0;
            VirtualProtect(at, sizeof(void*), guard, &ignored);
            return true;
        }

        Holder* HalfOf(int what)
        {
            if (what == AMOD_STAGE_UPDATE)
            {
                return &g_update;
            }
            if (what == AMOD_STAGE_RENDER)
            {
                return &g_render;
            }
            if (what == AMOD_STAGE_SHELL)
            {
                return &g_shell;
            }
            return what == AMOD_STAGE_LOAD ? &g_load : nullptr;
        }

        int Take(AmodMod mod, int what, void* callback, void* user)
        {
            ModContext* const context = Caller(mod);
            Holder* const half = HalfOf(what);

            if (context == nullptr || half == nullptr || g_vtable == nullptr)
            {
                return 0;
            }
            if (callback == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "taking the stage wants something to run");
            }
            if (what == AMOD_STAGE_LOAD && g_loaded)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "the game read its data before this mod was "
                            "loaded - only a mod loaded before the launcher can take that step");
            }
            if (half->owner != nullptr && half->owner != context)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "'%s' already holds that half of the frame",
                            half->owner->id.c_str());
            }

            half->owner = context;
            half->callback = callback;
            half->user = user;
            return 1;
        }

        int __cdecl ApiTakeUpdate(AmodMod mod, AmodStageUpdate update, void* user)
        {
            return Take(mod, AMOD_STAGE_UPDATE, reinterpret_cast<void*>(update), user);
        }

        int __cdecl ApiTakeRender(AmodMod mod, AmodStageRender render, void* user)
        {
            return Take(mod, AMOD_STAGE_RENDER, reinterpret_cast<void*>(render), user);
        }

        int __cdecl ApiTakeShell(AmodMod mod, AmodStageShell shell, void* user)
        {
            return Take(mod, AMOD_STAGE_SHELL, reinterpret_cast<void*>(shell), user);
        }

        int __cdecl ApiRunGameShell(AmodMod mod)
        {
            return (Caller(mod) != nullptr && g_inShell && g_gameShell != nullptr)
                       ? g_gameShell()
                       : 0;
        }

        int __cdecl ApiTakeLoad(AmodMod mod, AmodStageLoad load, void* user)
        {
            return Take(mod, AMOD_STAGE_LOAD, reinterpret_cast<void*>(load), user);
        }

        int __cdecl ApiRunGameLoad(AmodMod mod)
        {
            return (Caller(mod) != nullptr && g_inLoad && g_gameLoad != nullptr) ? g_gameLoad() : 0;
        }

        void __cdecl ApiRunMusic(AmodMod mod)
        {
            const MusicRoutine tick = reinterpret_cast<MusicRoutine>(GameModule::Resolve(kMusicTick));
            void* const music = GameModule::Resolve(kMusic);

            if (Caller(mod) != nullptr && tick != nullptr && music != nullptr)
            {
                tick(music);
            }
        }

        int __cdecl ApiGiveBack(AmodMod mod, int what)
        {
            ModContext* const context = Caller(mod);

            if (context == nullptr)
            {
                return 0;
            }

            if ((what & AMOD_STAGE_UPDATE) != 0 && g_update.owner == context)
            {
                g_update = Holder{};
            }
            if ((what & AMOD_STAGE_RENDER) != 0 && g_render.owner == context)
            {
                g_render = Holder{};
            }
            if ((what & AMOD_STAGE_SHELL) != 0 && g_shell.owner == context)
            {
                g_shell = Holder{};
            }
            if ((what & AMOD_STAGE_LOAD) != 0 && g_load.owner == context)
            {
                g_load = Holder{};
            }

            return 1;
        }

        int __cdecl ApiIsTaken(AmodMod mod, int what)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            const bool update = (what & AMOD_STAGE_UPDATE) != 0 && g_update.owner != nullptr;
            const bool render = (what & AMOD_STAGE_RENDER) != 0 && g_render.owner != nullptr;
            const bool shell = (what & AMOD_STAGE_SHELL) != 0 && g_shell.owner != nullptr;
            const bool load = (what & AMOD_STAGE_LOAD) != 0 && g_load.owner != nullptr;
            return (update || render || shell || load) ? 1 : 0;
        }

        void __cdecl ApiRunGameUpdate(AmodMod mod, float seconds)
        {
            if (Caller(mod) != nullptr && g_inUpdate && g_gameUpdate != nullptr)
            {
                g_gameUpdate(seconds);
            }
        }

        void __cdecl ApiRunGameRender(AmodMod mod)
        {
            if (Caller(mod) != nullptr && g_inRender && g_gameRender != nullptr)
            {
                g_gameRender();
            }
        }

        int __cdecl ApiTakeInput(AmodMod mod, int take)
        {
            ModContext* const context = Caller(mod);
            return (context != nullptr && InputApi::HoldKeyboard(context, take != 0)) ? 1 : 0;
        }

        void* Manager()
        {
            void** const slot = static_cast<void**>(GameModule::Resolve(kRenderManagerPtr));
            return slot != nullptr ? *slot : nullptr;
        }

        void* Device()
        {
            void* const manager = Manager();
            return manager != nullptr
                ? *reinterpret_cast<void**>(static_cast<uint8_t*>(manager) + kRenderDeviceOffset)
                : nullptr;
        }

        void SetTransform(uintptr_t slot, const float* matrix)
        {
            void* const manager = Manager();

            if (manager == nullptr)
            {
                return;
            }

            void** const vtable = *static_cast<void***>(manager);
            const auto set = reinterpret_cast<SetTransformRoutine>(vtable[slot / sizeof(void*)]);
            set(manager, nullptr, matrix);
        }

        void __cdecl ApiClear(AmodMod mod, uint32_t color)
        {
            void* const device = Caller(mod) != nullptr ? Device() : nullptr;

            if (device == nullptr)
            {
                return;
            }

            void** const vtable = *static_cast<void***>(device);
            const auto clear = reinterpret_cast<ClearRoutine>(vtable[kDeviceClear / sizeof(void*)]);

            clear(device, 0, nullptr, 0x03, color, 1.0f, 0);
        }

        int __cdecl ApiSetCamera(AmodMod mod, const float* eye, const float* at, const float* up, float fovDegrees, float nearZ, float farZ)
        {
            const LookAtRoutine lookAt = reinterpret_cast<LookAtRoutine>(GameModule::Resolve(kMatrixLookAt));
            const PerspectiveRoutine perspective = reinterpret_cast<PerspectiveRoutine>(GameModule::Resolve(kMatrixPerspective));

            if (Caller(mod) == nullptr || lookAt == nullptr || perspective == nullptr)
            {
                return 0;
            }
            if (eye == nullptr || at == nullptr || up == nullptr || nearZ <= 0.0f || farZ <= nearZ)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a camera wants three points and a depth "
                            "the near of which is nearer than the far");
            }

            // The game's own world is always 4:3 whatever the window is; a mod's own need not be.
            float wide = 0.0f;
            float tall = 0.0f;
            Viewport(wide, tall);

            const float aspect = tall > 0.0f ? wide / tall : 4.0f / 3.0f;

            float view[16] = {};
            float projection[16] = {};
            float world[16] = {};
            world[0] = world[5] = world[10] = world[15] = 1.0f;

            lookAt(view, nullptr, eye, at, up);
            perspective(projection, nullptr, fovDegrees * 3.1415927f / 180.0f, aspect, nearZ, farZ);

            SetTransform(kRenderSetView, view);
            SetTransform(kRenderSetProjection, projection);
            SetTransform(kRenderSetWorld, world);
            return 1;
        }
    }
        int __cdecl ApiLoading(AmodMod mod)
        {
            return (Caller(mod) != nullptr && ModLoadScreen::Up()) ? 1 : 0;
        }

        int __cdecl ApiLoadingSay(AmodMod mod, const char* text)
        {
            if (Caller(mod) == nullptr || text == nullptr || !ModLoadScreen::Up())
            {
                return 0;
            }

            ModLoadScreen::Report(text);
            return 1;
        }

        int __cdecl ApiLoadingStep(AmodMod mod, int percent, const char* text)
        {
            if (Caller(mod) == nullptr || !ModLoadScreen::Up())
            {
                return 0;
            }

            ModLoadScreen::Step(percent, text);
            return 1;
        }

        int __cdecl ApiLoadingFrame(AmodMod mod, AmodRenderCallback draw, void* user)
        {
            void* const device = Caller(mod) != nullptr ? Device() : nullptr;

            if (device == nullptr)
            {
                return 0;
            }

            // THE DEVICE, NOT THE GAME'S WRAPPER. A mod may be blocking on the loading screen,
            // where no scene is open, or inside its own render, where one already is - and the
            // game's own Render_Begin says D3DERR_INVALIDCALL to the second case and puts up a
            // fatal box about it. Asking the device tells us which case this is: opening a scene
            // succeeds only when there was none.
            void** const vtable = *static_cast<void***>(device);
            const auto begin = reinterpret_cast<SceneRoutine>(vtable[kDeviceBeginScene]);
            const auto end = reinterpret_cast<SceneRoutine>(vtable[kDeviceEndScene]);
            const auto show = reinterpret_cast<PresentRoutine>(vtable[kDevicePresent]);

            if (begin == nullptr || end == nullptr || show == nullptr)
            {
                return 0;
            }

            // Said once. The loader's own 2D refuses to draw unless the game's scene is up with
            // its sprite states in it, and whether that is true this early is the whole question.
            {
                static bool told = false;

                if (!told)
                {
                    told = true;

                    const uint8_t* const scene = static_cast<const uint8_t*>(
                        GameModule::Resolve(GameAddress::kScene));
                    void* states = nullptr;

                    if (scene != nullptr)
                    {
                        memcpy(&states, scene + GameAddress::kSceneSpriteStatesOffset, sizeof(states));
                    }

                    FORGE_LOG_INFO("stage: first loading frame - scene %p, sprite states %p, "
                        "2D %s", static_cast<const void*>(scene), states,
                        RenderApi::CanDraw2D() ? "ready" : "NOT READY, so nothing 2D will draw");
                }
            }

            const long opened = begin(device);

            if (draw != nullptr)
            {
                AmodFrame frame = {};

                frame.structSize = sizeof(frame);
                draw(&frame, user);
            }

            end(device);
            show(device, nullptr, nullptr, nullptr, nullptr);

            // There was a scene before this and there has to be one after: whoever was drawing in
            // it is still in the middle of doing so.
            if (opened < 0)
            {
                begin(device);
            }

            return 1;
        }


    bool StageApi::Install()
    {   
        // Gets the initialized game-state vtable.
        const StateRoutine current = reinterpret_cast<StateRoutine>(GameModule::Resolve(kGameStateCurrent));
        void* const state = current != nullptr ? current() : nullptr;

        g_vtable = state != nullptr ? *static_cast<void***>(state) : nullptr;

        if (g_vtable == nullptr || g_vtable != GameModule::Resolve(kAppStateVtable))
        {
            FORGE_LOG_WARN("The game's state does not carry the vtable it should, so the stage "
                           "interface is not offered");
            g_vtable = nullptr;
            return false;
        }

        g_gameRender = reinterpret_cast<RenderRoutine>(g_vtable[kAppStateRenderSlot / sizeof(void*)]);
        g_gameUpdate = reinterpret_cast<UpdateRoutine>(g_vtable[kAppStateUpdateSlot / sizeof(void*)]);
        g_gameShell = reinterpret_cast<ShellRoutine>(g_vtable[kAppStateShellSlot / sizeof(void*)]);
        g_gameLoad = reinterpret_cast<LoadRoutine>(g_vtable[kAppStateLoadSlot / sizeof(void*)]);

        if (g_gameRender != GameModule::Resolve(kAppRenderFrame) ||
            g_gameUpdate != GameModule::Resolve(kAppUpdateFrame) ||
            g_gameShell != GameModule::Resolve(kAppShellFrame) ||
            g_gameLoad != GameModule::Resolve(kMainLoadDataPak))
        {
            FORGE_LOG_WARN("The frame's two slots hold something else, so the stage interface is "
                           "not offered");
            g_vtable = nullptr;
            return false;
        }

        if (!WriteSlot(kAppStateRenderSlot, reinterpret_cast<void*>(&RenderDetour)) ||
            !WriteSlot(kAppStateUpdateSlot, reinterpret_cast<void*>(&UpdateDetour)) ||
            !WriteSlot(kAppStateShellSlot, reinterpret_cast<void*>(&ShellDetour)) ||
            !WriteSlot(kAppStateLoadSlot, reinterpret_cast<void*>(&LoadDetour)))
        {
            FORGE_LOG_ERROR("The frame's slots would not be written");
            Uninstall();
            return false;
        }

        g_table.structSize = sizeof(AmodStageApi);
        g_table.TakeUpdate = &ApiTakeUpdate;
        g_table.TakeRender = &ApiTakeRender;
        g_table.GiveBack = &ApiGiveBack;
        g_table.IsTaken = &ApiIsTaken;
        g_table.RunGameUpdate = &ApiRunGameUpdate;
        g_table.RunGameRender = &ApiRunGameRender;
        g_table.Clear = &ApiClear;
        g_table.SetCamera = &ApiSetCamera;
        g_table.Loading = &ApiLoading;
        g_table.LoadingSay = &ApiLoadingSay;
        g_table.LoadingStep = &ApiLoadingStep;
        g_table.LoadingFrame = &ApiLoadingFrame;
        g_table.TakeInput = &ApiTakeInput;
        g_table.TakeShell = &ApiTakeShell;
        g_table.RunGameShell = &ApiRunGameShell;
        g_table.TakeLoad = &ApiTakeLoad;
        g_table.RunGameLoad = &ApiRunGameLoad;
        g_table.RunMusic = &ApiRunMusic;

        FORGE_LOG_INFO("Stage interface installed");
        return true;
    }

    void StageApi::Uninstall()
    {
        g_update = Holder{};
        g_render = Holder{};
        g_shell = Holder{};
        g_load = Holder{};

        if (g_vtable != nullptr)
        {
            if (g_gameRender != nullptr)
            {
                WriteSlot(kAppStateRenderSlot, reinterpret_cast<void*>(g_gameRender));
            }
            if (g_gameUpdate != nullptr)
            {
                WriteSlot(kAppStateUpdateSlot, reinterpret_cast<void*>(g_gameUpdate));
            }
            if (g_gameShell != nullptr)
            {
                WriteSlot(kAppStateShellSlot, reinterpret_cast<void*>(g_gameShell));
            }
            if (g_gameLoad != nullptr)
            {
                WriteSlot(kAppStateLoadSlot, reinterpret_cast<void*>(g_gameLoad));
            }
        }

        g_vtable = nullptr;
        g_gameRender = nullptr;
        g_gameUpdate = nullptr;
        g_gameShell = nullptr;
        g_gameLoad = nullptr;
        g_table = AmodStageApi{};
    }

    const AmodStageApi* StageApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void StageApi::ForgetMod(ModContext* owner)
    {
        // The callback points into a DLL that is about to go.
        if (g_update.owner == owner)
        {
            g_update = Holder{};
        }
        if (g_render.owner == owner)
        {
            g_render = Holder{};
        }
        if (g_shell.owner == owner)
        {
            g_shell = Holder{};
        }
        if (g_load.owner == owner)
        {
            g_load = Holder{};
        }
    }
}
