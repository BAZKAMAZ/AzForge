#ifndef AZFORGE_INTEROP_API_H
#define AZFORGE_INTEROP_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AmodFindResult
{
    AMOD_FIND_OK = 0,
    AMOD_FIND_MALFORMED = 1,    /* pattern is not a byte pattern */
    AMOD_FIND_NOT_FOUND = 2,    /* no match */
    AMOD_FIND_AMBIGUOUS = 3     /* more than one match */
} AmodFindResult;

typedef struct AmodInteropApi
{
    uint32_t structSize;

    uintptr_t (__cdecl* ModuleBase)(AmodMod mod);
    uintptr_t (__cdecl* ModuleSize)(AmodMod mod);

    /* Byte pattern search, e.g. "56 8B 74 24 08 E8 ?? ?? ?? ??". Refuses 0 or 2+ hits. */
    AmodFindResult (__cdecl* Find)(AmodMod mod, const char* pattern, uintptr_t* address);

    /* Read/write mapped memory. Non-zero on success. */
    int (__cdecl* Read)(AmodMod mod, uintptr_t address, void* buffer, uint32_t size);
    int (__cdecl* Write)(AmodMod mod, uintptr_t address, const void* data, uint32_t size);

    /* Detour. prologueLength ≥ 5, whole instructions only. Returns token or AMOD_INVALID_TOKEN. */
    AmodToken (__cdecl* Hook)(AmodMod mod, uintptr_t target, void* detour,
                              const void* expectedPrologue, uint32_t prologueLength,
                              void** trampoline);
    void (__cdecl* Unhook)(AmodMod mod, AmodToken token);
} AmodInteropApi;

AMOD_LAST_FIELD(AmodInteropApi, Unhook, 0x1C);

#ifdef __cplusplus
}
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

        /* 0 when pattern names no single routine; pass why for the reason. */
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
