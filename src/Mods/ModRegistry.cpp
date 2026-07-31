#include "src/Mods/ModRegistry.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgeText.h"
#include "src/Core/ForgeVersion.h"

#include <algorithm>
#include <map>
#include <set>

namespace Forge
{
    namespace
    {
        bool HasModExtension(const std::filesystem::path& path)
        {
            std::wstring extension = path.extension().wstring();
            for (wchar_t& c : extension)
            {
                if (c >= L'A' && c <= L'Z')
                {
                    c = static_cast<wchar_t>(c - L'A' + L'a');
                }
            }
            return extension == kModArchiveExtension;
        }
    }

    bool ModRegistry::Scan(const std::filesystem::path& modsDirectory)
    {
        Clear();

        std::vector<std::filesystem::path> archives;
        if (!CollectArchives(modsDirectory, archives))
        {
            return false;
        }

        if (archives.empty())
        {
            FORGE_LOG_INFO("No .amod archives found in %s", PathToUtf8(modsDirectory).c_str());
            return true;
        }

        FORGE_LOG_INFO("Found %zu archive(s) in %s", archives.size(), PathToUtf8(modsDirectory).c_str());

        std::vector<ModRecord> records;
        records.reserve(archives.size());

        for (const std::filesystem::path& archivePath : archives)
        {
            LoadArchive(archivePath, records);
        }

        RemoveConflicting(records);
        SortByDependencies(records);

        m_loadOrder = std::move(records);
        return true;
    }

    bool ModRegistry::AddPrivileged(const std::filesystem::path& archivePath)
    {
        std::error_code status;
        if (!std::filesystem::is_regular_file(archivePath, status))
        {
            return false;
        }

        // Read the same way as any other mod, then marked
        std::vector<ModRecord> records;
        LoadArchive(archivePath, records);
        if (records.empty())
        {
            return false;
        }

        records.front().privileged = true;

        // First in the order, because whatever manages the others should be there before
        m_loadOrder.insert(m_loadOrder.begin(), std::move(records.front()));

        FORGE_LOG_INFO("'%s' loaded from beside the loader and is the mod manager", m_loadOrder.front().manifest.id.c_str());
        return true;
    }

    void ModRegistry::Clear()
    {
        m_loadOrder.clear();
        m_rejected = 0;
    }

    const std::vector<ModRecord>& ModRegistry::LoadOrder() const
    {
        return m_loadOrder;
    }

    size_t ModRegistry::RejectedCount() const
    {
        return m_rejected;
    }

    bool ModRegistry::CollectArchives(const std::filesystem::path& modsDirectory, std::vector<std::filesystem::path>& out)
    {
        std::error_code status;
        if (!std::filesystem::exists(modsDirectory, status))
        {
            std::error_code creation;
            std::filesystem::create_directories(modsDirectory, creation);
            if (creation)
            {
                FORGE_LOG_ERROR("Cannot create the mods directory %s: %s",
                                PathToUtf8(modsDirectory).c_str(), creation.message().c_str());
                return false;
            }

            FORGE_LOG_INFO("Created the mods directory %s", PathToUtf8(modsDirectory).c_str());
            return true;
        }

        if (!std::filesystem::is_directory(modsDirectory, status))
        {
            FORGE_LOG_ERROR("%s exists but is not a directory", PathToUtf8(modsDirectory).c_str());
            return false;
        }

        std::error_code iteration;
        std::filesystem::directory_iterator cursor(modsDirectory, iteration);
        if (iteration)
        {
            FORGE_LOG_ERROR("Cannot enumerate %s: %s",
                            PathToUtf8(modsDirectory).c_str(), iteration.message().c_str());
            return false;
        }

        for (const std::filesystem::directory_entry& item : cursor)
        {
            std::error_code kind;
            if (!item.is_regular_file(kind) || kind)
            {
                continue;
            }

            if (HasModExtension(item.path()))
            {
                out.push_back(item.path());
            }
        }

        std::sort(out.begin(), out.end(), [](const std::filesystem::path& a, const std::filesystem::path& b)
        {
            return a.filename().wstring() < b.filename().wstring();
        });

        return true;
    }

    void ModRegistry::LoadArchive(const std::filesystem::path& archivePath, std::vector<ModRecord>& out)
    {
        const std::string archiveText = PathToUtf8(archivePath.filename());

        auto archive = std::make_unique<ZipArchive>();
        std::string error;
        if (!archive->Open(archivePath, error))
        {
            FORGE_LOG_ERROR("%s: %s", archiveText.c_str(), error.c_str());
            ++m_rejected;
            return;
        }

        std::vector<uint8_t> manifestBytes;
        if (!archive->ReadByName(kModManifestName, manifestBytes, error))
        {
            FORGE_LOG_ERROR("%s: %s", archiveText.c_str(), error.c_str());
            ++m_rejected;
            return;
        }

        std::string document(reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size());
        if (document.size() >= 3 &&
            static_cast<unsigned char>(document[0]) == 0xEF &&
            static_cast<unsigned char>(document[1]) == 0xBB &&
            static_cast<unsigned char>(document[2]) == 0xBF)
        {
            document.erase(0, 3);
        }

        ModRecord record;
        if (!ParseModManifest(document, record.manifest, error))
        {
            FORGE_LOG_ERROR("%s: %s is invalid: %s", archiveText.c_str(), kModManifestName, error.c_str());
            ++m_rejected;
            return;
        }

        for (const ModRecord& existing : out)
        {
            if (existing.manifest.id == record.manifest.id)
            {
                FORGE_LOG_ERROR("%s declares id '%s', which %s already uses; the archive is skipped",
                                archiveText.c_str(), record.manifest.id.c_str(),
                                PathToUtf8(existing.archivePath.filename()).c_str());
                ++m_rejected;
                return;
            }
        }

        if (!record.manifest.entry.empty() && archive->Find(record.manifest.entry) == nullptr)
        {
            FORGE_LOG_ERROR("%s declares entry '%s', which the archive does not contain",
                            archiveText.c_str(), record.manifest.entry.c_str());
            ++m_rejected;
            return;
        }

        record.archivePath = archivePath;
        record.archive = std::move(archive);

        FORGE_LOG_INFO("  %s -> %s %s (api %d, %zu file(s))",
                       archiveText.c_str(),
                       record.manifest.id.c_str(),
                       record.manifest.version.c_str(),
                       record.manifest.apiVersion,
                       record.archive->Entries().size());

        out.push_back(std::move(record));
    }

    void ModRegistry::RemoveConflicting(std::vector<ModRecord>& records)
    {
        std::set<std::string> present;
        for (const ModRecord& record : records)
        {
            present.insert(record.manifest.id);
        }

        std::set<std::string> excluded;
        for (const ModRecord& record : records)
        {
            for (const std::string& other : record.manifest.conflicts)
            {
                if (present.find(other) == present.end())
                {
                    continue;
                }

                FORGE_LOG_ERROR("'%s' and '%s' declare a conflict; neither is loaded",
                                record.manifest.id.c_str(), other.c_str());
                excluded.insert(record.manifest.id);
                excluded.insert(other);
            }
        }

        if (excluded.empty())
        {
            return;
        }

        const size_t before = records.size();
        records.erase(std::remove_if(records.begin(), records.end(), [&excluded](const ModRecord& record)
        {
            return excluded.find(record.manifest.id) != excluded.end();
        }), records.end());

        m_rejected += before - records.size();
    }

    void ModRegistry::SortByDependencies(std::vector<ModRecord>& records)
    {
        if (records.empty())
        {
            return;
        }

        std::map<std::string, size_t> indexById;
        for (size_t i = 0; i < records.size(); ++i)
        {
            indexById.emplace(records[i].manifest.id, i);
        }

        // successors[dep] lists the mods that must be loaded after dep.
        std::map<std::string, std::vector<std::string>> successors;
        std::map<std::string, size_t> pending;

        for (const ModRecord& record : records)
        {
            pending.emplace(record.manifest.id, 0);
        }

        for (const ModRecord& record : records)
        {
            for (const std::string& dependency : record.manifest.loadAfter)
            {
                if (indexById.find(dependency) == indexById.end())
                {
                    FORGE_LOG_INFO("'%s' asks to load after '%s', which is not installed",
                                   record.manifest.id.c_str(), dependency.c_str());
                    continue;
                }

                successors[dependency].push_back(record.manifest.id);
                pending[record.manifest.id] += 1;
            }
        }

        std::set<std::string> ready;
        for (const auto& item : pending)
        {
            if (item.second == 0)
            {
                ready.insert(item.first);
            }
        }

        std::vector<std::string> sequence;
        sequence.reserve(records.size());

        while (!ready.empty())
        {
            const std::string current = *ready.begin();
            ready.erase(ready.begin());
            sequence.push_back(current);

            const auto edges = successors.find(current);
            if (edges == successors.end())
            {
                continue;
            }

            for (const std::string& successor : edges->second)
            {
                size_t& remaining = pending[successor];
                remaining -= 1;
                if (remaining == 0)
                {
                    ready.insert(successor);
                }
            }
        }

        if (sequence.size() != records.size())
        {
            std::string cycle;
            for (const auto& item : pending)
            {
                if (item.second != 0)
                {
                    if (!cycle.empty())
                    {
                        cycle += ", ";
                    }
                    cycle += item.first;
                }
            }

            FORGE_LOG_ERROR("load_after forms a cycle between %s; none of them is loaded", cycle.c_str());
            m_rejected += records.size() - sequence.size();
        }

        std::vector<ModRecord> ordered;
        ordered.reserve(sequence.size());
        for (const std::string& id : sequence)
        {
            ordered.push_back(std::move(records[indexById[id]]));
        }

        records = std::move(ordered);
    }
}
