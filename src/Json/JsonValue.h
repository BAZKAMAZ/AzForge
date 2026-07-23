#pragma once

#include <map>
#include <string>
#include <vector>

namespace Forge
{
    enum class JsonType
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    // Minimal JSON reader covering exactly what mod.json needs. Duplicate object keys,
    // trailing commas and comments are rejected rather than tolerated, so a malformed
    // manifest is reported instead of silently losing fields.
    class JsonValue
    {
    public:
        JsonType                                                    Type() const;
        bool                                                        IsNull() const;

        bool                                                        AsBoolean() const;
        double                                                      AsNumber() const;
        const std::string&                                          AsString() const;
        const std::vector<JsonValue>&                               AsArray() const;

        const JsonValue*                                            Member(const std::string& name) const;

        void                                                        SetNull();
        void                                                        SetBoolean(bool value);
        void                                                        SetNumber(double value);
        void                                                        SetString(std::string value);
        std::vector<JsonValue>&                                     MutableArray();
        std::map<std::string, JsonValue>&                           MutableObject();

    private:
        JsonType m_type = JsonType::Null;
        bool m_boolean = false;
        double m_number = 0.0;
        std::string m_string;
        std::vector<JsonValue> m_array;
        std::map<std::string, JsonValue> m_object;
    };

    // Parses a complete UTF-8 document. On failure returns false and fills error with a
    // message that names the byte offset.
    bool                                                            JsonParse(const std::string& text, JsonValue& out, std::string& error);
}
