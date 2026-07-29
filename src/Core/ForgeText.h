#pragma once

#include <filesystem>
#include <string>

namespace Forge
{
    // UTF-8 <-> UTF-16 conversion. Paths are handled as wide strings throughout the
    // loader; the log file and every manifest field are UTF-8.
    std::string                                                     ToUtf8(const std::wstring& text);
    std::wstring                                                    FromUtf8(const std::string& text);

    // A path in the form the log and the manifests use. Every caller that names a file in a
    // message wants this, which is why it lives here rather than being written out again in
    // each of them.
    std::string                                                     PathToUtf8(const std::filesystem::path& path);

    // Lowercases ASCII letters only. Archive entry names and resource paths are matched
    // case-insensitively, matching Package_FindEntryInPak @0x00422b60 which lowercases
    // the requested name with __strlwr before comparing.
    std::string                                                     ToLowerAscii(const std::string& text);

    // Replaces backslashes with forward slashes and strips a leading "./".
    std::string                                                     NormalizeSlashes(const std::string& path);
}
