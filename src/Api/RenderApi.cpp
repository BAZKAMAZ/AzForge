#include "pch.h"

#include "src/Api/RenderApi.h"

#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        // Render_Present and every element's render method are __thiscall (this in ecx); both
        // are emulated as __fastcall with an unused edx, the way the menu routines are.
        typedef void(__fastcall* PresentRoutine)(void* manager, void* unused);
        typedef char(__fastcall* BeginRoutine)(void* manager, void* unused);
        typedef void(__fastcall* ElementRenderRoutine)(void* element, void* unused);
        typedef void(__fastcall* DecorationsRoutine)(void* menu, void* unused);

        // The game's styled-text draw, called through a global pointer; see kDrawStyledTextPtr.
        typedef void(__cdecl* DrawStyledTextRoutine)(void* font, const char* text, int zero,
                                                     const float* centre, float* scale,
                                                     float* color, int align, int zero2);

        // The game's sprite blit, called through a global pointer; see kDrawSpritePtr. colour is
        // four floats or null (a logo passes null).
        typedef void(__cdecl* DrawSpriteRoutine)(int sprite, float x, float y, float w, float h,
                                                 int zero, float* color, int a7, int a8);

        // The game's $static_text draw, through a global pointer; see kDrawStaticTextPtr. colour
        // (a6) points at four floats in the record.
        typedef void(__cdecl* DrawStaticTextRoutine)(void* a1, void* a2, int a3, void* a4, void* a5,
                                                     float* color, int a7);

        // Resolves a named menu font to its font object; see kMenuFindFont.
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

        InlineHook g_hook;
        PresentRoutine g_originalPresent = nullptr;
        const float* g_delta = nullptr;

        // A detour on Render_Begin drives the frame-begin callbacks, run after BeginScene and
        // before the game draws - where a mod sets render state for the whole frame or draws
        // under everything.
        InlineHook g_beginHook;
        BeginRoutine g_originalBegin = nullptr;

        // Slot 0 (the render) of each element kind's vtable is patched with ElementRenderThunk;
        // the render it held is kept here, found again by the vtable an element carries at
        // element+0. One thunk in every vtable is what makes the element hooks cover every kind.
        struct HookedVtable
        {
            void* vtable;                   // patched slot 0 address, == the element's vtable
            ElementRenderRoutine original;  // render it held before patching
        };
        std::vector<HookedVtable> g_vtables;

        // A detour on Menu_DrawDecorations; when a mod draws a menu's background the original is
        // skipped so the game's own background is replaced.
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
        AmodToken g_nextToken = 1;

        // The game's styled-text pointer (kDrawStyledTextPtr) and the routine it held before it
        // was pointed at the thunk. g_currentStyleItem is the item being drawn while the thunk
        // runs, so the thunk knows which style callbacks apply.
        void** g_drawSlot = nullptr;
        DrawStyledTextRoutine g_originalDraw = nullptr;
        void* g_currentStyleItem = nullptr;

        // The sprite blit pointer and the routine it held; the sprite kinds are styled by
        // adjusting the colour it is handed, the way the text kinds are through g_originalDraw.
        void** g_spriteSlot = nullptr;
        DrawSpriteRoutine g_originalSprite = nullptr;

        // The $static_text draw pointer and the routine it held. This one is used only for
        // $static_text decorations, so every call through it is one, and no per-element context
        // is needed: the wildcard style callbacks apply, and the record's colour is restored
        // after the draw so the decoration itself is not permanently changed.
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

        // The live device, or null when there is no manager yet or the device is lost. Reading
        // the lost flag keeps a callback from drawing on a device the game itself is skipping.
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
            // Styling rides on the element-render hooks and the draw pointers; without the
            // element hooks, or with neither draw pointer to adjust, there is nothing to hang
            // it on.
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

        DrawStyledTextRoutine ActiveTextDraw();   // defined below, with the styled-text thunk

        void* __cdecl ApiGetFont(AmodMod mod, const char* name)
        {
            if (Resolve(mod) == nullptr || g_findFont == nullptr)
            {
                return nullptr;
            }
            return g_findFont(name);   // a null name resolves to the default font
        }

        // Draws text with a menu font (or the system font when font is null), centred at (x, y),
        // through the same routine the menu draws its labels with. Coordinates are in the
        // projection active when it runs - logical 640x480 inside a menu-render callback, the
        // backbuffer otherwise.
        void __cdecl ApiDrawText(AmodMod mod, void* font, float x, float y, const char* text,
                                 float scale, uint32_t color)
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

            float position[2] = { x, y };
            float scaling[2] = { scale, scale };
            float rgba[4] =
            {
                ((color >> 16) & 0xFF) / 255.0f, // R
                ((color >> 8) & 0xFF) / 255.0f,  // G
                (color & 0xFF) / 255.0f,         // B
                ((color >> 24) & 0xFF) / 255.0f, // A
            };
            draw(font, text, 0, position, scaling, rgba, 0x24, 0);
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

        // Replaces the game's styled-text draw. When an item with a style callback is being
        // drawn (g_currentStyleItem set by the item detour), the callbacks adjust the colour
        // and scale in place; then the real routine draws the item with the adjusted values.
        // For every other styled-text draw it is a straight pass-through.
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

        // Points the game's styled-text pointer at the thunk, capturing the real routine. Lazy
        // because the game fills the pointer only once it is running, and recaptures if the game
        // ever writes a fresh routine over it.
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

        // The sprite counterpart of DrawStyledTextThunk: adjusts the colour a sprite element is
        // drawn with while it draws, then blits it. A null colour (a logo) is left alone.
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

        // Recolours a $static_text decoration. Every call through this pointer is a static_text
        // draw, so the wildcard (id-less) style callbacks apply, keyed by the record's colour
        // buffer. The record's colour is saved and restored around the draw so the decoration is
        // adjusted only for this frame, not permanently.
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

        // Runs in place of the render of an element of any kind: fires the BEFORE callbacks, draws
        // the element through its own original render unless a callback asked to replace it, then
        // fires the AFTER callbacks. While it draws, the element is named to the styled-text thunk
        // so style callbacks recolour and resize it. The scene is open, so a callback can draw
        // through the device.
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
                // Style callbacks act through the text and sprite thunks while the element draws,
                // so it is named to them only for the length of its own draw. Text kinds route
                // through the styled-text thunk, sprite kinds through the sprite thunk.
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

        // Runs at the top of Render_Present, before the game ends the scene: the frame is fully
        // drawn and the scene is open, so a callback's draw lands over everything.
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

            g_originalPresent(manager, unused);
        }

        // Runs at the top of Render_Begin, after BeginScene but before the game draws the world
        // and menu. A callback here sets render state that the whole frame is then drawn with, or
        // draws a layer under everything. Only fires when the frame will actually be drawn.
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

        // Runs in place of a menu's background-decoration draw. When a mod draws the background,
        // the callback returns non-zero and the game's own decorations are skipped; the menu's
        // text and buttons still draw afterwards, on top. The scene is open here, so a callback
        // can draw through the device.
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

        // The frame-begin hook is additive: without it the overlay and the rest still work, and
        // OnFrameBegin simply refuses registrations.
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

        // Element-draw hooking patches slot 0 (the render) of every element kind's vtable with a
        // shared thunk. It is additive: a build without these vtables still gets the frame
        // overlay, and the element callbacks simply refuse registrations.
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

        // The draw pointers are null until the game is running, so they are only located here;
        // each swap is done lazily once an element with a style callback is first drawn.
        g_drawSlot = static_cast<void**>(GameModule::Resolve(GameAddress::kDrawStyledTextPtr));
        g_spriteSlot = static_cast<void**>(GameModule::Resolve(GameAddress::kDrawSpritePtr));
        g_staticTextSlot = static_cast<void**>(GameModule::Resolve(GameAddress::kDrawStaticTextPtr));
        g_findFont = reinterpret_cast<FindFontRoutine>(
            GameSymbols::Address(GameSymbol::MenuFindFont));

        // Custom-background hooking is additive too: without it the rest of the render interface
        // still works, and OnMenuBackground refuses registrations.
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

    const AmodRenderApi* RenderApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void RenderApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_subscribers.begin(); it != g_subscribers.end();)
        {
            it = it->owner == owner ? g_subscribers.erase(it) : it + 1;
        }
        for (auto it = g_beginSubs.begin(); it != g_beginSubs.end();)
        {
            it = it->owner == owner ? g_beginSubs.erase(it) : it + 1;
        }
        for (auto it = g_elementSubs.begin(); it != g_elementSubs.end();)
        {
            it = it->owner == owner ? g_elementSubs.erase(it) : it + 1;
        }
        for (auto it = g_styleSubs.begin(); it != g_styleSubs.end();)
        {
            it = it->owner == owner ? g_styleSubs.erase(it) : it + 1;
        }
        for (auto it = g_bgSubs.begin(); it != g_bgSubs.end();)
        {
            it = it->owner == owner ? g_bgSubs.erase(it) : it + 1;
        }
    }
}
