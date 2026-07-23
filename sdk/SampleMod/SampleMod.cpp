// SampleMod - a complete AzForge mod, and the template a new one starts from.
//
// Links against nothing: the ForgeApi headers are the only thing it includes from the
// platform. Building it produces SampleMod.amod, ready to drop into the mods folder.

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

        const Amod::Menu::Element quit = Amod::Menu::Find(loaded->menuId, "ID_QUIT");
        if (!quit.IsValid())
        {
            return;
        }

        // Keys are the ones the script itself uses.
        const float x = quit["pos"].Number(0);
        const float y = quit["pos"].Number(1);
        Amod::Log(AMOD_LOG_INFO, "%s sits at %.1f %.1f and carries %d propert(ies)",
                  quit.Id(), x, y, quit.PropertyCount());

        quit["pos"].Set(x + 90.0f, y + 123.6f);
        quit["help"].Set("Leave the game");

        // Refused rather than misdirected on an element with no text of its own, so this is
        // safe to attempt on anything.
        if (!quit["text"].Set("GOODBYE"))
        {
            Amod::LogText(AMOD_LOG_INFO, "this element carries no text of its own");
        }

        Amod::Log(AMOD_LOG_INFO, "%s measures %.0f by %.0f, help now reads \"%s\"",
                  quit.Id(), quit["size"].Number(0), quit["size"].Number(1),
                  quit["help"].Text());
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

            void* const window = Amod::Launcher::Window(AMOD_GAME_BUILD_AZANGARA_10);
            Amod::Log(AMOD_LOG_INFO, "launcher window is %p", window);
            break;
        }

        case AMOD_LAUNCHER_COMMAND:
        {
            AmodLauncherCommand* const command = static_cast<AmodLauncherCommand*>(data);
            Amod::Log(AMOD_LOG_INFO, "launcher command 0x%X, notify %d",
                      command->commandId, command->notifyCode);

            // Claiming a command keeps the game from acting on it.
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

    // Tokens are not kept: everything registered here is released when the mod is unloaded.
    Amod::Launcher::Subscribe(AMOD_LAUNCHER_LAYOUT_APPLIED, &OnLauncherEvent);
    Amod::Launcher::Subscribe(AMOD_LAUNCHER_OPENED, &OnLauncherEvent);
    Amod::Launcher::Subscribe(AMOD_LAUNCHER_COMMAND, &OnLauncherEvent);
    Amod::Launcher::Subscribe(AMOD_LAUNCHER_CLOSING, &OnLauncherEvent);

    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
    Amod::LogText(AMOD_LOG_INFO, "sample mod shutting down");
}
