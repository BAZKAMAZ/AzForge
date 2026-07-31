#include <windows.h>

#include "src/Mods/ModScan.h"

#include "src/Core/ForgeLog.h"
#include "src/Core/ForgeText.h"

namespace Forge
{
    namespace
    {
        typedef struct HAMSICONTEXT__* ForgeAmsiContext;
        typedef struct HAMSISESSION__* ForgeAmsiSession;

        // AMSI_RESULT. Anything from 32768 up is malware; the values below it run from "clean" through "not detected" and are all acceptable.
        constexpr int kAmsiResultMalwareThreshold = 32768;

        typedef HRESULT(WINAPI* AmsiInitializeRoutine)(LPCWSTR appName, ForgeAmsiContext* context);
        typedef void(WINAPI* AmsiUninitializeRoutine)(ForgeAmsiContext context);
        typedef HRESULT(WINAPI* AmsiScanBufferRoutine)(ForgeAmsiContext context, void* buffer, ULONG length, LPCWSTR contentName, ForgeAmsiSession session, int* result);

        HMODULE g_amsi = nullptr;
        ForgeAmsiContext g_context = nullptr;
        AmsiScanBufferRoutine g_scan = nullptr;
        AmsiUninitializeRoutine g_uninitialize = nullptr;
    }

    void ModScan::Initialize()
    {
        if (g_amsi != nullptr)
        {
            return;
        }

        g_amsi = LoadLibraryW(L"amsi.dll");
        if (g_amsi == nullptr)
        {
            FORGE_LOG_INFO("AMSI is not available on this system, so mods are loaded unscanned");
            return;
        }

        const AmsiInitializeRoutine initialize =
            reinterpret_cast<AmsiInitializeRoutine>(GetProcAddress(g_amsi, "AmsiInitialize"));
        g_scan = reinterpret_cast<AmsiScanBufferRoutine>(GetProcAddress(g_amsi, "AmsiScanBuffer"));
        g_uninitialize =
            reinterpret_cast<AmsiUninitializeRoutine>(GetProcAddress(g_amsi, "AmsiUninitialize"));

        if (initialize == nullptr || g_scan == nullptr || g_uninitialize == nullptr ||
            FAILED(initialize(L"AzForge", &g_context)))
        {
            FORGE_LOG_WARN("AMSI is present but would not start, so mods are loaded unscanned");
            Shutdown();
            return;
        }

        FORGE_LOG_INFO("Mods will be scanned through AMSI before they are loaded");
    }

    void ModScan::Shutdown()
    {
        if (g_context != nullptr && g_uninitialize != nullptr)
        {
            g_uninitialize(g_context);
        }

        g_context = nullptr;
        g_scan = nullptr;
        g_uninitialize = nullptr;

        if (g_amsi != nullptr)
        {
            FreeLibrary(g_amsi);
            g_amsi = nullptr;
        }
    }

    bool ModScan::IsClean(const std::string& modId, const std::vector<uint8_t>& payload, std::string& reason)
    {
        reason.clear();

        if (g_scan == nullptr || g_context == nullptr || payload.empty())
        {
            return true;
        }

        const std::wstring name = FromUtf8(modId);

        int result = 0;
        const HRESULT scanned = g_scan(g_context, const_cast<uint8_t*>(payload.data()), static_cast<ULONG>(payload.size()), name.c_str(), nullptr, &result);

        // A scan that could not run is not a detection.
        if (FAILED(scanned))
        {
            FORGE_LOG_WARN("The scan of '%s' did not complete (0x%08X), so it is loaded "
                           "unscanned", modId.c_str(), static_cast<unsigned>(scanned));
            return true;
        }

        if (result >= kAmsiResultMalwareThreshold)
        {
            reason = "the installed antivirus identified the mod's DLL as malware";
            return false;
        }

        return true;
    }
}
