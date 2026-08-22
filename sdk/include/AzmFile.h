#ifndef AZFORGE_AZM_FILE_H
#define AZFORGE_AZM_FILE_H

#include "ForgeTypes.h"

/* .azm model format. Little endian, chunked, extensible. */
#ifdef __cplusplus
extern "C" {
#endif

#define AZM_MAGIC 0x314D5A41 /* 'AZM1' */
#define AZM_VERSION 1

    /* Chunk ids. */
#define AZM_CHUNK_HEAD 0x44414548 /* 'HEAD' */
#define AZM_CHUNK_STRS 0x53525453 /* 'STRS' */
#define AZM_CHUNK_VERT 0x54524556 /* 'VERT' */
#define AZM_CHUNK_INDX 0x58444E49 /* 'INDX' */
#define AZM_CHUNK_MATL 0x4C54414D /* 'MATL' */
#define AZM_CHUNK_PART 0x54524150 /* 'PART' */
#define AZM_CHUNK_SUBM 0x4D425553 /* 'SUBM' */
#define AZM_CHUNK_COLL 0x4C4C4F43 /* 'COLL' */
#define AZM_CHUNK_POIN 0x4E494F50 /* 'POIN' */
#define AZM_CHUNK_MORF 0x46524F4D /* 'MORF', reserved */

/* Model axes. */
#define AZM_AXIS_X_PLUS 0
#define AZM_AXIS_X_MINUS 1
#define AZM_AXIS_Y_PLUS 2
#define AZM_AXIS_Y_MINUS 3
#define AZM_AXIS_Z_PLUS 4
#define AZM_AXIS_Z_MINUS 5

/* Material blend modes. */
#define AZM_BLEND_NONE 0
#define AZM_BLEND_BLEND 1
#define AZM_BLEND_ADD 2
#define AZM_BLEND_MUL 3

/* AzmMaterial flags. */
#define AZM_MATERIAL_TWO_SIDED 0x01
#define AZM_MATERIAL_GLOWS 0x02 /* full brightness */
#define AZM_MATERIAL_NO_DEPTH_WRITE 0x04
#define AZM_MATERIAL_PAINTABLE 0x08 /* vertex colour is editable */

/* AzmPart flags. */
#define AZM_PART_HIDDEN 0x01
#define AZM_PART_COLLISION 0x02
#define AZM_PART_NO_SHADOW 0x04

    typedef struct AzmHeader
    {
        uint32_t magic; /* AZM_MAGIC */
        uint32_t version; /* AZM_VERSION */
        uint32_t chunkCount;
        uint32_t flags;

    } AzmHeader;

    AMOD_LAST_FIELD(AzmHeader, flags, 0x0C);

    typedef struct AzmChunk
    {
        uint32_t id; /* AZM_CHUNK_* */
        uint32_t size; /* payload bytes */

    } AzmChunk;

    AMOD_LAST_FIELD(AzmChunk, size, 0x04);

    /* HEAD. Model units, axes, bounds and name. */
    typedef struct AzmHead
    {
        float unitsPerMetre; /* 1.0 = metres */
        uint32_t forwardAxis; /* AZM_AXIS_* */
        uint32_t upAxis;
        float boundsMin[3];
        float boundsMax[3];
        uint32_t nameOffset; /* into STRS */

    } AzmHead;

    AMOD_LAST_FIELD(AzmHead, nameOffset, 0x24);

    /* Table header. */
    typedef struct AzmTable
    {
        uint32_t count;
        uint32_t stride; /* bytes per row */

    } AzmTable;

    AMOD_LAST_FIELD(AzmTable, stride, 0x04);

    /* VERT. */
    typedef struct AzmVertex
    {
        float position[3];
        float normal[3];
        float uv[2];
        uint32_t colour; /* 0xAARRGGBB */

    } AzmVertex;

    AMOD_LAST_FIELD(AzmVertex, colour, 0x20);

    /* MATL. */
    typedef struct AzmMaterial
    {
        uint32_t nameOffset;
        uint32_t textureOffset; /* path in STRS */
        uint32_t tint; /* 0xAARRGGBB */
        uint32_t blend; /* AZM_BLEND_* */
        uint32_t flags; /* AZM_MATERIAL_* */
        float alpha; /* 0..1 */
        uint32_t reserved;

    } AzmMaterial;

    AMOD_LAST_FIELD(AzmMaterial, reserved, 0x18);

    /* PART. */
    typedef struct AzmPart
    {
        uint32_t nameOffset;
        int32_t parent; /* PART index, -1 for root */
        float pivot[3];
        uint32_t flags; /* AZM_PART_* */
        uint32_t reserved;

    } AzmPart;

    AMOD_LAST_FIELD(AzmPart, reserved, 0x18);

    /* SUBM. */
    typedef struct AzmSubmesh
    {
        uint32_t part;
        uint32_t material;
        uint32_t firstIndex;
        uint32_t indexCount;

    } AzmSubmesh;

    AMOD_LAST_FIELD(AzmSubmesh, indexCount, 0x0C);

    /* COLL. */
    typedef struct AzmCollisionBox
    {
        uint32_t nameOffset;
        float min[3];
        float max[3];

    } AzmCollisionBox;

    AMOD_LAST_FIELD(AzmCollisionBox, max, 0x10);

    /* POIN. */
    typedef struct AzmPoint
    {
        uint32_t nameOffset;
        float position[3];
        float direction[3];
        int32_t part; /* attached part, -1 for model */

    } AzmPoint;

    AMOD_LAST_FIELD(AzmPoint, part, 0x1C);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AZFORGE_AZM_FILE_H */