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

        const char* name;           /* "" when it was made from pixels rather than read */
        int width;
        int height;
        int mipmapped;
        int hasAlpha;               /* which of the two formats it was created in */
        int madeHere;               /* built from pixels, by a mod or by the game */
        int references;             /* extra owners besides the first */

        /* New fields are appended, never inserted. */
    } AmodTexture;

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

        /* Out of pixels, with no file anywhere */
        AmodTextureId(__cdecl* Create)(AmodMod mod, int width, int height, const void* pixels,
            int mipmapped, int hasAlpha);

        /* New pixels into a texture that already exists, whether it was made or read from a file */
        int(__cdecl* Replace)(AmodMod mod, AmodTextureId texture, int width, int height, const void* pixels);

        /* Every request for that path is answered with this texture instead, whoever asks */
        int(__cdecl* Override)(AmodMod mod, const char* path, AmodTextureId texture);
        int(__cdecl* ClearOverride)(AmodMod mod, const char* path);

        /* Fields added in later API versions go here, and nowhere else. */
    } AmodTextureApi;

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

        inline AmodTextureId                                        Create(int width, int height, const void* pixels,
            bool mipmapped = true,
            bool hasAlpha = false)
        {
            return (g_textures != nullptr && g_textures->Create != nullptr)
                ? g_textures->Create(g_textureSelf, width, height, pixels,
                    mipmapped ? 1 : 0, hasAlpha ? 1 : 0)
                : AMOD_INVALID_TEXTURE;
        }

        inline bool                                                 Replace(AmodTextureId texture, int width,
            int height, const void* pixels)
        {
            return g_textures != nullptr && g_textures->Replace != nullptr &&
                g_textures->Replace(g_textureSelf, texture, width, height, pixels) != 0;
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
