#include "src/Api/ModContext.h"

#include "src/Api/BlockExpressionApi.h"
#include "src/Api/BoxMenuApi.h"
#include "src/Api/ConsoleApi.h"
#include "src/Api/LauncherApi.h"
#include "src/Api/ObjectsMenuApi.h"
#include "src/Api/InteropApi.h"
#include "src/Api/ModManagerApi.h"
#include "src/Api/BonusesApi.h"
#include "src/Api/HudApi.h"
#include "src/Api/MonstersApi.h"
#include "src/Api/MmdApi.h"
#include "src/Api/CameraApi.h"
#include "src/Api/ParticlesApi.h"
#include "src/Api/FilesApi.h"
#include "src/Api/CtrlsApi.h"
#include "src/Api/PlayerApi.h"
#include "src/Api/SoundApi.h"
#include "src/Api/TextureApi.h"
#include "src/Api/InputApi.h"
#include "src/Api/InventoryApi.h"
#include "src/Api/RenderApi.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Core/ForgeText.h"
#include "src/Core/ForgeVersion.h"
#include "src/Game/GameModule.h"

namespace Forge
{
    namespace
    {
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
            static const std::string directory = ToUtf8(Paths::GameDirectory().wstring());
            return directory.c_str();
        }

        uintptr_t __cdecl ApiGetGameModuleBase(void)
        {
            return GameModule::Base();
        }

        uint32_t __cdecl ApiGetGameBuild(void)
        {
            return AMOD_GAME_BUILD_AZANGARA_10;
        }

        const char* __cdecl ApiLastError(AmodMod mod)
        {
            const ModContext* const context = Resolve(mod);
            return context != nullptr ? context->failText.c_str() : "";
        }

        int __cdecl ApiLastErrorCode(AmodMod mod)
        {
            const ModContext* const context = Resolve(mod);
            return context != nullptr ? context->failCode : AMOD_FAIL_NONE;
        }
    }

    bool ModContext::Declares(const char* capability) const
    {
        if (capability == nullptr)
        {
            return false;
        }
        for (const std::string& declared : capabilities)
        {
            if (declared == capability)
            {
                return true;
            }
        }
        return false;
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
        context.table.LastError = &ApiLastError;
        context.table.LastErrorCode = &ApiLastErrorCode;
        context.table.launcher = LauncherApi::Table();
        context.table.objectsMenu = ObjectsMenuApi::Table();
        context.table.boxMenu = BoxMenuApi::Table();
        context.table.blockExpression = BlockExpressionApi::Table();
        context.table.console = ConsoleApi::Table();
        context.table.render = RenderApi::Table();
        context.table.world = WorldApi::Table();
        context.table.bonuses = BonusesApi::Table();
        context.table.inventory = InventoryApi::Table();
        context.table.hud = HudApi::Table();
        context.table.monsters = MonstersApi::Table();
        context.table.mmd = MmdApi::Table();
        context.table.textures = TextureApi::Table();
        context.table.sound = SoundApi::Table();
        context.table.player = PlayerApi::Table();
        context.table.ctrls = CtrlsApi::Table();
        context.table.camera = CameraApi::Table();
        context.table.particles = ParticlesApi::Table();
        context.table.files = FilesApi::Table();
        context.table.input = InputApi::Table();

        context.table.modManager = context.privileged ? ModManagerApi::Table() : nullptr;

        if (context.Declares("interop"))
        {
            context.table.interop = InteropApi::Table();
        }
        else
        {
            context.table.interop = nullptr;
        }
    }
}
