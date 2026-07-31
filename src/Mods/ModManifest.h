#pragma once

#include <string>
#include <vector>

namespace Forge
{
    struct ModManifest
    {
        std::string id;                 // required, [a-z0-9._-]
        std::string name;               // required
        std::string version;            // required
        std::string author;
        std::string description;
        std::string gameVersion;
        std::string entry;              // DLL path inside the archive
        int apiVersion = 0;             // required
        std::vector<std::string> loadAfter;
        std::vector<std::string> conflicts;

        // "requires": subsystems the mod cannot work without
        std::vector<std::string> requiredSubsystems;

        // "capabilities": statement of intent for the mod list
        std::vector<std::string> capabilities;

        std::string icon;

        bool Declares(const std::string& capability) const;
    };

    bool ParseModManifest(const std::string& document, ModManifest& out, std::string& error);
}