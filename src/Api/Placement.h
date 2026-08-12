#pragma once

#include <stdint.h>

namespace Forge
{
    struct ModContext;

    class Placement
    {
    public:
        // Called when a mod-registered kind is stood up.
        typedef void (*BuildRoutine)(void* room, void* record, int kind, void* user);

        // Called when the game stands up a record a mod claimed.
        typedef void (*ClaimRoutine)(void* room, const void* record, int kind, void* user);

        static bool                                                 Install();
        static void                                                 Uninstall();
        static void                                                 ForgetMod(ModContext* owner);

        // Re-hold the slot; game rebuilds its table each tick.
        static void                                                 HoldSlot();

        // Game's own spawn routine.
        typedef void(__cdecl* SpawnRoutine)(void* room, void* record);
        static SpawnRoutine                                         Spawner();

        // Register a mod kind; 0 if name taken or no room left.
        static int                                                  RegisterKind(ModContext* owner, const char* name, BuildRoutine build, ClaimRoutine claim, void* user);

        static int                                                  KindByName(const char* name);
        static const char* NameOfKind(int kind);
        static bool                                                 IsModKind(int kind);

        // Claim a game record so ClaimRoutine is told when it is stood up.
        static void                                                 Borrow(const void* record, int kind);
        static int                                                  BorrowedKind(const void* record);
        static void                                                 ForgetBorrowed(const void* record);

        static void                                                 Stand(void* room, void* record);
    };
}