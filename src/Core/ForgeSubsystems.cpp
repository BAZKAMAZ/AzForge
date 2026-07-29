#include "pch.h"

#include "src/Core/ForgeSubsystems.h"

#include "src/Core/ForgeLog.h"

#include <vector>

namespace Forge
{
    namespace
    {
        struct Entry
        {
            std::string name;
            bool available;
        };

        // A vector rather than a map: there are eight of these and the order they were
        // installed in is the order worth reading them in.
        std::vector<Entry> g_entries;
    }

    void Subsystems::Record(const char* name, bool available)
    {
        if (name == nullptr)
        {
            return;
        }

        for (Entry& entry : g_entries)
        {
            if (entry.name == name)
            {
                entry.available = available;
                return;
            }
        }

        g_entries.push_back(Entry{ name, available });
    }

    bool Subsystems::IsAvailable(const std::string& name)
    {
        for (const Entry& entry : g_entries)
        {
            if (entry.name == name)
            {
                return entry.available;
            }
        }
        return false;
    }

    void Subsystems::WriteSummary()
    {
        std::string missing;
        for (const Entry& entry : g_entries)
        {
            if (!entry.available)
            {
                missing += missing.empty() ? "" : ", ";
                missing += entry.name;
            }
        }

        if (missing.empty())
        {
            FORGE_LOG_INFO("All %zu subsystems are available", g_entries.size());
            return;
        }

        // A warning rather than an error: the loader is still doing its job, but a mod that
        // needed one of these is about to be refused and this is the line that explains why.
        FORGE_LOG_WARN("These subsystems are not available and mods needing them will be "
                       "refused: %s", missing.c_str());
    }

    void Subsystems::Clear()
    {
        g_entries.clear();
    }
}
