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

        // Loaded from beside AzForge.dll rather than out of the mods folder, which is what
        // earns it the mod-manager interface. Trusting the location costs nothing: anyone who
        // can write there can replace the loader itself.
        bool privileged = false;
    };

    class ModRegistry
    {
    public:
        bool                                                        Scan(const std::filesystem::path& modsDirectory);

        // Adds one archive from outside the mods folder and marks it the manager. Called with
        // core.amod from the loader's own directory, after Scan.
        bool                                                        AddPrivileged(const std::filesystem::path& archivePath);

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
