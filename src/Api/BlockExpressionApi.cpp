#include "pch.h"

#include "src/Api/BlockExpressionApi.h"

#include "src/Api/ModContext.h"

#include <map>
#include <memory>
#include <set>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <vector>

namespace Forge
{
    namespace
    {
        // One key and the value it carries. The value's text points back into this node's own
        // storage, which is why an expression lives behind a unique_ptr: the pointer a mod
        // reads stays put when other expressions are added or removed around it.
        struct Expr
        {
            std::string key;
            std::string text;   // backing store for AMOD_VALUE_STRING
            AmodValue value = {};
        };

        struct Node
        {
            std::string name;
            std::vector<std::unique_ptr<Expr>> exprs;
            std::vector<std::unique_ptr<Node>> children;
        };

        // Everything one mod made. Trees are owned here; the live set is what a handle is
        // checked against before it is followed, so a stale or foreign block is refused
        // rather than dereferenced.
        struct ModStore
        {
            std::vector<std::unique_ptr<Node>> roots;
            std::map<void*, Node*> attached;
            std::set<const Node*> live;
        };

        std::map<ModContext*, ModStore> g_stores;

        AmodBlockExpressionApi g_table = {};

        ModContext* Resolve(AmodMod mod)
        {
            ModContext* context = static_cast<ModContext*>(mod);
            return (context != nullptr && context->magic == kModContextMagic) ? context : nullptr;
        }

        ModStore* StoreOf(AmodMod mod)
        {
            ModContext* const context = Resolve(mod);
            return context != nullptr ? &g_stores[context] : nullptr;
        }

        void RegisterSubtree(ModStore& store, Node* node)
        {
            store.live.insert(node);
            for (const std::unique_ptr<Node>& child : node->children)
            {
                RegisterSubtree(store, child.get());
            }
        }

        void UnregisterSubtree(ModStore& store, Node* node)
        {
            store.live.erase(node);
            for (const std::unique_ptr<Node>& child : node->children)
            {
                UnregisterSubtree(store, child.get());
            }
        }

        // Turns an AmodBlock back into a node of this mod's tree, or null when it is not one.
        Node* AsNode(ModStore* store, AmodBlock block)
        {
            if (store == nullptr || block == nullptr)
            {
                return nullptr;
            }
            Node* const node = static_cast<Node*>(block);
            return store->live.count(node) != 0 ? node : nullptr;
        }

        Node* NewRoot(ModStore& store, const char* name)
        {
            auto node = std::make_unique<Node>();
            node->name = name != nullptr ? name : "";
            Node* const raw = node.get();
            store.roots.push_back(std::move(node));
            store.live.insert(raw);
            return raw;
        }

        void FreeRoot(ModStore& store, Node* root)
        {
            for (auto it = store.roots.begin(); it != store.roots.end(); ++it)
            {
                if (it->get() != root)
                {
                    continue;
                }

                UnregisterSubtree(store, root);
                for (auto a = store.attached.begin(); a != store.attached.end();)
                {
                    a = a->second == root ? store.attached.erase(a) : ++a;
                }
                store.roots.erase(it);
                return;
            }
        }

        // --- the script syntax parser --------------------------------------------------
        //
        // The same shape the game reads - $key value, $name { ... }, // and /* */ comments,
        // quoted strings - but it builds this mod's own tree rather than a game object, so it
        // is free to enumerate everything and to keep keys the engine would not know.

        struct Parser
        {
            const char* p;
            const char* end;
        };

        bool AtEnd(Parser& s) { return s.p >= s.end; }

        void SkipTrivia(Parser& s)
        {
            while (!AtEnd(s))
            {
                const char c = *s.p;
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                {
                    ++s.p;
                }
                else if (c == '/' && s.p + 1 < s.end && s.p[1] == '/')
                {
                    while (!AtEnd(s) && *s.p != '\n')
                    {
                        ++s.p;
                    }
                }
                else if (c == '/' && s.p + 1 < s.end && s.p[1] == '*')
                {
                    s.p += 2;
                    while (s.p + 1 < s.end && !(s.p[0] == '*' && s.p[1] == '/'))
                    {
                        ++s.p;
                    }
                    s.p = s.p + 1 < s.end ? s.p + 2 : s.end;
                }
                else
                {
                    break;
                }
            }
        }

        // A single value token: a quoted string keeps its contents, a bare run stops at
        // whitespace or a structural character.
        bool ReadToken(Parser& s, std::string& token, bool& quoted)
        {
            SkipTrivia(s);
            if (AtEnd(s) || *s.p == '$' || *s.p == '{' || *s.p == '}')
            {
                return false;
            }

            token.clear();
            quoted = false;

            if (*s.p == '"')
            {
                quoted = true;
                ++s.p;
                while (!AtEnd(s) && *s.p != '"')
                {
                    token.push_back(*s.p++);
                }
                if (!AtEnd(s))
                {
                    ++s.p;
                }
                return true;
            }

            while (!AtEnd(s))
            {
                const char c = *s.p;
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                    c == '$' || c == '{' || c == '}' || c == '"')
                {
                    break;
                }
                if (c == '/' && s.p + 1 < s.end && (s.p[1] == '/' || s.p[1] == '*'))
                {
                    break;
                }
                token.push_back(*s.p++);
            }
            return !token.empty();
        }

        bool IsNumeric(const std::string& token, bool& isInteger)
        {
            if (token.empty())
            {
                return false;
            }
            char* endptr = nullptr;
            strtod(token.c_str(), &endptr);
            if (endptr != token.c_str() + token.size())
            {
                return false;
            }
            isInteger = token.find_first_of(".eE") == std::string::npos;
            return true;
        }

        // Classifies the value tokens of one expression into an AmodValue, the way the game's
        // readers would have taken them: a run of up to four numbers, a lone integer, or text.
        void Classify(const std::vector<std::string>& tokens, const std::vector<char>& quoted,
                      Expr& expr)
        {
            if (tokens.empty())
            {
                expr.value.kind = AMOD_VALUE_BOOL;
                expr.value.integer = 1;
                return;
            }

            bool allNumeric = true;
            bool allInteger = true;
            for (size_t i = 0; i < tokens.size(); ++i)
            {
                bool isInteger = false;
                if (quoted[i] || !IsNumeric(tokens[i], isInteger))
                {
                    allNumeric = false;
                    break;
                }
                allInteger = allInteger && isInteger;
            }

            if (allNumeric && tokens.size() == 1 && allInteger)
            {
                expr.value.kind = AMOD_VALUE_INT;
                expr.value.integer = static_cast<int>(strtol(tokens[0].c_str(), nullptr, 10));
                return;
            }

            if (allNumeric && tokens.size() >= 1 && tokens.size() <= 4)
            {
                expr.value.kind = AMOD_VALUE_FLOAT;
                expr.value.count = static_cast<uint32_t>(tokens.size());
                for (size_t i = 0; i < tokens.size(); ++i)
                {
                    expr.value.numbers[i] = static_cast<float>(strtod(tokens[i].c_str(), nullptr));
                }
                return;
            }

            // Anything else is kept as text: a single word, or several tokens joined back.
            expr.text = tokens[0];
            for (size_t i = 1; i < tokens.size(); ++i)
            {
                expr.text.push_back(' ');
                expr.text += tokens[i];
            }
            expr.value.kind = AMOD_VALUE_STRING;
            expr.value.text = expr.text.c_str();
        }

        Expr* FindExpr(Node* node, const char* key)
        {
            for (const std::unique_ptr<Expr>& expr : node->exprs)
            {
                if (expr->key == key)
                {
                    return expr.get();
                }
            }
            return nullptr;
        }

        Node* FindChild(Node* node, const char* name)
        {
            for (const std::unique_ptr<Node>& child : node->children)
            {
                if (child->name == name)
                {
                    return child.get();
                }
            }
            return nullptr;
        }

        void ParseBody(Parser& s, ModStore& store, Node* node, int depth)
        {
            while (true)
            {
                SkipTrivia(s);
                if (AtEnd(s) || *s.p == '}')
                {
                    return;
                }

                if (*s.p != '$')
                {
                    // Not a key where one is expected; skip it rather than loop forever.
                    ++s.p;
                    continue;
                }

                ++s.p; // '$'
                std::string key;
                while (!AtEnd(s))
                {
                    const char c = *s.p;
                    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                        c == '{' || c == '}' || c == '$' || c == '"')
                    {
                        break;
                    }
                    key.push_back(*s.p++);
                }
                if (key.empty())
                {
                    continue;
                }

                SkipTrivia(s);
                if (!AtEnd(s) && *s.p == '{')
                {
                    ++s.p; // '{'
                    Node* child = FindChild(node, key.c_str());
                    if (child == nullptr)
                    {
                        auto owned = std::make_unique<Node>();
                        owned->name = key;
                        child = owned.get();
                        node->children.push_back(std::move(owned));
                        store.live.insert(child);
                    }
                    if (depth < 64)
                    {
                        ParseBody(s, store, child, depth + 1);
                    }
                    SkipTrivia(s);
                    if (!AtEnd(s) && *s.p == '}')
                    {
                        ++s.p;
                    }
                    continue;
                }

                std::vector<std::string> tokens;
                std::vector<char> quoted;
                std::string token;
                bool isQuoted = false;
                while (ReadToken(s, token, isQuoted))
                {
                    tokens.push_back(token);
                    quoted.push_back(isQuoted ? 1 : 0);
                }

                auto expr = std::make_unique<Expr>();
                expr->key = key;
                Classify(tokens, quoted, *expr);
                node->exprs.push_back(std::move(expr));
            }
        }

        // --- the table -----------------------------------------------------------------

        AmodBlock __cdecl ApiNewTree(AmodMod mod, const char* name)
        {
            ModStore* const store = StoreOf(mod);
            return store != nullptr ? NewRoot(*store, name) : nullptr;
        }

        AmodBlock __cdecl ApiParse(AmodMod mod, const char* fragment)
        {
            ModStore* const store = StoreOf(mod);
            if (store == nullptr || fragment == nullptr)
            {
                return nullptr;
            }

            Node* const root = NewRoot(*store, "");
            Parser s{ fragment, fragment + strlen(fragment) };
            ParseBody(s, *store, root, 0);
            return root;
        }

        void __cdecl ApiFree(AmodMod mod, AmodBlock root)
        {
            ModStore* const store = StoreOf(mod);
            Node* const node = AsNode(store, root);
            if (node != nullptr)
            {
                FreeRoot(*store, node);
            }
        }

        AmodBlock __cdecl ApiAttach(AmodMod mod, void* object)
        {
            ModStore* const store = StoreOf(mod);
            if (store == nullptr || object == nullptr)
            {
                return nullptr;
            }

            const auto found = store->attached.find(object);
            if (found != store->attached.end())
            {
                return found->second;
            }

            Node* const root = NewRoot(*store, "");
            store->attached[object] = root;
            return root;
        }

        AmodBlock __cdecl ApiFind(AmodMod mod, void* object)
        {
            ModStore* const store = StoreOf(mod);
            if (store == nullptr)
            {
                return nullptr;
            }
            const auto found = store->attached.find(object);
            return found != store->attached.end() ? found->second : nullptr;
        }

        void __cdecl ApiDetach(AmodMod mod, void* object)
        {
            ModStore* const store = StoreOf(mod);
            if (store == nullptr)
            {
                return;
            }
            const auto found = store->attached.find(object);
            if (found != store->attached.end())
            {
                FreeRoot(*store, found->second); // also drops the attached entry
            }
        }

        AmodBlock __cdecl ApiChild(AmodMod mod, AmodBlock block, const char* name)
        {
            ModStore* const store = StoreOf(mod);
            Node* const node = AsNode(store, block);
            if (node == nullptr || name == nullptr)
            {
                return nullptr;
            }

            Node* child = FindChild(node, name);
            if (child == nullptr)
            {
                auto owned = std::make_unique<Node>();
                owned->name = name;
                child = owned.get();
                node->children.push_back(std::move(owned));
                store->live.insert(child);
            }
            return child;
        }

        int __cdecl ApiChildCount(AmodMod mod, AmodBlock block)
        {
            ModStore* const store = StoreOf(mod);
            Node* const node = AsNode(store, block);
            return node != nullptr ? static_cast<int>(node->children.size()) : 0;
        }

        AmodBlock __cdecl ApiChildAt(AmodMod mod, AmodBlock block, int index)
        {
            ModStore* const store = StoreOf(mod);
            Node* const node = AsNode(store, block);
            if (node == nullptr || index < 0 || index >= static_cast<int>(node->children.size()))
            {
                return nullptr;
            }
            return node->children[static_cast<size_t>(index)].get();
        }

        const char* __cdecl ApiName(AmodMod mod, AmodBlock block)
        {
            ModStore* const store = StoreOf(mod);
            Node* const node = AsNode(store, block);
            return node != nullptr ? node->name.c_str() : "";
        }

        int __cdecl ApiGetExpr(AmodMod mod, AmodBlock block, const char* key, AmodValue* value)
        {
            ModStore* const store = StoreOf(mod);
            Node* const node = AsNode(store, block);
            if (node == nullptr || key == nullptr || value == nullptr)
            {
                return 0;
            }

            const Expr* const expr = FindExpr(node, key);
            if (expr == nullptr)
            {
                return 0;
            }

            *value = expr->value;
            if (value->kind == AMOD_VALUE_STRING)
            {
                value->text = expr->text.c_str();
            }
            return 1;
        }

        int __cdecl ApiSetExpr(AmodMod mod, AmodBlock block, const char* key, const AmodValue* value)
        {
            ModStore* const store = StoreOf(mod);
            Node* const node = AsNode(store, block);
            if (node == nullptr || key == nullptr || value == nullptr)
            {
                return 0;
            }

            Expr* expr = FindExpr(node, key);
            if (expr == nullptr)
            {
                auto owned = std::make_unique<Expr>();
                owned->key = key;
                expr = owned.get();
                node->exprs.push_back(std::move(owned));
            }

            expr->value = *value;
            if (value->kind == AMOD_VALUE_STRING)
            {
                expr->text = value->text != nullptr ? value->text : "";
                expr->value.text = expr->text.c_str();
            }
            else
            {
                expr->text.clear();
            }
            return 1;
        }

        int __cdecl ApiHasExpr(AmodMod mod, AmodBlock block, const char* key)
        {
            ModStore* const store = StoreOf(mod);
            Node* const node = AsNode(store, block);
            return (node != nullptr && key != nullptr && FindExpr(node, key) != nullptr) ? 1 : 0;
        }

        int __cdecl ApiExprCount(AmodMod mod, AmodBlock block)
        {
            ModStore* const store = StoreOf(mod);
            Node* const node = AsNode(store, block);
            return node != nullptr ? static_cast<int>(node->exprs.size()) : 0;
        }

        const char* __cdecl ApiExprNameAt(AmodMod mod, AmodBlock block, int index)
        {
            ModStore* const store = StoreOf(mod);
            Node* const node = AsNode(store, block);
            if (node == nullptr || index < 0 || index >= static_cast<int>(node->exprs.size()))
            {
                return "";
            }
            return node->exprs[static_cast<size_t>(index)]->key.c_str();
        }

        void __cdecl ApiRemoveExpr(AmodMod mod, AmodBlock block, const char* key)
        {
            ModStore* const store = StoreOf(mod);
            Node* const node = AsNode(store, block);
            if (node == nullptr || key == nullptr)
            {
                return;
            }
            for (auto it = node->exprs.begin(); it != node->exprs.end(); ++it)
            {
                if ((*it)->key == key)
                {
                    node->exprs.erase(it);
                    return;
                }
            }
        }
    }

    const AmodBlockExpressionApi* BlockExpressionApi::Table()
    {
        if (g_table.structSize == 0)
        {
            g_table.structSize = sizeof(AmodBlockExpressionApi);
            g_table.NewTree = &ApiNewTree;
            g_table.Parse = &ApiParse;
            g_table.Free = &ApiFree;
            g_table.Attach = &ApiAttach;
            g_table.Find = &ApiFind;
            g_table.Detach = &ApiDetach;
            g_table.Child = &ApiChild;
            g_table.ChildCount = &ApiChildCount;
            g_table.ChildAt = &ApiChildAt;
            g_table.Name = &ApiName;
            g_table.GetExpr = &ApiGetExpr;
            g_table.SetExpr = &ApiSetExpr;
            g_table.HasExpr = &ApiHasExpr;
            g_table.ExprCount = &ApiExprCount;
            g_table.ExprNameAt = &ApiExprNameAt;
            g_table.RemoveExpr = &ApiRemoveExpr;
        }
        return &g_table;
    }

    void BlockExpressionApi::ForgetMod(ModContext* owner)
    {
        g_stores.erase(owner);
    }
}
