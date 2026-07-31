#pragma once

#include <string>

namespace Forge
{
    enum class LogLevel
    {
        Info,
        Warning,
        Error
    };

    class Log
    {
    public:
        static bool                                                 Open(const std::wstring& filePath);
        static void                                                 Close();
        static bool                                                 IsOpen();

        static void                                                 Write(LogLevel level, const char* format, ...);
        static void                                                 WriteSeparator();
    };
}

#define FORGE_LOG_INFO(...)  ::Forge::Log::Write(::Forge::LogLevel::Info, __VA_ARGS__)
#define FORGE_LOG_WARN(...)  ::Forge::Log::Write(::Forge::LogLevel::Warning, __VA_ARGS__)
#define FORGE_LOG_ERROR(...) ::Forge::Log::Write(::Forge::LogLevel::Error, __VA_ARGS__)
