#pragma once

#include <string>
#include <vector>

namespace Forge
{
    class Seal
    {
    public:
        static bool                                                 IsAvailable();

        static std::string                                          Tag(const std::string& id, const std::string& kind, const std::string& key, 
                                                                        const std::vector<std::string>& values);

        static std::string                                          Hide(const std::string& id, const std::string& kind, const std::string& key, int index, 
                                                                        const std::string& value);

        static bool                                                 Show(const std::string& id, const std::string& kind, const std::string& key, int index, 
                                                                        const std::string& hidden, std::string& out);
    };
}
