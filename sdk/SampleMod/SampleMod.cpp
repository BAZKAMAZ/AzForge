#include <ForgeApi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace Amod;

struct LeverRecord
{
    int kind;
    float column;
    float row;
    int once;
    int on;
    int isPlate;
    float slide;            // 0 up and red, 1 down and green; the frames between are the throw
    char tag[32];
    char needs[32];
    char texture[96];
    char sound[96];
};

struct DoorRecord
{
    int kind;
    float column;
    float row;
    float height;
    int open;               // 0 shut, 1 opening, 2 open, 3 closing
    float went;             // how far it has ridden, kept here so a room re-entered finds it so
    int unlocked;           // the key was spent on it, and it stays open whatever the lever says
    int shrinks;            // which of the two ways it opens, as the style lever had it
    char needs[32];
    char watches[32];       // the lever that opens and shuts it, without any key at all
    char style[32];         // and the lever that says whether it rides up or shrinks
};

static int g_leverKind = 0;
static int g_doorKind = 0;

// A colour belongs to a model, so a fade is a row of models the throw picks its way along
static const int kHandleSteps = 8;

static AmodModelId g_plate = 0;
static AmodModelId g_handle[kHandleSteps] = {};
static const int kDoorSteps = 8;
static AmodModelId g_door = 0;
static AmodModelId g_doorStep[kDoorSteps] = {};   // the same door at eight heights, foot in place

static const float kLeverUp = 0.035f;
static const float kLeverDown = -0.035f;
static const float kLeverSpeed = 4.0f;
static const float kDoorSpeed = 0.5f;

static AmodCtrlId g_against = AMOD_CTRL_NONE;
static int g_againstThisFrame = 0;

static void Face(AmodVertex* into, float halfWidth, float halfHeight, float z, float facing)
{
    const float xs[4] = { -halfWidth, halfWidth, halfWidth, -halfWidth };
    const float ys[4] = { -halfHeight, -halfHeight, halfHeight, halfHeight };
    const float us[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
    const float vs[4] = { 1.0f, 1.0f, 0.0f, 0.0f };

    for (int at = 0; at < 4; ++at)
    {
        into[at].position[0] = xs[at];
        into[at].position[1] = ys[at];
        into[at].position[2] = z;
        into[at].normal[0] = 0.0f;
        into[at].normal[1] = 0.0f;
        into[at].normal[2] = facing;
        into[at].uv[0] = us[at];
        into[at].uv[1] = vs[at];
    }
}

static AmodModelId BuildBox(const char* name, float halfWidth, float halfHeight, float halfDepth, uint32_t colour)
{
    AmodVertex vertices[8] = {};
    Face(vertices, halfWidth, halfHeight, -halfDepth, -1.0f);
    Face(vertices + 4, halfWidth, halfHeight, halfDepth, 1.0f);

    static const uint16_t kIndices[] = {
        0, 1, 2,  0, 2, 3,   2, 1, 0,  3, 2, 0,
        5, 4, 7,  5, 7, 6,   7, 4, 5,  6, 7, 5,
        4, 5, 1,  4, 1, 0,   1, 5, 4,  0, 1, 4,
        3, 2, 6,  3, 6, 7,   6, 2, 3,  7, 6, 3,
        4, 0, 3,  4, 3, 7,   3, 0, 4,  7, 3, 4,
        1, 5, 6,  1, 6, 2,   6, 5, 1,  2, 6, 1
    };

    const AmodModelId made = Mmd::Create(name, Mmd::Mesh(vertices, 8, kIndices, sizeof(kIndices) / sizeof(kIndices[0])));

    if (made != 0)
    {
        Mmd::RebuildNormals(made);
        Mmd::SetColour(made, colour);
    }
    return made;
}

static AmodModelId BuildPlate(const char* name, float halfWidth, float halfHeight, uint32_t colour)
{
    AmodVertex plate[4] = {};
    Face(plate, halfWidth, halfHeight, 0.0f, -1.0f);

    static const uint16_t kBothWays[] = { 0, 1, 2,  0, 2, 3,  2, 1, 0,  3, 2, 0 };
    const AmodModelId made = Mmd::Create(name, Mmd::Mesh(plate, 4, kBothWays, 12));

    if (made != 0)
    {
        Mmd::SetColour(made, colour);
    }
    return made;
}

static AmodModelId CopyModel(const char* name, const char* path, uint32_t colour)
{
    const AmodModelId source = Mmd::Load(path);
    AmodModel about = {};

    if (source == 0 || !Mmd::Get(source, about))
    {
        return 0;
    }

    AmodVertex vertices[2048];
    uint16_t indices[4096];

    const int vertexCount = Mmd::ReadVertices(source, 0, vertices, 2048);
    const int indexCount = Mmd::ReadIndices(source, 0, indices, 4096);

    if (vertexCount <= 0 || indexCount <= 0)
    {
        return 0;
    }

    const AmodModelId made = Mmd::Create(name,
        Mmd::Mesh(vertices, vertexCount, indices, indexCount));

    if (made != 0)
    {
        Mmd::SetColour(made, colour);
    }
    return made;
}

static void BuildShapes()
{
    g_plate = BuildPlate("lever_plate", 0.05f, 0.10f, 0xFF404448);

    for (int at = 0; at < kHandleSteps; ++at)
    {
        const float part = static_cast<float>(at) / (kHandleSteps - 1);
        const uint32_t red = static_cast<uint32_t>(0xD0 + (0x30 - 0xD0) * part);
        const uint32_t green = static_cast<uint32_t>(0x30 + (0xC0 - 0x30) * part);

        char name[32];
        snprintf(name, sizeof(name), "lever_%d", at);
        g_handle[at] = BuildBox(name, 0.035f, 0.022f, 0.05f, 0xFF000000 | (red << 16) | (green << 8) | 0x28);
    }

    g_door = CopyModel("door_purple", "models/ctrls/door.mmd", 0xFF9040C8);

    AmodVertex source[2048];
    uint16_t indices[4096];

    const int vertexCount = Mmd::ReadVertices(g_door, 0, source, 2048);
    const int indexCount = Mmd::ReadIndices(g_door, 0, indices, 4096);

    float foot = 0.0f;
    for (int at = 0; at < vertexCount; ++at)
    {
        foot = at == 0 || source[at].position[1] < foot ? source[at].position[1] : foot;
    }

    for (int step = 0; step < kDoorSteps; ++step)
    {
        const float left = 1.0f - static_cast<float>(step) / (kDoorSteps - 1);
        AmodVertex shrunk[2048];

        for (int at = 0; at < vertexCount; ++at)
        {
            shrunk[at] = source[at];
            shrunk[at].position[1] = foot + (source[at].position[1] - foot) * left;
        }

        char name[32];
        snprintf(name, sizeof(name), "door_step_%d", step);
        g_doorStep[step] = Mmd::Create(name,
            Mmd::Mesh(shrunk, vertexCount, indices, indexCount));

        if (g_doorStep[step] != 0)
        {
            Mmd::SetColour(g_doorStep[step], 0xFF9040C8);
        }
    }
}

static AmodModelId DoorAt(float part)
{
    const int step = static_cast<int>(part * (kDoorSteps - 1) + 0.5f);
    return g_doorStep[step < 0 ? 0 : (step >= kDoorSteps ? kDoorSteps - 1 : step)];
}

static AmodModelId HandleAt(float slide)
{
    const int step = static_cast<int>(slide * (kHandleSteps - 1) + 0.5f);
    return g_handle[step < 0 ? 0 : (step >= kHandleSteps ? kHandleSteps - 1 : step)];
}

static void CopyKey(char* into, size_t room, const Data::Block& block, const char* key, const char* fallback)
{
    const char* const text = block.IsValid() ? block[key].Text(fallback) : fallback;
    size_t at = 0;
    for (; text[at] != '\0' && at + 1 < room; ++at)
    {
        into[at] = text[at];
    }
    into[at] = '\0';
}

static int __cdecl Lever(const AmodCtrlEventInfo* info, void*)
{
    LeverRecord* const lever =
        const_cast<LeverRecord*>(static_cast<const LeverRecord*>(info->record));

    if (info->event == AMOD_CTRL_EVENT_PARSE)
    {
        const Data::Block block = Data::Parse(info->text);
        LeverRecord made = {};

        made.column = block.IsValid() ? block["cell_pos"].Number(0, 20.0f) : 20.0f;
        made.row = block.IsValid() ? block["cell_pos"].Number(1, 10.0f) : 10.0f;
        made.once = block.IsValid() ? block["once"].Int(0) : 0;
        made.on = block.IsValid() ? block["active"].Int(0) : 0;
        made.slide = made.on != 0 ? 1.0f : 0.0f;

        CopyKey(made.tag, sizeof(made.tag), block, "tag", "");
        CopyKey(made.needs, sizeof(made.needs), block, "needs", "");
        CopyKey(made.texture, sizeof(made.texture), block, "texture", "textures/ctrls/restore_plat.tga");
        CopyKey(made.sound, sizeof(made.sound), block, "sound", "sounds/res_plat_up.wav");

        LeverRecord board = made;
        board.isPlate = 1;
        Ctrls::Make(-1, g_leverKind, &board, sizeof(board));
        Ctrls::Make(-1, g_leverKind, &made, sizeof(made));

        if (block.IsValid())
        {
            block.Free();
        }
        return 1;
    }

    if (info->event == AMOD_CTRL_EVENT_BUILD)
    {
        AmodCtrlBody* const body = info->body;
        const float lift = lever->isPlate != 0
            ? 0.0f : kLeverUp + (kLeverDown - kLeverUp) * lever->slide;

        body->name = lever->isPlate != 0 ? "lever_plate" : "lever";
        body->position[0] = World::ColumnLeft(static_cast<int>(lever->column)) + 0.05f;
        body->position[1] = World::RowTop(static_cast<int>(lever->row)) - 0.05f + lift;
        body->position[2] = lever->isPlate != 0 ? 0.045f : 0.055f;   // nearer the camera is greater z

        body->box.structSize = sizeof(body->box);
        body->box.min[0] = -0.05f; body->box.min[1] = -0.03f; body->box.min[2] = -0.06f;
        body->box.max[0] = 0.05f;  body->box.max[1] = 0.03f;  body->box.max[2] = 0.06f;
        body->isSolid = 0;

        body->texture = Textures::Load(lever->texture);
        body->model = lever->isPlate != 0 ? g_plate : HandleAt(lever->slide);
        body->tag = lever->isPlate != 0 ? "" : lever->tag;
        return 1;
    }

    if (info->event == AMOD_CTRL_EVENT_UPDATE)
    {
        if (lever->isPlate != 0)
        {
            return 0;
        }

        Ctrls::SetTagOn(info->frame->index, lever->on != 0);

        const float wanted = lever->on != 0 ? 1.0f : 0.0f;
        if (lever->slide != wanted)
        {
            const float step = kLeverSpeed * info->frame->delta;
            lever->slide += lever->slide < wanted ? step : -step;
            lever->slide = lever->slide < 0.0f ? 0.0f : (lever->slide > 1.0f ? 1.0f : lever->slide);

            Ctrls::SetLook(info->frame->index, HandleAt(lever->slide), 0);

            Ctrls::SetPosition(info->frame->index, info->frame->position[0], World::RowTop(static_cast<int>(lever->row)) - 0.05f +
                                     kLeverUp + (kLeverDown - kLeverUp) * lever->slide, info->frame->position[2]);
        }
        return 0;
    }

    if (info->event != AMOD_CTRL_EVENT_TOUCH || !info->touch->isPlayer || lever->isPlate != 0)
    {
        return AMOD_CTRL_TOUCH_HANDLED;
    }
    if (lever->once != 0 && lever->on != 0)
    {
        return AMOD_CTRL_TOUCH_HANDLED;
    }
    if (lever->needs[0] != '\0' && !Inventory::IsCarrying(lever->needs))
    {
        return AMOD_CTRL_TOUCH_HANDLED;
    }

    // Only on the way in, or it would throw itself on every frame he stands against it
    g_againstThisFrame = 1;
    if (g_against == info->id)
    {
        return AMOD_CTRL_TOUCH_HANDLED;
    }
    g_against = info->id;

    // Only the state is set here; the update walks it there, which is what makes it a throw
    lever->on = lever->on != 0 ? 0 : 1;
    Ctrls::SetTagOn(info->touch->index, lever->on != 0);

    if (lever->sound[0] != '\0')
    {
        Sound::Play(Sound::Load(lever->sound));
    }
    return AMOD_CTRL_TOUCH_HANDLED;
}

static void MakeDoorsIn(int roomIndex)
{
    char cells[AMOD_ROOM_COLUMNS * AMOD_ROOM_ROWS] = {};
    World::Cells(roomIndex, cells, sizeof(cells));

    for (int col = 0; col < AMOD_ROOM_COLUMNS; ++col)
    {
        for (int row = 0; row < AMOD_ROOM_ROWS; ++row)
        {
            if (cells[row * AMOD_ROOM_COLUMNS + col] != 'Q')
            {
                continue;
            }

            int run = 0;
            while (row + run < AMOD_ROOM_ROWS &&
                   cells[(row + run) * AMOD_ROOM_COLUMNS + col] == 'Q')
            {
                ++run;
            }

            // Struck out of our copy, or the run is found again from its second cell
            for (int at = 0; at < run; ++at)
            {
                cells[(row + at) * AMOD_ROOM_COLUMNS + col] = ' ';
            }

            DoorRecord made = {};
            made.column = static_cast<float>(col);
            made.row = static_cast<float>(row);
            made.height = static_cast<float>(run);

            const Data::Block nothing;
            CopyKey(made.needs, sizeof(made.needs), nothing, "", "key_e");
            CopyKey(made.watches, sizeof(made.watches), nothing, "", "door_lever");
            CopyKey(made.style, sizeof(made.style), nothing, "", "e_bridge");
            Ctrls::Make(roomIndex, g_doorKind, &made, sizeof(made));
        }
    }
}

static int __cdecl PurpleDoor(const AmodCtrlEventInfo* info, void*)
{
    DoorRecord* const door = const_cast<DoorRecord*>(static_cast<const DoorRecord*>(info->record));

    if (info->event == AMOD_CTRL_EVENT_BUILD)
    {
        AmodCtrlBody* const body = info->body;
        const float tall = door->height * 0.1f;

        body->name = "purple_door";
        body->position[0] = World::ColumnLeft(static_cast<int>(door->column)) + 0.05f;
        body->position[1] = World::RowTop(static_cast<int>(door->row + door->height - 1.0f)) - 0.05f + door->went;
        body->position[2] = 0.05f;

        // Box_ForDoor's own shape: the bottom cell is the origin and it grows up from there
        body->box.structSize = sizeof(body->box);
        body->box.min[0] = -0.05f; body->box.min[1] = -0.05f; body->box.min[2] = -0.05f;
        body->box.max[0] = 0.05f;  body->box.max[1] = tall - 0.05f; body->box.max[2] = 0.25f;

        body->isSolid = door->open == 0 ? 1 : 0;
        body->model = g_door;
        body->texture = Textures::Load("textures/ctrls/door_white.tga");
        return 1;
    }

    if (info->event == AMOD_CTRL_EVENT_UPDATE)
    {
        const float tall = door->height * 0.1f;
        AmodCtrl thrown;

        const int watch = Ctrls::FindByTag(door->watches);
        const bool wanted = door->unlocked != 0 ||
            (watch >= 0 && Ctrls::Get(watch, thrown) && thrown.tagOn != 0);

        // Which way it goes is asked while it is shut, so it does not change halfway through
        if (door->open == 0)
        {
            const int style = Ctrls::FindByTag(door->style);
            door->shrinks = style >= 0 && Ctrls::Get(style, thrown) && thrown.tagOn != 0 ? 1 : 0;
        }

        if (wanted && door->open != 2)
        {
            door->open = 1;
            Ctrls::SetTouchable(info->frame->index, false);
        }
        else if (!wanted && door->open != 0)
        {
            door->open = 3;
        }

        if (door->open == 1 || door->open == 3)
        {
            const float step = kDoorSpeed * info->frame->delta;
            door->went += door->open == 1 ? step : -step;

            if (door->went >= tall) { door->went = tall; door->open = 2; }
            if (door->went <= 0.0f)
            {
                door->went = 0.0f;
                door->open = 0;
                Ctrls::SetTouchable(info->frame->index, true);
            }
        }

        const float part = tall > 0.0f ? door->went / tall : 0.0f;
        const float foot = World::RowTop(static_cast<int>(door->row + door->height - 1.0f)) - 0.05f;

        if (door->shrinks != 0)
        {
            AmodBox box = {};
            box.structSize = sizeof(box);
            box.min[0] = -0.05f; box.min[1] = -0.05f; box.min[2] = -0.05f;
            box.max[0] = 0.05f;  box.max[1] = tall * (1.0f - part) - 0.05f; box.max[2] = 0.25f;

            Ctrls::SetLook(info->frame->index, DoorAt(part), 0);
            Ctrls::SetBox(info->frame->index, box);
            Ctrls::SetPosition(info->frame->index, info->frame->position[0], foot, info->frame->position[2]);
        }
        else
        {
            Ctrls::SetLook(info->frame->index, g_door, 0);
            Ctrls::SetPosition(info->frame->index, info->frame->position[0], foot + door->went, info->frame->position[2]);
        }
        return 0;
    }

    if (info->event != AMOD_CTRL_EVENT_TOUCH || !info->touch->isPlayer || door->open != 0)
    {
        return AMOD_CTRL_TOUCH_HANDLED;
    }
    if (!Inventory::IsCarrying(door->needs))
    {
        return AMOD_CTRL_TOUCH_HANDLED;
    }

    const int slot = Inventory::SlotOf(door->needs);
    if (slot >= 0)
    {
        Inventory::Clear(slot);
    }

    door->unlocked = 1;
    door->open = 1;
    Ctrls::SetTouchable(info->touch->index, false);
    Sound::Play(Sound::Load("sounds/res_plat_down.wav"));
    return AMOD_CTRL_TOUCH_HANDLED;
}

static void __cdecl OnWorld(const AmodWorldEventInfo* info, void*)
{
    if (info->event == AMOD_WORLD_EVENT_LEVEL_LOADED)
    {
        for (int room = 0; room < World::RoomCount(); ++room)
        {
            MakeDoorsIn(room);
        }
    }
}

static float g_swing = 0.0f;

static void __cdecl DriveHandler(const AmodCtrlFrame* frame, void*)
{
    g_swing += frame->delta;

    const float wave = g_swing - static_cast<int>(g_swing);
    const float step = (wave < 0.5f ? wave : 1.0f - wave) * 0.4f - 0.1f;

    Ctrls::SetPosition(frame->index, frame->position[0] + step * frame->delta, frame->position[1], frame->position[2]);
}

static int g_eatTouch = 0;
static int g_touchedThisFrame = 0;
static int g_wasToucher = -1;
static int g_wasSide = 0;

// Every overlapping frame is a call, so only a changed meeting is worth a line
static AmodCtrlTouchResult __cdecl TouchHandler(const AmodCtrlTouchInfo* info, void* user)
{
    g_touchedThisFrame = 1;

    if (info->toucher != g_wasToucher || info->side != g_wasSide)
    {
        static const char* const kSides[] = { "?", "above", "below", "left", "right" };
        Console::Printf("%s met %s from %s\n", info->name,
            info->isPlayer ? "the player" : info->toucherName,
            kSides[info->side >= 0 && info->side <= 4 ? info->side : 0]);

        g_wasToucher = info->toucher;
        g_wasSide = info->side;
    }

    const int* const eat = static_cast<const int*>(user);
    return eat != nullptr && *eat != 0 ? AMOD_CTRL_TOUCH_HANDLED : AMOD_CTRL_TOUCH_DEFAULT;
}

static void Say(bool done, const char* said)
{
    Console::Printf(done ? "%s\n" : "ctrl: %s\n", done ? said : LastError());
}

static void __cdecl CtrlCommand(int argc, const char** argv, void*)
{
    if (argc < 2)
    {
        const int total = Ctrls::Count();
        Console::Printf("%d ctrl(s)\n", total);

        for (int at = 0; at < total; ++at)
        {
            AmodCtrl ctrl;
            if (Ctrls::Get(at, ctrl))
            {
                Console::Printf("  %2d #%-4u %-14s cell %2d %2d  %-12s %s %s\n",
                    at, ctrl.id, ctrl.name, ctrl.column, ctrl.row, ctrl.tag,
                    ctrl.tag[0] == '\0' ? " " : (ctrl.tagOn ? "on " : "off"),
                    ctrl.isHidden ? "hidden" : "");
            }
        }
        return;
    }

    if (strcmp(argv[1], "room") == 0 && argc > 2)
    {
        const int where = atoi(argv[2]);

        if (argc > 5)
        {
            Say(Ctrls::SetPlacementFloat(atoi(argv[3]), argv[4],
                                               static_cast<float>(atof(argv[5])), where), argv[4]);
            return;
        }

        const int held = Ctrls::PlacementCount(where);
        Console::Printf("room %d holds %d placement(s)\n", where, held);

        for (int at = 0; at < held; ++at)
        {
            AmodCtrlPlacement what;
            if (Ctrls::GetPlacement(at, what, where))
            {
                Console::Printf("  %2d #%-4u %-14s %s\n", at, what.id, what.kindName, what.tag);
            }
        }
        return;
    }

    if (strcmp(argv[1], "id") == 0 && argc > 2)
    {
        const AmodCtrlId id = static_cast<AmodCtrlId>(atoi(argv[2]));
        const int room = Ctrls::RoomOf(id);
        const int index = Ctrls::IndexOf(id);
        AmodCtrl ctrl;

        if (room < 0)
        {
            Console::Printf("#%u means nothing\n", id);
        }
        else if (index < 0)
        {
            Console::Printf("#%u is in room %d, not standing\n", id, room);
        }
        else if (Ctrls::Get(index, ctrl))
        {
            Console::Printf("#%u is %s, ctrl %d of room %d\n", id, ctrl.name, index, room);
        }
        return;
    }

    if (strcmp(argv[1], "tag") == 0 && argc > 2)
    {
        const int found = Ctrls::FindByTag(argv[2]);
        AmodCtrl wearer;

        if (found >= 0 && Ctrls::Get(found, wearer))
        {
            Console::Printf("%s is %s, worn by %s (ctrl %d)\n",
                                  argv[2], wearer.tagOn ? "on" : "off", wearer.name, found);
        }
        else
        {
            Console::Printf("nothing here wears %s\n", argv[2]);
        }
        return;
    }

    // A thing of the game's, taken whole: its mechanic stays, and the mod hears it alongside
    if (strcmp(argv[1], "borrow") == 0 && argc > 3)
    {
        char block[512];
        snprintf(block, sizeof(block),
            "$restore_plat\n{\n  $origin { $cell_pos %s %s }\n"
            "  $model \"models/ctrls/restore_plat.mmd\"\n"
            "  $texture \"textures/ctrls/restore_plat.tga\"\n}\n", argv[2], argv[3]);

        const AmodCtrlId id = Ctrls::Borrow(-1, block,
            [](const AmodCtrlEventInfo* info, void*) -> int
            {
                if (info->event == AMOD_CTRL_EVENT_TOUCH && info->touch->isPlayer)
                {
                    Console::Printf("#%u was stood on\n", info->id);
                }
                return AMOD_CTRL_TOUCH_DEFAULT;
            });

        Console::Printf(id != AMOD_CTRL_NONE ? "borrowed #%u\n" : "ctrl: %s\n", id != AMOD_CTRL_NONE ? id : 0);
        return;
    }

    if (strcmp(argv[1], "lever") == 0 && argc > 3)
    {
        LeverRecord lever = {};
        lever.column = static_cast<float>(atoi(argv[2]));
        lever.row = static_cast<float>(atoi(argv[3]));

        const Data::Block nothing;
        CopyKey(lever.tag, sizeof(lever.tag), nothing, "", argc > 4 ? argv[4] : "e_bridge");
        CopyKey(lever.texture, sizeof(lever.texture), nothing, "", "textures/ctrls/restore_plat.tga");
        CopyKey(lever.sound, sizeof(lever.sound), nothing, "", "sounds/res_plat_up.wav");

        LeverRecord board = lever;
        board.isPlate = 1;
        Ctrls::Make(-1, g_leverKind, &board, sizeof(board));

        const AmodCtrlId id = Ctrls::Make(-1, g_leverKind, &lever, sizeof(lever));
        Console::Printf(id != AMOD_CTRL_NONE ? "lever #%u for '%s'\n" : "ctrl: %s\n",
                              id != AMOD_CTRL_NONE ? id : 0, lever.tag);
        return;
    }

    // Its models out of the room, then its cells, which is what it was ever climbed by
    if (strcmp(argv[1], "strip") == 0 && argc > 3)
    {
        const int col = atoi(argv[2]);
        const int row = atoi(argv[3]);
        int taken = 0;

        // Backwards, because taking one out moves the numbers of everything after it up
        for (int at = World::StaticCount() - 1; at >= 0; --at)
        {
            AmodRoomStatic piece;
            if (!World::GetStatic(at, piece))
            {
                continue;
            }

            const int pieceCol = World::ColumnAt(piece.matrix[12]);
            const int pieceRow = World::RowAt(piece.matrix[13]);

            if (pieceCol >= col && pieceCol <= col + 1 && pieceRow >= row && pieceRow < row + 8)
            {
                taken += World::RemoveStatic(at) ? 1 : 0;
            }
        }

        for (int at = row; at < row + 8 && at < AMOD_ROOM_ROWS; ++at)
        {
            World::SetCell(col, at, ' ');
            World::SetCell(col + 1, at, ' ');
        }

        Console::Printf("%d model(s) gone\n", taken);
        return;
    }

    if (strcmp(argv[1], "new") == 0 && argc > 3)
    {
        char block[512];

        if (argc > 4 && strcmp(argv[4], "ladder") == 0)
        {
            const int col = atoi(argv[2]);
            const int row = atoi(argv[3]);
            char grid[AMOD_ROOM_COLUMNS * AMOD_ROOM_ROWS] = {};

            if (col < 0 || col + 1 >= AMOD_ROOM_COLUMNS)
            {
                Console::Printf("a ladder wants two columns\n");
                return;
            }

            for (int at = row; at < row + 8 && at < AMOD_ROOM_ROWS; ++at)
            {
                const char piece = at == row + 7 ? '=' : '-';   // the marked one leaves an entity
                grid[at * AMOD_ROOM_COLUMNS + col] = piece;
                grid[at * AMOD_ROOM_COLUMNS + col + 1] = piece;
            }

            snprintf(block, sizeof(block),
                "$ladder\n{\n  $model \"models/ctrls/ladder.mmd\"\n"
                "  $texture \"textures/ctrls/ladder.tga\"\n}\n");

            Say(Ctrls::CreateOn(block, grid, sizeof(grid)) != 0, "a ladder stands");
            return;
        }

        snprintf(block, sizeof(block),
            "$restore_plat\n{\n  $alight 0\n  $origin { $cell_pos %s %s }\n"
            "  $model \"models/ctrls/restore_plat.mmd\"\n"
            "  $texture \"textures/ctrls/restore_plat.tga\"\n"
            "  $fall_speed 0.3\n  $elevate_speed 0.5\n  $fall_dist 12\n}\n", argv[2], argv[3]);

        Say(Ctrls::Create(block) > 0, "made");
        return;
    }

    const int index = atoi(argv[1]);
    AmodCtrl ctrl;
    if (!Ctrls::Get(index, ctrl))
    {
        Console::Printf("ctrl: %s\n", LastError());
        return;
    }

    if (argc == 2)
    {
        Console::Printf("%s at %+.3f %+.3f, cell %d %d\n",
            ctrl.name, ctrl.position[0], ctrl.position[1], ctrl.column, ctrl.row);

        for (int at = 0; at < Ctrls::PropertyCount(index); ++at)
        {
            const char* const key = Ctrls::PropertyNameAt(index, at);
            AmodValue value = {};

            if (Ctrls::GetProperty(index, key, value))
            {
                Console::Printf("  %-14s %.3f\n", key,
                    value.kind == AMOD_VALUE_FLOAT ? value.numbers[0]
                                                   : static_cast<float>(value.integer));
            }
        }
        return;
    }

    if (strcmp(argv[2], "drive") == 0)  { Say(Ctrls::Drive(index, &DriveHandler), "driven"); return; }
    if (strcmp(argv[2], "freeze") == 0) { Say(Ctrls::Freeze(index), "frozen"); return; }
    if (strcmp(argv[2], "game") == 0)   { Say(Ctrls::Release(index), "given back"); return; }
    if (strcmp(argv[2], "remove") == 0) { Say(Ctrls::Remove(index), "taken away"); return; }
    if (strcmp(argv[2], "hide") == 0)   { Say(Ctrls::SetHidden(index, true), "hidden"); return; }
    if (strcmp(argv[2], "show") == 0)   { Say(Ctrls::SetHidden(index, false), "shown"); return; }

    // "eat" answers HANDLED, so the thing's own handler never runs and it stops doing its job
    if (strcmp(argv[2], "touch") == 0 || strcmp(argv[2], "eat") == 0 || strcmp(argv[2], "deaf") == 0)
    {
        g_eatTouch = strcmp(argv[2], "eat") == 0 ? 1 : 0;

        Say(strcmp(argv[2], "deaf") == 0 ? Ctrls::OnTouch(index, nullptr)
                                         : Ctrls::OnTouch(index, &TouchHandler, &g_eatTouch),
            argv[2]);
        return;
    }

    // Hung on the id, not the entity: the entity is freed the moment the room is left
    if (strcmp(argv[2], "needs") == 0 && argc > 3)
    {
        const Data::Block lock = Data::On(reinterpret_cast<void*>(ctrl.id));
        if (lock.IsValid())
        {
            lock["needs"].Set(argv[3]);
        }
        Say(lock.IsValid(), argv[3]);
        return;
    }

    if (strcmp(argv[2], "unlock") == 0)
    {
        Data::Detach(reinterpret_cast<void*>(ctrl.id));
        Say(Ctrls::Release(index), "free again");
        return;
    }

    if (strcmp(argv[2], "tagas") == 0 && argc > 3)
    {
        Say(Ctrls::SetTag(index, argv[3]), argv[3]);
        return;
    }

    if (strcmp(argv[2], "tagon") == 0 && argc > 3)
    {
        Say(Ctrls::SetTagOn(index, atoi(argv[3]) != 0), argv[3]);
        return;
    }

    if (argc < 4)
    {
        Console::Printf("ctrl <n> <key> <value>\n");
        return;
    }

    if (strcmp(argv[2], "box") == 0 && argc > 9)
    {
        AmodBox box = {};
        box.structSize = sizeof(box);

        for (int axis = 0; axis < 3; ++axis)
        {
            box.min[axis] = static_cast<float>(atof(argv[3 + axis]));
            box.max[axis] = static_cast<float>(atof(argv[6 + axis]));
        }

        Say(Ctrls::SetBox(index, box), "box set");
        return;
    }

    // color wants four numbers, so as many as were typed are sent
    AmodValue value = {};
    value.kind = AMOD_VALUE_FLOAT;
    value.count = 0;

    for (int at = 3; at < argc && value.count < 4; ++at, ++value.count)
    {
        value.numbers[value.count] = static_cast<float>(atof(argv[at]));
    }

    Say(Ctrls::SetProperty(index, argv[2], value), argv[2]);
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Console::AddCommand("ctrl", &CtrlCommand);
    BuildShapes();

    g_leverKind = Ctrls::RegisterKind("lever", &Lever);
    g_doorKind = Ctrls::RegisterKind("purple_door", &PurpleDoor);
    World::OnEvent(AMOD_WORLD_EVENT_LEVEL_LOADED, &OnWorld);
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
    if (g_touchedThisFrame == 0)
    {
        g_wasToucher = -1;
        g_wasSide = 0;
    }
    g_touchedThisFrame = 0;

    if (g_againstThisFrame == 0)
    {
        g_against = AMOD_CTRL_NONE;
    }
    g_againstThisFrame = 0;

    // Anything the mod has locked stands still until its item is carried
    const int total = Ctrls::Count();
    for (int at = 0; at < total; ++at)
    {
        AmodCtrl ctrl;
        if (!Ctrls::Get(at, ctrl))
        {
            continue;
        }

        const Data::Block lock = Data::Find(reinterpret_cast<void*>(ctrl.id));
        if (!lock.IsValid() || !lock.Has("needs"))
        {
            continue;
        }

        const bool carried = Inventory::IsCarrying(lock["needs"].Text());
        const bool held = Ctrls::Driver(at) == AMOD_CTRL_DRIVER_NOBODY;

        if (carried && held)
        {
            Ctrls::Release(at);
        }
        else if (!carried && !held)
        {
            Ctrls::Freeze(at);
        }
    }
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
