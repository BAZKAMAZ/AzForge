#include "src/Mods/ModFileSystem.h"

#include "src/Mods/ModState.h"

#include "src/Core/ForgeLog.h"
#include "src/Core/ForgeText.h"
#include "src/Core/ForgeVersion.h"

#include <map>

namespace Forge
{
    namespace
    {
        struct Provider
        {
            std::string modId;
            ZipArchive* archive = nullptr;
            const ZipEntry* entry = nullptr;
        };

        std::map<std::string, Provider> g_index;

        size_t PrefixLength()
        {
            return sizeof(kModFilesPrefix) - 1;
        }
    }

    void ModFileSystem::Build(const std::vector<ModRecord>& loadOrder)
    {
        Clear();

        const size_t prefixLength = PrefixLength();

        for (const ModRecord& record : loadOrder)
        {
            if (record.archive == nullptr || !ModState::IsEnabled(record.manifest.id))
            {
                continue;
            }

            for (const ZipEntry& entry : record.archive->Entries())
            {
                if (entry.lookupName.compare(0, prefixLength, kModFilesPrefix) != 0)
                {
                    continue;
                }

                const std::string resource = entry.lookupName.substr(prefixLength);
                if (resource.empty())
                {
                    continue;
                }

                const auto existing = g_index.find(resource);
                if (existing != g_index.end())
                {
                    FORGE_LOG_INFO("'%s' from '%s' overrides the copy from '%s'",
                                   resource.c_str(), record.manifest.id.c_str(),
                                   existing->second.modId.c_str());
                }

                Provider provider;
                provider.modId = record.manifest.id;
                provider.archive = record.archive.get();
                provider.entry = &entry;

                g_index[resource] = provider;
            }
        }

        FORGE_LOG_INFO("Resource overrides: %zu file(s)", g_index.size());
    }

    void ModFileSystem::Clear()
    {
        g_index.clear();
    }

    ModFileResult ModFileSystem::Read(const char* resourcePath, std::vector<uint8_t>& out, std::string& providerId, std::string& error)
    {
        out.clear();
        providerId.clear();
        error.clear();

        if (resourcePath == nullptr || resourcePath[0] == '\0' || g_index.empty())
        {
            return ModFileResult::NotFound;
        }

        const std::string key = ToLowerAscii(NormalizeSlashes(resourcePath));

        const auto found = g_index.find(key);
        if (found == g_index.end())
        {
            return ModFileResult::NotFound;
        }

        providerId = found->second.modId;

        if (!found->second.archive->Read(*found->second.entry, out, error))
        {
            return ModFileResult::Failed;
        }

        return ModFileResult::Loaded;
    }

    size_t ModFileSystem::FileCount()
    {
        return g_index.size();
    }
}
