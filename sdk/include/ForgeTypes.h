/*
 * ForgeTypes.h - the handful of declarations every part of the AzForge interface needs.
 *
 * Kept apart from ForgeApi.h so that a subsystem header such as LauncherApi.h can be
 * self-contained: it needs these types, but the core table needs a pointer to the
 * subsystem, and neither can include the other.
 */

#ifndef AZFORGE_FORGE_TYPES_H
#define AZFORGE_FORGE_TYPES_H

#include <stdint.h>

/* Version of the interface as a whole. A mod declares the version it was built against in
 * mod.json as api_version; the loader refuses a mod that asks for more than it implements. */
#define AMOD_API_VERSION                                            3

#define AMOD_EXPORT                                                 __declspec(dllexport)

/* Values AmodInit may return. Anything other than AMOD_OK leaves the mod unloaded, and the
 * reason is written to forge.log. */
#define AMOD_OK                                                     0
#define AMOD_ERROR                                                  (-1)
#define AMOD_ERROR_API_VERSION                                      (-2)

/* Identifies the layout of game.exe the loader knows. A mod that reaches past the typed
 * interface into raw game state has to name the layout it was written against, so that a
 * loader taught about a second build refuses the raw access instead of handing over
 * pointers into a structure that moved. */
#define AMOD_GAME_BUILD_UNKNOWN                                     0
#define AMOD_GAME_BUILD_AZANGARA_10                                 1

#ifdef __cplusplus
extern "C" {
#endif

/* Identifies the calling mod. The loader hands each mod its own tables with self already
 * filled in, so a mod never has to name itself and cannot act on behalf of another. */
typedef void* AmodMod;

/* Identifies one registration a mod made. Never zero for a successful call. Releasing a
 * token is optional: everything a mod registers is released when it is unloaded. */
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
