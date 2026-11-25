#pragma once

#include <string>

// Direct game launcher for AFModConstructor: no azangarastudio.amod required.
namespace GameLauncher {

std::string GameDir();
bool HasAzForge(const std::string& gameDir);
bool Launch(std::string* error = nullptr);

} // namespace GameLauncher
