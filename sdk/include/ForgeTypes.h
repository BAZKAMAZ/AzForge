/*
 * ForgeTypes.h - declarations every part of the AzForge interface needs.
 *
 * Kept apart from ForgeApi.h so a subsystem header can be self-contained: it needs these
 * types, the core table needs a pointer to the subsystem, and neither can include the other.
 */

#ifndef AZFORGE_FORGE_TYPES_H
#define AZFORGE_FORGE_TYPES_H

#include <stdint.h>

/* Version of the interface as a whole. A mod declares what it was built against in mod.json
 * as api_version; a mod asking for more than the loader implements is refused. */
#define AMOD_API_VERSION                                            7

#define AMOD_EXPORT                                                 __declspec(dllexport)

/* AmodInit results. Anything other than AMOD_OK leaves the mod unloaded and the reason in
 * forge.log. */
#define AMOD_OK                                                     0
#define AMOD_ERROR                                                  (-1)
#define AMOD_ERROR_API_VERSION                                      (-2)

/* Which layout of game.exe the loader knows. A mod reaching past the typed interface into
 * raw game state names the layout it was written against, so a loader taught about a second
 * build refuses rather than handing over pointers into a structure that moved. */
#define AMOD_GAME_BUILD_UNKNOWN                                     0
#define AMOD_GAME_BUILD_AZANGARA_10                                 1

#ifdef __cplusplus
extern "C" {
#endif

/* The calling mod. Each mod gets its own tables with self already filled in, so it never
 * names itself and cannot act for another. */
typedef void* AmodMod;

/* One registration a mod made. Never zero on success. Releasing it is optional: everything
 * a mod registers goes away with it. */
typedef uint32_t AmodToken;
#define AMOD_INVALID_TOKEN                                          0

typedef enum AmodLogLevel
{
    AMOD_LOG_INFO = 0,
    AMOD_LOG_WARNING = 1,
    AMOD_LOG_ERROR = 2
} AmodLogLevel;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AZFORGE_FORGE_TYPES_H */
