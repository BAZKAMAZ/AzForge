// core.amod - AzForge's own mod, and the MODS screen.
//
// It is a mod like any other, on purpose: the screen that manages mods going through the same
// interface everyone else gets is what stops that interface quietly growing a private door.
// The one thing it will have that other mods do not is the right to switch them on and off,
// and that will come from where it is installed - beside AzForge.dll rather than in mods -
// because anyone who can write there can already replace the loader itself.
//
// The screen's frame lives in files/scripts/menu_manager_mods.txt rather than here. What is
// built at run time is only what depends on which mods a player actually has.

#include <d3d8.h>        // from inc_d3d8; include before ForgeApi.h
#include <ForgeApi.h>

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Amod::Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Amod::Log(AMOD_LOG_INFO, "core is up; the MODS screen is not built yet");
    return AMOD_OK;
}

AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
