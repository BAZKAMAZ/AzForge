#pragma once

#include <string>
#include <vector>

namespace ModProject {

enum class Tier { Data = 0, Tcc = 1, Vs = 2 };

struct Manifest {
    std::string id;
    std::string name;
    std::string version = "1.0.0";
    std::string author;
    std::string description;
    int apiVersion = 1;
    std::vector<std::string> requires_;
    std::vector<std::string> capabilities;
    std::vector<std::string> loadAfter;
    std::vector<std::string> conflicts;
    std::string gameVersion = "1.0";
    std::string entry;
    std::string icon;
};

std::string SlugOf(const std::string& name);
int CurrentApiVersion();
const std::vector<std::string>& Subsystems();
bool IsModProject(const std::string& projectDir);

bool Create(const std::string& projectDir, const std::string& name, Tier tier = Tier::Tcc, std::string* error = nullptr);
bool Create(const std::string& projectDir, const Manifest& manifest, Tier tier = Tier::Tcc, std::string* error = nullptr);
bool LoadManifest(const std::string& projectDir, Manifest& out, std::string* error = nullptr);
bool SaveManifest(const std::string& projectDir, const Manifest& manifest, std::string* error = nullptr);

std::string AmodPackPath();
bool Build(const std::string& projectDir, std::string* outPath = nullptr, std::string* log = nullptr, std::string* error = nullptr);
bool Install(const std::string& amodPath, std::string* log = nullptr, std::string* error = nullptr);

} // namespace ModProject
