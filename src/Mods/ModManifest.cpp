#include "pch.h"

#include "src/Mods/ModManifest.h"

#include "src/Core/ForgeVersion.h"
#include "src/Json/JsonValue.h"

#include <stdio.h>

namespace Forge
{
    namespace
    {
        bool ReadRequiredString(const JsonValue& root, const char* key, std::string& out,
                                std::string& error)
        {
            const JsonValue* member = root.Member(key);
            if (member == nullptr)
            {
                error = std::string("'") + key + "' is missing";
                return false;
            }

            if (member->Type() != JsonType::String)
            {
                error = std::string("'") + key + "' must be a string";
                return false;
            }

            if (member->AsString().empty())
            {
                error = std::string("'") + key + "' must not be empty";
                return false;
            }

            out = member->AsString();
            return true;
        }

        bool ReadOptionalString(const JsonValue& root, const char* key, std::string& out,
                                std::string& error)
        {
            const JsonValue* member = root.Member(key);
            if (member == nullptr)
            {
                return true;
            }

            if (member->Type() != JsonType::String)
            {
                error = std::string("'") + key + "' must be a string";
                return false;
            }

            out = member->AsString();
            return true;
        }

        bool ReadOptionalStringArray(const JsonValue& root, const char* key,
                                     std::vector<std::string>& out, std::string& error)
        {
            const JsonValue* member = root.Member(key);
            if (member == nullptr)
            {
                return true;
            }

            if (member->Type() != JsonType::Array)
            {
                error = std::string("'") + key + "' must be an array";
                return false;
            }

            for (const JsonValue& element : member->AsArray())
            {
                if (element.Type() != JsonType::String || element.AsString().empty())
                {
                    error = std::string("'") + key + "' must contain non-empty strings";
                    return false;
                }
                out.push_back(element.AsString());
            }

            return true;
        }

        bool IsValidIdentifier(const std::string& id)
        {
            for (const char c : id)
            {
                const bool allowed = (c >= 'a' && c <= 'z') ||
                                     (c >= '0' && c <= '9') ||
                                     c == '.' || c == '_' || c == '-';
                if (!allowed)
                {
                    return false;
                }
            }
            return true;
        }
    }

    bool ParseModManifest(const std::string& document, ModManifest& out, std::string& error)
    {
        JsonValue root;
        if (!JsonParse(document, root, error))
        {
            return false;
        }

        if (root.Type() != JsonType::Object)
        {
            error = "the manifest must be a JSON object";
            return false;
        }

        if (!ReadRequiredString(root, "id", out.id, error) ||
            !ReadRequiredString(root, "name", out.name, error) ||
            !ReadRequiredString(root, "version", out.version, error))
        {
            return false;
        }

        if (!IsValidIdentifier(out.id))
        {
            error = "'id' accepts only lowercase letters, digits, '.', '_' and '-'";
            return false;
        }

        const JsonValue* apiVersion = root.Member("api_version");
        if (apiVersion == nullptr)
        {
            error = "'api_version' is missing";
            return false;
        }

        if (apiVersion->Type() != JsonType::Number)
        {
            error = "'api_version' must be a number";
            return false;
        }

        const double rawApiVersion = apiVersion->AsNumber();
        out.apiVersion = static_cast<int>(rawApiVersion);
        if (static_cast<double>(out.apiVersion) != rawApiVersion)
        {
            error = "'api_version' must be a whole number";
            return false;
        }

        if (out.apiVersion > kApiVersionCurrent)
        {
            char buffer[160];
            snprintf(buffer, sizeof(buffer),
                     "the mod requires API version %d, this loader provides %d",
                     out.apiVersion, kApiVersionCurrent);
            error = buffer;
            return false;
        }

        if (out.apiVersion < kApiVersionOldestSupported)
        {
            char buffer[160];
            snprintf(buffer, sizeof(buffer),
                     "API version %d is no longer supported, the oldest accepted is %d",
                     out.apiVersion, kApiVersionOldestSupported);
            error = buffer;
            return false;
        }

        if (!ReadOptionalString(root, "author", out.author, error) ||
            !ReadOptionalString(root, "description", out.description, error) ||
            !ReadOptionalString(root, "game_version", out.gameVersion, error) ||
            !ReadOptionalString(root, "entry", out.entry, error) ||
            !ReadOptionalStringArray(root, "load_after", out.loadAfter, error) ||
            !ReadOptionalStringArray(root, "conflicts", out.conflicts, error) ||
            !ReadOptionalStringArray(root, "requires", out.requiredSubsystems, error) ||
            !ReadOptionalStringArray(root, "capabilities", out.capabilities, error))
        {
            return false;
        }

        for (const std::string& other : out.loadAfter)
        {
            if (other == out.id)
            {
                error = "'load_after' refers to the mod itself";
                return false;
            }
        }

        for (const std::string& other : out.conflicts)
        {
            if (other == out.id)
            {
                error = "'conflicts' refers to the mod itself";
                return false;
            }
        }

        return true;
    }

    bool ModManifest::Declares(const std::string& capability) const
    {
        for (const std::string& declared : capabilities)
        {
            if (declared == capability)
            {
                return true;
            }
        }
        return false;
    }
}
