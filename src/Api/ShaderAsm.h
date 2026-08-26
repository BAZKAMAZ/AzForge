#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace Forge
{
    bool AssembleShader(const std::string& source, std::vector<uint32_t>& tokens, std::string& error);
}
