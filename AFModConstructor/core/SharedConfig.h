#pragma once

#include <string>
#include <vector>

// Shared config between AzForge and AFModConstructor: %USERPROFILE%\.bazkamaz\config.json
namespace SharedConfig {

std::string Dir();
std::string File();

std::string Get(const std::string& key, const std::string& fallback = {});
bool Set(const std::string& key, const std::string& value);

std::vector<std::string> GetList(const std::string& key);
bool SetList(const std::string& key, const std::vector<std::string>& items);

std::string GamePath();
bool SetGamePath(const std::string& path);

} // namespace SharedConfig
