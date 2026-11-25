#pragma once

#include <string>
#include <vector>

namespace ModPackages {

struct Package {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string archive;
    std::vector<std::string> namespaces;
    std::vector<std::string> tiers;
    std::vector<std::string> include;
    std::vector<std::string> sources;
    std::vector<std::string> libDirs;
    std::vector<std::string> libs;
    bool enabled = true;
    std::string error;
};

std::string Dir();
bool Scan(std::string* error = nullptr);
bool Owns(const Package& package, const std::string& id);
const std::vector<Package>& All();
const Package* Find(const std::string& id);

bool ReadFile(const Package& package, const std::string& path, std::string& out, std::string* error = nullptr);
bool ReadFile(const Package& package, const std::string& path, std::vector<unsigned char>& out, std::string* error = nullptr);

bool Unpack(const Package& package, std::string& folder, std::string* error = nullptr);
bool NeedsUnpack(const Package& package);

} // namespace ModPackages
