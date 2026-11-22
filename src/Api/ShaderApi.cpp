#include <windows.h>
#include <algorithm>
#include <string.h>
#include <vector>

#include "src/Api/ShaderApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Api/MmdApi.h"
#include "src/Api/TextureApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Api/ShaderAsm.h"

namespace Forge
{
    namespace
    {
        using ApiRead::Caller;

        // Device vtable slots.
        constexpr size_t kCreateVertexShader = 75;
        constexpr size_t kSetVertexShader = 76;
        constexpr size_t kDeleteVertexShader = 78;
        constexpr size_t kSetVertexShaderConstant = 79;
        constexpr size_t kCreatePixelShader = 87;
        constexpr size_t kSetPixelShader = 88;
        constexpr size_t kDeletePixelShader = 90;
        constexpr size_t kSetPixelShaderConstant = 91;
        constexpr size_t kSetRenderState = 50;
        constexpr size_t kDrawPrimitiveUp = 72;
        constexpr size_t kGetTransform = 38;
        constexpr size_t kGetTexture = 60;
        constexpr size_t kSetTexture = 61;
        constexpr size_t kDrawIndexedPrimitive = 71;
        constexpr size_t kSetStreamSource = 83;
        constexpr size_t kSetIndices = 85;
        constexpr size_t kRelease = 2;
        constexpr size_t kCreateTexture = 20;
        constexpr size_t kTextureLock = 16;
        constexpr size_t kTextureUnlock = 17;

        constexpr uint32_t kFormatArgb = 21;
        constexpr uint32_t kPoolManaged = 1;

        // Blending, the way the game's own draw does it.
        constexpr uint32_t kStateSrcBlend = 19;
        constexpr uint32_t kStateDestBlend = 20;
        constexpr uint32_t kBlendZero = 1;
        constexpr uint32_t kBlendOne = 2;
        constexpr uint32_t kBlendSrcAlpha = 5;
        constexpr uint32_t kBlendInvSrcAlpha = 6;
        constexpr uint32_t kBlendDestColour = 9;

        constexpr uint32_t kTransformView = 2;
        constexpr uint32_t kTransformProjection = 3;

        // Safe draw states: no depth, culling, blend or lighting.
        constexpr uint32_t kStateZEnable = 7;
        constexpr uint32_t kStateZWrite = 14;
        constexpr uint32_t kStateCull = 22;
        constexpr uint32_t kStateBlend = 27;
        constexpr uint32_t kStateLighting = 137;
        constexpr uint32_t kCullNone = 1;
        constexpr uint32_t kTriangleList = 4;

        // 0 restores the model FVF; pixel 0 is fixed function.
        constexpr uint32_t kModelFvf = 0x152;

        typedef long(__stdcall* CreateVsRoutine)(void* device, const uint32_t* decl, const uint32_t* fn, uint32_t* handle, uint32_t usage);
        typedef long(__stdcall* CreatePsRoutine)(void* device, const uint32_t* fn, uint32_t* handle);
        typedef long(__stdcall* SetShaderRoutine)(void* device, uint32_t handle);
        typedef long(__stdcall* DeleteShaderRoutine)(void* device, uint32_t handle);
        typedef long(__stdcall* SetConstantRoutine)(void* device, uint32_t reg, const void* data, uint32_t count);
        typedef long(__stdcall* SetStateRoutine)(void* device, uint32_t state, uint32_t value);
        typedef long(__stdcall* DrawUpRoutine)(void* device, uint32_t type, uint32_t primCount, const void* vertices, uint32_t stride);
        typedef long(__stdcall* GetTransformRoutine)(void* device, uint32_t which, float* matrix);
        typedef long(__stdcall* GetTextureRoutine)(void* device, uint32_t stage, void** texture);
        typedef long(__stdcall* SetTextureRoutine)(void* device, uint32_t stage, void* texture);
        typedef long(__stdcall* SetStreamRoutine)(void* device, uint32_t stream, void* buffer, uint32_t stride);
        typedef long(__stdcall* SetIndicesRoutine)(void* device, void* buffer, uint32_t baseVertex);
        typedef long(__stdcall* DrawIndexedRoutine)(void* device, uint32_t type, uint32_t minIndex, uint32_t vertexCount, uint32_t startIndex, uint32_t primitives);
        typedef unsigned long(__stdcall* ReleaseRoutine)(void* object);
        typedef long(__stdcall* CreateTextureRoutine)(void* device, uint32_t wide, uint32_t tall, uint32_t levels, uint32_t usage, uint32_t format, uint32_t pool, void** texture);
        typedef long(__stdcall* LockRectRoutine)(void* texture, uint32_t level, void* locked, const void* rect, uint32_t flags);
        typedef long(__stdcall* UnlockRectRoutine)(void* texture, uint32_t level);

        typedef long(__stdcall* AssembleRoutine)(const void* src, uint32_t len, uint32_t flags, void** constants, void** compiled, void** errors);

        template <typename Fn>
        Fn Slot(void* device, size_t slot)
        {
            if (device == nullptr)
            {
                return nullptr;
            }

            void** const vtable = *static_cast<void***>(device);
            return reinterpret_cast<Fn>(vtable[slot]);
        }

        void* Device()
        {
            return D3d8Hook::Device();
        }

        AssembleRoutine D3dxAssemble()
        {
            static bool looked = false;
            static AssembleRoutine found = nullptr;

            if (!looked)
            {
                looked = true;

                const HMODULE d3dx = LoadLibraryA("d3dx8.dll");

                if (d3dx != nullptr)
                {
                    found = reinterpret_cast<AssembleRoutine>(GetProcAddress(d3dx, "D3DXAssembleShader"));
                    FORGE_LOG_INFO("shader: d3dx8 %s", found != nullptr ? "is here, so source can go through it"
                        : "has no D3DXAssembleShader");
                }
                else
                {
                    FORGE_LOG_INFO("shader: no d3dx8 - source goes through the loader's own assembler");
                }
            }

            return found;
        }

        void ReleaseBuffer(void* buffer)
        {
            if (buffer != nullptr)
            {
                void** const vtable = *static_cast<void***>(buffer);
                // Release
                reinterpret_cast<uint32_t(__stdcall*)(void*)>(vtable[2])(buffer);
            }
        }

        void ReadBuffer(void* buffer, std::vector<uint32_t>& out)
        {
            if (buffer == nullptr)
            {
                return;
            }

            void** const vtable = *static_cast<void***>(buffer);
            const auto pointer = reinterpret_cast<void* (__stdcall*)(void*)>(vtable[3]);
            const auto size = reinterpret_cast<uint32_t(__stdcall*)(void*)>(vtable[4]);

            const uint32_t* const tokens = static_cast<const uint32_t*>(pointer(buffer));
            const uint32_t bytes = size(buffer);

            out.assign(tokens, tokens + bytes / 4);
            ReleaseBuffer(buffer);
        }

        bool Assemble(const char* source, std::vector<uint32_t>& tokens)
        {
            if (source == nullptr || source[0] == '\0')
            {
                return false;
            }

            const AssembleRoutine assemble = D3dxAssemble();

            if (assemble != nullptr)
            {
                void* compiled = nullptr;
                void* errors = nullptr;
                const long result = assemble(source, static_cast<uint32_t>(strlen(source)), 0, nullptr, &compiled, &errors);

                if (result >= 0 && compiled != nullptr)
                {
                    ReadBuffer(compiled, tokens);
                    ReleaseBuffer(errors);
                    return !tokens.empty();
                }

                FORGE_LOG_WARN("shader: d3dx8 refused the source (0x%08lX), trying our own", result);
            }

            std::string error;

            if (!AssembleShader(source, tokens, error))
            {
                FORGE_LOG_WARN("shader: %s", error.c_str());
                return false;
            }

            return true;
        }

        // vs.1.1: dp4 oPos.xyzw,v0,c0..c3 ; mov oD0,v2 ; mov oT0,v3. Tokens because d3dx8 is optional.
        const uint32_t kBasicVs[] =
        {
            0xFFFE0101,
            0x00000009, 0xC0010000, 0x90E40000, 0xA0E40000,
            0x00000009, 0xC0020000, 0x90E40000, 0xA0E40001,
            0x00000009, 0xC0040000, 0x90E40000, 0xA0E40002,
            0x00000009, 0xC0080000, 0x90E40000, 0xA0E40003,
            0x00000001, 0xD00F0000, 0x90E40002,
            0x00000001, 0xE00F0000, 0x90E40003,
            0x0000FFFF,
        };

        // Its stream: v0 float3 pos, v1 float3 normal, v2 d3dcolor, v3 float2 uv.
        const uint32_t kBasicDecl[] =
        {
            0x20000000,
            0x40020000,
            0x40020001,
            0x40040002,
            0x40010003,
            0xFFFFFFFF,
        };

        // ps.1.1: tex t0 ; mul r0,t0,v0.
        const uint32_t kBasicPs[] =
        {
            0xFFFF0101,
            0x00000042, 0xB00F0000,
            0x00000005, 0x800F0000, 0xB0E40000, 0x90E40000,
            0x0000FFFF,
        };

        // ps.1.1: mov r0,v0.
        const uint32_t kColorPs[] =
        {
            0xFFFF0101,
            0x00000001, 0x800F0000, 0x90E40000,
            0x0000FFFF,
        };

        struct Shader
        {
            ModContext* owner = nullptr;
            uint32_t id = 0;
            bool pixel = false;
            bool builtin = false;
            int which = -1;
            // the device own, rebuilt on a reset
            uint32_t handle = 0;
            std::vector<uint32_t> tokens;
            std::vector<uint32_t> declaration;
        };

        std::vector<Shader> g_shaders;
        uint32_t g_nextId = 1;
        AmodShaderApi g_table = {};

        // Where each instruction starts. In vs/ps 1.x an opcode is followed by its parameters, and
        // every parameter has the high bit set - so the stream walks itself.
        void Instructions(const std::vector<uint32_t>& tokens, std::vector<size_t>& out)
        {
            out.clear();

            for (size_t at = 1; at < tokens.size() && (tokens[at] & 0x0000FFFF) != 0x0000FFFF;)
            {
                out.push_back(at);
                ++at;

                while (at < tokens.size() && (tokens[at] & 0x80000000u) != 0)
                {
                    ++at;
                }
            }
        }

        // The device says no and nothing else. This asks it again with one instruction, then two,
        // until it says no - and names the one it stopped at, which is the whole of the answer.
        void WhichInstruction(const Shader& shader, void* device)
        {
            if (shader.pixel)
            {
                return;
            }

            std::vector<size_t> starts;

            Instructions(shader.tokens, starts);

            if (starts.size() < 2)
            {
                return;
            }

            const CreateVsRoutine create = Slot<CreateVsRoutine>(device, kCreateVertexShader);
            const DeleteShaderRoutine drop = Slot<DeleteShaderRoutine>(device, kDeleteVertexShader);
            const uint32_t* const decl = shader.declaration.empty() ? kBasicDecl : shader.declaration.data();

            if (create == nullptr)
            {
                return;
            }

            size_t took = 0;

            for (size_t count = 1; count <= starts.size(); ++count)
            {
                const size_t end = count < starts.size() ? starts[count] : shader.tokens.size() - 1;

                std::vector<uint32_t> part(shader.tokens.begin(), shader.tokens.begin() + end);

                // A vertex shader that never writes oPos is refused whatever else is in it, so
                // every piece gets one: mov oPos, c0.
                part.push_back(0x00000001);
                part.push_back(0xC00F0000);
                part.push_back(0xA0E40000);
                part.push_back(0x0000FFFF);

                uint32_t handle = 0;

                if (create(device, decl, part.data(), &handle, 0) < 0 || handle == 0)
                {
                    FORGE_LOG_WARN("shader: the device took %d instruction(s) and refused number %d "
                        "(opcode 0x%02X)", static_cast<int>(took), static_cast<int>(count),
                        shader.tokens[starts[count - 1]] & 0xFFFF);
                    return;
                }

                took = count;

                if (drop != nullptr)
                {
                    drop(device, handle);
                }
            }

            FORGE_LOG_WARN("shader: every instruction was taken on its own - %d of them; the whole "
                "is what it refused, so it is a limit and not a line", static_cast<int>(took));
        }

        // Builds a shader on the current device.
        bool Build(Shader& shader, void* device)
        {
            shader.handle = 0;

            if (device == nullptr || shader.tokens.empty())
            {
                return false;
            }

            uint32_t handle = 0;
            long result = -1;

            if (shader.pixel)
            {
                const CreatePsRoutine create = Slot<CreatePsRoutine>(device, kCreatePixelShader);
                result = create != nullptr ? create(device, shader.tokens.data(), &handle) : -1;
            }
            else
            {
                const CreateVsRoutine create = Slot<CreateVsRoutine>(device, kCreateVertexShader);
                const uint32_t* const decl = shader.declaration.empty() ? kBasicDecl : shader.declaration.data();
                result = create != nullptr ? create(device, decl, shader.tokens.data(), &handle, 0) : -1;
            }

            if (result < 0 || handle == 0)
            {
                FORGE_LOG_WARN("shader: the device refused a %s shader (0x%08lX)",
                    shader.pixel ? "pixel" : "vertex", result);
                WhichInstruction(shader, device);
                return false;
            }

            shader.handle = handle;
            return true;
        }

        Shader* Find(uint32_t id, bool pixel)
        {
            for (Shader& shader : g_shaders)
            {
                if (shader.id == id && shader.pixel == pixel)
                {
                    return &shader;
                }
            }
            return nullptr;
        }

        uint32_t Make(ModContext* owner, bool pixel, const uint32_t* tokens, int count, const uint32_t* declaration, bool builtin, int which)
        {
            void* const device = Device();

            if (device == nullptr || tokens == nullptr || count <= 0)
            {
                return AMOD_INVALID_SHADER;
            }

            Shader shader;
            shader.owner = owner;
            shader.id = g_nextId++;
            shader.pixel = pixel;
            shader.builtin = builtin;
            shader.which = which;
            shader.tokens.assign(tokens, tokens + count);

            if (!pixel && declaration != nullptr)
            {
                int span = 0;
                while (span < 256 && declaration[span] != 0xFFFFFFFF) { ++span; }
                shader.declaration.assign(declaration, declaration + span + 1);
            }

            if (!Build(shader, device))
            {
                return AMOD_INVALID_SHADER;
            }

            g_shaders.push_back(shader);
            return shader.id;
        }

        int __cdecl ApiIsAvailable(AmodMod mod)
        {
            return (Caller(mod) != nullptr && Device() != nullptr) ? 1 : 0;
        }

        int Span(const uint32_t* tokens)
        {
            int at = 0;
            while (at < 4096 && (tokens[at] & 0x0000FFFF) != 0x0000FFFF) { ++at; }
            return at + 1;
        }

        AmodVertexShader __cdecl ApiBuiltinVertex(AmodMod mod, AmodBuiltinShader which)
        {
            ModContext* const context = Caller(mod);

            if (context == nullptr || which != AMOD_SHADER_BASIC_VS)
            {
                return AMOD_INVALID_SHADER;
            }

            // Reuses an existing built-in shader.
            for (const Shader& shader : g_shaders)
            {
                if (shader.builtin && !shader.pixel && shader.which == which)
                {
                    return shader.id;
                }
            }

            return Make(context, false, kBasicVs, static_cast<int>(sizeof(kBasicVs) / 4), kBasicDecl,
                true, which);
        }

        AmodPixelShader __cdecl ApiBuiltinPixel(AmodMod mod, AmodBuiltinShader which)
        {
            ModContext* const context = Caller(mod);

            if (context == nullptr || (which != AMOD_SHADER_BASIC_PS && which != AMOD_SHADER_COLOR_PS))
            {
                return AMOD_INVALID_SHADER;
            }

            for (const Shader& shader : g_shaders)
            {
                if (shader.builtin && shader.pixel && shader.which == which)
                {
                    return shader.id;
                }
            }

            const uint32_t* const tokens = which == AMOD_SHADER_COLOR_PS ? kColorPs : kBasicPs;
            const int count = static_cast<int>((which == AMOD_SHADER_COLOR_PS ? sizeof(kColorPs) : sizeof(kBasicPs)) / 4);

            return Make(context, true, tokens, count, nullptr, true, which);
        }

        AmodVertexShader __cdecl ApiCreateVertex(AmodMod mod, const uint32_t* declaration, const uint32_t* tokens, int tokenCount)
        {
            ModContext* const context = Caller(mod);
            const int count = (tokens != nullptr && tokenCount <= 0) ? Span(tokens) : tokenCount;

            return context != nullptr ? Make(context, false, tokens, count, declaration, false, -1) : AMOD_INVALID_SHADER;
        }

        AmodPixelShader __cdecl ApiCreatePixel(AmodMod mod, const uint32_t* tokens, int tokenCount)
        {
            ModContext* const context = Caller(mod);
            const int count = (tokens != nullptr && tokenCount <= 0) ? Span(tokens) : tokenCount;

            return context != nullptr ? Make(context, true, tokens, count, nullptr, false, -1) : AMOD_INVALID_SHADER;
        }

        AmodVertexShader __cdecl ApiCreateVertexSource(AmodMod mod, const char* source, const uint32_t* declaration)
        {
            ModContext* const context = Caller(mod);
            std::vector<uint32_t> tokens;

            if (context == nullptr || !Assemble(source, tokens))
            {
                return AMOD_INVALID_SHADER;
            }

            return Make(context, false, tokens.data(), static_cast<int>(tokens.size()), declaration, false, -1);
        }

        AmodPixelShader __cdecl ApiCreatePixelSource(AmodMod mod, const char* source)
        {
            ModContext* const context = Caller(mod);
            std::vector<uint32_t> tokens;

            if (context == nullptr || !Assemble(source, tokens))
            {
                return AMOD_INVALID_SHADER;
            }

            return Make(context, true, tokens.data(), static_cast<int>(tokens.size()), nullptr, false, -1);
        }

        void Drop(Shader& shader, void* device)
        {
            if (device == nullptr || shader.handle == 0)
            {
                shader.handle = 0;
                return;
            }

            const DeleteShaderRoutine del = Slot<DeleteShaderRoutine>(device, shader.pixel ? kDeletePixelShader : kDeleteVertexShader);

            if (del != nullptr)
            {
                del(device, shader.handle);
            }
            shader.handle = 0;
        }

        int Delete(ModContext* context, uint32_t id, bool pixel)
        {
            if (context == nullptr)
            {
                return 0;
            }

            for (size_t at = 0; at < g_shaders.size(); ++at)
            {
                if (g_shaders[at].id == id && g_shaders[at].pixel == pixel)
                {
                    Drop(g_shaders[at], Device());
                    g_shaders.erase(g_shaders.begin() + at);
                    return 1;
                }
            }
            return 0;
        }

        int __cdecl ApiDeleteVertex(AmodMod mod, AmodVertexShader shader)
        {
            return Delete(Caller(mod), shader, false);
        }

        int __cdecl ApiDeletePixel(AmodMod mod, AmodPixelShader shader)
        {
            return Delete(Caller(mod), shader, true);
        }

        int __cdecl ApiSet(AmodMod mod, AmodVertexShader vertex, AmodPixelShader pixel)
        {
            void* const device = Caller(mod) != nullptr ? Device() : nullptr;

            if (device == nullptr)
            {
                return 0;
            }

            const SetShaderRoutine setVs = Slot<SetShaderRoutine>(device, kSetVertexShader);
            const SetShaderRoutine setPs = Slot<SetShaderRoutine>(device, kSetPixelShader);

            if (setVs != nullptr)
            {
                const Shader* const vs = Find(vertex, false);

                // 0 restores the fixed-function FVF.
                setVs(device, vs != nullptr ? vs->handle : kModelFvf);
            }

            if (setPs != nullptr)
            {
                const Shader* const ps = Find(pixel, true);

                setPs(device, ps != nullptr ? ps->handle : 0);
            }

            return 1;
        }

        int __cdecl ApiSetVertexConstant(AmodMod mod, int firstRegister, const float* values, int vec4Count)
        {
            void* const device = Caller(mod) != nullptr ? Device() : nullptr;
            const SetConstantRoutine set = Slot<SetConstantRoutine>(device, kSetVertexShaderConstant);

            if (set == nullptr || values == nullptr || firstRegister < 0 || vec4Count <= 0)
            {
                return 0;
            }

            return set(device, static_cast<uint32_t>(firstRegister), values, static_cast<uint32_t>(vec4Count)) >= 0 ? 1 : 0;
        }

        int __cdecl ApiSetPixelConstant(AmodMod mod, int firstRegister, const float* values, int vec4Count)
        {
            void* const device = Caller(mod) != nullptr ? Device() : nullptr;
            const SetConstantRoutine set = Slot<SetConstantRoutine>(device, kSetPixelShaderConstant);

            if (set == nullptr || values == nullptr || firstRegister < 0 || vec4Count <= 0)
            {
                return 0;
            }

            return set(device, static_cast<uint32_t>(firstRegister), values, static_cast<uint32_t>(vec4Count)) >= 0 ? 1 : 0;
        }

        // Where a draw's matrices land; c0 and c4 are what the built-in vertex shader reads.
        int g_transformRegister = 0;
        int g_worldRegister = 4;
        int g_tintRegister = -1;

        const float kIdentity[16] =
        {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };

        void Multiply(const float* left, const float* right, float* out)
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 4; ++column)
                {
                    out[row * 4 + column] =
                        left[row * 4 + 0] * right[0 * 4 + column] +
                        left[row * 4 + 1] * right[1 * 4 + column] +
                        left[row * 4 + 2] * right[2 * 4 + column] +
                        left[row * 4 + 3] * right[3 * 4 + column];
                }
            }
        }

        // A dp4 reads a column, so a matrix goes into its registers transposed.
        void SetMatrix(void* device, int firstRegister, const float* matrix)
        {
            const SetConstantRoutine set = Slot<SetConstantRoutine>(device, kSetVertexShaderConstant);

            if (set == nullptr || firstRegister < 0)
            {
                return;
            }

            float transposed[16];

            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 4; ++column)
                {
                    transposed[row * 4 + column] = matrix[column * 4 + row];
                }
            }

            set(device, static_cast<uint32_t>(firstRegister), transposed, 4);
        }

        void* SurfaceOf(AmodTextureId texture)
        {
            const uint8_t* const object = static_cast<const uint8_t*>(TextureApi::TextureFor(texture));

            return object != nullptr
                ? ApiRead::Read<void*>(object, GameAddress::kTextureSurfaceOffset)
                : nullptr;
        }

        void* g_white = nullptr;

        // A material with no texture still has to sample as itself, so t0 gets one white pixel.
        void* WhiteTexture(void* device)
        {
            if (g_white != nullptr)
            {
                return g_white;
            }

            const CreateTextureRoutine create = Slot<CreateTextureRoutine>(device, kCreateTexture);

            if (create == nullptr || create(device, 1, 1, 1, 0, kFormatArgb, kPoolManaged, &g_white) < 0)
            {
                g_white = nullptr;
                return nullptr;
            }

            struct Locked { int pitch; void* bits; } locked = {};
            const LockRectRoutine lock = Slot<LockRectRoutine>(g_white, kTextureLock);
            const UnlockRectRoutine unlock = Slot<UnlockRectRoutine>(g_white, kTextureUnlock);

            if (lock != nullptr && unlock != nullptr && lock(g_white, 0, &locked, nullptr, 0) >= 0)
            {
                *static_cast<uint32_t*>(locked.bits) = 0xFFFFFFFFu;
                unlock(g_white, 0);
            }

            return g_white;
        }

        // What the device has been told, so a run of draws that want the same thing says it once.
        // A city is hundreds of cells on one model, and that is the whole of why a batch is cheaper
        // than the same draws made one at a time.
        struct Standing
        {
            AmodModelId model = 0;
            AmodTextureId texture = 0;
            uint32_t tint = 0;
            int blend = -1;
            const float* constants = nullptr;
            bool any = false;
        };

        void SetBlend(void* device, SetStateRoutine state, int blend)
        {
            if (state == nullptr)
            {
                return;
            }

            if (blend == AMOD_DRAW_BLEND_NONE)
            {
                state(device, kStateBlend, 0);
                state(device, kStateZWrite, 1);
                return;
            }

            state(device, kStateBlend, 1);

            // An added light does not hide what is behind it, so it does not write depth either.
            state(device, kStateZWrite, blend == AMOD_DRAW_BLEND_ADD ? 0 : 1);

            switch (blend)
            {
                case AMOD_DRAW_BLEND_ADD:
                    state(device, kStateSrcBlend, kBlendSrcAlpha);
                    state(device, kStateDestBlend, kBlendOne);
                    break;

                case AMOD_DRAW_BLEND_MUL:
                    state(device, kStateSrcBlend, kBlendDestColour);
                    state(device, kStateDestBlend, kBlendZero);
                    break;

                default:
                    state(device, kStateSrcBlend, kBlendSrcAlpha);
                    state(device, kStateDestBlend, kBlendInvSrcAlpha);
                    break;
            }
        }

        void SetTint(void* device, SetConstantRoutine set, uint32_t tint)
        {
            if (set == nullptr || g_tintRegister < 0)
            {
                return;
            }

            const uint32_t wear = tint == 0 ? 0xFFFFFFFFu : tint;
            const float colour[4] =
            {
                ((wear >> 16) & 0xFF) / 255.0f,
                ((wear >> 8) & 0xFF) / 255.0f,
                (wear & 0xFF) / 255.0f,
                ((wear >> 24) & 0xFF) / 255.0f,
            };

            set(device, static_cast<uint32_t>(g_tintRegister), colour, 1);
        }

        int DrawBatch(const AmodShadedDraw* draws, int count)
        {
            void* const device = Device();

            if (device == nullptr || draws == nullptr || count <= 0)
            {
                return 0;
            }

            const SetStateRoutine state = Slot<SetStateRoutine>(device, kSetRenderState);
            const SetStreamRoutine setStream = Slot<SetStreamRoutine>(device, kSetStreamSource);
            const SetIndicesRoutine setIndices = Slot<SetIndicesRoutine>(device, kSetIndices);
            const SetTextureRoutine setTexture = Slot<SetTextureRoutine>(device, kSetTexture);
            const GetTextureRoutine getTexture = Slot<GetTextureRoutine>(device, kGetTexture);
            const DrawIndexedRoutine draw = Slot<DrawIndexedRoutine>(device, kDrawIndexedPrimitive);
            const GetTransformRoutine getTransform = Slot<GetTransformRoutine>(device, kGetTransform);
            const SetConstantRoutine setConstant = Slot<SetConstantRoutine>(device, kSetVertexShaderConstant);

            if (state == nullptr || setStream == nullptr || setIndices == nullptr || draw == nullptr)
            {
                return 0;
            }

            // Sorted by what costs a state change, not by the order the mod happened to ask in.
            std::vector<int> order;
            order.reserve(static_cast<size_t>(count));

            for (int at = 0; at < count; ++at)
            {
                if (draws[at].structSize >= sizeof(AmodShadedDraw))
                {
                    order.push_back(at);
                }
            }

            std::stable_sort(order.begin(), order.end(), [draws](int a, int b)
            {
                if (draws[a].blend != draws[b].blend)
                {
                    return draws[a].blend < draws[b].blend;
                }

                // Before the model: a set of lights is twenty-odd registers and a model is one
                // stream, so it is the dearer of the two to say twice.
                if (draws[a].constants != draws[b].constants)
                {
                    return draws[a].constants < draws[b].constants;
                }
                if (draws[a].model != draws[b].model)
                {
                    return draws[a].model < draws[b].model;
                }
                return draws[a].texture != draws[b].texture
                    ? draws[a].texture < draws[b].texture
                    : draws[a].tint < draws[b].tint;
            });

            float view[16] = {};
            float projection[16] = {};
            const bool haveCamera = g_transformRegister >= 0 && getTransform != nullptr &&
                getTransform(device, kTransformView, view) >= 0 &&
                getTransform(device, kTransformProjection, projection) >= 0;

            // What every shaded draw wants, said once for the whole batch.
            state(device, kStateLighting, 0);
            state(device, kStateZEnable, 1);
            state(device, kStateCull, kCullNone);

            void* was = nullptr;

            if (getTexture != nullptr)
            {
                getTexture(device, 0, &was);
            }

            Standing standing;
            MeshStreams streams;
            void* bound = nullptr;
            bool everBound = false;
            int drawn = 0;

            for (const int at : order)
            {
                const AmodShadedDraw& one = draws[at];

                if (!standing.any || one.model != standing.model)
                {
                    if (!MmdApi::StreamsFor(one.model, streams))
                    {
                        continue;
                    }

                    setStream(device, 0, streams.vertices, streams.stride);
                    setIndices(device, streams.indices, 0);
                    standing.model = one.model;
                }

                if (!standing.any || one.blend != standing.blend)
                {
                    SetBlend(device, state, one.blend);
                    standing.blend = one.blend;
                }

                if (!standing.any || one.tint != standing.tint)
                {
                    SetTint(device, setConstant, one.tint);
                    standing.tint = one.tint;
                }

                // What this draw brought of its own, set once for every draw that brought the same.
                if (one.constants != standing.constants)
                {
                    if (setConstant != nullptr && one.constants != nullptr &&
                        one.vec4Count > 0 && one.firstRegister >= 0)
                    {
                        setConstant(device, static_cast<uint32_t>(one.firstRegister),
                            one.constants, static_cast<uint32_t>(one.vec4Count));
                    }

                    standing.constants = one.constants;
                }

                standing.any = true;

                // The one thing no two draws can share: where this one stands.
                if (haveCamera)
                {
                    float product[16];
                    float whole[16];

                    Multiply(one.matrix, view, product);
                    Multiply(product, projection, whole);
                    SetMatrix(device, g_transformRegister, whole);
                }

                SetMatrix(device, g_worldRegister, one.matrix);

                void* const forced = one.texture != 0 ? SurfaceOf(one.texture) : nullptr;

                for (const MeshRange& range : streams.ranges)
                {
                    void* const wear = forced != nullptr ? forced : range.texture;
                    void* const bind = wear != nullptr ? wear : WhiteTexture(device);

                    if (setTexture != nullptr && (!everBound || bind != bound))
                    {
                        setTexture(device, 0, bind);
                        bound = bind;
                        everBound = true;
                    }

                    draw(device, kTriangleList, 0, streams.vertexCount, range.firstIndex,
                        range.primitiveCount);
                }

                ++drawn;
            }

            if (standing.blend != AMOD_DRAW_BLEND_NONE)
            {
                SetBlend(device, state, AMOD_DRAW_BLEND_NONE);
            }

            // The game draws on assuming what it bound is still there.
            if (setTexture != nullptr)
            {
                setTexture(device, 0, was);
            }

            if (was != nullptr)
            {
                Slot<ReleaseRoutine>(was, kRelease)(was);
            }

            return drawn;
        }

        int DrawMeshNow(AmodModelId model, const float* world, AmodTextureId texture, uint32_t tint)
        {
            AmodShadedDraw one = {};

            one.structSize = sizeof(one);
            one.model = model;
            one.texture = texture;
            one.tint = tint;
            one.blend = AMOD_DRAW_BLEND_NONE;

            if (world != nullptr)
            {
                memcpy(one.matrix, world, sizeof(one.matrix));
            }
            else
            {
                memcpy(one.matrix, kIdentity, sizeof(one.matrix));
            }

            return DrawBatch(&one, 1);
        }

        int __cdecl ApiDrawModel(AmodMod mod, AmodModelId model, const float* world, AmodTextureId texture)
        {
            return Caller(mod) != nullptr ? DrawMeshNow(model, world, texture, 0xFFFFFFFFu) : 0;
        }

        int __cdecl ApiDrawModels(AmodMod mod, const AmodShadedDraw* draws, int count)
        {
            return Caller(mod) != nullptr ? DrawBatch(draws, count) : 0;
        }

        int __cdecl ApiSetTintRegister(AmodMod mod, int tint)
        {
            if (Caller(mod) == nullptr || tint > 95)
            {
                return 0;
            }

            g_tintRegister = tint < 0 ? -1 : tint;
            return 1;
        }

        int __cdecl ApiSetTransformRegisters(AmodMod mod, int transform, int world)
        {
            if (Caller(mod) == nullptr || transform > 92 || world > 92)
            {
                return 0;
            }

            g_transformRegister = transform < 0 ? -1 : transform;
            g_worldRegister = world < 0 ? -1 : world;
            return 1;
        }

        int __cdecl ApiDrawTriangles(AmodMod mod, const void* vertices, int vertexCount, int stride)
        {
            void* const device = Caller(mod) != nullptr ? Device() : nullptr;

            if (device == nullptr || vertices == nullptr || vertexCount < 3 || stride <= 0)
            {
                return 0;
            }

            const SetStateRoutine state = Slot<SetStateRoutine>(device, kSetRenderState);
            const DrawUpRoutine draw = Slot<DrawUpRoutine>(device, kDrawPrimitiveUp);

            if (state == nullptr || draw == nullptr)
            {
                return 0;
            }

            state(device, kStateLighting, 0);
            state(device, kStateZEnable, 0);
            state(device, kStateCull, kCullNone);
            state(device, kStateBlend, 0);

            draw(device, kTriangleList, static_cast<uint32_t>(vertexCount / 3), vertices, static_cast<uint32_t>(stride));
            return 1;
        }
    }

    void ShaderApi::OnDeviceEvent(DeviceEvent event, void* device)
    {
        if (event == DeviceEvent::Lost || event == DeviceEvent::Released)
        {
            for (Shader& shader : g_shaders)
            {
                shader.handle = 0;
            }

            if (event == DeviceEvent::Released && g_white != nullptr)
            {
                Slot<ReleaseRoutine>(g_white, kRelease)(g_white);
                g_white = nullptr;
            }
            return;
        }

        if (event == DeviceEvent::Reset || event == DeviceEvent::Created)
        {
            void* const live = device != nullptr ? device : Device();

            for (Shader& shader : g_shaders)
            {
                Build(shader, live);
            }
        }
    }

    int ShaderApi::DrawMesh(AmodModelId model, const float* world, AmodTextureId texture, uint32_t tint)
    {
        return DrawMeshNow(model, world, texture, tint);
    }

    int ShaderApi::DrawMeshes(const AmodShadedDraw* draws, int count)
    {
        return DrawBatch(draws, count);
    }

    const AmodShaderApi* ShaderApi::Table()
    {
        return &g_table;
    }

    bool ShaderApi::Install()
    {
        g_table.structSize = sizeof(AmodShaderApi);
        g_table.IsAvailable = &ApiIsAvailable;
        g_table.BuiltinVertex = &ApiBuiltinVertex;
        g_table.BuiltinPixel = &ApiBuiltinPixel;
        g_table.CreateVertex = &ApiCreateVertex;
        g_table.CreatePixel = &ApiCreatePixel;
        g_table.DeleteVertex = &ApiDeleteVertex;
        g_table.DeletePixel = &ApiDeletePixel;
        g_table.Set = &ApiSet;
        g_table.SetVertexConstant = &ApiSetVertexConstant;
        g_table.SetPixelConstant = &ApiSetPixelConstant;
        g_table.DrawTriangles = &ApiDrawTriangles;
        g_table.CreateVertexSource = &ApiCreateVertexSource;
        g_table.CreatePixelSource = &ApiCreatePixelSource;
        g_table.DrawModel = &ApiDrawModel;
        g_table.SetTransformRegisters = &ApiSetTransformRegisters;
        g_table.SetTintRegister = &ApiSetTintRegister;
        g_table.DrawModels = &ApiDrawModels;

        FORGE_LOG_INFO("Shader interface installed");
        return true;
    }

    void ShaderApi::Uninstall()
    {
        void* const device = Device();

        for (Shader& shader : g_shaders)
        {
            Drop(shader, device);
        }
        g_shaders.clear();

        if (g_white != nullptr)
        {
            Slot<ReleaseRoutine>(g_white, kRelease)(g_white);
            g_white = nullptr;
        }
    }

    void ShaderApi::ForgetMod(ModContext* owner)
    {
        void* const device = Device();

        for (auto it = g_shaders.begin(); it != g_shaders.end();)
        {
            if (it->owner == owner)
            {
                Drop(*it, device);
                it = g_shaders.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}