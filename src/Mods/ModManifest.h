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

        // The "requires" key: subsystems the mod cannot work without, by the names AmodApi
        // uses - launcher, objects_menu, box_menu, block_expression, console, frame, render,
        // world. A mod that names one this loader could not install is refused with that
        // reason instead of loading and meeting a null table it may never have checked.
        // Naming nothing keeps the old behaviour: the mod loads and decides for itself.
        // Spelled out in full here because "requires" alone is a keyword in C++20.
        std::vector<std::string> requiredSubsystems;

        // The "capabilities" key: what the mod says it intends to do, for the mod list to
        // show a player before they switch it on.
        //
        // Be careful about what this is. A mod is a native DLL and its DllMain runs before
        // the loader has any say, so a mod that means harm is not stopped by anything it
        // failed to declare - it can open files and sockets through Win32 without the loader
        // ever seeing it. This is a statement of intent, and the only honest way to show it
        // is as one.
        //
        // What the loader does enforce is the part it stands in the middle of. "interop" is
        // the one that matters: a mod that has not declared it is not given the interop
        // table, so finding routines, detouring them and writing to the game's memory are
        // out of reach of a mod that never said it would. The rest are recorded, not gated.
        std::vector<std::string> capabilities;

        // The "icon" key: a file inside the archive the mod screen shows for it, defaulting
        // to icon.bmp. A mod without one is drawn as a plain square.
        std::string icon;

        bool                                                        Declares(const std::string& capability) const;
    };

    // Parses and validates a manifest document. Returns false with a human-readable
    // reason in error when the document is malformed or a required field is missing.
    bool                                                            ParseModManifest(const std::string& document, ModManifest& out, std::string& error);
}
