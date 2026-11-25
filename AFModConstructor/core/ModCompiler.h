#pragma once

#include <set>
#include <string>
#include <vector>

namespace ModCompiler {

struct Toolchain {
    std::string gcc;
    std::string machine;
    bool ok = false;
};

std::string BundledDir();
Toolchain Find();
std::string SdkInclude();

struct Extras {
    std::vector<std::string> include;
    std::vector<std::string> sources;
    std::vector<std::string> libDirs;
    std::vector<std::string> libs;
};

bool Gather(const std::string& tier, const std::set<std::string>& groups, Extras& out, std::string* error = nullptr);

bool Run(const std::string& commandLine, std::string* output, int* exitCode);
std::string Quote(const std::string& text);
std::string ShortPath(const std::string& path);

bool Compile(const std::string& sourceFile, const std::string& outDll, std::string* log = nullptr, std::string* error = nullptr);
bool Compile(const std::string& sourceFile, const std::string& outDll, const Extras& extras, std::string* log = nullptr, std::string* error = nullptr);

} // namespace ModCompiler
