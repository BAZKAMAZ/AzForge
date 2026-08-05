#ifndef AZFORGE_HUD_API_H
#define AZFORGE_HUD_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AmodHudApi
{
    uint32_t structSize;

    /* HUD key get/set. Available after the game has read game_info.txt (not in AmodInit). */
    int (__cdecl* GetProperty)(AmodMod mod, const char* key, AmodValue* value);
    int (__cdecl* SetProperty)(AmodMod mod, const char* key, const AmodValue* value);
    int (__cdecl* HasProperty)(AmodMod mod, const char* key);

    int (__cdecl* PropertyCount)(AmodMod mod);
    const char* (__cdecl* PropertyNameAt)(AmodMod mod, int index);

    /* Add $image blocks (game_info.txt syntax). Returns how many were built. */
    int (__cdecl* AddImages)(AmodMod mod, const char* fragment);
    int (__cdecl* ImageCount)(AmodMod mod);
    int (__cdecl* RemoveImage)(AmodMod mod, int index);
} AmodHudApi;

AMOD_LAST_FIELD(AmodHudApi, RemoveImage, 0x20);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

extern "C++" {

namespace Amod
{
    namespace Hud
    {
        inline const AmodHudApi* g_hud = nullptr;
        inline AmodMod g_self = nullptr;

        inline void                                                 Attach(const AmodHudApi* api, AmodMod self)
        {
            g_hud = api;
            g_self = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_hud != nullptr;
        }

        class Property
        {
        public:
            explicit Property(const char* key)
                : m_key(key)
            {
            }

            bool                                                    Exists() const
            {
                return g_hud != nullptr && g_hud->HasProperty(g_self, m_key) != 0;
            }

            bool                                                    Read(AmodValue& value) const
            {
                return g_hud != nullptr && g_hud->GetProperty(g_self, m_key, &value) != 0;
            }

            float                                                   Number(int index = 0, float fallback = 0.0f) const
            {
                AmodValue value = {};
                if (!Read(value) || value.kind != AMOD_VALUE_FLOAT ||
                    index < 0 || static_cast<uint32_t>(index) >= value.count)
                {
                    return fallback;
                }
                return value.numbers[index];
            }

            int                                                     Integer(int fallback = 0) const
            {
                AmodValue value = {};
                if (!Read(value) ||
                    (value.kind != AMOD_VALUE_INT && value.kind != AMOD_VALUE_BOOL))
                {
                    return fallback;
                }
                return value.integer;
            }

            bool                                                    Set(const AmodValue& value) const
            {
                return g_hud != nullptr && g_hud->SetProperty(g_self, m_key, &value) != 0;
            }

            bool                                                    Set(int number) const { return Set(Value(number)); }
            bool                                                    Set(const char* text) const { return Set(Value(text)); }
            bool                                                    Set(float a) const { return Set(Value(a)); }
            bool                                                    Set(float a, float b) const { return Set(Value(a, b)); }

        private:
            const char* m_key;
        };

        inline Property                                             At(const char* key)
        {
            return Property(key);
        }

        inline int                                                  AddImages(const char* fragment)
        {
            return (g_hud != nullptr && g_hud->AddImages != nullptr)
                       ? g_hud->AddImages(g_self, fragment)
                       : 0;
        }

        inline int                                                  ImageCount()
        {
            return (g_hud != nullptr && g_hud->ImageCount != nullptr) ? g_hud->ImageCount(g_self) : 0;
        }

        inline bool                                                 RemoveImage(int index)
        {
            return g_hud != nullptr && g_hud->RemoveImage != nullptr &&
                   g_hud->RemoveImage(g_self, index) != 0;
        }

        inline int                                                  PropertyCount()
        {
            return g_hud != nullptr ? g_hud->PropertyCount(g_self) : 0;
        }

        inline const char*                                          PropertyNameAt(int index)
        {
            return g_hud != nullptr ? g_hud->PropertyNameAt(g_self, index) : "";
        }
    }
}

} /* extern "C++" */

#endif /* __cplusplus */

#endif /* AZFORGE_HUD_API_H */
