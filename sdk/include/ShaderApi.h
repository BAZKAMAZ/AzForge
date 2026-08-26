#ifndef AZFORGE_SHADER_API_H
#define AZFORGE_SHADER_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t AmodVertexShader;
typedef uint32_t AmodPixelShader;

#define AMOD_INVALID_SHADER 0

/* Built-in shader pair. */
typedef enum AmodBuiltinShader
{
    AMOD_SHADER_BASIC_VS = 0, /* transforms by c0..c3, passes diffuse and tex0 */
    AMOD_SHADER_BASIC_PS = 1, /* tex0 * diffuse */
    AMOD_SHADER_COLOR_PS = 2  /* diffuse only */
} AmodBuiltinShader;

typedef struct AmodShaderApi
{
    uint32_t structSize;

    /* Checks shader availability on the current device. */
    int(__cdecl* IsAvailable)(AmodMod mod);

    /* Built-in shaders. */
    AmodVertexShader(__cdecl* BuiltinVertex)(AmodMod mod, AmodBuiltinShader which);
    AmodPixelShader(__cdecl* BuiltinPixel)(AmodMod mod, AmodBuiltinShader which);

    /* Creates shaders from compiled tokens. */
    AmodVertexShader(__cdecl* CreateVertex)(AmodMod mod, const uint32_t* declaration, const uint32_t* tokens, int tokenCount);
    AmodPixelShader(__cdecl* CreatePixel)(AmodMod mod, const uint32_t* tokens, int tokenCount);

    int(__cdecl* DeleteVertex)(AmodMod mod, AmodVertexShader shader);
    int(__cdecl* DeletePixel)(AmodMod mod, AmodPixelShader shader);

    /* Sets vertex/pixel shaders; 0 restores fixed function. */
    int(__cdecl* Set)(AmodMod mod, AmodVertexShader vertex, AmodPixelShader pixel);

    /* Sets float4 shader constants. */
    int(__cdecl* SetVertexConstant)(AmodMod mod, int firstRegister, const float* values, int vec4Count);
    int(__cdecl* SetPixelConstant)(AmodMod mod, int firstRegister, const float* values, int vec4Count);

    /* Draws game-layout vertices with the current shaders. */
    int(__cdecl* DrawTriangles)(AmodMod mod, const void* vertices, int vertexCount, int stride);

    /* Creates shaders from vs/ps 1.1 source. */
    AmodVertexShader(__cdecl* CreateVertexSource)(AmodMod mod, const char* source, const uint32_t* declaration);
    AmodPixelShader(__cdecl* CreatePixelSource)(AmodMod mod, const char* source);

} AmodShaderApi;

AMOD_LAST_FIELD(AmodShaderApi, CreatePixelSource, 0x34);

#ifdef __cplusplus
} /* extern "C" */

namespace Amod
{
    namespace Shader
    {
        inline const AmodShaderApi* g_shader = nullptr;
        inline AmodMod g_shaderSelf = nullptr;

        inline void Attach(const AmodShaderApi* api, AmodMod self)
        {
            g_shader = api;
            g_shaderSelf = self;
        }

        inline bool IsAvailable()
        {
            return g_shader != nullptr && g_shader->IsAvailable(g_shaderSelf) != 0;
        }

        inline AmodVertexShader BuiltinVertex(AmodBuiltinShader which)
        {
            return g_shader != nullptr ? g_shader->BuiltinVertex(g_shaderSelf, which) : AMOD_INVALID_SHADER;
        }

        inline AmodPixelShader BuiltinPixel(AmodBuiltinShader which)
        {
            return g_shader != nullptr ? g_shader->BuiltinPixel(g_shaderSelf, which) : AMOD_INVALID_SHADER;
        }

        inline AmodVertexShader CreateVertex(const uint32_t* tokens, int tokenCount, const uint32_t* declaration = nullptr)
        {
            return g_shader != nullptr ? g_shader->CreateVertex(g_shaderSelf, declaration, tokens, tokenCount) : AMOD_INVALID_SHADER;
        }

        inline AmodPixelShader CreatePixel(const uint32_t* tokens, int tokenCount)
        {
            return g_shader != nullptr ? g_shader->CreatePixel(g_shaderSelf, tokens, tokenCount) : AMOD_INVALID_SHADER;
        }

        inline bool DeleteVertex(AmodVertexShader shader)
        {
            return g_shader != nullptr && g_shader->DeleteVertex(g_shaderSelf, shader) != 0;
        }

        inline bool DeletePixel(AmodPixelShader shader)
        {
            return g_shader != nullptr && g_shader->DeletePixel(g_shaderSelf, shader) != 0;
        }

        inline bool Set(AmodVertexShader vertex, AmodPixelShader pixel)
        {
            return g_shader != nullptr && g_shader->Set(g_shaderSelf, vertex, pixel) != 0;
        }

        inline bool Clear()
        {
            return g_shader != nullptr && g_shader->Set(g_shaderSelf, AMOD_INVALID_SHADER, AMOD_INVALID_SHADER) != 0;
        }

        inline bool SetVertexConstant(int firstRegister, const float* values, int vec4Count)
        {
            return g_shader != nullptr && g_shader->SetVertexConstant(g_shaderSelf, firstRegister, values, vec4Count) != 0;
        }

        inline bool SetPixelConstant(int firstRegister, const float* values, int vec4Count)
        {
            return g_shader != nullptr && g_shader->SetPixelConstant(g_shaderSelf, firstRegister, values, vec4Count) != 0;
        }

        inline bool DrawTriangles(const void* vertices, int vertexCount, int stride)
        {
            return g_shader != nullptr && g_shader->DrawTriangles(g_shaderSelf, vertices, vertexCount, stride) != 0;
        }

        inline AmodVertexShader CreateVertexSource(const char* source, const uint32_t* declaration = nullptr)
        {
            return g_shader != nullptr ? g_shader->CreateVertexSource(g_shaderSelf, source, declaration) : AMOD_INVALID_SHADER;
        }

        inline AmodPixelShader CreatePixelSource(const char* source)
        {
            return g_shader != nullptr ? g_shader->CreatePixelSource(g_shaderSelf, source) : AMOD_INVALID_SHADER;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_SHADER_API_H */