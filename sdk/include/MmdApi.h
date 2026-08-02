#ifndef AZFORGE_MMD_API_H
#define AZFORGE_MMD_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* How a model came to be. */
#define AMOD_MODEL_KIND_MESH                                        0   /* built out of a mesh */
#define AMOD_MODEL_KIND_STATIC                                      1   /* one frame, from a file */
#define AMOD_MODEL_KIND_ANIMATED                                    2   /* several frames */

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

        const char* name;           /* the path it was loaded by, or what a mod named it */
        int kind;                   /* AMOD_MODEL_KIND_* */

        int frameCount;
        int vertexCount;            /* per frame */
        int indexCount;             /* per frame */
        int primitiveCount;

        float centre[3];            /* the average vertex */
        float extents[3];           /* largest absolute coordinate on each axis */

        /* Models are shared: this is how many owners there are besides the first. */
        int references;

        int mine;                   /* the mod asking built it */
        int edited;                 /* a mod has written to it, and what it was is being kept */

        
    } AmodModel;

    /* One vertex, exactly as a .mmd holds it. */
    typedef struct AmodVertex
    {
        float position[3];
        float normal[3];
        float uv[2];
    } AmodVertex;

    /* Geometry handed to the loader. Frames follow one another; every frame has the same counts. */
    typedef struct AmodMeshData
    {
        uint32_t structSize;

        int frameCount;             /* one for a still model */
        int vertexCount;            /* per frame */
        int indexCount;             /* per frame */
        int primitiveCount;         /* zero takes indexCount / 3 */

        const AmodVertex* vertices; /* frameCount * vertexCount */
        const uint16_t* indices;    /* frameCount * indexCount */

        
    } AmodMeshData;

    /* Turn about `axis` by `angle` instead of the three eulers. */
#define AMOD_DRAW_TURNS_ABOUT_AXIS                                  0x01

/* Place it by `matrix` rather than by `position`. Only a matrix can scale. */
#define AMOD_DRAW_HAS_MATRIX                                        0x02

/* One model put in front of the camera for one frame */
    typedef struct AmodModelDraw
    {
        uint32_t structSize;

        AmodModelId model;
        uint32_t texture;           /* from ResolveTexture; 0 keeps whatever is already set */

        float position[3];          /* used unless AMOD_DRAW_HAS_MATRIX is set */
        float matrix[16];
        float rotation[3];          /* radians, turned Z then Y then X after the placement */

        float axis[3];
        float angle;
        uint32_t flags;             /* AMOD_DRAW_* */

        float frame;                /* animation position; 0 draws the model still */

    } AmodModelDraw;

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

    /* One $frameset of a .anm: which frames it runs and how. */
    typedef struct AmodFrameset
    {
        uint32_t structSize;

        const char* name;
        const char* follow;         /* what plays when this one ends */

        float fps;
        int startFrame;
        int from;                   /* $range */
        int to;
        int loop;
        int stops;                  /* $break: it ends rather than holding the last frame */
        int backwards;              /* the range runs down */

    } AmodFrameset;

    /* Which vertices of a model an operation touches. Judged on the frame it is given. */
#define AMOD_VERTICES_ALL                                           0
#define AMOD_VERTICES_IN_BOX                                        1
#define AMOD_VERTICES_IN_SPHERE                                     2
#define AMOD_VERTICES_LISTED                                        3

    typedef struct AmodVertexFilter
    {
        uint32_t structSize;
        int kind;                   /* AMOD_VERTICES_* */

        float min[3];               /* IN_BOX, in the model's own coordinates */
        float max[3];

        float centre[3];            /* IN_SPHERE */
        float radius;

        const uint16_t* indices;    /* LISTED */
        int indexCount;
        
    } AmodVertexFilter;

    /* What an entity is playing */
    typedef struct AmodAnimState
    {
        uint32_t structSize;

        AmodAnimationId animation;
        const char* frameset;       /* "" when it is on nothing */

        int frameA;
        int frameB;
        float blend;                /* 0..1 from A to B */

        float speed;                /* what Anim_Play's rate was multiplied by */
        int backwards;
        int stopped;
        int finished;
        
    } AmodAnimState;

    /* A model the game is drawing, right now, with the device already set up for it. */
    typedef struct AmodModelDrawInfo
    {
        uint32_t structSize;

        AmodModelId model;
        int frameA;                 /* -1 when it is being drawn still */
        int frameB;
        float blend;                /* 0..1 from A to B */

        /* Filled when the world is drawing it, which is the only place the game knows where the thing is */
        uint32_t item;              /* one drawn thing; two entities sharing a model differ here */
        uint32_t texture;           /* what the game is drawing it with */
        float matrix[16];           /* model -> world, exactly as the game set it */
        float position[3];          /* the translation out of that matrix, for convenience */

    } AmodModelDrawInfo;

    typedef void(__cdecl* AmodModelDrawHandler)(const AmodModelDrawInfo* info, void* user);

    typedef struct AmodMmdApi
    {
        uint32_t structSize;

        /* --- what the game has in memory --------------------------------------------------- */

        int(__cdecl* GetCount)(AmodMod mod);
        AmodModelId(__cdecl* GetAt)(AmodMod mod, int index);
        AmodModelId(__cdecl* Find)(AmodMod mod, const char* name);
        int(__cdecl* Get)(AmodMod mod, AmodModelId model, AmodModel* out);
        int(__cdecl* IsValid)(AmodMod mod, AmodModelId model);

        /* --- getting hold of one ------------------------------------------------------------ */

        /* Through the game's own door: a model already loaded is shared rather than read again. */
        AmodModelId(__cdecl* Load)(AmodMod mod, const char* path);

        /* Gives back what Load took */
        int(__cdecl* Release)(AmodMod mod, AmodModelId model);

        /* --- the triangles ------------------------------------------------------------------- */

        /* Fills up to max and returns how many there are, whether they fitted or not */
        int(__cdecl* ReadVertices)(AmodMod mod, AmodModelId model, int frame, AmodVertex* out, int max);
        int(__cdecl* ReadIndices)(AmodMod mod, AmodModelId model, int frame, uint16_t* out, int max);

        /* Writes them back and rebuilds what is drawn */
        int(__cdecl* WriteVertices)(AmodMod mod, AmodModelId model, int frame,
            const AmodVertex* vertices, int count);
        int(__cdecl* WriteIndices)(AmodMod mod, AmodModelId model, int frame,
            const uint16_t* indices, int count);

        /* --- models of the mod's own ---------------------------------------------------------- */

        /* Builds one out of vertices */
        AmodModelId(__cdecl* Create)(AmodMod mod, const char* name, const AmodMeshData* mesh);

        /* Swaps the geometry of a model whole, counts and all. Snapshotted like a write. */
        int(__cdecl* Replace)(AmodMod mod, AmodModelId model, const AmodMeshData* mesh);

        /* Puts back what the model was before this mod first wrote to it. */
        int(__cdecl* Restore)(AmodMod mod, AmodModelId model);

        /* Writes the model out as a .mmd the game itself can load */
        int(__cdecl* Save)(AmodMod mod, AmodModelId model, const char* path);

        /* --- standing in for a file ------------------------------------------------------------ */

        /* Every request for `path` - a monster's $model, a bonus, a room, anything - is answered with this model instead */
        int(__cdecl* Override)(AmodMod mod, const char* path, AmodModelId model);
        int(__cdecl* ClearOverride)(AmodMod mod, const char* path);

        /* --- putting one in the world ---------------------------------------------------------- */

        /* The three models a room is drawn from, AMOD_ROOM_MODEL_* */
        AmodModelId(__cdecl* GetRoomModel)(AmodMod mod, int roomIndex, int slot);
        int(__cdecl* SetRoomModel)(AmodMod mod, int roomIndex, int slot, AmodModelId model);

        /* --- drawing ---------------------------------------------------------------------------- */

        /* With whatever transform and texture the device is already set to, so only from inside a render callback */
        int(__cdecl* Draw)(AmodMod mod, AmodModelId model);
        int(__cdecl* DrawFrames)(AmodMod mod, AmodModelId model, int from, int to, float blend);

        /* --- being asked -------------------------------------------------------------------------- */

        int(__cdecl* GetProperty)(AmodMod mod, AmodModelId model, const char* key, AmodValue* value);
        int(__cdecl* SetProperty)(AmodMod mod, AmodModelId model, const char* key, const AmodValue* value);
        int(__cdecl* HasProperty)(AmodMod mod, const char* key);
        int(__cdecl* PropertyCount)(AmodMod mod);
        const char* (__cdecl* PropertyNameAt)(AmodMod mod, int index);

        /* --- the frame's own draw list ------------------------------------------------------------ */

        /* Puts a model in front of the camera for this frame and this frame only - call it again next frame */
        int(__cdecl* Enqueue)(AmodMod mod, const AmodModelDraw* draw);
        int(__cdecl* QueueRoom)(AmodMod mod);

        /* A texture by name, resolved once and remembered. 0 when the game has nothing under it. */
        uint32_t(__cdecl* ResolveTexture)(AmodMod mod, const char* name);

        /* The camera the world is being drawn with, sixteen floats each */
        int(__cdecl* GetViewMatrix)(AmodMod mod, float* out);
        int(__cdecl* GetProjectionMatrix)(AmodMod mod, float* out);

        /* --- animations ---------------------------------------------------------------------------- */

        int(__cdecl* GetAnimationCount)(AmodMod mod);
        AmodAnimationId(__cdecl* GetAnimationAt)(AmodMod mod, int index);
        AmodAnimationId(__cdecl* FindAnimation)(AmodMod mod, const char* name);
        int(__cdecl* GetAnimation)(AmodMod mod, AmodAnimationId animation, AmodAnimation* out);
        int(__cdecl* GetFrameset)(AmodMod mod, AmodAnimationId animation, int index, AmodFrameset* out);

        AmodAnimationId(__cdecl* LoadAnimation)(AmodMod mod, const char* path);
        int(__cdecl* ReleaseAnimation)(AmodMod mod, AmodAnimationId animation);

        /* Every request for that .anm is answered with this animation instead. */
        int(__cdecl* OverrideAnimation)(AmodMod mod, const char* path, AmodAnimationId animation);
        int(__cdecl* ClearAnimationOverride)(AmodMod mod, const char* path);

        /* --- moving what a model is made of ---------------------------------------------------- */

        /* One vertex, without a buffer for the whole model */
        int(__cdecl* GetVertex)(AmodMod mod, AmodModelId model, int frame, int index, AmodVertex* out);

        /* Which vertices the filter picks. Fills up to max and returns how many there are. */
        int(__cdecl* SelectVertices)(AmodMod mod, AmodModelId model, int frame,
            const AmodVertexFilter* filter, uint16_t* out, int max);

        /* Puts the picked vertices through a 4x4 - positions through all of it, normals through the 3x3 corner */
        int(__cdecl* TransformVertices)(AmodMod mod, AmodModelId model, int frame,
            const AmodVertexFilter* filter, const float* matrix);

        /* --- colour without a texture ------------------------------------------------------------ */

        /* One colour per vertex, 0xAARRGGBB, as many as the model has */
        int(__cdecl* SetColours)(AmodMod mod, AmodModelId model, const uint32_t* colours, int count);
        int(__cdecl* ClearColours)(AmodMod mod, AmodModelId model);

        /* --- what an entity is playing ---------------------------------------------------------- */


        int(__cdecl* GetEntityAnim)(AmodMod mod, int roomIndex, int index, AmodAnimState* out);

        /* Starts a frameset of whatever animation that entity already has. */
        int(__cdecl* PlayEntityAnim)(AmodMod mod, int roomIndex, int index, const char* frameset);
        int(__cdecl* StopEntityAnim)(AmodMod mod, int roomIndex, int index);

        /* --- hanging one model off another ------------------------------------------------------ */

        /* Called every time that model is drawn, wherever the game draws it, with the world matrix already set for it */
        AmodToken(__cdecl* AddDrawHandler)(AmodMod mod, AmodModelId model,
            AmodModelDrawHandler handler, void* user);
        void(__cdecl* RemoveDrawHandler)(AmodMod mod, AmodToken token);

        /* Only meaningful inside such a handler */
        int(__cdecl* DrawWith)(AmodMod mod, AmodModelId model, uint32_t texture, const float* matrix);

        /* Fields added in later API versions go here, and nowhere else. */
    } AmodMmdApi;

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

        inline int                                                  ReadVertices(AmodModelId model, int frame,
            AmodVertex* out, int max)
        {
            return g_mmd != nullptr ? g_mmd->ReadVertices(g_mmdSelf, model, frame, out, max) : 0;
        }

        inline int                                                  ReadIndices(AmodModelId model, int frame,
            uint16_t* out, int max)
        {
            return g_mmd != nullptr ? g_mmd->ReadIndices(g_mmdSelf, model, frame, out, max) : 0;
        }

        inline bool                                                 WriteVertices(AmodModelId model, int frame,
            const AmodVertex* vertices, int count)
        {
            return g_mmd != nullptr &&
                g_mmd->WriteVertices(g_mmdSelf, model, frame, vertices, count) != 0;
        }

        inline bool                                                 WriteIndices(AmodModelId model, int frame,
            const uint16_t* indices, int count)
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

        inline AmodModelId                                          RoomModel(int roomIndex = -1,
            int slot = AMOD_ROOM_MODEL_GEOMETRY)
        {
            return g_mmd != nullptr ? g_mmd->GetRoomModel(g_mmdSelf, roomIndex, slot)
                : AMOD_INVALID_MODEL;
        }

        inline bool                                                 SetRoomModel(int roomIndex, int slot,
            AmodModelId model)
        {
            return g_mmd != nullptr && g_mmd->SetRoomModel(g_mmdSelf, roomIndex, slot, model) != 0;
        }

        inline bool                                                 Draw(AmodModelId model)
        {
            return g_mmd != nullptr && g_mmd->Draw(g_mmdSelf, model) != 0;
        }

        inline bool                                                 DrawFrames(AmodModelId model, int from, int to,
            float blend)
        {
            return g_mmd != nullptr && g_mmd->DrawFrames(g_mmdSelf, model, from, to, blend) != 0;
        }

        /* The mesh a mod holds in arrays of its own. Frames follow one another in both. */
        inline AmodMeshData                                         Mesh(const AmodVertex* vertices, int vertexCount,
            const uint16_t* indices, int indexCount,
            int frameCount = 1)
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

        inline bool                                                 PlayEntityAnim(int index, const char* frameset,
            int roomIndex = -1)
        {
            return g_mmd != nullptr && g_mmd->PlayEntityAnim != nullptr &&
                g_mmd->PlayEntityAnim(g_mmdSelf, roomIndex, index, frameset) != 0;
        }

        inline bool                                                 StopEntityAnim(int index, int roomIndex = -1)
        {
            return g_mmd != nullptr && g_mmd->StopEntityAnim != nullptr &&
                g_mmd->StopEntityAnim(g_mmdSelf, roomIndex, index) != 0;
        }

        inline AmodToken                                            OnDraw(AmodModelId model,
            AmodModelDrawHandler handler,
            void* user = nullptr)
        {
            return (g_mmd != nullptr && g_mmd->AddDrawHandler != nullptr)
                ? g_mmd->AddDrawHandler(g_mmdSelf, model, handler, user)
                : AMOD_INVALID_TOKEN;
        }

        inline void                                                 RemoveOnDraw(AmodToken token)
        {
            if (g_mmd != nullptr && g_mmd->RemoveDrawHandler != nullptr)
            {
                g_mmd->RemoveDrawHandler(g_mmdSelf, token);
            }
        }

        inline bool                                                 DrawWith(AmodModelId model, uint32_t texture,
            const Matrix& matrix)
        {
            return g_mmd != nullptr && g_mmd->DrawWith != nullptr &&
                g_mmd->DrawWith(g_mmdSelf, model, texture, matrix.m) != 0;
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
