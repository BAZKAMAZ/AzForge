#pragma once

#include <stdint.h>
#include <string>

namespace Forge
{
    class InlineHook
    {
    public:
        InlineHook() = default;
        ~InlineHook();

        InlineHook(const InlineHook&) = delete;
        InlineHook& operator=(const InlineHook&) = delete;

        bool                                                        Install(void* target, void* detour, const uint8_t* expectedPrologue, size_t prologueLength, std::string& error);
        bool                                                        IsInstalled() const;
        void                                                        Uninstall();
        void*                                                       Trampoline() const;

    private:
        static constexpr size_t kJumpLength = 5;
        static constexpr size_t kMaxPrologueLength = 16;

        uint8_t* m_target = nullptr;
        uint8_t* m_trampoline = nullptr;
        uint8_t m_original[kMaxPrologueLength] = {};
        size_t m_prologueLength = 0;

        bool m_installed = false;
    };
}
