#pragma once

#include <string>

namespace Zip {
bool WriteFolder(const std::string& folder, const std::string& outFile, std::string* error = nullptr);
}
