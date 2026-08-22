#ifndef AZFORGE_MMD_API_H
#define AZFORGE_MMD_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* How a model came to be. */
#define AMOD_MODEL_KIND_MESH                                        0 /* built out of a mesh */
#define AMOD_MODEL_KIND_STATIC                                      1 /* one frame, from a file */
#define AMOD_MODEL_KIND_ANIMATED                                    2 /* several frames */

/* Which of the three models a room is drawn from. */
#define AMOD_ROOM_MODEL_GEOMETRY                                    0
#define AMOD_ROOM_MODEL_LIGHTMAP                                    1
#define AMOD_ROOM_MODEL_BACKWALL                                    2

/* A model as long as it is loaded */
typedef uint32_t AmodModelId;
#define AMOD_INVALID_MODEL                                          0

typedef struct AmodModel
{
    uint32_t structSize;

    const char* name; /* the path it was loaded by, or what a mod named it */
    int kind; /* AMOD_MODEL_KIND_* */

    int frameCount;
    int vertexCount; /* per frame */
    int indexCount; /* per frame */
    int primitiveCount;

    float centre[3]; /* the average vertex */
    float extents[3]; /* largest absolute coordinate on each axis */

    /* Models are shared: this is how many owners there are besides the first. */
    int references;

    int mine; /* the mod asking built it */
    int edited; /* a mod has written to it, and what it was is being kept */

} AmodModel;

AMOD_LAST_FIELD(AmodModel, edited, 0x3C);

/* One vertex, exactly as a .mmd holds it. */
typedef struct AmodVertex
{
    float position[3];
    float normal[3];
    float uv[2];
} AmodVertex;

AMOD_LAST_FIELD(AmodVertex, uv, 0x18);

/* Geometry handed to the loader */
typedef struct AmodMeshData
{
    uint32_t structSize;

    int frameCount; /* one for a still model */
    int vertexCount; /* per frame */
    int indexCount; /* per frame */
    int primitiveCount; /* zero takes indexCount / 3 */

    const AmodVertex* vertices; /* frameCount * vertexCount */
    const uint16_t* indices; /* frameCount * indexCount */


} AmodMeshData;

AMOD_LAST_FIELD(AmodMeshData, indices, 0x18);

/* Turn about `axis` by `angle` instead of the three eulers. */
#define AMOD_DRAW_TURNS_ABOUT_AXIS                                  0x01

/* Place it by `matrix` rather than by `position`. Only a matrix can scale. */
#define AMOD_DRAW_HAS_MATRIX                                        0x02

/* Blend modes. */
#define AMOD_DRAW_BLEND_NONE                                        0
#define AMOD_DRAW_BLEND_BLEND                                       1 /* src_alpha, inv_src_alpha */
#define AMOD_DRAW_BLEND_ADD                                         2
#define AMOD_DRAW_BLEND_MUL                                         3

/* One model put in front of the camera for one frame */
typedef struct AmodModelDraw
{
    uint32_t structSize;

    AmodModelId model;
    uint32_t texture; /* from ResolveTexture; 0 keeps whatever is already set */

    float position[3]; /* used unless AMOD_DRAW_HAS_MATRIX is set */
    float matrix[16];
    float rotation[3]; /* radians, turned Z then Y then X after the placement */

    float axis[3];
    float angle;
    uint32_t flags; /* AMOD_DRAW_* */

    float frame; /* animation position; 0 draws the model still */
    int blend; /* AMOD_DRAW_BLEND_*; anything but NONE is what makes vertex alpha count */

    /* Multiplies everything the model is drawn in, 0xAARRGGBB, for this draw only. 0 and
       0xFFFFFFFF both mean "as it is".

       This is what a colour belongs in. A model's own colours live in its vertex buffer, so
       painting them is painting every copy of that model at once - twenty cars sharing one mesh
       cannot be twenty colours that way, and the alternative is twenty meshes. A tint costs one
       render state, applies to this draw alone, and leaves the baked shading underneath it
       intact: it is multiplied over the top rather than replacing anything. */
    uint32_t tint;

} AmodModelDraw;

AMOD_LAST_FIELD(AmodModelDraw, tint, 0x80);

/* An animation as long as it is loaded, checked the way a model is. */
typedef uint32_t AmodAnimationId;
#define AMOD_INVALID_ANIMATION                                      0

typedef struct AmodAnimation
{
    uint32_t structSize;

    const char* name;
    int framesetCount;
    int references;

} AmodAnimation;

AMOD_LAST_FIELD(AmodAnimation, references, 0x0C);

/* One $frameset of a .anm: which frames it runs and how. */
typedef struct AmodFrameset
{
    uint32_t structSize;

    const char* name;
    const char* follow; /* what plays when this one ends */

    float fps;
    int startFrame;
    int from; /* $range */
    int to;
    int loop;
    int stops; /* $break: it ends rather than holding the last frame */
    int backwards; /* the range runs down */

} AmodFrameset;

AMOD_LAST_FIELD(AmodFrameset, backwards, 0x24);

    // Which of an animation's two box sets is meant
#define AMOD_ANIM_BOX_SELF                                          0 /* $anim_bbox */
#define AMOD_ANIM_BOX_ENTITY                                        1 /* $anim_bbox_entity */

/* Which vertices of a model an operation touches. Judged on the frame it is given. */
#define AMOD_VERTICES_ALL                                           0
#define AMOD_VERTICES_IN_BOX                                        1
#define AMOD_VERTICES_IN_SPHERE                                     2
#define AMOD_VERTICES_LISTED                                        3

typedef struct AmodVertexFilter
{
    uint32_t structSize;
    int kind; /* AMOD_VERTICES_* */

    float min[3]; /* IN_BOX, in the model's own coordinates */
    float max[3];

    float centre[3]; /* IN_SPHERE */
    float radius;

    const uint16_t* indices; /* LISTED */
    int indexCount;

} AmodVertexFilter;

AMOD_LAST_FIELD(AmodVertexFilter, indexCount, 0x34);

/* What an entity is playing */
typedef struct AmodAnimState
{
    uint32_t structSize;

    AmodAnimationId animation;
    const char* frameset; /* "" when it is on nothing */

    int frameA;
    int frameB;
    float blend; /* 0..1 from A to B */

    float speed; /* what Anim_Play's rate was multiplied by */
    int backwards;
    int stopped;
    int finished;

} AmodAnimState;

AMOD_LAST_FIELD(AmodAnimState, finished, 0x24);

/* A model the game is drawing, right now, with the device already set up for it. */
typedef struct AmodModelDrawInfo
{
    uint32_t structSize;

    AmodModelId model;
    int frameA; /* -1 when it is being drawn still */
    int frameB;
    float blend; /* 0..1 from A to B */

    uint32_t item; /* one drawn thing; two entities sharing a model differ here */
    uint32_t texture; /* what the game is drawing it with */
    float matrix[16]; /* model -> world, exactly as the game set it */
    float position[3]; /* the translation out of that matrix, for convenience */

} AmodModelDrawInfo;

AMOD_LAST_FIELD(AmodModelDrawInfo, position, 0x5C);

typedef void(__cdecl* AmodModelDrawHandler)(const AmodModelDrawInfo* info, void* user);

/* A real light, of the kind the device works out for itself.

   The engine ships with lighting switched off - Scene_DrawModels calls SetRenderState(LIGHTING,
   false) before it draws anything, and the vertex diffuse is forced white - which is why every
   mod so far has had to bake its shading in. But the vertex format is XYZ|NORMAL|DIFFUSE|TEX1:
   the NORMALS ARE THERE, in every vertex, and nothing but that one call stands between a mod and
   the fixed-function lighting the hardware has always done.

   Two things to know before using them:

     THE DEVICE COUNTS EIGHT AT ONCE, sometimes fewer. LightRoom says how many. A city has
     hundreds, so what is handed over each frame is the handful that matter here.

     IT IS WORKED OUT PER VERTEX, not per pixel. A road quad with four corners sixteen metres
     apart cannot show a pool of light in its middle - there is no vertex there to light. Any
     surface meant to be lit has to be a mesh, not a slab. */
typedef struct AmodLight
{
    uint32_t structSize;

    float position[3];
    uint32_t colour;        /* 0xAARRGGBB */
    float brightness;       /* colour multiplier */
    float range;            /* light range */

    /* Directional light; position and range are ignored. */
    int fromEverywhere;
    float direction[3];     /* light direction */

} AmodLight;

AMOD_LAST_FIELD(AmodLight, direction, 0x20);

/* Model index range with its own texture. */
typedef struct AmodModelMaterial
{
    uint32_t structSize;

    int firstIndex;         /* first index */
    int primitiveCount;     /* primitive count */
    uint32_t texture;       /* ResolveTexture result */

} AmodModelMaterial;

AMOD_LAST_FIELD(AmodModelMaterial, texture, 0x0C);

typedef struct AmodMmdApi
{
    uint32_t structSize;

    /* Game models. */
    int(__cdecl* GetCount)(AmodMod mod);
    AmodModelId(__cdecl* GetAt)(AmodMod mod, int index);
    AmodModelId(__cdecl* Find)(AmodMod mod, const char* name);
    int(__cdecl* Get)(AmodMod mod, AmodModelId model, AmodModel* out);
    int(__cdecl* IsValid)(AmodMod mod, AmodModelId model);

    /* Loading models. */
    AmodModelId(__cdecl* Load)(AmodMod mod, const char* path);
    int(__cdecl* Release)(AmodMod mod, AmodModelId model);

    /* Model geometry. */
    int(__cdecl* ReadVertices)(AmodMod mod, AmodModelId model, int frame, AmodVertex* out, int max);
    int(__cdecl* ReadIndices)(AmodMod mod, AmodModelId model, int frame, uint16_t* out, int max);
    int(__cdecl* WriteVertices)(AmodMod mod, AmodModelId model, int frame, const AmodVertex* vertices, int count);
    int(__cdecl* WriteIndices)(AmodMod mod, AmodModelId model, int frame, const uint16_t* indices, int count);
    AmodModelId(__cdecl* Create)(AmodMod mod, const char* name, const AmodMeshData* mesh);
    int(__cdecl* Replace)(AmodMod mod, AmodModelId model, const AmodMeshData* mesh);
    int(__cdecl* Restore)(AmodMod mod, AmodModelId model);
    int(__cdecl* Save)(AmodMod mod, AmodModelId model, const char* path);

    /* Model file overrides. */
    int(__cdecl* Override)(AmodMod mod, const char* path, AmodModelId model);
    int(__cdecl* ClearOverride)(AmodMod mod, const char* path);

    /* Room models. */
    AmodModelId(__cdecl* GetRoomModel)(AmodMod mod, int roomIndex, int slot);
    int(__cdecl* SetRoomModel)(AmodMod mod, int roomIndex, int slot, AmodModelId model);

    /* Drawing. */
    int(__cdecl* Draw)(AmodMod mod, AmodModelId model);
    int(__cdecl* DrawFrames)(AmodMod mod, AmodModelId model, int from, int to, float blend);

    /* Model properties. */
    int(__cdecl* GetProperty)(AmodMod mod, AmodModelId model, const char* key, AmodValue* value);
    int(__cdecl* SetProperty)(AmodMod mod, AmodModelId model, const char* key, const AmodValue* value);
    int(__cdecl* HasProperty)(AmodMod mod, const char* key);
    int(__cdecl* PropertyCount)(AmodMod mod);
    const char* (__cdecl* PropertyNameAt)(AmodMod mod, int index);

    /* Frame draw queue. */
    int(__cdecl* Enqueue)(AmodMod mod, const AmodModelDraw* draw);
    int(__cdecl* QueueRoom)(AmodMod mod);

    /* Resolves a texture by name. */
    uint32_t(__cdecl* ResolveTexture)(AmodMod mod, const char* name);

    /* Current camera matrices. */
    int(__cdecl* GetViewMatrix)(AmodMod mod, float* out);
    int(__cdecl* GetProjectionMatrix)(AmodMod mod, float* out);

    /* Animations. */
    int(__cdecl* GetAnimationCount)(AmodMod mod);
    AmodAnimationId(__cdecl* GetAnimationAt)(AmodMod mod, int index);
    AmodAnimationId(__cdecl* FindAnimation)(AmodMod mod, const char* name);
    int(__cdecl* GetAnimation)(AmodMod mod, AmodAnimationId animation, AmodAnimation* out);
    int(__cdecl* GetFrameset)(AmodMod mod, AmodAnimationId animation, int index, AmodFrameset* out);
    AmodAnimationId(__cdecl* LoadAnimation)(AmodMod mod, const char* path);
    int(__cdecl* ReleaseAnimation)(AmodMod mod, AmodAnimationId animation);
    int(__cdecl* OverrideAnimation)(AmodMod mod, const char* path, AmodAnimationId animation);
    int(__cdecl* ClearAnimationOverride)(AmodMod mod, const char* path);

    /* Vertex operations. */
    int(__cdecl* GetVertex)(AmodMod mod, AmodModelId model, int frame, int index, AmodVertex* out);
    int(__cdecl* SelectVertices)(AmodMod mod, AmodModelId model, int frame, const AmodVertexFilter* filter, uint16_t* out, int max);
    int(__cdecl* TransformVertices)(AmodMod mod, AmodModelId model, int frame, const AmodVertexFilter* filter, const float* matrix);

    /* Vertex colours. */
    int(__cdecl* SetColours)(AmodMod mod, AmodModelId model, const uint32_t* colours, int count);
    int(__cdecl* ClearColours)(AmodMod mod, AmodModelId model);

    /* Entity animation. */
    int(__cdecl* GetEntityAnim)(AmodMod mod, int roomIndex, int index, AmodAnimState* out);
    int(__cdecl* PlayEntityAnim)(AmodMod mod, int roomIndex, int index, const char* frameset);
    int(__cdecl* StopEntityAnim)(AmodMod mod, int roomIndex, int index);

    /* Model draw handlers. */
    AmodToken(__cdecl* AddDrawHandler)(AmodMod mod, AmodModelId model, AmodModelDrawHandler handler, void* user);
    void(__cdecl* RemoveDrawHandler)(AmodMod mod, AmodToken token);
    int(__cdecl* DrawWith)(AmodMod mod, AmodModelId model, uint32_t texture, const float* matrix);

    int(__cdecl* GetAnimBox)(AmodMod mod, AmodAnimationId animation, int which, int frame, AmodBox* out);
    int(__cdecl* SetAnimBox)(AmodMod mod, AmodAnimationId animation, int which, int frame, const AmodBox* box);
    int(__cdecl* GetAnimBoxFrames)(AmodMod mod, AmodAnimationId animation, int which);
    int(__cdecl* EnsureAnimBox)(AmodMod mod, AmodAnimationId animation, int which, int frames);

    AmodToken(__cdecl* AddPreDrawHandler)(AmodMod mod, AmodModelId model, AmodModelDrawHandler handler, void* user);
    int(__cdecl* RebuildNormals)(AmodMod mod, AmodModelId model, int frame);

    /* Handler tokens; Remove* is a legacy alias. */
    void(__cdecl* Cancel)(AmodMod mod, AmodToken token);

    /* Model materials. */
    int(__cdecl* AddMaterial)(AmodMod mod, AmodModelId model, int firstIndex, int primitiveCount, uint32_t texture);
    int(__cdecl* MaterialCount)(AmodMod mod, AmodModelId model);
    int(__cdecl* GetMaterial)(AmodMod mod, AmodModelId model, int index, AmodModelMaterial* out);
    int(__cdecl* ClearMaterials)(AmodMod mod, AmodModelId model);

    /* Draws a list without queue limits. */
    int(__cdecl* DrawMany)(AmodMod mod, const AmodModelDraw* draws, int count);

    /* Global draw tint. */
    void(__cdecl* SetWorldTint)(AmodMod mod, uint32_t colour);
    uint32_t(__cdecl* WorldTint)(AmodMod mod);

    /* Dynamic lights. */
    int(__cdecl* LightRoom)(AmodMod mod);
    int(__cdecl* SetLight)(AmodMod mod, int index, const AmodLight* light);

    /* Ambient light colour. */
    void(__cdecl* SetAmbient)(AmodMod mod, uint32_t colour);
    uint32_t(__cdecl* Ambient)(AmodMod mod);

} AmodMmdApi;

AMOD_LAST_FIELD(AmodMmdApi, Ambient, 0x114);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

#include <stdlib.h>

namespace Amod
{
    namespace Mmd
    {
        inline const AmodMmdApi* g_mmd = nullptr;
        inline AmodMod g_mmdSelf = nullptr;

        inline void                                                 Attach(const AmodMmdApi* api, AmodMod self)
        {
            g_mmd = api;
            g_mmdSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_mmd != nullptr;
        }

        inline int                                                  Count()
        {
            return g_mmd != nullptr ? g_mmd->GetCount(g_mmdSelf) : 0;
        }

        inline AmodModelId                                          At(int index)
        {
            return g_mmd != nullptr ? g_mmd->GetAt(g_mmdSelf, index) : AMOD_INVALID_MODEL;
        }

        inline AmodModelId                                          Find(const char* name)
        {
            return g_mmd != nullptr ? g_mmd->Find(g_mmdSelf, name) : AMOD_INVALID_MODEL;
        }

        inline AmodModelId                                          Load(const char* path)
        {
            return g_mmd != nullptr ? g_mmd->Load(g_mmdSelf, path) : AMOD_INVALID_MODEL;
        }

        inline bool                                                 Release(AmodModelId model)
        {
            return g_mmd != nullptr && g_mmd->Release(g_mmdSelf, model) != 0;
        }

        inline bool                                                 Get(AmodModelId model, AmodModel& out)
        {
            out = AmodModel{};
            out.structSize = sizeof(out);
            return g_mmd != nullptr && g_mmd->Get(g_mmdSelf, model, &out) != 0;
        }

        inline bool                                                 IsValid(AmodModelId model)
        {
            return g_mmd != nullptr && g_mmd->IsValid(g_mmdSelf, model) != 0;
        }

        inline int                                                  ReadVertices(AmodModelId model, int frame, AmodVertex* out, int max)
        {
            return g_mmd != nullptr ? g_mmd->ReadVertices(g_mmdSelf, model, frame, out, max) : 0;
        }

        inline int                                                  ReadIndices(AmodModelId model, int frame, uint16_t* out, int max)
        {
            return g_mmd != nullptr ? g_mmd->ReadIndices(g_mmdSelf, model, frame, out, max) : 0;
        }

        inline bool                                                 WriteVertices(AmodModelId model, int frame, const AmodVertex* vertices, int count)
        {
            return g_mmd != nullptr &&
                g_mmd->WriteVertices(g_mmdSelf, model, frame, vertices, count) != 0;
        }

        inline bool                                                 WriteIndices(AmodModelId model, int frame, const uint16_t* indices, int count)
        {
            return g_mmd != nullptr &&
                g_mmd->WriteIndices(g_mmdSelf, model, frame, indices, count) != 0;
        }

        inline bool                                                 Restore(AmodModelId model)
        {
            return g_mmd != nullptr && g_mmd->Restore(g_mmdSelf, model) != 0;
        }

        inline bool                                                 Override(const char* path, AmodModelId model)
        {
            return g_mmd != nullptr && g_mmd->Override(g_mmdSelf, path, model) != 0;
        }

        inline bool                                                 ClearOverride(const char* path)
        {
            return g_mmd != nullptr && g_mmd->ClearOverride(g_mmdSelf, path) != 0;
        }

        inline AmodModelId                                          RoomModel(int roomIndex = -1, int slot = AMOD_ROOM_MODEL_GEOMETRY)
        {
            return g_mmd != nullptr ? g_mmd->GetRoomModel(g_mmdSelf, roomIndex, slot)
                : AMOD_INVALID_MODEL;
        }

        inline bool                                                 SetRoomModel(int roomIndex, int slot, AmodModelId model)
        {
            return g_mmd != nullptr && g_mmd->SetRoomModel(g_mmdSelf, roomIndex, slot, model) != 0;
        }

        inline bool                                                 Draw(AmodModelId model)
        {
            return g_mmd != nullptr && g_mmd->Draw(g_mmdSelf, model) != 0;
        }

        inline bool                                                 DrawFrames(AmodModelId model, int from, int to, float blend)
        {
            return g_mmd != nullptr && g_mmd->DrawFrames(g_mmdSelf, model, from, to, blend) != 0;
        }

        /* The mesh a mod holds in arrays of its own. Frames follow one another in both. */
        inline AmodMeshData                                         Mesh(const AmodVertex* vertices, int vertexCount, const uint16_t* indices, int indexCount, int frameCount = 1)
        {
            AmodMeshData mesh = {};
            mesh.structSize = sizeof(mesh);
            mesh.frameCount = frameCount;
            mesh.vertexCount = vertexCount;
            mesh.indexCount = indexCount;
            mesh.vertices = vertices;
            mesh.indices = indices;
            return mesh;
        }

        inline AmodModelId                                          Create(const char* name, const AmodMeshData& mesh)
        {
            return g_mmd != nullptr ? g_mmd->Create(g_mmdSelf, name, &mesh) : AMOD_INVALID_MODEL;
        }

        inline bool                                                 Save(AmodModelId model, const char* path)
        {
            return g_mmd != nullptr && g_mmd->Save != nullptr &&
                g_mmd->Save(g_mmdSelf, model, path) != 0;
        }

        inline bool                                                 Replace(AmodModelId model, const AmodMeshData& mesh)
        {
            return g_mmd != nullptr && g_mmd->Replace(g_mmdSelf, model, &mesh) != 0;
        }

        /* In front of the camera, so z belongs between -0.1 and -10 */
        inline AmodModelDraw                                        Placement(AmodModelId model, float x, float y,
            float z, float scale = 0.0f)
        {
            AmodModelDraw draw = {};
            draw.structSize = sizeof(draw);
            draw.model = model;
            draw.position[0] = x;
            draw.position[1] = y;
            draw.position[2] = z;

            if (scale > 0.0f)
            {
                draw.flags |= AMOD_DRAW_HAS_MATRIX;
                draw.matrix[0] = scale;
                draw.matrix[5] = scale;
                draw.matrix[10] = scale;
                draw.matrix[15] = 1.0f;
                draw.matrix[12] = x;
                draw.matrix[13] = y;
                draw.matrix[14] = z;
            }
            return draw;
        }

        inline bool                                                 Enqueue(const AmodModelDraw& draw)
        {
            return g_mmd != nullptr && g_mmd->Enqueue != nullptr &&
                g_mmd->Enqueue(g_mmdSelf, &draw) != 0;
        }

        inline int                                                  QueueRoom()
        {
            return (g_mmd != nullptr && g_mmd->QueueRoom != nullptr) ? g_mmd->QueueRoom(g_mmdSelf) : 0;
        }

        inline uint32_t                                             Texture(const char* name)
        {
            return (g_mmd != nullptr && g_mmd->ResolveTexture != nullptr)
                ? g_mmd->ResolveTexture(g_mmdSelf, name)
                : 0;
        }

        inline bool                                                 View(float* out16)
        {
            return g_mmd != nullptr && g_mmd->GetViewMatrix != nullptr &&
                g_mmd->GetViewMatrix(g_mmdSelf, out16) != 0;
        }

        inline bool                                                 Projection(float* out16)
        {
            return g_mmd != nullptr && g_mmd->GetProjectionMatrix != nullptr &&
                g_mmd->GetProjectionMatrix(g_mmdSelf, out16) != 0;
        }

        inline int                                                  AnimationCount()
        {
            return (g_mmd != nullptr && g_mmd->GetAnimationCount != nullptr)
                ? g_mmd->GetAnimationCount(g_mmdSelf)
                : 0;
        }

        inline AmodAnimationId                                      AnimationAt(int index)
        {
            return (g_mmd != nullptr && g_mmd->GetAnimationAt != nullptr)
                ? g_mmd->GetAnimationAt(g_mmdSelf, index)
                : AMOD_INVALID_ANIMATION;
        }

        inline AmodAnimationId                                      FindAnimation(const char* name)
        {
            return (g_mmd != nullptr && g_mmd->FindAnimation != nullptr)
                ? g_mmd->FindAnimation(g_mmdSelf, name)
                : AMOD_INVALID_ANIMATION;
        }

        inline AmodAnimationId                                      LoadAnimation(const char* path)
        {
            return (g_mmd != nullptr && g_mmd->LoadAnimation != nullptr)
                ? g_mmd->LoadAnimation(g_mmdSelf, path)
                : AMOD_INVALID_ANIMATION;
        }

        inline bool                                                 ReleaseAnimation(AmodAnimationId animation)
        {
            return g_mmd != nullptr && g_mmd->ReleaseAnimation != nullptr &&
                g_mmd->ReleaseAnimation(g_mmdSelf, animation) != 0;
        }

        inline bool                                                 GetAnimation(AmodAnimationId animation,
            AmodAnimation& out)
        {
            out = AmodAnimation{};
            out.structSize = sizeof(out);
            return g_mmd != nullptr && g_mmd->GetAnimation != nullptr &&
                g_mmd->GetAnimation(g_mmdSelf, animation, &out) != 0;
        }

        inline bool                                                 GetFrameset(AmodAnimationId animation, int index,
            AmodFrameset& out)
        {
            out = AmodFrameset{};
            out.structSize = sizeof(out);
            return g_mmd != nullptr && g_mmd->GetFrameset != nullptr &&
                g_mmd->GetFrameset(g_mmdSelf, animation, index, &out) != 0;
        }

        inline bool                                                 GetAnimBox(AmodAnimationId animation, int which, int frame, AmodBox& out)
        {
            out = AmodBox{};
            out.structSize = sizeof(out);
            return g_mmd != nullptr && g_mmd->GetAnimBox != nullptr && g_mmd->GetAnimBox(g_mmdSelf, animation, which, frame, &out) != 0;
        }

        inline bool                                                 SetAnimBox(AmodAnimationId animation, int which, int frame, const AmodBox& box)
        {
            return g_mmd != nullptr && g_mmd->SetAnimBox != nullptr && g_mmd->SetAnimBox(g_mmdSelf, animation, which, frame, &box) != 0;
        }

        inline int                                                  AnimBoxFrames(AmodAnimationId animation, int which)
        {
            return (g_mmd != nullptr && g_mmd->GetAnimBoxFrames != nullptr) ? g_mmd->GetAnimBoxFrames(g_mmdSelf, animation, which) : 0;
        }

        inline bool                                                 EnsureAnimBox(AmodAnimationId animation, int which, int frames)
        {
            return g_mmd != nullptr && g_mmd->EnsureAnimBox != nullptr && g_mmd->EnsureAnimBox(g_mmdSelf, animation, which, frames) != 0;
        }

        // After moving vertices about, so the light shades the shape that is there now.
        inline bool                                                 RebuildNormals(AmodModelId model, int frame = -1)
        {
            return g_mmd != nullptr && g_mmd->RebuildNormals != nullptr && g_mmd->RebuildNormals(g_mmdSelf, model, frame) != 0;
        }

        inline bool                                                 OverrideAnimation(const char* path,
            AmodAnimationId animation)
        {
            return g_mmd != nullptr && g_mmd->OverrideAnimation != nullptr &&
                g_mmd->OverrideAnimation(g_mmdSelf, path, animation) != 0;
        }

        inline bool                                                 ClearAnimationOverride(const char* path)
        {
            return g_mmd != nullptr && g_mmd->ClearAnimationOverride != nullptr &&
                g_mmd->ClearAnimationOverride(g_mmdSelf, path) != 0;
        }

        inline AmodVertexFilter                                     Everything()
        {
            AmodVertexFilter filter = {};
            filter.structSize = sizeof(filter);
            filter.kind = AMOD_VERTICES_ALL;
            return filter;
        }

        inline AmodVertexFilter                                     InBox(float minX, float minY, float minZ,
            float maxX, float maxY, float maxZ)
        {
            AmodVertexFilter filter = Everything();
            filter.kind = AMOD_VERTICES_IN_BOX;
            filter.min[0] = minX; filter.min[1] = minY; filter.min[2] = minZ;
            filter.max[0] = maxX; filter.max[1] = maxY; filter.max[2] = maxZ;
            return filter;
        }

        inline AmodVertexFilter                                     InSphere(float x, float y, float z, float radius)
        {
            AmodVertexFilter filter = Everything();
            filter.kind = AMOD_VERTICES_IN_SPHERE;
            filter.centre[0] = x; filter.centre[1] = y; filter.centre[2] = z;
            filter.radius = radius;
            return filter;
        }

        inline AmodVertexFilter                                     OfThese(const uint16_t* indices, int count)
        {
            AmodVertexFilter filter = Everything();
            filter.kind = AMOD_VERTICES_LISTED;
            filter.indices = indices;
            filter.indexCount = count;
            return filter;
        }

        /* Row vectors, so translation sits in 12, 13 and 14 - the game's own layout. */
        struct Matrix
        {
            float m[16];

            static Matrix                                           Identity()
            {
                Matrix out = {};
                out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.0f;
                return out;
            }

            static Matrix                                           Move(float x, float y, float z)
            {
                Matrix out = Identity();
                out.m[12] = x; out.m[13] = y; out.m[14] = z;
                return out;
            }

            static Matrix                                           Resize(float x, float y, float z)
            {
                Matrix out = Identity();
                out.m[0] = x; out.m[5] = y; out.m[10] = z;
                return out;
            }

            static Matrix                                           Resize(float by) { return Resize(by, by, by); }

            Matrix                                                  Then(const Matrix& next) const
            {
                Matrix out = {};
                for (int row = 0; row < 4; ++row)
                {
                    for (int column = 0; column < 4; ++column)
                    {
                        float sum = 0.0f;
                        for (int at = 0; at < 4; ++at)
                        {
                            sum += m[row * 4 + at] * next.m[at * 4 + column];
                        }
                        out.m[row * 4 + column] = sum;
                    }
                }
                return out;
            }
        };

        inline bool                                                 GetVertex(AmodModelId model, int frame,
            int index, AmodVertex& out)
        {
            return g_mmd != nullptr && g_mmd->GetVertex != nullptr &&
                g_mmd->GetVertex(g_mmdSelf, model, frame, index, &out) != 0;
        }

        inline int                                                  SelectVertices(AmodModelId model, int frame,
            const AmodVertexFilter& filter,
            uint16_t* out, int max)
        {
            return (g_mmd != nullptr && g_mmd->SelectVertices != nullptr)
                ? g_mmd->SelectVertices(g_mmdSelf, model, frame, &filter, out, max)
                : 0;
        }

        inline bool                                                 Transform(AmodModelId model, int frame,
            const AmodVertexFilter& filter,
            const Matrix& matrix)
        {
            return g_mmd != nullptr && g_mmd->TransformVertices != nullptr &&
                g_mmd->TransformVertices(g_mmdSelf, model, frame, &filter, matrix.m) != 0;
        }

        /* Every frame of the whole model. */
        inline bool                                                 Transform(AmodModelId model, const Matrix& matrix)
        {
            return Transform(model, -1, Everything(), matrix);
        }

        inline bool                                                 SetColours(AmodModelId model,
            const uint32_t* colours, int count)
        {
            return g_mmd != nullptr && g_mmd->SetColours != nullptr &&
                g_mmd->SetColours(g_mmdSelf, model, colours, count) != 0;
        }

        /* The whole model one colour. */
        inline bool                                                 SetColour(AmodModelId model, uint32_t colour)
        {
            AmodModel about;
            if (!Get(model, about) || about.vertexCount <= 0)
            {
                return false;
            }

            bool done = false;
            uint32_t* const colours =
                static_cast<uint32_t*>(malloc(sizeof(uint32_t) * about.vertexCount));

            if (colours != nullptr)
            {
                for (int at = 0; at < about.vertexCount; ++at)
                {
                    colours[at] = colour;
                }
                done = SetColours(model, colours, about.vertexCount);
                free(colours);
            }
            return done;
        }

        inline bool                                                 ClearColours(AmodModelId model)
        {
            return g_mmd != nullptr && g_mmd->ClearColours != nullptr &&
                g_mmd->ClearColours(g_mmdSelf, model) != 0;
        }

        inline bool                                                 GetEntityAnim(int index, AmodAnimState& out,
            int roomIndex = -1)
        {
            out = AmodAnimState{};
            out.structSize = sizeof(out);
            return g_mmd != nullptr && g_mmd->GetEntityAnim != nullptr &&
                g_mmd->GetEntityAnim(g_mmdSelf, roomIndex, index, &out) != 0;
        }

        inline bool                                                 PlayEntityAnim(int index, const char* frameset, int roomIndex = -1)
        {
            return g_mmd != nullptr && g_mmd->PlayEntityAnim != nullptr &&
                g_mmd->PlayEntityAnim(g_mmdSelf, roomIndex, index, frameset) != 0;
        }

        inline bool                                                 StopEntityAnim(int index, int roomIndex = -1)
        {
            return g_mmd != nullptr && g_mmd->StopEntityAnim != nullptr &&
                g_mmd->StopEntityAnim(g_mmdSelf, roomIndex, index) != 0;
        }

        inline AmodToken                                            OnDraw(AmodModelId model, AmodModelDrawHandler handler, void* user = nullptr)
        {
            return (g_mmd != nullptr && g_mmd->AddDrawHandler != nullptr)
                ? g_mmd->AddDrawHandler(g_mmdSelf, model, handler, user)
                : AMOD_INVALID_TOKEN;
        }
        inline AmodToken                                            BeforeDraw(AmodModelId model, AmodModelDrawHandler handler, void* user = nullptr)
        {
            return (g_mmd != nullptr && g_mmd->AddPreDrawHandler != nullptr)
                ? g_mmd->AddPreDrawHandler(g_mmdSelf, model, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline void                                                 Cancel(AmodToken token)
        {
            if (g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi) && g_mmd->Cancel != nullptr)
            {
                g_mmd->Cancel(g_mmdSelf, token);
            }
        }

        inline int                                                  AddMaterial(AmodModelId model, int firstIndex, int primitiveCount, uint32_t texture)
        {
            return (g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi))
                ? g_mmd->AddMaterial(g_mmdSelf, model, firstIndex, primitiveCount, texture)
                : -1;
        }

        inline int                                                  MaterialCount(AmodModelId model)
        {
            return (g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi))
                ? g_mmd->MaterialCount(g_mmdSelf, model)
                : 0;
        }

        inline bool                                                 GetMaterial(AmodModelId model, int index, AmodModelMaterial* out)
        {
            if (g_mmd == nullptr || g_mmd->structSize < sizeof(AmodMmdApi) || out == nullptr)
            {
                return false;
            }
            out->structSize = sizeof(AmodModelMaterial);
            return g_mmd->GetMaterial(g_mmdSelf, model, index, out) != 0;
        }

        inline bool                                                 ClearMaterials(AmodModelId model)
        {
            return g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi) &&
                   g_mmd->ClearMaterials(g_mmdSelf, model) != 0;
        }

        inline bool                                                 DrawWith(AmodModelId model, uint32_t texture,
            const Matrix& matrix)
        {
            return g_mmd != nullptr && g_mmd->DrawWith != nullptr &&
                g_mmd->DrawWith(g_mmdSelf, model, texture, matrix.m) != 0;
        }

        inline void                                                 SetWorldTint(uint32_t colour)
        {
            if (g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi))
            {
                g_mmd->SetWorldTint(g_mmdSelf, colour);
            }
        }

        inline int                                                  LightRoom()
        {
            return (g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi))
                ? g_mmd->LightRoom(g_mmdSelf)
                : 0;
        }

        inline bool                                                 SetLight(int index, const AmodLight& light)
        {
            AmodLight one = light;
            one.structSize = sizeof(one);

            return g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi) &&
                   g_mmd->SetLight(g_mmdSelf, index, &one) != 0;
        }

        inline bool                                                 ClearLight(int index)
        {
            return g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi) &&
                   g_mmd->SetLight(g_mmdSelf, index, nullptr) != 0;
        }

        inline void                                                 SetAmbient(uint32_t colour)
        {
            if (g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi))
            {
                g_mmd->SetAmbient(g_mmdSelf, colour);
            }
        }

        inline uint32_t                                             Ambient()
        {
            return (g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi))
                ? g_mmd->Ambient(g_mmdSelf)
                : 0xFF000000u;
        }

        inline uint32_t                                             WorldTint()
        {
            return (g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi))
                ? g_mmd->WorldTint(g_mmdSelf)
                : 0xFFFFFFFFu;
        }

        inline int                                                  DrawMany(const AmodModelDraw* draws, int count)
        {
            return (g_mmd != nullptr && g_mmd->structSize >= sizeof(AmodMmdApi) && g_mmd->DrawMany != nullptr)
                ? g_mmd->DrawMany(g_mmdSelf, draws, count)
                : 0;
        }

        class Property
        {
        public:
            Property(AmodModelId model, const char* key) : m_model(model), m_key(key) {}

            bool                                                    Read(AmodValue& value) const
            {
                return g_mmd != nullptr && g_mmd->GetProperty(g_mmdSelf, m_model, m_key, &value) != 0;
            }

            bool                                                    Set(const AmodValue& value) const
            {
                return g_mmd != nullptr && g_mmd->SetProperty(g_mmdSelf, m_model, m_key, &value) != 0;
            }

            bool                                                    Set(int number) const { return Set(Amod::Value(number)); }
            bool                                                    Set(float a, float b, float c) const { return Set(Amod::Value(a, b, c)); }

            int                                                     Integer(int fallback = 0) const
            {
                AmodValue value = {};
                if (!Read(value) || (value.kind != AMOD_VALUE_INT && value.kind != AMOD_VALUE_BOOL))
                {
                    return fallback;
                }
                return value.integer;
            }

            float                                                   Number(int index = 0, float fallback = 0.0f) const
            {
                AmodValue value = {};
                if (!Read(value) || value.kind != AMOD_VALUE_FLOAT ||
                    index < 0 || static_cast<uint32_t>(index) >= value.count)
                {
                    return fallback;
                }
                return value.numbers[index];
            }

            const char* Text(const char* fallback = "") const
            {
                AmodValue value = {};
                if (!Read(value) || value.kind != AMOD_VALUE_STRING || value.text == nullptr)
                {
                    return fallback;
                }
                return value.text;
            }

        private:
            AmodModelId m_model;
            const char* m_key;
        };

        class ModelRef
        {
        public:
            explicit ModelRef(AmodModelId model) : m_model(model) {}
            Property                                                operator[](const char* key) const { return Property(m_model, key); }

        private:
            AmodModelId m_model;
        };

        inline ModelRef                                             Of(AmodModelId model) { return ModelRef(model); }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_MMD_API_H */
