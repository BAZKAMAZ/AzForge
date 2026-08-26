#include <windows.h>
#include <string.h>
#include <vector>

#include "src/Api/ShaderApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
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

        // Safe draw states: no depth, culling, blend or lighting.
        constexpr uint32_t kStateZEnable = 7;
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