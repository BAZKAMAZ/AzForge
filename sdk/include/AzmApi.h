#ifndef AZFORGE_AZM_API_H
#define AZFORGE_AZM_API_H

#include "ForgeTypes.h"
#include "AzmFile.h"
#include "MmdApi.h"

/* Models with parts, materials and colour in the vertex - what .mmd cannot carry. A .azm is read
   once and becomes one engine mesh per part, with the file's materials as index ranges and the
   file's colours painted on; from there it is drawn like anything else, only in one batch.

   The whole of a frame goes through Queue and one Flush: a city of two hundred houses is one
   sorted draw, not two hundred. Draw is Queue and Flush together, for the one-off case. */

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t AmodAzmId;
#define AMOD_INVALID_AZM                                            0

typedef struct AmodAzmModel
{
    uint32_t structSize;

    const char* name; /* what the file called itself */
    const char* path; /* what it was read by */

    int partCount;
    int materialCount;
    int pointCount;
    int boxCount;

    int vertexCount;
    int triangleCount;
    int meshCount; /* engine meshes, more than partCount when a part had to be cut */

    float boundsMin[3];
    float boundsMax[3];

    float unitsPerMetre;
    int forwardAxis; /* AZM_AXIS_* */
    int upAxis;

} AmodAzmModel;

AMOD_LAST_FIELD(AmodAzmModel, upAxis, 0x48);

typedef struct AmodAzmPart
{
    uint32_t structSize;

    const char* name;
    int parent; /* -1 for a root */
    float pivot[3];
    uint32_t flags; /* AZM_PART_* */

    /* Engine mesh part. */
    AmodModelId model; /* engine mesh */
    int blend; /* AMOD_DRAW_BLEND_* */
    int hidden; /* hidden state */
} AmodAzmPart;

AMOD_LAST_FIELD(AmodAzmPart, hidden, 0x24);

typedef struct AmodAzmPoint
{
    uint32_t structSize;
    const char* name;
    float position[3];
    float direction[3];
    int part; /* attached part, -1 for model */

} AmodAzmPoint;

AMOD_LAST_FIELD(AmodAzmPoint, part, 0x20);

typedef struct AmodAzmBox
{
    uint32_t structSize;
    const char* name;
    float min[3];
    float max[3];

} AmodAzmBox;

AMOD_LAST_FIELD(AmodAzmBox, max, 0x14);

typedef struct AmodAzmMaterial
{
    uint32_t structSize;
    const char* name;
    const char* texture;
    uint32_t tint; /* source tint, 0xAARRGGBB */
    uint32_t flags; /* AZM_MATERIAL_* */
    int blend; /* AMOD_DRAW_BLEND_* */
    float alpha;

} AmodAzmMaterial;

AMOD_LAST_FIELD(AmodAzmMaterial, alpha, 0x18);

typedef struct AmodAzmApi
{
    uint32_t structSize;

    /* Getting models. */
    AmodAzmId(__cdecl* Load)(AmodMod mod, const char* path);
    int(__cdecl* Release)(AmodMod mod, AmodAzmId model);
    int(__cdecl* IsValid)(AmodMod mod, AmodAzmId model);
    AmodAzmId(__cdecl* Open)(AmodMod mod, const char* name, const void* bytes, int length);

    /* Reading model data. */
    int(__cdecl* Get)(AmodMod mod, AmodAzmId model, AmodAzmModel* out);
    int(__cdecl* GetPart)(AmodMod mod, AmodAzmId model, int index, AmodAzmPart* out);
    int(__cdecl* FindPart)(AmodMod mod, AmodAzmId model, const char* name);
    int(__cdecl* GetPoint)(AmodMod mod, AmodAzmId model, int index, AmodAzmPoint* out);
    int(__cdecl* FindPoint)(AmodMod mod, AmodAzmId model, const char* name);
    int(__cdecl* GetBox)(AmodMod mod, AmodAzmId model, int index, AmodAzmBox* out);
    int(__cdecl* GetMaterial)(AmodMod mod, AmodAzmId model, int index, AmodAzmMaterial* out);
    int(__cdecl* FindMaterial)(AmodMod mod, AmodAzmId model, const char* name);

    /* Model pose. */
    int(__cdecl* SetPartTurn)(AmodMod mod, AmodAzmId model, int part, const float axis[3], float angle);
    int(__cdecl* SetPartMatrix)(AmodMod mod, AmodAzmId model, int part, const float* matrix);
    int(__cdecl* SetPartHidden)(AmodMod mod, AmodAzmId model, int part, int hidden);
    int(__cdecl* ClearPose)(AmodMod mod, AmodAzmId model);

    /* Paints a material's vertices. */
    int(__cdecl* Paint)(AmodMod mod, AmodAzmId model, int material, uint32_t colour);

    /* Drawing. */
    int(__cdecl* Queue)(AmodMod mod, AmodAzmId model, const float* matrix);
    int(__cdecl* Flush)(AmodMod mod);
    int(__cdecl* Draw)(AmodMod mod, AmodAzmId model, const float* matrix);
    int(__cdecl* Queued)(AmodMod mod);

    /* Draw tint. */
    int(__cdecl* SetTint)(AmodMod mod, AmodAzmId model, uint32_t colour);
    int(__cdecl* SetPartTint)(AmodMod mod, AmodAzmId model, int part, uint32_t colour);
    int(__cdecl* ClearTints)(AmodMod mod, AmodAzmId model);

} AmodAzmApi;

AMOD_LAST_FIELD(AmodAzmApi, ClearTints, 0x60);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Azm
    {
        inline const AmodAzmApi* g_azm = nullptr;
        inline AmodMod g_azmSelf = nullptr;

        inline void                                                 Attach(const AmodAzmApi* api, AmodMod self)
        {
            g_azm = api;
            g_azmSelf = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_azm != nullptr;
        }

        inline AmodAzmId                                            Load(const char* path)
        {
            return g_azm != nullptr ? g_azm->Load(g_azmSelf, path) : AMOD_INVALID_AZM;
        }

        inline AmodAzmId                                            Open(const char* name, const void* bytes, int length)
        {
            return g_azm != nullptr ? g_azm->Open(g_azmSelf, name, bytes, length) : AMOD_INVALID_AZM;
        }

        inline bool                                                 Release(AmodAzmId model)
        {
            return g_azm != nullptr && g_azm->Release(g_azmSelf, model) != 0;
        }

        inline bool                                                 IsValid(AmodAzmId model)
        {
            return g_azm != nullptr && g_azm->IsValid(g_azmSelf, model) != 0;
        }

        inline bool                                                 Get(AmodAzmId model, AmodAzmModel& out)
        {
            out.structSize = sizeof(out);
            return g_azm != nullptr && g_azm->Get(g_azmSelf, model, &out) != 0;
        }

        inline bool                                                 GetPart(AmodAzmId model, int index, AmodAzmPart& out)
        {
            out.structSize = sizeof(out);
            return g_azm != nullptr && g_azm->GetPart(g_azmSelf, model, index, &out) != 0;
        }

        inline int                                                  FindPart(AmodAzmId model, const char* name)
        {
            return g_azm != nullptr ? g_azm->FindPart(g_azmSelf, model, name) : -1;
        }

        inline bool                                                 GetPoint(AmodAzmId model, int index, AmodAzmPoint& out)
        {
            out.structSize = sizeof(out);
            return g_azm != nullptr && g_azm->GetPoint(g_azmSelf, model, index, &out) != 0;
        }

        inline int                                                  FindPoint(AmodAzmId model, const char* name)
        {
            return g_azm != nullptr ? g_azm->FindPoint(g_azmSelf, model, name) : -1;
        }

        inline bool                                                 GetBox(AmodAzmId model, int index, AmodAzmBox& out)
        {
            out.structSize = sizeof(out);
            return g_azm != nullptr && g_azm->GetBox(g_azmSelf, model, index, &out) != 0;
        }

        inline bool                                                 GetMaterial(AmodAzmId model, int index, AmodAzmMaterial& out)
        {
            out.structSize = sizeof(out);
            return g_azm != nullptr && g_azm->GetMaterial(g_azmSelf, model, index, &out) != 0;
        }

        inline int                                                  FindMaterial(AmodAzmId model, const char* name)
        {
            return g_azm != nullptr ? g_azm->FindMaterial(g_azmSelf, model, name) : -1;
        }

        inline bool                                                 Turn(AmodAzmId model, int part, float x, float y, float z, float angle)
        {
            const float axis[3] = { x, y, z };
            return g_azm != nullptr && g_azm->SetPartTurn(g_azmSelf, model, part, axis, angle) != 0;
        }

        inline bool                                                 SetPartMatrix(AmodAzmId model, int part, const float* matrix)
        {
            return g_azm != nullptr && g_azm->SetPartMatrix(g_azmSelf, model, part, matrix) != 0;
        }

        inline bool                                                 SetPartHidden(AmodAzmId model, int part, bool hidden)
        {
            return g_azm != nullptr && g_azm->SetPartHidden(g_azmSelf, model, part, hidden ? 1 : 0) != 0;
        }

        inline bool                                                 ClearPose(AmodAzmId model)
        {
            return g_azm != nullptr && g_azm->ClearPose(g_azmSelf, model) != 0;
        }

        inline bool                                                 Paint(AmodAzmId model, int material, uint32_t colour)
        {
            return g_azm != nullptr && g_azm->Paint(g_azmSelf, model, material, colour) != 0;
        }

        inline bool                                                 Queue(AmodAzmId model, const float* matrix)
        {
            return g_azm != nullptr && g_azm->Queue(g_azmSelf, model, matrix) != 0;
        }

        inline int                                                  Flush()
        {
            return g_azm != nullptr ? g_azm->Flush(g_azmSelf) : 0;
        }

        inline int                                                  Draw(AmodAzmId model, const float* matrix)
        {
            return g_azm != nullptr ? g_azm->Draw(g_azmSelf, model, matrix) : 0;
        }

        inline int                                                  Queued()
        {
            return g_azm != nullptr ? g_azm->Queued(g_azmSelf) : 0;
        }

        inline bool                                                 SetTint(AmodAzmId model, uint32_t colour)
        {
            return g_azm != nullptr && g_azm->structSize >= sizeof(AmodAzmApi) &&
                   g_azm->SetTint(g_azmSelf, model, colour) != 0;
        }

        inline bool                                                 SetPartTint(AmodAzmId model, int part, uint32_t colour)
        {
            return g_azm != nullptr && g_azm->structSize >= sizeof(AmodAzmApi) &&
                   g_azm->SetPartTint(g_azmSelf, model, part, colour) != 0;
        }

        inline bool                                                 ClearTints(AmodAzmId model)
        {
            return g_azm != nullptr && g_azm->structSize >= sizeof(AmodAzmApi) &&
                   g_azm->ClearTints(g_azmSelf, model) != 0;
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_AZM_API_H */
