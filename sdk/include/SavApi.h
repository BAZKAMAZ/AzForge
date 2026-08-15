#ifndef AZFORGE_SAV_API_H
#define AZFORGE_SAV_API_H

#include "ForgeTypes.h"
#include "WorldApi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Asked for this mod's bytes while a slot is being written. Call WriteSlotData inside it. */
typedef void(__cdecl* AmodSlotSaveHandler)(int slot, void* user);

/* What this mod stored. `bytes` null and `size` 0 when the save holds nothing of its own. */
typedef void(__cdecl* AmodSlotLoadHandler)(int slot, const void* bytes, int size, void* user);

/* In `level`, the thing the mod calls `name` stands at `value`. */
typedef struct AmodLevelChange
{
    uint32_t structSize;

    const char* level;  /* the path, as AmodWorldLevel.script gives it */
    const char* name;   /* the mod's own, "door_7" */
    int value;

} AmodLevelChange;

AMOD_LAST_FIELD(AmodLevelChange, value, 0x0C);

/* packs.sav - the levels the player has opened, one 0x100 block of text each. */

typedef struct AmodSavApi
{
    uint32_t structSize;

    /* Levels the player has opened. */
    int(__cdecl* Count)(AmodMod mod);
    const char* (__cdecl* NameAt)(AmodMod mod, int index);
    int(__cdecl* IsOpen)(AmodMod mod, const char* levelId);

    /* Safe on every finish; does nothing if already open. Index, or -1. */
    int(__cdecl* Open)(AmodMod mod, const char* levelId);

    /* Also blocks it: the CASTLES screen reopens its whole $enabled_castles list every time it is
       built, so taking an id out is not enough on its own. Open unblocks it again. */
    int(__cdecl* Close)(AmodMod mod, const char* levelId);

    /* The block on its own, for a level that must never open. Not kept across a restart. */
    int(__cdecl* Block)(AmodMod mod, const char* levelId, int blocked);
    int(__cdecl* IsBlocked)(AmodMod mod, const char* levelId);

    /* The pack the player is on. */
    int(__cdecl* Current)(AmodMod mod);
    int(__cdecl* SetCurrent)(AmodMod mod, int pack);
    int(__cdecl* PackCount)(AmodMod mod);

    /* Write packs.sav now. */
    int(__cdecl* Save)(AmodMod mod);

    /* By place rather than by name. The file is only a list of ids, so a mod's own palace keeps
       with no change to the format. */
    int(__cdecl* Remove)(AmodMod mod, int index);
    int(__cdecl* SetId)(AmodMod mod, int index, const char* levelId);

    /* saves/saveNNN.sav: a 0x770 header, then a section per subsystem. */
    int(__cdecl* SlotExists)(AmodMod mod, int slot);

    /* "" when there is no such slot. Valid until the next call. */
    const char* (__cdecl* SlotName)(AmodMod mod, int slot);
    const char* (__cdecl* SlotLevel)(AmodMod mod, int slot);

    /* The game's own save and load. Loading drops whatever is being played. */
    int(__cdecl* SaveSlot)(AmodMod mod, int slot, const char* caption);
    int(__cdecl* LoadSlot)(AmodMod mod, int slot);

    int(__cdecl* DeleteSlot)(AmodMod mod, int slot);

    /* A mod's own state, appended after everything the game wrote - it never reads to the end of
       the file, so the tail is free. Register in AmodInit. */
    AmodToken(__cdecl* OnSlotSave)(AmodMod mod, AmodSlotSaveHandler handler, void* user);
    AmodToken(__cdecl* OnSlotLoad)(AmodMod mod, AmodSlotLoadHandler handler, void* user);
    void(__cdecl* Cancel)(AmodMod mod, AmodToken token);

    /* Only inside a save handler: what to keep. Called twice replaces the first lot. */
    int(__cdecl* WriteSlotData)(AmodMod mod, const void* bytes, int size);

    /* Kept per slot under a name the mod chooses, and handed back before the level is drawn.
       AzForge only remembers it; applying it is the mod's, through World and Ctrls. */
    int(__cdecl* SetLevelChange)(AmodMod mod, const char* level, const char* name, int value);
    int(__cdecl* GetLevelChange)(AmodMod mod, const char* level, const char* name, int fallback);
    int(__cdecl* HasLevelChange)(AmodMod mod, const char* level, const char* name);
    int(__cdecl* ClearLevelChange)(AmodMod mod, const char* level, const char* name);

    /* This mod's own, for applying the lot on load. */
    int(__cdecl* LevelChangeCount)(AmodMod mod);
    int(__cdecl* LevelChangeAt)(AmodMod mod, int index, AmodLevelChange* out);

    /* Game level section stored in a disk slot; valid only for the loaded level. Fixed length; tag and four extra bytes are room-owned. */
    int(__cdecl* GetSlotRoomThing)(AmodMod mod, int slot, int roomIndex, int index, AmodRoomThing* out);
    int(__cdecl* SetSlotRoomThing)(AmodMod mod, int slot, int roomIndex, int index, const AmodRoomThing* thing);

    int(__cdecl* GetSlotRoomMemory)(AmodMod mod, int slot, int roomIndex, AmodRoomMemory* out);
    int(__cdecl* SetSlotRoomMemory)(AmodMod mod, int slot, int roomIndex, const AmodRoomMemory* memory);

} AmodSavApi;

AMOD_LAST_FIELD(AmodSavApi, SetSlotRoomMemory, 0x84);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Progress
    {
        inline const AmodSavApi* g_sav = nullptr;
        inline AmodMod g_savSelf = nullptr;

        inline void                                                 Attach(const AmodSavApi* api, AmodMod self)
        {
            g_sav = api;
            g_savSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_sav != nullptr;
        }

        inline int                                                  Count()
        {
            return g_sav != nullptr ? g_sav->Count(g_savSelf) : 0;
        }

        inline const char*                                          NameAt(int index)
        {
            return g_sav != nullptr ? g_sav->NameAt(g_savSelf, index) : "";
        }

        inline bool                                                 IsOpen(const char* levelId)
        {
            return g_sav != nullptr && g_sav->IsOpen(g_savSelf, levelId) != 0;
        }

        inline int                                                  Open(const char* levelId)
        {
            return g_sav != nullptr ? g_sav->Open(g_savSelf, levelId) : -1;
        }

        inline bool                                                 Close(const char* levelId)
        {
            return g_sav != nullptr && g_sav->Close(g_savSelf, levelId) != 0;
        }

        inline bool                                                 Block(const char* levelId, bool blocked = true)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->Block != nullptr &&
                g_sav->Block(g_savSelf, levelId, blocked ? 1 : 0) != 0;
        }

        inline bool                                                 IsBlocked(const char* levelId)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->IsBlocked != nullptr &&
                g_sav->IsBlocked(g_savSelf, levelId) != 0;
        }

        /* Open, under the name a mod adding its own palace reaches for. */
        inline int                                                  Add(const char* levelId)
        {
            return Open(levelId);
        }

        inline bool                                                 Remove(int index)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->Remove != nullptr &&
                g_sav->Remove(g_savSelf, index) != 0;
        }

        inline const char*                                          GetId(int index)
        {
            return NameAt(index);
        }

        inline bool                                                 SetId(int index, const char* levelId)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->SetId != nullptr &&
                g_sav->SetId(g_savSelf, index, levelId) != 0;
        }

        /* Saved games. */
        inline bool                                                 SlotExists(int slot)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->SlotExists != nullptr &&
                g_sav->SlotExists(g_savSelf, slot) != 0;
        }

        inline const char*                                          SlotName(int slot)
        {
            return (g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->SlotName != nullptr)
                ? g_sav->SlotName(g_savSelf, slot)
                : "";
        }

        inline const char*                                          SlotLevel(int slot)
        {
            return (g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->SlotLevel != nullptr)
                ? g_sav->SlotLevel(g_savSelf, slot)
                : "";
        }

        inline bool                                                 SaveSlot(int slot, const char* caption)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->SaveSlot != nullptr &&
                g_sav->SaveSlot(g_savSelf, slot, caption) != 0;
        }

        inline bool                                                 LoadSlot(int slot)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->LoadSlot != nullptr &&
                g_sav->LoadSlot(g_savSelf, slot) != 0;
        }

        inline bool                                                 DeleteSlot(int slot)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->DeleteSlot != nullptr &&
                g_sav->DeleteSlot(g_savSelf, slot) != 0;
        }

        /* Register in AmodInit. */
        inline AmodToken                                            OnSlotSave(AmodSlotSaveHandler handler, void* user = nullptr)
        {
            return (g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->OnSlotSave != nullptr)
                ? g_sav->OnSlotSave(g_savSelf, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline AmodToken                                            OnSlotLoad(AmodSlotLoadHandler handler, void* user = nullptr)
        {
            return (g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->OnSlotLoad != nullptr)
                ? g_sav->OnSlotLoad(g_savSelf, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline void                                                 Cancel(AmodToken token)
        {
            if (g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->Cancel != nullptr)
            {
                g_sav->Cancel(g_savSelf, token);
            }
        }

        /* Only meaningful inside a save handler. */
        inline bool                                                 WriteSlotData(const void* bytes, int size)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->WriteSlotData != nullptr &&
                g_sav->WriteSlotData(g_savSelf, bytes, size) != 0;
        }

        /* Kept in the slot, handed back on load. */
        inline bool                                                 SetLevelChange(const char* level, const char* name, int value)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->SetLevelChange != nullptr &&
                g_sav->SetLevelChange(g_savSelf, level, name, value) != 0;
        }

        inline int                                                  GetLevelChange(const char* level, const char* name, int fallback = 0)
        {
            return (g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->GetLevelChange != nullptr)
                ? g_sav->GetLevelChange(g_savSelf, level, name, fallback)
                : fallback;
        }

        inline bool                                                 HasLevelChange(const char* level, const char* name)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->HasLevelChange != nullptr &&
                g_sav->HasLevelChange(g_savSelf, level, name) != 0;
        }

        inline bool                                                 ClearLevelChange(const char* level, const char* name)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->ClearLevelChange != nullptr &&
                g_sav->ClearLevelChange(g_savSelf, level, name) != 0;
        }

        inline int                                                  LevelChangeCount()
        {
            return (g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->LevelChangeCount != nullptr)
                ? g_sav->LevelChangeCount(g_savSelf)
                : 0;
        }

        inline bool                                                 LevelChangeAt(int index, AmodLevelChange& out)
        {
            out = AmodLevelChange{};
            out.structSize = sizeof(out);
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->LevelChangeAt != nullptr &&
                g_sav->LevelChangeAt(g_savSelf, index, &out) != 0;
        }

        /* The game's own level section, patched where it lies. Wants that level loaded. */
        inline bool                                                 GetSlotRoomThing(int slot, int roomIndex, int index, AmodRoomThing& out)
        {
            out = AmodRoomThing{};
            out.structSize = sizeof(out);
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->GetSlotRoomThing != nullptr &&
                g_sav->GetSlotRoomThing(g_savSelf, slot, roomIndex, index, &out) != 0;
        }

        inline bool                                                 SetSlotRoomThing(int slot, int roomIndex, int index, const AmodRoomThing& thing)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->SetSlotRoomThing != nullptr &&
                g_sav->SetSlotRoomThing(g_savSelf, slot, roomIndex, index, &thing) != 0;
        }

        inline bool                                                 GetSlotRoomMemory(int slot, int roomIndex, AmodRoomMemory& out)
        {
            out = AmodRoomMemory{};
            out.structSize = sizeof(out);
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->GetSlotRoomMemory != nullptr &&
                g_sav->GetSlotRoomMemory(g_savSelf, slot, roomIndex, &out) != 0;
        }

        inline bool                                                 SetSlotRoomMemory(int slot, int roomIndex, const AmodRoomMemory& memory)
        {
            return g_sav != nullptr && g_sav->structSize >= sizeof(AmodSavApi) && g_sav->SetSlotRoomMemory != nullptr &&
                g_sav->SetSlotRoomMemory(g_savSelf, slot, roomIndex, &memory) != 0;
        }

        inline int                                                  Current()
        {
            return g_sav != nullptr ? g_sav->Current(g_savSelf) : 0;
        }

        inline bool                                                 SetCurrent(int pack)
        {
            return g_sav != nullptr && g_sav->SetCurrent(g_savSelf, pack) != 0;
        }

        inline int                                                  PackCount()
        {
            return g_sav != nullptr ? g_sav->PackCount(g_savSelf) : 0;
        }

        inline bool                                                 Save()
        {
            return g_sav != nullptr && g_sav->Save(g_savSelf) != 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_SAV_API_H */
