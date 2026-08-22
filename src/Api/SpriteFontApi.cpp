#include "src/Api/SpriteFontApi.h"
#include "src/Api/RenderApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Api/TextureApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Hook/InlineHook.h"

#include <RenderApi.h>
#include <string.h>
#include <memory>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace GameAddress;
        using ApiRead::Caller;
        using ApiRead::Fail;

        typedef void(__fastcall* DrawTextRoutine)(void* font, void* unused, const char* text, const float* pos, const float* scale, const float* colour, int length, float fixedWidth);
        typedef void(__fastcall* MeasureRoutine)(void* font, void* unused, const char* text, float* out, int length);
        typedef void*(__cdecl* FindFontRoutine)(const char* name);
        typedef void(__fastcall* EventTextRoutine)(void* unused);

        AmodSpriteFontApi g_table = {};

        DrawTextRoutine g_draw = nullptr;
        MeasureRoutine g_measure = nullptr;
        FindFontRoutine g_findFont = nullptr;

        struct Font
        {
            ModContext* owner;
            std::string name;
            AmodFontId id;
            void* borrowed;                 // the game's, when the mod only asked for one by name
            std::vector<uint8_t> bytes;
            std::vector<void*> textures;
            bool ownLineHeight;
        };

        struct Replaced
        {
            ModContext* owner;
            std::string name;
            void* was;
        };

        struct Line
        {
            ModContext* owner;
            void* font;
            std::string text;
            float x;
            float y;
            float scale;
            uint32_t color;
        };

        std::vector<std::unique_ptr<Font>> g_fonts;
        std::vector<Replaced> g_replaced;
        std::vector<Line> g_queued;
        AmodFontId g_nextId = 1;

        const size_t kQueueLimit = 256;

        InlineHook g_phaseHook;
        EventTextRoutine g_originalPhase = nullptr;

        Font* FontById(AmodFontId id)
        {
            for (const std::unique_ptr<Font>& font : g_fonts)
            {
                if (font->id == id)
                {
                    return font.get();
                }
            }
            return nullptr;
        }

        void* ObjectOf(const Font* font)
        {
            if (font == nullptr)
            {
                return nullptr;
            }
            return font->borrowed != nullptr ? font->borrowed : const_cast<uint8_t*>(font->bytes.data());
        }

        template <typename T>
        void Write(void* base, uintptr_t offset, const T& value)
        {
            memcpy(static_cast<uint8_t*>(base) + offset, &value, sizeof(T));
        }

        template <typename T>
        T Read(const void* base, uintptr_t offset)
        {
            T value;
            memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof(T));
            return value;
        }

        uint8_t* MenuFonts()
        {
            void** const slot = static_cast<void**>(GameModule::Resolve(kMenuFontTable));
            return slot != nullptr ? static_cast<uint8_t*>(*slot) : nullptr;
        }

        int MenuFontCount()
        {
            const int* const count = static_cast<const int*>(GameModule::Resolve(kMenuFontCount));
            return count != nullptr ? *count : 0;
        }

        uint8_t* MenuFontEntry(const char* name)
        {
            uint8_t* const table = MenuFonts();
            const int count = MenuFontCount();

            if (table == nullptr || name == nullptr)
            {
                return nullptr;
            }

            for (int at = 0; at < count; ++at)
            {
                uint8_t* const entry = table + at * kMenuFontStride;
                if (strcmp(reinterpret_cast<const char*>(entry), name) == 0)
                {
                    return entry;
                }
            }

            return nullptr;
        }

        // The pen steps by the glyph's width plus its bearing, so a font with no glyphs measures 0.
        float TallestGlyph(const Font* font)
        {
            float tallest = 0.0f;

            for (size_t ch = 0; ch < kFontGlyphCount; ++ch)
            {
                const uintptr_t glyph = kFontGlyphsOffset + ch * kFontGlyphStride;
                if (Read<void*>(font->bytes.data(), glyph + kGlyphTextureOffset) == nullptr)
                {
                    continue;
                }

                const float tall = Read<float>(font->bytes.data(), glyph + kGlyphTallOffset);
                tallest = tall > tallest ? tall : tallest;
            }

            return tallest;
        }

        // The array the font points at may move as atlases are added, so the font is re-pointed.
        void HoldTexture(Font* font, void* texture)
        {
            for (void* const held : font->textures)
            {
                if (held == texture)
                {
                    return;
                }
            }

            font->textures.push_back(texture);

            void** const first = font->textures.data();
            Write(font->bytes.data(), kFontTexturesOffset, first);
            Write(font->bytes.data(), kFontTextureCountOffset, static_cast<int>(font->textures.size()));
        }

        void Unpack(uint32_t color, float* rgba)
        {
            rgba[0] = ((color >> 16) & 0xFF) / 255.0f;
            rgba[1] = ((color >> 8) & 0xFF) / 255.0f;
            rgba[2] = (color & 0xFF) / 255.0f;
            rgba[3] = ((color >> 24) & 0xFF) / 255.0f;
        }

        float WidthOf(void* object, const char* text)
        {
            float out[2] = {};

            if (g_measure != nullptr && object != nullptr && text != nullptr)
            {
                g_measure(object, nullptr, text, out, 0);
            }

            return out[0];
        }

        void Viewport(float& wide, float& tall)
        {
            void** const slot = static_cast<void**>(GameModule::Resolve(kRenderManagerPtr));
            void* const manager = slot != nullptr ? *slot : nullptr;

            wide = AMOD_MENU_WIDE;
            tall = AMOD_MENU_TALL;

            if (manager == nullptr)
            {
                return;
            }

            const uint8_t* const info = static_cast<uint8_t*>(manager) + kRenderViewportInfoOffset;
            const int pixelsWide = Read<int>(info, kRenderViewportWidthOffset);
            const int pixelsTall = Read<int>(info, kRenderViewportHeightOffset);

            wide = pixelsWide > 0 ? static_cast<float>(pixelsWide) : AMOD_MENU_WIDE;
            tall = pixelsTall > 0 ? static_cast<float>(pixelsTall) : AMOD_MENU_TALL;
        }

        void Flush()
        {
            float wide = 0.0f;
            float tall = 0.0f;
            Viewport(wide, tall);

            // The ortho covers real pixels and counts y up; a mod speaks 640x480 counting down.
            const float across = wide / AMOD_MENU_WIDE;
            const float down = tall / AMOD_MENU_TALL;

            for (const Line& line : g_queued)
            {
                const float pos[2] = { line.x * across, tall - line.y * down };
                const float sizes[2] = { line.scale * across, line.scale * down };
                float rgba[4] = {};
                Unpack(line.color, rgba);

                g_draw(line.font, nullptr, line.text.c_str(), pos, sizes, rgba, 0, 0.0f);
            }

            g_queued.clear();
        }

        bool g_flushAtPresent = false;

        void __fastcall PhaseDetour(void* self)
        {
            g_originalPhase(self);

            if (!g_flushAtPresent)
            {
                Flush();
            }
        }

        void __cdecl FlushAtPresent()
        {
            Flush();
        }

        AmodFontId __cdecl ApiCreate(AmodMod mod, const char* name)
        {
            ModContext* const context = Caller(mod);

            if (context == nullptr)
            {
                return AMOD_FONT_NONE;
            }

            std::unique_ptr<Font> font(new Font());
            font->owner = context;
            font->name = name != nullptr ? name : "";
            font->id = g_nextId++;
            font->borrowed = nullptr;
            font->bytes.assign(kFontSize, 0);
            font->ownLineHeight = false;

            Write(font->bytes.data(), 0, kFontMagic);

            const AmodFontId id = font->id;
            g_fonts.push_back(std::move(font));
            return id;
        }

        int __cdecl ApiDestroy(AmodMod mod, AmodFontId id)
        {
            ModContext* const context = Caller(mod);
            const Font* const font = context != nullptr ? FontById(id) : nullptr;

            if (font == nullptr || font->owner != context)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no font %u of this mod's", id);
            }

            // A font still standing in for one of the game's would leave a dangling pointer.
            for (auto it = g_replaced.begin(); it != g_replaced.end();)
            {
                uint8_t* const entry = MenuFontEntry(it->name.c_str());
                const bool ours = entry != nullptr && Read<void*>(entry, kMenuFontOffset) == ObjectOf(font);

                if (ours)
                {
                    Write(entry, kMenuFontOffset, it->was);
                }
                it = ours ? g_replaced.erase(it) : ++it;
            }

            for (auto it = g_fonts.begin(); it != g_fonts.end(); ++it)
            {
                if ((*it)->id == id)
                {
                    g_fonts.erase(it);
                    return 1;
                }
            }

            return 0;
        }

        AmodFontId __cdecl ApiFromGame(AmodMod mod, const char* name)
        {
            ModContext* const context = Caller(mod);

            if (context == nullptr || g_findFont == nullptr)
            {
                return AMOD_FONT_NONE;
            }

            void* const object = (name != nullptr && name[0] != 0)
                                     ? g_findFont(name)
                                     : GameModule::Resolve(kSystemFont);

            if (object == nullptr)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "the game has no font called '%s'", name != nullptr ? name : "");
                return AMOD_FONT_NONE;
            }

            for (const std::unique_ptr<Font>& held : g_fonts)
            {
                if (held->borrowed == object && held->owner == context)
                {
                    return held->id;
                }
            }

            std::unique_ptr<Font> font(new Font());
            font->owner = context;
            font->name = name != nullptr ? name : "";
            font->id = g_nextId++;
            font->borrowed = object;
            font->ownLineHeight = true;

            const AmodFontId id = font->id;
            g_fonts.push_back(std::move(font));
            return id;
        }

        Font* Mine(AmodMod mod, AmodFontId id)
        {
            ModContext* const context = Caller(mod);
            Font* const font = context != nullptr ? FontById(id) : nullptr;

            if (font == nullptr || font->owner != context || font->borrowed != nullptr)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "there is no font %u this mod can change", id);
                return nullptr;
            }

            return font;
        }

        int __cdecl ApiSetGlyph(AmodMod mod, AmodFontId id, int ch, AmodTextureId texture, float x, float y, float width, float height, float offsetX, float offsetY)
        {
            Font* const font = Mine(mod, id);
            uint8_t* const skin = font != nullptr
                                      ? static_cast<uint8_t*>(TextureApi::TextureFor(texture))
                                      : nullptr;

            if (font == nullptr)
            {
                return 0;
            }
            if (skin == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no texture %u to cut a glyph from", texture);
            }
            if (ch < 0 || ch >= static_cast<int>(kFontGlyphCount))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a glyph is one of 256 characters, not %d", ch);
            }

            const int wide = ApiRead::Read<int>(skin, kTextureWidthOffset);
            const int tall = ApiRead::Read<int>(skin, kTextureHeightOffset);

            if (wide <= 0 || tall <= 0)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "texture %u has no size yet", texture);
            }

            uint8_t* const glyph = font->bytes.data() + kFontGlyphsOffset + ch * kFontGlyphStride;

            Write(glyph, kGlyphTextureOffset, static_cast<void*>(skin));
            Write(glyph, kGlyphUOffset, x / wide);
            Write(glyph, kGlyphVOffset, y / tall);
            Write(glyph, kGlyphUWideOffset, width / wide);
            Write(glyph, kGlyphVTallOffset, height / tall);
            Write(glyph, kGlyphWideOffset, width);
            Write(glyph, kGlyphTallOffset, height);
            Write(glyph, kGlyphOffsetXOffset, offsetX);
            Write(glyph, kGlyphOffsetYOffset, offsetY);

            HoldTexture(font, skin);

            if (!font->ownLineHeight)
            {
                Write(font->bytes.data(), kFontLineHeightOffset, TallestGlyph(font));
            }

            return 1;
        }

        int __cdecl ApiSetGrid(AmodMod mod, AmodFontId id, AmodTextureId texture, const char* chars, float cellWidth, float cellHeight, float offsetX)
        {
            Font* const font = Mine(mod, id);
            uint8_t* const skin = font != nullptr
                                      ? static_cast<uint8_t*>(TextureApi::TextureFor(texture))
                                      : nullptr;

            if (font == nullptr)
            {
                return 0;
            }
            if (skin == nullptr || chars == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a grid wants a texture and the characters on it");
            }
            if (cellWidth <= 0.0f || cellHeight <= 0.0f)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a grid cell cannot be %gx%g", cellWidth, cellHeight);
            }

            const int wide = ApiRead::Read<int>(skin, kTextureWidthOffset);
            const int columns = wide > 0 ? static_cast<int>(wide / cellWidth) : 0;

            if (columns <= 0)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "a %g wide cell does not fit texture %u", cellWidth, texture);
            }

            int placed = 0;
            for (int at = 0; chars[at] != 0; ++at)
            {
                const float x = static_cast<float>(at % columns) * cellWidth;
                const float y = static_cast<float>(at / columns) * cellHeight;

                if (ApiSetGlyph(mod, id, static_cast<uint8_t>(chars[at]), texture, x, y, cellWidth, cellHeight, offsetX, 0.0f) != 0)
                {
                    ++placed;
                }
            }

            return placed;
        }

        int __cdecl ApiSetLineHeight(AmodMod mod, AmodFontId id, float height)
        {
            Font* const font = Mine(mod, id);

            if (font == nullptr)
            {
                return 0;
            }

            font->ownLineHeight = true;
            Write(font->bytes.data(), kFontLineHeightOffset, height);
            return 1;
        }

        float __cdecl ApiGetLineHeight(AmodMod mod, AmodFontId id)
        {
            ModContext* const context = Caller(mod);
            const Font* const font = context != nullptr ? FontById(id) : nullptr;
            void* const object = ObjectOf(font);

            return object != nullptr ? Read<float>(object, kFontLineHeightOffset) : 0.0f;
        }

        float __cdecl ApiMeasure(AmodMod mod, AmodFontId id, const char* text, float scale)
        {
            ModContext* const context = Caller(mod);
            const Font* const font = context != nullptr ? FontById(id) : nullptr;

            return WidthOf(ObjectOf(font), text) * scale;
        }

        // A line waits for the one moment the game is in 2D rather than drawing where it was asked.
        void __cdecl ApiDraw(AmodMod mod, AmodFontId id, float x, float y, const char* text, float scale, uint32_t color, int align)
        {
            ModContext* const context = Caller(mod);
            const Font* const font = context != nullptr ? FontById(id) : nullptr;
            void* const object = ObjectOf(font);

            if (object == nullptr || text == nullptr || g_draw == nullptr ||
                g_queued.size() >= kQueueLimit)
            {
                return;
            }

            float left = x;
            if (align == AMOD_FONT_CENTRE || align == AMOD_FONT_RIGHT)
            {
                const float width = WidthOf(object, text) * scale;
                left = align == AMOD_FONT_CENTRE ? x - width * 0.5f : x - width;
            }

            g_queued.push_back(Line{ context, object, text, left, y, scale, color });
        }

        int __cdecl ApiGameFontCount(AmodMod mod)
        {
            return Caller(mod) != nullptr ? MenuFontCount() : 0;
        }

        const char* __cdecl ApiGameFontName(AmodMod mod, int index)
        {
            uint8_t* const table = Caller(mod) != nullptr ? MenuFonts() : nullptr;

            if (table == nullptr || index < 0 || index >= MenuFontCount())
            {
                return "";
            }

            return reinterpret_cast<const char*>(table + index * kMenuFontStride);
        }

        int __cdecl ApiReplace(AmodMod mod, const char* gameFont, AmodFontId id)
        {
            ModContext* const context = Caller(mod);
            const Font* const font = context != nullptr ? FontById(id) : nullptr;
            uint8_t* const entry = context != nullptr ? MenuFontEntry(gameFont) : nullptr;
            void* const object = ObjectOf(font);

            if (font == nullptr || object == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no font %u", id);
            }
            if (entry == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "the game has no font called '%s'", gameFont != nullptr ? gameFont : "");
            }

            for (const Replaced& held : g_replaced)
            {
                if (held.name == gameFont)
                {
                    Write(entry, kMenuFontOffset, object);
                    return 1;
                }
            }

            g_replaced.push_back(Replaced{ context, gameFont, Read<void*>(entry, kMenuFontOffset) });
            Write(entry, kMenuFontOffset, object);
            return 1;
        }

        int __cdecl ApiRestore(AmodMod mod, const char* gameFont)
        {
            ModContext* const context = Caller(mod);

            if (context == nullptr || gameFont == nullptr)
            {
                return 0;
            }

            for (auto it = g_replaced.begin(); it != g_replaced.end(); ++it)
            {
                if (it->name != gameFont || it->owner != context)
                {
                    continue;
                }

                uint8_t* const entry = MenuFontEntry(gameFont);
                if (entry != nullptr)
                {
                    Write(entry, kMenuFontOffset, it->was);
                }

                g_replaced.erase(it);
                return 1;
            }

            return Fail(mod, AMOD_FAIL_NO_SUCH, "this mod is not drawing over '%s'", gameFont);
        }
    }

    bool SpriteFontApi::Install()
    {
        g_draw = reinterpret_cast<DrawTextRoutine>(GameModule::Resolve(kFontDrawText));
        g_measure = reinterpret_cast<MeasureRoutine>(GameModule::Resolve(kFontMeasure));
        g_findFont = reinterpret_cast<FindFontRoutine>(GameModule::Resolve(kMenuFindFont));

        if (g_draw == nullptr || g_measure == nullptr || g_findFont == nullptr)
        {
            FORGE_LOG_WARN("The game's font routines were not located, so the sprite font "
                           "interface is not offered");
            return false;
        }

        void* const phase = GameModule::Resolve(kEventTextRender);
        std::string mismatch;

        if (phase == nullptr || !GameModule::VerifyBytes(phase, kEventTextRenderPrologue, sizeof(kEventTextRenderPrologue), mismatch))
        {
            FORGE_LOG_WARN("The game's 2D phase does not open as expected, so text a mod draws "
                           "would land in the world");
            return false;
        }

        std::string error;
        if (!g_phaseHook.Install(phase, reinterpret_cast<void*>(&PhaseDetour), kEventTextRenderPrologue, sizeof(kEventTextRenderPrologue), error))
        {
            FORGE_LOG_ERROR("Cannot hook the 2D phase: %s", error.c_str());
            return false;
        }

        g_originalPhase = reinterpret_cast<EventTextRoutine>(g_phaseHook.Trampoline());

        g_table.structSize = sizeof(AmodSpriteFontApi);
        g_table.Create = &ApiCreate;
        g_table.Destroy = &ApiDestroy;
        g_table.FromGame = &ApiFromGame;
        g_table.SetGlyph = &ApiSetGlyph;
        g_table.SetGrid = &ApiSetGrid;
        g_table.SetLineHeight = &ApiSetLineHeight;
        g_table.GetLineHeight = &ApiGetLineHeight;
        g_table.Measure = &ApiMeasure;
        g_table.Draw = &ApiDraw;
        g_table.GameFontCount = &ApiGameFontCount;
        g_table.GameFontName = &ApiGameFontName;
        g_table.Replace = &ApiReplace;
        g_table.Restore = &ApiRestore;

        g_flushAtPresent = RenderApi::Table() != nullptr;

        if (g_flushAtPresent)
        {
            RenderApi::AddInternalPresent(&FlushAtPresent);
        }

        FORGE_LOG_INFO("Sprite font interface installed");
        return true;
    }

    void SpriteFontApi::Uninstall()
    {
        // Before the fonts go: the menu table can be holding one of them.
        for (const Replaced& held : g_replaced)
        {
            uint8_t* const entry = MenuFontEntry(held.name.c_str());
            if (entry != nullptr)
            {
                Write(entry, kMenuFontOffset, held.was);
            }
        }

        g_phaseHook.Uninstall();
        g_originalPhase = nullptr;
        g_queued.clear();
        g_replaced.clear();
        g_fonts.clear();
        g_draw = nullptr;
        g_measure = nullptr;
        g_findFont = nullptr;
        g_table = AmodSpriteFontApi{};
    }

    const AmodSpriteFontApi* SpriteFontApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void SpriteFontApi::ForgetMod(ModContext* owner)
    {
        // Anything the mod queued this frame would draw with bytes that are about to go.
        for (auto it = g_queued.begin(); it != g_queued.end();)
        {
            it = it->owner == owner ? g_queued.erase(it) : ++it;
        }

        // The game's own font goes back first, or it would draw with freed bytes.
        for (auto it = g_replaced.begin(); it != g_replaced.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            uint8_t* const entry = MenuFontEntry(it->name.c_str());
            if (entry != nullptr)
            {
                Write(entry, kMenuFontOffset, it->was);
            }

            it = g_replaced.erase(it);
        }

        for (auto it = g_fonts.begin(); it != g_fonts.end();)
        {
            it = (*it)->owner == owner ? g_fonts.erase(it) : ++it;
        }
    }
}
