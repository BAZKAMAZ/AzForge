#ifndef AZFORGE_TEXTURE_API_H
#define AZFORGE_TEXTURE_API_H

#include "ForgeTypes.h"

typedef uint32_t AmodTextureId;
#define AMOD_INVALID_TEXTURE                                        0

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AmodTexture
{
    uint32_t structSize;

    const char* name; /* "" when it was made from pixels rather than read */
    int width;
    int height;
    int mipmapped;
    int hasAlpha; /* which of the two formats it was created in */
    int madeHere; /* built from pixels, by a mod or by the game */
    int references; /* extra owners besides the first */

} AmodTexture;

AMOD_LAST_FIELD(AmodTexture, references, 0x1C);

typedef struct AmodTextureApi
{
    uint32_t structSize;

    int(__cdecl* GetCount)(AmodMod mod);
    AmodTextureId(__cdecl* GetAt)(AmodMod mod, int index);
    AmodTextureId(__cdecl* Find)(AmodMod mod, const char* name);
    int(__cdecl* Get)(AmodMod mod, AmodTextureId texture, AmodTexture* out);
    int(__cdecl* IsValid)(AmodMod mod, AmodTextureId texture);

    /* Through the game's own door */
    AmodTextureId(__cdecl* Load)(AmodMod mod, const char* path, int mipmapped, int hasAlpha);
    int(__cdecl* Release)(AmodMod mod, AmodTextureId texture);

    /* Creates a texture from RGBA pixels. */
    AmodTextureId(__cdecl* Create)(AmodMod mod, int width, int height, const void* pixels, int mipmapped, int hasAlpha);

    /* Replaces existing texture pixels, in the same order. */
    int(__cdecl* Replace)(AmodMod mod, AmodTextureId texture, int width, int height, const void* pixels);

    /* Redirects requests for a texture path. */
    int(__cdecl* Override)(AmodMod mod, const char* path, AmodTextureId texture);
    int(__cdecl* ClearOverride)(AmodMod mod, const char* path);

    int(__cdecl* ReadPixels)(AmodMod mod, AmodTextureId texture, void* buffer, int capacity);

    /* Converts between AmodTextureId and game handles. */
    AmodTextureId(__cdecl* FromGame)(AmodMod mod, uint32_t handle);
    uint32_t(__cdecl* ToGame)(AmodMod mod, AmodTextureId texture);

    /* Saves a texture to a file. */
    int(__cdecl* Save)(AmodMod mod, AmodTextureId texture, const char* path);

} AmodTextureApi;

AMOD_LAST_FIELD(AmodTextureApi, Save, 0x3C);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Textures
    {
        inline const AmodTextureApi* g_textures = nullptr;
        inline AmodMod g_textureSelf = nullptr;

        inline void                                                 Attach(const AmodTextureApi* api, AmodMod self)
        {
            g_textures = api;
            g_textureSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_textures != nullptr;
        }

        inline int                                                  Count()
        {
            return g_textures != nullptr ? g_textures->GetCount(g_textureSelf) : 0;
        }

        inline AmodTextureId                                        At(int index)
        {
            return g_textures != nullptr ? g_textures->GetAt(g_textureSelf, index)
                : AMOD_INVALID_TEXTURE;
        }

        inline AmodTextureId                                        Find(const char* name)
        {
            return g_textures != nullptr ? g_textures->Find(g_textureSelf, name)
                : AMOD_INVALID_TEXTURE;
        }

        inline bool                                                 Get(AmodTextureId texture, AmodTexture& out)
        {
            out = AmodTexture{};
            out.structSize = sizeof(out);
            return g_textures != nullptr && g_textures->Get(g_textureSelf, texture, &out) != 0;
        }

        inline bool                                                 IsValid(AmodTextureId texture)
        {
            return g_textures != nullptr && g_textures->IsValid(g_textureSelf, texture) != 0;
        }

        inline AmodTextureId                                        Load(const char* path, bool mipmapped = true,
            bool hasAlpha = false)
        {
            return g_textures != nullptr
                ? g_textures->Load(g_textureSelf, path, mipmapped ? 1 : 0, hasAlpha ? 1 : 0)
                : AMOD_INVALID_TEXTURE;
        }

        inline bool                                                 Release(AmodTextureId texture)
        {
            return g_textures != nullptr && g_textures->Release(g_textureSelf, texture) != 0;
        }

        /* One pixel for Create and Replace, whose buffer is bytes R, G, B, A. */
        inline uint32_t                                             Pixel(int red, int green, int blue, int alpha = 255)
        {
            return static_cast<uint32_t>(red & 0xFF) | (static_cast<uint32_t>(green & 0xFF) << 8) |
                   (static_cast<uint32_t>(blue & 0xFF) << 16) | (static_cast<uint32_t>(alpha & 0xFF) << 24);
        }

        /* The same, out of a colour written the way the rest of the interface writes one. */
        inline uint32_t                                             PixelOf(uint32_t argb)
        {
            return Pixel(static_cast<int>((argb >> 16) & 0xFF), static_cast<int>((argb >> 8) & 0xFF),
                static_cast<int>(argb & 0xFF), static_cast<int>((argb >> 24) & 0xFF));
        }

        inline AmodTextureId                                        Create(int width, int height, const void* pixels,
            bool mipmapped = true,
            bool hasAlpha = false)
        {
            return (g_textures != nullptr && g_textures->Create != nullptr)
                ? g_textures->Create(g_textureSelf, width, height, pixels,
                    mipmapped ? 1 : 0, hasAlpha ? 1 : 0)
                : AMOD_INVALID_TEXTURE;
        }

        /* How many bytes there are to read; pass a buffer to get them. */
        inline int                                                  ReadPixels(AmodTextureId texture, void* buffer = nullptr, int capacity = 0)
        {
            return (g_textures != nullptr && g_textures->ReadPixels != nullptr)
                ? g_textures->ReadPixels(g_textureSelf, texture, buffer, capacity)
                : 0;
        }

        inline bool                                                 Replace(AmodTextureId texture, int width,
            int height, const void* pixels)
        {
            return g_textures != nullptr && g_textures->Replace != nullptr &&
                g_textures->Replace(g_textureSelf, texture, width, height, pixels) != 0;
        }

        /* What a draw handler was told, as something the rest of the interface will take. */
        inline AmodTextureId                                        FromGame(uint32_t handle)
        {
            return (g_textures != nullptr && g_textures->structSize >= sizeof(AmodTextureApi) && g_textures->FromGame != nullptr)
                ? g_textures->FromGame(g_textureSelf, handle)
                : AMOD_INVALID_TEXTURE;
        }

        /* And back, for Mmd::Enqueue and Mmd::DrawWith, which speak the game's handle. */
        inline uint32_t                                             ToGame(AmodTextureId texture)
        {
            return (g_textures != nullptr && g_textures->structSize >= sizeof(AmodTextureApi) && g_textures->ToGame != nullptr)
                ? g_textures->ToGame(g_textureSelf, texture)
                : 0;
        }

        inline bool                                                 Save(AmodTextureId texture, const char* path)
        {
            return g_textures != nullptr && g_textures->structSize >= sizeof(AmodTextureApi) &&
                g_textures->Save != nullptr && g_textures->Save(g_textureSelf, texture, path) != 0;
        }

        inline bool                                                 Override(const char* path, AmodTextureId texture)
        {
            return g_textures != nullptr && g_textures->Override != nullptr &&
                g_textures->Override(g_textureSelf, path, texture) != 0;
        }

        inline bool                                                 ClearOverride(const char* path)
        {
            return g_textures != nullptr && g_textures->ClearOverride != nullptr &&
                g_textures->ClearOverride(g_textureSelf, path) != 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_TEXTURE_API_H */
