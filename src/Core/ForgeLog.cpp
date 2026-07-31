#include <windows.h>

#include "src/Core/ForgeLog.h"

#include <stdarg.h>
#include <stdio.h>

// windows.h reaches sdk/include/ConsoleApi.h rather than the SDK's own <consoleapi.h> - the file
// system does not tell those two names apart, and sdk\include is searched first - so the console
// pair it would have declared is declared here.
extern "C" __declspec(dllimport) BOOL WINAPI AllocConsole(void);
extern "C" __declspec(dllimport) BOOL WINAPI FreeConsole(void);

namespace Forge
{
    namespace
    {
        HANDLE g_file = INVALID_HANDLE_VALUE;
        HANDLE g_console = INVALID_HANDLE_VALUE;
        CRITICAL_SECTION g_lock;
        bool g_lockReady = false;

        const char* LevelTag(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Info:    return "INFO ";
            case LogLevel::Warning: return "WARN ";
            case LogLevel::Error:   return "ERROR";
            }
            return "?????";
        }

        void AttachConsole()
        {
            wchar_t value[8] = {};
            const DWORD length = GetEnvironmentVariableW(L"AZFORGE_CONSOLE", value, ARRAYSIZE(value));
            if (length == 0 || length >= ARRAYSIZE(value) || value[0] == L'0')
            {
                return;
            }

            if (!AllocConsole())
            {
                return;
            }

            SetConsoleOutputCP(CP_UTF8);
            SetConsoleTitleW(L"AzForge");
            g_console = GetStdHandle(STD_OUTPUT_HANDLE);
        }

        // durable=true flushes past the OS cache (WARN/ERROR only — Info must not pay per frame).
        void Emit(const char* text, size_t length, bool durable)
        {
            DWORD written = 0;
            if (g_file != INVALID_HANDLE_VALUE)
            {
                WriteFile(g_file, text, static_cast<DWORD>(length), &written, nullptr);
                if (durable)
                {
                    FlushFileBuffers(g_file);
                }
            }
            if (g_console != INVALID_HANDLE_VALUE)
            {
                WriteFile(g_console, text, static_cast<DWORD>(length), &written, nullptr);
            }
            OutputDebugStringA(text);
        }
    }

    bool Log::Open(const std::wstring& filePath)
    {
        if (g_file != INVALID_HANDLE_VALUE)
        {
            return true;
        }

        if (!g_lockReady)
        {
            // Spin-count form does not raise on OOM (unlike the plain one on older Windows).
            InitializeCriticalSectionAndSpinCount(&g_lock, 4000);
            g_lockReady = true;
        }

        g_file = CreateFileW(filePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        DWORD written = 0;
        WriteFile(g_file, bom, sizeof(bom), &written, nullptr);

        AttachConsole();
        return true;
    }

    void Log::Close()
    {
        if (g_file != INVALID_HANDLE_VALUE)
        {
            FlushFileBuffers(g_file);
            CloseHandle(g_file);
            g_file = INVALID_HANDLE_VALUE;
        }

        if (g_console != INVALID_HANDLE_VALUE)
        {
            FreeConsole();
            g_console = INVALID_HANDLE_VALUE;
        }

        if (g_lockReady)
        {
            DeleteCriticalSection(&g_lock);
            g_lockReady = false;
        }
    }

    bool Log::IsOpen()
    {
        return g_file != INVALID_HANDLE_VALUE;
    }

    void Log::Write(LogLevel level, const char* format, ...)
    {
        char message[2048];
        va_list args;
        va_start(args, format);
        const int produced = vsnprintf(message, sizeof(message), format, args);
        va_end(args);

        if (produced < 0)
        {
            return;
        }

        SYSTEMTIME now = {};
        GetLocalTime(&now);

        char record[2176];
        const int length = snprintf(record, sizeof(record), "[%02u:%02u:%02u.%03u] [%s] %s\r\n",
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
            LevelTag(level), message);
        if (length <= 0)
        {
            return;
        }

        if (g_lockReady)
        {
            EnterCriticalSection(&g_lock);
        }

        Emit(record, static_cast<size_t>(length), level != LogLevel::Info);

        if (g_lockReady)
        {
            LeaveCriticalSection(&g_lock);
        }
    }

    void Log::WriteSeparator()
    {
        static const char kSeparator[] = "------------------------------------------------------------\r\n";

        if (g_lockReady)
        {
            EnterCriticalSection(&g_lock);
        }

        Emit(kSeparator, sizeof(kSeparator) - 1, false);

        if (g_lockReady)
        {
            LeaveCriticalSection(&g_lock);
        }
    }
}