// SampleMod - the smallest complete AzForge mod, and the template a new mod starts from.
//
// It links against nothing: the ForgeApi headers are the only thing it includes from the
// platform, and everything it calls arrives through the tables handed to AmodInit. Building
// it produces SampleMod.amod, ready to drop into the mods folder.

#include <ForgeApi.h>

namespace
{
    void __cdecl OnMenuEvent(AmodMenuEvent event, void* data, void* user)
    {
        (void)user;

        if (event != AMOD_MENU_LOADED)
        {
            return;
        }

        const AmodMenuLoaded* const loaded = static_cast<const AmodMenuLoaded*>(data);
        Amod::Log(AMOD_LOG_INFO, "menu '%s' built, %d element(s)",
                  loaded->menuId, Amod::Menu::Count(loaded->menuId));

        // Interactive elements are addressed by the $id their script block declares.
        const AmodMenuItem quit = Amod::Menu::Find(loaded->menuId, "ID_QUIT");
        if (quit == nullptr)
        {
            return;
        }

        float x = 0.0f;
        float y = 0.0f;
        if (Amod::Menu::GetPosition(quit, &x, &y))
        {
            Amod::Log(AMOD_LOG_INFO, "ID_QUIT sits at %.1f %.1f, moving it right", x, y);
            Amod::Menu::SetPosition(quit, x + 90.0f, y + 123.6f);
        }

        // Text only exists on elements the game built as $item, which the loader tracks
        // rather than guesses.
        if (Amod::Menu::IsTextItem(quit))
        {
            Amod::Menu::SetText(quit, "GOODBYE");
        }
    }

    void __cdecl OnLauncherEvent(AmodLauncherEvent event, void* data, void* user)
    {
        (void)user;

        switch (event)
        {
        case AMOD_LAUNCHER_LAYOUT_APPLIED:
        {
            // The game hides these three in code right after reading its dialog script, so
            // bringing them back belongs here rather than in AMOD_LAUNCHER_OPENED: this
            // event comes round again every time the player changes language, which is when
            // the game would otherwise hide them a second time.
            Amod::Launcher::SetVisible(AMOD_LAUNCHER_ID_BUYNOW_BUTTON, true);
            Amod::Launcher::SetText(AMOD_LAUNCHER_ID_BUYNOW_BUTTON, "BUY NOW");
            break;
        }

        case AMOD_LAUNCHER_OPENED:
        {
            Amod::Launcher::SetCaption("Azangara - AzForge sample mod");
            Amod::Launcher::SetText(AMOD_LAUNCHER_ID_PLAY, "PLAY!");

            // Reaching a control by the name the game's own dialog script uses, rather than
            // by a number that only holds for this build.
            const int help = Amod::Launcher::FindControl("IDC_HELP_BUTTON");
            Amod::Log(AMOD_LOG_INFO, "launcher opened, IDC_HELP_BUTTON resolves to 0x%X", help);

            // The escape hatch: anything this interface does not cover is reachable through
            // the window handle, which is only handed over for the layout this mod names.
            void* const window = Amod::Launcher::Window(AMOD_GAME_BUILD_AZANGARA_10);
            Amod::Log(AMOD_LOG_INFO, "launcher window is %p", window);
            break;
        }

        case AMOD_LAUNCHER_COMMAND:
        {
            AmodLauncherCommand* const command = static_cast<AmodLauncherCommand*>(data);
            Amod::Log(AMOD_LOG_INFO, "launcher command 0x%X, notify %d",
                      command->commandId, command->notifyCode);

            // Claiming a command keeps the game from acting on it. Here the buy button is
            // stopped from opening a browser; everything else is left alone.
            if (command->commandId == AMOD_LAUNCHER_ID_BUYNOW_BUTTON)
            {
                Amod::LogText(AMOD_LOG_INFO, "the buy button was claimed by the mod");
                command->handled = 1;
            }
            break;
        }

        case AMOD_LAUNCHER_CLOSING:
            Amod::LogText(AMOD_LOG_INFO, "launcher closed");
            break;
        }
    }
}

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    // Attach fails when the loader is older than the API this mod was built against. There
    // is nothing sensible to do with half an interface, so the mod refuses to load and the
    // loader writes the reason to forge.log.
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Amod::Log(AMOD_LOG_INFO, "%s %s loaded", Amod::ModId(), Amod::ModVersion());
    Amod::Log(AMOD_LOG_INFO, "running on AzForge %s, game build %u",
              Amod::LoaderVersion(), Amod::GameBuild());
    Amod::Log(AMOD_LOG_INFO, "game.exe is mapped at 0x%08X",
              static_cast<unsigned>(Amod::GameModuleBase()));

    if (Amod::Menu::IsAvailable())
    {
        Amod::Menu::Subscribe(AMOD_MENU_LOADED, &OnMenuEvent);
    }

    if (!Amod::Launcher::IsAvailable())
    {
        Amod::LogText(AMOD_LOG_WARNING, "this loader offers no launcher interface");
        return AMOD_OK;
    }

    // The tokens are not kept: everything registered here is released when the mod is
    // unloaded, in the reverse of the order it was registered.
    Amod::Launcher::Subscribe(AMOD_LAUNCHER_LAYOUT_APPLIED, &OnLauncherEvent);
    Amod::Launcher::Subscribe(AMOD_LAUNCHER_OPENED, &OnLauncherEvent);
    Amod::Launcher::Subscribe(AMOD_LAUNCHER_COMMAND, &OnLauncherEvent);
    Amod::Launcher::Subscribe(AMOD_LAUNCHER_CLOSING, &OnLauncherEvent);

    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
    // By the time this runs the loader has already released everything the mod registered.
    Amod::LogText(AMOD_LOG_INFO, "sample mod shutting down");
}
