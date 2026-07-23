#include "pch.h"

#include "src/Hook/InlineHook.h"

#include <stdio.h>
#include <string.h>

namespace Forge
{
    namespace
    {
        constexpr uint8_t kOpcodeJumpRelative = 0xE9;
        constexpr uint8_t kOpcodeNop = 0x90;

        void WriteRelativeJump(uint8_t* at, const void* destination)
        {
            const intptr_t relative = reinterpret_cast<intptr_t>(destination) -
                                      (reinterpret_cast<intptr_t>(at) + 5);

            at[0] = kOpcodeJumpRelative;
            memcpy(at + 1, &relative, sizeof(int32_t));
        }
    }

    InlineHook::~InlineHook()
    {
        Uninstall();
    }

    bool InlineHook::Install(void* target, void* detour, const uint8_t* expectedPrologue,
                             size_t prologueLength, std::string& error)
    {
        error.clear();

        if (m_installed)
        {
            error = "the hook is already installed";
            return false;
        }

        if (target == nullptr || detour == nullptr || expectedPrologue == nullptr)
        {
            error = "the hook was given a null target, detour or prologue";
            return false;
        }

        if (prologueLength < kJumpLength || prologueLength > kMaxPrologueLength)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "prologue length %zu is outside the supported range %zu..%zu",
                     prologueLength, kJumpLength, kMaxPrologueLength);
            error = buffer;
            return false;
        }

        uint8_t* const site = static_cast<uint8_t*>(target);

        if (memcmp(site, expectedPrologue, prologueLength) != 0)
        {
            error = "the bytes at the target do not match the reversed prologue";
            return false;
        }

        // The trampoline holds the stolen instructions plus a jump back into the original
        // body. It is allocated separately so the game's code section keeps its own
        // protection once the detour is written.
        m_trampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, kMaxPrologueLength + kJumpLength,
                                                          MEM_COMMIT | MEM_RESERVE,
                                                          PAGE_EXECUTE_READWRITE));
        if (m_trampoline == nullptr)
        {
            error = "cannot allocate the trampoline";
            return false;
        }

        memcpy(m_trampoline, site, prologueLength);
        WriteRelativeJump(m_trampoline + prologueLength, site + prologueLength);

        DWORD previousProtection = 0;
        if (!VirtualProtect(site, prologueLength, PAGE_EXECUTE_READWRITE, &previousProtection))
        {
            VirtualFree(m_trampoline, 0, MEM_RELEASE);
            m_trampoline = nullptr;
            error = "cannot make the target writable";
            return false;
        }

        memcpy(m_original, site, prologueLength);

        WriteRelativeJump(site, detour);
        for (size_t i = kJumpLength; i < prologueLength; ++i)
        {
            site[i] = kOpcodeNop;
        }

        DWORD restored = 0;
        VirtualProtect(site, prologueLength, previousProtection, &restored);
        FlushInstructionCache(GetCurrentProcess(), site, prologueLength);

        m_target = site;
        m_prologueLength = prologueLength;
        m_installed = true;
        return true;
    }

    void InlineHook::Uninstall()
    {
        if (!m_installed)
        {
            return;
        }

        DWORD previousProtection = 0;
        if (VirtualProtect(m_target, m_prologueLength, PAGE_EXECUTE_READWRITE, &previousProtection))
        {
            memcpy(m_target, m_original, m_prologueLength);

            DWORD restored = 0;
            VirtualProtect(m_target, m_prologueLength, previousProtection, &restored);
            FlushInstructionCache(GetCurrentProcess(), m_target, m_prologueLength);
        }

        if (m_trampoline != nullptr)
        {
            VirtualFree(m_trampoline, 0, MEM_RELEASE);
            m_trampoline = nullptr;
        }

        m_target = nullptr;
        m_prologueLength = 0;
        m_installed = false;
    }

    bool InlineHook::IsInstalled() const
    {
        return m_installed;
    }

    void* InlineHook::Trampoline() const
    {
        return m_trampoline;
    }
}
