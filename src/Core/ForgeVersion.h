#pragma once

#include <ForgeTypes.h>

// Loader version. Reported in the log banner and to mods through the API context.
#define AZFORGE_VERSION_MAJOR                                       0
#define AZFORGE_VERSION_MINOR                                       1
#define AZFORGE_VERSION_PATCH                                       0
#define AZFORGE_VERSION_STRING                                      "0.1.0"

namespace Forge
{
    // Mod API contract version. A mod declares the version it was built against in
    // mod.json; manifests outside the supported range are rejected instead of being
    // loaded with reduced functionality.
    //
    // Taken from the published header rather than restated here: two copies of this number
    // drift, and the first symptom is the packer refusing a mod the loader would accept.
    constexpr int kApiVersionCurrent = AMOD_API_VERSION;
    constexpr int kApiVersionOldestSupported = 1;

    // Name of the core module and of the archive format handled by the loader.
    constexpr wchar_t kForgeModuleName[] = L"AzForge.dll";
    constexpr wchar_t kModArchiveExtension[] = L".amod";
    constexpr char kModManifestName[] = "mod.json";

    // Archive folder whose contents replace game resources. An entry named
    // files/scripts/rs/gui.txt answers a request for scripts/rs/gui.txt.
    constexpr char kModFilesPrefix[] = "files/";
}
