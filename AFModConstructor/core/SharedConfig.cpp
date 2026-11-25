#include "SharedConfig.h"

#include "PathUtil.h"
#include "json.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace SharedConfig {
namespace {

json Load() {
    const std::string file = File();
    if (file.empty()) return json::object();
    std::ifstream in(PathUtil::MakePath(file), std::ios::binary);
    if (!in) return json::object();
    json data = json::parse(in, nullptr, false);
    return data.is_object() ? data : json::object();
}

bool Save(const json& data) {
    const std::string dir = Dir();
    if (dir.empty()) return false;
    std::error_code ec;
    fs::create_directories(PathUtil::MakePath(dir), ec);
    std::ofstream out(PathUtil::MakePath(File()), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << data.dump(2) << "\n";
    return true;
}

} // namespace

std::string Dir() {
    wchar_t profile[32768] = {};
    const DWORD size = GetEnvironmentVariableW(L"USERPROFILE", profile, static_cast<DWORD>(std::size(profile)));
    if (size == 0) return {};
    return PathUtil::U8(fs::path(profile) / ".bazkamaz");
}

std::string File() {
    const std::string dir = Dir();
    return dir.empty() ? std::string() : PathUtil::U8(PathUtil::MakePath(dir) / "config.json");
}

std::string Get(const std::string& key, const std::string& fallback) {
    const json data = Load();
    const auto it = data.find(key);
    return it != data.end() && it->is_string() ? it->get<std::string>() : fallback;
}

bool Set(const std::string& key, const std::string& value) {
    json data = Load();
    data[key] = value;
    return Save(data);
}

std::vector<std::string> GetList(const std::string& key) {
    std::vector<std::string> out;
    const json data = Load();
    const auto it = data.find(key);
    if (it != data.end() && it->is_array()) {
        for (const auto& item : *it) {
            if (item.is_string()) out.push_back(item.get<std::string>());
        }
    }
    return out;
}

bool SetList(const std::string& key, const std::vector<std::string>& items) {
    json data = Load();
    data[key] = items;
    return Save(data);
}

std::string GamePath() { return Get("gamePath"); }
bool SetGamePath(const std::string& path) { return Set("gamePath", path); }

} // namespace SharedConfig
