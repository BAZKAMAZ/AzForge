#ifndef AZFORGE_CONFIG_API_H
#define AZFORGE_CONFIG_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AMOD_CONFIG_SAVED                                           0x01

/* Never written, whatever else is set - what the game marks its own scratch with. */
#define AMOD_CONFIG_NEVER_SAVED                                     0x04

typedef struct AmodConfigKey
{
    uint32_t structSize;

    const char* name;
    const char* value;   /* what it holds now */
    const char* fallback; /* what it was registered with */
    float number;        /* the same value read as a number, 0 when it is not one */
    uint32_t flags;      /* AMOD_CONFIG_* */

} AmodConfigKey;

AMOD_LAST_FIELD(AmodConfigKey, flags, 0x14);

typedef struct AmodConfigApi
{
    uint32_t structSize;

    /* Is there such a key at all. */
    int(__cdecl* Has)(AmodMod mod, const char* name);

    /* Everything about one key. */
    int(__cdecl* Get)(AmodMod mod, const char* name, AmodConfigKey* out);

    /* The value as text, or `fallback` when there is no such key. Valid until it is set again. */
    const char* (__cdecl* GetText)(AmodMod mod, const char* name, const char* fallback);

    /* The value as a number, or `fallback` when there is no such key. */
    float(__cdecl* GetNumber)(AmodMod mod, const char* name, float fallback);

    /* Change a key that already exists. 0 when there is none - Add first. */
    int(__cdecl* SetText)(AmodMod mod, const char* name, const char* value);
    int(__cdecl* SetNumber)(AmodMod mod, const char* name, float value);

    /* Make a key the game does not have*/
    int(__cdecl* Add)(AmodMod mod, const char* name, const char* value, uint32_t flags);

    /* Walking the lot, for a mod that wants to show or dump them. Order is the game's own. */
    int(__cdecl* Count)(AmodMod mod);
    const char* (__cdecl* NameAt)(AmodMod mod, int index);

    /* Write the file now rather than waiting for the game to do it on the way out. A null path means the one the game itself uses. */
    int(__cdecl* Save)(AmodMod mod, const char* path);

} AmodConfigApi;

AMOD_LAST_FIELD(AmodConfigApi, Save, 0x28);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Config
    {
        inline const AmodConfigApi* g_config = nullptr;
        inline AmodMod g_configSelf = nullptr;

        inline void                                                 Attach(const AmodConfigApi* api, AmodMod self)
        {
            g_config = api;
            g_configSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_config != nullptr;
        }

        inline bool                                                 Has(const char* name)
        {
            return g_config != nullptr && g_config->Has(g_configSelf, name) != 0;
        }

        inline bool                                                 Get(const char* name, AmodConfigKey& out)
        {
            out = AmodConfigKey{};
            out.structSize = sizeof(out);
            return g_config != nullptr && g_config->Get(g_configSelf, name, &out) != 0;
        }

        inline const char*                                          Text(const char* name, const char* fallback = "")
        {
            return g_config != nullptr ? g_config->GetText(g_configSelf, name, fallback) : fallback;
        }

        inline float                                                Number(const char* name, float fallback = 0.0f)
        {
            return g_config != nullptr ? g_config->GetNumber(g_configSelf, name, fallback) : fallback;
        }

        inline bool                                                 Set(const char* name, const char* value)
        {
            return g_config != nullptr && g_config->SetText(g_configSelf, name, value) != 0;
        }

        inline bool                                                 Set(const char* name, float value)
        {
            return g_config != nullptr && g_config->SetNumber(g_configSelf, name, value) != 0;
        }

        inline bool                                                 Set(const char* name, int value)
        {
            return Set(name, static_cast<float>(value));
        }

        inline bool                                                 Set(const char* name, bool value)
        {
            return Set(name, value ? 1.0f : 0.0f);
        }

        inline bool                                                 Add(const char* name, const char* value, uint32_t flags = AMOD_CONFIG_SAVED)
        {
            return g_config != nullptr && g_config->Add(g_configSelf, name, value, flags) != 0;
        }

        inline int                                                  Count()
        {
            return g_config != nullptr ? g_config->Count(g_configSelf) : 0;
        }

        inline const char*                                          NameAt(int index)
        {
            return g_config != nullptr ? g_config->NameAt(g_configSelf, index) : "";
        }

        inline bool                                                 Save(const char* path = nullptr)
        {
            return g_config != nullptr && g_config->Save(g_configSelf, path) != 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_CONFIG_API_H */
