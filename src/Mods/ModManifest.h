#pragma once

#include <string>
#include <vector>

namespace Forge
{
    // Contents of mod.json. Fields marked required must be present and non-empty; a
    // manifest that fails validation is rejected outright and the mod is not loaded.
    struct ModManifest
    {
        std::string id;                 // required, [a-z0-9._-]
        std::string name;               // required
        std::string version;            // required
        std::string author;
        std::string description;
        std::string gameVersion;
        std::string entry;              // path to the mod DLL inside the archive
        int apiVersion = 0;             // required
        std::vector<std::string> loadAfter;
        std::vector<std::string> conflicts;
    };

    // Parses and validates a manifest document. Returns false with a human-readable
    // reason in error when the document is malformed or a required field is missing.
    bool                                                            ParseModManifest(const std::string& document, ModManifest& out, std::string& error);
}
