#pragma once

#include <filesystem>
#include <string>

namespace Forge
{
    std::string                                                     ToUtf8(const std::wstring& text);
    std::wstring                                                    FromUtf8(const std::string& text);
    std::string                                                     PathToUtf8(const std::filesystem::path& path);
    std::string                                                     ToLowerAscii(const std::string& text);
    std::string                                                     NormalizeSlashes(const std::string& path);
}
