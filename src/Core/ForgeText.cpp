#include <windows.h>

#include "src/Core/ForgeText.h"

namespace Forge
{
    std::string ToUtf8(const std::wstring& text)
    {
        if (text.empty())
        {
            return std::string();
        }

        const int required = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                                 nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            return std::string();
        }

        std::string result(static_cast<size_t>(required), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                            result.data(), required, nullptr, nullptr);
        return result;
    }

    std::wstring FromUtf8(const std::string& text)
    {
        if (text.empty())
        {
            return std::wstring();
        }

        const int required = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                                 nullptr, 0);
        if (required <= 0)
        {
            return std::wstring();
        }

        std::wstring result(static_cast<size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                            result.data(), required);
        return result;
    }

    std::string ToLowerAscii(const std::string& text)
    {
        std::string result = text;
        for (char& c : result)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return result;
    }

    std::string NormalizeSlashes(const std::string& path)
    {
        std::string result = path;
        for (char& c : result)
        {
            if (c == '\\')
            {
                c = '/';
            }
        }

        while (result.size() >= 2 && result[0] == '.' && result[1] == '/')
        {
            result.erase(0, 2);
        }

        return result;
    }

    std::string PathToUtf8(const std::filesystem::path& path)
    {
        return ToUtf8(path.wstring());
    }
}
