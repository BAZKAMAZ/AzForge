#ifndef AZFORGE_FILES_API_H
#define AZFORGE_FILES_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AmodFilesApi
{
    uint32_t structSize;

    /* Bytes the game would get for path (mod answer or data.pak). Null buffer = size only. */
    int (__cdecl* Read)(AmodMod mod, const char* path, void* buffer, int capacity);

    /* Answer path with a copy of these bytes; replace on second Serve. Null bytes = Clear. */
    int (__cdecl* Serve)(AmodMod mod, const char* path, const void* bytes, int size);

    /* Stop answering; game uses its own copy. Unload does this for the mod. */
    int (__cdecl* Clear)(AmodMod mod, const char* path);

    /* Paths this mod is currently answering for. */
    int (__cdecl* ServedCount)(AmodMod mod);
    const char* (__cdecl* ServedAt)(AmodMod mod, int index);

} AmodFilesApi;

AMOD_LAST_FIELD(AmodFilesApi, ServedAt, 0x14);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Files
    {
        inline const AmodFilesApi* g_files = nullptr;
        inline AmodMod g_filesSelf = nullptr;

        inline void                                                 Attach(const AmodFilesApi* api, AmodMod self)
        {
            g_files = api;
            g_filesSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_files != nullptr;
        }

        /* Bytes there are; pass a buffer to get them. */
        inline int                                                  Read(const char* path, void* buffer = nullptr, int capacity = 0)
        {
            return g_files != nullptr ? g_files->Read(g_filesSelf, path, buffer, capacity) : 0;
        }

        inline bool                                                 Serve(const char* path, const void* bytes, int size)
        {
            return g_files != nullptr && g_files->Serve(g_filesSelf, path, bytes, size) != 0;
        }

        inline bool                                                 Clear(const char* path)
        {
            return g_files != nullptr && g_files->Clear(g_filesSelf, path) != 0;
        }

        inline int                                                  ServedCount()
        {
            return g_files != nullptr ? g_files->ServedCount(g_filesSelf) : 0;
        }

        inline const char*                                          ServedAt(int index)
        {
            return g_files != nullptr ? g_files->ServedAt(g_filesSelf, index) : "";
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_FILES_API_H */
