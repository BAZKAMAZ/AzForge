#include "pch.h"

#include "src/Api/ModContext.h"

#include "src/Api/BoxMenuApi.h"
#include "src/Api/LauncherApi.h"
#include "src/Api/ObjectsMenuApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Core/ForgeText.h"
#include "src/Core/ForgeVersion.h"
#include "src/Game/GameModule.h"

namespace Forge
{
    namespace
    {
        // Turns the handle a mod passed back into a context, or nullptr when it is not one.
        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            if (context == nullptr || context->magic != kModContextMagic)
            {
                return nullptr;
            }
            return context;
        }

        LogLevel TranslateLevel(AmodLogLevel level)
        {
            switch (level)
            {
            case AMOD_LOG_WARNING: return LogLevel::Warning;
            case AMOD_LOG_ERROR:   return LogLevel::Error;
            case AMOD_LOG_INFO:    break;
            }
            return LogLevel::Info;
        }

        void __cdecl ApiLog(AmodMod mod, AmodLogLevel level, const char* text)
        {
            ModContext* context = Resolve(mod);
            if (context == nullptr || text == nullptr)
            {
                FORGE_LOG_ERROR("A mod called Log with an invalid handle or message");
                return;
            }

            // "%s" rather than the text itself: a mod's message is data, and passing it as a
            // format string would let a stray percent sign read the stack.
            Log::Write(TranslateLevel(level), "[%s] %s", context->id.c_str(), text);
        }

        const char* __cdecl ApiGetModId(AmodMod mod)
        {
            const ModContext* context = Resolve(mod);
            return context != nullptr ? context->id.c_str() : "";
        }

        const char* __cdecl ApiGetModVersion(AmodMod mod)
        {
            const ModContext* context = Resolve(mod);
            return context != nullptr ? context->version.c_str() : "";
        }

        const char* __cdecl ApiGetLoaderVersion(void)
        {
            return AZFORGE_VERSION_STRING;
        }

        const char* __cdecl ApiGetGameDirectory(void)
        {
            // Built once and kept alive for the process: the API promises the pointer stays
            // valid for as long as the mod is loaded.
            static const std::string directory = ToUtf8(Paths::GameDirectory().wstring());
            return directory.c_str();
        }

        uintptr_t __cdecl ApiGetGameModuleBase(void)
        {
            return GameModule::Base();
        }

        uint32_t __cdecl ApiGetGameBuild(void)
        {
            // The loader refuses to run at all unless every signature matched, so reaching
            // this point means the executable is the one AzForge was reversed against.
            return AMOD_GAME_BUILD_AZANGARA_10;
        }
    }

    void BuildApiTable(ModContext& context)
    {
        context.magic = kModContextMagic;

        context.table.structSize = sizeof(AmodApi);
        context.table.apiVersion = AMOD_API_VERSION;
        context.table.self = &context;

        context.table.Log = &ApiLog;
        context.table.GetModId = &ApiGetModId;
        context.table.GetModVersion = &ApiGetModVersion;
        context.table.GetLoaderVersion = &ApiGetLoaderVersion;
        context.table.GetGameDirectory = &ApiGetGameDirectory;
        context.table.GetGameModuleBase = &ApiGetGameModuleBase;
        context.table.GetGameBuild = &ApiGetGameBuild;
        context.table.launcher = LauncherApi::Table();
        context.table.objectsMenu = ObjectsMenuApi::Table();
        context.table.boxMenu = BoxMenuApi::Table();
    }
}
