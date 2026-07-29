#include "pch.h"

#include "src/Api/ModManagerApi.h"

#include "src/Api/FrameApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeBoot.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Core/ForgeText.h"
#include "src/Mods/ModFileSystem.h"
#include "src/Mods/ModHost.h"
#include "src/Mods/ModLoadScreen.h"
#include "src/Mods/ModState.h"

#include <fstream>
#include <stdio.h>
#include <map>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        AmodModManagerApi g_table = {};

        // Anything read once and handed out as a pointer lives here, because the interface
        // promises those pointers outlive the call.
        std::map<std::string, std::string> g_iconPaths;

        // Asked for by the screen, applied on the frame tick.
        std::vector<int> g_pending;

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        const std::vector<ModRecord>& Records()
        {
            return Boot::Registry().LoadOrder();
        }

        const ModRecord* At(int index)
        {
            const std::vector<ModRecord>& records = Records();
            return (index >= 0 && index < static_cast<int>(records.size())) ? &records[index]
                                                                           : nullptr;
        }

        // A mod carrying no code is as loaded as it can be whenever it is enabled: its files
        // are the whole of it, and they are in the override table.
        bool IsInForce(const ModRecord& record)
        {
            return record.manifest.entry.empty()
                       ? ModState::IsEnabled(record.manifest.id)
                       : ModHost::IsLoaded(record.manifest.id);
        }

        bool OutOfStep(const ModRecord& record)
        {
            return ModState::IsEnabled(record.manifest.id) != IsInForce(record);
        }

        // Nothing here runs while a mod is on the stack: this is a frame tick of the loader's
        // own, and those run after the game's frame and before any mod's update.
        void __cdecl ApplyPending()
        {
            if (g_pending.empty())
            {
                return;
            }

            // Taken by copy: an AmodInit reached from here can ask for another reload.
            const std::vector<int> pending = g_pending;
            g_pending.clear();

            // The game's own loading screen for as long as this takes. It is where a player
            // already knows to wait for mods, and a mod with a lot of content takes long enough
            // to be worth showing.
            const bool onScreen = ModLoadScreen::Enter();
            size_t done = 0;

            for (const int index : pending)
            {
                const ModRecord* const record = At(index);
                if (record == nullptr)
                {
                    continue;
                }

                if (onScreen)
                {
                    char line[128];
                    snprintf(line, sizeof(line), "Mods %zu of %zu", done, pending.size());
                    ModLoadScreen::Report(line);
                }

                std::string error;
                if (!ModHost::Reload(*record, error))
                {
                    FORGE_LOG_WARN("'%s' was not reloaded: %s", record->manifest.id.c_str(),
                                   error.c_str());
                }

                ++done;
            }

            if (onScreen)
            {
                char line[128];
                snprintf(line, sizeof(line), "Mods %zu of %zu", done, pending.size());
                ModLoadScreen::Report(line);
                ModLoadScreen::Leave();
            }

            // The files a mod provides go with the mod, and this table is what the game reads
            // resources through. Whatever it has already read stays as it was.
            ModFileSystem::Build(Records());
        }

        // Unpacks the mod's icon next to the loader's cache, once, and answers where it went.
        // The screen wants something it can hand to a texture loader, and the icon is inside
        // an archive nothing else can open.
        const char* IconFile(const ModRecord& record)
        {
            const auto found = g_iconPaths.find(record.manifest.id);
            if (found != g_iconPaths.end())
            {
                return found->second.c_str();
            }

            // Remembered even when there is none, so a mod without an icon is not unpacked
            // again once per frame for as long as the screen is open.
            std::string& slot = g_iconPaths[record.manifest.id];

            // Named or not, the same file is looked for: a mod that simply puts an icon.bmp in
            // its archive gets an icon without having to say so.
            const bool declared = !record.manifest.icon.empty();
            const std::string name = declared ? record.manifest.icon : "icon.bmp";

            std::vector<uint8_t> bytes;
            std::string error;
            if (record.archive == nullptr || !record.archive->ReadByName(name, bytes, error) ||
                bytes.empty())
            {
                // Only when the mod said it had one: a mod that never claimed an icon is not
                // doing anything wrong by not having one.
                if (declared)
                {
                    FORGE_LOG_WARN("[%s] declares the icon '%s', which is not in its archive",
                                   record.manifest.id.c_str(), name.c_str());
                }
                return slot.c_str();
            }

            std::error_code creation;
            std::filesystem::create_directories(Paths::CacheDirectory(), creation);

            const std::filesystem::path path =
                Paths::CacheDirectory() / (FromUtf8(record.manifest.id) + L".icon.bmp");

            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                return slot.c_str();
            }

            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
            file.close();

            slot = PathToUtf8(path);
            return slot.c_str();
        }

        int __cdecl ApiCount(AmodMod mod)
        {
            return Resolve(mod) != nullptr ? static_cast<int>(Records().size()) : 0;
        }

        int __cdecl ApiGet(AmodMod mod, int index, AmodModEntry* out)
        {
            const ModRecord* const record = At(index);
            if (Resolve(mod) == nullptr || out == nullptr || record == nullptr ||
                out->structSize < sizeof(uint32_t))
            {
                return 0;
            }

            const ModManifest& manifest = record->manifest;

            AmodModEntry entry = {};
            entry.structSize = sizeof(AmodModEntry);
            entry.id = manifest.id.c_str();
            entry.name = manifest.name.c_str();
            entry.version = manifest.version.c_str();
            entry.author = manifest.author.c_str();
            entry.description = manifest.description.c_str();
            entry.apiVersion = manifest.apiVersion;
            entry.icon = IconFile(*record);
            entry.capabilityCount = static_cast<int>(manifest.capabilities.size());

            // Held for the life of the process so the pointer stays good.
            static std::map<std::string, std::string> archiveNames;
            std::string& archiveName = archiveNames[manifest.id];
            if (archiveName.empty())
            {
                archiveName = PathToUtf8(record->archivePath.filename());
            }
            entry.archive = archiveName.c_str();

            entry.enabled = ModState::IsEnabled(manifest.id) ? 1 : 0;
            entry.loaded = IsInForce(*record) ? 1 : 0;

            // Everything else Reload settles in this run; a mod that has to be up before the
            // launcher is the one that genuinely cannot be, whatever the switch now says.
            entry.needsRestart = (manifest.Declares("launcher") && OutOfStep(*record)) ? 1 : 0;

            const uint32_t copied = out->structSize < sizeof(entry)
                                        ? out->structSize
                                        : static_cast<uint32_t>(sizeof(entry));
            memcpy(out, &entry, copied);
            memcpy(out, &copied, sizeof(copied));
            return 1;
        }

        const char* __cdecl ApiCapability(AmodMod mod, int index, int capability)
        {
            const ModRecord* const record = At(index);
            if (Resolve(mod) == nullptr || record == nullptr || capability < 0 ||
                capability >= static_cast<int>(record->manifest.capabilities.size()))
            {
                return "";
            }
            return record->manifest.capabilities[capability].c_str();
        }

        const char* __cdecl ApiFailureReason(AmodMod mod, int index)
        {
            const ModRecord* const record = At(index);
            if (Resolve(mod) == nullptr || record == nullptr)
            {
                return "";
            }
            return ModHost::FailureFor(record->manifest.id);
        }

        int __cdecl ApiSetEnabled(AmodMod mod, int index, int enabled)
        {
            const ModRecord* const record = At(index);
            if (Resolve(mod) == nullptr || record == nullptr)
            {
                return 0;
            }

            // The manager cannot switch itself off. It is not installed where the switch
            // reaches, and a screen that can remove the only way back to itself is a trap.
            if (record->privileged)
            {
                FORGE_LOG_WARN("The mod manager cannot be switched off from its own screen");
                return 0;
            }

            return ModState::SetEnabled(record->manifest.id, enabled != 0) ? 1 : 0;
        }

        // Worked out rather than remembered, so it answers false again of its own accord once
        // every switch has been acted on.
        int __cdecl ApiIsDirty(AmodMod mod)
        {
            if (Resolve(mod) == nullptr)
            {
                return 0;
            }

            for (const ModRecord& record : Records())
            {
                if (OutOfStep(record))
                {
                    return 1;
                }
            }
            return 0;
        }

        int __cdecl ApiReload(AmodMod mod, int index)
        {
            const ModRecord* const record = At(index);
            if (Resolve(mod) == nullptr || record == nullptr)
            {
                return 0;
            }

            if (!record->manifest.entry.empty() && record->manifest.Declares("launcher"))
            {
                FORGE_LOG_WARN("'%s' changes the launcher, which has already run; it takes a "
                               "restart, not a reload", record->manifest.id.c_str());
                return 0;
            }

            g_pending.push_back(index);
            return 1;
        }
    }

    bool ModManagerApi::Install()
    {
        FrameApi::AddInternalTick(&ApplyPending);
        return true;
    }

    const AmodModManagerApi* ModManagerApi::Table()
    {
        if (g_table.structSize == 0)
        {
            g_table.structSize = sizeof(AmodModManagerApi);
            g_table.Count = &ApiCount;
            g_table.Get = &ApiGet;
            g_table.Capability = &ApiCapability;
            g_table.FailureReason = &ApiFailureReason;
            g_table.SetEnabled = &ApiSetEnabled;
            g_table.IsDirty = &ApiIsDirty;
            g_table.Reload = &ApiReload;
        }
        return &g_table;
    }

    void ModManagerApi::Clear()
    {
        // The icons were unpacked into the cache and nothing else knows they are there, so they
        // go with the interface that made them. The directory follows once it is empty - the
        // mod libraries have already released their own copies by now.
        for (const auto& entry : g_iconPaths)
        {
            if (!entry.second.empty())
            {
                std::error_code removal;
                std::filesystem::remove(FromUtf8(entry.second), removal);
            }
        }

        std::error_code directory;
        std::filesystem::remove(Paths::CacheDirectory(), directory);

        g_iconPaths.clear();
        g_pending.clear();
    }
}
