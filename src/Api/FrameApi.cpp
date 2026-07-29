#include "pch.h"

#include "src/Api/FrameApi.h"

#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

#include <vector>

namespace Forge
{
    namespace
    {
        typedef void(__cdecl* FrameTickRoutine)(void);

        struct Subscriber
        {
            ModContext* owner;
            AmodUpdateRoutine update;
        };

        InlineHook g_frameHook;
        FrameTickRoutine g_originalTick = nullptr;
        const float* g_delta = nullptr;

        std::vector<Subscriber> g_subscribers;
        std::vector<void(__cdecl*)()> g_internalTicks;
        std::vector<void(__cdecl*)()> g_lateTicks;

        double g_elapsed = 0.0;
        uint64_t g_frameIndex = 0;

        // Runs after the game's frame, so a mod sees the advanced world and this frame's delta.
        void __cdecl FrameTickDetour(void)
        {
            g_originalTick();

            for (void(__cdecl* const tick)() : g_internalTicks)
            {
                tick();
            }

            if (g_subscribers.empty())
            {
                for (void(__cdecl* const tick)() : g_lateTicks)
                {
                    tick();
                }
                return;
            }

            AmodFrame frame = {};
            frame.structSize = sizeof(AmodFrame);
            frame.delta = g_delta != nullptr ? *g_delta : 0.0f;
            g_elapsed += frame.delta;
            frame.elapsed = g_elapsed;
            frame.frameIndex = g_frameIndex++;

            // A copy, so a mod unloading itself from its own update cannot break iteration.
            const std::vector<Subscriber> snapshot = g_subscribers;
            for (const Subscriber& subscriber : snapshot)
            {
                if (subscriber.update != nullptr)
                {
                    subscriber.update(&frame);
                }
            }

            for (void(__cdecl* const tick)() : g_lateTicks)
            {
                tick();
            }
        }
    }

    bool FrameApi::Install()
    {
        void* const target = GameSymbols::Address(GameSymbol::GameFrameTick);
        if (target == nullptr)
        {
            FORGE_LOG_WARN("Game_FrameTick was not located, so AmodUpdate is not available");
            return false;
        }

        g_delta = static_cast<const float*>(GameModule::Resolve(GameAddress::kFrameDelta));

        std::string mismatch;
        if (!GameModule::VerifyBytes(target, kGameFrameTickPrologue,
                                     sizeof(kGameFrameTickPrologue), mismatch))
        {
            FORGE_LOG_ERROR("Game_FrameTick does not open as expected (%s)", mismatch.c_str());
            return false;
        }

        std::string error;
        if (!g_frameHook.Install(target, reinterpret_cast<void*>(&FrameTickDetour),
                                 kGameFrameTickPrologue, sizeof(kGameFrameTickPrologue), error))
        {
            FORGE_LOG_ERROR("Cannot hook Game_FrameTick: %s", error.c_str());
            return false;
        }

        g_originalTick = reinterpret_cast<FrameTickRoutine>(g_frameHook.Trampoline());

        FORGE_LOG_INFO("Frame interface installed");
        return true;
    }

    void FrameApi::Uninstall()
    {
        g_frameHook.Uninstall();
        g_originalTick = nullptr;
        g_delta = nullptr;
        g_subscribers.clear();
        g_internalTicks.clear();
        g_lateTicks.clear();
        g_elapsed = 0.0;
        g_frameIndex = 0;
    }

    void FrameApi::AddInternalTick(void(__cdecl* tick)(), bool afterMods)
    {
        if (tick != nullptr)
        {
            (afterMods ? g_lateTicks : g_internalTicks).push_back(tick);
        }
    }

    void FrameApi::Register(ModContext* owner, AmodUpdateRoutine update)
    {
        if (update == nullptr)
        {
            return;
        }
        g_subscribers.push_back(Subscriber{ owner, update });
    }

    void FrameApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_subscribers.begin(); it != g_subscribers.end();)
        {
            it = it->owner == owner ? g_subscribers.erase(it) : ++it;
        }
    }
}
