#pragma once

#include <string>

namespace ModRuntime {

std::string Library();
std::string Header();
bool Build(std::string* log = nullptr, std::string* error = nullptr);
std::string Stamp();

} // namespace ModRuntime
