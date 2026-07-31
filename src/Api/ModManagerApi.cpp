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

        void __cdecl ApplyPending()
        {
            if (g_pending.empty())
            {
                return;
            }

            // Taken by copy: an AmodInit reached from here can ask for another reload.
            const std::vector<int> pending = g_pending;
            g_pending.clear();

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

            ModFileSystem::Build(Records());
        }

        const char* IconFile(const ModRecord& record)
        {
            const auto found = g_iconPaths.find(record.manifest.id);
            if (found != g_iconPaths.end())
            {
                return found->second.c_str();
            }

            std::string& slot = g_iconPaths[record.manifest.id];

            const bool declared = !record.manifest.icon.empty();
            const std::string name = declared ? record.manifest.icon : "icon.bmp";

            std::vector<uint8_t> bytes;
            std::string error;
            if (record.archive == nullptr || !record.archive->ReadByName(name, bytes, error) ||
                bytes.empty())
            {
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

            if (record->privileged)
            {
                FORGE_LOG_WARN("The mod manager cannot be switched off from its own screen");
                return 0;
            }

            return ModState::SetEnabled(record->manifest.id, enabled != 0) ? 1 : 0;
        }

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
