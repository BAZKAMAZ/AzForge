#ifndef AZFORGE_SPRITE_FONT_API_H
#define AZFORGE_SPRITE_FONT_API_H

#include "ForgeTypes.h"
#include "TextureApi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A font of the mod's own, or one of the game's borrowed by name. */
typedef uint32_t AmodFontId;

#define AMOD_FONT_NONE                                              0

/* Where x sits on the line the text is drawn on. */
typedef enum AmodFontAlign
{
    AMOD_FONT_LEFT   = 0,
    AMOD_FONT_CENTRE = 1,
    AMOD_FONT_RIGHT  = 2
} AmodFontAlign;

typedef struct AmodSpriteFontApi
{
    uint32_t structSize;

    /* Creates an empty 256-glyph font. */
    AmodFontId(__cdecl* Create)(AmodMod mod, const char* name);
    int(__cdecl* Destroy)(AmodMod mod, AmodFontId font);

    /* Gets a game font by name. */
    AmodFontId(__cdecl* FromGame)(AmodMod mod, const char* name);

    /* Sets one glyph in texture pixels. */
    int(__cdecl* SetGlyph)(AmodMod mod, AmodFontId font, int ch, AmodTextureId texture, float x, float y, float width, float height, float offsetX, float offsetY);

    /* Fills glyphs from a regular grid. */
    int(__cdecl* SetGrid)(AmodMod mod, AmodFontId font, AmodTextureId texture, const char* chars, float cellWidth, float cellHeight, float offsetX);

    /* Sets or gets line height. */
    int(__cdecl* SetLineHeight)(AmodMod mod, AmodFontId font, float height);
    float(__cdecl* GetLineHeight)(AmodMod mod, AmodFontId font);

    /* Measures text width at scale. */
    float(__cdecl* Measure)(AmodMod mod, AmodFontId font, const char* text, float scale);

    /* Draws text in 640x480 menu space. */
    void(__cdecl* Draw)(AmodMod mod, AmodFontId font, float x, float y, const char* text, float scale, uint32_t color, int align);

    /* Lists game fonts by name. */
    int(__cdecl* GameFontCount)(AmodMod mod);
    const char* (__cdecl* GameFontName)(AmodMod mod, int index);

    /* Replaces a game font until restored. */
    int(__cdecl* Replace)(AmodMod mod, const char* gameFont, AmodFontId font);
    int(__cdecl* Restore)(AmodMod mod, const char* gameFont);

} AmodSpriteFontApi;

AMOD_LAST_FIELD(AmodSpriteFontApi, Restore, 0x34);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace SpriteFont
    {
        inline const AmodSpriteFontApi* g_fonts = nullptr;
        inline AmodMod g_fontSelf = nullptr;

        inline void                                                 Attach(const AmodSpriteFontApi* api, AmodMod self)
        {
            g_fonts = api;
            g_fontSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_fonts != nullptr;
        }

        inline AmodFontId                                           Create(const char* name)
        {
            return g_fonts != nullptr ? g_fonts->Create(g_fontSelf, name) : AMOD_FONT_NONE;
        }

        inline bool                                                 Destroy(AmodFontId font)
        {
            return g_fonts != nullptr && g_fonts->Destroy(g_fontSelf, font) != 0;
        }

        inline AmodFontId                                           FromGame(const char* name)
        {
            return g_fonts != nullptr ? g_fonts->FromGame(g_fontSelf, name) : AMOD_FONT_NONE;
        }

        inline bool                                                 SetGlyph(AmodFontId font, int ch, AmodTextureId texture, float x, float y, float width, float height, float offsetX = 1.0f, float offsetY = 0.0f)
        {
            return g_fonts != nullptr &&
                   g_fonts->SetGlyph(g_fontSelf, font, ch, texture, x, y, width, height, offsetX, offsetY) != 0;
        }

        inline bool                                                 SetGrid(AmodFontId font, AmodTextureId texture, const char* chars, float cellWidth, float cellHeight, float offsetX = 1.0f)
        {
            return g_fonts != nullptr &&
                   g_fonts->SetGrid(g_fontSelf, font, texture, chars, cellWidth, cellHeight, offsetX) != 0;
        }

        inline bool                                                 SetLineHeight(AmodFontId font, float height)
        {
            return g_fonts != nullptr && g_fonts->SetLineHeight(g_fontSelf, font, height) != 0;
        }

        inline float                                                LineHeight(AmodFontId font)
        {
            return g_fonts != nullptr ? g_fonts->GetLineHeight(g_fontSelf, font) : 0.0f;
        }

        inline float                                                Measure(AmodFontId font, const char* text, float scale = 1.0f)
        {
            return g_fonts != nullptr ? g_fonts->Measure(g_fontSelf, font, text, scale) : 0.0f;
        }

        inline void                                                 Draw(AmodFontId font, float x, float y, const char* text, float scale = 1.0f, unsigned long color = 0xFFFFFFFF, int align = AMOD_FONT_LEFT)
        {
            if (g_fonts != nullptr)
            {
                g_fonts->Draw(g_fontSelf, font, x, y, text, scale, static_cast<uint32_t>(color), align);
            }
        }

        inline int                                                  GameFontCount()
        {
            return g_fonts != nullptr ? g_fonts->GameFontCount(g_fontSelf) : 0;
        }

        inline const char*                                          GameFontName(int index)
        {
            return g_fonts != nullptr ? g_fonts->GameFontName(g_fontSelf, index) : "";
        }

        inline bool                                                 Replace(const char* gameFont, AmodFontId font)
        {
            return g_fonts != nullptr && g_fonts->Replace(g_fontSelf, gameFont, font) != 0;
        }

        inline bool                                                 Restore(const char* gameFont)
        {
            return g_fonts != nullptr && g_fonts->Restore(g_fontSelf, gameFont) != 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_SPRITE_FONT_API_H */
