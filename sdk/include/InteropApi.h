/*
 * InteropApi.h - reaching the game directly, the way the loader does.
 *
 * Every other subsystem here is a typed view of something AzForge already understands. This
 * one is the way out of that: it hands a mod the three tools the loader is built on - finding
 * a routine by a byte signature, detouring it, and reading or writing the memory around it -
 * so a mod can add a capability the loader has never heard of without the loader being changed
 * and shipped again.
 *
 * That is the point. A typed interface can only ever cover what has already been reversed, and
 * a mod author who needs something else should not have to wait for a new AzForge.dll.
 *
 * What the loader still guarantees:
 *   - A signature that matches nowhere, or in more than one place, is refused rather than
 *     guessed at. An ambiguous signature is a defect in the signature, and binding to the
 *     first hit would silently bind to the wrong routine.
 *   - A hook states the bytes it expects to find and is refused when they differ, so a hook
 *     written against one build cannot be applied to another.
 *   - A read or a write is checked against what is actually mapped, so a stray address is an
 *     error rather than an access violation inside the game.
 *   - Every hook belongs to the mod that made it and is removed when that mod is unloaded,
 *     including when a player switches it off.
 *
 * What it does not guarantee: that what a mod does with this is correct. A detour with the
 * wrong calling convention, or a write to a field that means something else, will break the
 * game. Addresses are the mod's own responsibility - AmodApi::GetGameBuild tells it which
 * layout it is looking at, and a mod using raw addresses should check it.
 *
 * Reached through AmodApi::interop, null on a loader older than API version 18.
 */

#ifndef AZFORGE_INTEROP_API_H
#define AZFORGE_INTEROP_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How a search ended. Anything other than AMOD_FIND_OK leaves the address untouched. */
typedef enum AmodFindResult
{
    AMOD_FIND_OK = 0,
    AMOD_FIND_MALFORMED = 1,    /* the pattern is not a byte pattern */
    AMOD_FIND_NOT_FOUND = 2,    /* no match: this is not the build the mod was written for */
    AMOD_FIND_AMBIGUOUS = 3     /* more than one match, so the pattern names nothing */
} AmodFindResult;

typedef struct AmodInteropApi
{
    uint32_t structSize;

    /* Where game.exe is mapped, and how far it runs. The base is the same value
     * AmodApi::GetGameModuleBase gives; the size is what makes a bounds check possible. */
    uintptr_t (__cdecl* ModuleBase)(AmodMod mod);
    uintptr_t (__cdecl* ModuleSize)(AmodMod mod);

    /* Finds a byte pattern in the executable's code, written the way a disassembler prints
     * one: "56 8B 74 24 08 E8 ?? ?? ?? ??". Wildcard every byte that moves with the code -
     * the operand of a call or jump leaving the pattern, and any absolute address - and keep
     * short jumps literal, because they are relative and they carry the shape of the routine.
     *
     * The whole of the code is searched, so a pattern matching twice is refused: see
     * AmodFindResult. */
    AmodFindResult (__cdecl* Find)(AmodMod mod, const char* pattern, uintptr_t* address);

    /* Reads size bytes from the game into the buffer. Non-zero on success; zero when the
     * range is not mapped and readable, which is what turns a stray address into an answer
     * rather than a crash. The address may be anywhere - the module, the heap, the stack -
     * because the structures worth reading are not all in the image. */
    int (__cdecl* Read)(AmodMod mod, uintptr_t address, void* buffer, uint32_t size);

    /* Writes size bytes into the game, making the pages writable for the length of the write
     * and putting the protection back afterwards, so patching code works as well as data.
     * Non-zero on success. */
    int (__cdecl* Write)(AmodMod mod, uintptr_t address, const void* data, uint32_t size);

    /* Detours a routine. expectedPrologue is what the first prologueLength bytes must contain
     * and the hook is refused when they do not, which is what stops a hook meant for one build
     * from being written into another. prologueLength must be at least 5 - the length of the
     * jump - and must cover whole instructions, none of which may be position-dependent or a
     * target of a jump from inside the routine; the mod states this because only the mod knows
     * what it reversed.
     *
     * trampoline receives an entry point that runs the original prologue and continues into
     * the rest of the routine, which is how a detour calls the routine it replaced. It stays
     * valid until the hook is removed.
     *
     * Returns the token that owns the hook, or AMOD_INVALID_TOKEN. The hook is removed when
     * the token is released or when the mod is unloaded, whichever comes first. */
    AmodToken (__cdecl* Hook)(AmodMod mod, uintptr_t target, void* detour,
                              const void* expectedPrologue, uint32_t prologueLength,
                              void** trampoline);

    /* Puts the original bytes back. Doing nothing is also fine: an unreleased hook goes when
     * the mod does. */
    void (__cdecl* Unhook)(AmodMod mod, AmodToken token);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodInteropApi;

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Interop
    {
        inline const AmodInteropApi* g_interop = nullptr;
        inline AmodMod g_self = nullptr;

        inline void                                                 Attach(const AmodInteropApi* api, AmodMod self)
        {
            g_interop = api;
            g_self = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_interop != nullptr;
        }

        inline uintptr_t                                            ModuleBase()
        {
            return g_interop != nullptr ? g_interop->ModuleBase(g_self) : 0;
        }

        inline uintptr_t                                            ModuleSize()
        {
            return g_interop != nullptr ? g_interop->ModuleSize(g_self) : 0;
        }

        /* Zero when the pattern names no single routine; pass a result pointer to learn why. */
        inline uintptr_t                                            Find(const char* pattern, AmodFindResult* why = nullptr)
        {
            uintptr_t address = 0;
            const AmodFindResult result = g_interop != nullptr
                                              ? g_interop->Find(g_self, pattern, &address)
                                              : AMOD_FIND_NOT_FOUND;
            if (why != nullptr)
            {
                *why = result;
            }
            return result == AMOD_FIND_OK ? address : 0;
        }

        inline bool                                                 Read(uintptr_t address, void* buffer, uint32_t size)
        {
            return g_interop != nullptr && g_interop->Read(g_self, address, buffer, size) != 0;
        }

        inline bool                                                 Write(uintptr_t address, const void* data, uint32_t size)
        {
            return g_interop != nullptr && g_interop->Write(g_self, address, data, size) != 0;
        }

        /* Typed read of one value, for walking a structure a field at a time. */
        template <typename T>
        inline bool                                                 ReadValue(uintptr_t address, T& out)
        {
            return Read(address, &out, static_cast<uint32_t>(sizeof(T)));
        }

        template <typename T>
        inline bool                                                 WriteValue(uintptr_t address, const T& value)
        {
            return Write(address, &value, static_cast<uint32_t>(sizeof(T)));
        }

        inline AmodToken                                            Hook(uintptr_t target, void* detour,
                                                                         const void* expectedPrologue,
                                                                         uint32_t prologueLength,
                                                                         void** trampoline)
        {
            return g_interop != nullptr
                       ? g_interop->Hook(g_self, target, detour, expectedPrologue, prologueLength,
                                         trampoline)
                       : AMOD_INVALID_TOKEN;
        }

        inline void                                                 Unhook(AmodToken token)
        {
            if (g_interop != nullptr)
            {
                g_interop->Unhook(g_self, token);
            }
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_INTEROP_API_H */
