#ifndef AZFORGE_BLOCK_EXPRESSION_API_H
#define AZFORGE_BLOCK_EXPRESSION_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque block node; owned by the mod until Free / Detach / unload. */
typedef void* AmodBlock;

typedef struct AmodBlockExpressionApi
{
    uint32_t structSize;

    AmodBlock (__cdecl* NewTree)(AmodMod mod, const char* name);
    AmodBlock (__cdecl* Parse)(AmodMod mod, const char* fragment);
    void      (__cdecl* Free)(AmodMod mod, AmodBlock root);

    /* Tree hung off a game object by handle. */
    AmodBlock (__cdecl* Attach)(AmodMod mod, void* object);
    AmodBlock (__cdecl* Find)(AmodMod mod, void* object);
    void      (__cdecl* Detach)(AmodMod mod, void* object);

    AmodBlock   (__cdecl* Child)(AmodMod mod, AmodBlock block, const char* name);
    int         (__cdecl* ChildCount)(AmodMod mod, AmodBlock block);
    AmodBlock   (__cdecl* ChildAt)(AmodMod mod, AmodBlock block, int index);
    const char* (__cdecl* Name)(AmodMod mod, AmodBlock block);

    int         (__cdecl* GetExpr)(AmodMod mod, AmodBlock block, const char* key, AmodValue* value);
    int         (__cdecl* SetExpr)(AmodMod mod, AmodBlock block, const char* key, const AmodValue* value);
    int         (__cdecl* HasExpr)(AmodMod mod, AmodBlock block, const char* key);
    int         (__cdecl* ExprCount)(AmodMod mod, AmodBlock block);
    const char* (__cdecl* ExprNameAt)(AmodMod mod, AmodBlock block, int index);
    void        (__cdecl* RemoveExpr)(AmodMod mod, AmodBlock block, const char* key);
} AmodBlockExpressionApi;

AMOD_LAST_FIELD(AmodBlockExpressionApi, RemoveExpr, 0x40);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

namespace Amod
{
    namespace Data
    {
        inline const AmodBlockExpressionApi* g_data = nullptr;
        inline AmodMod g_self = nullptr;

        inline void                                                 Attach(const AmodBlockExpressionApi* api, AmodMod self)
        {
            g_data = api;
            g_self = self;
        }

        inline bool                                                 IsAvailable()
        {
            return g_data != nullptr;
        }

        class Expression
        {
        public:
            Expression(AmodBlock block, const char* key)
                : m_block(block)
                , m_key(key)
            {
            }

            bool                                                    Exists() const
            {
                return g_data != nullptr && g_data->HasExpr(g_self, m_block, m_key) != 0;
            }

            bool                                                    Read(AmodValue& value) const
            {
                return g_data != nullptr && g_data->GetExpr(g_self, m_block, m_key, &value) != 0;
            }

            float                                                   Number(int index = 0, float fallback = 0.0f) const
            {
                AmodValue value = {};
                if (!Read(value))
                {
                    return fallback;
                }
                if (value.kind == AMOD_VALUE_INT || value.kind == AMOD_VALUE_BOOL)
                {
                    return index == 0 ? static_cast<float>(value.integer) : fallback;
                }
                if (value.kind != AMOD_VALUE_FLOAT || index < 0 ||
                    static_cast<uint32_t>(index) >= value.count)
                {
                    return fallback;
                }
                return value.numbers[index];
            }

            int                                                     Int(int fallback = 0) const
            {
                AmodValue value = {};
                if (!Read(value))
                {
                    return fallback;
                }
                if (value.kind == AMOD_VALUE_INT || value.kind == AMOD_VALUE_BOOL)
                {
                    return value.integer;
                }
                if (value.kind == AMOD_VALUE_FLOAT && value.count > 0)
                {
                    return static_cast<int>(value.numbers[0]);
                }
                return fallback;
            }

            bool                                                    Bool(bool fallback = false) const
            {
                return Int(fallback ? 1 : 0) != 0;
            }

            const char*                                             Text(const char* fallback = "") const
            {
                AmodValue value = {};
                if (!Read(value) || value.kind != AMOD_VALUE_STRING || value.text == nullptr)
                {
                    return fallback;
                }
                return value.text;
            }

            bool                                                    Set(const AmodValue& value) const
            {
                return g_data != nullptr && g_data->SetExpr(g_self, m_block, m_key, &value) != 0;
            }

            bool                                                    Set(int integer) const
            {
                AmodValue value = {};
                value.kind = AMOD_VALUE_INT;
                value.integer = integer;
                return Set(value);
            }

            bool                                                    Set(bool boolean) const
            {
                AmodValue value = {};
                value.kind = AMOD_VALUE_BOOL;
                value.integer = boolean ? 1 : 0;
                return Set(value);
            }

            bool                                                    Set(const char* text) const
            {
                AmodValue value = {};
                value.kind = AMOD_VALUE_STRING;
                value.text = text;
                return Set(value);
            }

            bool                                                    Set(float a) const                    { return SetNumbers(&a, 1); }
            bool                                                    Set(float a, float b) const           { const float n[2] = { a, b }; return SetNumbers(n, 2); }
            bool                                                    Set(float a, float b, float c) const  { const float n[3] = { a, b, c }; return SetNumbers(n, 3); }
            bool                                                    Set(float a, float b, float c, float d) const { const float n[4] = { a, b, c, d }; return SetNumbers(n, 4); }

        private:
            bool                                                    SetNumbers(const float* numbers, uint32_t count) const
            {
                AmodValue value = {};
                value.kind = AMOD_VALUE_FLOAT;
                value.count = count;
                for (uint32_t i = 0; i < count; ++i)
                {
                    value.numbers[i] = numbers[i];
                }
                return Set(value);
            }

            AmodBlock m_block;
            const char* m_key;
        };

        class Block
        {
        public:
            Block(AmodBlock block = nullptr)
                : m_block(block)
            {
            }

            bool                                                    IsValid() const   { return m_block != nullptr; }
            AmodBlock                                               Handle() const    { return m_block; }

            const char*                                             Name() const
            {
                return g_data != nullptr ? g_data->Name(g_self, m_block) : "";
            }

            Expression                                              operator[](const char* key) const
            {
                return Expression(m_block, key);
            }

            Block                                                   Child(const char* name) const
            {
                return Block(g_data != nullptr ? g_data->Child(g_self, m_block, name) : nullptr);
            }

            int                                                     ChildCount() const
            {
                return g_data != nullptr ? g_data->ChildCount(g_self, m_block) : 0;
            }

            Block                                                   ChildAt(int index) const
            {
                return Block(g_data != nullptr ? g_data->ChildAt(g_self, m_block, index) : nullptr);
            }

            int                                                     ExprCount() const
            {
                return g_data != nullptr ? g_data->ExprCount(g_self, m_block) : 0;
            }

            const char*                                             ExprNameAt(int index) const
            {
                return g_data != nullptr ? g_data->ExprNameAt(g_self, m_block, index) : "";
            }

            bool                                                    Has(const char* key) const
            {
                return g_data != nullptr && g_data->HasExpr(g_self, m_block, key) != 0;
            }

            void                                                    Remove(const char* key) const
            {
                if (g_data != nullptr)
                {
                    g_data->RemoveExpr(g_self, m_block, key);
                }
            }

            void                                                    Free() const
            {
                if (g_data != nullptr)
                {
                    g_data->Free(g_self, m_block);
                }
            }

        private:
            AmodBlock m_block;
        };

        inline Block                                                New(const char* name = "")
        {
            return Block(g_data != nullptr ? g_data->NewTree(g_self, name) : nullptr);
        }

        inline Block                                                Parse(const char* fragment)
        {
            return Block(g_data != nullptr ? g_data->Parse(g_self, fragment) : nullptr);
        }

        inline Block                                                On(void* object)
        {
            return Block(g_data != nullptr ? g_data->Attach(g_self, object) : nullptr);
        }

        inline Block                                                Find(void* object)
        {
            return Block(g_data != nullptr ? g_data->Find(g_self, object) : nullptr);
        }

        inline void                                                 Detach(void* object)
        {
            if (g_data != nullptr)
            {
                g_data->Detach(g_self, object);
            }
        }
    }
}

#endif /* __cplusplus */

#endif /* AZFORGE_BLOCK_EXPRESSION_API_H */
