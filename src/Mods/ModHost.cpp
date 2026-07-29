#include "pch.h"

#include "src/Mods/ModHost.h"

#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Mods/ModLibrary.h"
#include "src/Mods/ModState.h"

#include <memory>

namespace Forge
{
    namespace
    {
        // Held by pointer because a loaded mod holds the address of the table inside its
        // ModLibrary, which must not move as the collection grows.
        std::vector<std::unique_ptr<ModLibrary>> g_loaded;

        // Counted across both phases, because what the loading screen reports is the whole
        // run and not the half of it that happened there.
        size_t g_failed = 0;
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
        // What the loading screen is counting towards, so a mod switched off is not one of
        // them: "3 of 7" has to mean seven that were going to be loaded.
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

    void ModHost::Load(const std::vector<ModRecord>& loadOrder, ModPhase phase,
                       void (*afterEach)())
    {
        // A mod belongs to the early phase only if it said it changes the launcher, because
        // the launcher is the one thing that has already happened by the time the loading
        // screen is up.
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
            // A mod the player switched off is passed over in silence here: it was already
            // reported when the switches were read, and repeating it once per mod per run
            // would bury the lines that matter.
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
                ++g_failed;
            }
            else
            {
                g_loaded.push_back(std::move(library));
            }

            // After the failure as well as the success: the count the player is being shown
            // has moved either way.
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

        // The cache directory only ever holds the transient copies, which are gone by now.
        std::error_code removal;
        std::filesystem::remove(Paths::CacheDirectory(), removal);

        g_failed = 0;
    }

    size_t ModHost::LoadedCount()
    {
        return g_loaded.size();
    }
}
