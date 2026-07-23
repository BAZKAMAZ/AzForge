/*
 * LauncherApi.h - the start-up launcher.
 *
 * The launcher is the dialog the game shows before it starts: renderer choice, language,
 * the logo, and the buttons that play, register, buy or quit. It is a resource dialog
 * inside game.exe whose text and layout the game itself reads from
 * scripts/local/dialog_<lang>.txt, so a mod that only wants different wording can replace
 * that file and write no code at all.
 *
 * This interface is for what that file cannot express: reacting to buttons, changing
 * controls while the dialog is up, and the logo, which the game loads with LoadImage
 * straight from disk without passing through the resource layer.
 *
 * Reached through AmodApi::launcher, which is null on a loader older than API version 2.
 */

#ifndef AZFORGE_LAUNCHER_API_H
#define AZFORGE_LAUNCHER_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AmodLauncherEvent
{
    /* The dialog is up and the game has finished applying its own layout, which is the
     * moment for a mod to change it. Event data is null. */
    AMOD_LAUNCHER_OPENED = 0,

    /* A control was used, before the game acts on it. Event data is AmodLauncherCommand,
     * and setting its handled field keeps the game from acting. */
    AMOD_LAUNCHER_COMMAND = 1,

    /* The dialog has closed. Event data is null, and the window handle is gone by now. */
    AMOD_LAUNCHER_CLOSING = 2,

    /* The game has just finished styling the dialog from its own script, which it does
     * once while the dialog is coming up and again every time the language changes. Event
     * data is null.
     *
     * This is where a mod undoes a decision the game makes in code rather than in script:
     * the registration and purchase controls are force-hidden after the script has been
     * applied, so showing them from AMOD_LAUNCHER_OPENED alone would last only until the
     * player switched language. */
    AMOD_LAUNCHER_LAYOUT_APPLIED = 3
} AmodLauncherEvent;

typedef struct AmodLauncherCommand
{
    int commandId;      /* one of the AMOD_LAUNCHER_ID_* values below */
    int notifyCode;     /* the notification the control sent: BN_CLICKED, CBN_SELCHANGE, ... */
    int handled;        /* set to non-zero to stop the game from handling this command */
} AmodLauncherCommand;

/* Control ids of the stock launcher. FindControl resolves the same controls by the names
 * the game's own dialog script uses, which is the safer way to reach them. */
#define AMOD_LAUNCHER_ID_RASTER_COMBO                               0x3E8
#define AMOD_LAUNCHER_ID_CONFIG_BUTTON                              0x3E9
#define AMOD_LAUNCHER_ID_REGISTER_BUTTON                            0x3EA
#define AMOD_LAUNCHER_ID_REG_STATIC                                 0x3EC
#define AMOD_LAUNCHER_ID_HELP_BUTTON                                0x3ED
#define AMOD_LAUNCHER_ID_BUYNOW_BUTTON                              0x3EE
#define AMOD_LAUNCHER_ID_LANG_COMBO                                 0x3EF
#define AMOD_LAUNCHER_ID_LANG_STATIC                                0x3F0
#define AMOD_LAUNCHER_ID_RENDERAPI_STATIC                           0x3F1
#define AMOD_LAUNCHER_ID_PLAY                                       0x3F3
#define AMOD_LAUNCHER_ID_EXIT                                       0x3F4

typedef void (__cdecl* AmodLauncherCallback)(AmodLauncherEvent event, void* data, void* user);

/* Subsystem table. It carries its own structSize for the same reason the core table does:
 * fields are only ever appended, and structSize says how many of them the running loader
 * filled in. */
typedef struct AmodLauncherApi
{
    uint32_t structSize;

    /* --- since API version 2 ------------------------------------------------------- */

    /* Registers a callback. The returned token can be released early with Unsubscribe;
     * otherwise it goes away with the mod. */
    AmodToken (__cdecl* Subscribe)(AmodMod mod, AmodLauncherEvent event,
                                   AmodLauncherCallback callback, void* user);
    void (__cdecl* Unsubscribe)(AmodMod mod, AmodToken token);

    /* Non-zero while the launcher dialog exists. Everything below only works then. */
    int (__cdecl* IsOpen)(AmodMod mod);

    /* Control id for one of the names the game's dialog script uses, such as
     * "IDC_HELP_BUTTON". Returns 0 when there is no such control. */
    int (__cdecl* FindControl)(AmodMod mod, const char* name);

    /* These answer non-zero on success, zero when the launcher is not up or the control
     * does not exist. */
    int (__cdecl* SetText)(AmodMod mod, int controlId, const char* text);
    int (__cdecl* GetText)(AmodMod mod, int controlId, char* buffer, uint32_t size);
    int (__cdecl* SetVisible)(AmodMod mod, int controlId, int visible);
    int (__cdecl* SetEnabled)(AmodMod mod, int controlId, int enabled);
    int (__cdecl* SetCaption)(AmodMod mod, const char* text);

    /* Zero for any of x, y, width or height keeps the control's current value, which is the
     * same rule the game's own dialog script follows. */
    int (__cdecl* SetBounds)(AmodMod mod, int controlId, int x, int y, int width, int height);

    /* Replaces the logo with a BMP held in memory. */
    int (__cdecl* SetLogo)(AmodMod mod, const void* bitmapFile, uint32_t size);

    /* The dialog's HWND, for everything this interface does not cover: creating controls,
     * subclassing, custom drawing. Returns null unless expectedGameBuild matches what
     * AmodApi::GetGameBuild reports, because raw access is only meaningful against a
     * layout the caller was written for. */
    void* (__cdecl* GetWindow)(AmodMod mod, uint32_t expectedGameBuild);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodLauncherApi;

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

/*
 * Convenience layer. Inline code compiled into the mod that calls through the table above,
 * so it can change freely without affecting the ABI. Amod::Attach wires it up.
 */
namespace Amod
{
    namespace Launcher
    {
        inline const AmodLauncherApi* g_launcher = nullptr;
        inline AmodMod g_self = nullptr;

        inline void                                                 Attach(const AmodLauncherApi* api, AmodMod self)
        {
            g_launcher = api;
            g_self = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_launcher != nullptr;
        }

        inline AmodToken                                            Subscribe(AmodLauncherEvent event, AmodLauncherCallback callback, void* user = nullptr)
        {
            return g_launcher != nullptr ? g_launcher->Subscribe(g_self, event, callback, user)
                                         : AMOD_INVALID_TOKEN;
        }

        inline void                                                 Unsubscribe(AmodToken token)
        {
            if (g_launcher != nullptr)
            {
                g_launcher->Unsubscribe(g_self, token);
            }
        }

        inline bool                                                 IsOpen()
        {
            return g_launcher != nullptr && g_launcher->IsOpen(g_self) != 0;
        }

        inline int                                                  FindControl(const char* name)
        {
            return g_launcher != nullptr ? g_launcher->FindControl(g_self, name) : 0;
        }

        inline bool                                                 SetText(int controlId, const char* text)
        {
            return g_launcher != nullptr && g_launcher->SetText(g_self, controlId, text) != 0;
        }

        inline bool                                                 GetText(int controlId, char* buffer, uint32_t size)
        {
            return g_launcher != nullptr && g_launcher->GetText(g_self, controlId, buffer, size) != 0;
        }

        inline bool                                                 SetVisible(int controlId, bool visible)
        {
            return g_launcher != nullptr && g_launcher->SetVisible(g_self, controlId, visible ? 1 : 0) != 0;
        }

        inline bool                                                 SetEnabled(int controlId, bool enabled)
        {
            return g_launcher != nullptr && g_launcher->SetEnabled(g_self, controlId, enabled ? 1 : 0) != 0;
        }

        inline bool                                                 SetCaption(const char* text)
        {
            return g_launcher != nullptr && g_launcher->SetCaption(g_self, text) != 0;
        }

        inline bool                                                 SetBounds(int controlId, int x, int y, int width, int height)
        {
            return g_launcher != nullptr && g_launcher->SetBounds(g_self, controlId, x, y, width, height) != 0;
        }

        inline bool                                                 SetLogo(const void* bitmapFile, uint32_t size)
        {
            return g_launcher != nullptr && g_launcher->SetLogo(g_self, bitmapFile, size) != 0;
        }

        inline void*                                                Window(uint32_t expectedGameBuild = AMOD_GAME_BUILD_AZANGARA_10)
        {
            return g_launcher != nullptr ? g_launcher->GetWindow(g_self, expectedGameBuild) : nullptr;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_LAUNCHER_API_H */
