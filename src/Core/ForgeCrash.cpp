#include "pch.h"

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
        constexpr size_t kStackWords = 256;     // how far up the stack the scan looks
        constexpr int kStackFrames = 16;        // how many return addresses it prints

        // Only the codes that mean the process is finished. A C++ throw (0xE06D7363) is not
        // one of them: the game throws on a missing script and handles it.
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
            case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
            case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
            case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
            case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
            case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
            default:                              return "exception";
            }
        }

        // "game.exe+0x0004cab0", which is the form a reverse address is looked up by.
        bool Describe(const void* address, char* buffer, size_t size)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    static_cast<LPCSTR>(address), &module) ||
                module == nullptr)
            {
                return false;
            }

            char path[MAX_PATH] = {};
            GetModuleFileNameA(module, path, sizeof(path));
            const char* const slash = strrchr(path, '\\');

            sprintf_s(buffer, size, "%s+0x%08X", slash != nullptr ? slash + 1 : path,
                      static_cast<unsigned>(reinterpret_cast<uintptr_t>(address) -
                                            reinterpret_cast<uintptr_t>(module)));
            return true;
        }

        bool IsCode(const void* address)
        {
            MEMORY_BASIC_INFORMATION info = {};
            if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
                info.State != MEM_COMMIT)
            {
                return false;
            }

            const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                     PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            return (info.Protect & executable) != 0;
        }

        // No frame pointer is assumed. Everything on the stack that points into code is
        // printed instead: it carries returns that are no longer live, but the call chain is
        // in there, in order, which is what is wanted.
        void WriteStack(const CONTEXT& context)
        {
            // Clamped to the committed part of the stack, so the scan cannot fault its way
            // into a second report.
            MEMORY_BASIC_INFORMATION region = {};
            if (VirtualQuery(reinterpret_cast<const void*>(context.Esp), &region, sizeof(region))
                    != sizeof(region) ||
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

                char where[MAX_PATH + 32];
                if (candidate != nullptr && IsCode(candidate) && Describe(candidate, where, sizeof(where)))
                {
                    FORGE_LOG_ERROR("    called from 0x%08X  %s",
                                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(candidate)),
                                    where);
                    ++written;
                }
            }
        }

        LONG CALLBACK OnException(EXCEPTION_POINTERS* pointers)
        {
            if (pointers == nullptr || pointers->ExceptionRecord == nullptr ||
                pointers->ContextRecord == nullptr ||
                !IsFatal(pointers->ExceptionRecord->ExceptionCode) ||
                g_reported >= kMaxReports)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            ++g_reported;

            const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
            const CONTEXT& context = *pointers->ContextRecord;

            char where[MAX_PATH + 32];
            if (!Describe(record.ExceptionAddress, where, sizeof(where)))
            {
                strcpy_s(where, "outside every loaded module");
            }

            Log::WriteSeparator();
            FORGE_LOG_ERROR("A %s at 0x%08X  %s", CodeName(record.ExceptionCode),
                            static_cast<unsigned>(
                                reinterpret_cast<uintptr_t>(record.ExceptionAddress)), where);

            if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                record.NumberParameters >= 2)
            {
                const ULONG_PTR operation = record.ExceptionInformation[0];
                FORGE_LOG_ERROR("  it tried to %s 0x%08X",
                                operation == 0 ? "read" : (operation == 1 ? "write" : "execute"),
                                static_cast<unsigned>(record.ExceptionInformation[1]));
            }

            FORGE_LOG_ERROR("  eax=%08X ebx=%08X ecx=%08X edx=%08X",
                            context.Eax, context.Ebx, context.Ecx, context.Edx);
            FORGE_LOG_ERROR("  esi=%08X edi=%08X esp=%08X ebp=%08X",
                            context.Esi, context.Edi, context.Esp, context.Ebp);

            WriteStack(context);
            Log::WriteSeparator();

            // Handed on: this is a report, not a recovery. Whoever would have handled the
            // exception still does, and a crash still crashes.
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }

    bool Crash::Install()
    {
        if (g_handle != nullptr)
        {
            return true;
        }

        // A vectored handler rather than the unhandled filter: the game sets its own filter
        // after the loader is up, and this one is first in line whatever it does.
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
    }
}
