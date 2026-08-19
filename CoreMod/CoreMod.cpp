#include <d3d8.h>
#include <ForgeApi.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// The left window of the script, in the game's 640x480 space with y growing upwards.
static const float kListLeft = 20.0f;
static const float kListTop = 400.0f;
static const float kListWidth = 380.0f;
static const float kListHeight = 285.0f;

// The rail down its right edge, and the grid of tiles left of the rail.
static const float kRailWidth = 12.0f;
static const float kRailX = kListLeft + kListWidth - kRailWidth - 6.0f;
static const float kGridLeft = kListLeft + 8.0f;
static const float kGridTop = kListTop - 8.0f;
static const float kGridWidth = kRailX - 6.0f - kGridLeft;
static const float kGridHeight = kListHeight - 16.0f;

static const int kColumns = 3;
static const int kRows = 2;
static const float kIcon = 84.0f;

// The two lines under the icon, measured from its centre.
static const float kNameGap = 18.0f;
static const float kAuthorGap = 34.0f;

// How much of a name fits in a cell, and in the panel, at the scales the script sets. The stone
// font is wide and has no ellipsis worth drawing, so it is a plain cut.
static const size_t kTileNameChars = 14;
static const size_t kPanelWidthChars = 22;
static const int kPanelLines = 12;

// Where a tile that is not on the page is parked: off the screen, so it neither draws nor
// catches a click whatever the visibility flag turns out to govern.
static const float kOffScreen = -500.0f;

struct ModTile
{
    int index = 0;
    std::unique_ptr<Amod::Render::Texture> icon;
    bool iconTried = false;
    AmodToken draw = AMOD_INVALID_TOKEN;
};

static std::vector<ModTile> g_tiles;
static AmodToken g_railDraw = AMOD_INVALID_TOKEN;
static bool g_built = false;
static int g_selected = 0;
static int g_firstRow = 0;
static int g_laidOut = -1;
static bool g_dragging = false;

static float CellWidth()
{
    return kGridWidth / kColumns;
}

static float CellHeight()
{
    return kGridHeight / kRows;
}

static int RowCount()
{
    const int count = static_cast<int>(g_tiles.size());
    return (count + kColumns - 1) / kColumns;
}

static int MaxFirstRow()
{
    const int rows = RowCount();
    return rows > kRows ? rows - kRows : 0;
}

static void RailRect(float& top, float& height)
{
    top = kGridTop;
    height = kGridHeight;
}

static bool TileRect(int index, float& cx, float& cy)
{
    const int column = index % kColumns;
    const int row = index / kColumns - g_firstRow;

    if (row < 0 || row >= kRows)
    {
        cx = kOffScreen;
        cy = kOffScreen;
        return false;
    }

    cx = kGridLeft + CellWidth() * (column + 0.5f);
    cy = kGridTop - CellHeight() * row - kIcon * 0.5f + 4.0f;
    return true;
}

static std::string Cut(const char* text, size_t width)
{
    std::string result = text != nullptr ? text : "";
    return result.size() > width ? result.substr(0, width) : result;
}

static int LineCount(const std::string& text)
{
    int lines = 0;
    for (const char c : text)
    {
        lines += c == '\n' ? 1 : 0;
    }
    return lines;
}

// Adds one line, broken at spaces to fit the panel
static void AddLine(std::string& text, const std::string& line)
{
    std::string rest = line;

    while (LineCount(text) < kPanelLines)
    {
        if (rest.size() <= kPanelWidthChars)
        {
            text += rest + "\n";
            return;
        }

        size_t cut = rest.rfind(' ', kPanelWidthChars);
        if (cut == std::string::npos || cut == 0)
        {
            cut = kPanelWidthChars;
        }

        text += rest.substr(0, cut) + "\n";
        rest = rest.substr(rest[cut] == ' ' ? cut + 1 : cut);
    }
}

static void WriteDetail()
{
    AmodModEntry entry;
    if (!Amod::Mods::Get(g_selected, entry))
    {
        return;
    }

    Amod::Menu::Element name = Amod::Menu::Find("IDM_MODS", "ID_MODS_NAME");
    if (name.IsValid())
    {
        name["text"].Set(Cut(entry.name, kPanelWidthChars).c_str());
    }

    std::string text;
    AddLine(text, std::string("by ") + (entry.author[0] != '\0' ? entry.author : "nobody"));
    AddLine(text, std::string("version ") + entry.version);
    AddLine(text, std::string("API ") + std::to_string(entry.apiVersion) + ", " + entry.archive);

    if (entry.description[0] != '\0')
    {
        AddLine(text, "");
        AddLine(text, entry.description);
    }

    AddLine(text, "");
    if (entry.capabilityCount > 0)
    {
        AddLine(text, "it says it will:");
        for (int c = 0; c < entry.capabilityCount; ++c)
        {
            AddLine(text, std::string("  ") + Amod::Mods::Capability(g_selected, c));
        }
    }
    else
    {
        AddLine(text, "it declares nothing");
    }

    const char* const failure = Amod::Mods::FailureReason(g_selected);
    if (failure[0] != '\0')
    {
        AddLine(text, std::string("refused: ") + failure);
    }

    if (entry.needsRestart)
    {
        AddLine(text, "restart to apply");
    }
    else if (entry.enabled != entry.loaded)
    {
        AddLine(text, entry.enabled != 0 ? "loads when you leave" : "unloads when you leave");
    }

    Amod::Menu::Element detail = Amod::Menu::Find("IDM_MODS", "ID_MODS_DETAIL");
    if (detail.IsValid())
    {
        detail["text"].Set(text.c_str());
    }

    Amod::Menu::Element toggle = Amod::Menu::Find("IDM_MODS", "ID_MODS_TOGGLE");
    if (toggle.IsValid())
    {
        toggle["text"].Set(entry.enabled ? "DISABLE" : "ENABLE");
    }
}

static void TileId(char* buffer, size_t size, const char* prefix, int index)
{
    snprintf(buffer, size, "%s%d", prefix, index);
}

static void LayoutTiles()
{
    for (size_t i = 0; i < g_tiles.size(); ++i)
    {
        const int index = g_tiles[i].index;

        float cx = 0.0f;
        float cy = 0.0f;
        const bool onPage = TileRect(index, cx, cy);

        AmodModEntry entry;
        const bool known = Amod::Mods::Get(index, entry);

        char id[32];
        TileId(id, sizeof(id), "ID_MOD_", index);
        Amod::Menu::Element button = Amod::Menu::Find("IDM_MODS", id);
        if (button.IsValid())
        {
            button["pos"].Set(cx, cy);
            button["visible"].Set(onPage);
        }

        TileId(id, sizeof(id), "ID_MOD_NAME_", index);
        Amod::Menu::Element name = Amod::Menu::Find("IDM_MODS", id);
        if (name.IsValid())
        {
            name["pos"].Set(cx, cy - kIcon * 0.5f - kNameGap);
            name["text"].Set(onPage && known ? Cut(entry.name, kTileNameChars).c_str() : "");
        }

        TileId(id, sizeof(id), "ID_MOD_AUTHOR_", index);
        Amod::Menu::Element author = Amod::Menu::Find("IDM_MODS", id);
        if (author.IsValid())
        {
            author["pos"].Set(cx, cy - kIcon * 0.5f - kAuthorGap);
            author["text"].Set(onPage && known ? Cut(entry.author, kTileNameChars).c_str() : "");
        }
    }

    g_laidOut = g_firstRow;
}

static void __cdecl DrawTile(AmodMenuItem item, AmodElementPhase phase, void* user);
static void __cdecl DrawRail(AmodMenuItem item, AmodElementPhase phase, void* user);

static void BuildRail()
{
    float railTop = 0.0f;
    float railHeight = 0.0f;
    RailRect(railTop, railHeight);

    char fragment[1024];
    snprintf(fragment, sizeof(fragment),
             "$img_buttons\n"
             "{\n"
             "  $button\n"
             "  {\n"
             "    $id \"ID_MODS_RAIL\"\n"
             "    $pos %.0f %.0f\n"
             "    $rect_size %.0f %.0f\n"
             "    $h_align \"center\"\n"
             "    $v_align \"center\"\n"
             "    $img_enabled \"textures/frame/a40.png\"\n"
             "    $img_disabled \"textures/frame/a40.png\"\n"
             "    $help \"\"\n"
             "    $warning \"\"\n"
             "    $unreg_enabled 1\n"
             "    $keyctrl_button 0\n"
             "  }\n"
             "}\n",
             kRailX + kRailWidth * 0.5f, railTop - railHeight * 0.5f, kRailWidth, railHeight);

    Amod::Menu::Add("IDM_MODS", fragment);
    g_railDraw = Amod::Render::OnElementDraw("ID_MODS_RAIL", AMOD_ELEMENT_REPLACE | AMOD_ELEMENT_AFTER, &DrawRail);
}

static void BuildTiles()
{
    if (g_built)
    {
        return;
    }

    g_tiles.clear();

    const int count = Amod::Mods::Count();
    g_tiles.resize(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i)
    {
        g_tiles[static_cast<size_t>(i)].index = i;

        AmodModEntry entry;
        if (!Amod::Mods::Get(i, entry))
        {
            continue;
        }

        char fragment[1024];
        snprintf(fragment, sizeof(fragment),
                 "$img_buttons\n"
                 "{\n"
                 "  $button\n"
                 "  {\n"
                 "    $id \"ID_MOD_%d\"\n"
                 "    $pos %.0f %.0f\n"
                 "    $rect_size %.0f %.0f\n"
                 "    $h_align \"center\"\n"
                 "    $v_align \"center\"\n"
                 "    $img_enabled \"textures/frame/a40.png\"\n"
                 "    $img_disabled \"textures/frame/a40.png\"\n"
                 "    $help \"%s\"\n"
                 "    $warning \"\"\n"
                 "    $unreg_enabled 1\n"
                 "    $keyctrl_button 0\n"
                 "  }\n"
                 "}\n",
                 i, kOffScreen, kOffScreen, kIcon, kIcon,
                 Cut(entry.name, 40).c_str());

        Amod::Menu::Add("IDM_MODS", fragment);

        char tileId[32];
        TileId(tileId, sizeof(tileId), "ID_MOD_", i);
        g_tiles[static_cast<size_t>(i)].draw =
            Amod::Render::OnElementDraw(tileId, AMOD_ELEMENT_AFTER, &DrawTile, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

        // The name first, because that is what a player is looking for, the author under it.
        snprintf(fragment, sizeof(fragment),
                 "$static_text\n"
                 "{\n"
                 "  $static\n"
                 "  {\n"
                 "    $id \"ID_MOD_NAME_%d\"\n"
                 "    $text \"\"\n"
                 "    $pos %.0f %.0f\n"
                 "    $font_scale 0.45 0.5\n"
                 "    $font_color 1 1 1 1\n"
                 "    $align \"center\"\n"
                 "  }\n"
                 "  $static\n"
                 "  {\n"
                 "    $id \"ID_MOD_AUTHOR_%d\"\n"
                 "    $text \"\"\n"
                 "    $pos %.0f %.0f\n"
                 "    $font_scale 0.4 0.45\n"
                 "    $font_color 1 1 1 0.65\n"
                 "    $align \"center\"\n"
                 "  }\n"
                 "}\n",
                 i, kOffScreen, kOffScreen,
                 i, kOffScreen, kOffScreen);

        Amod::Menu::Add("IDM_MODS", fragment);
    }

    BuildRail();
    LayoutTiles();
    WriteDetail();
    g_built = true;
}

static void DropTiles()
{
    for (ModTile& tile : g_tiles)
    {
        if (tile.draw != AMOD_INVALID_TOKEN)
        {
            Amod::Render::CancelElementDraw(tile.draw);
        }
    }

    if (g_railDraw != AMOD_INVALID_TOKEN)
    {
        Amod::Render::CancelElementDraw(g_railDraw);
        g_railDraw = AMOD_INVALID_TOKEN;
    }

    g_tiles.clear();
    g_built = false;
    g_laidOut = -1;
}

// Turns a cursor height into a first row, which is what the rail means.
static void ScrollTo(float cursorY)
{
    float railTop = 0.0f;
    float railHeight = 0.0f;
    RailRect(railTop, railHeight);

    const float visible = static_cast<float>(kRows) / static_cast<float>(RowCount());
    const float gripHeight = railHeight * visible;
    const float travel = railHeight - gripHeight;
    if (travel <= 0.0f)
    {
        return;
    }

    // Measured from the middle of the grip, so it does not jump under the cursor when a drag
    // starts anywhere but its centre.
    const float from = railTop - gripHeight * 0.5f;
    float progress = (from - cursorY) / travel;
    progress = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);

    g_firstRow = static_cast<int>(progress * MaxFirstRow() + 0.5f);
}

static void ScrollBy(int rows)
{
    int row = g_firstRow + rows;
    row = row < 0 ? 0 : (row > MaxFirstRow() ? MaxFirstRow() : row);
    g_firstRow = row;
}

static void UpdateScrollbar()
{
    if (!Amod::Input::IsAvailable() || RowCount() <= kRows)
    {
        g_dragging = false;
        return;
    }

    // The wheel, which wants no rail and no aiming: away from the player scrolls up the list.
    ScrollBy(-Amod::Input::Wheel());

    float railTop = 0.0f;
    float railHeight = 0.0f;
    RailRect(railTop, railHeight);

    float cursorX = 0.0f;
    float cursorY = 0.0f;
    Amod::Input::Cursor(cursorX, cursorY);

    cursorY = 480.0f - cursorY;

    const bool onRail = cursorX >= kRailX && cursorX <= kRailX + kRailWidth && cursorY <= railTop && cursorY >= railTop - railHeight;
    const bool down = Amod::Input::IsDown(AMOD_KEY_LBUTTON);

    g_dragging = g_dragging ? down : (onRail && down);

    if (g_dragging)
    {
        ScrollTo(cursorY);
    }
}

static void DrawMenuRect(float cx, float cy, float width, float height, unsigned long color)
{
    float sx = 0.0f, sy = 0.0f, sw = 0.0f, sh = 0.0f;
    Amod::Render::MenuToScreen(cx, cy, &sx, &sy);
    Amod::Render::MenuToScreenSize(width, height, &sw, &sh);
    Amod::Render::Rect(sx, sy, sw, sh, 0.0f, color);
}

static void DrawMenuOutline(float cx, float cy, float width, float height, float thickness, unsigned long color)
{
    float sx = 0.0f, sy = 0.0f, sw = 0.0f, sh = 0.0f;
    Amod::Render::MenuToScreen(cx, cy, &sx, &sy);
    Amod::Render::MenuToScreenSize(width, height, &sw, &sh);
    Amod::Render::RectOutline(sx, sy, sw, sh, 0.0f, thickness, color);
}

// Drawn as part of the element rather than over the finished frame
static void __cdecl DrawTile(AmodMenuItem, AmodElementPhase, void* user)
{
    const int index = static_cast<int>(reinterpret_cast<uintptr_t>(user));
    if (index < 0 || index >= static_cast<int>(g_tiles.size()))
    {
        return;
    }

    ModTile& tile = g_tiles[static_cast<size_t>(index)];

    float cx = 0.0f;
    float cy = 0.0f;
    if (!TileRect(tile.index, cx, cy))
    {
        return;
    }

    AmodModEntry entry;
    if (!Amod::Mods::Get(tile.index, entry))
    {
        return;
    }

    if (tile.icon != nullptr && tile.icon->IsStale())
    {
        tile.icon.reset();
        tile.iconTried = false;
    }

    if (!tile.iconTried)
    {
        tile.iconTried = true;
        if (entry.icon[0] != '\0')
        {
            auto texture = std::make_unique<Amod::Render::Texture>();
            if (texture->Load(entry.icon))
            {
                tile.icon = std::move(texture);
            }
            else
            {
                Amod::Log(AMOD_LOG_WARNING, "the icon of '%s' could not be read; a BMP has to "
                                            "be 24- or 32-bit", entry.id);
            }
        }
    }

    const unsigned long tint = entry.enabled ? 0xFFFFFFFFul : 0x80FFFFFFul;

    if (tile.icon != nullptr)
    {
        float sx = 0.0f, sy = 0.0f, sw = 0.0f, sh = 0.0f;
        Amod::Render::MenuToScreen(cx, cy, &sx, &sy);
        Amod::Render::MenuToScreenSize(kIcon, kIcon, &sw, &sh);
        Amod::Render::DrawTexture(tile.icon->Get(), sx, sy, sw, sh, 0.0f, tint);
    }
    else
    {
        DrawMenuRect(cx, cy, kIcon, kIcon, tint);
    }

    if (tile.index == g_selected)
    {
        DrawMenuOutline(cx, cy, kIcon + 6.0f, kIcon + 6.0f, 2.0f, 0xFFFFD24Bul);
    }
}

static void __cdecl DrawRail(AmodMenuItem, AmodElementPhase phase, void*)
{
    if (phase != AMOD_ELEMENT_AFTER || RowCount() <= kRows)
    {
        return;
    }

    float railTop = 0.0f;
    float railHeight = 0.0f;
    RailRect(railTop, railHeight);

    DrawMenuRect(kRailX + kRailWidth * 0.5f, railTop - railHeight * 0.5f, kRailWidth, railHeight, 0x40FFFFFFul);

    const float visible = static_cast<float>(kRows) / static_cast<float>(RowCount());
    const float gripHeight = railHeight * visible;
    const float travel = railHeight - gripHeight;
    const float progress = MaxFirstRow() > 0 ? static_cast<float>(g_firstRow) / MaxFirstRow() : 0.0f;

    DrawMenuRect(kRailX + kRailWidth * 0.5f, railTop - gripHeight * 0.5f - travel * progress, kRailWidth - 4.0f, gripHeight, 0xFFFFD24Bul);
}

static void __cdecl OnMenuLoaded(AmodMenuEvent, void* data, void*)
{
    const AmodMenuLoaded* const loaded = static_cast<const AmodMenuLoaded*>(data);

    if (strcmp(loaded->menuId, "IDM_MAIN_MENU") == 0)
    {
        Amod::Menu::Add("IDM_MAIN_MENU",
                        "$menu_items\n"
                        "{\n"
                        "  $item\n"
                        "  {\n"
                        "    $v_align \"center\"\n"
                        "    $h_align \"center\"\n"
                        "    $id \"ID_MODS\"\n"
                        "    $text \"MODS\"\n"
                        "    $pos 320 340\n"
                        "    $rect_size 270 45\n"
                        "    $plank_width 5\n"
                        "    $sub_menu \"IDM_MODS\"\n"
                        "    $help \"Manager mods\"\n"
                        "    $warning \"\"\n"
                        "    $unreg_enabled 1\n"
                        "  }\n"
                        "}\n");

        BuildTiles();
        return;
    }

    if (strcmp(loaded->menuId, "IDM_MODS") == 0)
    {
        g_selected = 0;
        g_firstRow = 0;
        g_dragging = false;
        DropTiles();
    }
}

static void ApplySwitches()
{
    const int count = Amod::Mods::Count();

    for (int i = 0; i < count; ++i)
    {
        AmodModEntry entry;
        if (!Amod::Mods::Get(i, entry) || entry.enabled == entry.loaded)
        {
            continue;
        }

        if (entry.needsRestart)
        {
            Amod::Log(AMOD_LOG_INFO, "'%s' changes the launcher, so it waits for a restart",
                      entry.id);
            continue;
        }

        Amod::Mods::Reload(i);
    }
}

static void __cdecl OnMenuClicked(AmodMenuEvent, void* data, void*)
{
    AmodMenuClicked* const click = static_cast<AmodMenuClicked*>(data);
    if (strcmp(click->menuId, "IDM_MODS") != 0)
    {
        return;
    }

    if (strncmp(click->itemId, "ID_MOD_", 7) == 0 &&
        click->itemId[7] >= '0' && click->itemId[7] <= '9')
    {
        g_selected = atoi(click->itemId + 7);
        WriteDetail();
        click->handled = 1;
        return;
    }

    if (strcmp(click->itemId, "ID_MODS_RAIL") == 0)
    {
        float cursorX = 0.0f;
        float cursorY = 0.0f;
        Amod::Input::Cursor(cursorX, cursorY);
        ScrollTo(480.0f - cursorY);
        click->handled = 1;
        return;
    }

    if (strcmp(click->itemId, "ID_MODS_TOGGLE") == 0)
    {
        AmodModEntry entry;
        if (Amod::Mods::Get(g_selected, entry))
        {
            Amod::Mods::SetEnabled(g_selected, entry.enabled == 0);
            WriteDetail();
        }
        click->handled = 1;
        return;
    }

    if (strcmp(click->itemId, "ID_BACK") == 0)
    {
        ApplySwitches();
    }
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    if (!Amod::Mods::IsAvailable())
    {
        Amod::Log(AMOD_LOG_ERROR, "core has no mod manager, so it is not installed beside "
                                  "AzForge.dll; the MODS screen will not be built");
        return AMOD_ERROR;
    }

    Amod::Menu::Subscribe(AMOD_MENU_LOADED, &OnMenuLoaded);
    Amod::Menu::Subscribe(AMOD_MENU_CLICKED, &OnMenuClicked);

    if (!Amod::Box::RegisterFromScript("IDM_MODS", "scripts/menu_manager_mods.txt"))
    {
        Amod::Log(AMOD_LOG_ERROR, "the MODS screen could not be built from its script");
        return AMOD_ERROR;
    }

    Amod::Log(AMOD_LOG_INFO, "the MODS screen is registered as '%s'; %d mod(s) installed", "IDM_MODS", Amod::Mods::Count());
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
    if (!Amod::Box::IsCurrent("IDM_MODS"))
    {
        return;
    }

    BuildTiles();
    UpdateScrollbar();

    if (g_laidOut != g_firstRow)
    {
        LayoutTiles();
    }
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
    DropTiles();
}
