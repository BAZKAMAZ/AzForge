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

        bool privileged = false;
    };

    class ModRegistry
    {
    public:
        bool                                                        Scan(const std::filesystem::path& modsDirectory);
        bool                                                        AddPrivileged(const std::filesystem::path& archivePath);

        void                                                        Clear();

        const std::vector<ModRecord>&                               LoadOrder() const;
        size_t                                                      RejectedCount() const;

    private:
        void                                                        LoadArchive(const std::filesystem::path& archivePath, std::vector<ModRecord>& out);
        void                                                        RemoveConflicting(std::vector<ModRecord>& records);
        void                                                        SortByDependencies(std::vector<ModRecord>& records);

        bool                                                        CollectArchives(const std::filesystem::path& modsDirectory, std::vector<std::filesystem::path>& out);

        std::vector<ModRecord> m_loadOrder;
        size_t m_rejected = 0;
    };
}
