#include <windows.h>

#include "src/Api/RenderApi.h"
#include "src/Api/TextureApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Game/D3d8Hook.h"
#include "src/Api/ShaderApi.h"
#include "src/Hook/InlineHook.h"

#include <math.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        // Present / element render: __thiscall → __fastcall (ECX = this, EDX unused).
        typedef void(__fastcall* PresentRoutine)(void* manager, void* unused);
        typedef char(__fastcall* BeginRoutine)(void* manager, void* unused);
        typedef void(__fastcall* ElementRenderRoutine)(void* element, void* unused);
        typedef void(__fastcall* DecorationsRoutine)(void* menu, void* unused);
        typedef void(__cdecl* InternalDrawRoutine)();

        // g_pfnDrawStyledText (kDrawStyledTextPtr).
        typedef void(__cdecl* DrawStyledTextRoutine)(void* font, const char* text, int zero, const float* centre, float* scale, float* color, int align, int zero2);

        typedef void(__cdecl* DrawSpriteRoutine)(int sprite, float x, float y, float w, float h, int zero, float* color, int a7, int a8);

        // Eight floats on the stack, the manager in ECX: Sprite_Draw's own last call.
        typedef void(__fastcall* DrawSpritePartRoutine)(void* manager, void* unused, float x, float y, float w, float h, float u0, float v0, float u1, float v1);
        typedef void(__fastcall* SetColourRoutine)(void* manager, void* unused, const float* rgba);
        typedef void(__fastcall* SetSpriteTextureRoutine)(void* manager, void* unused, void* texture);
        typedef void(__fastcall* ApplyStatesRoutine)(void* manager, void* unused, void* states);
        typedef void(__cdecl* DrawStaticTextRoutine)(void* a1, void* a2, int a3, void* a4, void* a5, float* color, int a7);
        typedef void*(__cdecl* FindFontRoutine)(const char* name);

        struct Subscriber
        {
            ModContext* owner;
            AmodRenderCallback callback;
            void* user;
            AmodToken token;
        };

        struct ElementSub
        {
            ModContext* owner;
            std::string id;         // empty matches every item
            uint32_t phases;        // mask of AmodElementPhase
            AmodElementDrawCallback callback;
            void* user;
            AmodToken token;
        };

        struct StyleSub
        {
            ModContext* owner;
            std::string id;         // empty matches every item
            AmodItemStyleCallback callback;
            void* user;
            AmodToken token;
        };

        struct BgSub
        {
            ModContext* owner;
            AmodMenuBackgroundCallback callback;
            void* user;
            AmodToken token;
        };

        struct DeviceSub
        {
            ModContext* owner;
            AmodDeviceCallback callback;
            void* user;
            AmodToken token;
        };

        InlineHook g_hook;
        PresentRoutine g_originalPresent = nullptr;
        const float* g_delta = nullptr;

        InlineHook g_beginHook;
        BeginRoutine g_originalBegin = nullptr;

        struct HookedVtable
        {
            void* vtable;                   // patched slot 0 address, == the element's vtable
            ElementRenderRoutine original;  // render it held before patching
        };
        std::vector<HookedVtable> g_vtables;

        InlineHook g_decorHook;
        DecorationsRoutine g_originalDecorations = nullptr;

        // Resolves a menu font by name for Render::Text; located in Install.
        FindFontRoutine g_findFont = nullptr;

        AmodRenderApi g_table = {};

        std::vector<Subscriber> g_subscribers;
        std::vector<Subscriber> g_beginSubs;
        std::vector<ElementSub> g_elementSubs;
        std::vector<StyleSub> g_styleSubs;
        std::vector<BgSub> g_bgSubs;
        std::vector<DeviceSub> g_deviceSubs;
        std::vector<InternalDrawRoutine> g_internalPresent;
        std::vector<InternalDrawRoutine> g_afterPresent;
        AmodToken g_nextToken = 1;

        void** g_drawSlot = nullptr;
        DrawStyledTextRoutine g_originalDraw = nullptr;
        void* g_currentStyleItem = nullptr;

        void** g_spriteSlot = nullptr;
        DrawSpriteRoutine g_originalSprite = nullptr;

        void** g_staticTextSlot = nullptr;
        DrawStaticTextRoutine g_originalStaticText = nullptr;

        double g_elapsed = 0.0;
        uint64_t g_frameIndex = 0;

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        // The render manager, or null before the device has been created.
        void* Manager()
        {
            void** const slot =
                static_cast<void**>(GameModule::Resolve(GameAddress::kRenderManagerPtr));
            return slot != nullptr ? *slot : nullptr;
        }

        void* Device()
        {
            void* const manager = Manager();
            if (manager == nullptr)
            {
                return nullptr;
            }
            if (*(static_cast<char*>(manager) + GameAddress::kRenderDeviceLostOffset) != 0)
            {
                return nullptr;
            }
            return *reinterpret_cast<void**>(static_cast<char*>(manager) +
                                             GameAddress::kRenderDeviceOffset);
        }

        void* __cdecl ApiGetDevice(AmodMod mod)
        {
            return Resolve(mod) != nullptr ? Device() : nullptr;
        }

        void __cdecl ApiGetViewport(AmodMod mod, int* width, int* height)
        {
            void* const manager = Manager();
            if (Resolve(mod) == nullptr || manager == nullptr)
            {
                return;
            }
            char* const info = static_cast<char*>(manager) + GameAddress::kRenderViewportInfoOffset;
            if (width != nullptr)
            {
                *width = *reinterpret_cast<int*>(info + GameAddress::kRenderViewportWidthOffset);
            }
            if (height != nullptr)
            {
                *height = *reinterpret_cast<int*>(info + GameAddress::kRenderViewportHeightOffset);
            }
        }

        void Unpack(uint32_t color, float* rgba);

        // Converts viewport 2D space to 640x480 menu space with Y inverted.
        void ToViewport(float& x, float& y, float& w, float& h)
        {
            void* const manager = Manager();

            if (manager == nullptr)
            {
                return;
            }

            const uint8_t* const info = static_cast<const uint8_t*>(manager) + GameAddress::kRenderViewportInfoOffset;
            const int wide = *reinterpret_cast<const int*>(info + GameAddress::kRenderViewportWidthOffset);
            const int tall = *reinterpret_cast<const int*>(info + GameAddress::kRenderViewportHeightOffset);

            if (wide <= 0 || tall <= 0)
            {
                return;
            }

            const float across = static_cast<float>(wide) / AMOD_MENU_WIDE;
            const float down = static_cast<float>(tall) / AMOD_MENU_TALL;

            x *= across;
            w *= across;
            h *= down;

            // Both spaces name the top edge; they differ only in which way they count.
            y = static_cast<float>(tall) - y * down;
        }

        void __cdecl ApiDrawSpritePart(AmodMod mod, AmodTextureId texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t color)
        {
            void* const manager = Manager();
            void* const skin = Resolve(mod) != nullptr ? TextureApi::TextureFor(texture) : nullptr;

            if (manager == nullptr || skin == nullptr)
            {
                return;
            }

            ToViewport(x, y, w, h);

            void** const vtable = *static_cast<void***>(manager);
            const auto setColour = reinterpret_cast<SetColourRoutine>(vtable[GameAddress::kRenderSetColour / sizeof(void*)]);
            const auto setTexture = reinterpret_cast<SetSpriteTextureRoutine>(vtable[GameAddress::kRenderSetTexture / sizeof(void*)]);
            const auto apply = reinterpret_cast<ApplyStatesRoutine>(vtable[GameAddress::kRenderApplyStates / sizeof(void*)]);
            const auto draw = reinterpret_cast<DrawSpritePartRoutine>(vtable[GameAddress::kRenderDrawSprite2D / sizeof(void*)]);

            const uint8_t* const scene = static_cast<const uint8_t*>(GameModule::Resolve(GameAddress::kScene));

            if (scene != nullptr)
            {
                void* states = nullptr;
                memcpy(&states, scene + GameAddress::kSceneSpriteStatesOffset, sizeof(states));

                if (states != nullptr)
                {
                    apply(manager, nullptr, states);
                }
            }

            float rgba[4] = {};
            Unpack(color, rgba);

            setColour(manager, nullptr, rgba);
            setTexture(manager, nullptr, skin);
            draw(manager, nullptr, x, y, w, h, u0, v0, u1, v1);
        }

        // Device calls: COM, so __stdcall with the interface first.
        typedef long(__stdcall* SetRenderStateRoutine)(void* device, uint32_t state, uint32_t value);
        typedef long(__stdcall* GetRenderStateRoutine)(void* device, uint32_t state, uint32_t* value);
        typedef long(__stdcall* SetStageStateRoutine)(void* device, uint32_t stage, uint32_t type, uint32_t value);
        typedef long(__stdcall* GetStageStateRoutine)(void* device, uint32_t stage, uint32_t type, uint32_t* value);
        typedef long(__stdcall* SetVertexShaderRoutine)(void* device, uint32_t handle);
        typedef long(__stdcall* DrawPrimitiveUpRoutine)(void* device, uint32_t primitive, uint32_t count, const void* data, uint32_t stride);

        struct BatchVertex
        {
            float x;
            float y;
            float z;
            float rhw;
            uint32_t colour;
            float u;
            float v;
        };

        const uint32_t kBatchStates[] = { 7, 14, 15, 19, 20, 22, 27, 28, 137 };
        const uint32_t kBatchStages[] = { 1, 2, 3, 4, 5, 6 };

        std::vector<BatchVertex> g_batch;

        template <typename T>
        T DeviceSlot(void* device, uintptr_t offset)
        {
            void** const vtable = *static_cast<void***>(device);
            return reinterpret_cast<T>(vtable[offset / sizeof(void*)]);
        }

        int __cdecl ApiDrawQuads(AmodMod mod, AmodTextureId texture, const AmodQuad* quads, int count)
        {
            void* const manager = Manager();
            void* const device = Device();
            void* const skin = Resolve(mod) != nullptr ? TextureApi::TextureFor(texture) : nullptr;

            if (manager == nullptr || device == nullptr || quads == nullptr || count <= 0)
            {
                return 0;
            }

            const uint8_t* const info =
                static_cast<const uint8_t*>(manager) + GameAddress::kRenderViewportInfoOffset;
            const int wide = *reinterpret_cast<const int*>(info + GameAddress::kRenderViewportWidthOffset);
            const int tall = *reinterpret_cast<const int*>(info + GameAddress::kRenderViewportHeightOffset);
            const float across = wide > 0 ? wide / AMOD_MENU_WIDE : 1.0f;
            const float down = tall > 0 ? tall / AMOD_MENU_TALL : 1.0f;

            g_batch.clear();
            g_batch.reserve(static_cast<size_t>(count) * 6);

            for (int at = 0; at < count; ++at)
            {
                const AmodQuad& quad = quads[at];
                const float middleX = (quad.x + quad.w * 0.5f) * across;
                const float middleY = (quad.y + quad.h * 0.5f) * down;
                const float halfW = quad.w * 0.5f * across;
                const float halfH = quad.h * 0.5f * down;
                const float turn = cosf(quad.angle);
                const float lean = sinf(quad.angle);

                const float offsetX[4] = { -halfW, halfW, halfW, -halfW };
                const float offsetY[4] = { -halfH, -halfH, halfH, halfH };
                const float u[4] = { quad.u0, quad.u1, quad.u1, quad.u0 };
                const float v[4] = { quad.v0, quad.v0, quad.v1, quad.v1 };
                const int order[6] = { 0, 1, 2, 0, 2, 3 };
                BatchVertex corner[4] = {};

                for (int which = 0; which < 4; ++which)
                {
                    corner[which].x = middleX + offsetX[which] * turn - offsetY[which] * lean;
                    corner[which].y = middleY + offsetX[which] * lean + offsetY[which] * turn;
                    corner[which].rhw = 1.0f;
                    corner[which].colour = quad.colour;
                    corner[which].u = u[which];
                    corner[which].v = v[which];
                }

                // Two triangles rather than a fan, so the whole batch is one list.
                for (int which = 0; which < 6; ++which)
                {
                    g_batch.push_back(corner[order[which]]);
                }
            }

            const SetRenderStateRoutine set = DeviceSlot<SetRenderStateRoutine>(device, GameAddress::kDeviceSetRenderState);
            const GetRenderStateRoutine get = DeviceSlot<GetRenderStateRoutine>(device, GameAddress::kDeviceGetRenderState);
            const SetStageStateRoutine setStage = DeviceSlot<SetStageStateRoutine>(device, GameAddress::kDeviceSetStageState);
            const GetStageStateRoutine getStage = DeviceSlot<GetStageStateRoutine>(device, GameAddress::kDeviceGetStageState);

            uint32_t wasState[sizeof(kBatchStates) / sizeof(kBatchStates[0])] = {};
            uint32_t wasStage[sizeof(kBatchStages) / sizeof(kBatchStages[0])] = {};

            for (size_t which = 0; which < sizeof(kBatchStates) / sizeof(kBatchStates[0]); ++which)
            {
                get(device, kBatchStates[which], &wasState[which]);
            }
            for (size_t which = 0; which < sizeof(kBatchStages) / sizeof(kBatchStages[0]); ++which)
            {
                getStage(device, 0, kBatchStages[which], &wasStage[which]);
            }

            set(device, 7, 0);      // no depth: this is a flat pass over a finished picture
            set(device, 14, 0);
            set(device, 15, 0);
            set(device, 22, 1);     // no culling, so a mirrored quad still shows
            set(device, 28, 0);
            set(device, 137, 0);
            set(device, 27, 1);     // blended
            set(device, 19, 5);
            set(device, 20, 6);

            const auto bind = reinterpret_cast<SetSpriteTextureRoutine>(
                (*static_cast<void***>(manager))[GameAddress::kRenderSetTexture / sizeof(void*)]);

            bind(manager, nullptr, skin);

            // With a texture the colour multiplies it; without one the colour is all there is.
            setStage(device, 0, 1, skin != nullptr ? 4u : 2u);
            setStage(device, 0, 2, skin != nullptr ? 2u : 0u);
            setStage(device, 0, 3, 0);
            setStage(device, 0, 4, skin != nullptr ? 4u : 2u);
            setStage(device, 0, 5, skin != nullptr ? 2u : 0u);
            setStage(device, 0, 6, 0);

            DeviceSlot<SetVertexShaderRoutine>(device, GameAddress::kDeviceSetVertexShader)(device, 0x144);

            const DrawPrimitiveUpRoutine draw = DeviceSlot<DrawPrimitiveUpRoutine>(device, GameAddress::kDeviceDrawPrimitiveUP);
            const size_t kMostAtOnce = 8190;    // a multiple of six, so no quad is cut in half

            for (size_t from = 0; from < g_batch.size(); from += kMostAtOnce)
            {
                const size_t here = g_batch.size() - from < kMostAtOnce ? g_batch.size() - from : kMostAtOnce;
                draw(device, 4, static_cast<uint32_t>(here / 3), &g_batch[from], sizeof(BatchVertex));
            }

            for (size_t which = 0; which < sizeof(kBatchStates) / sizeof(kBatchStates[0]); ++which)
            {
                set(device, kBatchStates[which], wasState[which]);
            }
            for (size_t which = 0; which < sizeof(kBatchStages) / sizeof(kBatchStages[0]); ++which)
            {
                setStage(device, 0, kBatchStages[which], wasStage[which]);
            }

            return count;
        }

        void* __cdecl ApiGetWindow(AmodMod mod)
        {
            HWND* const slot = Resolve(mod) != nullptr
                                   ? static_cast<HWND*>(GameModule::Resolve(GameAddress::kGameWindow))
                                   : nullptr;

            return slot != nullptr ? *slot : nullptr;
        }

        AmodToken __cdecl ApiSubscribe(AmodMod mod, AmodRenderCallback callback, void* user)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || callback == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }
            const AmodToken token = g_nextToken++;
            g_subscribers.push_back(Subscriber{ context, callback, user, token });
            return token;
        }

        void __cdecl ApiUnsubscribe(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || token == AMOD_INVALID_TOKEN)
            {
                return;
            }
            for (auto it = g_subscribers.begin(); it != g_subscribers.end(); ++it)
            {
                if (it->owner == context && it->token == token)
                {
                    g_subscribers.erase(it);
                    return;
                }
            }
            for (auto it = g_beginSubs.begin(); it != g_beginSubs.end(); ++it)
            {
                if (it->owner == context && it->token == token)
                {
                    g_beginSubs.erase(it);
                    return;
                }
            }
        }

        AmodToken __cdecl ApiSubscribeFrameBegin(AmodMod mod, AmodRenderCallback callback, void* user)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || callback == nullptr || g_originalBegin == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }
            const AmodToken token = g_nextToken++;
            g_beginSubs.push_back(Subscriber{ context, callback, user, token });
            return token;
        }

        AmodToken __cdecl ApiOnElementDraw(AmodMod mod, const char* id, uint32_t phases,
                                           AmodElementDrawCallback callback, void* user)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || callback == nullptr || phases == 0 || g_vtables.empty())
            {
                return AMOD_INVALID_TOKEN;
            }
            const AmodToken token = g_nextToken++;
            g_elementSubs.push_back(
                ElementSub{ context, id != nullptr ? id : "", phases, callback, user, token });
            return token;
        }

        void __cdecl ApiCancelElementDraw(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || token == AMOD_INVALID_TOKEN)
            {
                return;
            }
            for (auto it = g_elementSubs.begin(); it != g_elementSubs.end(); ++it)
            {
                if (it->owner == context && it->token == token)
                {
                    g_elementSubs.erase(it);
                    return;
                }
            }
        }

        AmodToken __cdecl ApiOnItemStyle(AmodMod mod, const char* id,
                                         AmodItemStyleCallback callback, void* user)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || callback == nullptr || g_vtables.empty() ||
                (g_drawSlot == nullptr && g_spriteSlot == nullptr))
            {
                return AMOD_INVALID_TOKEN;
            }
            const AmodToken token = g_nextToken++;
            g_styleSubs.push_back(StyleSub{ context, id != nullptr ? id : "", callback, user, token });
            return token;
        }

        void __cdecl ApiCancelItemStyle(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || token == AMOD_INVALID_TOKEN)
            {
                return;
            }
            for (auto it = g_styleSubs.begin(); it != g_styleSubs.end(); ++it)
            {
                if (it->owner == context && it->token == token)
                {
                    g_styleSubs.erase(it);
                    return;
                }
            }
        }

        AmodToken __cdecl ApiOnMenuBackground(AmodMod mod, AmodMenuBackgroundCallback callback,
                                              void* user)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || callback == nullptr || g_originalDecorations == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }
            const AmodToken token = g_nextToken++;
            g_bgSubs.push_back(BgSub{ context, callback, user, token });
            return token;
        }

        void __cdecl ApiCancelMenuBackground(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || token == AMOD_INVALID_TOKEN)
            {
                return;
            }
            for (auto it = g_bgSubs.begin(); it != g_bgSubs.end(); ++it)
            {
                if (it->owner == context && it->token == token)
                {
                    g_bgSubs.erase(it);
                    return;
                }
            }
        }

        AmodToken __cdecl ApiOnDevice(AmodMod mod, AmodDeviceCallback callback, void* user)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || callback == nullptr)
            {
                return AMOD_INVALID_TOKEN;
            }
            const AmodToken token = g_nextToken++;
            g_deviceSubs.push_back(DeviceSub{ context, callback, user, token });
            return token;
        }

        // Every token this subsystem hands out comes from one counter, so one canceller does.
        void __cdecl ApiCancel(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Resolve(mod);
            if (context == nullptr || token == AMOD_INVALID_TOKEN)
            {
                return;
            }
            ApiUnsubscribe(mod, token);
            ApiCancelElementDraw(mod, token);
            ApiCancelItemStyle(mod, token);
            ApiCancelMenuBackground(mod, token);
            for (auto it = g_deviceSubs.begin(); it != g_deviceSubs.end(); ++it)
            {
                if (it->owner == context && it->token == token)
                {
                    g_deviceSubs.erase(it);
                    return;
                }
            }
        }

        uint32_t __cdecl ApiDeviceGeneration(AmodMod mod)
        {
            return Resolve(mod) != nullptr ? D3d8Hook::Generation() : 0;
        }

        // What the d3d8 hook reports, passed on to the mods that asked.
        void OnDeviceEvent(DeviceEvent event, void* device)
        {
            ShaderApi::OnDeviceEvent(event, device);

            const std::vector<DeviceSub> snapshot = g_deviceSubs;
            for (const DeviceSub& subscriber : snapshot)
            {
                if (subscriber.callback != nullptr)
                {
                    subscriber.callback(static_cast<AmodDeviceEvent>(event), device, subscriber.user);
                }
            }
        }

        DrawStyledTextRoutine ActiveTextDraw();   // defined below, with the styled-text thunk

        void* __cdecl ApiGetFont(AmodMod mod, const char* name)
        {
            if (Resolve(mod) == nullptr || g_findFont == nullptr)
            {
                return nullptr;
            }
            return g_findFont(name);   // a null name resolves to the default font
        }

        // MENU SPACE INTO PIXELS, which is what the game's text routine actually works in - and it
        // counts y up from the bottom. Passing menu units straight through was right only while the
        // viewport happened to be 640x480; anywhere else the letters walked away from the rects
        // they were meant to sit against, and their size stayed put while everything else grew.
        void TextPlace(float x, float y, float scale, float* position, float* scaling)
        {
            void* const manager = Manager();
            float across = 1.0f;
            float down = 1.0f;

            if (manager != nullptr)
            {
                const uint8_t* const info = static_cast<const uint8_t*>(manager) + GameAddress::kRenderViewportInfoOffset;
                const int wide = *reinterpret_cast<const int*>(info + GameAddress::kRenderViewportWidthOffset);
                const int tall = *reinterpret_cast<const int*>(info + GameAddress::kRenderViewportHeightOffset);

                if (wide > 0 && tall > 0)
                {
                    across = static_cast<float>(wide) / AMOD_MENU_WIDE;
                    down = static_cast<float>(tall) / AMOD_MENU_TALL;
                }
            }

            position[0] = x * across;
            position[1] = (AMOD_MENU_TALL - y) * down;

            // A line asked for at a size should cover the same part of the screen whatever the
            // screen is, like everything else drawn in menu space.
            scaling[0] = scale * across;
            scaling[1] = scale * down;
        }

        void __cdecl ApiDrawText(AmodMod mod, void* font, float x, float y, const char* text, float scale, uint32_t color)
        {
            if (Resolve(mod) == nullptr || text == nullptr)
            {
                return;
            }
            const DrawStyledTextRoutine draw = ActiveTextDraw();
            if (font == nullptr)
            {
                font = GameModule::Resolve(GameAddress::kSystemFont);
            }
            if (draw == nullptr || font == nullptr)
            {
                return;
            }

            float position[2] = {};
            float scaling[2] = {};

            TextPlace(x, y, scale, position, scaling);
            float rgba[4] =
            {
                ((color >> 16) & 0xFF) / 255.0f, // R
                ((color >> 8) & 0xFF) / 255.0f,  // G
                (color & 0xFF) / 255.0f,         // B
                ((color >> 24) & 0xFF) / 255.0f, // A
            };
            draw(font, text, 0, position, scaling, rgba, 0x24, 0);
        }

        void Unpack(uint32_t color, float* rgba)
        {
            rgba[0] = ((color >> 16) & 0xFF) / 255.0f;
            rgba[1] = ((color >> 8) & 0xFF) / 255.0f;
            rgba[2] = (color & 0xFF) / 255.0f;
            rgba[3] = ((color >> 24) & 0xFF) / 255.0f;
        }

        void __cdecl ApiDrawTextAligned(AmodMod mod, void* font, float x, float y, const char* text, float scale, uint32_t color, int align)
        {
            if (Resolve(mod) == nullptr || text == nullptr)
            {
                return;
            }
            const DrawStyledTextRoutine draw = ActiveTextDraw();
            if (font == nullptr)
            {
                font = GameModule::Resolve(GameAddress::kSystemFont);
            }
            if (draw == nullptr || font == nullptr)
            {
                return;
            }

            float position[2] = {};
            float scaling[2] = {};

            TextPlace(x, y, scale, position, scaling);
            float rgba[4] = {};
            Unpack(color, rgba);

            draw(font, text, 0, position, scaling, rgba, align, 0);
        }

        // Slot holds the thunk once styling is in; sprites go through the same door as the game's.
        DrawSpriteRoutine ActiveSpriteDraw()
        {
            return g_spriteSlot != nullptr ? reinterpret_cast<DrawSpriteRoutine>(*g_spriteSlot) : nullptr;
        }

        bool SceneReady()
        {
            const uint8_t* const scene = static_cast<const uint8_t*>(
                GameModule::Resolve(GameAddress::kScene));

            if (scene == nullptr)
            {
                return false;
            }

            void* states = nullptr;
            memcpy(&states, scene + GameAddress::kSceneSpriteStatesOffset, sizeof(states));
            return states != nullptr;
        }

        void __cdecl ApiDrawSprite(AmodMod mod, AmodTextureId texture, float x, float y, float w, float h, uint32_t color)
        {
            const DrawSpriteRoutine draw = ActiveSpriteDraw();
            void* const skin = Resolve(mod) != nullptr ? TextureApi::TextureFor(texture) : nullptr;

            if (draw == nullptr || skin == nullptr || !SceneReady())
            {
                return;
            }

            float rgba[4] = {};
            Unpack(color, rgba);
            ToViewport(x, y, w, h);

            draw(static_cast<int>(reinterpret_cast<uintptr_t>(skin)), x, y, w, h, 0, rgba, 0, 0);
        }

        // One opaque white texel; blit multiplies by the colour so any rect is this stretched.
        AmodTextureId g_white = AMOD_INVALID_TEXTURE;

        AmodTextureId WhitePixel(AmodMod mod)
        {
            const AmodTextureApi* const textures = TextureApi::Table();

            if (textures == nullptr || textures->Create == nullptr || textures->IsValid == nullptr)
            {
                return AMOD_INVALID_TEXTURE;
            }

            // Remade when the mod that first asked for it took it away on unload
            if (g_white != AMOD_INVALID_TEXTURE && textures->IsValid(mod, g_white) != 0)
            {
                return g_white;
            }

            const uint32_t texel = 0xFFFFFFFF;
            g_white = textures->Create(mod, 1, 1, &texel, 0, 1);
            return g_white;
        }

        void __cdecl ApiFillRect(AmodMod mod, float x, float y, float w, float h, uint32_t color)
        {
            if (Resolve(mod) == nullptr)
            {
                return;
            }

            ApiDrawSprite(mod, WhitePixel(mod), x, y, w, h, color);
        }

        bool ElementMatches(const ElementSub& sub, const char* id)
        {
            return sub.id.empty() || (id != nullptr && sub.id == id);
        }

        bool StyleMatches(const char* id)
        {
            for (const StyleSub& sub : g_styleSubs)
            {
                if (sub.id.empty() || (id != nullptr && sub.id == id))
                {
                    return true;
                }
            }
            return false;
        }

        void __cdecl DrawStyledTextThunk(void* font, const char* text, int zero,
                                         const float* centre, float* scale, float* color,
                                         int align, int zero2)
        {
            void* const item = g_currentStyleItem;
            if (item != nullptr)
            {
                const char* const id =
                    static_cast<const char*>(item) + GameAddress::kMenuElementIdOffset;
                for (const StyleSub& sub : g_styleSubs)
                {
                    if ((sub.id.empty() || sub.id == id) && sub.callback != nullptr)
                    {
                        sub.callback(item, color, scale, sub.user);
                    }
                }
            }

            if (g_originalDraw != nullptr)
            {
                g_originalDraw(font, text, zero, centre, scale, color, align, zero2);
            }
        }

        void EnsureDrawSwapped()
        {
            if (g_drawSlot == nullptr)
            {
                return;
            }
            void* const current = *g_drawSlot;
            if (current == nullptr || current == reinterpret_cast<void*>(&DrawStyledTextThunk))
            {
                return;
            }
            g_originalDraw = reinterpret_cast<DrawStyledTextRoutine>(current);
            *g_drawSlot = reinterpret_cast<void*>(&DrawStyledTextThunk);
        }

        // The game's real styled-text routine, so a mod's Render::Text draws through it whether
        // or not the pointer has been swapped for the style thunk.
        DrawStyledTextRoutine ActiveTextDraw()
        {
            if (g_originalDraw != nullptr)
            {
                return g_originalDraw;
            }
            if (g_drawSlot != nullptr && *g_drawSlot != nullptr)
            {
                return reinterpret_cast<DrawStyledTextRoutine>(*g_drawSlot);
            }
            return nullptr;
        }

        void __cdecl DrawSpriteThunk(int sprite, float x, float y, float w, float h, int zero,
                                     float* color, int a7, int a8)
        {
            void* const item = g_currentStyleItem;
            if (item != nullptr && color != nullptr)
            {
                const char* const id =
                    static_cast<const char*>(item) + GameAddress::kMenuElementIdOffset;
                float scale[2] = { 1.0f, 1.0f };    // scale is baked into w/h for a sprite
                for (const StyleSub& sub : g_styleSubs)
                {
                    if ((sub.id.empty() || sub.id == id) && sub.callback != nullptr)
                    {
                        sub.callback(item, color, scale, sub.user);
                    }
                }
            }

            if (g_originalSprite != nullptr)
            {
                g_originalSprite(sprite, x, y, w, h, zero, color, a7, a8);
            }
        }

        void EnsureSpriteSwapped()
        {
            if (g_spriteSlot == nullptr)
            {
                return;
            }
            void* const current = *g_spriteSlot;
            if (current == nullptr || current == reinterpret_cast<void*>(&DrawSpriteThunk))
            {
                return;
            }
            g_originalSprite = reinterpret_cast<DrawSpriteRoutine>(current);
            *g_spriteSlot = reinterpret_cast<void*>(&DrawSpriteThunk);
        }

        void __cdecl DrawStaticTextThunk(void* a1, void* a2, int a3, void* a4, void* a5,
                                         float* color, int a7)
        {
            if (color != nullptr && !g_styleSubs.empty())
            {
                const float saved[4] = { color[0], color[1], color[2], color[3] };
                float scale[2] = { 1.0f, 1.0f };
                bool applied = false;
                for (const StyleSub& sub : g_styleSubs)
                {
                    if (sub.id.empty() && sub.callback != nullptr)
                    {
                        sub.callback(color, color, scale, sub.user);
                        applied = true;
                    }
                }

                if (g_originalStaticText != nullptr)
                {
                    g_originalStaticText(a1, a2, a3, a4, a5, color, a7);
                }

                if (applied)
                {
                    color[0] = saved[0];
                    color[1] = saved[1];
                    color[2] = saved[2];
                    color[3] = saved[3];
                }
                return;
            }

            if (g_originalStaticText != nullptr)
            {
                g_originalStaticText(a1, a2, a3, a4, a5, color, a7);
            }
        }

        void EnsureStaticTextSwapped()
        {
            if (g_staticTextSlot == nullptr)
            {
                return;
            }
            void* const current = *g_staticTextSlot;
            if (current == nullptr || current == reinterpret_cast<void*>(&DrawStaticTextThunk))
            {
                return;
            }
            g_originalStaticText = reinterpret_cast<DrawStaticTextRoutine>(current);
            *g_staticTextSlot = reinterpret_cast<void*>(&DrawStaticTextThunk);
        }

        // The render an element's vtable held before it was patched, found by the vtable.
        ElementRenderRoutine OriginalRender(void* vtable)
        {
            for (const HookedVtable& hooked : g_vtables)
            {
                if (hooked.vtable == vtable)
                {
                    return hooked.original;
                }
            }
            return nullptr;
        }

        void __fastcall ElementRenderThunk(void* element, void* unused)
        {
            if (element == nullptr)
            {
                return;
            }
            const ElementRenderRoutine original = OriginalRender(*reinterpret_cast<void**>(element));
            if (original == nullptr)
            {
                return;
            }

            if (g_elementSubs.empty() && g_styleSubs.empty())
            {
                original(element, unused);
                return;
            }

            const char* const id = static_cast<const char*>(element) + GameAddress::kMenuElementIdOffset;

            // A copy, so a callback cancelling a subscription cannot break the walk.
            const std::vector<ElementSub> snapshot = g_elementSubs;

            bool replace = false;
            for (const ElementSub& sub : snapshot)
            {
                if (!ElementMatches(sub, id))
                {
                    continue;
                }
                if ((sub.phases & AMOD_ELEMENT_REPLACE) != 0)
                {
                    replace = true;
                }
                if ((sub.phases & AMOD_ELEMENT_BEFORE) != 0 && sub.callback != nullptr)
                {
                    sub.callback(element, AMOD_ELEMENT_BEFORE, sub.user);
                }
            }

            if (!replace)
            {
                if (!g_styleSubs.empty())
                {
                    EnsureDrawSwapped();
                    EnsureSpriteSwapped();
                    EnsureStaticTextSwapped();  // covers the $static_text drawn in this same menu
                    if (StyleMatches(id))
                    {
                        g_currentStyleItem = element;
                    }
                }
                original(element, unused);
                g_currentStyleItem = nullptr;
            }

            for (const ElementSub& sub : snapshot)
            {
                if (ElementMatches(sub, id) && (sub.phases & AMOD_ELEMENT_AFTER) != 0 &&
                    sub.callback != nullptr)
                {
                    sub.callback(element, AMOD_ELEMENT_AFTER, sub.user);
                }
            }
        }

        void __fastcall PresentDetour(void* manager, void* unused)
        {
            if (!g_subscribers.empty() && manager != nullptr &&
                *(static_cast<char*>(manager) + GameAddress::kRenderDeviceLostOffset) == 0)
            {
                AmodFrame frame = {};
                frame.structSize = sizeof(AmodFrame);
                frame.delta = g_delta != nullptr ? *g_delta : 0.0f;
                g_elapsed += frame.delta;
                frame.elapsed = g_elapsed;
                frame.frameIndex = g_frameIndex++;

                // A copy, so a mod cancelling its callback from inside it cannot break the walk.
                const std::vector<Subscriber> snapshot = g_subscribers;
                for (const Subscriber& subscriber : snapshot)
                {
                    if (subscriber.callback != nullptr)
                    {
                        subscriber.callback(&frame, subscriber.user);
                    }
                }
            }

            // The loader's own screen draws over the mods, and without one subscribed.
            if (manager != nullptr && *(static_cast<char*>(manager) + GameAddress::kRenderDeviceLostOffset) == 0)
            {
                for (const InternalDrawRoutine draw : g_internalPresent)
                {
                    draw();
                }
            }

            g_originalPresent(manager, unused);

            for (const InternalDrawRoutine tick : g_afterPresent)
            {
                tick();
            }
        }

        char __fastcall BeginDetour(void* manager, void* unused)
        {
            const char result = g_originalBegin(manager, unused);
            if (result != 0 && !g_beginSubs.empty() && manager != nullptr &&
                *(static_cast<char*>(manager) + GameAddress::kRenderDeviceLostOffset) == 0)
            {
                AmodFrame frame = {};
                frame.structSize = sizeof(AmodFrame);
                frame.delta = g_delta != nullptr ? *g_delta : 0.0f;
                frame.elapsed = g_elapsed;      // Render_Present advances it; begin reads it as is
                frame.frameIndex = g_frameIndex;

                const std::vector<Subscriber> snapshot = g_beginSubs;
                for (const Subscriber& subscriber : snapshot)
                {
                    if (subscriber.callback != nullptr)
                    {
                        subscriber.callback(&frame, subscriber.user);
                    }
                }
            }
            return result;
        }

        void __fastcall DecorationsDetour(void* menu, void* unused)
        {
            if (g_bgSubs.empty() || menu == nullptr)
            {
                g_originalDecorations(menu, unused);
                return;
            }

            const char* const menuId =
                static_cast<const char*>(menu) + GameAddress::kMenuIdOffset;

            bool suppress = false;
            const std::vector<BgSub> snapshot = g_bgSubs;
            for (const BgSub& sub : snapshot)
            {
                if (sub.callback != nullptr && sub.callback(menuId, sub.user) != 0)
                {
                    suppress = true;
                }
            }

            if (!suppress)
            {
                g_originalDecorations(menu, unused);
            }
        }
    }

    bool RenderApi::Install()
    {
        void* const target = GameSymbols::Address(GameSymbol::RenderPresent);
        if (target == nullptr)
        {
            FORGE_LOG_WARN("Render_Present was not located, so the render interface is not offered");
            return false;
        }

        g_delta = static_cast<const float*>(GameModule::Resolve(GameAddress::kFrameDelta));

        std::string mismatch;
        if (!GameModule::VerifyBytes(target, kRenderPresentPrologue,
                                     sizeof(kRenderPresentPrologue), mismatch))
        {
            FORGE_LOG_ERROR("Render_Present does not open as expected (%s)", mismatch.c_str());
            return false;
        }

        std::string error;
        if (!g_hook.Install(target, reinterpret_cast<void*>(&PresentDetour),
                            kRenderPresentPrologue, sizeof(kRenderPresentPrologue), error))
        {
            FORGE_LOG_ERROR("Cannot hook Render_Present: %s", error.c_str());
            return false;
        }

        g_originalPresent = reinterpret_cast<PresentRoutine>(g_hook.Trampoline());

        void* const beginTarget = GameSymbols::Address(GameSymbol::RenderBegin);
        if (beginTarget != nullptr &&
            GameModule::VerifyBytes(beginTarget, kRenderBeginPrologue,
                                    sizeof(kRenderBeginPrologue), mismatch) &&
            g_beginHook.Install(beginTarget, reinterpret_cast<void*>(&BeginDetour),
                                kRenderBeginPrologue, sizeof(kRenderBeginPrologue), error))
        {
            g_originalBegin = reinterpret_cast<BeginRoutine>(g_beginHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("Render_Begin was not hooked, frame-begin callbacks are unavailable");
        }

        for (const uintptr_t vtableAddress : GameAddress::kElementVtables)
        {
            void** const slot = static_cast<void**>(GameModule::Resolve(vtableAddress));
            if (slot == nullptr)
            {
                continue;
            }
            DWORD previous = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous))
            {
                continue;
            }
            const ElementRenderRoutine original = reinterpret_cast<ElementRenderRoutine>(*slot);
            *slot = reinterpret_cast<void*>(&ElementRenderThunk);
            VirtualProtect(slot, sizeof(void*), previous, &previous);
            g_vtables.push_back(HookedVtable{ static_cast<void*>(slot), original });
        }
        if (g_vtables.empty())
        {
            FORGE_LOG_WARN("No element vtables were patched, element-draw callbacks are unavailable");
        }

        g_drawSlot = static_cast<void**>(GameModule::Resolve(GameAddress::kDrawStyledTextPtr));
        g_spriteSlot = static_cast<void**>(GameModule::Resolve(GameAddress::kDrawSpritePtr));
        g_staticTextSlot = static_cast<void**>(GameModule::Resolve(GameAddress::kDrawStaticTextPtr));
        g_findFont = reinterpret_cast<FindFontRoutine>(
            GameSymbols::Address(GameSymbol::MenuFindFont));

        void* const decorTarget = GameSymbols::Address(GameSymbol::MenuDrawDecorations);
        if (decorTarget != nullptr &&
            GameModule::VerifyBytes(decorTarget, kMenuDrawDecorationsPrologue,
                                    sizeof(kMenuDrawDecorationsPrologue), mismatch) &&
            g_decorHook.Install(decorTarget, reinterpret_cast<void*>(&DecorationsDetour),
                                kMenuDrawDecorationsPrologue, sizeof(kMenuDrawDecorationsPrologue),
                                error))
        {
            g_originalDecorations = reinterpret_cast<DecorationsRoutine>(g_decorHook.Trampoline());
        }
        else
        {
            FORGE_LOG_WARN("Menu_DrawDecorations was not hooked, custom backgrounds are unavailable");
        }

        g_table.structSize = sizeof(AmodRenderApi);
        g_table.GetDevice = &ApiGetDevice;
        g_table.GetViewport = &ApiGetViewport;
        g_table.GetWindow = &ApiGetWindow;
        g_table.DrawQuads = &ApiDrawQuads;
        g_table.DrawSpritePart = &ApiDrawSpritePart;
        g_table.Subscribe = &ApiSubscribe;
        g_table.Unsubscribe = &ApiUnsubscribe;
        g_table.SubscribeFrameBegin = &ApiSubscribeFrameBegin;
        g_table.OnElementDraw = &ApiOnElementDraw;
        g_table.CancelElementDraw = &ApiCancelElementDraw;
        g_table.OnItemStyle = &ApiOnItemStyle;
        g_table.CancelItemStyle = &ApiCancelItemStyle;
        g_table.OnMenuBackground = &ApiOnMenuBackground;
        g_table.CancelMenuBackground = &ApiCancelMenuBackground;
        g_table.GetFont = &ApiGetFont;
        g_table.DrawTextLine = &ApiDrawText;
        g_table.DrawTextLineAligned = &ApiDrawTextAligned;
        g_table.DrawSprite = &ApiDrawSprite;
        g_table.FillRect = &ApiFillRect;
        g_table.OnDevice = &ApiOnDevice;
        g_table.Cancel = &ApiCancel;
        g_table.DeviceGeneration = &ApiDeviceGeneration;

        D3d8Hook::SetSink(&OnDeviceEvent);

        FORGE_LOG_INFO("Render interface installed");
        return true;
    }

    void RenderApi::Uninstall()
    {
        // Put the game's own draw routines back before the thunks go away with the DLL.
        if (g_drawSlot != nullptr && g_originalDraw != nullptr &&
            *g_drawSlot == reinterpret_cast<void*>(&DrawStyledTextThunk))
        {
            *g_drawSlot = reinterpret_cast<void*>(g_originalDraw);
        }
        if (g_spriteSlot != nullptr && g_originalSprite != nullptr &&
            *g_spriteSlot == reinterpret_cast<void*>(&DrawSpriteThunk))
        {
            *g_spriteSlot = reinterpret_cast<void*>(g_originalSprite);
        }
        if (g_staticTextSlot != nullptr && g_originalStaticText != nullptr &&
            *g_staticTextSlot == reinterpret_cast<void*>(&DrawStaticTextThunk))
        {
            *g_staticTextSlot = reinterpret_cast<void*>(g_originalStaticText);
        }

        // Put every element vtable's render back before the thunk goes away with the DLL.
        for (const HookedVtable& hooked : g_vtables)
        {
            void** const slot = static_cast<void**>(hooked.vtable);
            DWORD previous = 0;
            if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous))
            {
                *slot = reinterpret_cast<void*>(hooked.original);
                VirtualProtect(slot, sizeof(void*), previous, &previous);
            }
        }
        g_vtables.clear();

        D3d8Hook::SetSink(nullptr);

        g_hook.Uninstall();
        g_beginHook.Uninstall();
        g_decorHook.Uninstall();
        g_originalPresent = nullptr;
        g_originalBegin = nullptr;
        g_originalDecorations = nullptr;
        g_findFont = nullptr;
        g_delta = nullptr;
        g_subscribers.clear();
        g_beginSubs.clear();
        g_elementSubs.clear();
        g_styleSubs.clear();
        g_bgSubs.clear();
        g_internalPresent.clear();
        g_afterPresent.clear();
        g_deviceSubs.clear();
        g_drawSlot = nullptr;
        g_originalDraw = nullptr;
        g_spriteSlot = nullptr;
        g_originalSprite = nullptr;
        g_staticTextSlot = nullptr;
        g_originalStaticText = nullptr;
        g_currentStyleItem = nullptr;
        g_table = {};
        g_elapsed = 0.0;
        g_frameIndex = 0;
    }

    bool RenderApi::CanDraw2D()
    {
        return ActiveSpriteDraw() != nullptr && SceneReady();
    }

    const AmodRenderApi* RenderApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void RenderApi::AddInternalPresent(void(__cdecl* draw)())
    {
        if (draw != nullptr)
        {
            g_internalPresent.push_back(draw);
        }
    }

    void RenderApi::AddAfterPresent(void(__cdecl* tick)())
    {
        if (tick != nullptr)
        {
            g_afterPresent.push_back(tick);
        }
    }

    bool RenderApi::IsPresentHooked()
    {
        return g_originalPresent != nullptr;
    }

    void RenderApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_subscribers.begin(); it != g_subscribers.end();)
        {
            it = it->owner == owner ? g_subscribers.erase(it) : ++it;
        }
        for (auto it = g_beginSubs.begin(); it != g_beginSubs.end();)
        {
            it = it->owner == owner ? g_beginSubs.erase(it) : ++it;
        }
        for (auto it = g_elementSubs.begin(); it != g_elementSubs.end();)
        {
            it = it->owner == owner ? g_elementSubs.erase(it) : ++it;
        }
        for (auto it = g_styleSubs.begin(); it != g_styleSubs.end();)
        {
            it = it->owner == owner ? g_styleSubs.erase(it) : ++it;
        }
        for (auto it = g_bgSubs.begin(); it != g_bgSubs.end();)
        {
            it = it->owner == owner ? g_bgSubs.erase(it) : ++it;
        }
        for (auto it = g_deviceSubs.begin(); it != g_deviceSubs.end();)
        {
            it = it->owner == owner ? g_deviceSubs.erase(it) : ++it;
        }
    }
}
