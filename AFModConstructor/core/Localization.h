#pragma once

#include <string>
#include <vector>

namespace Lang {

bool Load(const std::string& filePath);
bool Has(const std::string& key);

const char* Get(const char* key);
const char* Get(const std::string& key);
const char* Compose(const char* prefix, const char* key, const char* suffix = "");

const std::string& CurrentLanguage();

void AddPackage(const std::string& id, const std::string& text, const std::vector<std::string>& allowed, std::vector<std::string>* rejected = nullptr);
void RemovePackage(const std::string& id);
void ClearPackages();

} // namespace Lang

#define LANG(key) Lang::Get(key)
