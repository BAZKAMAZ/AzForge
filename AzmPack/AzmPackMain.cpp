#include "src/Json/JsonValue.h"
#include "AzmFile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <math.h>
#include <sstream>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitError = 1;
    constexpr int kExitUsage = 2;

    constexpr int kEngineVertexLimit = 0xFFFF;

    void Print(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    Vec3 Normalised(const Vec3& vector)
    {
        const float length = sqrtf(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
        return length < 1.0e-12f ? Vec3{ 0.0f, 1.0f, 0.0f }
                                 : Vec3{ vector.x / length, vector.y / length, vector.z / length };
    }

    uint32_t PackColour(float red, float green, float blue, float alpha)
    {
        const auto channel = [](float value)
        {
            const int at = static_cast<int>(value * 255.0f + 0.5f);
            return static_cast<uint32_t>(at < 0 ? 0 : at > 255 ? 255 : at);
        };

        return (channel(alpha) << 24) | (channel(red) << 16) | (channel(green) << 8) | channel(blue);
    }

    struct Material
    {
        std::string name;
        std::string texture;
        float tint[3] = { 1.0f, 1.0f, 1.0f };
        float alpha = 1.0f;
        uint32_t blend = AZM_BLEND_NONE;
        uint32_t flags = 0;
    };

    struct Corner
    {
        int position = -1;
        int texcoord = -1;
        int normal = -1;
    };

    struct Face
    {
        Corner corner[3];
        int material = -1;
    };

    struct Group
    {
        std::string name;
        std::vector<Face> faces;
    };

    struct Source
    {
        std::vector<Vec3> positions;
        std::vector<Vec3> colours;
        std::vector<char> painted;      // whether the position above carried its own colour
        std::vector<float> texcoords;   // two floats each
        std::vector<Vec3> normals;
        std::vector<Group> groups;
        std::vector<std::string> libraries;

        std::vector<Material> materials;
        std::map<std::string, int> materialIndex;
    };

    struct Options
    {
        std::filesystem::path source;
        std::filesystem::path out;
        std::filesystem::path sidecar;
        std::filesystem::path mtl;
        std::string name;

        float units = 1.0f;
        uint32_t forward = AZM_AXIS_Z_PLUS;
        uint32_t up = AZM_AXIS_Y_PLUS;
        Vec3 light = { -0.35f, -1.0f, -0.5f };
        float ambient = 0.4f;
        bool shade = true;
        bool keepV = false;
    };

    int MaterialSlot(Source& source, const std::string& name)
    {
        const auto known = source.materialIndex.find(name);

        if (known != source.materialIndex.end())
        {
            return known->second;
        }

        Material material;
        material.name = name.empty() ? "default" : name;

        source.materialIndex[name] = static_cast<int>(source.materials.size());
        source.materials.push_back(material);
        return static_cast<int>(source.materials.size()) - 1;
    }

    // Only what the format carries: a texture, a colour, an alpha and whether it glows.
    void ParseMtl(const std::filesystem::path& path, Source& source)
    {
        std::ifstream file(path);

        if (!file.is_open())
        {
            Print("azmpack: no material library at %s\n", path.string().c_str());
            return;
        }

        int current = -1;
        std::string line;

        while (std::getline(file, line))
        {
            std::istringstream words(line);
            std::string key;

            if (!(words >> key) || key.empty() || key[0] == '#')
            {
                continue;
            }

            std::transform(key.begin(), key.end(), key.begin(), [](char at) { return static_cast<char>(tolower(at)); });

            if (key == "newmtl")
            {
                std::string name;
                std::getline(words >> std::ws, name);
                current = MaterialSlot(source, name);
                continue;
            }

            if (current < 0)
            {
                continue;
            }

            Material& material = source.materials[static_cast<size_t>(current)];

            if (key == "kd")
            {
                words >> material.tint[0] >> material.tint[1] >> material.tint[2];
            }
            else if (key == "d")
            {
                words >> material.alpha;
            }
            else if (key == "tr")
            {
                float value = 0.0f;
                words >> value;
                material.alpha = 1.0f - value;
            }
            else if ((key == "map_kd" || key == "map_ka") && material.texture.empty())
            {
                std::string rest;
                std::getline(words >> std::ws, rest);

                // An exporter can put options before the file name; the path is the last word.
                const size_t last = rest.find_last_of(" \t");
                material.texture = last == std::string::npos ? rest : rest.substr(last + 1);
                std::replace(material.texture.begin(), material.texture.end(), '\\', '/');
            }
            else if (key == "ke")
            {
                float glow[3] = {};
                words >> glow[0] >> glow[1] >> glow[2];

                if (glow[0] > 0.01f || glow[1] > 0.01f || glow[2] > 0.01f)
                {
                    material.flags |= AZM_MATERIAL_GLOWS;
                }
            }
        }

        for (Material& material : source.materials)
        {
            material.blend = material.alpha < 0.999f ? AZM_BLEND_BLEND : material.blend;
        }
    }

    bool ParseObj(const std::filesystem::path& path, Source& source, std::string& error)
    {
        std::ifstream file(path);

        if (!file.is_open())
        {
            error = "cannot open " + path.string();
            return false;
        }

        const auto resolve = [](const std::string& text, size_t total)
        {
            const int value = atoi(text.c_str());
            return value > 0 ? value - 1 : static_cast<int>(total) + value;
        };

        source.groups.push_back(Group{ "model", {} });
        int material = -1;
        bool named = false;
        std::string line;

        while (std::getline(file, line))
        {
            std::istringstream words(line);
            std::string key;

            if (!(words >> key) || key.empty() || key[0] == '#')
            {
                continue;
            }

            if (key == "v")
            {
                Vec3 position;
                Vec3 colour;
                words >> position.x >> position.y >> position.z;

                const bool tinted = static_cast<bool>(words >> colour.x >> colour.y >> colour.z);
                source.positions.push_back(position);
                source.colours.push_back(tinted ? colour : Vec3{ 1.0f, 1.0f, 1.0f });
                source.painted.push_back(tinted ? 1 : 0);
            }
            else if (key == "vt")
            {
                float u = 0.0f;
                float v = 0.0f;
                words >> u >> v;
                source.texcoords.push_back(u);
                source.texcoords.push_back(v);
            }
            else if (key == "vn")
            {
                Vec3 normal;
                words >> normal.x >> normal.y >> normal.z;
                source.normals.push_back(normal);
            }
            else if (key == "o" || key == "g")
            {
                std::string name;
                std::getline(words >> std::ws, name);

                if (name.empty())
                {
                    name = "part" + std::to_string(source.groups.size());
                }

                // The first name replaces the placeholder rather than opening a second group.
                if (!named && source.groups.back().faces.empty())
                {
                    source.groups.back().name = name;
                }
                else
                {
                    source.groups.push_back(Group{ name, {} });
                }
                named = true;
            }
            else if (key == "usemtl")
            {
                std::string name;
                std::getline(words >> std::ws, name);
                material = MaterialSlot(source, name);
            }
            else if (key == "mtllib")
            {
                std::string name;
                std::getline(words >> std::ws, name);
                source.libraries.push_back(name);
            }
            else if (key == "f")
            {
                std::vector<Corner> corners;
                std::string word;

                while (words >> word)
                {
                    std::string pieces[3];
                    size_t at = 0;

                    for (const char letter : word)
                    {
                        if (letter == '/')
                        {
                            at = at < 2 ? at + 1 : at;
                        }
                        else
                        {
                            pieces[at] += letter;
                        }
                    }

                    Corner corner;
                    corner.position = pieces[0].empty() ? -1 : resolve(pieces[0], source.positions.size());
                    corner.texcoord = pieces[1].empty() ? -1 : resolve(pieces[1], source.texcoords.size() / 2);
                    corner.normal = pieces[2].empty() ? -1 : resolve(pieces[2], source.normals.size());
                    corners.push_back(corner);
                }

                // Fanned from the first corner: an OBJ face is planar and convex by definition.
                for (size_t at = 1; at + 1 < corners.size(); ++at)
                {
                    Face face;
                    face.corner[0] = corners[0];
                    face.corner[1] = corners[at];
                    face.corner[2] = corners[at + 1];
                    face.material = material;
                    source.groups.back().faces.push_back(face);
                }
            }
        }

        source.groups.erase(std::remove_if(source.groups.begin(), source.groups.end(),
            [](const Group& group) { return group.faces.empty(); }), source.groups.end());

        return true;
    }

    struct Built
    {
        std::vector<char> strings;
        std::map<std::string, uint32_t> offsets;

        std::vector<AzmVertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<AzmMaterial> materials;
        std::vector<AzmPart> parts;
        std::vector<AzmSubmesh> submeshes;
        std::vector<AzmCollisionBox> boxes;
        std::vector<AzmPoint> points;

        uint32_t String(const std::string& text)
        {
            const auto known = offsets.find(text);

            if (known != offsets.end())
            {
                return known->second;
            }

            const uint32_t offset = static_cast<uint32_t>(strings.size());
            strings.insert(strings.end(), text.begin(), text.end());
            strings.push_back('\0');
            offsets[text] = offset;
            return offset;
        }
    };

    struct VertexKey
    {
        float position[3];
        float normal[3];
        float uv[2];
        uint32_t colour;

        bool operator==(const VertexKey& other) const
        {
            return memcmp(this, &other, sizeof(VertexKey)) == 0;
        }
    };

    struct VertexHash
    {
        size_t operator()(const VertexKey& key) const
        {
            const uint8_t* const bytes = reinterpret_cast<const uint8_t*>(&key);
            size_t hash = 2166136261u;

            for (size_t at = 0; at < sizeof(VertexKey); ++at)
            {
                hash = (hash ^ bytes[at]) * 16777619u;
            }
            return hash;
        }
    };

    Vec3 FaceNormal(const Source& source, const Face& face)
    {
        const Vec3& a = source.positions[static_cast<size_t>(face.corner[0].position)];
        const Vec3& b = source.positions[static_cast<size_t>(face.corner[1].position)];
        const Vec3& c = source.positions[static_cast<size_t>(face.corner[2].position)];

        const Vec3 first = { b.x - a.x, b.y - a.y, b.z - a.z };
        const Vec3 second = { c.x - a.x, c.y - a.y, c.z - a.z };

        return Normalised(Vec3{
            first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x });
    }

    // What an OBJ without vn needs: every face's normal added up at the positions it touches.
    std::vector<Vec3> SmoothNormals(const Source& source)
    {
        std::vector<Vec3> sums(source.positions.size());

        for (const Group& group : source.groups)
        {
            for (const Face& face : group.faces)
            {
                const Vec3 normal = FaceNormal(source, face);

                for (const Corner& corner : face.corner)
                {
                    Vec3& total = sums[static_cast<size_t>(corner.position)];
                    total.x += normal.x;
                    total.y += normal.y;
                    total.z += normal.z;
                }
            }
        }

        for (Vec3& total : sums)
        {
            total = Normalised(total);
        }
        return sums;
    }

    void BoundsOf(const Source& source, const Group& group, Vec3& least, Vec3& most)
    {
        least = { FLT_MAX, FLT_MAX, FLT_MAX };
        most = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

        for (const Face& face : group.faces)
        {
            for (const Corner& corner : face.corner)
            {
                const Vec3& point = source.positions[static_cast<size_t>(corner.position)];

                least.x = point.x < least.x ? point.x : least.x;
                least.y = point.y < least.y ? point.y : least.y;
                least.z = point.z < least.z ? point.z : least.z;
                most.x = point.x > most.x ? point.x : most.x;
                most.y = point.y > most.y ? point.y : most.y;
                most.z = point.z > most.z ? point.z : most.z;
            }
        }
    }

    Vec3 AverageNormal(const Source& source, const Group& group)
    {
        Vec3 total;

        for (const Face& face : group.faces)
        {
            const Vec3 normal = FaceNormal(source, face);
            total.x += normal.x;
            total.y += normal.y;
            total.z += normal.z;
        }
        return Normalised(total);
    }

    uint32_t AddVertex(Built& built, std::unordered_map<VertexKey, uint32_t, VertexHash>& seen, const Source& source, const std::vector<Vec3>& fallback, 
        const Corner& corner, const Material& material, const Options& options)
    {
        const Vec3& position = source.positions[static_cast<size_t>(corner.position)];

        const Vec3 normal = (corner.normal >= 0 && !source.normals.empty())
            ? source.normals[static_cast<size_t>(corner.normal)]
            : (!fallback.empty() ? fallback[static_cast<size_t>(corner.position)] : Vec3{ 0.0f, 1.0f, 0.0f });

        float uv[2] = { 0.0f, 0.0f };

        if (corner.texcoord >= 0 && !source.texcoords.empty())
        {
            uv[0] = source.texcoords[static_cast<size_t>(corner.texcoord) * 2];
            uv[1] = source.texcoords[static_cast<size_t>(corner.texcoord) * 2 + 1];

            // OBJ counts v up from the bottom, a texture counts down from the top.
            uv[1] = options.keepV ? uv[1] : 1.0f - uv[1];
        }

        Vec3 tint = { 1.0f, 1.0f, 1.0f };

        if (source.painted[static_cast<size_t>(corner.position)] != 0)
        {
            tint = source.colours[static_cast<size_t>(corner.position)];
        }
        else if ((material.flags & AZM_MATERIAL_PAINTABLE) == 0)
        {
            tint = { material.tint[0], material.tint[1], material.tint[2] };
        }

        if (options.shade && (material.flags & AZM_MATERIAL_GLOWS) == 0)
        {
            const float facing = -(normal.x * options.light.x + normal.y * options.light.y + normal.z * options.light.z);
            const float lit = facing > 0.0f ? facing : 0.0f;
            const float shade = options.ambient + (1.0f - options.ambient) * lit;

            tint.x *= shade;
            tint.y *= shade;
            tint.z *= shade;
        }

        VertexKey key = {};
        key.position[0] = position.x;
        key.position[1] = position.y;
        key.position[2] = position.z;
        key.normal[0] = normal.x;
        key.normal[1] = normal.y;
        key.normal[2] = normal.z;
        key.uv[0] = uv[0];
        key.uv[1] = uv[1];
        key.colour = PackColour(tint.x, tint.y, tint.z, material.alpha);

        const auto known = seen.find(key);

        if (known != seen.end())
        {
            return known->second;
        }

        AzmVertex vertex = {};
        memcpy(vertex.position, key.position, sizeof(vertex.position));
        memcpy(vertex.normal, key.normal, sizeof(vertex.normal));
        memcpy(vertex.uv, key.uv, sizeof(vertex.uv));
        vertex.colour = key.colour;

        const uint32_t index = static_cast<uint32_t>(built.vertices.size());
        seen[key] = index;
        built.vertices.push_back(vertex);
        return index;
    }

    const Forge::JsonValue* MemberOf(const Forge::JsonValue& value, const char* name)
    {
        return value.Type() == Forge::JsonType::Object ? value.Member(name) : nullptr;
    }

    bool ReadSidecar(const std::filesystem::path& path, Forge::JsonValue& out, std::string& error)
    {
        std::ifstream file(path);

        if (!file.is_open())
        {
            error = "cannot open " + path.string();
            return false;
        }

        std::ostringstream text;
        text << file.rdbuf();
        return Forge::JsonParse(text.str(), out, error);
    }

    void ApplyMaterialFlags(Source& source, const Forge::JsonValue& sidecar, const char* key, uint32_t flag)
    {
        const Forge::JsonValue* const list = MemberOf(sidecar, key);

        if (list == nullptr || list->Type() != Forge::JsonType::Array)
        {
            return;
        }

        for (const Forge::JsonValue& entry : list->AsArray())
        {
            const auto known = source.materialIndex.find(entry.AsString());

            if (known != source.materialIndex.end())
            {
                source.materials[static_cast<size_t>(known->second)].flags |= flag;
            }
            else
            {
                Print("azmpack: the sidecar names a material the model has not got: %s\n", entry.AsString().c_str());
            }
        }
    }

    bool Build(const Options& options, Source& source, Built& built, std::string& error)
    {
        if (!ParseObj(options.source, source, error))
        {
            return false;
        }

        if (source.positions.empty())
        {
            error = options.source.string() + " holds no vertices";
            return false;
        }

        const std::filesystem::path folder = std::filesystem::absolute(options.source).parent_path();

        for (const std::string& library : source.libraries)
        {
            ParseMtl(folder / library, source);
        }

        if (!options.mtl.empty())
        {
            ParseMtl(options.mtl, source);
        }

        Forge::JsonValue sidecar;

        if (!options.sidecar.empty() && !ReadSidecar(options.sidecar, sidecar, error))
        {
            return false;
        }

        ApplyMaterialFlags(source, sidecar, "paintable", AZM_MATERIAL_PAINTABLE);
        ApplyMaterialFlags(source, sidecar, "two_sided", AZM_MATERIAL_TWO_SIDED);

        const Forge::JsonValue* const added = MemberOf(sidecar, "additive");

        for (size_t at = 0; added != nullptr && added->Type() == Forge::JsonType::Array &&
             at < added->AsArray().size(); ++at)
        {
            const auto known = source.materialIndex.find(added->AsArray()[at].AsString());

            if (known != source.materialIndex.end())
            {
                Material& material = source.materials[static_cast<size_t>(known->second)];

                material.blend = AZM_BLEND_ADD;
                material.flags |= AZM_MATERIAL_GLOWS;
            }
        }

        const std::vector<Vec3> fallback = source.normals.empty() ? SmoothNormals(source)
                                                                  : std::vector<Vec3>();

        // The three things a name can mean.
        std::vector<const Group*> drawn;
        std::map<std::string, uint32_t> partOfName;

        for (const Group& group : source.groups)
        {
            Vec3 least;
            Vec3 most;
            BoundsOf(source, group, least, most);

            if (group.name.compare(0, 4, "col_") == 0)
            {
                AzmCollisionBox box = {};
                box.nameOffset = built.String(group.name.substr(4));
                box.min[0] = least.x; box.min[1] = least.y; box.min[2] = least.z;
                box.max[0] = most.x; box.max[1] = most.y; box.max[2] = most.z;
                built.boxes.push_back(box);
            }
            else if (group.name.compare(0, 3, "pt_") == 0)
            {
                const Vec3 facing = AverageNormal(source, group);

                AzmPoint point = {};
                point.nameOffset = built.String(group.name.substr(3));
                point.position[0] = (least.x + most.x) * 0.5f;
                point.position[1] = (least.y + most.y) * 0.5f;
                point.position[2] = (least.z + most.z) * 0.5f;
                point.direction[0] = facing.x;
                point.direction[1] = facing.y;
                point.direction[2] = facing.z;
                point.part = -1;
                built.points.push_back(point);
            }
            else
            {
                AzmPart part = {};
                part.nameOffset = built.String(group.name);
                part.parent = -1;
                part.pivot[0] = (least.x + most.x) * 0.5f;
                part.pivot[1] = (least.y + most.y) * 0.5f;
                part.pivot[2] = (least.z + most.z) * 0.5f;

                partOfName[group.name] = static_cast<uint32_t>(built.parts.size());
                built.parts.push_back(part);
                drawn.push_back(&group);
            }
        }

        // What names cannot say.
        Forge::JsonValue* const parts = sidecar.Type() == Forge::JsonType::Object
            ? const_cast<Forge::JsonValue*>(sidecar.Member("parts"))
            : nullptr;

        if (parts != nullptr && parts->Type() == Forge::JsonType::Object)
        {
            for (auto& entry : parts->MutableObject())
            {
                const auto known = partOfName.find(entry.first);

                if (known == partOfName.end())
                {
                    Print("azmpack: the sidecar names a part the model has not got: %s\n",
                        entry.first.c_str());
                    continue;
                }

                AzmPart& part = built.parts[known->second];
                const Forge::JsonValue* const parent = MemberOf(entry.second, "parent");
                const Forge::JsonValue* const pivot = MemberOf(entry.second, "pivot");
                const Forge::JsonValue* const hidden = MemberOf(entry.second, "hidden");

                if (parent != nullptr)
                {
                    const auto owner = partOfName.find(parent->AsString());
                    part.parent = owner != partOfName.end() ? static_cast<int32_t>(owner->second) : -1;

                    if (part.parent < 0)
                    {
                        Print("azmpack: %s asks for a parent that is not there: %s\n",
                            entry.first.c_str(), parent->AsString().c_str());
                    }
                }

                if (pivot != nullptr && pivot->Type() == Forge::JsonType::Array &&
                    pivot->AsArray().size() >= 3)
                {
                    for (int at = 0; at < 3; ++at)
                    {
                        part.pivot[at] = static_cast<float>(pivot->AsArray()[static_cast<size_t>(at)].AsNumber());
                    }
                }

                part.flags |= (hidden != nullptr && hidden->AsBoolean()) ? AZM_PART_HIDDEN : 0;
            }
        }

        Forge::JsonValue* const points = sidecar.Type() == Forge::JsonType::Object
            ? const_cast<Forge::JsonValue*>(sidecar.Member("points"))
            : nullptr;

        if (points != nullptr && points->Type() == Forge::JsonType::Object)
        {
            for (auto& entry : points->MutableObject())
            {
                const Forge::JsonValue* const where = MemberOf(entry.second, "position");
                const Forge::JsonValue* const facing = MemberOf(entry.second, "direction");
                const Forge::JsonValue* const owner = MemberOf(entry.second, "part");

                AzmPoint point = {};
                point.nameOffset = built.String(entry.first);
                point.direction[2] = 1.0f;
                point.part = -1;

                for (int at = 0; where != nullptr && where->Type() == Forge::JsonType::Array &&
                     where->AsArray().size() >= 3 && at < 3; ++at)
                {
                    point.position[at] = static_cast<float>(where->AsArray()[static_cast<size_t>(at)].AsNumber());
                }

                for (int at = 0; facing != nullptr && facing->Type() == Forge::JsonType::Array &&
                     facing->AsArray().size() >= 3 && at < 3; ++at)
                {
                    point.direction[at] = static_cast<float>(facing->AsArray()[static_cast<size_t>(at)].AsNumber());
                }

                if (owner != nullptr)
                {
                    const auto known = partOfName.find(owner->AsString());
                    point.part = known != partOfName.end() ? static_cast<int32_t>(known->second) : -1;
                }

                built.points.push_back(point);
            }
        }

        std::unordered_map<VertexKey, uint32_t, VertexHash> seen;
        std::vector<int> written(source.materials.size(), -1);

        for (size_t at = 0; at < drawn.size(); ++at)
        {
            const Group& group = *drawn[at];
            std::vector<int> order;

            for (const Face& face : group.faces)
            {
                if (std::find(order.begin(), order.end(), face.material) == order.end())
                {
                    order.push_back(face.material);
                }
            }

            for (const int slot : order)
            {
                const Material& material = source.materials[static_cast<size_t>(slot < 0 ? MaterialSlot(source, "") : slot)];

                if (written[static_cast<size_t>(slot)] < 0)
                {
                    AzmMaterial row = {};
                    row.nameOffset = built.String(material.name);
                    row.textureOffset = built.String(material.texture);
                    row.tint = PackColour(material.tint[0], material.tint[1], material.tint[2], 1.0f);
                    row.blend = material.blend;
                    row.flags = material.flags;
                    row.alpha = material.alpha;

                    written[static_cast<size_t>(slot)] = static_cast<int>(built.materials.size());
                    built.materials.push_back(row);
                }

                const uint32_t first = static_cast<uint32_t>(built.indices.size());

                for (const Face& face : group.faces)
                {
                    if (face.material != slot)
                    {
                        continue;
                    }

                    for (const Corner& corner : face.corner)
                    {
                        built.indices.push_back(AddVertex(built, seen, source, fallback, corner,
                            material, options));
                    }
                }

                AzmSubmesh submesh = {};
                submesh.part = static_cast<uint32_t>(at);
                submesh.material = static_cast<uint32_t>(written[static_cast<size_t>(slot)]);
                submesh.firstIndex = first;
                submesh.indexCount = static_cast<uint32_t>(built.indices.size()) - first;
                built.submeshes.push_back(submesh);
            }
        }

        return true;
    }

    void Append(std::vector<uint8_t>& out, const void* data, size_t bytes)
    {
        const uint8_t* const at = static_cast<const uint8_t*>(data);
        out.insert(out.end(), at, at + bytes);
    }

    void AppendChunk(std::vector<uint8_t>& out, uint32_t id, const std::vector<uint8_t>& payload)
    {
        const size_t padding = (4 - payload.size() % 4) % 4;

        AzmChunk chunk = {};
        chunk.id = id;
        chunk.size = static_cast<uint32_t>(payload.size() + padding);

        Append(out, &chunk, sizeof(chunk));
        Append(out, payload.data(), payload.size());
        out.insert(out.end(), padding, 0);
    }

    template <typename Row>
    std::vector<uint8_t> Table(const std::vector<Row>& rows)
    {
        AzmTable table = {};
        table.count = static_cast<uint32_t>(rows.size());
        table.stride = static_cast<uint32_t>(sizeof(Row));

        std::vector<uint8_t> payload;
        Append(payload, &table, sizeof(table));

        if (!rows.empty())
        {
            Append(payload, rows.data(), rows.size() * sizeof(Row));
        }
        return payload;
    }

    bool WriteAzm(const Options& options, const Built& built, std::string& error)
    {
        Vec3 least = { FLT_MAX, FLT_MAX, FLT_MAX };
        Vec3 most = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

        for (const AzmVertex& vertex : built.vertices)
        {
            least.x = vertex.position[0] < least.x ? vertex.position[0] : least.x;
            least.y = vertex.position[1] < least.y ? vertex.position[1] : least.y;
            least.z = vertex.position[2] < least.z ? vertex.position[2] : least.z;
            most.x = vertex.position[0] > most.x ? vertex.position[0] : most.x;
            most.y = vertex.position[1] > most.y ? vertex.position[1] : most.y;
            most.z = vertex.position[2] > most.z ? vertex.position[2] : most.z;
        }

        Built& writable = const_cast<Built&>(built);
        const std::string name = options.name.empty()
            ? options.out.stem().string()
            : options.name;

        AzmHead head = {};
        head.unitsPerMetre = options.units;
        head.forwardAxis = options.forward;
        head.upAxis = options.up;
        head.boundsMin[0] = least.x; head.boundsMin[1] = least.y; head.boundsMin[2] = least.z;
        head.boundsMax[0] = most.x; head.boundsMax[1] = most.y; head.boundsMax[2] = most.z;
        head.nameOffset = writable.String(name);

        std::vector<uint8_t> indices;
        const uint32_t indexCount = static_cast<uint32_t>(built.indices.size());
        Append(indices, &indexCount, sizeof(indexCount));

        if (indexCount != 0)
        {
            Append(indices, built.indices.data(), built.indices.size() * sizeof(uint32_t));
        }

        // STRS is assembled last on purpose: everything above puts names into it as it goes.
        std::vector<uint8_t> body;
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> chunks;

        chunks.emplace_back(AZM_CHUNK_HEAD, std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&head), reinterpret_cast<const uint8_t*>(&head) + sizeof(head)));
        chunks.emplace_back(AZM_CHUNK_STRS, std::vector<uint8_t>(
            writable.strings.begin(), writable.strings.end()));
        chunks.emplace_back(AZM_CHUNK_VERT, Table(built.vertices));
        chunks.emplace_back(AZM_CHUNK_INDX, indices);
        chunks.emplace_back(AZM_CHUNK_MATL, Table(built.materials));
        chunks.emplace_back(AZM_CHUNK_PART, Table(built.parts));
        chunks.emplace_back(AZM_CHUNK_SUBM, Table(built.submeshes));

        if (!built.boxes.empty())
        {
            chunks.emplace_back(AZM_CHUNK_COLL, Table(built.boxes));
        }

        if (!built.points.empty())
        {
            chunks.emplace_back(AZM_CHUNK_POIN, Table(built.points));
        }

        AzmHeader header = {};
        header.magic = AZM_MAGIC;
        header.version = AZM_VERSION;
        header.chunkCount = static_cast<uint32_t>(chunks.size());

        Append(body, &header, sizeof(header));

        for (const auto& chunk : chunks)
        {
            AppendChunk(body, chunk.first, chunk.second);
        }

        std::ofstream file(options.out, std::ios::binary);

        if (!file.is_open())
        {
            error = "cannot write " + options.out.string();
            return false;
        }

        file.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
        return file.good();
    }

    const char* AxisName(uint32_t axis)
    {
        static const char* const names[] = { "+x", "-x", "+y", "-y", "+z", "-z" };
        return axis < 6 ? names[axis] : "?";
    }

    const char* BlendName(uint32_t blend)
    {
        static const char* const names[] = { "none", "blend", "add", "mul" };
        return blend < 4 ? names[blend] : "?";
    }

    int CommandDump(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);

        if (!file.is_open())
        {
            Print("azmpack: cannot open %s\n", path.string().c_str());
            return kExitError;
        }

        const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (data.size() < sizeof(AzmHeader))
        {
            Print("azmpack: %s is too short to be a model\n", path.string().c_str());
            return kExitError;
        }

        AzmHeader header = {};
        memcpy(&header, data.data(), sizeof(header));

        if (header.magic != AZM_MAGIC)
        {
            Print("azmpack: %s does not start with AZM1\n", path.string().c_str());
            return kExitError;
        }

        Print("%s: version %u, %u chunk(s), %zu bytes\n", path.filename().string().c_str(),
            header.version, header.chunkCount, data.size());

        std::map<uint32_t, std::pair<size_t, uint32_t>> chunks;
        size_t at = sizeof(AzmHeader);

        while (at + sizeof(AzmChunk) <= data.size())
        {
            AzmChunk chunk = {};
            memcpy(&chunk, data.data() + at, sizeof(chunk));

            char name[5] = {};
            memcpy(name, &chunk.id, 4);

            Print("  %s  %u bytes\n", name, chunk.size);
            chunks[chunk.id] = { at + sizeof(AzmChunk), chunk.size };
            at += sizeof(AzmChunk) + chunk.size;
        }

        const auto strings = chunks.find(AZM_CHUNK_STRS);
        const auto text = [&](uint32_t offset) -> std::string
        {
            if (strings == chunks.end() || offset >= strings->second.second)
            {
                return "";
            }
            return std::string(reinterpret_cast<const char*>(data.data()) + strings->second.first + offset);
        };

        const auto table = [&](uint32_t id, AzmTable& out) -> size_t
        {
            const auto found = chunks.find(id);

            if (found == chunks.end() || found->second.second < sizeof(AzmTable))
            {
                out = {};
                return 0;
            }

            memcpy(&out, data.data() + found->second.first, sizeof(out));
            return found->second.first + sizeof(AzmTable);
        };

        const auto head = chunks.find(AZM_CHUNK_HEAD);

        if (head != chunks.end() && head->second.second >= sizeof(AzmHead))
        {
            AzmHead info = {};
            memcpy(&info, data.data() + head->second.first, sizeof(info));

            Print("  name '%s', %g unit(s) a metre, faces %s, up %s\n", text(info.nameOffset).c_str(),
                info.unitsPerMetre, AxisName(info.forwardAxis), AxisName(info.upAxis));
            Print("  bounds %.3f %.3f %.3f .. %.3f %.3f %.3f\n", info.boundsMin[0], info.boundsMin[1],
                info.boundsMin[2], info.boundsMax[0], info.boundsMax[1], info.boundsMax[2]);
        }

        AzmTable vertices = {};

        if (table(AZM_CHUNK_VERT, vertices) != 0)
        {
            Print("  %u vertices, %u bytes each%s\n", vertices.count, vertices.stride,
                vertices.count <= kEngineVertexLimit ? "" : "  <- over 0xFFFF, the loader cuts it");
        }

        const auto indices = chunks.find(AZM_CHUNK_INDX);

        if (indices != chunks.end() && indices->second.second >= sizeof(uint32_t))
        {
            uint32_t count = 0;
            memcpy(&count, data.data() + indices->second.first, sizeof(count));
            Print("  %u indices, %u triangle(s)\n", count, count / 3);
        }

        AzmTable materials = {};
        size_t row = table(AZM_CHUNK_MATL, materials);

        for (uint32_t index = 0; index < materials.count; ++index)
        {
            AzmMaterial entry = {};
            memcpy(&entry, data.data() + row + index * materials.stride, sizeof(entry));

            Print("  material %u '%s' texture '%s' tint %08X %s alpha %.2f flags %02X\n", index,
                text(entry.nameOffset).c_str(), text(entry.textureOffset).c_str(), entry.tint,
                BlendName(entry.blend), entry.alpha, entry.flags);
        }

        AzmTable parts = {};
        row = table(AZM_CHUNK_PART, parts);

        for (uint32_t index = 0; index < parts.count; ++index)
        {
            AzmPart entry = {};
            memcpy(&entry, data.data() + row + index * parts.stride, sizeof(entry));

            Print("  part %u '%s' parent %d pivot %.3f %.3f %.3f flags %02X\n", index,
                text(entry.nameOffset).c_str(), entry.parent, entry.pivot[0], entry.pivot[1],
                entry.pivot[2], entry.flags);
        }

        AzmTable submeshes = {};
        row = table(AZM_CHUNK_SUBM, submeshes);

        for (uint32_t index = 0; index < submeshes.count; ++index)
        {
            AzmSubmesh entry = {};
            memcpy(&entry, data.data() + row + index * submeshes.stride, sizeof(entry));

            Print("  submesh %u part %u material %u indices %u..%u\n", index, entry.part,
                entry.material, entry.firstIndex, entry.firstIndex + entry.indexCount);
        }

        AzmTable boxes = {};
        row = table(AZM_CHUNK_COLL, boxes);

        for (uint32_t index = 0; index < boxes.count; ++index)
        {
            AzmCollisionBox entry = {};
            memcpy(&entry, data.data() + row + index * boxes.stride, sizeof(entry));

            Print("  box %u '%s' %.3f %.3f %.3f .. %.3f %.3f %.3f\n", index,
                text(entry.nameOffset).c_str(), entry.min[0], entry.min[1], entry.min[2],
                entry.max[0], entry.max[1], entry.max[2]);
        }

        AzmTable points = {};
        row = table(AZM_CHUNK_POIN, points);

        for (uint32_t index = 0; index < points.count; ++index)
        {
            AzmPoint entry = {};
            memcpy(&entry, data.data() + row + index * points.stride, sizeof(entry));

            Print("  point %u '%s' at %.3f %.3f %.3f facing %.3f %.3f %.3f, part %d\n", index,
                text(entry.nameOffset).c_str(), entry.position[0], entry.position[1],
                entry.position[2], entry.direction[0], entry.direction[1], entry.direction[2],
                entry.part);
        }

        return kExitOk;
    }

    bool ReadAxis(const std::string& text, uint32_t& out)
    {
        static const char* const names[] = { "+x", "-x", "+y", "-y", "+z", "-z" };

        for (uint32_t at = 0; at < 6; ++at)
        {
            if (text == names[at])
            {
                out = at;
                return true;
            }
        }
        return false;
    }

    std::string Narrow(const std::wstring& text)
    {
        if (text.empty())
        {
            return std::string();
        }

        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(bytes), '\0');

        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), bytes, nullptr, nullptr);
        return out;
    }

    int CommandPack(const std::vector<std::wstring>& args)
    {
        Options options;

        for (size_t at = 0; at < args.size(); ++at)
        {
            const std::wstring& word = args[at];
            const bool more = at + 1 < args.size();

            if (word == L"-o" && more)                      options.out = args[++at];
            else if (word == L"--sidecar" && more)          options.sidecar = args[++at];
            else if (word == L"--mtl" && more)              options.mtl = args[++at];
            else if (word == L"--name" && more)             options.name = Narrow(args[++at]);
            else if (word == L"--units" && more)            options.units = static_cast<float>(_wtof(args[++at].c_str()));
            else if (word == L"--ambient" && more)          options.ambient = static_cast<float>(_wtof(args[++at].c_str()));
            else if (word == L"--no-shade")                 options.shade = false;
            else if (word == L"--keep-v")                   options.keepV = true;
            else if (word == L"--forward" && more)
            {
                if (!ReadAxis(Narrow(args[++at]), options.forward))
                {
                    Print("azmpack: --forward wants one of +x -x +y -y +z -z\n");
                    return kExitUsage;
                }
            }
            else if (word == L"--up" && more)
            {
                if (!ReadAxis(Narrow(args[++at]), options.up))
                {
                    Print("azmpack: --up wants one of +x -x +y -y +z -z\n");
                    return kExitUsage;
                }
            }
            else if (word == L"--light" && more)
            {
                const std::string text = Narrow(args[++at]);
                if (sscanf_s(text.c_str(), "%f,%f,%f", &options.light.x, &options.light.y,
                             &options.light.z) != 3)
                {
                    Print("azmpack: --light wants three numbers, as x,y,z\n");
                    return kExitUsage;
                }
            }
            else if (!word.empty() && word[0] == L'-')
            {
                Print("azmpack: what is %s?\n", Narrow(word).c_str());
                return kExitUsage;
            }
            else if (options.source.empty())
            {
                options.source = word;
            }
        }

        if (options.source.empty())
        {
            Print("azmpack: pack needs an .obj\n");
            return kExitUsage;
        }

        if (options.out.empty())
        {
            options.out = options.source;
            options.out.replace_extension(".azm");
        }

        options.light = Normalised(options.light);

        Source source;
        Built built;
        std::string error;

        if (!Build(options, source, built, error))
        {
            Print("azmpack: %s\n", error.c_str());
            return kExitError;
        }

        if (built.vertices.empty())
        {
            Print("azmpack: nothing was drawn - every object in %s is a col_ or a pt_\n",
                options.source.filename().string().c_str());
            return kExitError;
        }

        if (!WriteAzm(options, built, error))
        {
            Print("azmpack: %s\n", error.c_str());
            return kExitError;
        }

        Print("%s -> %s\n", options.source.filename().string().c_str(),
            options.out.filename().string().c_str());
        Print("  %zu vertices, %zu triangles, %zu material(s), %zu part(s), %zu submesh(es)\n",
            built.vertices.size(), built.indices.size() / 3, built.materials.size(),
            built.parts.size(), built.submeshes.size());

        if (!built.boxes.empty())
        {
            Print("  %zu collision box(es)\n", built.boxes.size());
        }

        if (!built.points.empty())
        {
            Print("  %zu named point(s)\n", built.points.size());
        }

        if (built.vertices.size() > kEngineVertexLimit)
        {
            Print("  over %d vertices: the loader cuts it into that many at a time\n",
                kEngineVertexLimit);
        }

        return kExitOk;
    }

    void PrintUsage()
    {
        Print("azmpack - OBJ+MTL into .azm, the model format AzForge reads (version %d)\n\n",
            AZM_VERSION);
        Print("  azmpack pack <file.obj> [-o <file.azm>] [options]   build a model\n");
        Print("  azmpack dump <file.azm>                             print what one holds\n\n");
        Print("Options:\n");
        Print("  --sidecar <file.json>  parents, pivots, points and collision\n");
        Print("  --mtl <file.mtl>       a material library besides the one the OBJ names\n");
        Print("  --name <text>          what the model calls itself; the file name otherwise\n");
        Print("  --units <number>       source units in a metre (default 1)\n");
        Print("  --forward <axis>       which way the model faces (default +z)\n");
        Print("  --up <axis>            which way is up (default +y)\n");
        Print("  --light <x,y,z>        the direction the baked light travels (default -0.35,-1,-0.5)\n");
        Print("  --ambient <number>     how bright a face turned away stays (default 0.4)\n");
        Print("  --no-shade             leave the vertex colour unshaded\n");
        Print("  --keep-v               do not flip v; OBJ counts it up, a texture counts down\n\n");
        Print("Object names decide what an object is: col_* a collision box, pt_* a named point,\n");
        Print("anything else a part. Shade is baked into the vertex colour because the engine\n");
        Print("draws a mod's mesh with no light at all.\n");
    }
}

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2)
    {
        PrintUsage();
        return kExitUsage;
    }

    const std::wstring command = argv[1];
    const std::vector<std::wstring> args(argv + 2, argv + argc);

    if (command == L"pack")
    {
        return CommandPack(args);
    }

    if (command == L"dump" && !args.empty())
    {
        return CommandDump(args[0]);
    }

    PrintUsage();
    return kExitUsage;
}
