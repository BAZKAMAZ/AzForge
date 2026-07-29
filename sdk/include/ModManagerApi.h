/*
 * ModManagerApi.h - the installed mods, and which of them are switched on.
 *
 * This is the one interface AzForge does not hand to every mod. It goes to core.amod, and
 * core.amod is recognised by where it is installed: beside AzForge.dll rather than in the mods
 * folder. Anyone able to write there could replace the loader itself, so trusting that
 * location costs nothing that was not already given away - and it needs no signature, no
 * pinned hash and nobody vetting anything.
 *
 * SetEnabled records the wish and writes it to disk; Reload is what makes it true in this run,
 * for one mod. What the loader can take back is everything that mod registered through it - its
 * menus, its hooks, its callbacks - and not what the game has since done with a resource the mod
 * provided, so a mod whose files are already in a loaded level is honestly a restart.
 *
 * Reached through AmodApi::modManager, null for every mod that is not core.
 */

#ifndef AZFORGE_MOD_MANAGER_API_H
#define AZFORGE_MOD_MANAGER_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One installed mod. Every string points into the loader and stays valid for the life of the
 * process; a field the manifest left out is an empty string, never null. */
typedef struct AmodModEntry
{
    uint32_t structSize;

    const char* id;
    const char* name;
    const char* version;
    const char* author;
    const char* description;
    const char* archive;        /* the .amod file name, for a player to find on disk */

    int apiVersion;             /* what the mod was built against */

    /* A file on disk the mod's icon was unpacked to, or "" when it carries none. It is left
     * where it is for the life of the process, so it can be loaded as a texture. */
    const char* icon;

    int enabled;                /* as the switch stands */
    int loaded;                 /* as it actually is right now. A mod that carries no code is
                                 * loaded whenever it is enabled: its files are all it has. */

    /* Set only when this mod cannot be brought into step without starting the process again,
     * which means one that has to be up before the launcher. Everything else Reload settles. */
    int needsRestart;

    int capabilityCount;

    /* New fields are appended, never inserted. */
} AmodModEntry;

typedef struct AmodModManagerApi
{
    uint32_t structSize;

    /* Every installed mod, switched on or off, loaded or not. */
    int (__cdecl* Count)(AmodMod mod);

    /* Fills in one by index. The caller sets out->structSize first. Non-zero on success. */
    int (__cdecl* Get)(AmodMod mod, int index, AmodModEntry* out);

    /* One of that mod's declared capabilities, or "" past the end. What a mod says it will
     * do, which is not the same as what it can do - a native DLL is not held to it. */
    const char* (__cdecl* Capability)(AmodMod mod, int index, int capability);

    /* Why a mod was refused this run, or "" when it was not. */
    const char* (__cdecl* FailureReason)(AmodMod mod, int index);

    /* Records the wish and writes it to disk at once, so it survives even a crash before the
     * player leaves the screen. Non-zero when something changed. */
    int (__cdecl* SetEnabled)(AmodMod mod, int index, int enabled);

    /* Whether any switch is still out of step with what is actually loaded. */
    int (__cdecl* IsDirty)(AmodMod mod);

    /* Brings one mod into step with its switch: loads it if it was turned on, takes it down if
     * it was turned off. Answers whether the request was accepted, not whether it is done - it
     * happens at the start of the next frame, which is the one moment no mod is on the stack
     * and a DLL can be let go of safely. What happened is in the log, and Get() tells the
     * screen a frame later.
     *
     * Refused for a mod that has to be up before the launcher: that one is a restart, and the
     * entry says so. Reloading a mod others were built on is allowed and is the caller's
     * business - the loader has no way to know what one mod took from another. */
    int (__cdecl* Reload)(AmodMod mod, int index);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodModManagerApi;

#ifdef __cplusplus
} /* extern "C" */
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

        /* False in every mod but core, which is the point. */
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
