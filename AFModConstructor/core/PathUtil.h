#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

namespace PathUtil {

inline std::filesystem::path MakePath(const std::string& utf8) {
#if defined(__cpp_lib_char8_t) || __cplusplus >= 202002L
    return std::filesystem::path(reinterpret_cast<const char8_t*>(utf8.c_str()));
#else
    return std::filesystem::u8path(utf8);
#endif
}

inline std::string U8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

inline std::string ExeDir() {
    wchar_t buf[32768] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (len == 0) return {};
    return U8(std::filesystem::path(buf).parent_path());
}

inline std::filesystem::path ResourcePath(const std::string& rel) {
    return MakePath(ExeDir()) / MakePath(rel);
}

} // namespace PathUtil
