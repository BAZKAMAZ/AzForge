#pragma once

#include <ForgeTypes.h>

#define AZFORGE_VERSION_MAJOR                                       0
#define AZFORGE_VERSION_MINOR                                       1
#define AZFORGE_VERSION_PATCH                                       0
#define AZFORGE_VERSION_STRING                                      "0.1.0"

namespace Forge
{
    // From published header — do not restate, the two copies would drift.
    constexpr int kApiVersionCurrent = AMOD_API_VERSION;
    constexpr int kApiVersionOldestSupported = 1;

    constexpr wchar_t kForgeModuleName[] = L"AzForge.dll";
    constexpr wchar_t kModArchiveExtension[] = L".amod";
    constexpr char kModManifestName[] = "mod.json";

    // Archive prefix: files/scripts/rs/gui.txt → scripts/rs/gui.txt
    constexpr char kModFilesPrefix[] = "files/";
}