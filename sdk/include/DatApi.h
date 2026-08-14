#ifndef AZFORGE_DAT_API_H
#define AZFORGE_DAT_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* scores.dat. Entries are grouped by castle, so an index here is a walk. */
typedef struct AmodScoreEntry
{
    uint32_t structSize;

    const char* name;  /* who set it, up to 0x3F characters */
    const char* level; /* the level path, which is what ties it to a castle */
    float time;
    int score;

    int castle;
    int inCastle;      /* its place within that castle */

    int filled;        /* 0 is thrown away by the next save - what the score script builds */

} AmodScoreEntry;

AMOD_LAST_FIELD(AmodScoreEntry, filled, 0x1C);

typedef struct AmodDatApi
{
    uint32_t structSize;

    /* Every entry the game is holding, across all castles. */
    int(__cdecl* Count)(AmodMod mod);
    int(__cdecl* Get)(AmodMod mod, int index, AmodScoreEntry* out);

    /* Change one that is already there. A null name leaves the name alone. */
    int(__cdecl* Set)(AmodMod mod, int index, const char* name, float time, int score);

    /* The level picks the castle, the score picks the place. Index, or -1. */
    int(__cdecl* Add)(AmodMod mod, const char* level, const char* name, float time, int score);

    /* How the game groups them. */
    int(__cdecl* CastleCount)(AmodMod mod);
    int(__cdecl* CastleEntryCount)(AmodMod mod, int castle);

    /* Which castle a level belongs to, or -1. */
    int(__cdecl* CastleOfLevel)(AmodMod mod, const char* level);

    /* Write scores.dat now. */
    int(__cdecl* Save)(AmodMod mod);

    /* One field at a time. */
    const char* (__cdecl* GetName)(AmodMod mod, int index);
    int(__cdecl* SetName)(AmodMod mod, int index, const char* name);
    int(__cdecl* GetPoint)(AmodMod mod, int index);
    int(__cdecl* SetPoint)(AmodMod mod, int index, int score);

    /* Drop one; the rest of its castle closes up. */
    int(__cdecl* Remove)(AmodMod mod, int index);

} AmodDatApi;

AMOD_LAST_FIELD(AmodDatApi, Remove, 0x34);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Scores
    {
        inline const AmodDatApi* g_dat = nullptr;
        inline AmodMod g_datSelf = nullptr;

        inline void                                                 Attach(const AmodDatApi* api, AmodMod self)
        {
            g_dat = api;
            g_datSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_dat != nullptr;
        }

        inline int                                                  Count()
        {
            return g_dat != nullptr ? g_dat->Count(g_datSelf) : 0;
        }

        inline bool                                                 Get(int index, AmodScoreEntry& out)
        {
            out = AmodScoreEntry{};
            out.structSize = sizeof(out);
            return g_dat != nullptr && g_dat->Get(g_datSelf, index, &out) != 0;
        }

        inline bool                                                 Set(int index, const char* name, float time, int score)
        {
            return g_dat != nullptr && g_dat->Set(g_datSelf, index, name, time, score) != 0;
        }

        inline int                                                  Add(const char* level, const char* name,
            float time, int score)
        {
            return g_dat != nullptr ? g_dat->Add(g_datSelf, level, name, time, score) : -1;
        }

        inline int                                                  CastleCount()
        {
            return g_dat != nullptr ? g_dat->CastleCount(g_datSelf) : 0;
        }

        inline int                                                  CastleEntryCount(int castle)
        {
            return g_dat != nullptr ? g_dat->CastleEntryCount(g_datSelf, castle) : 0;
        }

        inline int                                                  CastleOfLevel(const char* level)
        {
            return g_dat != nullptr ? g_dat->CastleOfLevel(g_datSelf, level) : -1;
        }

        inline bool                                                 Save()
        {
            return g_dat != nullptr && g_dat->Save(g_datSelf) != 0;
        }

        inline const char*                                          GetName(int index)
        {
            return (g_dat != nullptr && g_dat->structSize >= sizeof(AmodDatApi) && g_dat->GetName != nullptr)
                ? g_dat->GetName(g_datSelf, index)
                : "";
        }

        inline bool                                                 SetName(int index, const char* name)
        {
            return g_dat != nullptr && g_dat->structSize >= sizeof(AmodDatApi) && g_dat->SetName != nullptr &&
                g_dat->SetName(g_datSelf, index, name) != 0;
        }

        inline int                                                  GetPoint(int index)
        {
            return (g_dat != nullptr && g_dat->structSize >= sizeof(AmodDatApi) && g_dat->GetPoint != nullptr)
                ? g_dat->GetPoint(g_datSelf, index)
                : 0;
        }

        inline bool                                                 SetPoint(int index, int score)
        {
            return g_dat != nullptr && g_dat->structSize >= sizeof(AmodDatApi) && g_dat->SetPoint != nullptr &&
                g_dat->SetPoint(g_datSelf, index, score) != 0;
        }

        inline bool                                                 Remove(int index)
        {
            return g_dat != nullptr && g_dat->structSize >= sizeof(AmodDatApi) && g_dat->Remove != nullptr &&
                g_dat->Remove(g_datSelf, index) != 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_DAT_API_H */
