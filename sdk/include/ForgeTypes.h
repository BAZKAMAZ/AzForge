#ifndef AZFORGE_FORGE_TYPES_H
#define AZFORGE_FORGE_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define AMOD_API_VERSION                                            98

// Offsets are fixed at build time
#ifdef __cplusplus
#define AMOD_LAST_FIELD(table, field, at)                           \
    static_assert(offsetof(table, field) == (at), #table ": fields are appended, never inserted - " #field " has moved")
#else
#define AMOD_LAST_FIELD(table, field, at)
#endif

#define AMOD_EXPORT                                                 __declspec(dllexport)

#define AMOD_OK                                                     0
#define AMOD_ERROR                                                  (-1)
#define AMOD_ERROR_API_VERSION                                      (-2)

// Read after a call returned false
#define AMOD_FAIL_NONE                                              0
#define AMOD_FAIL_NO_SUCH                                           1 /* no such name, index or id */
#define AMOD_FAIL_BAD_ARGUMENT                                      2 /* the call itself was malformed */
#define AMOD_FAIL_NOT_HERE                                          3 /* no level/room/player right now */
#define AMOD_FAIL_NOT_DECLARED                                      4 /* script never wrote that part */
#define AMOD_FAIL_REFUSED                                           5 /* the game was asked and said no */
#define AMOD_FAIL_NOT_OFFERED                                       6 /* this loader does not carry the call */

#define AMOD_GAME_BUILD_UNKNOWN                                     0
#define AMOD_GAME_BUILD_AZANGARA_10                                 1

#ifdef __cplusplus
extern "C" {
#endif

    typedef void* AmodMod;

    typedef uint32_t AmodToken;
#define AMOD_INVALID_TOKEN                                          0

    typedef enum AmodLogLevel
    {
        AMOD_LOG_INFO = 0,
        AMOD_LOG_WARNING = 1,
        AMOD_LOG_ERROR = 2
    } AmodLogLevel;

    typedef enum AmodValueKind
    {
        AMOD_VALUE_NONE = 0,
        AMOD_VALUE_BOOL = 1,
        AMOD_VALUE_INT = 2,
        AMOD_VALUE_FLOAT = 3, /* 1..4 floats, see count */
        AMOD_VALUE_STRING = 4
    } AmodValueKind;

    typedef struct AmodValue
    {
        AmodValueKind kind;
        uint32_t count; /* floats in use */
        int integer; /* BOOL / INT */
        float numbers[4]; /* FLOAT */
        const char* text; /* STRING; points into owner when read */
    } AmodValue;

    AMOD_LAST_FIELD(AmodValue, text, 0x1C);

    // A collision box
    typedef struct AmodBox
    {
        uint32_t structSize;

        float min[3];
        float max[3];


    } AmodBox;

    AMOD_LAST_FIELD(AmodBox, max, 0x10);

    typedef struct AmodFrame
    {
        uint32_t structSize;
        float delta; /* seconds since previous frame */
        double elapsed; /* seconds since first frame */
        uint64_t frameIndex; /* starts at 0 */
    } AmodFrame;

    AMOD_LAST_FIELD(AmodFrame, frameIndex, 0x10);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

extern "C++" {

    namespace Amod
    {
        inline AmodValue                                                Value(int number)
        {
            AmodValue value = {};
            value.kind = AMOD_VALUE_INT;
            value.integer = number;
            return value;
        }

        inline AmodValue                                                Value(bool boolean)
        {
            AmodValue value = {};
            value.kind = AMOD_VALUE_BOOL;
            value.integer = boolean ? 1 : 0;
            return value;
        }

        inline AmodValue                                                Value(const char* text)
        {
            AmodValue value = {};
            value.kind = AMOD_VALUE_STRING;
            value.text = text;
            return value;
        }

        inline AmodValue                                                Values(const float* numbers, uint32_t count)
        {
            AmodValue value = {};
            value.kind = AMOD_VALUE_FLOAT;
            value.count = count > 4u ? 4u : count;
            for (uint32_t at = 0; at < value.count; ++at)
            {
                value.numbers[at] = numbers[at];
            }
            return value;
        }

        inline AmodValue                                                Value(float a)
        {
            return Values(&a, 1);
        }

        inline AmodValue                                                Value(float a, float b)
        {
            const float numbers[2] = { a, b };
            return Values(numbers, 2);
        }

        inline AmodValue                                                Value(float a, float b, float c)
        {
            const float numbers[3] = { a, b, c };
            return Values(numbers, 3);
        }

        inline AmodValue                                                Value(float a, float b, float c, float d)
        {
            const float numbers[4] = { a, b, c, d };
            return Values(numbers, 4);
        }

        using Bounds = AmodBox;
        using Frame = AmodFrame;
    }

} /* extern "C++" */

#endif /* __cplusplus */

#endif /* AZFORGE_FORGE_TYPES_H */
