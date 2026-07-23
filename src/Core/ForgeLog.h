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

    // Log file written next to game.exe. Every record is flushed to disk as it is
    // produced so that a crash during loading still leaves a complete log.
    //
    // Set the AZFORGE_CONSOLE environment variable to 1 to additionally allocate a
    // console window and mirror the log to it.
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
