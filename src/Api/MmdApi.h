#pragma once

#include <MmdApi.h>

#include <stdint.h>
#include <vector>

namespace Forge
{
    struct ModContext;

    // One material of a mesh, as the device wants it drawn.
    struct MeshRange
    {
        uint32_t firstIndex = 0;
        uint32_t primitiveCount = 0;
        void* texture = nullptr;
    };

    // What a mesh gives a draw that does not go through the game's own render.
    struct MeshStreams
    {
        void* vertices = nullptr;
        void* indices = nullptr;
        uint32_t vertexCount = 0;
        uint32_t stride = 0;
        std::vector<MeshRange> ranges;
    };

    class MmdApi
    {
    public:
        static bool                                                 Install();
        static void                                                 Uninstall();

        static const AmodMmdApi*                                    Table();

        static int                                                  ReadAnimState(const uint8_t* state, AmodAnimState* out);

        static AmodModelId                                          IdFor(void* model);
        static void*                                                ModelFor(AmodModelId id);

        static void*                                                AnimationFor(AmodAnimationId id);

        static void                                                 EnterDraw();
        static void                                                 LeaveDraw();

        static bool                                                 StreamsFor(AmodModelId model, MeshStreams& out);

        static void                                                 ForgetMod(ModContext* owner);
    };
}
