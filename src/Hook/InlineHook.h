#pragma once

#include <stdint.h>
#include <string>

namespace Forge
{
    // Five-byte inline detour with a trampoline back into the original code.
    //
    // The stolen bytes are not disassembled at run time. The caller states how many bytes
    // it is taking and what they must contain; the opening instructions of every target are
    // known from the reverse of game.exe, so the caller is in a position to guarantee that
    // the range covers whole, position-independent instructions. Installing refuses when
    // the bytes present differ from the ones stated, which is what stops the hook from
    // being applied to a build it was not reversed against.
    class InlineHook
    {
    public:
        InlineHook() = default;
        ~InlineHook();

        InlineHook(const InlineHook&) = delete;
        InlineHook& operator=(const InlineHook&) = delete;

        bool                                                        Install(void* target, void* detour, const uint8_t* expectedPrologue, size_t prologueLength, std::string& error);
        void                                                        Uninstall();
        bool                                                        IsInstalled() const;

        // Entry point that runs the original prologue and jumps back past the detour.
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
