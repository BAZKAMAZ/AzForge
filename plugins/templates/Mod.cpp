#include <ForgeApi.h>

using namespace Amod;

AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api)
{
    if (!Attach(api))
    {
        return AMOD_ERROR_API_VERSION;
    }

    Log(AMOD_LOG_INFO, "%s is up", ModId());
    return AMOD_OK;
}

// Once a frame while a level is running
AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame*)
{
}

// Everything the mod registered has already been released by the time this is called
AMOD_EXPORT void __cdecl AmodShutdown(void)
{
}
