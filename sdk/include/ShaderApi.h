#ifndef AZFORGE_SHADER_API_H
#define AZFORGE_SHADER_API_H

#include "ForgeTypes.h"
#include "MmdApi.h"
#include "TextureApi.h"

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

/* One model in a batch: where it stands, what it wears, and how it is blended. */
typedef struct AmodShadedDraw
{
    uint32_t structSize;

    AmodModelId model;
    AmodTextureId texture;  /* 0 keeps the model's own */
    uint32_t tint;          /* 0xAARRGGBB; reaches the shader at the tint register */
    int blend;              /* AMOD_DRAW_BLEND_* */
    float matrix[16];       /* the world matrix, row major */

    /* Constants this draw wants set before it, and nobody else needs: the lights that reach THIS
       model, and not the ones that reach the rest of the world. A shader holds ten lights at once
       and a map holds a hundred - the ten are per draw, which is what makes the hundred possible.
       Draws pointing at the same block are drawn together and it is uploaded once. Null for none. */
    const float* constants;
    int firstRegister;
    int vec4Count;

} AmodShadedDraw;

AMOD_LAST_FIELD(AmodShadedDraw, vec4Count, 0x5C);

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

    /* Draws a model with the current shaders; texture 0 keeps the model's own. */
    int(__cdecl* DrawModel)(AmodMod mod, AmodModelId model, const float* world, AmodTextureId texture);

    /* Where the loader writes the matrices it makes for a draw; -1 for neither. */
    int(__cdecl* SetTransformRegisters)(AmodMod mod, int worldViewProjection, int world);

    /* Where a draw's tint lands, as red, green, blue, alpha in 0..1; -1 for nowhere. */
    int(__cdecl* SetTintRegister)(AmodMod mod, int tint);

    /* Draws many models with the current shaders, sorted so a state is set once for a run. */
    int(__cdecl* DrawModels)(AmodMod mod, const AmodShadedDraw* draws, int count);

} AmodShaderApi;

AMOD_LAST_FIELD(AmodShaderApi, DrawModels, 0x44);

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

        inline bool DrawModel(AmodModelId model, const float* world, AmodTextureId texture = 0)
        {
            return g_shader != nullptr && g_shader->structSize >= sizeof(AmodShaderApi) &&
                   g_shader->DrawModel(g_shaderSelf, model, world, texture) != 0;
        }

        inline bool SetTransformRegisters(int worldViewProjection, int world)
        {
            return g_shader != nullptr && g_shader->structSize >= sizeof(AmodShaderApi) &&
                   g_shader->SetTransformRegisters(g_shaderSelf, worldViewProjection, world) != 0;
        }

        inline int DrawModels(const AmodShadedDraw* draws, int count)
        {
            return (g_shader != nullptr && g_shader->structSize >= sizeof(AmodShaderApi))
                ? g_shader->DrawModels(g_shaderSelf, draws, count) : 0;
        }

        inline bool SetTintRegister(int tint)
        {
            return g_shader != nullptr && g_shader->structSize >= sizeof(AmodShaderApi) &&
                   g_shader->SetTintRegister(g_shaderSelf, tint) != 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_SHADER_API_H */