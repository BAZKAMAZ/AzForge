#include "pch.h"

#include "src/Game/Signature.h"

namespace Forge
{
    namespace
    {
        bool HexDigit(char c, uint8_t& value)
        {
            if (c >= '0' && c <= '9') { value = static_cast<uint8_t>(c - '0'); return true; }
            if (c >= 'a' && c <= 'f') { value = static_cast<uint8_t>(c - 'a' + 10); return true; }
            if (c >= 'A' && c <= 'F') { value = static_cast<uint8_t>(c - 'A' + 10); return true; }
            return false;
        }
    }

    bool Signature::Parse(const char* pattern, std::string& error)
    {
        error.clear();
        m_bytes.clear();
        m_mask.clear();

        if (pattern == nullptr)
        {
            error = "the pattern is empty";
            return false;
        }

        for (const char* cursor = pattern; *cursor != '\0';)
        {
            if (*cursor == ' ')
            {
                ++cursor;
                continue;
            }

            if (*cursor == '?')
            {
                ++cursor;
                if (*cursor == '?')
                {
                    ++cursor;
                }
                m_bytes.push_back(0);
                m_mask.push_back(0x00);
                continue;
            }

            uint8_t high = 0;
            uint8_t low = 0;
            if (!HexDigit(cursor[0], high) || !HexDigit(cursor[1], low))
            {
                error = "the pattern contains something that is neither a byte nor a wildcard";
                return false;
            }

            m_bytes.push_back(static_cast<uint8_t>((high << 4) | low));
            m_mask.push_back(0xFF);
            cursor += 2;
        }

        if (m_bytes.empty())
        {
            error = "the pattern is empty";
            return false;
        }

        if (m_mask.front() == 0x00)
        {
            error = "the pattern starts with a wildcard, which makes matches ambiguous";
            return false;
        }

        return true;
    }

    bool Signature::MatchesAt(const uint8_t* candidate) const
    {
        for (size_t i = 0; i < m_bytes.size(); ++i)
        {
            if ((candidate[i] & m_mask[i]) != m_bytes[i])
            {
                return false;
            }
        }
        return true;
    }

    size_t Signature::Length() const
    {
        return m_bytes.size();
    }

    size_t Signature::FindAll(const MemoryRange& range, std::vector<const uint8_t*>& hits,
                              size_t limit) const
    {
        hits.clear();

        if (range.begin == nullptr || range.size < m_bytes.size() || m_bytes.empty())
        {
            return 0;
        }

        const uint8_t first = m_bytes.front();
        const size_t last = range.size - m_bytes.size();

        for (size_t offset = 0; offset <= last; ++offset)
        {
            if (range.begin[offset] != first)
            {
                continue;
            }

            if (MatchesAt(range.begin + offset))
            {
                hits.push_back(range.begin + offset);
                if (hits.size() >= limit)
                {
                    break;
                }
            }
        }

        return hits.size();
    }
}
