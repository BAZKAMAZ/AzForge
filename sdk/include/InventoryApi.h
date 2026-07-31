#ifndef AZFORGE_INVENTORY_API_H
#define AZFORGE_INVENTORY_API_H

/* For AmodBonusTimer: who counts a carried item down is the same question whether the item is * being defined or being put into a slot. */
#include "BonusesApi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How many slots the player has to begin with - what the game itself was built for */
#define AMOD_INVENTORY_SLOTS                                        5

/* And the most there can be, once the loader has moved them out of the player. */
#define AMOD_INVENTORY_SLOTS_MAX                                    32

/* One carried item. An empty slot answers with an empty name, which is exactly how the game * itself tells a free slot from a full one. */
typedef struct AmodInventoryItem
{
    uint32_t structSize;

    const char* name;           /* the bonus type it came from, "" when the slot is free */

    /* What is left of its timer and what it started with, in seconds. Both zero for an item that * does not run down. */
    float timeLeft;
    float timeTotal;

    int isTotem;
    int isTorch;

    int pointsOnRunOut;         /* $pts_out - what is scored if it expires rather than being used */
    float burnTime;             /* a torch's own */

    /* New fields are appended, never inserted. */
} AmodInventoryItem;

/* The scoring chain */
typedef struct AmodBonusChain
{
    uint32_t structSize;

    int length;                 /* how many have been taken in a row */
    int points;                 /* what the chain has scored so far */
    float timeLeft;             /* of the window; at zero the next bonus starts a new chain */
    float window;               /* what the timer is reset to on each take */

    /* New fields are appended, never inserted. */
} AmodBonusChain;

typedef struct AmodInventoryApi
{
    uint32_t structSize;

    /* Whether there is a player to ask about at all - there is none in a menu. */
    int (__cdecl* IsAvailable)(AmodMod mod);

    /* One of the AMOD_INVENTORY_SLOTS slots, free or not. Non-zero when it could be read. */
    int (__cdecl* GetItem)(AmodMod mod, int slot, AmodInventoryItem* out);

    /* How many slots are in use. */
    int (__cdecl* GetUsedCount)(AmodMod mod);

    int (__cdecl* GetChain)(AmodMod mod, AmodBonusChain* out);

    /* How many bonuses the level still has, by the three counts the game keeps: the ordinary * ones, th... */
    int (__cdecl* GetBonusesLeft)(AmodMod mod);
    int (__cdecl* GetMegaBonusesLeft)(AmodMod mod);
    int (__cdecl* GetOtherBonusesLeft)(AmodMod mod);

    /* Which slot holds an item of that name, or -1. The game's own lookup, and the one it uses to * decide where a second totem goes. */
    int (__cdecl* FindSlot)(AmodMod mod, const char* name);

    /* The first slot with nothing in it, or -1 when all five are full. */
    int (__cdecl* FindFreeSlot)(AmodMod mod);

    
    int (__cdecl* PutItem)(AmodMod mod, int slot, const char* typeName, float time,
                           AmodBonusTimer timer);

    /* Empties a slot the way the game does when an item runs out, the use sound included. */
    int (__cdecl* ClearItem)(AmodMod mod, int slot);

    /* Moves a carried item's timer */
    int (__cdecl* SetTimeLeft)(AmodMod mod, int slot, float seconds);

    /* The icon this one slot shows, without touching the type it came from - two slots holding the * same kind can look different. */
    int (__cdecl* SetIcon)(AmodMod mod, int slot, const char* file);

    
    int (__cdecl* GetItemProperty)(AmodMod mod, int slot, const char* key, AmodValue* value);
    int (__cdecl* SetItemProperty)(AmodMod mod, int slot, const char* key, const AmodValue* value);
    int (__cdecl* HasItemProperty)(AmodMod mod, int slot, const char* key);
    int (__cdecl* ItemPropertyCount)(AmodMod mod);
    const char* (__cdecl* ItemPropertyNameAt)(AmodMod mod, int index);

    
    int (__cdecl* GetSlotCount)(AmodMod mod);
    int (__cdecl* SetSlotCount)(AmodMod mod, int count);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodInventoryApi;

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
