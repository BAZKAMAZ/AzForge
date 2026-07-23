#include "pch.h"

#include "src/Json/JsonValue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Forge
{
    namespace
    {
        const std::string g_emptyString;
        const std::vector<JsonValue> g_emptyArray;

        class Parser
        {
        public:
            Parser(const std::string& text, std::string& error)
                : m_text(text)
                , m_error(error)
            {
            }

            bool ParseDocument(JsonValue& out)
            {
                SkipWhitespace();
                if (!ParseValue(out, 0))
                {
                    return false;
                }

                SkipWhitespace();
                if (m_position != m_text.size())
                {
                    return Fail("trailing data after the top-level value");
                }

                return true;
            }

        private:
            static constexpr int kMaxDepth = 32;

            bool Fail(const char* reason)
            {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "%s at byte %zu", reason, m_position);
                m_error = buffer;
                return false;
            }

            bool AtEnd() const
            {
                return m_position >= m_text.size();
            }

            char Peek() const
            {
                return m_text[m_position];
            }

            void SkipWhitespace()
            {
                while (!AtEnd())
                {
                    const char c = Peek();
                    if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                    {
                        return;
                    }
                    ++m_position;
                }
            }

            bool Expect(char expected)
            {
                if (AtEnd() || Peek() != expected)
                {
                    char reason[64];
                    snprintf(reason, sizeof(reason), "expected '%c'", expected);
                    return Fail(reason);
                }
                ++m_position;
                return true;
            }

            bool ParseValue(JsonValue& out, int depth)
            {
                if (depth > kMaxDepth)
                {
                    return Fail("nesting is too deep");
                }

                if (AtEnd())
                {
                    return Fail("unexpected end of document");
                }

                switch (Peek())
                {
                case '{': return ParseObject(out, depth);
                case '[': return ParseArray(out, depth);
                case '"': return ParseStringValue(out);
                case 't': return ParseLiteral("true", [&out]() { out.SetBoolean(true); });
                case 'f': return ParseLiteral("false", [&out]() { out.SetBoolean(false); });
                case 'n': return ParseLiteral("null", [&out]() { out.SetNull(); });
                default:  return ParseNumber(out);
                }
            }

            template <typename Apply>
            bool ParseLiteral(const char* literal, Apply apply)
            {
                const size_t length = strlen(literal);
                if (m_text.compare(m_position, length, literal) != 0)
                {
                    return Fail("unknown literal");
                }
                m_position += length;
                apply();
                return true;
            }

            bool ParseNumber(JsonValue& out)
            {
                const size_t start = m_position;

                if (!AtEnd() && Peek() == '-')
                {
                    ++m_position;
                }

                while (!AtEnd())
                {
                    const char c = Peek();
                    const bool numeric = (c >= '0' && c <= '9') || c == '.' ||
                                         c == 'e' || c == 'E' || c == '+' || c == '-';
                    if (!numeric)
                    {
                        break;
                    }
                    ++m_position;
                }

                if (m_position == start)
                {
                    return Fail("expected a value");
                }

                const std::string token = m_text.substr(start, m_position - start);
                char* stop = nullptr;
                const double value = strtod(token.c_str(), &stop);
                if (stop == nullptr || *stop != '\0')
                {
                    return Fail("malformed number");
                }

                out.SetNumber(value);
                return true;
            }

            bool ParseStringValue(JsonValue& out)
            {
                std::string text;
                if (!ParseString(text))
                {
                    return false;
                }
                out.SetString(std::move(text));
                return true;
            }

            void AppendUtf8(unsigned int codePoint, std::string& out)
            {
                if (codePoint < 0x80)
                {
                    out.push_back(static_cast<char>(codePoint));
                }
                else if (codePoint < 0x800)
                {
                    out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
                    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                }
                else if (codePoint < 0x10000)
                {
                    out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                }
            }

            bool ParseHex4(unsigned int& out)
            {
                if (m_position + 4 > m_text.size())
                {
                    return Fail("truncated \\u escape");
                }

                out = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = m_text[m_position + i];
                    unsigned int digit = 0;
                    if (c >= '0' && c <= '9')      digit = static_cast<unsigned int>(c - '0');
                    else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned int>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned int>(c - 'A' + 10);
                    else return Fail("malformed \\u escape");

                    out = (out << 4) | digit;
                }

                m_position += 4;
                return true;
            }

            bool ParseString(std::string& out)
            {
                if (!Expect('"'))
                {
                    return false;
                }

                out.clear();

                while (true)
                {
                    if (AtEnd())
                    {
                        return Fail("unterminated string");
                    }

                    const char c = m_text[m_position++];
                    if (c == '"')
                    {
                        return true;
                    }

                    if (c != '\\')
                    {
                        out.push_back(c);
                        continue;
                    }

                    if (AtEnd())
                    {
                        return Fail("unterminated escape");
                    }

                    const char escape = m_text[m_position++];
                    switch (escape)
                    {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u':
                    {
                        unsigned int codePoint = 0;
                        if (!ParseHex4(codePoint))
                        {
                            return false;
                        }

                        if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
                        {
                            if (m_position + 2 > m_text.size() ||
                                m_text[m_position] != '\\' || m_text[m_position + 1] != 'u')
                            {
                                return Fail("high surrogate without a low surrogate");
                            }

                            m_position += 2;

                            unsigned int low = 0;
                            if (!ParseHex4(low))
                            {
                                return false;
                            }

                            if (low < 0xDC00 || low > 0xDFFF)
                            {
                                return Fail("invalid low surrogate");
                            }

                            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                        }
                        else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
                        {
                            return Fail("unpaired low surrogate");
                        }

                        AppendUtf8(codePoint, out);
                        break;
                    }
                    default:
                        return Fail("unknown escape sequence");
                    }
                }
            }

            bool ParseArray(JsonValue& out, int depth)
            {
                if (!Expect('['))
                {
                    return false;
                }

                std::vector<JsonValue>& items = out.MutableArray();

                SkipWhitespace();
                if (!AtEnd() && Peek() == ']')
                {
                    ++m_position;
                    return true;
                }

                while (true)
                {
                    SkipWhitespace();

                    JsonValue element;
                    if (!ParseValue(element, depth + 1))
                    {
                        return false;
                    }
                    items.push_back(std::move(element));

                    SkipWhitespace();
                    if (AtEnd())
                    {
                        return Fail("unterminated array");
                    }

                    const char c = m_text[m_position++];
                    if (c == ']')
                    {
                        return true;
                    }
                    if (c != ',')
                    {
                        --m_position;
                        return Fail("expected ',' or ']'");
                    }
                }
            }

            bool ParseObject(JsonValue& out, int depth)
            {
                if (!Expect('{'))
                {
                    return false;
                }

                std::map<std::string, JsonValue>& members = out.MutableObject();

                SkipWhitespace();
                if (!AtEnd() && Peek() == '}')
                {
                    ++m_position;
                    return true;
                }

                while (true)
                {
                    SkipWhitespace();

                    std::string key;
                    if (!ParseString(key))
                    {
                        return false;
                    }

                    if (members.find(key) != members.end())
                    {
                        return Fail("duplicate object key");
                    }

                    SkipWhitespace();
                    if (!Expect(':'))
                    {
                        return false;
                    }

                    SkipWhitespace();

                    JsonValue value;
                    if (!ParseValue(value, depth + 1))
                    {
                        return false;
                    }
                    members.emplace(std::move(key), std::move(value));

                    SkipWhitespace();
                    if (AtEnd())
                    {
                        return Fail("unterminated object");
                    }

                    const char c = m_text[m_position++];
                    if (c == '}')
                    {
                        return true;
                    }
                    if (c != ',')
                    {
                        --m_position;
                        return Fail("expected ',' or '}'");
                    }
                }
            }

            const std::string& m_text;
            std::string& m_error;
            size_t m_position = 0;
        };
    }

    JsonType JsonValue::Type() const
    {
        return m_type;
    }

    bool JsonValue::IsNull() const
    {
        return m_type == JsonType::Null;
    }

    bool JsonValue::AsBoolean() const
    {
        return m_boolean;
    }

    double JsonValue::AsNumber() const
    {
        return m_number;
    }

    const std::string& JsonValue::AsString() const
    {
        return m_type == JsonType::String ? m_string : g_emptyString;
    }

    const std::vector<JsonValue>& JsonValue::AsArray() const
    {
        return m_type == JsonType::Array ? m_array : g_emptyArray;
    }

    const JsonValue* JsonValue::Member(const std::string& name) const
    {
        if (m_type != JsonType::Object)
        {
            return nullptr;
        }

        const auto found = m_object.find(name);
        return found == m_object.end() ? nullptr : &found->second;
    }

    void JsonValue::SetNull()
    {
        m_type = JsonType::Null;
    }

    void JsonValue::SetBoolean(bool value)
    {
        m_type = JsonType::Boolean;
        m_boolean = value;
    }

    void JsonValue::SetNumber(double value)
    {
        m_type = JsonType::Number;
        m_number = value;
    }

    void JsonValue::SetString(std::string value)
    {
        m_type = JsonType::String;
        m_string = std::move(value);
    }

    std::vector<JsonValue>& JsonValue::MutableArray()
    {
        m_type = JsonType::Array;
        return m_array;
    }

    std::map<std::string, JsonValue>& JsonValue::MutableObject()
    {
        m_type = JsonType::Object;
        return m_object;
    }

    bool JsonParse(const std::string& text, JsonValue& out, std::string& error)
    {
        error.clear();
        Parser parser(text, error);
        return parser.ParseDocument(out);
    }
}
