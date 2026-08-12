#include <ForgeApi.h>

#include <stdlib.h>
#include <string.h>

using namespace Amod;

bool g_watching = false;
bool g_refusing = false;
AmodToken g_spawn = AMOD_INVALID_TOKEN;

bool Same(const char* a, const char* b)
{
    return a != nullptr && b != nullptr && strcmp(a, b) == 0;
}

bool Off(int argc, const char** argv, int at)
{
    return argc > at && Same(argv[at], "off");
}

// Every placement the room is about to stand up, before it exists.
int __cdecl OnBonusSpawn(const AmodBonusSpawnInfo* info, void*)
{
    if (g_watching)
    {
        Console::Printf("  %-14s type '%s'%s at %.2f %.2f %.2f\n", info->name, info->resourceId,
            info->hasType != 0 ? "" : "  <- NO SUCH TYPE", info->position[0], info->position[1],
            info->position[2]);
    }

    return g_refusing ? 0 : 1;
}

AmodToken g_slotEvents = AMOD_INVALID_TOKEN;
AmodToken g_chainEvents = AMOD_INVALID_TOKEN;

void __cdecl OnSlotEmptied(const AmodInventoryEventInfo* info, void*)
{
    Console::Printf("  slot %d: '%s' %s, %d points were riding on it\n", info->slot, info->name,
        info->event == AMOD_INVENTORY_EXPIRED ? "ran out" : "was used",
        info->pointsOnRunOut);
}

void __cdecl OnChain(const AmodChainEventInfo* info, void*)
{
    if (info->broke != 0)
    {
        Console::Printf("  chain broke after %d scored\n", info->points);
        return;
    }

    Console::Printf("  chain %d, %d scored\n", info->length, info->points);
}

int g_kind = 0;
float g_lived = 0.0f;

int __cdecl OnMineKind(const AmodBonusKindInfo* info, void*)
{
    if (info->event == AMOD_BONUS_EVENT_BUILD)
    {
        info->body->model = "models/bonus/key.mmd";
        info->body->texture = "textures/rooms/5_01_wall.jpg";
        info->body->pointsOnTake = 250;

        info->body->glowColor[0] = 1.0f;
        info->body->glowColor[3] = 1.0f;
        info->body->glowColor2[2] = 1.0f;
        info->body->glowColor2[3] = 1.0f;

        info->body->rockingAmp = 0.05f;
        info->body->rockingSpeed = 3.0f;
        info->body->rockingAxis[2] = 1.0f;

        info->body->lightColor[0] = 1.0f;
        info->body->lightRadius = 1.5f;
        return 1;
    }

    // Its own tick, which is the half a type could never have: it climbs while it waits.
    if (info->event == AMOD_BONUS_EVENT_UPDATE)
    {
        g_lived = g_lived + info->deltaTime;
        info->body->glowColor[1] = g_lived - static_cast<int>(g_lived);
        return 0;
    }

    Console::Printf("  kind '%s' taken at %d %d - %d points\n", info->take->tag,
        info->take->column, info->take->row, info->take->pointsOnTake);
    return AMOD_BONUS_TAKE_DEFAULT;
}

int g_borrowKind = 0;
bool g_suppress = true;
bool g_borrowing = false;
AmodToken g_worldEvents = AMOD_INVALID_TOKEN;

int BorrowThisRoom()
{
    const int entities = World::CurrentSpawnCount();
    int taken = 0;

    for (int at = 0; at < entities; ++at)
    {
        AmodBonusSpawnInfo one = {};
        if (!Bonuses::GetPlacement(at, one) || one.isLive == 0 || one.kind != 0)
        {
            continue;
        }
        if (Bonuses::BorrowPlacement(g_borrowKind, at))
        {
            ++taken;
        }
    }
    return taken;
}

void __cdecl OnWorldEvent(const AmodWorldEventInfo* info, void*)
{
    if (g_borrowing && info->event == AMOD_WORLD_EVENT_ROOM_CHANGED)
    {
        BorrowThisRoom();
    }
}

struct Dress
{
    AmodModelId model;
    AmodTextureId texture;
};

Dress g_dresses[64] = {};
int g_dressCount = 0;
bool g_hah = false;
float g_hahTime = 0.0f;
int g_hahAt = 0;

void CollectDresses()
{
    const int types = Bonuses::TypeCount();
    g_dressCount = 0;

    for (int at = 0; at < types && g_dressCount < 64; ++at)
    {
        AmodBonusType one = {};
        if (!Bonuses::GetType(at, one) || one.modelId == 0 || one.textureId == 0)
        {
            continue;
        }

        // Skip a pair already taken, so a table full of keys does not fill the list with them
        bool seen = false;
        for (int back = 0; back < g_dressCount; ++back)
        {
            seen = seen || (g_dresses[back].model == one.modelId && g_dresses[back].texture == one.textureId);
        }

        if (!seen)
        {
            g_dresses[g_dressCount].model = one.modelId;
            g_dresses[g_dressCount].texture = one.textureId;
            ++g_dressCount;
        }
    }
}

int __cdecl OnBorrowedBonus(const AmodBonusKindInfo* info, void*)
{
    // BUILD never comes for a borrowed one - the game built it, we only took the two slots.
    if (info->event == AMOD_BONUS_EVENT_UPDATE)
    {
        g_lived = g_lived + info->deltaTime;

        const float wave = g_lived - static_cast<int>(g_lived);
        info->body->glowColor[0] = wave;
        info->body->glowColor[2] = 1.0f - wave;
        info->body->glowColor[3] = 1.0f;

        if (g_hah && g_dressCount > 0)
        {
            info->body->modelId = g_dresses[g_hahAt].model;
            info->body->textureId = g_dresses[g_hahAt].texture;
        }

        return g_suppress ? 1 : 0;
    }

    Console::Printf("  borrowed '%s' taken at %d %d - handing it back to the game\n", info->take->resourceId, info->take->column, info->take->row);
    return AMOD_BONUS_TAKE_DEFAULT;
}

void __cdecl BonusCommand(int argc, const char** argv, void*)
{
    if (g_spawn == AMOD_INVALID_TOKEN)
    {
        g_spawn = Bonuses::OnSpawn(&OnBonusSpawn);
    }

    if (g_spawn == AMOD_INVALID_TOKEN)
    {
        Console::Print("bonus: this loader does not offer the spawn event\n");
        return;
    }

    const char* const what = argc > 1 ? argv[1] : "watch";

    if (Same(what, "watch"))
    {
        g_watching = !Off(argc, argv, 2);
        Console::Print(g_watching
            ? "bonus: reporting every placement as a room stands it up - walk into one\n"
            : "bonus: quiet again\n");
        return;
    }

    if (Same(what, "none"))
    {
        g_refusing = !Off(argc, argv, 2);
        Console::Print(g_refusing
            ? "bonus: every placement refused - walk into a room to find it empty\n"
            : "bonus: placements allowed again\n");
        return;
    }

    if (Same(what, "mine"))
    {
        const bool carried = argc > 2 && Same(argv[2], "carry");

        Bonuses::NewType made(carried ? "azforge.minekey" : "azforge.mine");
        made.Texture("textures/rooms/5_01_wall.jpg");
        made.Model("models/bonus/key.mmd");
        made.PointsOnTake(500);
        made.Glow(0.2f, 1.0f, 0.4f);

        if (carried)
        {
            made.Icon("textures/console.jpg");
        }
        else
        {
            made.Behaves(AMOD_BONUS_LIKE_SCORED);
        }

        Console::Printf(made.Define()
            ? "bonus: '%s' defined from nothing - place it with 'bonus place %s'\n"
            : "bonus: '%s' would not take - see forge.log, and a type can only be defined "
                "outside a level\n",
            carried ? "azforge.minekey" : "azforge.mine",
            carried ? "azforge.minekey" : "azforge.mine");
        return;
    }

    if (Same(what, "kind"))
    {
        if (g_kind == 0)
        {
            g_kind = Bonuses::RegisterKind("azforge.mineKind", &OnMineKind);
        }

        if (g_kind == 0)
        {
            Console::Print("bonus: this loader does not offer kinds of a mod's own\n");
            return;
        }

        const int column = argc > 2 ? atoi(argv[2]) : 20;
        const int row = argc > 3 ? atoi(argv[3]) : 14;

        Console::Printf(Bonuses::PlaceKind(g_kind, column, row, "mine")
            ? "bonus: kind %d placed at %d %d - it pulses, bobs and lights the floor\n"
            : "bonus: kind %d would not place at %d %d - are you in a level?\n",
            g_kind, column, row);
        return;
    }

    // Take over what the level itself put in this room
    if (Same(what, "borrow"))
    {
        if (g_borrowKind == 0)
        {
            g_borrowKind = Bonuses::RegisterKind("azforge.borrowed", &OnBorrowedBonus);
        }

        if (g_borrowKind == 0)
        {
            Console::Print("bonus: this loader does not offer kinds of a mod's own\n");
            return;
        }

        if (Off(argc, argv, 2))
        {
            g_borrowing = false;
            Console::Printf("bonus: %d given back to the game, look and all\n", Bonuses::ReturnAll());
            return;
        }

        if (g_worldEvents == AMOD_INVALID_TOKEN)
        {
            g_worldEvents = World::OnEvent(AMOD_WORLD_EVENT_ROOM_CHANGED, &OnWorldEvent);
        }

        g_borrowing = true;
        g_suppress = !Same(argc > 2 ? argv[2] : "", "bob");

        Console::Printf("bonus: %d of this room's own taken over - they pulse, %s, and still "
            "score the game's way when picked up. Every room from here on, too.\n",
            BorrowThisRoom(), g_suppress ? "no longer bob" : "go on bobbing");
        return;
    }

    // Every bonus in the room, wearing a different type's clothes every second
    if (Same(what, "hah"))
    {
        if (Off(argc, argv, 2))
        {
            g_hah = false;
            g_borrowing = false;
            Console::Printf("bonus: %d given back, wearing what they came in\n", Bonuses::ReturnAll());
            return;
        }

        if (g_borrowKind == 0)
        {
            Console::Print("bonus: this loader does not offer kinds of a mod's own\n");
            return;
        }

        CollectDresses();
        if (g_dressCount == 0)
        {
            Console::Print("bonus: no bonus type in this level carries both a model and a texture\n");
            return;
        }

        if (g_worldEvents == AMOD_INVALID_TOKEN)
        {
            g_worldEvents = World::OnEvent(AMOD_WORLD_EVENT_ROOM_CHANGED, &OnWorldEvent);
        }

        g_hah = true;
        g_borrowing = true;
        g_suppress = false;

        Console::Printf("bonus: %d taken over, changing between %d looks a second apart - the "
            "texture always the one cut for that model\n", BorrowThisRoom(), g_dressCount);
        return;
    }

    // Every room of the level, without walking into any of them
    if (Same(what, "rooms"))
    {
        const int rooms = World::RoomCount();
        int total = 0;

        for (int room = 0; room < rooms; ++room)
        {
            const int held = Bonuses::PlacementCountIn(room);
            int live = 0;

            for (int at = 0; at < held; ++at)
            {
                AmodBonusSpawnInfo one = {};
                if (Bonuses::GetPlacement(at, one, room) && one.isLive != 0)
                {
                    ++live;
                }
            }

            if (live != 0)
            {
                Console::Printf("  room %3d  %d bonus(es)\n", room, live);
                total = total + live;
            }
        }

        Console::Printf("bonus: %d standing across %d rooms\n", total, rooms);
        return;
    }

    // Dressing a room the player is not in, and the next room is the plainest proof
    if (Same(what, "far"))
    {
        const int room = argc > 2 ? atoi(argv[2]) : 0;
        const int column = argc > 3 ? atoi(argv[3]) : 20;
        const int row = argc > 4 ? atoi(argv[4]) : 14;

        Console::Printf(Bonuses::PlaceKindIn(g_kind, room, column, row, "far")
            ? "bonus: kind put in room %d at %d %d - walk there, it stands up on arrival\n"
            : "bonus: room %d would not take one at %d %d\n", room, column, row);
        return;
    }

    // What one placement says about itself, as against what its type says
    if (Same(what, "prop"))
    {
        const int at = argc > 2 ? atoi(argv[2]) : 0;

        if (argc > 4)
        {
            AmodValue wanted = {};
            wanted.kind = AMOD_VALUE_STRING;
            wanted.text = argv[4];

            bool numeric = argv[4][0] == '-' || (argv[4][0] >= '0' && argv[4][0] <= '9');
            bool fractional = argc > 5;

            for (const char* walk = argv[4]; numeric && *walk != '\0'; ++walk)
            {
                fractional = fractional || *walk == '.';
            }

            if (numeric && fractional)
            {
                wanted.kind = AMOD_VALUE_FLOAT;
                wanted.count = 0;
                for (int give = 4; give < argc && wanted.count < 4; ++give)
                {
                    wanted.numbers[wanted.count] = static_cast<float>(atof(argv[give]));
                    ++wanted.count;
                }
            }
            else if (numeric)
            {
                wanted.kind = AMOD_VALUE_INT;
                wanted.integer = atoi(argv[4]);
            }

            Console::Printf(Bonuses::SetPlacementProperty(at, argv[3], wanted)
                ? "bonus: placement %d, '%s' set\n"
                : "bonus: placement %d would not take '%s'\n", at, argv[3]);
            return;
        }

        const int keys = Bonuses::PlacementPropertyCount();
        for (int key = 0; key < keys; ++key)
        {
            const char* const name = Bonuses::PlacementPropertyNameAt(key);
            AmodValue value = {};

            if (!Bonuses::GetPlacementProperty(at, name, value))
            {
                continue;
            }

            if (value.kind == AMOD_VALUE_STRING)
            {
                Console::Printf("  %-10s '%s'\n", name, value.text);
            }
            else if (value.kind == AMOD_VALUE_FLOAT)
            {
                Console::Printf("  %-10s %.3f\n", name, value.numbers[0]);
            }
            else
            {
                Console::Printf("  %-10s %d\n", name, value.integer);
            }
        }
        return;
    }

    if (Same(what, "place"))
    {
        const char* const name = argc > 2 ? argv[2] : "key_a";
        const int column = argc > 3 ? atoi(argv[3]) : 20;
        const int row = argc > 4 ? atoi(argv[4]) : 14;
        const char* const texture = argc > 5 ? argv[5] : nullptr;

        // A look of its own, so this one differs from every other bonus of its type
        Bonuses::NewPlacement made(name, column, row);
        made.Tag("mine");
        if (texture != nullptr)
        {
            made.Texture(texture);
        }

        Console::Printf(made.Put()
            ? "bonus: '%s' placed at %d %d\n"
            : "bonus: '%s' refused at %d %d - is that a type this level knows?\n", name, column, row);
        return;
    }

    if (Same(what, "carried"))
    {
        const int slots = Inventory::SlotCount();
        for (int at = 0; at < slots; ++at)
        {
            const Inventory::Item item = Inventory::At(at);
            if (item.IsEmpty())
            {
                continue;
            }

            Console::Printf("  slot %d  %-12s res '%s'  %.1f of %.1f s\n", at, item.Name(),
                item.ResourceId(), item.GetTimeLeft(), item.GetTimeTotal());
        }
        return;
    }

    if (Same(what, "room"))
    {
        const int entities = World::CurrentSpawnCount();
        int shown = 0;

        for (int at = 0; at < entities; ++at)
        {
            AmodBonusSpawnInfo one = {};
            if (!Bonuses::GetPlacement(at, one) || one.isLive == 0)
            {
                continue;
            }

            Console::Printf("  %3d  %-12s res '%s'  tag '%s'  cell %d %d%s\n", at, one.name,
                one.resourceId, one.tag, World::ColumnAt(one.position[0]),
                World::RowAt(one.position[1]), one.kind != 0 ? "  <- a kind of ours" : "");
            ++shown;
        }

        Console::Printf("bonus: %d standing in this room\n", shown);
        return;
    }

    if (Same(what, "take"))
    {
        if (argc > 3)
        {
            const int index = Bonuses::IndexOfCell(atoi(argv[2]), atoi(argv[3]));
            if (index < 0)
            {
                Console::Print("bonus: nothing is standing on that cell\n");
                return;
            }

            Console::Print(Bonuses::RemovePlacement(index)
                ? "bonus: taken out of the room, and the level's count with it\n"
                : "bonus: it would not go\n");
            return;
        }

        const char* const tag = argc > 2 ? argv[2] : "mine";
        Console::Printf("bonus: %d wearing '%s' taken out at once\n", Bonuses::RemoveTagged(tag), tag);
        return;
    }

    if (Same(what, "takeall"))
    {
        const char* const name = argc > 2 ? argv[2] : "key_a";
        Console::Printf("bonus: %d of '%s' taken out at once\n", Bonuses::RemoveNamed(name), name);
        return;
    }

    if (Same(what, "events"))
    {
        if (Off(argc, argv, 2))
        {
            Inventory::RemoveSlotHandler(g_slotEvents);
            Inventory::RemoveChainHandler(g_chainEvents);
            g_slotEvents = AMOD_INVALID_TOKEN;
            g_chainEvents = AMOD_INVALID_TOKEN;
            Console::Print("bonus: quiet\n");
            return;
        }

        // Asking twice would subscribe twice and say everything twice
        if (g_slotEvents != AMOD_INVALID_TOKEN)
        {
            Console::Print("bonus: already telling\n");
            return;
        }

        g_slotEvents = Inventory::OnSlot(&OnSlotEmptied);
        g_chainEvents = Inventory::OnChain(&OnChain);

        Console::Print(g_slotEvents != AMOD_INVALID_TOKEN
            ? "bonus: telling on every slot that empties and every change of the chain\n"
            : "bonus: this loader does not offer them\n");
        return;
    }

    // Waiting out a totem to see the event is no way to test it
    if (Same(what, "expire"))
    {
        const int slot = argc > 2 ? atoi(argv[2]) : 0;

        const Inventory::Item item = Inventory::At(slot);
        if (item.IsEmpty())
        {
            Console::Printf("bonus: slot %d is empty\n", slot);
            return;
        }

        Console::Printf(Inventory::SetTimeLeft(slot, 0.5f)
            ? "bonus: slot %d has half a second left - watch for 'ran out'\n"
            : "bonus: slot %d has no timer to shorten\n", slot);
        return;
    }

    if (Same(what, "hold"))
    {
        const int slot = argc > 2 ? atoi(argv[2]) : 0;
        const bool ticking = Off(argc, argv, 3);

        Console::Printf(Inventory::SetTicking(slot, ticking)
            ? "bonus: slot %d %s\n" : "bonus: slot %d would not change\n",
            slot, ticking ? "counts down again" : "is held where it stands");
        return;
    }

    if (Same(what, "move"))
    {
        const int from = argc > 2 ? atoi(argv[2]) : 0;
        const int to = argc > 3 ? atoi(argv[3]) : 1;

        const bool moved = Inventory::Move(from, to);
        Console::Printf(moved ? "bonus: slot %d -> %d, icon and timer intact\n"
            : "bonus: %d -> %d refused, try swap\n", from, to);
        return;
    }

    if (Same(what, "swap"))
    {
        const int first = argc > 2 ? atoi(argv[2]) : 0;
        const int second = argc > 3 ? atoi(argv[3]) : 1;

        Console::Printf(Inventory::Swap(first, second)
            ? "bonus: slots %d and %d swapped\n" : "bonus: %d and %d would not swap\n", first, second);
        return;
    }

    Console::Print("bonus: watch [off] | none [off] | mine [carry] | kind [col] [row]\n");
    Console::Print("       borrow [off|bob] | hah [off] | place [name] [col] [row] [texture]\n");
    Console::Print("       rooms | far [room] [col] [row] | prop <n> [key] [value]\n");
    Console::Print("       carried | room | take [tag] | take [col] [row] | takeall [name]\n");
    Console::Print("       move a b | swap a b | events [off] | hold <slot> [off] | expire <slot>\n");
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("bonus", &BonusCommand);

    g_kind = Bonuses::RegisterKind("azforge.mineKind", &OnMineKind);
    g_borrowKind = Bonuses::RegisterKind("azforge.borrowed", [](const AmodBonusKindInfo* info)
    {
        return OnBorrowedBonus(info, nullptr);
    });

    Log(AMOD_LOG_INFO, "%s is up", ModId());
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame* frame)
{
    if (!g_hah || g_dressCount == 0)
    {
        return;
    }

    g_hahTime = g_hahTime + frame->delta;
    if (g_hahTime >= 1.0f)
    {
        g_hahTime = 0.0f;
        g_hahAt = (g_hahAt + 1) % g_dressCount;
    }
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
