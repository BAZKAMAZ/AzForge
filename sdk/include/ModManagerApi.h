#ifndef AZFORGE_MOD_MANAGER_API_H
#define AZFORGE_MOD_MANAGER_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One installed mod. Strings live for the process; missing fields are "". */
typedef struct AmodModEntry
{
    uint32_t structSize;

    const char* id;
    const char* name;
    const char* version;
    const char* author;
    const char* description;
    const char* archive;        /* .amod file name */
    int apiVersion;
    const char* icon;           /* unpacked icon path, or "" */
    int enabled;
    int loaded;                 /* file-only mods are "loaded" whenever enabled */
    int needsRestart;           /* true for launcher-phase mods */
    int capabilityCount;
} AmodModEntry;

typedef struct AmodModManagerApi
{
    uint32_t structSize;

    int (__cdecl* Count)(AmodMod mod);
    int (__cdecl* Get)(AmodMod mod, int index, AmodModEntry* out);

    const char* (__cdecl* Capability)(AmodMod mod, int index, int capability);
    const char* (__cdecl* FailureReason)(AmodMod mod, int index);

    /* Writes the switch to disk immediately. */
    int (__cdecl* SetEnabled)(AmodMod mod, int index, int enabled);
    int (__cdecl* IsDirty)(AmodMod mod);

    /* Load/unload at next frame start. Refused for launcher-phase mods. */
    int (__cdecl* Reload)(AmodMod mod, int index);
} AmodModManagerApi;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Mods
    {
        inline const AmodModManagerApi* g_mods = nullptr;
        inline AmodMod g_self = nullptr;

        inline void                                                 Attach(const AmodModManagerApi* api, AmodMod self)
        {
            g_mods = api;
            g_self = self;
        }

        /* False in every mod but core. */
        inline bool                                                 IsAvailable()
        {
            return g_mods != nullptr;
        }

        inline int                                                  Count()
        {
            return g_mods != nullptr ? g_mods->Count(g_self) : 0;
        }

        inline bool                                                 Get(int index, AmodModEntry& out)
        {
            out = AmodModEntry{};
            out.structSize = sizeof(AmodModEntry);
            return g_mods != nullptr && g_mods->Get(g_self, index, &out) != 0;
        }

        inline const char*                                          Capability(int index, int capability)
        {
            return g_mods != nullptr ? g_mods->Capability(g_self, index, capability) : "";
        }

        inline const char*                                          FailureReason(int index)
        {
            return g_mods != nullptr ? g_mods->FailureReason(g_self, index) : "";
        }

        inline bool                                                 SetEnabled(int index, bool enabled)
        {
            return g_mods != nullptr && g_mods->SetEnabled(g_self, index, enabled ? 1 : 0) != 0;
        }

        inline bool                                                 IsDirty()
        {
            return g_mods != nullptr && g_mods->IsDirty(g_self) != 0;
        }

        inline bool                                                 Reload(int index)
        {
            return g_mods != nullptr && g_mods->structSize >= sizeof(AmodModManagerApi) &&
                   g_mods->Reload(g_self, index) != 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_MOD_MANAGER_API_H */
