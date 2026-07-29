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

    // Log file written next to game.exe.
    //
    // Every record is written as it is produced, and a warning or an error is additionally
    // forced past the operating system's cache. That distinction is the point: a process
    // that crashes leaves a complete log either way, because the kernel writes the cache out
    // when it tears the handle down, but a machine that hangs hard does not - and a bad
    // detour is exactly the fault that hangs one. Forcing the records that explain a failure
    // costs a seek each; forcing the rest would make a mod that logs every frame pay for a
    // seek every frame.
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
