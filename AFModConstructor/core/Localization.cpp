#include "Localization.h"

#include "PathUtil.h"

#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::unordered_map<std::string, std::string> g_strings;
std::unordered_map<std::string, std::string> g_composed;
std::unordered_map<std::string, std::string> g_packageStrings;
std::unordered_map<std::string, std::vector<std::string>> g_packageKeys;
std::string g_currentLang;

void TrimAscii(std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i > 0) s.erase(0, i);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
}

void UnescapeValue(std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case 'n':  out.push_back('\n'); ++i; continue;
                case 't':  out.push_back('\t'); ++i; continue;
                case 'r':  out.push_back('\r'); ++i; continue;
                case '\\': out.push_back('\\'); ++i; continue;
                default: break;
            }
        }
        out.push_back(s[i]);
    }
    s = std::move(out);
}

} // namespace

namespace Lang {

bool Load(const std::string& filePath) {
    g_strings.clear();
    g_composed.clear();
    g_currentLang.clear();

    const fs::path p = PathUtil::MakePath(filePath);
    std::ifstream file(p, std::ios::binary);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        TrimAscii(key);
        if (key.empty()) continue;
        if (!val.empty() && val.back() == '\r') val.pop_back();
        UnescapeValue(val);

        g_strings.emplace(std::move(key), std::move(val));
    }

    g_currentLang = PathUtil::U8(p.stem());
    return true;
}

bool Has(const std::string& key) {
    return g_strings.find(key) != g_strings.end() ||
           g_packageStrings.find(key) != g_packageStrings.end();
}

void AddPackage(const std::string& id, const std::string& text, const std::vector<std::string>& allowed, std::vector<std::string>* rejected) {
    RemovePackage(id);

    std::unordered_map<std::string, char> wanted;
    for (const std::string& key : allowed) wanted[key] = 1;

    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;

        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        TrimAscii(key);
        if (key.empty()) continue;

        if (wanted.find(key) == wanted.end()) {
            if (rejected) rejected->push_back(key);
            continue;
        }
        if (g_strings.find(key) != g_strings.end()) continue;

        UnescapeValue(val);
        const auto already = g_packageStrings.find(key);
        if (already != g_packageStrings.end()) {
            if (already->second != val && rejected) rejected->push_back(key);
            continue;
        }

        g_packageStrings[key] = val;
        g_packageKeys[id].push_back(key);
    }
    g_composed.clear();
}

void RemovePackage(const std::string& id) {
    const auto found = g_packageKeys.find(id);
    if (found == g_packageKeys.end()) return;
    for (const std::string& key : found->second) g_packageStrings.erase(key);
    g_packageKeys.erase(found);
    g_composed.clear();
}

void ClearPackages() {
    g_packageStrings.clear();
    g_packageKeys.clear();
    g_composed.clear();
}

const char* Get(const char* key) {
    if (!key) return "";
    auto it = g_strings.find(key);
    if (it != g_strings.end()) return it->second.c_str();
    it = g_packageStrings.find(key);
    if (it != g_packageStrings.end()) return it->second.c_str();
    return key;
}

const char* Get(const std::string& key) { return Get(key.c_str()); }

const char* Compose(const char* prefix, const char* key, const char* suffix) {
    const char* text = Get(key);
    std::string id = std::string(prefix) + '\x1f' + key + '\x1f' + suffix;
    auto it = g_composed.find(id);
    if (it == g_composed.end()) {
        it = g_composed.emplace(std::move(id), std::string(prefix) + text + suffix).first;
    }
    return it->second.c_str();
}

const std::string& CurrentLanguage() { return g_currentLang; }

} // namespace Lang
