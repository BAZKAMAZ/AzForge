#include "ModPackages.h"

#include "PathUtil.h"
#include "SharedConfig.h"
#include "json.hpp"
#include "src/Zip/ZipArchive.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::vector<ModPackages::Package> g_packages;

std::vector<std::string> ReadList(const json& j, const char* key) {
    std::vector<std::string> list;
    if (!j.contains(key) || !j[key].is_array()) return list;
    for (const json& item : j[key]) {
        if (item.is_string()) list.push_back(item.get<std::string>());
    }
    return list;
}

bool IsPlainId(const std::string& id) {
    if (id.empty() || id.size() > 48) return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return id[0] >= 'a' && id[0] <= 'z';
}

bool ReadManifest(ModPackages::Package& package, std::string* error) {
    Forge::ZipArchive zip;
    std::string zipError;
    if (!zip.Open(PathUtil::MakePath(package.archive), zipError)) {
        if (error) *error = zipError;
        return false;
    }

    std::vector<uint8_t> bytes;
    if (!zip.ReadByName("package.json", bytes, zipError)) {
        if (error) *error = "no package.json: " + zipError;
        return false;
    }

    json j;
    try {
        j = json::parse(std::string(bytes.begin(), bytes.end()));
    } catch (const std::exception& e) {
        if (error) *error = std::string("package.json: ") + e.what();
        return false;
    }

    package.id = j.value("id", "");
    if (!IsPlainId(package.id)) {
        if (error) *error = "a package id is lowercase latin letters, digits and underscore";
        return false;
    }

    package.name = j.value("name", package.id);
    package.version = j.value("version", "");
    package.author = j.value("author", "");
    package.tiers = ReadList(j, "tiers");
    package.namespaces = ReadList(j, "namespaces");
    if (package.namespaces.empty()) package.namespaces.push_back(package.id);
    package.include = ReadList(j, "include");
    package.sources = ReadList(j, "sources");
    package.libDirs = ReadList(j, "libDirs");
    package.libs = ReadList(j, "libs");
    if (package.tiers.empty()) package.tiers.push_back("tcc");
    return true;
}

} // namespace

std::string ModPackages::Dir() {
    return PathUtil::U8(PathUtil::MakePath(SharedConfig::Dir()) / "packages");
}

bool ModPackages::Owns(const Package& package, const std::string& id) {
    for (const std::string& space : package.namespaces) {
        if (id == space) return true;
        if (id.size() > space.size() && id.compare(0, space.size(), space) == 0 && id[space.size()] == '.')
            return true;
    }
    return false;
}

bool ModPackages::Scan(std::string* error) {
    g_packages.clear();

    std::error_code ec;
    const fs::path dir = PathUtil::MakePath(Dir());
    fs::create_directories(dir, ec);

    std::vector<fs::path> archives;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string suffix = PathUtil::U8(entry.path().extension());
        for (char& c : suffix) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (suffix == ".zip") archives.push_back(entry.path());
    }
    std::sort(archives.begin(), archives.end());

    for (const fs::path& archivePath : archives) {
        Package package;
        package.archive = PathUtil::U8(archivePath);

        std::string why;
        if (!ReadManifest(package, &why)) {
            package.id = PathUtil::U8(archivePath.stem());
            package.name = package.id;
            package.enabled = false;
            package.error = why;
        }

        for (const Package& already : g_packages) {
            if (!package.error.empty()) break;
            if (already.id == package.id) {
                package.enabled = false;
                package.error = "id already taken by " + already.archive;
                break;
            }
            for (const std::string& space : package.namespaces) {
                if (!Owns(already, space)) continue;
                package.enabled = false;
                package.error = "namespace '" + space + "' already taken by " + already.id;
                break;
            }
        }
        g_packages.push_back(std::move(package));
    }

    if (error && g_packages.empty()) *error = "no packages in " + Dir();
    return true;
}

const std::vector<ModPackages::Package>& ModPackages::All() { return g_packages; }

const ModPackages::Package* ModPackages::Find(const std::string& id) {
    for (const Package& package : g_packages) {
        if (package.id == id) return &package;
    }
    return nullptr;
}

bool ModPackages::ReadFile(const Package& package, const std::string& path, std::vector<unsigned char>& out, std::string* error) {
    Forge::ZipArchive zip;
    std::string why;
    if (!zip.Open(PathUtil::MakePath(package.archive), why)) {
        if (error) *error = why;
        return false;
    }
    std::vector<uint8_t> raw;
    if (!zip.ReadByName(path, raw, why)) {
        if (error) *error = why;
        return false;
    }
    out.assign(raw.begin(), raw.end());
    return true;
}

bool ModPackages::ReadFile(const Package& package, const std::string& path, std::string& out, std::string* error) {
    std::vector<unsigned char> bytes;
    if (!ReadFile(package, path, bytes, error)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

bool ModPackages::NeedsUnpack(const Package& package) {
    return !package.include.empty() || !package.sources.empty() || !package.libDirs.empty();
}

bool ModPackages::Unpack(const Package& package, std::string& folder, std::string* error) {
    const fs::path cache = PathUtil::MakePath(Dir()) / ".cache" /
                           PathUtil::MakePath(package.id + (package.version.empty() ? "" : "-" + package.version));
    folder = PathUtil::U8(cache);

    std::error_code ec;
    const fs::path stamp = cache / "package.json";
    if (fs::exists(stamp, ec)) {
        const fs::file_time_type built = fs::last_write_time(stamp, ec);
        const fs::file_time_type packed = fs::last_write_time(PathUtil::MakePath(package.archive), ec);
        if (!ec && packed <= built) return true;
        fs::remove_all(cache, ec);
    }

    Forge::ZipArchive zip;
    std::string why;
    if (!zip.Open(PathUtil::MakePath(package.archive), why)) {
        if (error) *error = why;
        return false;
    }

    fs::create_directories(cache, ec);
    for (const Forge::ZipEntry& entry : zip.Entries()) {
        if (entry.name.empty() || entry.name.back() == '/') continue;

        const fs::path target = cache / PathUtil::MakePath(entry.lookupName);
        fs::create_directories(target.parent_path(), ec);

        std::vector<uint8_t> bytes;
        if (!zip.Read(entry, bytes, why)) {
            if (error) *error = entry.name + ": " + why;
            return false;
        }
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot write " + PathUtil::U8(target);
            return false;
        }
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
    }
    return true;
}
