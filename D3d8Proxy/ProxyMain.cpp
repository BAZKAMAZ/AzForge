#include <windows.h>
#include <wchar.h>

#define FORGE_D3D8_EXPORTS(X) \
    X(Direct3D8EnableMaximizedWindowedModeShim, 1) \
    X(ValidatePixelShader, 2) \
    X(ValidateVertexShader, 3) \
    X(DebugSetMute, 4) \
    X(Direct3DCreate8, 5)

#define FORGE_PROXY_DECLARE_SLOT(name, ordinal) static FARPROC s_##name = nullptr;
FORGE_D3D8_EXPORTS(FORGE_PROXY_DECLARE_SLOT)
#undef FORGE_PROXY_DECLARE_SLOT

static void ReportMissingExport()
{
    MessageBoxW(nullptr,
        L"A module asked d3d8.dll for a function the system library does not provide.\n\n"
        L"Remove d3d8.dll from the game folder to run the game unmodified.",
        L"AzForge", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    ExitProcess(1);
}

#define FORGE_D3D8_FORWARDS(X) \
    X(Direct3D8EnableMaximizedWindowedModeShim, 1) \
    X(ValidatePixelShader, 2) \
    X(ValidateVertexShader, 3) \
    X(DebugSetMute, 4)

#define FORGE_PROXY_DEFINE_STUB(name, ordinal) \
    extern "C" __declspec(naked) void ProxyStub_##name() { __asm jmp dword ptr [s_##name] }
FORGE_D3D8_FORWARDS(FORGE_PROXY_DEFINE_STUB)
#undef FORGE_PROXY_DEFINE_STUB

#define FORGE_PROXY_EXPORT_STUB(name, ordinal) __pragma(comment(linker, "/EXPORT:" #name "=_ProxyStub_" #name ",@" #ordinal))
FORGE_D3D8_FORWARDS(FORGE_PROXY_EXPORT_STUB)
#undef FORGE_PROXY_EXPORT_STUB

#pragma comment(linker, "/EXPORT:Direct3DCreate8=_ProxyDirect3DCreate8@4,@5")

static HMODULE s_systemD3d8 = nullptr;
static HMODULE s_forge = nullptr;
static HMODULE s_self = nullptr;
static volatile LONG s_bootClaimed = 0;

static void ReportFatal(const wchar_t* message, DWORD error)
{
    wchar_t full[1024] = {};
    _snwprintf_s(full, _TRUNCATE, L"%s\n\nWindows error %lu.", message, error);

    wchar_t path[MAX_PATH + 32] = {};
    const DWORD copied = GetModuleFileNameW(s_self, path, MAX_PATH);

    if (copied != 0 && copied < MAX_PATH)
    {
        wchar_t* const separator = wcsrchr(path, L'\\');
        if (separator != nullptr)
        {
            separator[1] = L'\0';
            if (wcscat_s(path, L"forge-boot-failed.txt") == 0)
            {
                const HANDLE note = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

                if (note != INVALID_HANDLE_VALUE)
                {
                    const unsigned char bom[2] = { 0xFF, 0xFE };
                    DWORD written = 0;
                    WriteFile(note, bom, sizeof(bom), &written, nullptr);
                    WriteFile(note, full, static_cast<DWORD>(wcslen(full) * sizeof(wchar_t)), &written, nullptr);
                    CloseHandle(note);
                }
            }
        }
    }

    MessageBoxW(nullptr, full, L"AzForge", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

static bool ResolveSystemD3d8()
{
    wchar_t path[MAX_PATH + 16];
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);

    if (length == 0 || length >= MAX_PATH || wcscat_s(path, L"\\d3d8.dll") != 0)
    {
        return false;
    }

    s_systemD3d8 = LoadLibraryW(path);
    if (s_systemD3d8 == nullptr)
    {
        return false;
    }

#define FORGE_PROXY_RESOLVE_SLOT(name, ordinal) \
    s_##name = GetProcAddress(s_systemD3d8, #name); \
    if (s_##name == nullptr) s_##name = reinterpret_cast<FARPROC>(&ReportMissingExport);
    FORGE_D3D8_EXPORTS(FORGE_PROXY_RESOLVE_SLOT)
#undef FORGE_PROXY_RESOLVE_SLOT

        return s_Direct3DCreate8 != reinterpret_cast<FARPROC>(&ReportMissingExport);
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

extern "C" void __cdecl EnsureForgeBooted(void)
{
    if (InterlockedCompareExchange(&s_bootClaimed, 1, 0) != 0)
    {
        return;
    }

    if (!LoadForge(s_self))
    {
        const DWORD why = GetLastError();
        ReportFatal(L"AzForge could not load AzForge.dll from the game folder.\n\n"
            L"Make sure AzForge.dll sits next to game.exe, or remove d3d8.dll from "
            L"the game folder to run the game unmodified.", why);
    }
}

typedef void* (WINAPI* Direct3DCreate8Routine)(UINT);
typedef void* (__cdecl* HookD3d8Routine)(void*);

// Hooks the IDirect3D8 object before passing it to the game.
extern "C" void* WINAPI ProxyDirect3DCreate8(UINT sdkVersion)
{
    EnsureForgeBooted();

    void* const d3d8 = reinterpret_cast<Direct3DCreate8Routine>(s_Direct3DCreate8)(sdkVersion);
    if (d3d8 == nullptr || s_forge == nullptr)
    {
        return d3d8;
    }

    const HookD3d8Routine hook = reinterpret_cast<HookD3d8Routine>(GetProcAddress(s_forge, "AzForge_HookD3d8"));
    return hook != nullptr ? hook(d3d8) : d3d8;
}

typedef UINT(WINAPI* TimeBeginPeriodRoutine)(UINT);
static TimeBeginPeriodRoutine s_timeBeginPeriod = nullptr;

// Boots AzForge from the first winmm call.
static UINT WINAPI ProxyTimeBeginPeriod(UINT period)
{
    EnsureForgeBooted();
    return s_timeBeginPeriod(period);
}

static void** FindImportSlot(HMODULE module, FARPROC target)
{
    char* const base = reinterpret_cast<char*>(module);
    if (base == nullptr)
    {
        return nullptr;
    }

    const IMAGE_DOS_HEADER* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return nullptr;
    }

    const IMAGE_NT_HEADERS32* const headers = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (headers->Signature != IMAGE_NT_SIGNATURE)
    {
        return nullptr;
    }

    const IMAGE_DATA_DIRECTORY& imports = headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.VirtualAddress == 0)
    {
        return nullptr;
    }

    const IMAGE_IMPORT_DESCRIPTOR* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);

    for (; descriptor->FirstThunk != 0; ++descriptor)
    {
        void** slot = reinterpret_cast<void**>(base + descriptor->FirstThunk);

        for (; *slot != nullptr; ++slot)
        {
            if (*slot == reinterpret_cast<void*>(target))
            {
                return slot;
            }
        }
    }

    return nullptr;
}

static bool InstallBootTrigger()
{
    const HMODULE winmm = GetModuleHandleW(L"winmm.dll");
    if (winmm == nullptr)
    {
        return false;
    }

    s_timeBeginPeriod = reinterpret_cast<TimeBeginPeriodRoutine>(GetProcAddress(winmm, "timeBeginPeriod"));
    if (s_timeBeginPeriod == nullptr)
    {
        return false;
    }

    void** const slot = FindImportSlot(GetModuleHandleW(nullptr), reinterpret_cast<FARPROC>(s_timeBeginPeriod));
    if (slot == nullptr)
    {
        return false;
    }

    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous))
    {
        return false;
    }

    *slot = reinterpret_cast<void*>(&ProxyTimeBeginPeriod);
    VirtualProtect(slot, sizeof(void*), previous, &previous);
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
    s_self = hModule;

    if (!ResolveSystemD3d8())
    {
        ReportFatal(L"AzForge could not forward d3d8.dll to the system library.\n\n"
            L"The game cannot start with this proxy in place. Remove d3d8.dll from "
            L"the game folder to run the game unmodified.", GetLastError());
        return FALSE;
    }

    // Reports failure and continues without mods.
    if (!InstallBootTrigger())
    {
        ReportFatal(L"AzForge could not attach to the game's startup.\n\n"
            L"The game will run without mods.", GetLastError());
    }

    return TRUE;
}