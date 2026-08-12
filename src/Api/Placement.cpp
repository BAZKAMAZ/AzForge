#include <windows.h>

#include "src/Api/Placement.h"
#include "src/Api/ApiRead.h"
#include "src/Api/FrameApi.h"
#include "src/Api/ModContext.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"

#include <stddef.h>
#include <string.h>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;

        // Past the range the game's own dispatcher branches on, so a mod kind can never be mistaken for one of its own
        constexpr int kFirstModKind = 100;

        struct Registered
        {
            ModContext* owner;
            int kind;
            std::vector<char> name;
            Placement::BuildRoutine build;
            Placement::ClaimRoutine claim;
            void* user;
        };

        std::vector<Registered> g_kinds;
        int g_nextKind = kFirstModKind;

        struct Borrowed
        {
            const void* record;
            int kind;
        };

        std::vector<Borrowed> g_borrowed;

        Placement::SpawnRoutine g_gameSpawn = nullptr;

        const Registered* KindOf(int kind)
        {
            for (const Registered& known : g_kinds)
            {
                if (known.kind == kind)
                {
                    return &known;
                }
            }
            return nullptr;
        }

        int KindAtHead(const void* record)
        {
            return (record != nullptr && Readable(record, sizeof(int)))
                ? Read<int>(static_cast<const uint8_t*>(record), GameAddress::kSpawnKindOffset)
                : 0;
        }

        void __cdecl SpawnThunk(void* room, void* record)
        {
            const int kind = KindAtHead(record);
            const Registered* const mine = KindOf(kind);

            if (mine != nullptr)
            {
                if (mine->build != nullptr)
                {
                    mine->build(room, record, kind, mine->user);
                }
                return;
            }

            if (g_gameSpawn != nullptr)
            {
                g_gameSpawn(room, record);
            }

            const int borrowed = Placement::BorrowedKind(record);
            const Registered* const claimer = borrowed != 0 ? KindOf(borrowed) : nullptr;

            if (claimer != nullptr && claimer->claim != nullptr)
            {
                claimer->claim(room, record, borrowed, claimer->user);
            }
        }
    }

    bool Placement::Install()
    {
        if (GameModule::Resolve(GameAddress::kSpawnPlacementSlot) == nullptr)
        {
            FORGE_LOG_ERROR("The placement slot is not where it was reversed from");
            return false;
        }

        static bool ticking = false;

        HoldSlot();
        if (!ticking)
        {
            FrameApi::AddInternalTick(&Placement::HoldSlot);
            ticking = true;
        }
        return true;
    }

    void Placement::HoldSlot()
    {
        void** const slot = static_cast<void**>(GameModule::Resolve(GameAddress::kSpawnPlacementSlot));
        if (slot == nullptr || *slot == reinterpret_cast<void*>(&SpawnThunk))
        {
            return;
        }

        if (*slot != nullptr)
        {
            g_gameSpawn = reinterpret_cast<SpawnRoutine>(*slot);
        }
        *slot = reinterpret_cast<void*>(&SpawnThunk);
    }

    Placement::SpawnRoutine Placement::Spawner()
    {
        HoldSlot();
        return g_gameSpawn;
    }

    int Placement::RegisterKind(ModContext* owner, const char* name, BuildRoutine build,
        ClaimRoutine claim, void* user)
    {
        if (owner == nullptr || name == nullptr || name[0] == '\0' || build == nullptr)
        {
            return 0;
        }
        if (KindByName(name) != 0)
        {
            return 0;
        }

        Registered mine = {};
        mine.owner = owner;
        mine.kind = g_nextKind++;
        mine.name.assign(name, name + strlen(name) + 1);
        mine.build = build;
        mine.claim = claim;
        mine.user = user;

        g_kinds.push_back(mine);
        HoldSlot();

        return mine.kind;
    }

    int Placement::KindByName(const char* name)
    {
        if (name == nullptr)
        {
            return 0;
        }

        for (const Registered& known : g_kinds)
        {
            if (strcmp(known.name.data(), name) == 0)
            {
                return known.kind;
            }
        }
        return 0;
    }

    const char* Placement::NameOfKind(int kind)
    {
        const Registered* const known = KindOf(kind);
        return known != nullptr ? known->name.data() : "";
    }

    bool Placement::IsModKind(int kind)
    {
        return kind >= kFirstModKind;
    }

    void Placement::Borrow(const void* record, int kind)
    {
        if (record == nullptr)
        {
            return;
        }

        ForgetBorrowed(record);

        const Borrowed mine = { record, kind };
        g_borrowed.push_back(mine);
    }

    int Placement::BorrowedKind(const void* record)
    {
        for (const Borrowed& known : g_borrowed)
        {
            if (known.record == record)
            {
                return known.kind;
            }
        }
        return 0;
    }

    void Placement::ForgetBorrowed(const void* record)
    {
        for (auto it = g_borrowed.begin(); it != g_borrowed.end();)
        {
            it = it->record == record ? g_borrowed.erase(it) : it + 1;
        }
    }

    void Placement::Stand(void* room, void* record)
    {
        SpawnThunk(room, record);
    }

    void Placement::ForgetMod(ModContext* owner)
    {
        for (auto it = g_kinds.begin(); it != g_kinds.end();)
        {
            it = it->owner == owner ? g_kinds.erase(it) : it + 1;
        }
    }

    void Placement::Uninstall()
    {
        void** const slot = static_cast<void**>(GameModule::Resolve(GameAddress::kSpawnPlacementSlot));
        if (slot != nullptr && *slot == reinterpret_cast<void*>(&SpawnThunk) && g_gameSpawn != nullptr)
        {
            *slot = reinterpret_cast<void*>(g_gameSpawn);
        }

        g_gameSpawn = nullptr;
        g_kinds.clear();
        g_borrowed.clear();
    }
}
