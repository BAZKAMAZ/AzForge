/*
 * BlockExpressionApi.h - a mod's own blocks and expressions.
 *
 * The game's scripts are blocks ($name { ... }) of expressions ($key value ...). This gives a
 * mod the same shape for its own data: a tree it authors in that syntax, reads and writes by
 * key, and either keeps to itself as configuration or state, or hangs off any game object as
 * fields the engine never sees.
 *
 * It is tied to nothing in the game. The values are the same AmodValue the menu interface
 * uses, but the tree is the mod's - the engine does not parse these keys or draw them; only
 * the mod that wrote them reads them back.
 *
 * Reached through AmodApi::blockExpression, null on a loader older than API version 8.
 */

#ifndef AZFORGE_BLOCK_EXPRESSION_API_H
#define AZFORGE_BLOCK_EXPRESSION_API_H

#include "ForgeTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A block: a node in a mod-owned tree, holding expressions and child blocks. Opaque, and
 * owned by the mod - valid until it is freed, detached, or the mod is unloaded. */
typedef void* AmodBlock;

typedef struct AmodBlockExpressionApi
{
    uint32_t structSize;

    /* Standalone trees the mod owns. Parse reads a fragment in the game's script syntax into
     * a tree; NewTree starts an empty one. Free releases a root and its subtree, though
     * everything a mod made is freed when it unloads regardless. */
    AmodBlock (__cdecl* NewTree)(AmodMod mod, const char* name);
    AmodBlock (__cdecl* Parse)(AmodMod mod, const char* fragment);
    void      (__cdecl* Free)(AmodMod mod, AmodBlock root);

    /* A tree hung off a game object, keyed by its handle - a menu element, a room object,
     * anything. Attach get-or-creates, Find answers null when there is none, Detach frees
     * it. The mod owns the lifetime: a handle the game reused is the mod's to detach. */
    AmodBlock (__cdecl* Attach)(AmodMod mod, void* object);
    AmodBlock (__cdecl* Find)(AmodMod mod, void* object);
    void      (__cdecl* Detach)(AmodMod mod, void* object);

    /* Child blocks. Child get-or-creates one by name; the rest walk what is there. */
    AmodBlock   (__cdecl* Child)(AmodMod mod, AmodBlock block, const char* name);
    int         (__cdecl* ChildCount)(AmodMod mod, AmodBlock block);
    AmodBlock   (__cdecl* ChildAt)(AmodMod mod, AmodBlock block, int index);
    const char* (__cdecl* Name)(AmodMod mod, AmodBlock block);

    /* Expressions on a block. Set creates or replaces one, Get and Has query, Remove drops
     * one. A value read points into the block and stays valid until that key is set again. */
    int         (__cdecl* GetExpr)(AmodMod mod, AmodBlock block, const char* key, AmodValue* value);
    int         (__cdecl* SetExpr)(AmodMod mod, AmodBlock block, const char* key, const AmodValue* value);
    int         (__cdecl* HasExpr)(AmodMod mod, AmodBlock block, const char* key);
    int         (__cdecl* ExprCount)(AmodMod mod, AmodBlock block);
    const char* (__cdecl* ExprNameAt)(AmodMod mod, AmodBlock block, int index);
    void        (__cdecl* RemoveExpr)(AmodMod mod, AmodBlock block, const char* key);

    /* Fields added in later API versions go here, and nowhere else. */
} AmodBlockExpressionApi;

#ifdef __cplusplus
} /* extern "C" */
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

        /* One expression of one block. Holds nothing, meant to be used on the spot. */
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

            /* The child block of this name, made if it is not there yet. */
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

        /* The tree hung off a game object, made if it is not there yet. */
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
