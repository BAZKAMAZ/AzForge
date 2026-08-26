#include <windows.h>

#include "src/Api/AzmApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/MmdApi.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Mods/ModFileSystem.h"

#include <fstream>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;

        AmodAzmApi g_table = {};

        constexpr int kEngineVertexLimit = 0xFFFF;

        struct Material
        {
            std::string name;
            std::string texture;
            uint32_t tint = 0xFFFFFFFF;
            uint32_t flags = 0;
            int blend = AMOD_DRAW_BLEND_NONE;
            float alpha = 1.0f;
            uint32_t handle = 0;
            uint32_t paint = 0xFFFFFFFF;
        };

        struct Mesh
        {
            AmodModelId model = AMOD_INVALID_MODEL;
            uint32_t texture = 0;
            std::vector<uint32_t> base;
            std::vector<uint16_t> owner;
        };

        struct Part
        {
            std::string name;
            int parent = -1;
            float pivot[3] = {};
            uint32_t flags = 0;
            int blend = AMOD_DRAW_BLEND_NONE;
            bool hidden = false;

            bool posed = false;
            float pose[16] = {};
            uint32_t tint = 0xFFFFFFFFu;

            std::vector<int> meshes;
        };

        struct Named
        {
            std::string name;
            float first[3] = {};
            float second[3] = {};
            int part = -1;
        };

        struct Loaded
        {
            AmodAzmId id = AMOD_INVALID_AZM;
            ModContext* owner = nullptr;
            int references = 1;

            std::string path;
            std::string name;

            std::vector<Material> materials;
            std::vector<Mesh> meshes;
            std::vector<Part> parts;
            std::vector<Named> points;
            std::vector<Named> boxes;

            float boundsMin[3] = {};
            float boundsMax[3] = {};
            float units = 1.0f;
            uint32_t forward = AZM_AXIS_Z_PLUS;
            uint32_t up = AZM_AXIS_Y_PLUS;

            int vertexCount = 0;
            int triangleCount = 0;
            uint32_t tint = 0xFFFFFFFFu;
        };

        std::vector<Loaded> g_loaded;
        std::vector<AmodModelDraw> g_queue;
        uint32_t g_nextId = 1;

        Loaded* Find(AmodAzmId id)
        {
            for (Loaded& loaded : g_loaded)
            {
                if (loaded.id == id)
                {
                    return &loaded;
                }
            }
            return nullptr;
        }

        Loaded* FindFor(AmodMod mod, AmodAzmId id)
        {
            ModContext* const context = Caller(mod);
            Loaded* const loaded = context != nullptr ? Find(id) : nullptr;
            return (loaded != nullptr && loaded->owner == context) ? loaded : nullptr;
        }

        void Identity(float* out)
        {
            memset(out, 0, sizeof(float) * 16);
            out[0] = out[5] = out[10] = out[15] = 1.0f;
        }

        void Multiply(const float* a, const float* b, float* out)
        {
            float work[16];

            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 4; ++column)
                {
                    float sum = 0.0f;

                    for (int at = 0; at < 4; ++at)
                    {
                        sum += a[row * 4 + at] * b[at * 4 + column];
                    }
                    work[row * 4 + column] = sum;
                }
            }

            memcpy(out, work, sizeof(work));
        }

        void AxisAngle(const float* axis, float angle, float* out)
        {
            const float length = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
            const float unit[3] =
            {
                length > 1.0e-9f ? axis[0] / length : 0.0f,
                length > 1.0e-9f ? axis[1] / length : 1.0f,
                length > 1.0e-9f ? axis[2] / length : 0.0f,
            };

            const float turn = cosf(angle);
            const float lean = sinf(angle);
            const float rest = 1.0f - turn;

            Identity(out);
            out[0] = turn * (1.0f - unit[0] * unit[0]) + unit[0] * unit[0];
            out[1] = unit[1] * unit[0] * rest + lean * unit[2];
            out[2] = unit[2] * unit[0] * rest - unit[1] * lean;
            out[4] = unit[1] * unit[0] * rest - unit[2] * lean;
            out[5] = turn * (1.0f - unit[1] * unit[1]) + unit[1] * unit[1];
            out[6] = unit[0] * lean + unit[2] * unit[1] * rest;
            out[8] = unit[1] * lean + unit[2] * unit[0] * rest;
            out[9] = unit[2] * unit[1] * rest - lean * unit[0];
            out[10] = turn * (1.0f - unit[2] * unit[2]) + unit[2] * unit[2];
        }

        // A part's own turn happens about its pivot, not about the model's origin.
        void AboutPivot(const Part& part, float* out)
        {
            if (!part.posed)
            {
                Identity(out);
                return;
            }

            float there[16];
            float back[16];
            float work[16];

            Identity(there);
            there[12] = -part.pivot[0];
            there[13] = -part.pivot[1];
            there[14] = -part.pivot[2];

            Identity(back);
            back[12] = part.pivot[0];
            back[13] = part.pivot[1];
            back[14] = part.pivot[2];

            Multiply(there, part.pose, work);
            Multiply(work, back, out);
        }

        // Its own turn, then every parent's, then wherever the model itself stands.
        void WorldFor(const Loaded& loaded, int index, const float* placement, float* out)
        {
            float total[16];
            AboutPivot(loaded.parts[static_cast<size_t>(index)], total);

            int parent = loaded.parts[static_cast<size_t>(index)].parent;

            for (int guard = 0; parent >= 0 && parent < static_cast<int>(loaded.parts.size()) &&
                 guard < 64; ++guard)
            {
                float above[16];
                AboutPivot(loaded.parts[static_cast<size_t>(parent)], above);
                Multiply(total, above, total);
                parent = loaded.parts[static_cast<size_t>(parent)].parent;
            }

            Multiply(total, placement, out);
        }

        struct Reader
        {
            const uint8_t* bytes = nullptr;
            size_t length = 0;

            struct Chunk
            {
                uint32_t id = 0;
                size_t at = 0;
                uint32_t size = 0;
            };

            std::vector<Chunk> chunks;

            const Chunk* Get(uint32_t id) const
            {
                for (const Chunk& chunk : chunks)
                {
                    if (chunk.id == id)
                    {
                        return &chunk;
                    }
                }
                return nullptr;
            }

            template <typename T>
            bool Read(size_t at, T& out) const
            {
                if (at + sizeof(T) > length)
                {
                    return false;
                }
                memcpy(&out, bytes + at, sizeof(T));
                return true;
            }

            template <typename T>
            bool Row(const Chunk* chunk, const AzmTable& table, uint32_t index, T& out) const
            {
                const size_t at = chunk->at + sizeof(AzmTable) + static_cast<size_t>(index) * table.stride;
                const size_t take = table.stride < sizeof(T) ? table.stride : sizeof(T);

                if (chunk == nullptr || index >= table.count || at + table.stride > length)
                {
                    return false;
                }

                memset(&out, 0, sizeof(T));
                memcpy(&out, bytes + at, take);
                return true;
            }

            bool Table(uint32_t id, AzmTable& out, const Chunk*& chunk) const
            {
                chunk = Get(id);

                if (chunk == nullptr || chunk->size < sizeof(AzmTable) || !Read(chunk->at, out))
                {
                    out = {};
                    return false;
                }

                return out.stride != 0 && chunk->size >= sizeof(AzmTable) + static_cast<size_t>(out.count) * out.stride;
            }

            std::string Text(uint32_t offset) const
            {
                const Chunk* const strings = Get(AZM_CHUNK_STRS);

                if (strings == nullptr || offset >= strings->size)
                {
                    return std::string();
                }

                const char* const from = reinterpret_cast<const char*>(bytes + strings->at + offset);
                const size_t room = strings->size - offset;
                const size_t end = strnlen(from, room);
                return std::string(from, end);
            }
        };

        bool OpenReader(const uint8_t* bytes, size_t length, Reader& out, std::string& error)
        {
            AzmHeader header = {};

            if (length < sizeof(AzmHeader))
            {
                error = "too short to be a model";
                return false;
            }

            memcpy(&header, bytes, sizeof(header));

            if (header.magic != AZM_MAGIC)
            {
                error = "does not start with AZM1";
                return false;
            }

            if (header.version > AZM_VERSION)
            {
                error = "was written by a newer azmpack";
                return false;
            }

            out.bytes = bytes;
            out.length = length;

            size_t at = sizeof(AzmHeader);

            while (at + sizeof(AzmChunk) <= length)
            {
                AzmChunk chunk = {};
                memcpy(&chunk, bytes + at, sizeof(chunk));

                if (at + sizeof(AzmChunk) + chunk.size > length)
                {
                    error = "a chunk runs off the end of the file";
                    return false;
                }

                out.chunks.push_back(Reader::Chunk{ chunk.id, at + sizeof(AzmChunk), chunk.size });
                at += sizeof(AzmChunk) + chunk.size;
            }

            return true;
        }

        bool ReadWhole(const char* path, std::vector<uint8_t>& out)
        {
            std::string provider;
            std::string error;

            if (ModFileSystem::Read(path, out, provider, error) == ModFileResult::Loaded)
            {
                return true;
            }

            std::ifstream file(path, std::ios::binary);

            if (!file.is_open())
            {
                return false;
            }

            out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            return !out.empty();
        }

        struct Building
        {
            std::vector<AmodVertex> vertices;
            std::vector<uint16_t> indices;
            std::vector<uint32_t> colours;
            std::vector<uint16_t> owner;
            std::vector<int> remap;

            struct Range
            {
                uint32_t material = 0;
                int first = 0;
                int count = 0;
            };

            std::vector<Range> ranges;
        };

        std::string TextureBeside(const std::string& modelPath, const std::string& texture)
        {
            const size_t slash = modelPath.find_last_of("/\\");

            if (slash == std::string::npos)
            {
                return texture;
            }

            return modelPath.substr(0, slash + 1) + texture;
        }

        bool BuildMesh(AmodMod mod, Loaded& loaded, Building& building, const std::string& name)
        {
            const AmodMmdApi* const mmd = MmdApi::Table();

            if (building.indices.empty() || mmd == nullptr || mmd->Create == nullptr)
            {
                return false;
            }

            AmodMeshData data = {};
            data.structSize = sizeof(data);
            data.frameCount = 1;
            data.vertexCount = static_cast<int>(building.vertices.size());
            data.indexCount = static_cast<int>(building.indices.size());
            data.primitiveCount = data.indexCount / 3;
            data.vertices = building.vertices.data();
            data.indices = building.indices.data();

            const AmodModelId model = mmd->Create(mod, name.c_str(), &data);

            if (model == AMOD_INVALID_MODEL)
            {
                return false;
            }

            Mesh mesh;
            mesh.model = model;
            mesh.base = building.colours;
            mesh.owner = building.owner;

            if (mmd->SetColours != nullptr)
            {
                mmd->SetColours(mod, model, mesh.base.data(), static_cast<int>(mesh.base.size()));
            }

            for (const Building::Range& range : building.ranges)
            {
                Material& material = loaded.materials[range.material];

                if (material.handle == 0 && !material.texture.empty() && mmd->ResolveTexture != nullptr)
                {
                    const std::string here = TextureBeside(loaded.path, material.texture);

                    material.handle = mmd->ResolveTexture(mod, here.c_str());

                    if (material.handle == 0)
                    {
                        material.handle = mmd->ResolveTexture(mod, material.texture.c_str());
                    }
                }

                if (mmd->AddMaterial != nullptr)
                {
                    mmd->AddMaterial(mod, model, range.first, range.count / 3, material.handle);
                }

                mesh.texture = mesh.texture == 0 ? material.handle : mesh.texture;
            }

            loaded.meshes.push_back(mesh);
            return true;
        }

        bool BuildParts(AmodMod mod, Loaded& loaded, const Reader& reader, const AzmTable& partTable, const Reader::Chunk* partChunk, const AzmTable& submeshTable,
            const Reader::Chunk* submeshChunk, const uint8_t* indexBase, uint32_t indexCount, const AzmTable& vertexTable, const Reader::Chunk* vertexChunk)
        {
            for (uint32_t at = 0; at < partTable.count; ++at)
            {
                Building building;
                building.remap.assign(vertexTable.count, -1);

                Part& part = loaded.parts[at];
                int made = 0;

                for (uint32_t which = 0; which < submeshTable.count; ++which)
                {
                    AzmSubmesh submesh = {};

                    if (!reader.Row(submeshChunk, submeshTable, which, submesh) || submesh.part != at ||
                        submesh.material >= loaded.materials.size())
                    {
                        continue;
                    }

                    if (submesh.firstIndex + submesh.indexCount > indexCount)
                    {
                        FORGE_LOG_WARN("azm: '%s' submesh %u runs past the indices", loaded.path.c_str(), which);
                        continue;
                    }

                    part.blend = part.blend == AMOD_DRAW_BLEND_NONE
                        ? loaded.materials[submesh.material].blend
                        : part.blend;

                    Building::Range range;
                    range.material = submesh.material;
                    range.first = static_cast<int>(building.indices.size());

                    for (uint32_t step = 0; step < submesh.indexCount; step += 3)
                    {
                        // A triangle is kept whole: three corners never straddle two meshes.
                        if (building.vertices.size() + 3 > static_cast<size_t>(kEngineVertexLimit))
                        {
                            range.count = static_cast<int>(building.indices.size()) - range.first;

                            if (range.count > 0)
                            {
                                building.ranges.push_back(range);
                            }

                            made += BuildMesh(mod, loaded, building, loaded.path + ":" + part.name +
                                              ":" + std::to_string(made)) ? 1 : 0;
                            part.meshes.push_back(static_cast<int>(loaded.meshes.size()) - 1);

                            building.vertices.clear();
                            building.indices.clear();
                            building.colours.clear();
                            building.owner.clear();
                            building.ranges.clear();
                            building.remap.assign(vertexTable.count, -1);

                            range.first = 0;
                        }

                        for (uint32_t corner = 0; corner < 3; ++corner)
                        {
                            uint32_t index = 0;
                            memcpy(&index, indexBase + (static_cast<size_t>(submesh.firstIndex) + step + corner) * sizeof(uint32_t), sizeof(index));

                            if (index >= vertexTable.count)
                            {
                                index = 0;
                            }

                            int& slot = building.remap[index];

                            if (slot < 0)
                            {
                                AzmVertex vertex = {};
                                reader.Row(vertexChunk, vertexTable, index, vertex);

                                AmodVertex shaped = {};
                                memcpy(shaped.position, vertex.position, sizeof(shaped.position));
                                memcpy(shaped.normal, vertex.normal, sizeof(shaped.normal));
                                memcpy(shaped.uv, vertex.uv, sizeof(shaped.uv));

                                slot = static_cast<int>(building.vertices.size());
                                building.vertices.push_back(shaped);
                                building.colours.push_back(vertex.colour);
                                building.owner.push_back(static_cast<uint16_t>(submesh.material));
                            }

                            building.indices.push_back(static_cast<uint16_t>(slot));
                        }
                    }

                    range.count = static_cast<int>(building.indices.size()) - range.first;

                    if (range.count > 0)
                    {
                        building.ranges.push_back(range);
                    }
                }

                if (!building.indices.empty())
                {
                    loaded.triangleCount += static_cast<int>(building.indices.size()) / 3;
                    loaded.vertexCount += static_cast<int>(building.vertices.size());

                    if (BuildMesh(mod, loaded, building, loaded.path + ":" + part.name + ":" + std::to_string(made)))
                    {
                        part.meshes.push_back(static_cast<int>(loaded.meshes.size()) - 1);
                    }
                }
            }

            return !loaded.meshes.empty();
        }

        AmodAzmId BuildFrom(AmodMod mod, const char* path, const uint8_t* bytes, size_t length)
        {
            ModContext* const context = Caller(mod);
            Reader reader;
            std::string error;

            if (context == nullptr || !OpenReader(bytes, length, reader, error))
            {
                FORGE_LOG_WARN("azm: '%s' %s", path != nullptr ? path : "?", error.c_str());
                return AMOD_INVALID_AZM;
            }

            Loaded loaded;
            loaded.owner = context;
            loaded.path = path != nullptr ? path : "";

            const Reader::Chunk* const head = reader.Get(AZM_CHUNK_HEAD);
            AzmHead info = {};

            if (head != nullptr && head->size >= sizeof(AzmHead) && reader.Read(head->at, info))
            {
                loaded.units = info.unitsPerMetre > 0.0f ? info.unitsPerMetre : 1.0f;
                loaded.forward = info.forwardAxis;
                loaded.up = info.upAxis;
                memcpy(loaded.boundsMin, info.boundsMin, sizeof(loaded.boundsMin));
                memcpy(loaded.boundsMax, info.boundsMax, sizeof(loaded.boundsMax));
                loaded.name = reader.Text(info.nameOffset);
            }

            const Reader::Chunk* chunk = nullptr;
            AzmTable table = {};

            if (reader.Table(AZM_CHUNK_MATL, table, chunk))
            {
                for (uint32_t at = 0; at < table.count; ++at)
                {
                    AzmMaterial row = {};
                    reader.Row(chunk, table, at, row);

                    Material material;
                    material.name = reader.Text(row.nameOffset);
                    material.texture = reader.Text(row.textureOffset);
                    material.tint = row.tint;
                    material.flags = row.flags;
                    material.blend = static_cast<int>(row.blend);
                    material.alpha = row.alpha;
                    loaded.materials.push_back(material);
                }
            }

            const Reader::Chunk* partChunk = nullptr;
            AzmTable partTable = {};

            if (reader.Table(AZM_CHUNK_PART, partTable, partChunk))
            {
                for (uint32_t at = 0; at < partTable.count; ++at)
                {
                    AzmPart row = {};
                    reader.Row(partChunk, partTable, at, row);

                    Part part;
                    part.name = reader.Text(row.nameOffset);
                    part.parent = row.parent;
                    part.flags = row.flags;
                    part.hidden = (row.flags & AZM_PART_HIDDEN) != 0;
                    memcpy(part.pivot, row.pivot, sizeof(part.pivot));
                    loaded.parts.push_back(part);
                }
            }

            if (reader.Table(AZM_CHUNK_POIN, table, chunk))
            {
                for (uint32_t at = 0; at < table.count; ++at)
                {
                    AzmPoint row = {};
                    reader.Row(chunk, table, at, row);

                    Named point;
                    point.name = reader.Text(row.nameOffset);
                    memcpy(point.first, row.position, sizeof(point.first));
                    memcpy(point.second, row.direction, sizeof(point.second));
                    point.part = row.part;
                    loaded.points.push_back(point);
                }
            }

            if (reader.Table(AZM_CHUNK_COLL, table, chunk))
            {
                for (uint32_t at = 0; at < table.count; ++at)
                {
                    AzmCollisionBox row = {};
                    reader.Row(chunk, table, at, row);

                    Named box;
                    box.name = reader.Text(row.nameOffset);
                    memcpy(box.first, row.min, sizeof(box.first));
                    memcpy(box.second, row.max, sizeof(box.second));
                    loaded.boxes.push_back(box);
                }
            }

            const Reader::Chunk* const indexChunk = reader.Get(AZM_CHUNK_INDX);
            uint32_t indexCount = 0;

            if (indexChunk == nullptr || indexChunk->size < sizeof(uint32_t) || !reader.Read(indexChunk->at, indexCount) ||
                indexChunk->size < sizeof(uint32_t) + static_cast<size_t>(indexCount) * sizeof(uint32_t))
            {
                FORGE_LOG_WARN("azm: '%s' has no indices worth reading", loaded.path.c_str());
                return AMOD_INVALID_AZM;
            }

            const Reader::Chunk* vertexChunk = nullptr;
            AzmTable vertexTable = {};
            const Reader::Chunk* submeshChunk = nullptr;
            AzmTable submeshTable = {};

            if (!reader.Table(AZM_CHUNK_VERT, vertexTable, vertexChunk) ||
                !reader.Table(AZM_CHUNK_SUBM, submeshTable, submeshChunk) || loaded.parts.empty())
            {
                FORGE_LOG_WARN("azm: '%s' is missing its vertices, its submeshes or its parts", loaded.path.c_str());
                return AMOD_INVALID_AZM;
            }

            if (!BuildParts(mod, loaded, reader, partTable, partChunk, submeshTable, submeshChunk, bytes + indexChunk->at + sizeof(uint32_t), 
                indexCount, vertexTable, vertexChunk))
            {
                FORGE_LOG_WARN("azm: '%s' built no meshes at all", loaded.path.c_str());
                return AMOD_INVALID_AZM;
            }

            loaded.id = g_nextId++;
            g_loaded.push_back(loaded);

            FORGE_LOG_INFO("azm: '%s' %d part(s), %d mesh(es), %d triangle(s)", loaded.path.c_str(),
                static_cast<int>(loaded.parts.size()), static_cast<int>(loaded.meshes.size()),
                loaded.triangleCount);

            return loaded.id;
        }

        AmodAzmId __cdecl ApiLoad(AmodMod mod, const char* path)
        {
            ModContext* const context = Caller(mod);

            if (context == nullptr || path == nullptr || path[0] == '\0')
            {
                return AMOD_INVALID_AZM;
            }

            for (Loaded& loaded : g_loaded)
            {
                if (loaded.owner == context && loaded.path == path)
                {
                    ++loaded.references;
                    return loaded.id;
                }
            }

            std::vector<uint8_t> bytes;

            if (!ReadWhole(path, bytes))
            {
                return static_cast<AmodAzmId>(Fail(mod, AMOD_FAIL_NO_SUCH, "no model at %s", path));
            }

            return BuildFrom(mod, path, bytes.data(), bytes.size());
        }

        AmodAzmId __cdecl ApiOpen(AmodMod mod, const char* name, const void* bytes, int length)
        {
            return (bytes != nullptr && length > 0)
                ? BuildFrom(mod, name, static_cast<const uint8_t*>(bytes), static_cast<size_t>(length))
                : AMOD_INVALID_AZM;
        }

        int __cdecl ApiRelease(AmodMod mod, AmodAzmId id)
        {
            Loaded* const loaded = FindFor(mod, id);
            const AmodMmdApi* const mmd = MmdApi::Table();

            if (loaded == nullptr || --loaded->references > 0)
            {
                return loaded != nullptr ? 1 : 0;
            }

            for (const Mesh& mesh : loaded->meshes)
            {
                if (mmd != nullptr && mmd->Release != nullptr)
                {
                    mmd->Release(mod, mesh.model);
                }
            }

            for (auto it = g_loaded.begin(); it != g_loaded.end(); ++it)
            {
                if (it->id == id)
                {
                    g_loaded.erase(it);
                    return 1;
                }
            }
            return 1;
        }

        int __cdecl ApiIsValid(AmodMod mod, AmodAzmId id)
        {
            return FindFor(mod, id) != nullptr ? 1 : 0;
        }

        int __cdecl ApiGet(AmodMod mod, AmodAzmId id, AmodAzmModel* out)
        {
            const Loaded* const loaded = FindFor(mod, id);

            if (loaded == nullptr || out == nullptr || out->structSize < sizeof(AmodAzmModel))
            {
                return 0;
            }

            out->name = loaded->name.c_str();
            out->path = loaded->path.c_str();
            out->partCount = static_cast<int>(loaded->parts.size());
            out->materialCount = static_cast<int>(loaded->materials.size());
            out->pointCount = static_cast<int>(loaded->points.size());
            out->boxCount = static_cast<int>(loaded->boxes.size());
            out->vertexCount = loaded->vertexCount;
            out->triangleCount = loaded->triangleCount;
            out->meshCount = static_cast<int>(loaded->meshes.size());
            memcpy(out->boundsMin, loaded->boundsMin, sizeof(out->boundsMin));
            memcpy(out->boundsMax, loaded->boundsMax, sizeof(out->boundsMax));
            out->unitsPerMetre = loaded->units;
            out->forwardAxis = static_cast<int>(loaded->forward);
            out->upAxis = static_cast<int>(loaded->up);
            return 1;
        }

        int __cdecl ApiGetPart(AmodMod mod, AmodAzmId id, int index, AmodAzmPart* out)
        {
            const Loaded* const loaded = FindFor(mod, id);

            if (loaded == nullptr || out == nullptr || out->structSize < sizeof(AmodAzmPart) || index < 0 || index >= static_cast<int>(loaded->parts.size()))
            {
                return 0;
            }

            const Part& part = loaded->parts[static_cast<size_t>(index)];

            out->name = part.name.c_str();
            out->parent = part.parent;
            memcpy(out->pivot, part.pivot, sizeof(out->pivot));
            out->flags = part.flags;
            out->model = part.meshes.empty() ? AMOD_INVALID_MODEL
                                             : loaded->meshes[static_cast<size_t>(part.meshes[0])].model;
            out->blend = part.blend;
            out->hidden = part.hidden ? 1 : 0;
            return 1;
        }

        int __cdecl ApiFindPart(AmodMod mod, AmodAzmId id, const char* name)
        {
            const Loaded* const loaded = FindFor(mod, id);

            for (size_t at = 0; loaded != nullptr && name != nullptr && at < loaded->parts.size(); ++at)
            {
                if (loaded->parts[at].name == name)
                {
                    return static_cast<int>(at);
                }
            }
            return -1;
        }

        int __cdecl ApiGetPoint(AmodMod mod, AmodAzmId id, int index, AmodAzmPoint* out)
        {
            const Loaded* const loaded = FindFor(mod, id);

            if (loaded == nullptr || out == nullptr || out->structSize < sizeof(AmodAzmPoint) ||
                index < 0 || index >= static_cast<int>(loaded->points.size()))
            {
                return 0;
            }

            const Named& point = loaded->points[static_cast<size_t>(index)];

            out->name = point.name.c_str();
            memcpy(out->position, point.first, sizeof(out->position));
            memcpy(out->direction, point.second, sizeof(out->direction));
            out->part = point.part;
            return 1;
        }

        int __cdecl ApiFindPoint(AmodMod mod, AmodAzmId id, const char* name)
        {
            const Loaded* const loaded = FindFor(mod, id);

            for (size_t at = 0; loaded != nullptr && name != nullptr && at < loaded->points.size(); ++at)
            {
                if (loaded->points[at].name == name)
                {
                    return static_cast<int>(at);
                }
            }
            return -1;
        }

        int __cdecl ApiGetBox(AmodMod mod, AmodAzmId id, int index, AmodAzmBox* out)
        {
            const Loaded* const loaded = FindFor(mod, id);

            if (loaded == nullptr || out == nullptr || out->structSize < sizeof(AmodAzmBox) ||
                index < 0 || index >= static_cast<int>(loaded->boxes.size()))
            {
                return 0;
            }

            const Named& box = loaded->boxes[static_cast<size_t>(index)];

            out->name = box.name.c_str();
            memcpy(out->min, box.first, sizeof(out->min));
            memcpy(out->max, box.second, sizeof(out->max));
            return 1;
        }

        int __cdecl ApiGetMaterial(AmodMod mod, AmodAzmId id, int index, AmodAzmMaterial* out)
        {
            const Loaded* const loaded = FindFor(mod, id);

            if (loaded == nullptr || out == nullptr || out->structSize < sizeof(AmodAzmMaterial) ||
                index < 0 || index >= static_cast<int>(loaded->materials.size()))
            {
                return 0;
            }

            const Material& material = loaded->materials[static_cast<size_t>(index)];

            out->name = material.name.c_str();
            out->texture = material.texture.c_str();
            out->tint = material.tint;
            out->flags = material.flags;
            out->blend = material.blend;
            out->alpha = material.alpha;
            return 1;
        }

        int __cdecl ApiFindMaterial(AmodMod mod, AmodAzmId id, const char* name)
        {
            const Loaded* const loaded = FindFor(mod, id);

            for (size_t at = 0; loaded != nullptr && name != nullptr && at < loaded->materials.size(); ++at)
            {
                if (loaded->materials[at].name == name)
                {
                    return static_cast<int>(at);
                }
            }
            return -1;
        }

        Part* PartFor(AmodMod mod, AmodAzmId id, int index)
        {
            Loaded* const loaded = FindFor(mod, id);
            return (loaded != nullptr && index >= 0 && index < static_cast<int>(loaded->parts.size()))
                ? &loaded->parts[static_cast<size_t>(index)]
                : nullptr;
        }

        int __cdecl ApiSetPartTurn(AmodMod mod, AmodAzmId id, int index, const float* axis, float angle)
        {
            Part* const part = PartFor(mod, id, index);

            if (part == nullptr || axis == nullptr)
            {
                return 0;
            }

            AxisAngle(axis, angle, part->pose);
            part->posed = angle != 0.0f;
            return 1;
        }

        int __cdecl ApiSetPartMatrix(AmodMod mod, AmodAzmId id, int index, const float* matrix)
        {
            Part* const part = PartFor(mod, id, index);

            if (part == nullptr || matrix == nullptr)
            {
                return 0;
            }

            memcpy(part->pose, matrix, sizeof(part->pose));
            part->posed = true;
            return 1;
        }

        int __cdecl ApiSetPartHidden(AmodMod mod, AmodAzmId id, int index, int hidden)
        {
            Part* const part = PartFor(mod, id, index);

            if (part == nullptr)
            {
                return 0;
            }

            part->hidden = hidden != 0;
            return 1;
        }

        int __cdecl ApiClearPose(AmodMod mod, AmodAzmId id)
        {
            Loaded* const loaded = FindFor(mod, id);

            if (loaded == nullptr)
            {
                return 0;
            }

            for (Part& part : loaded->parts)
            {
                part.posed = false;
            }
            return 1;
        }

        int __cdecl ApiPaint(AmodMod mod, AmodAzmId id, int index, uint32_t colour)
        {
            Loaded* const loaded = FindFor(mod, id);
            const AmodMmdApi* const mmd = MmdApi::Table();

            if (loaded == nullptr || mmd == nullptr || mmd->SetColours == nullptr || index < 0 || index >= static_cast<int>(loaded->materials.size()))
            {
                return 0;
            }

            loaded->materials[static_cast<size_t>(index)].paint = colour;

            const auto scale = [](uint32_t was, uint32_t by, int shift)
            {
                const uint32_t left = (was >> shift) & 0xFF;
                const uint32_t right = (by >> shift) & 0xFF;
                return ((left * right) / 255) << shift;
            };

            for (Mesh& mesh : loaded->meshes)
            {
                std::vector<uint32_t> painted = mesh.base;
                bool touched = false;

                for (size_t at = 0; at < painted.size() && at < mesh.owner.size(); ++at)
                {
                    const uint32_t paint = loaded->materials[mesh.owner[at]].paint;

                    if (paint == 0xFFFFFFFF)
                    {
                        continue;
                    }

                    painted[at] = (mesh.base[at] & 0xFF000000) | scale(mesh.base[at], paint, 16) | scale(mesh.base[at], paint, 8) | 
                        scale(mesh.base[at], paint, 0);
                    touched = true;
                }

                if (touched || colour == 0xFFFFFFFF)
                {
                    mmd->SetColours(mod, mesh.model, painted.data(), static_cast<int>(painted.size()));
                }
            }

            return 1;
        }

        int __cdecl ApiQueue(AmodMod mod, AmodAzmId id, const float* matrix)
        {
            const Loaded* const loaded = FindFor(mod, id);

            if (loaded == nullptr)
            {
                return 0;
            }

            float placement[16];

            if (matrix != nullptr)
            {
                memcpy(placement, matrix, sizeof(placement));
            }
            else
            {
                Identity(placement);
            }

            int queued = 0;

            for (size_t at = 0; at < loaded->parts.size(); ++at)
            {
                const Part& part = loaded->parts[at];

                if (part.hidden || (part.flags & AZM_PART_COLLISION) != 0)
                {
                    continue;
                }

                float world[16];
                WorldFor(*loaded, static_cast<int>(at), placement, world);

                for (const int which : part.meshes)
                {
                    AmodModelDraw draw = {};
                    draw.structSize = sizeof(draw);
                    draw.model = loaded->meshes[static_cast<size_t>(which)].model;
                    draw.texture = loaded->meshes[static_cast<size_t>(which)].texture;
                    draw.flags = AMOD_DRAW_HAS_MATRIX;
                    draw.blend = part.blend;

                    draw.tint = part.tint != 0xFFFFFFFFu ? part.tint : loaded->tint;
                    memcpy(draw.matrix, world, sizeof(draw.matrix));

                    g_queue.push_back(draw);
                    ++queued;
                }
            }

            return queued;
        }

        int __cdecl ApiFlush(AmodMod mod)
        {
            const AmodMmdApi* const mmd = MmdApi::Table();

            if (Caller(mod) == nullptr || mmd == nullptr || mmd->DrawMany == nullptr || g_queue.empty())
            {
                g_queue.clear();
                return 0;
            }

            const int drawn = mmd->DrawMany(mod, g_queue.data(), static_cast<int>(g_queue.size()));
            g_queue.clear();
            return drawn;
        }

        int __cdecl ApiDraw(AmodMod mod, AmodAzmId id, const float* matrix)
        {
            return ApiQueue(mod, id, matrix) > 0 ? ApiFlush(mod) : 0;
        }

        int __cdecl ApiQueued(AmodMod mod)
        {
            return Caller(mod) != nullptr ? static_cast<int>(g_queue.size()) : 0;
        }

        int __cdecl ApiSetTint(AmodMod mod, AmodAzmId id, uint32_t colour)
        {
            Loaded* const loaded = FindFor(mod, id);

            if (loaded == nullptr)
            {
                return 0;
            }

            loaded->tint = colour;
            return 1;
        }

        int __cdecl ApiSetPartTint(AmodMod mod, AmodAzmId id, int index, uint32_t colour)
        {
            Part* const part = PartFor(mod, id, index);

            if (part == nullptr)
            {
                return 0;
            }

            part->tint = colour;
            return 1;
        }

        int __cdecl ApiClearTints(AmodMod mod, AmodAzmId id)
        {
            Loaded* const loaded = FindFor(mod, id);

            if (loaded == nullptr)
            {
                return 0;
            }

            loaded->tint = 0xFFFFFFFFu;

            for (Part& part : loaded->parts)
            {
                part.tint = 0xFFFFFFFFu;
            }
            return 1;
        }
    }

    bool AzmApi::Install()
    {
        g_table = {};
        g_table.structSize = sizeof(g_table);

        g_table.Load = &ApiLoad;
        g_table.Release = &ApiRelease;
        g_table.IsValid = &ApiIsValid;
        g_table.Open = &ApiOpen;

        g_table.Get = &ApiGet;
        g_table.GetPart = &ApiGetPart;
        g_table.FindPart = &ApiFindPart;
        g_table.GetPoint = &ApiGetPoint;
        g_table.FindPoint = &ApiFindPoint;
        g_table.GetBox = &ApiGetBox;
        g_table.GetMaterial = &ApiGetMaterial;
        g_table.FindMaterial = &ApiFindMaterial;

        g_table.SetPartTurn = &ApiSetPartTurn;
        g_table.SetPartMatrix = &ApiSetPartMatrix;
        g_table.SetPartHidden = &ApiSetPartHidden;
        g_table.ClearPose = &ApiClearPose;
        g_table.Paint = &ApiPaint;

        g_table.Queue = &ApiQueue;
        g_table.Flush = &ApiFlush;
        g_table.Draw = &ApiDraw;
        g_table.Queued = &ApiQueued;

        g_table.SetTint = &ApiSetTint;
        g_table.SetPartTint = &ApiSetPartTint;
        g_table.ClearTints = &ApiClearTints;

        return true;
    }

    void AzmApi::Uninstall()
    {
        g_loaded.clear();
        g_queue.clear();
    }

    const AmodAzmApi* AzmApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    void AzmApi::ForgetMod(ModContext* owner)
    {
        for (auto it = g_loaded.begin(); it != g_loaded.end();)
        {
            it = it->owner == owner ? g_loaded.erase(it) : it + 1;
        }

        g_queue.clear();
    }
}
