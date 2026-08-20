#include "src/Mods/ModStore.h"

#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Core/ForgeSeal.h"
#include "src/Core/ForgeText.h"

#include <windows.h>

#include <filesystem>
#include <fstream>

namespace Forge
{
    namespace
    {
        const char kProtectedMark = '+';

        struct Held
        {
            StoreRecord record;
            bool protect;
        };

        struct Kept
        {
            ModContext* owner;
            std::string id;
            std::vector<Held> records;
            bool loaded;
            bool dirty;
        };

        // Saves changes early to preserve session state without writing every frame.
        const uint32_t kWriteAfterMs = 2000;

        uint32_t g_lastWrite = 0;

        std::vector<Kept> g_kept;

        Kept* Find(ModContext* owner)
        {
            for (Kept& entry : g_kept)
            {
                if (entry.owner == owner)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        std::filesystem::path FileOf(const std::string& id)
        {
            return Paths::ModsDirectory() / L"state" / (FromUtf8(id) + L".forge");
        }

        // Values with a space in them are written quoted, so a line splits back the same way.
        std::vector<std::string> Split(const std::string& line)
        {
            std::vector<std::string> parts;
            size_t at = 0;

            while (at < line.size())
            {
                while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
                {
                    ++at;
                }
                if (at >= line.size())
                {
                    break;
                }

                std::string value;
                if (line[at] == '"')
                {
                    for (++at; at < line.size() && line[at] != '"'; ++at)
                    {
                        value += line[at];
                    }
                    ++at;
                }
                else
                {
                    for (; at < line.size() && line[at] != ' ' && line[at] != '\t'; ++at)
                    {
                        value += line[at];
                    }
                }

                parts.push_back(value);
            }

            return parts;
        }

        // The mark goes into the tag, or a protected record could be rewritten as a plain one.
        std::string Marked(const std::string& kind, bool protect)
        {
            return protect ? std::string(1, kProtectedMark) + kind : kind;
        }

        // Every record is tagged over its plain values; only a protected one is also hidden.
        bool Hide(const std::string& id, const StoreRecord& record, bool protect, std::vector<std::string>& out)
        {
            const std::string kind = Marked(record.kind, protect);
            const std::string tag = Seal::Tag(id, kind, record.key, record.values);

            if (tag.empty())
            {
                return false;
            }

            out.clear();

            for (size_t at = 0; at < record.values.size(); ++at)
            {
                if (!protect)
                {
                    out.push_back(record.values[at]);
                    continue;
                }

                const std::string hidden = Seal::Hide(id, kind, record.key, static_cast<int>(at),
                                                      record.values[at]);

                if (hidden.empty() && !record.values[at].empty())
                {
                    return false;
                }

                out.push_back(hidden);
            }

            out.push_back(tag);
            return true;
        }

        // The values come back out of hiding and the tag is asked whether they are the ones written.
        bool Show(const std::string& id, StoreRecord& record, bool protect)
        {
            if (record.values.empty())
            {
                return false;
            }

            const std::string kind = Marked(record.kind, protect);
            const std::string tag = record.values.back();
            record.values.pop_back();

            for (size_t at = 0; protect && at < record.values.size(); ++at)
            {
                std::string shown;

                if (!Seal::Show(id, kind, record.key, static_cast<int>(at), record.values[at], shown))
                {
                    return false;
                }

                record.values[at] = shown;
            }

            return tag == Seal::Tag(id, kind, record.key, record.values);
        }

        std::string Quoted(const std::string& value)
        {
            if (!value.empty() && value.find_first_of(" \t\"") == std::string::npos)
            {
                return value;
            }

            std::string out = "\"";
            for (const char letter : value)
            {
                if (letter != '"')
                {
                    out += letter;
                }
            }
            out += '"';
            return out;
        }
    }

    void ModStore::Load(ModContext* owner, const std::string& id)
    {
        if (owner == nullptr)
        {
            return;
        }

        Kept* entry = Find(owner);
        if (entry == nullptr)
        {
            g_kept.push_back(Kept{ owner, id, {}, false, false });
            entry = &g_kept.back();
        }

        entry->id = id;
        entry->records.clear();
        entry->loaded = true;

        std::ifstream file(FileOf(id));
        if (!file)
        {
            return;
        }

        std::string line;
        while (std::getline(file, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            {
                line.pop_back();
            }
            if (line.empty() || line[0] == '#')
            {
                continue;
            }

            const std::vector<std::string> parts = Split(line);
            if (parts.size() < 3)
            {
                continue;
            }

            const bool protect = parts[0][0] == kProtectedMark;

            StoreRecord record;
            record.kind = protect ? parts[0].substr(1) : parts[0];
            record.key = parts[1];
            record.values.assign(parts.begin() + 2, parts.end());

            // A record that does not answer to its tag is left out, never quietly defaulted.
            if (!Show(id, record, protect))
            {
                FORGE_LOG_WARN("[%s] kept %s '%s' does not answer to its tag and is not put back",
                               id.c_str(), record.kind.c_str(), record.key.c_str());
                continue;
            }

            entry->records.push_back(Held{ record, protect });
        }

        FORGE_LOG_INFO("[%s] %zu kept setting(s) read back", id.c_str(), entry->records.size());
    }

    void ModStore::Save(ModContext* owner)
    {
        Kept* const entry = Find(owner);
        if (entry == nullptr || !entry->loaded)
        {
            return;
        }

        entry->dirty = false;

        const std::filesystem::path file = FileOf(entry->id);
        std::error_code failure;

        if (entry->records.empty())
        {
            std::filesystem::remove(file, failure);
            return;
        }

        std::filesystem::create_directories(file.parent_path(), failure);

        std::ofstream out(file, std::ios::trunc);
        if (!out)
        {
            FORGE_LOG_ERROR("[%s] its kept settings could not be written", entry->id.c_str());
            return;
        }

        for (const Held& held : entry->records)
        {
            std::vector<std::string> values;

            if (!Hide(entry->id, held.record, held.protect, values))
            {
                FORGE_LOG_ERROR("[%s] kept %s '%s' could not be sealed, so it is not written", entry->id.c_str(), held.record.kind.c_str(), held.record.key.c_str());
                continue;
            }

            if (held.protect)
            {
                out << kProtectedMark;
            }

            out << held.record.kind << ' ' << Quoted(held.record.key);
            for (const std::string& value : values)
            {
                out << ' ' << Quoted(value);
            }
            out << '\n';
        }
    }

    void __cdecl ModStore::Tick()
    {
        const uint32_t now = GetTickCount();

        if (now - g_lastWrite < kWriteAfterMs)
        {
            return;
        }

        g_lastWrite = now;

        // Save clears the flag, so a mod that changes nothing costs one comparison a second.
        for (Kept& entry : g_kept)
        {
            if (entry.dirty)
            {
                Save(entry.owner);
            }
        }
    }

    void ModStore::Drop(ModContext* owner)
    {
        for (auto it = g_kept.begin(); it != g_kept.end();)
        {
            it = it->owner == owner ? g_kept.erase(it) : ++it;
        }
    }

    void ModStore::Put(ModContext* owner, const StoreRecord& record)
    {
        Kept* const entry = Find(owner);
        if (entry == nullptr)
        {
            return;
        }

        // Only the values: whether the record is protected is the store's to remember.
        for (Held& held : entry->records)
        {
            if (held.record.kind == record.kind && held.record.key == record.key)
            {
                entry->dirty = entry->dirty || held.record.values != record.values;
                held.record.values = record.values;
                return;
            }
        }

        entry->records.push_back(Held{ record, false });
        entry->dirty = true;
    }

    void ModStore::Protect(ModContext* owner, const char* kind, const char* key, bool protect)
    {
        Kept* const entry = Find(owner);

        if (entry == nullptr || kind == nullptr || key == nullptr)
        {
            return;
        }

        for (Held& held : entry->records)
        {
            if (held.record.kind == kind && held.record.key == key)
            {
                entry->dirty = entry->dirty || held.protect != protect;
                held.protect = protect;
                return;
            }
        }
    }

    void ModStore::Remove(ModContext* owner, const char* kind, const char* key)
    {
        Kept* const entry = Find(owner);
        if (entry == nullptr || kind == nullptr || key == nullptr)
        {
            return;
        }

        for (auto it = entry->records.begin(); it != entry->records.end();)
        {
            const bool wanted = it->record.kind == kind && it->record.key == key;
            entry->dirty = entry->dirty || wanted;
            it = wanted ? entry->records.erase(it) : ++it;
        }
    }

    std::vector<StoreRecord> ModStore::Records(ModContext* owner, const char* kind)
    {
        const Kept* const entry = Find(owner);
        std::vector<StoreRecord> found;

        if (entry == nullptr || kind == nullptr)
        {
            return found;
        }

        for (const Held& held : entry->records)
        {
            if (held.record.kind == kind)
            {
                found.push_back(held.record);
            }
        }

        return found;
    }
}
