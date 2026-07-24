#include <ForgeApi.h>
#include <string.h>

namespace
{
    bool IsKind(const Amod::Menu::Element& element, const char* kind)
    {
        return strcmp(element["kind"].Text(), kind) == 0;
    }

    void Report(const char* what, bool changed)
    {
        Amod::Log(changed ? AMOD_LOG_INFO : AMOD_LOG_WARNING, "  %-28s %s",
                  what, changed ? "changed" : "refused");
    }

    // Interactive elements are named, so they are reached by $id.
    void RetitleMainMenu(const char* menuId)
    {
        const Amod::Menu::Element newGame = Amod::Menu::Find(menuId, "ID_NEW_GAME");
        Report("ID_NEW_GAME text", newGame["text"].Set("ONWARDS"));
        Report("ID_NEW_GAME help", newGame["help"].Set("A modded label, from a mod"));

        const Amod::Menu::Element quit = Amod::Menu::Find(menuId, "ID_QUIT");
        Report("ID_QUIT position", quit["pos"].Set(quit["pos"].Number(0) + 60.0f,
                                                   quit["pos"].Number(1)));
        Report("ID_QUIT text", quit["text"].Set("ENOUGH"));

        // IDM_PACKS is parked at 0 0 by the script, which puts it off screen. Giving it a
        // place is the sort of thing only a mod can do without touching the game's files.
        const Amod::Menu::Element packs = Amod::Menu::Find(menuId, "IDM_PACKS");
        Report("IDM_PACKS position", packs["pos"].Set(320.0f, 25.0f));

        // An object that exists in no script: built by handing the game's own parser a
        // fragment, so it comes out of the same code path as everything around it.
        const Amod::Menu::Element made = Amod::Menu::Add(menuId, R"(
            $menu_items {
                $item {
                    $id        "ID_FORGE"
                    $text      "AZFORGE"
                    $pos       320 340
                    $rect_size 240 45
                    $plank_width 6
                    $sub_menu  "IDM_FORGE"
                    $h_align   "center"
                    $v_align   "center"
                    $help      "This item was never in a script"
                }
            }
        )");

        Report("added ID_FORGE", made.IsValid());
        if (made.IsValid())
        {
            Amod::Log(AMOD_LOG_INFO, "  %-28s %s, %d key(s), text \"%s\"", "built object",
                      made["kind"].Text(), made.PropertyCount(), made["text"].Text());
        }

        // The AZFORGE item above carries $sub_menu "IDM_FORGE", a menu no script declares.
        // Register it once and fill it, and the button leads somewhere; its ID_BACK returns
        // the way every other back button does.
        if (Amod::Box::IsAvailable() && !Amod::Box::Exists("IDM_FORGE"))
        {
            Report("registered IDM_FORGE", Amod::Box::Register("IDM_FORGE"));

            Amod::Menu::Add("IDM_FORGE", R"(
                $menu_items {
                    $item {
                        $id      "ID_FORGE_GRAPHICS"
                        $text    "GRAPHICS"
                        $pos     320 200
                        $rect_size 240 45
                        $h_align "center"
                        $v_align "center"
                        $help    "A button with no $sub_menu - the mod handles it"
                    }
                    $item {
                        $id      "ID_BACK"
                        $text    "BACK"
                        $pos     320 120
                        $rect_size 184 45
                        $h_align "center"
                        $v_align "center"
                        $help    "Return to the main menu"
                    }
                }
            )");
            Amod::Menu::Add("IDM_FORGE", R"(
                $static_text {
                    $static {
                        $text        "A MENU FROM A MOD"
                        $pos         320 360
                        $font_scale  1.2 1.2
                        $font_color  0.4 0.9 1 1
                        $line_height 24
                        $align       "center"
                    }
                }
            )");
        }

        Amod::Log(AMOD_LOG_INFO, "  %-28s \"%s\", IDM_FORGE exists %d", "current menu",
                  Amod::Box::Current(), Amod::Box::Exists("IDM_FORGE") ? 1 : 0);

        // Decorations take the same call, though they take a different road behind it: their
        // parsers hand back a whole collection, which the loader merges into the one the
        // menu already has rather than replacing it.
        const Amod::Menu::Element caption = Amod::Menu::Add(menuId, R"(
            $static_text {
                $static {
                    $text        "built at runtime"
                    $pos         120 20
                    $font_scale  0.8 0.8
                    $font_color  0.3 0.6 0.7 1
                    $line_height 18
                    $align       "center"
                }
            }
        )");

        Report("added static", caption.IsValid());
        if (caption.IsValid())
        {
            Amod::Log(AMOD_LOG_INFO, "  %-28s %s, \"%s\"", "built decoration",
                      caption["kind"].Text(), caption["text"].Text());
        }

        // A still image, using a texture the menu already loads so nothing new has to be
        // found on disk. It goes in the top-left corner.
        const Amod::Menu::Element badge = Amod::Menu::Add(menuId, R"(
            $images {
                $image {
                    $texture "textures/menu/main_2.jpg"
                    $pos     10 470
                    $size    96 96
                }
            }
        )");

        Report("added image", badge.IsValid());
        if (badge.IsValid())
        {
            Amod::Log(AMOD_LOG_INFO, "  %-28s %.0f %.0f, %.0f by %.0f", "built image",
                      badge["pos"].Number(0), badge["pos"].Number(1),
                      badge["size"].Number(0), badge["size"].Number(1));
        }

        // And an animated one - the same flame the menu already runs, given a second home on
        // the left so the merge is obvious next to the original on the right.
        const Amod::Menu::Element spark = Amod::Menu::Add(menuId, R"(
            $images_anim {
                $image {
                    $pos    100 205
                    $size   126 96
                    $frames {
                        $texture "textures/menu/flame01.tga"
                        $texture "textures/menu/flame02.tga"
                        $texture "textures/loading.jpg"
                        $texture "textures/menu/flame04.tga"
                        $fps 10
                    }
                }
            }
        )");
        Report("added image_anim", spark.IsValid());

        // The 3d cursor that sits beside whichever item is selected, nudged and turned.
        Report("ID_NEW_GAME cursor", newGame["cursor3d.pos"].Set(-3.3f, 1.2f, -7.0f));
        Report("ID_NEW_GAME orient", newGame["cursor3d.orient"].Set(0.36f, 0.6f, 1.57f));
        Report("ID_NEW_GAME plank", newGame["plank_width"].Set(14));
    }

    // Decorations carry no $id at all, so they are reached by walking the menu and picking
    // on what the game built them as. The main menu's only static is the UNREGISTERED
    // banner, which the script marks $unreg_only 1 so a bought copy never draws it; clearing
    // that shows the wording anyway - it is still in the game, just never otherwise reached.
    void RestyleDecorations(const char* menuId)
    {
        const int total = Amod::Menu::Count(menuId);

        for (int i = 0; i < total; ++i)
        {
            const Amod::Menu::Element element = Amod::Menu::At(menuId, i);
            if (IsKind(element, "static"))
            {
                Report("static unreg_only", element["unreg_only"].Set(0));
            }
        }
    }

    void RelabelGraphics(const char* menuId)
    {
        const Amod::Menu::Element fullscreen = Amod::Menu::Find(menuId, "ID_FULLSCREEN");
        Report("ID_FULLSCREEN entry 0", fullscreen["items"][0].Set("Windowed"));
        Report("ID_FULLSCREEN entry 1", fullscreen["items"][1].Set("Full screen"));

        const Amod::Menu::Element filter = Amod::Menu::Find(menuId, "ID_TEX_FILTER");
        Report("ID_TEX_FILTER entry 0", filter["items"][0].Set("Soft"));

        // Nothing to reach: growing a collection is creating an object, not editing one.
        Report("ID_TEX_FILTER entry 9", filter["items"][9].Set("Nope"));

        const Amod::Menu::Element brightness = Amod::Menu::Find(menuId, "ID_BRIGHTNESS");
        Report("ID_BRIGHTNESS range", brightness["range"].Set(0.5f, 1.0f));
    }

    void RelabelScores(const char* menuId)
    {
        const int total = Amod::Menu::Count(menuId);

        for (int i = 0; i < total; ++i)
        {
            const Amod::Menu::Element element = Amod::Menu::At(menuId, i);
            if (!IsKind(element, "table"))
            {
                continue;
            }

            // The score screen's own table renders because the score subsystem binds it and
            // drives it; editing its columns and colours here works for the same reason.
            Report("table column 1", element["col.title"][1].Set("Hero"));
            Report("table column 2", element["col.title"][2].Set("Points"));
            Report("table row spacing", element["line_step"].Set(34.0f));
            Report("table header colour", element["header_color"].Set(1.0f, 0.8f, 0.2f, 1.0f));
        }
    }

    // A slider, a list and a text button, none of which the sound script declares. Three
    // different kinds means three different parsers, each with its own argument list, and
    // one entry point covering all of them.
    void FurnishSound(const char* menuId)
    {
        const Amod::Menu::Element slider = Amod::Menu::Add(menuId, R"(
            $sliders {
                $slider {
                    $id        "ID_FORGE_VOL"
                    $caption   { $offs -20 18  $rect_len 282 }
                    $pos       326 180
                    $rect_size 272 43
                    $margin    50 50
                    $range     0 1
                    $help      "A slider built by a mod"
                }
            }
        )");
        Report("added ID_FORGE_VOL", slider.IsValid());

        const Amod::Menu::Element list = Amod::Menu::Add(menuId, R"(
            $lists {
                $list {
                    $id        "ID_FORGE_MODE"
                    $caption   { $offs -20 18  $rect_len 282 }
                    $pos       326 130
                    $rect_size 272 43
                    $items     { $text "Off"  $text "On"  $text "Whatever" }
                    $help      "A list built by a mod"
                }
            }
        )");

        Report("added ID_FORGE_MODE", list.IsValid());
        if (list.IsValid())
        {
            Amod::Log(AMOD_LOG_INFO, "  %-28s %d entr(ies), first \"%s\"", "list contents",
                      list["items"].Integer(), list["items"][0].Text());
        }
    }

    void __cdecl OnMenuClicked(AmodMenuEvent event, void* data, void* user)
    {
        (void)user;
        (void)event;

        AmodMenuClicked* const click = static_cast<AmodMenuClicked*>(data);
        Amod::Log(AMOD_LOG_INFO, "click: %s / %s", click->menuId, click->itemId);

        // GRAPHICS carries no $sub_menu, so the game does nothing with it; the mod makes it
        // navigate, and claims the click so nothing else acts on it.
        if (strcmp(click->itemId, "ID_FORGE_GRAPHICS") == 0)
        {
            Amod::Box::Open("IDM_GRAPHICS");
            click->handled = 1;
        }
    }

    void __cdecl OnMenuEvent(AmodMenuEvent event, void* data, void* user)
    {
        (void)user;

        if (event != AMOD_MENU_LOADED)
        {
            return;
        }

        const char* const menuId = static_cast<const AmodMenuLoaded*>(data)->menuId;

        if (strcmp(menuId, "IDM_MAIN_MENU") == 0)
        {
            Amod::LogText(AMOD_LOG_INFO, "reworking IDM_MAIN_MENU");
            RetitleMainMenu(menuId);
            RestyleDecorations(menuId);
        }
        else if (strcmp(menuId, "IDM_GRAPHICS") == 0)
        {
            Amod::LogText(AMOD_LOG_INFO, "reworking IDM_GRAPHICS");
            RelabelGraphics(menuId);
        }
        else if (strcmp(menuId, "IDM_SOUND") == 0)
        {
            Amod::LogText(AMOD_LOG_INFO, "reworking IDM_SOUND");
            FurnishSound(menuId);
        }
        else if (strcmp(menuId, "IDM_SCORES") == 0)
        {
            Amod::LogText(AMOD_LOG_INFO, "reworking IDM_SCORES");
            RelabelScores(menuId);
        }
    }

    void __cdecl OnLauncherEvent(AmodLauncherEvent event, void* data, void* user)
    {
        (void)user;

        switch (event)
        {
        case AMOD_LAUNCHER_LAYOUT_APPLIED:
            // The game force-hides these after reading its dialog script, and does it again
            // on every language change, so undoing it belongs here rather than in OPENED.
            Amod::Launcher::SetVisible(AMOD_LAUNCHER_ID_BUYNOW_BUTTON, true);
            Amod::Launcher::SetText(AMOD_LAUNCHER_ID_BUYNOW_BUTTON, "BUY NOW");
            break;

        case AMOD_LAUNCHER_OPENED:
        {
            Amod::Launcher::SetCaption("Azangara - AzForge sample mod");
            Amod::Launcher::SetText(AMOD_LAUNCHER_ID_PLAY, "PLAY!");

            const int help = Amod::Launcher::FindControl("IDC_HELP_BUTTON");
            Amod::Log(AMOD_LOG_INFO, "launcher opened, IDC_HELP_BUTTON resolves to 0x%X", help);
            break;
        }

        case AMOD_LAUNCHER_COMMAND:
        {
            AmodLauncherCommand* const command = static_cast<AmodLauncherCommand*>(data);

            // Claiming a command keeps the game from acting on it.
            if (command->commandId == AMOD_LAUNCHER_ID_BUYNOW_BUTTON)
            {
                Amod::LogText(AMOD_LOG_INFO, "the buy button was claimed by the mod");
                command->handled = 1;
            }
            break;
        }

        case AMOD_LAUNCHER_CLOSING:
            break;
        }
    }
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Amod::Log(AMOD_LOG_INFO, "%s %s loaded", Amod::ModId(), Amod::ModVersion());
    Amod::Log(AMOD_LOG_INFO, "running on AzForge %s, game build %u",
              Amod::LoaderVersion(), Amod::GameBuild());

    if (Amod::Menu::IsAvailable())
    {
        Amod::Menu::Subscribe(AMOD_MENU_LOADED, &OnMenuEvent);
        Amod::Menu::Subscribe(AMOD_MENU_CLICKED, &OnMenuClicked);
    }

    if (!Amod::Launcher::IsAvailable())
    {
        Amod::LogText(AMOD_LOG_WARNING, "this loader offers no launcher interface");
        return AMOD_OK;
    }

    // Tokens are not kept: everything registered here is released when the mod is unloaded.
    Amod::Launcher::Subscribe(AMOD_LAUNCHER_LAYOUT_APPLIED, &OnLauncherEvent);
    Amod::Launcher::Subscribe(AMOD_LAUNCHER_OPENED, &OnLauncherEvent);
    Amod::Launcher::Subscribe(AMOD_LAUNCHER_COMMAND, &OnLauncherEvent);

    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
    Amod::LogText(AMOD_LOG_INFO, "sample mod shutting down");
}
