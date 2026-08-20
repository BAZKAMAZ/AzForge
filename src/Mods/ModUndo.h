#pragma once

#include <vector>

namespace Forge
{
    class UndoGuard
    {
    public:
        explicit UndoGuard(bool& flag) : m_flag(flag) { m_flag = true; }
        ~UndoGuard() { m_flag = false; }

        UndoGuard(const UndoGuard&) = delete;
        UndoGuard& operator=(const UndoGuard&) = delete;

    private:
        bool& m_flag;
    };

    template <typename T, typename Owned>
    std::vector<T> TakeOwned(std::vector<T>& list, Owned owned)
    {
        std::vector<T> mine;

        for (auto it = list.begin(); it != list.end();)
        {
            if (!owned(*it))
            {
                ++it;
                continue;
            }

            mine.push_back(*it);
            it = list.erase(it);
        }

        return mine;
    }
}
