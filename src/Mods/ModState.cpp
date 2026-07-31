#include "src/Mods/ModState.h"

#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Core/ForgeText.h"

#include <fstream>
#include <map>

namespace Forge
{
    namespace
    {
        std::map<std::string, bool> g_state;
        bool g_loaded = false;

        std::filesystem::path StateFile()
        {
            return Paths::ModsDirectory() / "enabled.cfg";
        }

        std::string Trim(const std::string& text)
        {
            size_t begin = 0;
            size_t end = text.size();
            while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r'))
            {
                ++begin;
            }
            while (end > begin &&
                   (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
            {
                --end;
            }
            return text.substr(begin, end - begin);
        }
    }

    void ModState::Load()
    {
        g_state.clear();
        g_loaded = true;

        std::ifstream file(StateFile());
        if (!file.is_open())
        {
            return;
        }

        std::string line;
        while (std::getline(file, line))
        {
            line = Trim(line);
            if (line.empty() || line[0] == '#')
            {
                continue;
            }

            // "<id> <flag>", split at the last space so an id is taken whole.
            const size_t split = line.find_last_of(" \t");
            if (split == std::string::npos)
            {
                continue;
            }

            const std::string id = Trim(line.substr(0, split));
            const std::string flag = Trim(line.substr(split + 1));
            if (!id.empty() && (flag == "0" || flag == "1"))
            {
                g_state[id] = flag == "1";
            }
        }

        size_t disabled = 0;
        for (const auto& entry : g_state)
        {
            if (!entry.second)
            {
                ++disabled;
            }
        }

        if (disabled != 0)
        {
            FORGE_LOG_INFO("%zu mod(s) are switched off in %s", disabled,
                           PathToUtf8(StateFile()).c_str());
        }
    }

    void ModState::Save()
    {
        std::ofstream file(StateFile(), std::ios::trunc);
        if (!file.is_open())
        {
            FORGE_LOG_ERROR("Cannot write %s, so the mod switches will not survive this run",
                            PathToUtf8(StateFile()).c_str());
            return;
        }

        file << "# Which mods are switched on. A mod that is not named here is on.\n";
        file << "# Written by AzForge; safe to edit by hand while the game is not running.\n";

        for (const auto& entry : g_state)
        {
            file << entry.first << ' ' << (entry.second ? '1' : '0') << '\n';
        }
    }

    bool ModState::IsEnabled(const std::string& id)
    {
        const auto found = g_state.find(id);
        return found == g_state.end() || found->second;
    }

    bool ModState::SetEnabled(const std::string& id, bool enabled)
    {
        if (id.empty() || IsEnabled(id) == enabled)
        {
            return false;
        }

        g_state[id] = enabled;
        Save();

        FORGE_LOG_INFO("'%s' will be %s on the next start", id.c_str(),
                       enabled ? "loaded" : "left out");
        return true;
    }

    void ModState::Clear()
    {
        g_state.clear();
        g_loaded = false;
    }
}
