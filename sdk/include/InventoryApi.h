#ifndef AZFORGE_INVENTORY_API_H
#define AZFORGE_INVENTORY_API_H

#include "BonusesApi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starting slot count the game was built for. */
#define AMOD_INVENTORY_SLOTS                                        5

/* Max after the loader moves slots out of the player. */
#define AMOD_INVENTORY_SLOTS_MAX                                    32

/* One carried item. Empty name = free slot (same as the game). */
typedef struct AmodInventoryItem
{
    uint32_t structSize;

    const char* name;           /* bonus type name; "" if free */

    /* Timer left and total, seconds. Both 0 if it does not run down. */
    float timeLeft;
    float timeTotal;

    int isTotem;
    int isTorch;

    int pointsOnRunOut;         /* $pts_out if it expires rather than used */
    float burnTime;             /* torch only */

    /* $res_id of the type; distinguishes types that share a name. "" if free or type gone. */
    const char* resourceId;

    /* New fields are appended, never inserted. */
} AmodInventoryItem;

AMOD_LAST_FIELD(AmodInventoryItem, resourceId, 0x20);

/* Scoring chain */
typedef struct AmodBonusChain
{
    uint32_t structSize;

    int length;                 /* takes in a row */
    int points;                 /* scored so far */
    float timeLeft;             /* of the window; 0 starts a new chain */
    float window;               /* reset value on each take */

    /* New fields are appended, never inserted. */
} AmodBonusChain;

AMOD_LAST_FIELD(AmodBonusChain, window, 0x10);

/* Why a slot emptied. */
typedef enum AmodInventoryEvent
{
    AMOD_INVENTORY_EXPIRED = 0, /* timer ran out */
    AMOD_INVENTORY_USED = 1     /* left with time still on it */
} AmodInventoryEvent;

typedef struct AmodInventoryEventInfo
{
    uint32_t structSize;

    AmodInventoryEvent event;
    int slot;

    const char* name;
    const char* resourceId;

    float timeLeft;             /* remaining; separates the two events */
    int pointsOnRunOut;         /* type $pts_out */

    /* New fields are appended, never inserted. */
} AmodInventoryEventInfo;

AMOD_LAST_FIELD(AmodInventoryEventInfo, pointsOnRunOut, 0x18);

/* After the slot has emptied; reading it back shows free. */
typedef void(__cdecl* AmodInventoryHandler)(const AmodInventoryEventInfo* info, void* user);

typedef struct AmodChainEventInfo
{
    uint32_t structSize;

    int length;                 /* just reached; 0 when broken */
    int points;                 /* scored by then */
    int broke;                  /* 1 window ran out, 0 grew */

    /* New fields are appended, never inserted. */
} AmodChainEventInfo;

AMOD_LAST_FIELD(AmodChainEventInfo, broke, 0x0C);

/* On chain grow or break; no need to poll each frame. */
typedef void(__cdecl* AmodChainHandler)(const AmodChainEventInfo* info, void* user);

typedef struct AmodInventoryApi
{
    uint32_t structSize;

    /* false in menu / no player */
    int(__cdecl* IsAvailable)(AmodMod mod);

    /* One of AMOD_INVENTORY_SLOTS; nonzero if readable. */
    int(__cdecl* GetItem)(AmodMod mod, int slot, AmodInventoryItem* out);

    /* How many slots are in use. */
    int(__cdecl* GetUsedCount)(AmodMod mod);

    int(__cdecl* GetChain)(AmodMod mod, AmodBonusChain* out);

    /* Remaining bonuses by the three game counts. */
    int(__cdecl* GetBonusesLeft)(AmodMod mod);
    int(__cdecl* GetMegaBonusesLeft)(AmodMod mod);
    int(__cdecl* GetOtherBonusesLeft)(AmodMod mod);

    /* Slot holding that name, or -1. */
    int(__cdecl* FindSlot)(AmodMod mod, const char* name);

    /* First empty slot, or -1 if full. */
    int(__cdecl* FindFreeSlot)(AmodMod mod);

    int(__cdecl* PutItem)(AmodMod mod, int slot, const char* typeName, float time,
        AmodBonusTimer timer);

    /* Empty a slot the game way (use sound included). */
    int(__cdecl* ClearItem)(AmodMod mod, int slot);

    /* Move a carried item's timer. */
    int(__cdecl* SetTimeLeft)(AmodMod mod, int slot, float seconds);

    /* Slot icon only; type unchanged. Same kind may look different per slot. */
    int(__cdecl* SetIcon)(AmodMod mod, int slot, const char* file);

    int(__cdecl* GetItemProperty)(AmodMod mod, int slot, const char* key, AmodValue* value);
    int(__cdecl* SetItemProperty)(AmodMod mod, int slot, const char* key, const AmodValue* value);
    int(__cdecl* HasItemProperty)(AmodMod mod, int slot, const char* key);
    int(__cdecl* ItemPropertyCount)(AmodMod mod);
    const char* (__cdecl* ItemPropertyNameAt)(AmodMod mod, int index);

    int(__cdecl* GetSlotCount)(AmodMod mod);
    int(__cdecl* SetSlotCount)(AmodMod mod, int count);

    /* Move/swap whole slot records (keeps per-slot icon/timer). Move onto full fails; use Swap. */
    int(__cdecl* MoveItem)(AmodMod mod, int from, int to);
    int(__cdecl* SwapItems)(AmodMod mod, int first, int second);

    AmodToken(__cdecl* AddSlotHandler)(AmodMod mod, AmodInventoryHandler handler, void* user);
    void(__cdecl* RemoveSlotHandler)(AmodMod mod, AmodToken token);

    AmodToken(__cdecl* AddChainHandler)(AmodMod mod, AmodChainHandler handler, void* user);
    void(__cdecl* RemoveChainHandler)(AmodMod mod, AmodToken token);

    /* Whether the game counts this slot down. Off freezes the timer and keeps the ring. */
    int(__cdecl* SetTicking)(AmodMod mod, int slot, int ticking);
    int(__cdecl* IsTicking)(AmodMod mod, int slot);

    /* Fields added in later API versions go here, and nowhere else. */
    /* Every handler this subsystem hands a token for. The Remove* entry above it is the older
       spelling of the same call, kept for mods already built against it. */
    void(__cdecl* Cancel)(AmodMod mod, AmodToken token);

} AmodInventoryApi;

AMOD_LAST_FIELD(AmodInventoryApi, Cancel, 0x74);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Inventory
    {
        inline const AmodInventoryApi* g_inventory = nullptr;
        inline AmodMod g_self = nullptr;

        inline void                                                 Attach(const AmodInventoryApi* api, AmodMod self)
        {
            g_inventory = api;
            g_self = self;
        }

        /* Whether the interface is there. IsCarrying below answers whether there is a player. */
        inline bool                                                 IsAvailable()
        {
            return g_inventory != nullptr;
        }

        inline bool                                                 HasPlayer()
        {
            return g_inventory != nullptr && g_inventory->IsAvailable(g_self) != 0;
        }

        class Item
        {
        public:
            Item() { m_data = AmodInventoryItem{}; }
            explicit Item(const AmodInventoryItem& data) : m_data(data) {}

            bool                                                    IsEmpty() const { return Name()[0] == '\0'; }
            const char*                                             Name() const { return m_data.name != nullptr ? m_data.name : ""; }

            float                                                   GetTimeLeft() const { return m_data.timeLeft; }
            float                                                   GetTimeTotal() const { return m_data.timeTotal; }

            /* How much of its life is left, 0 to 1, for anything drawing a bar. One for an item * that does not run down at all. */
            float                                                   GetFraction() const
            {
                return m_data.timeTotal > 0.0f ? m_data.timeLeft / m_data.timeTotal : 1.0f;
            }

            bool                                                    IsTotem() const { return m_data.isTotem != 0; }
            bool                                                    IsTorch() const { return m_data.isTorch != 0; }

            int                                                     GetPointsOnRunOut() const { return m_data.pointsOnRunOut; }
            float                                                   GetBurnTime() const { return m_data.burnTime; }

            /* What every call about a bonus type asks for, so a carried item can be handed to one */
            const char*                                             ResourceId() const { return m_data.resourceId != nullptr ? m_data.resourceId : ""; }

            const AmodInventoryItem&                                Raw() const { return m_data; }

        private:
            AmodInventoryItem m_data;
        };

        inline Item                                                 At(int slot)
        {
            AmodInventoryItem data = {};
            data.structSize = sizeof(data);

            return (g_inventory != nullptr && g_inventory->GetItem(g_self, slot, &data) != 0)
                       ? Item(data)
                       : Item();
        }

        inline int                                                  UsedCount()
        {
            return g_inventory != nullptr ? g_inventory->GetUsedCount(g_self) : 0;
        }

        /* Whether one of the slots holds an item of that name. */
        inline bool                                                 IsCarrying(const char* name)
        {
            for (int slot = 0; slot < AMOD_INVENTORY_SLOTS; ++slot)
            {
                const Item item = At(slot);
                if (item.IsEmpty() || name == nullptr)
                {
                    continue;
                }

                const char* mine = item.Name();
                const char* theirs = name;
                while (*mine != '\0' && *theirs != '\0' && *mine == *theirs)
                {
                    ++mine;
                    ++theirs;
                }
                if (*mine == *theirs)
                {
                    return true;
                }
            }
            return false;
        }

        /* How many slots the player carries */
        inline int                                                  SlotCount()
        {
            return (g_inventory != nullptr && g_inventory->GetSlotCount != nullptr)
                       ? g_inventory->GetSlotCount(g_self)
                       : 0;
        }

        inline bool                                                 SetSlotCount(int count)
        {
            return g_inventory != nullptr && g_inventory->SetSlotCount != nullptr &&
                   g_inventory->SetSlotCount(g_self, count) != 0;
        }

        /* Which slot holds one of these, or -1. */
        inline int                                                  SlotOf(const char* name)
        {
            return (g_inventory != nullptr && g_inventory->FindSlot != nullptr)
                       ? g_inventory->FindSlot(g_self, name)
                       : -1;
        }

        inline int                                                  FreeSlot()
        {
            return (g_inventory != nullptr && g_inventory->FindFreeSlot != nullptr)
                       ? g_inventory->FindFreeSlot(g_self)
                       : -1;
        }

        /* Puts an item into a slot */
        inline bool                                                 Put(int slot, const char* typeName, float time = 0.0f,
                                                                        AmodBonusTimer timer = AMOD_BONUS_TIMER_GAME)
        {
            return g_inventory != nullptr && g_inventory->PutItem != nullptr &&
                   g_inventory->PutItem(g_self, slot, typeName, time, timer) != 0;
        }

        /* Into the first free slot. False when all five are taken. */
        inline bool                                                 Add(const char* typeName, float time = 0.0f,
                                                                       AmodBonusTimer timer = AMOD_BONUS_TIMER_GAME)
        {
            const int slot = FreeSlot();
            return slot >= 0 && Put(slot, typeName, time, timer);
        }

        inline bool                                                 Clear(int slot)
        {
            return g_inventory != nullptr && g_inventory->ClearItem != nullptr &&
                   g_inventory->ClearItem(g_self, slot) != 0;
        }

        /* Carries the slot over whole, keeping whatever was set on that one slot */
        inline bool                                                 Move(int from, int to)
        {
            return g_inventory != nullptr && g_inventory->MoveItem != nullptr &&
                   g_inventory->MoveItem(g_self, from, to) != 0;
        }

        inline bool                                                 Swap(int first, int second)
        {
            return g_inventory != nullptr && g_inventory->SwapItems != nullptr &&
                   g_inventory->SwapItems(g_self, first, second) != 0;
        }

        /* Told when a slot empties, whichever way it emptied */
        inline AmodToken                                            OnSlot(AmodInventoryHandler handler, void* user = nullptr)
        {
            return (g_inventory != nullptr && g_inventory->AddSlotHandler != nullptr)
                       ? g_inventory->AddSlotHandler(g_self, handler, user)
                       : AMOD_INVALID_TOKEN;
        }

        inline AmodToken                                            OnChain(AmodChainHandler handler, void* user = nullptr)
        {
            return (g_inventory != nullptr && g_inventory->AddChainHandler != nullptr)
                       ? g_inventory->AddChainHandler(g_self, handler, user)
                       : AMOD_INVALID_TOKEN;
        }

        inline void                                                 Cancel(AmodToken token)
        {
            if (g_inventory != nullptr && g_inventory->structSize >= sizeof(AmodInventoryApi) &&
                g_inventory->Cancel != nullptr)
            {
                g_inventory->Cancel(g_self, token);
            }
        }

        /* Stops the game counting this slot down, and starts it again */
        inline bool                                                 SetTicking(int slot, bool ticking)
        {
            return g_inventory != nullptr && g_inventory->SetTicking != nullptr &&
                   g_inventory->SetTicking(g_self, slot, ticking ? 1 : 0) != 0;
        }

        inline bool                                                 IsTicking(int slot)
        {
            return g_inventory != nullptr && g_inventory->IsTicking != nullptr &&
                   g_inventory->IsTicking(g_self, slot) != 0;
        }

        
        class ItemProperty
        {
        public:
            ItemProperty(int slot, const char* key)
                : m_slot(slot)
                , m_key(key)
            {
            }

            bool                                                    Exists() const
            {
                return g_inventory != nullptr && g_inventory->HasItemProperty != nullptr &&
                       g_inventory->HasItemProperty(g_self, m_slot, m_key) != 0;
            }

            bool                                                    Read(AmodValue& value) const
            {
                return g_inventory != nullptr && g_inventory->GetItemProperty != nullptr &&
                       g_inventory->GetItemProperty(g_self, m_slot, m_key, &value) != 0;
            }

            int                                                     Integer(int fallback = 0) const
            {
                AmodValue value = {};
                if (!Read(value) ||
                    (value.kind != AMOD_VALUE_INT && value.kind != AMOD_VALUE_BOOL))
                {
                    return fallback;
                }
                return value.integer;
            }

            bool                                                    Boolean(bool fallback = false) const
            {
                return Integer(fallback ? 1 : 0) != 0;
            }

            float                                                   Number(int index = 0, float fallback = 0.0f) const
            {
                AmodValue value = {};
                if (!Read(value) || value.kind != AMOD_VALUE_FLOAT ||
                    index < 0 || static_cast<uint32_t>(index) >= value.count)
                {
                    return fallback;
                }
                return value.numbers[index];
            }

            const char*                                             Text(const char* fallback = "") const
            {
                AmodValue value = {};
                if (!Read(value) || value.kind != AMOD_VALUE_STRING || value.text == nullptr)
                {
                    return fallback;
                }
                return value.text;
            }

            bool                                                    Set(const AmodValue& value) const
            {
                return g_inventory != nullptr && g_inventory->SetItemProperty != nullptr &&
                       g_inventory->SetItemProperty(g_self, m_slot, m_key, &value) != 0;
            }

            bool                                                    Set(int number) const { return Set(Amod::Value(number)); }
            bool                                                    Set(bool boolean) const { return Set(Amod::Value(boolean)); }
            bool                                                    Set(const char* text) const { return Set(Amod::Value(text)); }
            bool                                                    Set(float a) const { return Set(Amod::Value(a)); }
            bool                                                    Set(float a, float b) const { return Set(Amod::Value(a, b)); }
            bool                                                    Set(float a, float b, float c) const { return Set(Amod::Value(a, b, c)); }

        private:
            int m_slot;
            const char* m_key;
        };

        class SlotRef
        {
        public:
            explicit SlotRef(int slot)
                : m_slot(slot)
            {
            }

            ItemProperty                                            operator[](const char* key) const
            {
                return ItemProperty(m_slot, key);
            }

            int                                                     PropertyCount() const
            {
                return (g_inventory != nullptr && g_inventory->ItemPropertyCount != nullptr)
                           ? g_inventory->ItemPropertyCount(g_self)
                           : 0;
            }

            const char*                                             PropertyNameAt(int index) const
            {
                return (g_inventory != nullptr && g_inventory->ItemPropertyNameAt != nullptr)
                           ? g_inventory->ItemPropertyNameAt(g_self, index)
                           : "";
            }

        private:
            int m_slot;
        };

        inline SlotRef                                              Slot(int slot)
        {
            return SlotRef(slot);
        }

        inline bool                                                 SetIcon(int slot, const char* file)
        {
            return g_inventory != nullptr && g_inventory->SetIcon != nullptr &&
                   g_inventory->SetIcon(g_self, slot, file) != 0;
        }

        inline bool                                                 SetTimeLeft(int slot, float seconds)
        {
            return g_inventory != nullptr && g_inventory->SetTimeLeft != nullptr &&
                   g_inventory->SetTimeLeft(g_self, slot, seconds) != 0;
        }

        inline bool                                                 GetChain(AmodBonusChain& out)
        {
            out = AmodBonusChain{};
            out.structSize = sizeof(out);
            return g_inventory != nullptr && g_inventory->GetChain(g_self, &out) != 0;
        }

        inline int                                                  BonusesLeft()
        {
            return g_inventory != nullptr ? g_inventory->GetBonusesLeft(g_self) : 0;
        }

        inline int                                                  MegaBonusesLeft()
        {
            return g_inventory != nullptr ? g_inventory->GetMegaBonusesLeft(g_self) : 0;
        }

        inline int                                                  OtherBonusesLeft()
        {
            return g_inventory != nullptr ? g_inventory->GetOtherBonusesLeft(g_self) : 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_INVENTORY_API_H */
