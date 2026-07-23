#pragma once

#include "src/Mods/ModManifest.h"
#include "src/Zip/ZipArchive.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Forge
{
    struct ModRecord
    {
        ModManifest manifest;
        std::filesystem::path archivePath;
        std::unique_ptr<ZipArchive> archive;
    };

    // Discovers every .amod in the mods directory, validates its manifest and produces the
    // order in which the mods are to be loaded.
    //
    // Ordering rules:
    //   - load_after names mods that must come first. A name that is not installed is an
    //     ordering hint with nothing to order against and is reported, not treated as a
    //     missing dependency.
    //   - conflicts names mods that cannot be active at the same time. When both sides are
    //     present neither is loaded, so the outcome does not depend on scan order.
    //   - A cycle in load_after cannot be satisfied, so every mod inside the cycle is
    //     dropped. Mods outside it are unaffected.
    //   - Mods that are otherwise unconstrained are ordered by id, so the same set of
    //     archives always produces the same load order.
    class ModRegistry
    {
    public:
        bool                                                        Scan(const std::filesystem::path& modsDirectory);
        void                                                        Clear();

        const std::vector<ModRecord>&                               LoadOrder() const;
        size_t                                                      RejectedCount() const;

    private:
        bool                                                        CollectArchives(const std::filesystem::path& modsDirectory, std::vector<std::filesystem::path>& out);
        void                                                        LoadArchive(const std::filesystem::path& archivePath, std::vector<ModRecord>& out);
        void                                                        RemoveConflicting(std::vector<ModRecord>& records);
        void                                                        SortByDependencies(std::vector<ModRecord>& records);

        std::vector<ModRecord> m_loadOrder;
        size_t m_rejected = 0;
    };
}
