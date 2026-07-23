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
    // on what the game built them as.
    void RestyleDecorations(const char* menuId)
    {
        const int total = Amod::Menu::Count(menuId);

        int tiles = 0;
        int flipped = 0;
        int firstTexture = 0;
        Amod::Menu::Element lastTile;

        for (int i = 0; i < total; ++i)
        {
            const Amod::Menu::Element element = Amod::Menu::At(menuId, i);

            if (IsKind(element, "static"))
            {
                //Report("static text", element["text"].Set("RUNNING ON AZFORGE"));
                //Report("static colour", element["font_color"].Set(0.3f, 1.0f, 0.4f, 1.0f));
                //Report("static scale", element["font_scale"].Set(1.4f, 1.4f));

                //// The main menu's only static is the UNREGISTERED banner, which the script
                //// marks $unreg_only 1 so a bought copy never draws it. Clearing that shows
                //// it anyway - the trial-version wording is still in the game, just never
                //// reached, and a mod can reach it.
                Report("static unreg_only", element["unreg_only"].Set(0));
            }
            else if (IsKind(element, "image_anim"))
            {
                // The flame the main menu keeps on the right, moved to the left.
                //Report("flame position", element["pos"].Set(105.0f, 405.0f));
            }
            //else if (IsKind(element, "image"))
            //{
            //    // The main menu's background is cut into seven of these, so mirroring every
            //    // one of them is hard to miss.
            //    ++tiles;
            //    if (element["horz_flip"].Set(true))
            //    {
            //        ++flipped;
            //    }

            //    if (tiles == 1)
            //    {
            //        firstTexture = element["texture"].Integer();
            //    }
            //    lastTile = element;

            //    // An image has no text of its own, and says so rather than writing
            //    // somewhere unrelated.
            //    if (element["text"].Set("nowhere"))
            //    {
            //        Amod::LogText(AMOD_LOG_ERROR, "  an image accepted a text it cannot hold");
            //    }
            //}
        }

        Amod::Log(tiles == flipped ? AMOD_LOG_INFO : AMOD_LOG_WARNING,
                  "  %-28s %d of %d", "background tiles flipped", flipped, tiles);

        // A texture is a resolved handle, so one anonymous image can be given another's
        // artwork without the loader ever naming a file.
        if (lastTile.IsValid() && firstTexture != 0)
        {
            Report("last tile texture", lastTile["texture"].Set(firstTexture));
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
