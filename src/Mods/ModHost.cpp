#include "src/Mods/ModHost.h"

#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Mods/ModLibrary.h"
#include "src/Mods/ModState.h"

#include <map>
#include <memory>
#include <set>

namespace Forge
{
    namespace
    {
        // unique_ptr so ModLibrary (and its AmodApi table) does not move.
        std::vector<std::unique_ptr<ModLibrary>> g_loaded;

        size_t g_failed = 0;
        std::set<std::string> g_loadedIds;
        std::map<std::string, std::string> g_failures;
    }

    void ModHost::SweepCache()
    {
        const std::filesystem::path cache = Paths::CacheDirectory();

        std::error_code status;
        if (!std::filesystem::is_directory(cache, status))
        {
            return;
        }

        size_t removed = 0;
        size_t retained = 0;

        std::error_code iteration;
        for (const std::filesystem::directory_entry& item :
            std::filesystem::directory_iterator(cache, iteration))
        {
            std::error_code removal;
            if (std::filesystem::remove(item.path(), removal))
            {
                ++removed;
            }
            else
            {
                ++retained;
            }
        }

        if (removed != 0)
        {
            FORGE_LOG_INFO("Swept %zu file(s) left in the cache by an earlier run", removed);
        }

        if (retained != 0)
        {
            FORGE_LOG_INFO("%zu cached file(s) are in use by another instance and were left alone",
                retained);
        }

        std::error_code directory;
        std::filesystem::remove(cache, directory);
    }

    size_t ModHost::CodeModCount(const std::vector<ModRecord>& loadOrder)
    {
        size_t withCode = 0;
        for (const ModRecord& record : loadOrder)
        {
            if (!record.manifest.entry.empty() && ModState::IsEnabled(record.manifest.id))
            {
                ++withCode;
            }
        }
        return withCode;
    }

    void ModHost::Load(const std::vector<ModRecord>& loadOrder, ModPhase phase, void (*afterEach)())
    {
        const bool early = phase == ModPhase::BeforeLauncher;

        size_t inPhase = 0;
        for (const ModRecord& record : loadOrder)
        {
            if (!record.manifest.entry.empty() &&
                record.manifest.Declares("launcher") == early &&
                ModState::IsEnabled(record.manifest.id))
            {
                ++inPhase;
            }
        }

        if (inPhase == 0)
        {
            return;
        }

        Log::WriteSeparator();
        FORGE_LOG_INFO("Loading %zu mod librar%s %s", inPhase, inPhase == 1 ? "y" : "ies",
            early ? "before the launcher" : "on the loading screen");

        for (const ModRecord& record : loadOrder)
        {
            if (record.manifest.entry.empty() ||
                record.manifest.Declares("launcher") != early ||
                !ModState::IsEnabled(record.manifest.id))
            {
                continue;
            }

            auto library = std::make_unique<ModLibrary>();

            std::string error;
            if (!library->Load(record, error))
            {
                FORGE_LOG_ERROR("'%s' was not loaded: %s", record.manifest.id.c_str(), error.c_str());
                g_failures[record.manifest.id] = error;
                ++g_failed;
            }
            else
            {
                g_loadedIds.insert(record.manifest.id);
                g_loaded.push_back(std::move(library));
            }

            if (afterEach != nullptr)
            {
                afterEach();
            }
        }

        FORGE_LOG_INFO("%zu of %zu mod librar%s loaded", g_loaded.size(),
            g_loaded.size() + g_failed, (g_loaded.size() + g_failed) == 1 ? "y" : "ies");
        Log::WriteSeparator();
    }

    size_t ModHost::FailedCount()
    {
        return g_failed;
    }

    void ModHost::UnloadAll()
    {
        if (g_loaded.empty())
        {
            return;
        }

        FORGE_LOG_INFO("Unloading %zu mod librar%s", g_loaded.size(), g_loaded.size() == 1 ? "y" : "ies");

        while (!g_loaded.empty())
        {
            g_loaded.back()->Unload();
            g_loaded.pop_back();
        }

        std::error_code removal;
        std::filesystem::remove(Paths::CacheDirectory(), removal);

        g_failed = 0;
        g_loadedIds.clear();
        g_failures.clear();
    }

    bool ModHost::Reload(const ModRecord& record, std::string& error)
    {
        const std::string& id = record.manifest.id;
        const bool wanted = ModState::IsEnabled(id);
        const bool loaded = IsLoaded(id);

        // File-only mod — nothing to load/unload.
        if (record.manifest.entry.empty())
        {
            return true;
        }

        if (record.manifest.Declares("launcher"))
        {
            error = "it changes the launcher, which ran before the game window existed";
            return false;
        }

        if (wanted == loaded)
        {
            return true;
        }

        if (!wanted)
        {
            for (auto it = g_loaded.begin(); it != g_loaded.end(); ++it)
            {
                if ((*it)->Id() == id)
                {
                    (*it)->Unload();
                    g_loaded.erase(it);
                    break;
                }
            }

            g_loadedIds.erase(id);
            FORGE_LOG_INFO("'%s' was taken down while the game was running", id.c_str());
            return true;
        }

        auto library = std::make_unique<ModLibrary>();
        if (!library->Load(record, error))
        {
            g_failures[id] = error;
            ++g_failed;
            return false;
        }

        g_failures.erase(id);
        g_loadedIds.insert(id);
        g_loaded.push_back(std::move(library));

        FORGE_LOG_INFO("'%s' was brought up while the game was running", id.c_str());
        return true;
    }

    bool ModHost::IsLoaded(const std::string& id)
    {
        return g_loadedIds.count(id) != 0;
    }

    const char* ModHost::FailureFor(const std::string& id)
    {
        const auto found = g_failures.find(id);
        return found != g_failures.end() ? found->second.c_str() : "";
    }

    size_t ModHost::LoadedCount()
    {
        return g_loaded.size();
    }
}