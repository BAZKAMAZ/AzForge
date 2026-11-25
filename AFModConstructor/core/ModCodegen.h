#pragma once

#include "ModLogic.h"

#include <set>
#include <string>
#include <vector>

namespace ModCodegen {

std::string Generate(const std::vector<ModLogic::Sheet>& sheets, std::vector<std::string>* warnings = nullptr);

std::string GenerateRulePreview(const ModLogic::Sheet& sheet, const ModLogic::Rule& rule, std::vector<std::string>* warnings = nullptr);

std::set<std::string> UsedGroups(const std::vector<ModLogic::Sheet>& sheets);

bool Write(const std::string& projectDir, const std::vector<ModLogic::Sheet>& sheets, std::string* outPath = nullptr, std::string* error = nullptr, std::vector<std::string>* warnings = nullptr);

} // namespace ModCodegen
