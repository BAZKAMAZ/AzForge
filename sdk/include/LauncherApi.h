/*
 * LauncherApi.h - the start-up launcher.
 *
 * The dialog the game shows before it starts: renderer choice, language, logo, and the
 * buttons that play, register, buy or quit. Its text and layout come from
 * scripts/local/dialog_<lang>.txt, so a mod that only wants different wording can replace
 * that file instead. This interface covers what the file cannot express: reacting to
 * buttons, changing controls while the dialog is up, and the logo, which the game loads
 * straight from disk without passing through the resource layer.
 *
 * Reached through AmodApi::launcher, null on a loader older than API version 2.
 */

#ifndef AZFORGE_LAUNCHER_API_H
#define AZFORGE_LAUNCHER_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AmodLauncherEvent
{
    /* The dialog is up and the game has applied its own layout. Data is null. */
    AMOD_LAUNCHER_OPENED = 0,

    /* A control was used, before the game acts on it. Data is AmodLauncherCommand. */
    AMOD_LAUNCHER_COMMAND = 1,

    /* The dialog has closed and the window handle is gone. Data is null. */
    AMOD_LAUNCHER_CLOSING = 2,

    /* The game has finished styling the dialog from its script, which happens once while
     * the dialog comes up and again on every language change. Data is null.
     *
     * This is where a mod undoes something the game decides in code rather than in script:
     * the registration and purchase controls are force-hidden after the script is applied,
     * so showing them from AMOD_LAUNCHER_OPENED alone would last only until the player
     * switched language. */
    AMOD_LAUNCHER_LAYOUT_APPLIED = 3
} AmodLauncherEvent;

typedef struct AmodLauncherCommand
{
    int commandId;      /* one of AMOD_LAUNCHER_ID_* */
    int notifyCode;     /* BN_CLICKED, CBN_SELCHANGE and so on */
    int handled;        /* set non-zero to stop the game handling this command */
} AmodLauncherCommand;

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

typedef struct AmodLauncherApi
{
    uint32_t structSize;

    AmodToken (__cdecl* Subscribe)(AmodMod mod, AmodLauncherEvent event,
                                   AmodLauncherCallback callback, void* user);
    void (__cdecl* Unsubscribe)(AmodMod mod, AmodToken token);

    /* Non-zero while the dialog exists. Everything below only works then. */
    int (__cdecl* IsOpen)(AmodMod mod);

    /* Control id for a name the game's dialog script uses, such as "IDC_HELP_BUTTON",
     * or 0. Safer than a literal id, which only holds for this build. */
    int (__cdecl* FindControl)(AmodMod mod, const char* name);

    /* Non-zero on success, zero when the launcher is not up or the control does not
     * exist. */
    int (__cdecl* SetText)(AmodMod mod, int controlId, const char* text);
    int (__cdecl* GetText)(AmodMod mod, int controlId, char* buffer, uint32_t size);
    int (__cdecl* SetVisible)(AmodMod mod, int controlId, int visible);
    int (__cdecl* SetEnabled)(AmodMod mod, int controlId, int enabled);
    int (__cdecl* SetCaption)(AmodMod mod, const char* text);

    /* Zero for any of x, y, width or height keeps the control's current value, the same
     * rule the game's dialog script follows. */
    int (__cdecl* SetBounds)(AmodMod mod, int controlId, int x, int y, int width, int height);

    /* Replaces the logo with a BMP held in memory. */
    int (__cdecl* SetLogo)(AmodMod mod, const void* bitmapFile, uint32_t size);

    /* The dialog's HWND, for creating controls, subclassing or custom drawing. Null unless
     * expectedGameBuild matches AmodApi::GetGameBuild. */
    void* (__cdecl* GetWindow)(AmodMod mod, uint32_t expectedGameBuild);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodLauncherApi;

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

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
