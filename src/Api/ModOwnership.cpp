#include "pch.h"

#include "src/Api/ModOwnership.h"

namespace Forge
{
    uint32_t ModOwnership::Add(ReleaseRoutine release, void* payload)
    {
        Entry entry;
        entry.token = m_nextToken++;
        entry.release = release;
        entry.payload = payload;

        m_entries.push_back(entry);
        return entry.token;
    }

    bool ModOwnership::Remove(uint32_t token)
    {
        for (size_t i = m_entries.size(); i-- > 0;)
        {
            if (m_entries[i].token != token)
            {
                continue;
            }

            const Entry entry = m_entries[i];
            m_entries.erase(m_entries.begin() + static_cast<ptrdiff_t>(i));

            if (entry.release != nullptr)
            {
                entry.release(entry.payload);
            }
            return true;
        }

        return false;
    }

    void ModOwnership::ReleaseAll()
    {
        // Reverse order, and the entry is taken out of the list before it is released so a
        // release routine that reaches back in cannot see a half-dead registration.
        while (!m_entries.empty())
        {
            const Entry entry = m_entries.back();
            m_entries.pop_back();

            if (entry.release != nullptr)
            {
                entry.release(entry.payload);
            }
        }
    }

    size_t ModOwnership::Count() const
    {
        return m_entries.size();
    }
}
