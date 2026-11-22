#include <windows.h>
#include <dbghelp.h>

#include "src/Core/ForgeCrash.h"
#include "src/Core/ForgeLog.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace Forge
{
    namespace
    {
        PVOID g_handle = nullptr;
        int g_reported = 0;

        constexpr int kMaxReports = 8;
        constexpr size_t kStackWords = 256;
        constexpr int kStackFrames = 16;

        // Fatal exception codes.
        bool IsFatal(DWORD code)
        {
            switch (code)
            {
            case EXCEPTION_ACCESS_VIOLATION:
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            case EXCEPTION_ILLEGAL_INSTRUCTION:
            case EXCEPTION_IN_PAGE_ERROR:
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
            case EXCEPTION_PRIV_INSTRUCTION:
            case EXCEPTION_STACK_OVERFLOW:
                return true;
            default:
                return false;
            }
        }

        const char* CodeName(DWORD code)
        {
            switch (code)
            {
            case EXCEPTION_ACCESS_VIOLATION: return "access violation";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
            case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
            case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
            case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
            case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
            case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
            default: return "exception";
            }
        }

        // Symbols load on the first report only; a module that shipped no PDB just keeps its offset.
        bool g_symbolsTried = false;
        bool g_symbolsReady = false;

        bool Symbols()
        {
            if (!g_symbolsTried)
            {
                g_symbolsTried = true;
                SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
                g_symbolsReady = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
            }

            return g_symbolsReady;
        }

        // " in Function (File.cpp:123)", or nothing when the module has no symbols to give.
        void Symbolize(const void* address, char* buffer, size_t size)
        {
            buffer[0] = 0;

            if (!Symbols())
            {
                return;
            }

            const DWORD64 at = static_cast<DWORD64>(reinterpret_cast<uintptr_t>(address));
            alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
            SYMBOL_INFO* const symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
            DWORD64 displacement = 0;

            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;

            if (!SymFromAddr(GetCurrentProcess(), at, &displacement, symbol))
            {
                return;
            }

            IMAGEHLP_LINE64 line = {};
            DWORD column = 0;
            line.SizeOfStruct = sizeof(line);

            if (SymGetLineFromAddr64(GetCurrentProcess(), at, &column, &line) && line.FileName != nullptr)
            {
                const char* const slash = strrchr(line.FileName, '\\');
                _snprintf_s(buffer, size, _TRUNCATE, " in %s (%s:%u)", symbol->Name,
                    slash != nullptr ? slash + 1 : line.FileName, static_cast<unsigned>(line.LineNumber));
            }
            else
            {
                _snprintf_s(buffer, size, _TRUNCATE, " in %s+0x%X", symbol->Name, static_cast<unsigned>(displacement));
            }
        }

        // Formats an address as "game.exe+offset", with the function and line when the module has symbols.
        bool Describe(const void* address, char* buffer, size_t size)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                static_cast<LPCSTR>(address), &module) || module == nullptr)
            {
                return false;
            }

            char path[MAX_PATH] = {};
            GetModuleFileNameA(module, path, sizeof(path));
            const char* const slash = strrchr(path, '\\');

            // Truncated rather than refused: sprintf_s on a short buffer would end the process from inside its own report.
            char symbol[512] = {};
            Symbolize(address, symbol, sizeof(symbol));

            _snprintf_s(buffer, size, _TRUNCATE, "%s+0x%08X%s", slash != nullptr ? slash + 1 : path,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)), symbol);
            return true;
        }

        // A fault a guarded call can live through; a stack overflow cannot be undone here.
        int GuardFilter(DWORD code)
        {
            return IsFatal(code) && code != EXCEPTION_STACK_OVERFLOW ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
        }

        bool IsCode(const void* address)
        {
            MEMORY_BASIC_INFORMATION info = {};
            if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT)
            {
                return false;
            }

            const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return (info.Protect & executable) != 0;
        }

        // Scans the stack for return addresses.
        void WriteStack(const CONTEXT& context)
        {
            MEMORY_BASIC_INFORMATION region = {};
            if (VirtualQuery(reinterpret_cast<const void*>(context.Esp), &region, sizeof(region)) != sizeof(region) ||
                region.State != MEM_COMMIT)
            {
                return;
            }

            const uintptr_t end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
            const size_t available = (end - context.Esp) / sizeof(uint32_t);
            const size_t words = available < kStackWords ? available : kStackWords;
            const uint32_t* const stack = reinterpret_cast<const uint32_t*>(context.Esp);
            int written = 0;

            for (size_t i = 0; i < words && written < kStackFrames; ++i)
            {
                const void* const candidate = reinterpret_cast<const void*>(stack[i]);
                char where[MAX_PATH + 512];

                if (candidate != nullptr && IsCode(candidate) && Describe(candidate, where, sizeof(where)))
                {
                    FORGE_LOG_ERROR("    called from 0x%08X  %s", static_cast<unsigned>(reinterpret_cast<uintptr_t>(candidate)), where);
                    ++written;
                }
            }
        }

        LONG CALLBACK OnException(EXCEPTION_POINTERS* pointers)
        {
            if (pointers == nullptr || pointers->ExceptionRecord == nullptr || pointers->ContextRecord == nullptr ||
                !IsFatal(pointers->ExceptionRecord->ExceptionCode) || g_reported >= kMaxReports)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            ++g_reported;

            const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
            const CONTEXT& context = *pointers->ContextRecord;

            char where[MAX_PATH + 512];
            if (!Describe(record.ExceptionAddress, where, sizeof(where)))
            {
                strcpy_s(where, "outside every loaded module");
            }

            Log::WriteSeparator();
            FORGE_LOG_ERROR("A %s at 0x%08X  %s", CodeName(record.ExceptionCode),
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(record.ExceptionAddress)), where);

            if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
            {
                const ULONG_PTR operation = record.ExceptionInformation[0];
                FORGE_LOG_ERROR("  it tried to %s 0x%08X", operation == 0 ? "read" : (operation == 1 ? "write" : "execute"),
                    static_cast<unsigned>(record.ExceptionInformation[1]));
            }

            FORGE_LOG_ERROR("  eax=%08X ebx=%08X ecx=%08X edx=%08X", context.Eax, context.Ebx, context.Ecx, context.Edx);
            FORGE_LOG_ERROR("  esi=%08X edi=%08X esp=%08X ebp=%08X", context.Esi, context.Edi, context.Esp, context.Ebp);

            WriteStack(context);
            Log::WriteSeparator();

            // Passes the exception on.
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }

    bool Crash::Install()
    {
        if (g_handle != nullptr)
        {
            return true;
        }

        // Installs a first-priority vectored handler.
        g_handle = AddVectoredExceptionHandler(1, &OnException);
        return g_handle != nullptr;
    }

    void Crash::Uninstall()
    {
        if (g_handle != nullptr)
        {
            RemoveVectoredExceptionHandler(g_handle);
            g_handle = nullptr;
        }

        if (g_symbolsReady)
        {
            SymCleanup(GetCurrentProcess());
        }

        g_symbolsTried = false;
        g_symbolsReady = false;
    }

    bool Crash::Guarded(void(__cdecl* call)(void* context), void* context)
    {
        // No C++ objects in here: __try cannot share a function with anything that unwinds.
        __try
        {
            call(context);
            return true;
        }
        __except (GuardFilter(GetExceptionCode()))
        {
            return false;
        }
    }
}