#pragma once

#include <string>
#include <vector>

namespace Forge
{
    struct ModContext;

    struct StoreRecord
    {
        std::string kind;
        std::string key;
        std::vector<std::string> values;
    };

    class ModStore
    {
    public:
        static void                                                 Load(ModContext* owner, const std::string& id);
        static void                                                 Save(ModContext* owner);

        static void __cdecl                                         Tick();
        static void                                                 Drop(ModContext* owner);

        static void                                                 Put(ModContext* owner, const StoreRecord& record);
        static void                                                 Remove(ModContext* owner, const char* kind, const char* key);

        static void                                                 Protect(ModContext* owner, const char* kind, const char* key, bool protect);

        static std::vector<StoreRecord>                             Records(ModContext* owner, const char* kind);
    };
}
