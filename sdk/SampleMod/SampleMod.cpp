#include <d3d8.h>
#include <ForgeApi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const kRedKnife = "red_knife";
static const float kRedKnifeSeconds = 20.0f;

static const char* const kRedTexture = "textures/bonus/knife_red.jpg";
static const char* const kRedIcon = "textures/bonus/knife_red_icon.png";
static const char* const kPlainTexture = "textures/bonus/knife.jpg";
static const char* const kPlainIcon = "textures/bonus/knife_icon.png";

static float g_redKnifeLeft = 0.0f;
static float g_blink = 0.0f;
static bool g_red = true;
static unsigned g_mine = 0;

static bool SlotHolds(int slot, const char* name)
{
    return strcmp(Amod::Inventory::At(slot).Name(), name) == 0;
}

static void DropTheKnives()
{
    for (int slot = 0; slot < Amod::Inventory::SlotCount(); ++slot)
    {
        if ((g_mine & (1u << slot)) != 0 &&
            (SlotHolds(slot, kRedKnife) || SlotHolds(slot, "knife")))
        {
            Amod::Inventory::Clear(slot);
        }
    }
    g_mine = 0;
}

static void FillWithKnives()
{
    const int count = Amod::Inventory::SlotCount();

    if (Amod::Inventory::SlotOf(kRedKnife) < 0)
    {
        const int mine = Amod::Inventory::FreeSlot();
        if (mine >= 0 && Amod::Inventory::Put(mine, kRedKnife, kRedKnifeSeconds, AMOD_BONUS_TIMER_MOD))
        {
            g_mine |= 1u << mine;
            Amod::Inventory::Slot(mine)["icon"].Set(kRedIcon);
        }
    }

    int empty = 0;
    for (int slot = 0; slot < count; ++slot)
    {
        empty += Amod::Inventory::At(slot).IsEmpty() ? 1 : 0;
    }

    for (int slot = 0; slot < count; ++slot)
    {
        if (empty > 1 && Amod::Inventory::At(slot).IsEmpty() &&
            Amod::Inventory::Put(slot, "knife", 0.0f, AMOD_BONUS_TIMER_NONE))
        {
            g_mine |= 1u << slot;
            --empty;
        }
        if (SlotHolds(slot, kRedKnife))
        {
            Amod::Inventory::Slot(slot)["time_left"].Set(g_redKnifeLeft);
        }
    }
}

static AmodBonusTakeResult __cdecl OnBonusTaken(const AmodBonusTakeInfo* info, void*)
{
    if (strcmp(info->resourceId, kRedKnife) != 0)
    {
        return AMOD_BONUS_TAKE_DEFAULT;
    }

    g_redKnifeLeft = kRedKnifeSeconds;
    FillWithKnives();

    Amod::Bonuses::TakeChainLink(info->pointsOnTake, info->position);
    return AMOD_BONUS_TAKE_HANDLED;
}

static void __cdecl SpendCommand(int, const char**, void*)
{
    for (int slot = 0; slot < Amod::Inventory::SlotCount(); ++slot)
    {
        if (SlotHolds(slot, "knife"))
        {
            Amod::Inventory::Clear(slot);
            Amod::Console::Printf("spent the knife in slot %d\n", slot);
            return;
        }
    }
    Amod::Console::Printf("no knife is carried\n");
}

static void __cdecl FloaterCommand(int argc, const char** argv, void*)
{
    if (argc < 3)
    {
        Amod::Console::Printf("floater <column> <row>\n");
        return;
    }

    const int column = atoi(argv[1]);
    const int row = atoi(argv[2]);

    Amod::Console::Printf(Amod::Bonuses::NewPlacement("floater", column, row).Put()
                              ? "a floater on cell %d,%d\n"
                              : "could not put a floater on cell %d,%d\n",
                          column, row);
}

static void __cdecl PlaceCommand(int argc, const char** argv, void*)
{
    if (argc < 4)
    {
        Amod::Console::Printf("place <name> <column> <row>   e.g. place diamond 20 14\n");
        return;
    }

    const int column = atoi(argv[2]);
    const int row = atoi(argv[3]);

    Amod::Console::Printf(Amod::Bonuses::Place(argv[1], column, row)
                              ? "put a '%s' on cell %d,%d\n"
                              : "could not put a '%s' on cell %d,%d - is that a name from "
                                "bonus_res.txt?\n",
                          argv[1], column, row);
}

static bool g_ownRow = false;
static int g_holders = 0;

static void SetHolders(int count)
{
    if (!g_ownRow)
    {
        for (int i = 0; i < 10; ++i)
        {
            Amod::Hud::RemoveImage(0);
        }
        g_ownRow = true;
    }

    for (int i = 0; i < g_holders; ++i)
    {
        Amod::Hud::RemoveImage(Amod::Hud::ImageCount() - 1);
    }
    g_holders = 0;

    for (int half = 0; half < count * 2; ++half)
    {
        char fragment[320];
        sprintf_s(fragment, sizeof(fragment),
                  "$images\n{\n\t$image\n\t{\n"
                  "\t\t$texture \"textures/ui/derjatel.png\"\n"
                  "\t\t$pos %d 480\n\t\t$size 30 30\n%s\t}\n}\n",
                  half * 30, (half % 2) != 0 ? "\t\t$horz_flip\n" : "");

        g_holders += Amod::Hud::AddImages(fragment);
    }
}

static void __cdecl InventoryCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        Amod::Console::Printf("inv <count>   now %d, up to %d\n", Amod::Inventory::SlotCount(),
                              AMOD_INVENTORY_SLOTS_MAX);
        return;
    }

    const int count = atoi(argv[1]);

    if (Amod::Inventory::SetSlotCount(count))
    {
        // Nothing in the game shortens the step when there are more slots to fit, so the row is
        // squeezed by hand until it would run off a 640-wide screen.
        const float start = Amod::Hud::At("items.start_pos").Number(0, 30.0f);
        const float step = (640.0f - start) / static_cast<float>(count);

        Amod::Hud::At("items.offs").Set(step < 60.0f ? step : 60.0f, 0.0f);
        SetHolders(count);

        Amod::Console::Printf("the player carries %d slot(s)\n", count);
    }
    else
    {
        Amod::Console::Printf("%d slots is refused - 1 to %d is what there is room for\n", count,
                              AMOD_INVENTORY_SLOTS_MAX);
    }
}

static void __cdecl KindsCommand(int, const char**, void*)
{
    const int count = Amod::Bonuses::TypeCount();
    Amod::Console::Printf("%d kinds of bonus\n", count);

    for (int index = 0; index < count; ++index)
    {
        AmodBonusType type;
        if (!Amod::Bonuses::GetType(index, type))
        {
            continue;
        }

        Amod::Console::Printf("%2d  %-12s %-12s %5d pts%s%s%s\n", index, type.resourceId,
                              type.name, type.pointsOnTake,
                              type.isTorch != 0 ? "  torch" : (type.isTotem != 0 ? "  totem" : ""),
                              type.makesTrain != 0 ? "  train" : "",
                              type.time > 0.0f ? "  timed" : "");
    }
}

// The game carries the slots from one level into the next, so they are emptied on the first frame
// the new player exists - the carried state is restored after the event.
static bool g_emptyThem = false;

static void __cdecl OnWorldEvent(const AmodWorldEventInfo*, void*)
{
    g_redKnifeLeft = 0.0f;
    g_emptyThem = true;
}

static void EmptyEverySlot()
{
    for (int slot = 0; slot < Amod::Inventory::SlotCount(); ++slot)
    {
        Amod::Inventory::Clear(slot);
    }
    g_mine = 0;
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Amod::Console::AddCommand("place", &PlaceCommand);
    Amod::Console::AddCommand("kinds", &KindsCommand);
    Amod::Console::AddCommand("spend", &SpendCommand);
    Amod::Console::AddCommand("floater", &FloaterCommand);
    Amod::Console::AddCommand("inv", &InventoryCommand);
    Amod::Bonuses::OnTake(&OnBonusTaken);
    Amod::World::OnEvent(AMOD_WORLD_EVENT_LEVEL_LOADED, &OnWorldEvent);
    Amod::World::OnEvent(AMOD_WORLD_EVENT_LEVEL_UNLOADED, &OnWorldEvent);

    // The game has not read bonus_res.txt yet at this point, so the loader holds these until it has.
    AmodBonusType known;

    if (!Amod::Bonuses::FindType(kRedKnife, known))
    {
        Amod::Bonuses::NewType(kRedKnife, "knife")
            .Glow(1.0f, 0.15f, 0.1f, 1.0f)
            .Glow2(1.0f, 0.3f, 0.2f, 1.0f)
            .Timer(kRedKnifeSeconds, AMOD_BONUS_TIMER_MOD)
            .PointsOnTake(50)
            .Define();
    }

    if (!Amod::Bonuses::FindType("floater", known))
    {
        Amod::Bonuses::NewType("floater", "bonus_1")
            .Behaves(AMOD_BONUS_LIKE_SCORED)
            .PointsOnTake(4000)
            .Glow(1.0f, 0.4f, 0.1f, 1.0f)
            .Rocking(0.15f, 2.0f, 0.0f, 0.0f, 1.0f)
            .Define();
    }

    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame* frame)
{
    g_blink += frame->delta;
    if (g_blink >= 0.3f)
    {
        g_blink = 0.0f;
        g_red = !g_red;

        Amod::Bonuses::Type(kRedKnife)["texture"].Set(g_red ? kRedTexture : kPlainTexture);

        const int carried = Amod::Inventory::SlotOf(kRedKnife);
        if (carried >= 0)
        {
            Amod::Inventory::Slot(carried)["icon"].Set(g_red ? kRedIcon : kPlainIcon);
        }
    }

    if (!Amod::Inventory::HasPlayer())
    {
        return;
    }

    if (g_emptyThem)
    {
        EmptyEverySlot();
        g_emptyThem = false;
    }

    if (g_redKnifeLeft <= 0.0f)
    {
        const int carried = Amod::Inventory::SlotOf(kRedKnife);
        if (carried < 0)
        {
            return;
        }

        g_redKnifeLeft = kRedKnifeSeconds;
        g_mine |= 1u << carried;
        Amod::Inventory::Slot(carried)["icon"].Set(kRedIcon);
    }

    g_redKnifeLeft -= frame->delta;
    if (g_redKnifeLeft > 0.0f)
    {
        FillWithKnives();
        return;
    }

    g_redKnifeLeft = 0.0f;

    AmodBonusType type;
    const int left = Amod::Bonuses::FindType(kRedKnife, type) ? type.pointsOnRunOut : 0;

    DropTheKnives();
    Amod::Bonuses::AwardPoints(left);
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
