// winmm.dll proxy for Azangara.
//
// Placing this module next to game.exe takes over the winmm.dll name for the whole
// process, not just for the executable, so every export of the system library has to be
// re-exported here. See WinmmExports.h for why the seven imports of game.exe are not
// enough.
//
// The forwarding stubs are naked tail jumps, so the arguments the caller pushed and the
// __stdcall stack cleanup performed by the real function both stay untouched regardless of
// each function's signature. This is 32-bit only by construction, which matches the game.

#include <windows.h>
#include <wchar.h>

#include "WinmmExports.h"

// One slot per export, filled from the system library during process attach.
#define FORGE_PROXY_DECLARE_SLOT(name) static FARPROC s_##name = nullptr;
FORGE_WINMM_EXPORTS(FORGE_PROXY_DECLARE_SLOT)
#undef FORGE_PROXY_DECLARE_SLOT

// Reached only when the system library does not provide an export this proxy advertises.
// Terminating here is deliberate: the caller expects a working function and there is
// nothing sensible to return in its place.
static void ReportMissingExport()
{
    MessageBoxW(nullptr,
                L"A module asked winmm.dll for a function the system library does not provide.\n\n"
                L"Remove winmm.dll from the game folder to run the game unmodified.",
                L"AzForge", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    ExitProcess(1);
}

#define FORGE_PROXY_DEFINE_STUB(name) \
    extern "C" __declspec(naked) void ProxyStub_##name() { __asm { jmp dword ptr [s_##name] } }
FORGE_WINMM_EXPORTS(FORGE_PROXY_DEFINE_STUB)
#undef FORGE_PROXY_DEFINE_STUB

#define FORGE_PROXY_EXPORT_STUB(name) \
    __pragma(comment(linker, "/EXPORT:" #name "=_ProxyStub_" #name))
FORGE_WINMM_EXPORTS(FORGE_PROXY_EXPORT_STUB)
#undef FORGE_PROXY_EXPORT_STUB

static HMODULE s_systemWinmm = nullptr;
static HMODULE s_forge = nullptr;

static void ReportFatal(const wchar_t* message)
{
    MessageBoxW(nullptr, message, L"AzForge", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

static bool ResolveSystemWinmm()
{
    wchar_t path[MAX_PATH + 16];
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return false;
    }

    if (wcscat_s(path, L"\\winmm.dll") != 0)
    {
        return false;
    }

    s_systemWinmm = LoadLibraryW(path);
    if (s_systemWinmm == nullptr)
    {
        return false;
    }

    // An export missing from the system library is bound to the reporting stub rather than
    // left null, so calling it names the problem instead of jumping to address zero.
    // Windows versions differ in the tail of this table; only a module that actually calls
    // such a function is affected.
#define FORGE_PROXY_RESOLVE_SLOT(name)                                          \
    s_##name = GetProcAddress(s_systemWinmm, #name);                            \
    if (s_##name == nullptr)                                                    \
    {                                                                           \
        s_##name = reinterpret_cast<FARPROC>(&ReportMissingExport);             \
    }
    FORGE_WINMM_EXPORTS(FORGE_PROXY_RESOLVE_SLOT)
#undef FORGE_PROXY_RESOLVE_SLOT

    // The seven functions game.exe imports must be present on any supported system.
    return s_joyGetDevCapsA != reinterpret_cast<FARPROC>(&ReportMissingExport) &&
           s_joyGetNumDevs != reinterpret_cast<FARPROC>(&ReportMissingExport) &&
           s_joyGetPos != reinterpret_cast<FARPROC>(&ReportMissingExport) &&
           s_joyReleaseCapture != reinterpret_cast<FARPROC>(&ReportMissingExport) &&
           s_joySetCapture != reinterpret_cast<FARPROC>(&ReportMissingExport) &&
           s_timeBeginPeriod != reinterpret_cast<FARPROC>(&ReportMissingExport) &&
           s_timeGetTime != reinterpret_cast<FARPROC>(&ReportMissingExport);
}

static bool LoadForge(HMODULE self)
{
    wchar_t path[MAX_PATH + 32];
    const DWORD copied = GetModuleFileNameW(self, path, MAX_PATH);
    if (copied == 0 || copied >= MAX_PATH)
    {
        return false;
    }

    wchar_t* separator = wcsrchr(path, L'\\');
    if (separator == nullptr)
    {
        return false;
    }
    separator[1] = L'\0';

    if (wcscat_s(path, L"AzForge.dll") != 0)
    {
        return false;
    }

    s_forge = LoadLibraryW(path);
    if (s_forge == nullptr)
    {
        return false;
    }

    typedef void(__cdecl* BootRoutine)();
    const BootRoutine boot = reinterpret_cast<BootRoutine>(GetProcAddress(s_forge, "AzForge_Boot"));
    if (boot == nullptr)
    {
        return false;
    }

    boot();
    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(lpReserved);

    if (ul_reason_for_call != DLL_PROCESS_ATTACH)
    {
        return TRUE;
    }

    DisableThreadLibraryCalls(hModule);

    if (!ResolveSystemWinmm())
    {
        ReportFatal(L"AzForge could not forward winmm.dll to the system library.\n\n"
                    L"The game cannot start with this proxy in place. Remove winmm.dll from "
                    L"the game folder to run the game unmodified.");
        return FALSE;
    }

    if (!LoadForge(hModule))
    {
        ReportFatal(L"AzForge could not load AzForge.dll from the game folder.\n\n"
                    L"Make sure AzForge.dll sits next to game.exe, or remove winmm.dll from "
                    L"the game folder to run the game unmodified.");
        return FALSE;
    }

    return TRUE;
}
