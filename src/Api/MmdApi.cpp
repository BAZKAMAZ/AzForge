#include <windows.h>

#include "src/Api/MmdApi.h"
#include "src/Api/ApiProperty.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Api/TextureApi.h"
#include "src/Api/WorldApi.h"
#include "src/Core/ForgeLog.h"
#include "src/Core/ForgePaths.h"
#include "src/Core/ForgeText.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameFunctions.h"
#include "src/Game/GameModule.h"
#include "src/Game/GameSymbols.h"
#include "src/Hook/InlineHook.h"

#include <ctype.h>
#include <filesystem>
#include <fstream>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;
        using ApiProperty::Field;
        using ApiProperty::Kind;

        AmodMmdApi g_table = {};

        typedef uint8_t* (__cdecl* LoadOrFindRoutine)(const char* path);
        typedef void(__cdecl* ReleaseModelRoutine)(uint8_t* model);
        typedef uint8_t* (__cdecl* PackageOpenRoutine)(const char* path);
        typedef void(__cdecl* PackageFreeRoutine)(uint8_t* handle);
        typedef void(__fastcall* VerticesRenderRoutine)(uint8_t* record, void* unused, void* device);
        typedef char(__fastcall* VerticesUploadRoutine)(uint8_t* record);
        typedef uint8_t* (__cdecl* CreateFromMeshRoutine)(int unused, const void* mesh);
        typedef char(__stdcall* BuildFromMeshRoutine)(uint8_t* model);
        typedef void(__cdecl* RenderRoutine)(uint8_t* model);
        typedef void(__cdecl* RenderAnimatedRoutine)(uint8_t* model, int from, int to, float blend);
        typedef uint8_t* (__cdecl* MeshCreateRoutine)(void);
        typedef void(__cdecl* MeshFreeRoutine)(uint8_t* mesh);
        typedef uint8_t* (__cdecl* MeshLoadRoutine)(const char* path);
        typedef void(__stdcall* MeshComputeRoutine)(uint8_t* mesh, int frame);
        typedef char(__stdcall* MeshWorkingCopyRoutine)(uint8_t* mesh);
        typedef void(__thiscall* ReleaseVerticesRoutine)(void* device, void* vertices);
        typedef void(__cdecl* SceneAddModelRoutine)(const void* item);
        typedef void(__fastcall* SceneDrawModelsRoutine)(void* scene, void* unused);
        typedef uint8_t* (__cdecl* LoadOrFindAnimRoutine)(const char* path);
        typedef void(__cdecl* AnimReleaseRoutine)(uint8_t* animation);
        typedef uint32_t(__cdecl* ResolveRoutine)(const char* name);
        typedef void(__cdecl* AnimPlayRoutine)(uint8_t* animation, const char* frameset, uint8_t* state, const float* position, uint32_t flags);

        template <typename T> T Routine(uintptr_t address)
        {
            return reinterpret_cast<T>(GameModule::Resolve(address));
        }

        InlineHook g_loadHook;
        InlineHook g_releaseHook;
        InlineHook g_drawHook;
        InlineHook g_loadAnimHook;
        LoadOrFindRoutine g_originalLoad = nullptr;
        ReleaseModelRoutine g_originalRelease = nullptr;
        SceneDrawModelsRoutine g_originalDraw = nullptr;
        LoadOrFindAnimRoutine g_originalLoadAnim = nullptr;
        typedef void(__stdcall* DrawNodeRoutine)(uint8_t* node);
        DrawNodeRoutine g_originalDrawNode = nullptr;
        InlineHook g_drawNodeHook;

        RenderAnimatedRoutine g_originalRenderAnimated = nullptr;
        InlineHook g_renderAnimHook;
        InlineHook g_verticesHook;
        VerticesRenderRoutine g_originalVerticesRender = nullptr;

        void WriteInt(uint8_t* record, uintptr_t offset, int value)
        {
            memcpy(record + offset, &value, sizeof(value));
        }

        void WritePointer(uint8_t* record, uintptr_t offset, const void* value)
        {
            const uintptr_t written = reinterpret_cast<uintptr_t>(value);
            memcpy(record + offset, &written, sizeof(written));
        }

        uint8_t* ListHead()
        {
            uint8_t** const head = static_cast<uint8_t**>(GameModule::Resolve(GameAddress::kModelListHead));
            return head != nullptr ? *head : nullptr;
        }

        bool IsModel(const uint8_t* model)
        {
            return model != nullptr && Readable(model, GameAddress::kModelSize) &&
                Read<uint32_t>(model, GameAddress::kModelMagicOffset) == GameAddress::kModelMagic;
        }

        uint8_t* NextModel(const uint8_t* model)
        {
            return reinterpret_cast<uint8_t*>(Read<uintptr_t>(model, GameAddress::kModelNextOffset));
        }

        // Cap walk so a corrupted ring ends.
        constexpr int kModelWalkLimit = 0x4000;

        bool Listed(const uint8_t* model)
        {
            int seen = 0;
            for (uint8_t* walk = ListHead(); IsModel(walk) && seen < kModelWalkLimit;
                walk = NextModel(walk), ++seen)
            {
                if (walk == model)
                {
                    return true;
                }
            }
            return false;
        }

        uint8_t* ModelMesh(const uint8_t* model)
        {
            uint8_t* const mesh = reinterpret_cast<uint8_t*>(Read<uintptr_t>(model, GameAddress::kModelMeshOffset));
            return (mesh != nullptr && Readable(mesh, GameAddress::kMeshSize)) ? mesh : nullptr;
        }

        uint8_t* MeshFrame(const uint8_t* mesh, int frame)
        {
            uint8_t* const frames = reinterpret_cast<uint8_t*>(Read<uintptr_t>(mesh, GameAddress::kMeshFramesOffset));
            const int count = Read<int>(mesh, GameAddress::kMeshFrameCountOffset);

            if (frames == nullptr || frame < 0 || frame >= count)
            {
                return nullptr;
            }

            uint8_t* const at = frames + static_cast<size_t>(frame) * GameAddress::kMeshFrameStride;
            return Readable(at, GameAddress::kMeshFrameStride) ? at : nullptr;
        }

        uint8_t* FrameVertices(const uint8_t* model, int frame, int& count);

        void RepaintAll(const uint8_t* model);
        void ForgetPaint(const uint8_t* model);
        void* RenderDevice();
        void CarryOverDrawings(const uint8_t* was, uint8_t* fresh);

        struct Geometry
        {
            int frameCount = 0;
            int vertexCount = 0;
            int indexCount = 0;
            int primitiveCount = 0;
            std::vector<uint8_t> vertices;      // frameCount * vertexCount * kMeshVertexSize
            std::vector<uint16_t> indices;      // frameCount * indexCount

            bool Sane() const
            {
                return frameCount > 0 && frameCount < 0x4000 && vertexCount > 0 &&
                    vertexCount <= 0xFFFF && indexCount > 0 && indexCount < 0x100000 &&
                    vertices.size() == static_cast<size_t>(frameCount) * vertexCount *
                    GameAddress::kMeshVertexSize && indices.size() == static_cast<size_t>(frameCount) * indexCount;
            }
        };

        struct FileGeometry
        {
            const uint8_t* model = nullptr;
            Geometry geometry;
        };

        FileGeometry g_fileCache;

        void ForgetFileGeometry(const uint8_t* model)
        {
            if (model == nullptr || g_fileCache.model == model)
            {
                g_fileCache.model = nullptr;
                g_fileCache.geometry = Geometry{};
            }
        }

        bool ReadGeometryFromMesh(const uint8_t* mesh, Geometry& out);

        const Geometry* FileGeometryOf(const uint8_t* model)
        {
            if (g_fileCache.model == model && g_fileCache.geometry.Sane())
            {
                return &g_fileCache.geometry;
            }

            const MeshLoadRoutine meshLoad = Routine<MeshLoadRoutine>(GameAddress::kMeshLoadFile);
            const MeshFreeRoutine meshFree = Routine<MeshFreeRoutine>(GameAddress::kMeshFree);

            if (meshLoad == nullptr || meshFree == nullptr || !NameAt(model, GameAddress::kModelNameOffset))
            {
                return nullptr;
            }

            uint8_t* const mesh = meshLoad(ReadText(model, GameAddress::kModelNameOffset));
            if (mesh == nullptr)
            {
                return nullptr;
            }

            Geometry read;
            const bool ok = ReadGeometryFromMesh(mesh, read);
            meshFree(mesh);

            if (!ok)
            {
                return nullptr;
            }

            g_fileCache.model = model;
            g_fileCache.geometry = read;
            return &g_fileCache.geometry;
        }

        bool ReadGeometryFromMesh(const uint8_t* mesh, Geometry& out)
        {
            const uint8_t* const first = mesh != nullptr ? MeshFrame(mesh, 0) : nullptr;
            if (first == nullptr)
            {
                return false;
            }

            out = Geometry{};
            out.frameCount = Read<int>(mesh, GameAddress::kMeshFrameCountOffset);
            out.vertexCount = Read<int>(first, GameAddress::kMeshFrameVertexCountOffset);
            out.indexCount = Read<int>(first, GameAddress::kMeshFrameIndexCountOffset);
            out.primitiveCount = Read<int>(first, GameAddress::kMeshFramePrimitiveCountOffset);

            if (out.frameCount < 1 || out.frameCount >= 0x4000 || out.vertexCount < 1 ||
                out.vertexCount > 0xFFFF || out.indexCount < 1 || out.indexCount >= 0x100000)
            {
                return false;
            }

            const size_t vertexBytes = static_cast<size_t>(out.vertexCount) * GameAddress::kMeshVertexSize;

            out.vertices.resize(static_cast<size_t>(out.frameCount) * vertexBytes);
            out.indices.resize(static_cast<size_t>(out.frameCount) * out.indexCount);

            bool complete = out.Sane();
            for (int frame = 0; complete && frame < out.frameCount; ++frame)
            {
                const uint8_t* const at = MeshFrame(mesh, frame);
                const void* const source = at != nullptr
                    ? reinterpret_cast<const void*>(Read<uintptr_t>(at, GameAddress::kMeshFrameVerticesOffset))
                    : nullptr;
                const void* const sourceIndices = at != nullptr
                    ? reinterpret_cast<const void*>(Read<uintptr_t>(at, GameAddress::kMeshFrameIndicesOffset))
                    : nullptr;

                complete = source != nullptr && sourceIndices != nullptr &&
                    Readable(source, vertexBytes) && Readable(sourceIndices, static_cast<size_t>(out.indexCount) * 2);

                if (complete)
                {
                    memcpy(out.vertices.data() + static_cast<size_t>(frame) * vertexBytes, source, vertexBytes);
                    memcpy(out.indices.data() + static_cast<size_t>(frame) * out.indexCount, sourceIndices, static_cast<size_t>(out.indexCount) * 2);
                }
            }
            return complete;
        }

        bool ReadGeometry(const uint8_t* model, Geometry& out)
        {
            const uint8_t* const mesh = ModelMesh(model);
            if (mesh != nullptr)
            {
                return ReadGeometryFromMesh(mesh, out);
            }

            const Geometry* const cached = FileGeometryOf(model);
            if (cached == nullptr)
            {
                return false;
            }

            out = *cached;
            return true;
        }

        uint8_t* BuildMesh(const Geometry& geometry)
        {
            const MeshCreateRoutine create = Routine<MeshCreateRoutine>(GameAddress::kMeshCreate);
            const MeshFreeRoutine meshFree = Routine<MeshFreeRoutine>(GameAddress::kMeshFree);
            const MeshComputeRoutine centre = Routine<MeshComputeRoutine>(GameAddress::kMeshComputeCenter);
            const MeshComputeRoutine extents = Routine<MeshComputeRoutine>(GameAddress::kMeshComputeExtents);
            const MeshWorkingCopyRoutine working = Routine<MeshWorkingCopyRoutine>(GameAddress::kMeshMakeWorkingCopy);

            if (create == nullptr || meshFree == nullptr || !geometry.Sane())
            {
                return nullptr;
            }

            uint8_t* const mesh = create();
            if (mesh == nullptr)
            {
                return nullptr;
            }

            uint8_t* const frames = static_cast<uint8_t*>(GameFunctions::Allocate(static_cast<int>(geometry.frameCount * GameAddress::kMeshFrameStride), ".\\Mesh.cpp", 0x1d));

            if (frames == nullptr)
            {
                GameFunctions::Free(mesh);
                return nullptr;
            }

            WritePointer(mesh, GameAddress::kMeshFramesOffset, frames);
            WriteInt(mesh, GameAddress::kMeshFrameCountOffset, geometry.frameCount);

            const size_t vertexBytes = static_cast<size_t>(geometry.vertexCount) * GameAddress::kMeshVertexSize;
            const size_t indexBytes = static_cast<size_t>(geometry.indexCount) * 2;
            const int primitives = geometry.primitiveCount > 0 ? geometry.primitiveCount : geometry.indexCount / 3;

            for (int frame = 0; frame < geometry.frameCount; ++frame)
            {
                uint8_t* const at = frames + static_cast<size_t>(frame) * GameAddress::kMeshFrameStride;

                void* const vertices = GameFunctions::Allocate(static_cast<int>(vertexBytes), ".\\Mesh.cpp", 0x31);
                void* const indices = GameFunctions::Allocate(static_cast<int>(indexBytes), ".\\Mesh.cpp", 0x35);

                if (vertices == nullptr || indices == nullptr)
                {
                    WritePointer(at, GameAddress::kMeshFrameVerticesOffset, vertices);
                    WritePointer(at, GameAddress::kMeshFrameIndicesOffset, indices);
                    meshFree(mesh);
                    return nullptr;
                }

                memcpy(vertices, geometry.vertices.data() + static_cast<size_t>(frame) * vertexBytes, vertexBytes);
                memcpy(indices, geometry.indices.data() + static_cast<size_t>(frame) * geometry.indexCount, indexBytes);

                WritePointer(at, GameAddress::kMeshFrameVerticesOffset, vertices);
                WriteInt(at, GameAddress::kMeshFrameVertexCountOffset, geometry.vertexCount);
                WritePointer(at, GameAddress::kMeshFrameIndicesOffset, indices);
                WriteInt(at, GameAddress::kMeshFrameIndexCountOffset, geometry.indexCount);
                WriteInt(at, GameAddress::kMeshFramePrimitiveCountOffset, primitives);
            }

            if (centre != nullptr)
            {
                centre(mesh, 0);
            }
            if (extents != nullptr)
            {
                extents(mesh, 0);
            }

            if (geometry.frameCount > 1 && working != nullptr)
            {
                working(mesh);
            }
            return mesh;
        }

        void ReleaseVertexBuffer(uint8_t* model)
        {
            void** const at = static_cast<void**>(GameModule::Resolve(GameAddress::kRenderDevice));
            void* const device = at != nullptr ? *at : nullptr;
            void* const vertices = reinterpret_cast<void*>(Read<uintptr_t>(model, GameAddress::kModelVertexBufferOffset));

            if (device != nullptr && vertices != nullptr)
            {
                void** const vtable = *static_cast<void***>(device);
                const auto release = reinterpret_cast<ReleaseVerticesRoutine>(vtable[GameAddress::kRenderDeviceReleaseVertices / sizeof(void*)]);

                release(device, vertices);
            }
            WritePointer(model, GameAddress::kModelVertexBufferOffset, nullptr);
        }

        bool InstallMesh(uint8_t* model, uint8_t* mesh)
        {
            const BuildFromMeshRoutine build = Routine<BuildFromMeshRoutine>(GameAddress::kModelBuildFromMesh);
            const MeshFreeRoutine meshFree = Routine<MeshFreeRoutine>(GameAddress::kMeshFree);

            if (build == nullptr || meshFree == nullptr || mesh == nullptr)
            {
                return false;
            }

            ForgetFileGeometry(model);
            ReleaseVertexBuffer(model);

            uint8_t* const previous = ModelMesh(model);
            if (previous != nullptr && previous != mesh)
            {
                meshFree(previous);
            }

            WritePointer(model, GameAddress::kModelMeshOffset, mesh);
            WriteInt(model, GameAddress::kModelKindOffset, Read<int>(mesh, GameAddress::kMeshFrameCountOffset) > 1 ? GameAddress::kModelKindAnimated : GameAddress::kModelKindMesh);

            if (build(model) == 0)
            {
                return false;
            }

            // Fresh buffer is white; reapply paint.
            RepaintAll(model);
            return true;
        }

        constexpr uint32_t kMaterialTag = 0x4C544D41; // 'AMTL', little end first

        struct Material
        {
            int firstIndex = 0;
            int primitiveCount = 0;
            uint32_t texture = 0;
        };

        struct Tracked
        {
            uint32_t id = 0;
            uint8_t* model = nullptr;

            std::vector<Material> materials;
            ModContext* materialsOwner = nullptr;
            ModContext* creator = nullptr;

            std::string name;
            bool resident = true;

            uint32_t validatedAt = 0;

            bool snapshotTaken = false;
            ModContext* editor = nullptr;
            int wasKind = 0;
            float wasCentre[3] = {};
            float wasExtents[3] = {};

            bool fromFile = false;
            Geometry snapshot;
        };

        void ReadMaterialsFromFile(const char* path, Tracked& tracked);

        struct Held
        {
            ModContext* owner;
            uint32_t id;
        };

        struct Substitute
        {
            ModContext* owner;
            std::string path;
            uint32_t id;
        };

        struct RoomSwap
        {
            ModContext* owner;
            int roomIndex;
            int slot;
            uint8_t* room;
            uintptr_t was;
            uintptr_t applied;
        };

        uint32_t g_listGeneration = 1;

        struct WalkMemo
        {
            uint32_t generation = 0;
            int index = -1;
            uint8_t* model = nullptr;
        };

        WalkMemo g_walk;

        std::vector<Tracked> g_tracked;
        std::vector<Held> g_held;
        std::vector<Substitute> g_substitutes;
        std::vector<RoomSwap> g_roomSwaps;
        uint32_t g_nextId = 1;

        Tracked* TrackedOf(const uint8_t* model)
        {
            for (Tracked& entry : g_tracked)
            {
                if (entry.model == model && entry.resident)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        Tracked* TrackedById(AmodModelId id)
        {
            for (Tracked& entry : g_tracked)
            {
                if (entry.id != id)
                {
                    continue;
                }

                if (!entry.resident)
                {
                    return nullptr;
                }

                if (entry.validatedAt == g_listGeneration && entry.model != nullptr)
                {
                    return &entry;
                }

                if (entry.model != nullptr && IsModel(entry.model) && Listed(entry.model))
                {
                    entry.validatedAt = g_listGeneration;
                    return &entry;
                }
                return nullptr;
            }
            return nullptr;
        }

        AmodModelId Track(uint8_t* model, ModContext* creator)
        {
            if (!IsModel(model))
            {
                return AMOD_INVALID_MODEL;
            }

            Tracked* existing = TrackedOf(model);
            if (existing != nullptr)
            {
                return existing->id;
            }

            Tracked entry;
            entry.id = g_nextId++;
            entry.model = model;
            entry.creator = creator;
            entry.name = NameAt(model, GameAddress::kModelNameOffset) ? ReadText(model, GameAddress::kModelNameOffset) : "";
            g_tracked.push_back(entry);
            return entry.id;
        }

        void Forget(const uint8_t* model)
        {
            uint32_t id = AMOD_INVALID_MODEL;
            bool waiting = false;

            for (auto it = g_tracked.begin(); it != g_tracked.end();)
            {
                if (it->model != model)
                {
                    ++it;
                    continue;
                }

                id = it->id;

                // Named models may return; mesh-built ones are erased.
                if (it->name.empty())
                {
                    it = g_tracked.erase(it);
                    continue;
                }

                it->resident = false;
                it->validatedAt = 0;
                waiting = true;
                ++it;
            }

            ForgetFileGeometry(model);

            // Keep paint with id until the model returns.
            if (!waiting)
            {
                ForgetPaint(model);
            }

            if (id == AMOD_INVALID_MODEL)
            {
                return;
            }

            for (auto it = g_held.begin(); it != g_held.end();)
            {
                it = it->id == id ? g_held.erase(it) : ++it;
            }
            for (auto it = g_substitutes.begin(); it != g_substitutes.end();)
            {
                it = it->id == id ? g_substitutes.erase(it) : ++it;
            }
            for (auto it = g_roomSwaps.begin(); it != g_roomSwaps.end();)
            {
                it = it->applied == reinterpret_cast<uintptr_t>(model) ? g_roomSwaps.erase(it) : ++it;
            }
        }

        std::string Normalised(const char* path)
        {
            std::string text = path != nullptr ? path : "";
            for (char& letter : text)
            {
                letter = letter == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(letter)));
            }
            return text;
        }

        uint8_t* SubstituteFor(const char* path)
        {
            const std::string wanted = Normalised(path);
            if (wanted.empty())
            {
                return nullptr;
            }

            for (const Substitute& entry : g_substitutes)
            {
                if (entry.path != wanted)
                {
                    continue;
                }

                const Tracked* const tracked = TrackedById(entry.id);
                if (tracked != nullptr)
                {
                    return tracked->model;
                }
            }
            return nullptr;
        }

        // detours

        void CarryOver(uint8_t* fresh)
        {
            if (!IsModel(fresh) || !NameAt(fresh, GameAddress::kModelNameOffset))
            {
                return;
            }

            const std::string name = Normalised(ReadText(fresh, GameAddress::kModelNameOffset));

            for (Tracked& entry : g_tracked)
            {
                if (entry.resident || entry.name.empty() || Normalised(entry.name.c_str()) != name)
                {
                    continue;
                }

                uint8_t* const was = entry.model;

                entry.model = fresh;
                entry.resident = true;
                entry.validatedAt = 0;
                entry.snapshotTaken = false;    // the snapshot belonged to a mesh that is gone
                entry.snapshot = Geometry{};

                // Forward decls; defined further down.
                CarryOverDrawings(was, fresh);

                FORGE_LOG_INFO("'%s' was loaded again and model %u went with it, colours and draw "
                    "handlers and all", entry.name.c_str(), entry.id);
                break;
            }
        }

        uint8_t* __cdecl LoadOrFindDetour(const char* path)
        {
            uint8_t* const standing = SubstituteFor(path);
            if (standing != nullptr)
            {

                WriteInt(standing, GameAddress::kModelReferencesOffset, Read<int>(standing, GameAddress::kModelReferencesOffset) + 1);
                return standing;
            }

            uint8_t* const loaded = g_originalLoad != nullptr ? g_originalLoad(path) : nullptr;
            ++g_listGeneration;

            if (loaded != nullptr)
            {
                CarryOver(loaded);
            }
            return loaded;
        }

        void __cdecl ReleaseDetour(uint8_t* model)
        {
            if (IsModel(model) && Read<int>(model, GameAddress::kModelReferencesOffset) == 0)
            {
                Forget(model);
            }
            if (g_originalRelease != nullptr)
            {
                g_originalRelease(model);
            }
            ++g_listGeneration;
        }

        bool InstallHooks()
        {
            void* const load = GameSymbols::Address(GameSymbol::LoadOrFindModel);
            void* const release = GameSymbols::Address(GameSymbol::ModelRelease);
            if (load == nullptr || release == nullptr)
            {
                return false;
            }

            std::string error;
            if (!g_loadHook.Install(load, reinterpret_cast<void*>(&LoadOrFindDetour), kLoadOrFindModelPrologue, sizeof(kLoadOrFindModelPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook LoadOrFindModel: %s", error.c_str());
                return false;
            }

            if (!g_releaseHook.Install(release, reinterpret_cast<void*>(&ReleaseDetour), kModelReleasePrologue, sizeof(kModelReleasePrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Model_Release: %s", error.c_str());
                g_loadHook.Uninstall();
                return false;
            }

            g_originalLoad = reinterpret_cast<LoadOrFindRoutine>(g_loadHook.Trampoline());
            g_originalRelease = reinterpret_cast<ReleaseModelRoutine>(g_releaseHook.Trampoline());
            return true;
        }

        // animations

        struct TrackedAnim
        {
            uint32_t id = 0;
            uint8_t* animation = nullptr;
        };

        std::vector<TrackedAnim> g_animations;
        std::vector<Held> g_heldAnims;
        std::vector<Substitute> g_animSubstitutes;

        uint8_t* AnimListHead()
        {
            uint8_t** const head = static_cast<uint8_t**>(GameModule::Resolve(GameAddress::kAnimListHead));
            return head != nullptr ? *head : nullptr;
        }

        bool IsAnimation(const uint8_t* animation)
        {
            return animation != nullptr &&
                Readable(animation, GameAddress::kAnimReferencesOffset + sizeof(int)) && Read<uint32_t>(animation, 0) == GameAddress::kAnimMagic;
        }

        uint8_t* NextAnim(const uint8_t* animation)
        {
            return reinterpret_cast<uint8_t*>(Read<uintptr_t>(animation, GameAddress::kAnimNextOffset));
        }

        bool AnimListed(const uint8_t* animation)
        {
            int seen = 0;
            for (uint8_t* walk = AnimListHead(); IsAnimation(walk) && seen < kModelWalkLimit;
                walk = NextAnim(walk), ++seen)
            {
                if (walk == animation)
                {
                    return true;
                }
            }
            return false;
        }

        TrackedAnim* AnimById(AmodAnimationId id)
        {
            for (TrackedAnim& entry : g_animations)
            {
                if (entry.id == id)
                {
                    return AnimListed(entry.animation) ? &entry : nullptr;
                }
            }
            return nullptr;
        }

        AmodAnimationId TrackAnim(uint8_t* animation)
        {
            if (!IsAnimation(animation) || !AnimListed(animation))
            {
                return AMOD_INVALID_ANIMATION;
            }

            for (TrackedAnim& entry : g_animations)
            {
                if (entry.animation == animation)
                {
                    return entry.id;
                }
            }

            TrackedAnim entry;
            entry.id = g_nextId++;
            entry.animation = animation;
            g_animations.push_back(entry);
            return entry.id;
        }

        uint8_t* AnimSubstituteFor(const char* path)
        {
            const std::string wanted = Normalised(path);
            if (wanted.empty())
            {
                return nullptr;
            }

            for (const Substitute& entry : g_animSubstitutes)
            {
                if (entry.path == wanted)
                {
                    const TrackedAnim* const tracked = AnimById(entry.id);
                    if (tracked != nullptr)
                    {
                        return tracked->animation;
                    }
                }
            }
            return nullptr;
        }

        uint8_t* __cdecl LoadOrFindAnimDetour(const char* path)
        {
            uint8_t* const standing = AnimSubstituteFor(path);
            if (standing != nullptr)
            {
                WriteInt(standing, GameAddress::kAnimReferencesOffset, Read<int>(standing, GameAddress::kAnimReferencesOffset) + 1);
                return standing;
            }
            return g_originalLoadAnim != nullptr ? g_originalLoadAnim(path) : nullptr;
        }

        struct QueuedDraw
        {
            ModContext* owner;
            int blend;
            uint8_t item[GameAddress::kDrawItemSize];
        };

        std::vector<QueuedDraw> g_queued;

        uint8_t* Scene()
        {
            return static_cast<uint8_t*>(GameModule::Resolve(GameAddress::kScene));
        }

        // The four the game parsed out of scripts/rs at start-up and kept side by side
        uint32_t StateFor(const uint8_t* scene, int blend)
        {
            const uintptr_t offsets[] =
            {
                GameAddress::kSceneStateSolidOffset,
                GameAddress::kSceneStateBlendOffset,
                GameAddress::kSceneStateAddOffset,
                GameAddress::kSceneStateMulOffset,
            };

            return (scene != nullptr && blend >= 0 && blend < static_cast<int>(std::size(offsets)) && Readable(scene, offsets[blend] + sizeof(uint32_t)))
                ? Read<uint32_t>(scene, offsets[blend])
                : 0;
        }

        int QueueUsed()
        {
            const uint8_t* const scene = Scene();
            const int used = scene != nullptr
                ? Read<int>(scene, GameAddress::kSceneDrawCountOffset)
                : static_cast<int>(GameAddress::kSceneDrawCapacity);

            return (used < 0 || used > static_cast<int>(GameAddress::kSceneDrawCapacity))
                ? static_cast<int>(GameAddress::kSceneDrawCapacity)
                : used;
        }

        void __fastcall SceneDrawModelsDetour(void* scene, void* unused)
        {
            const SceneAddModelRoutine add = Routine<SceneAddModelRoutine>(GameAddress::kSceneAddModel);
            const std::vector<QueuedDraw> queued = g_queued;

            g_queued.clear();

            if (add == nullptr || g_originalDraw == nullptr)
            {
                return;
            }

            uint8_t* const at = static_cast<uint8_t*>(scene);
            uint32_t* const slot = (at != nullptr && Readable(at, GameAddress::kSceneStateSolidOffset + sizeof(uint32_t)))
                ? reinterpret_cast<uint32_t*>(at + GameAddress::kSceneStateSolidOffset)
                : nullptr;
            const uint32_t solid = slot != nullptr ? *slot : 0;

            for (int blend = AMOD_DRAW_BLEND_NONE; blend <= AMOD_DRAW_BLEND_MUL; ++blend)
            {
                int taken = 0;

                for (const QueuedDraw& one : queued)
                {
                    if (one.blend == blend)
                    {
                        add(one.item);
                        ++taken;
                    }
                }

                // The first pass carries the game's own draws as well, so it runs either way
                if (taken == 0 && blend != AMOD_DRAW_BLEND_NONE)
                {
                    continue;
                }

                const uint32_t state = StateFor(at, blend);

                if (slot != nullptr && state != 0)
                {
                    *slot = state;
                }

                g_originalDraw(scene, unused);

                if (slot != nullptr)
                {
                    *slot = solid;
                }
            }
        }

        bool InstallDrawHooks()
        {
            void* const draw = GameSymbols::Address(GameSymbol::SceneDrawModels);
            void* const loadAnim = GameSymbols::Address(GameSymbol::LoadOrFindAnim);
            if (draw == nullptr || loadAnim == nullptr)
            {
                return false;
            }

            std::string error;
            if (!g_drawHook.Install(draw, reinterpret_cast<void*>(&SceneDrawModelsDetour), kSceneDrawModelsPrologue, sizeof(kSceneDrawModelsPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Scene_DrawModels: %s", error.c_str());
                return false;
            }

            if (!g_loadAnimHook.Install(loadAnim, reinterpret_cast<void*>(&LoadOrFindAnimDetour),
                kLoadOrFindAnimPrologue, sizeof(kLoadOrFindAnimPrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook LoadOrFindAnim: %s", error.c_str());
                g_drawHook.Uninstall();
                return false;
            }

            g_originalDraw = reinterpret_cast<SceneDrawModelsRoutine>(g_drawHook.Trampoline());
            g_originalLoadAnim = reinterpret_cast<LoadOrFindAnimRoutine>(g_loadAnimHook.Trampoline());
            return true;
        }

        // snapshots

        bool FileHoldsTheOriginal(const uint8_t* model)
        {
            const MeshLoadRoutine meshLoad = Routine<MeshLoadRoutine>(GameAddress::kMeshLoadFile);
            const MeshFreeRoutine meshFree = Routine<MeshFreeRoutine>(GameAddress::kMeshFree);

            if (meshLoad == nullptr || meshFree == nullptr || !NameAt(model, GameAddress::kModelNameOffset))
            {
                return false;
            }

            uint8_t* const mesh = meshLoad(ReadText(model, GameAddress::kModelNameOffset));
            if (mesh == nullptr)
            {
                return false;
            }

            const bool usable = MeshFrame(mesh, 0) != nullptr;
            meshFree(mesh);
            return usable;
        }

        bool EnsureSnapshot(Tracked& tracked, ModContext* who)
        {
            if (tracked.creator != nullptr || tracked.snapshotTaken)
            {
                return true;
            }

            tracked.fromFile = FileHoldsTheOriginal(tracked.model);

            if (!tracked.fromFile && !ReadGeometry(tracked.model, tracked.snapshot))
            {
                return false;
            }

            tracked.wasKind = Read<int>(tracked.model, GameAddress::kModelKindOffset);
            memcpy(tracked.wasCentre, tracked.model + GameAddress::kModelCentreOffset, sizeof(tracked.wasCentre));
            memcpy(tracked.wasExtents, tracked.model + GameAddress::kModelExtentsOffset, sizeof(tracked.wasExtents));

            tracked.snapshotTaken = true;
            tracked.editor = who;
            return true;
        }

        bool RestoreSnapshot(Tracked& tracked)
        {
            if (!tracked.snapshotTaken)
            {
                return true;
            }

            if (!IsModel(tracked.model) || !Listed(tracked.model))
            {
                tracked.snapshotTaken = false;
                tracked.snapshot = Geometry{};
                return false;
            }

            const MeshLoadRoutine meshLoad = Routine<MeshLoadRoutine>(GameAddress::kMeshLoadFile);
            uint8_t* const mesh = tracked.fromFile
                ? (meshLoad != nullptr
                    ? meshLoad(ReadText(tracked.model, GameAddress::kModelNameOffset))
                    : nullptr)
                : BuildMesh(tracked.snapshot);

            if (mesh == nullptr || !InstallMesh(tracked.model, mesh))
            {
                return false;
            }

            if (tracked.wasKind == GameAddress::kModelKindStatic)
            {
                const MeshFreeRoutine meshFree = Routine<MeshFreeRoutine>(GameAddress::kMeshFree);
                if (meshFree != nullptr)
                {
                    meshFree(mesh);
                }
                WritePointer(tracked.model, GameAddress::kModelMeshOffset, nullptr);
            }
            WriteInt(tracked.model, GameAddress::kModelKindOffset, tracked.wasKind);

            memcpy(tracked.model + GameAddress::kModelCentreOffset, tracked.wasCentre, sizeof(tracked.wasCentre));
            memcpy(tracked.model + GameAddress::kModelExtentsOffset, tracked.wasExtents, sizeof(tracked.wasExtents));

            tracked.snapshotTaken = false;
            tracked.editor = nullptr;
            tracked.fromFile = false;
            tracked.snapshot = Geometry{};
            return true;
        }

        // reading a model out

        int IndexCountOf(const uint8_t* model)
        {
            const uint8_t* const mesh = ModelMesh(model);
            const uint8_t* const first = mesh != nullptr ? MeshFrame(mesh, 0) : nullptr;

            return first != nullptr
                ? Read<int>(first, GameAddress::kMeshFrameIndexCountOffset)
                : Read<int>(model, GameAddress::kModelPrimitiveCountOffset) * 3;
        }

        int FrameCountOf(const uint8_t* model)
        {
            const uint8_t* const mesh = ModelMesh(model);
            return mesh != nullptr ? Read<int>(mesh, GameAddress::kMeshFrameCountOffset) : 1;
        }

        void FillModel(const Tracked& tracked, ModContext* asking, AmodModel& out)
        {
            const uint8_t* const model = tracked.model;

            out = AmodModel{};
            out.structSize = sizeof(AmodModel);
            out.name = NameAt(model, GameAddress::kModelNameOffset)
                ? ReadText(model, GameAddress::kModelNameOffset)
                : "";
            out.kind = Read<int>(model, GameAddress::kModelKindOffset);
            out.frameCount = FrameCountOf(model);
            out.vertexCount = Read<int>(model, GameAddress::kModelVertexCountOffset);
            out.indexCount = IndexCountOf(model);
            out.primitiveCount = Read<int>(model, GameAddress::kModelPrimitiveCountOffset);
            out.references = Read<int>(model, GameAddress::kModelReferencesOffset);
            out.mine = tracked.creator == asking ? 1 : 0;
            out.edited = tracked.snapshotTaken ? 1 : 0;

            for (int axis = 0; axis < 3; ++axis)
            {
                const uintptr_t step = static_cast<uintptr_t>(axis) * sizeof(float);
                out.centre[axis] = Read<float>(model, GameAddress::kModelCentreOffset + step);
                out.extents[axis] = Read<float>(model, GameAddress::kModelExtentsOffset + step);
            }
        }

        // the table

        int __cdecl ApiGetCount(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            int total = 0;
            for (uint8_t* walk = ListHead(); IsModel(walk) && total < kModelWalkLimit;
                walk = NextModel(walk))
            {
                ++total;
            }
            return total;
        }

        AmodModelId __cdecl ApiGetAt(AmodMod mod, int index)
        {
            if (Caller(mod) == nullptr || index < 0)
            {
                return AMOD_INVALID_MODEL;
            }

            uint8_t* walk = ListHead();
            int at = 0;

            if (g_walk.generation == g_listGeneration && g_walk.index >= 0 && g_walk.index <= index && g_walk.model != nullptr && IsModel(g_walk.model))
            {
                walk = g_walk.model;
                at = g_walk.index;
            }

            for (int steps = 0; IsModel(walk) && steps < kModelWalkLimit;
                walk = NextModel(walk), ++at, ++steps)
            {
                if (at == index)
                {
                    g_walk.generation = g_listGeneration;
                    g_walk.index = at;
                    g_walk.model = walk;
                    return Track(walk, nullptr);
                }
            }
            return AMOD_INVALID_MODEL;
        }

        AmodModelId __cdecl ApiFind(AmodMod mod, const char* name)
        {
            const std::string wanted = Normalised(name);
            if (Caller(mod) == nullptr || wanted.empty())
            {
                return AMOD_INVALID_MODEL;
            }

            int seen = 0;
            for (uint8_t* walk = ListHead(); IsModel(walk) && seen < kModelWalkLimit;
                walk = NextModel(walk), ++seen)
            {
                if (NameAt(walk, GameAddress::kModelNameOffset) && Normalised(ReadText(walk, GameAddress::kModelNameOffset)) == wanted)
                {
                    return Track(walk, nullptr);
                }
            }
            return AMOD_INVALID_MODEL;
        }

        int __cdecl ApiGet(AmodMod mod, AmodModelId model, AmodModel* out)
        {
            ModContext* const context = Caller(mod);
            const Tracked* const tracked = context != nullptr ? TrackedById(model) : nullptr;
            if (tracked == nullptr || out == nullptr)
            {
                return 0;
            }

            AmodModel filled;
            FillModel(*tracked, context, filled);
            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiIsValid(AmodMod mod, AmodModelId model)
        {
            return (Caller(mod) != nullptr && TrackedById(model) != nullptr) ? 1 : 0;
        }

        AmodModelId __cdecl ApiLoad(AmodMod mod, const char* path)
        {
            ModContext* const context = Caller(mod);
            const LoadOrFindRoutine load = Routine<LoadOrFindRoutine>(GameAddress::kLoadOrFindModel);

            if (context == nullptr || load == nullptr || path == nullptr || path[0] == '\0')
            {
                return AMOD_INVALID_MODEL;
            }

            uint8_t* const model = load(path);
            const AmodModelId id = Track(model, nullptr);
            if (id == AMOD_INVALID_MODEL)
            {
                return AMOD_INVALID_MODEL;
            }

            Tracked* const tracked = TrackedById(id);
            if (tracked != nullptr && tracked->materials.empty())
            {
                ReadMaterialsFromFile(path, *tracked);
            }

            Held held;
            held.owner = context;
            held.id = id;
            g_held.push_back(held);
            return id;
        }

        int __cdecl ApiRelease(AmodMod mod, AmodModelId model)
        {
            ModContext* const context = Caller(mod);
            const Tracked* const tracked = context != nullptr ? TrackedById(model) : nullptr;
            const ReleaseModelRoutine release = g_originalRelease != nullptr ? g_originalRelease
                : Routine<ReleaseModelRoutine>(GameAddress::kModelRelease);

            if (tracked == nullptr || release == nullptr)
            {
                return 0;
            }

            for (auto it = g_held.begin(); it != g_held.end(); ++it)
            {
                if (it->owner == context && it->id == model)
                {
                    uint8_t* const target = tracked->model;
                    g_held.erase(it);

                    if (Read<int>(target, GameAddress::kModelReferencesOffset) == 0)
                    {
                        Forget(target);
                    }
                    release(target);
                    return 1;
                }
            }
            return 0;
        }

        int __cdecl ApiReadVertices(AmodMod mod, AmodModelId model, int frame, AmodVertex* out, int max)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;
            if (tracked == nullptr || frame < 0)
            {
                return 0;
            }

            int count = 0;
            const uint8_t* const direct = FrameVertices(tracked->model, frame, count);
            if (direct != nullptr)
            {
                const int wanted = (out != nullptr && max > 0) ? (max < count ? max : count) : 0;

                for (int at = 0; at < wanted; ++at)
                {
                    memcpy(&out[at], direct + static_cast<size_t>(at) * GameAddress::kMeshVertexSize, sizeof(AmodVertex));
                }
                return count;
            }

            Geometry geometry;
            if (!ReadGeometry(tracked->model, geometry) || frame >= geometry.frameCount)
            {
                return 0;
            }

            const int wanted = out != nullptr && max > 0
                ? (max < geometry.vertexCount ? max : geometry.vertexCount)
                : 0;

            const uint8_t* const source = geometry.vertices.data() + static_cast<size_t>(frame) * geometry.vertexCount * GameAddress::kMeshVertexSize;

            for (int at = 0; at < wanted; ++at)
            {
                memcpy(&out[at], source + static_cast<size_t>(at) * GameAddress::kMeshVertexSize, sizeof(AmodVertex));
            }
            return geometry.vertexCount;
        }

        int __cdecl ApiReadIndices(AmodMod mod, AmodModelId model, int frame, uint16_t* out, int max)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;
            Geometry geometry;
            if (tracked == nullptr || !ReadGeometry(tracked->model, geometry) || frame < 0 || frame >= geometry.frameCount)
            {
                return 0;
            }

            const int wanted = out != nullptr && max > 0
                ? (max < geometry.indexCount ? max : geometry.indexCount)
                : 0;

            memcpy(out, geometry.indices.data() + static_cast<size_t>(frame) * geometry.indexCount, static_cast<size_t>(wanted) * 2);
            return geometry.indexCount;
        }

        int WriteFrame(ModContext* context, AmodModelId model, int frame, const AmodVertex* vertices, int vertexCount, const uint16_t* indices, int indexCount)
        {
            Tracked* const tracked = context != nullptr ? TrackedById(model) : nullptr;
            Geometry geometry;
            if (tracked == nullptr || !ReadGeometry(tracked->model, geometry) || frame < 0 || frame >= geometry.frameCount)
            {
                return 0;
            }

            if ((vertices != nullptr && vertexCount != geometry.vertexCount) || (indices != nullptr && indexCount != geometry.indexCount))
            {
                return 0;
            }

            if (!EnsureSnapshot(*tracked, context))
            {
                return 0;
            }

            const size_t vertexBytes = static_cast<size_t>(geometry.vertexCount) * GameAddress::kMeshVertexSize;

            if (vertices != nullptr)
            {
                memcpy(geometry.vertices.data() + static_cast<size_t>(frame) * vertexBytes, vertices, vertexBytes);
            }
            if (indices != nullptr)
            {
                memcpy(geometry.indices.data() + static_cast<size_t>(frame) * geometry.indexCount, indices, static_cast<size_t>(geometry.indexCount) * 2);
            }

            uint8_t* const mesh = BuildMesh(geometry);
            return (mesh != nullptr && InstallMesh(tracked->model, mesh)) ? 1 : 0;
        }

        int __cdecl ApiWriteVertices(AmodMod mod, AmodModelId model, int frame, const AmodVertex* vertices, int count)
        {
            return vertices != nullptr
                ? WriteFrame(Caller(mod), model, frame, vertices, count, nullptr, 0)
                : 0;
        }

        int __cdecl ApiWriteIndices(AmodMod mod, AmodModelId model, int frame, const uint16_t* indices, int count)
        {
            return indices != nullptr
                ? WriteFrame(Caller(mod), model, frame, nullptr, 0, indices, count)
                : 0;
        }

        bool TakeMeshData(const AmodMeshData* data, Geometry& out)
        {
            if (data == nullptr || data->structSize < sizeof(AmodMeshData) || data->vertices == nullptr || data->indices == nullptr)
            {
                return false;
            }

            out = Geometry{};
            out.frameCount = data->frameCount > 0 ? data->frameCount : 1;
            out.vertexCount = data->vertexCount;
            out.indexCount = data->indexCount;
            out.primitiveCount = data->primitiveCount > 0 ? data->primitiveCount : data->indexCount / 3;

            const size_t vertexBytes = static_cast<size_t>(out.vertexCount) * GameAddress::kMeshVertexSize;

            if (out.frameCount < 1 || out.vertexCount < 1 || out.indexCount < 1 || out.vertexCount > 0xFFFF)
            {
                return false;
            }

            out.vertices.resize(static_cast<size_t>(out.frameCount) * vertexBytes);
            out.indices.resize(static_cast<size_t>(out.frameCount) * out.indexCount);

            for (int frame = 0; frame < out.frameCount; ++frame)
            {
                const AmodVertex* const source = data->vertices + static_cast<size_t>(frame) * out.vertexCount;

                for (int at = 0; at < out.vertexCount; ++at)
                {
                    memcpy(out.vertices.data() + static_cast<size_t>(frame) * vertexBytes +
                        static_cast<size_t>(at) * GameAddress::kMeshVertexSize, &source[at], sizeof(AmodVertex));
                }
            }

            memcpy(out.indices.data(), data->indices, static_cast<size_t>(out.frameCount) * out.indexCount * 2);
            return out.Sane();
        }

        AmodModelId __cdecl ApiCreate(AmodMod mod, const char* name, const AmodMeshData* mesh)
        {
            ModContext* const context = Caller(mod);
            const CreateFromMeshRoutine create = Routine<CreateFromMeshRoutine>(GameAddress::kModelCreateFromMesh);

            Geometry geometry;
            if (context == nullptr || create == nullptr || name == nullptr || name[0] == '\0' || !TakeMeshData(mesh, geometry))
            {
                return AMOD_INVALID_MODEL;
            }

            const std::string wanted = Normalised(name);
            for (Tracked& entry : g_tracked)
            {
                if (entry.creator == nullptr || !IsModel(entry.model) || !Listed(entry.model) ||
                    Normalised(ReadText(entry.model, GameAddress::kModelNameOffset)) != wanted)
                {
                    continue;
                }

                uint8_t* const rebuilt = BuildMesh(geometry);
                return (rebuilt != nullptr && InstallMesh(entry.model, rebuilt)) ? entry.id
                    : AMOD_INVALID_MODEL;
            }

            struct SourceFrame
            {
                const void* vertices;
                int vertexCount;
                const void* indices;
                int indexCount;
                int primitiveCount;
            };

            struct SourceMesh
            {
                uint8_t head[GameAddress::kMeshFramesOffset];
                SourceFrame* frames;
                int frameCount;
                uint8_t tail[GameAddress::kMeshSize - GameAddress::kMeshFrameCountOffset - 4];
            };

            std::vector<SourceFrame> frames(static_cast<size_t>(geometry.frameCount));
            const size_t vertexBytes = static_cast<size_t>(geometry.vertexCount) * GameAddress::kMeshVertexSize;

            for (int frame = 0; frame < geometry.frameCount; ++frame)
            {
                frames[frame].vertices = geometry.vertices.data() + static_cast<size_t>(frame) * vertexBytes;
                frames[frame].vertexCount = geometry.vertexCount;
                frames[frame].indices = geometry.indices.data() + static_cast<size_t>(frame) * geometry.indexCount;
                frames[frame].indexCount = geometry.indexCount;
                frames[frame].primitiveCount = geometry.primitiveCount;
            }

            SourceMesh source = {};
            source.frames = frames.data();
            source.frameCount = geometry.frameCount;

            uint8_t* const model = create(0, &source);
            ++g_listGeneration;

            if (!IsModel(model))
            {
                return AMOD_INVALID_MODEL;
            }

            strncpy_s(reinterpret_cast<char*>(model) + GameAddress::kModelNameOffset, GameAddress::kModelNameLength, name, GameAddress::kModelNameLength - 1);

            if (geometry.frameCount > 1)
            {
                uint8_t* const full = BuildMesh(geometry);
                if (full == nullptr || !InstallMesh(model, full))
                {
                    FORGE_LOG_ERROR("Model '%s' was built but only its first frame took", name);
                }
            }

            return Track(model, context);
        }

        int __cdecl ApiReplace(AmodMod mod, AmodModelId model, const AmodMeshData* mesh)
        {
            ModContext* const context = Caller(mod);
            Tracked* const tracked = context != nullptr ? TrackedById(model) : nullptr;

            Geometry geometry;
            if (tracked == nullptr || !TakeMeshData(mesh, geometry) || !EnsureSnapshot(*tracked, context))
            {
                return 0;
            }

            uint8_t* const built = BuildMesh(geometry);
            return (built != nullptr && InstallMesh(tracked->model, built)) ? 1 : 0;
        }

        int __cdecl ApiRestore(AmodMod mod, AmodModelId model)
        {
            Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;

            if (tracked == nullptr || !tracked->snapshotTaken)
            {
                return 0;
            }
            return RestoreSnapshot(*tracked) ? 1 : 0;
        }

        int __cdecl ApiSave(AmodMod mod, AmodModelId model, const char* path)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;

            Geometry geometry;
            if (tracked == nullptr || path == nullptr || path[0] == '\0' || !ReadGeometry(tracked->model, geometry))
            {
                return 0;
            }

            std::filesystem::path file = std::filesystem::path(FromUtf8(path));
            if (file.is_relative())
            {
                file = Paths::GameDirectory() / file;
            }

            std::error_code failure;
            std::filesystem::create_directories(file.parent_path(), failure);

            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                FORGE_LOG_ERROR("Cannot write '%s'", PathToUtf8(file).c_str());
                return 0;
            }

            const uint16_t version = 1;

            const uint16_t padding = 0xCCCC;
            const int primitives = geometry.primitiveCount > 0 ? geometry.primitiveCount
                : geometry.indexCount / 3;

            out.write(reinterpret_cast<const char*>(&version), sizeof(version));
            out.write(reinterpret_cast<const char*>(&padding), sizeof(padding));
            out.write(reinterpret_cast<const char*>(&geometry.frameCount), sizeof(int));
            out.write(reinterpret_cast<const char*>(&geometry.vertexCount), sizeof(int));
            out.write(reinterpret_cast<const char*>(&primitives), sizeof(int));
            out.write(reinterpret_cast<const char*>(&geometry.indexCount), sizeof(int));

            const size_t vertexBytes = static_cast<size_t>(geometry.vertexCount) * GameAddress::kMeshVertexSize;

            for (int frame = 0; frame < geometry.frameCount; ++frame)
            {
                out.write(reinterpret_cast<const char*>(geometry.vertices.data() +
                    static_cast<size_t>(frame) * vertexBytes), static_cast<std::streamsize>(vertexBytes));
                out.write(reinterpret_cast<const char*>(
                    geometry.indices.data() +
                    static_cast<size_t>(frame) * geometry.indexCount), static_cast<std::streamsize>(geometry.indexCount) * 2);
            }

            // After the frames, where the game's parser never looks.
            if (!tracked->materials.empty())
            {
                const uint32_t tag = kMaterialTag;
                const uint32_t count = static_cast<uint32_t>(tracked->materials.size());

                out.write(reinterpret_cast<const char*>(&tag), sizeof(tag));
                out.write(reinterpret_cast<const char*>(&count), sizeof(count));

                for (const Material& material : tracked->materials)
                {
                    const uint8_t* const object =
                        static_cast<const uint8_t*>(TextureApi::TextureFor(material.texture));
                    const char* const name = object != nullptr
                        ? reinterpret_cast<const char*>(object + GameAddress::kTextureNameOffset)
                        : "";

                    const uint32_t first = static_cast<uint32_t>(material.firstIndex);
                    const uint32_t primitives = static_cast<uint32_t>(material.primitiveCount);
                    const uint32_t length = static_cast<uint32_t>(strlen(name));

                    out.write(reinterpret_cast<const char*>(&first), sizeof(first));
                    out.write(reinterpret_cast<const char*>(&primitives), sizeof(primitives));
                    out.write(reinterpret_cast<const char*>(&length), sizeof(length));
                    out.write(name, static_cast<std::streamsize>(length));
                }
            }

            out.close();
            if (!out)
            {
                FORGE_LOG_ERROR("'%s' was not written whole", PathToUtf8(file).c_str());
                return 0;
            }

            FORGE_LOG_INFO("Wrote '%s': %d frame(s), %d verts, %d indices, %zu material(s)",
                PathToUtf8(file).c_str(), geometry.frameCount, geometry.vertexCount,
                geometry.indexCount, tracked->materials.size());
            return 1;
        }

        int __cdecl ApiOverride(AmodMod mod, const char* path, AmodModelId model)
        {
            ModContext* const context = Caller(mod);
            const Tracked* const tracked = context != nullptr ? TrackedById(model) : nullptr;
            const std::string wanted = Normalised(path);

            if (tracked == nullptr || wanted.empty() || g_originalLoad == nullptr)
            {
                return 0;
            }

            for (Substitute& entry : g_substitutes)
            {
                if (entry.path == wanted)
                {
                    entry.owner = context;
                    entry.id = model;
                    return 1;
                }
            }

            Substitute entry;
            entry.owner = context;
            entry.path = wanted;
            entry.id = model;
            g_substitutes.push_back(entry);
            return 1;
        }

        int __cdecl ApiClearOverride(AmodMod mod, const char* path)
        {
            ModContext* const context = Caller(mod);
            const std::string wanted = Normalised(path);

            for (auto it = g_substitutes.begin(); it != g_substitutes.end(); ++it)
            {
                if (it->owner == context && it->path == wanted)
                {
                    g_substitutes.erase(it);
                    return 1;
                }
            }
            return 0;
        }

        uintptr_t RoomModelOffset(int slot)
        {
            switch (slot)
            {
            case AMOD_ROOM_MODEL_LIGHTMAP: return GameAddress::kRoomLightmapModelOffset;
            case AMOD_ROOM_MODEL_BACKWALL: return GameAddress::kRoomBackWallModelOffset;
            case AMOD_ROOM_MODEL_GEOMETRY: return GameAddress::kRoomGeometryModelOffset;
            }
            return 0;
        }

        uint8_t* RoomFor(int roomIndex)
        {
            return roomIndex < 0 ? WorldApi::CurrentRoom() : WorldApi::RoomAt(roomIndex);
        }

        void GiveBackRoomModel(uintptr_t applied)
        {
            uint8_t* const model = reinterpret_cast<uint8_t*>(applied);
            const ReleaseModelRoutine release = g_originalRelease != nullptr
                ? g_originalRelease
                : Routine<ReleaseModelRoutine>(GameAddress::kModelRelease);

            if (release != nullptr && IsModel(model) && Listed(model))
            {
                if (Read<int>(model, GameAddress::kModelReferencesOffset) == 0)
                {
                    Forget(model);
                }
                release(model);
            }
        }

        AmodModelId __cdecl ApiGetRoomModel(AmodMod mod, int roomIndex, int slot)
        {
            const uintptr_t offset = RoomModelOffset(slot);
            uint8_t* const room = Caller(mod) != nullptr ? RoomFor(roomIndex) : nullptr;

            if (room == nullptr || offset == 0 || !Readable(room + offset, sizeof(uintptr_t)))
            {
                return AMOD_INVALID_MODEL;
            }

            uint8_t* const model = reinterpret_cast<uint8_t*>(Read<uintptr_t>(room, offset));
            return IsModel(model) ? Track(model, nullptr) : AMOD_INVALID_MODEL;
        }

        int __cdecl ApiSetRoomModel(AmodMod mod, int roomIndex, int slot, AmodModelId model)
        {
            ModContext* const context = Caller(mod);
            const Tracked* const tracked = context != nullptr ? TrackedById(model) : nullptr;
            const uintptr_t offset = RoomModelOffset(slot);
            uint8_t* const room = context != nullptr ? RoomFor(roomIndex) : nullptr;

            if (tracked == nullptr || room == nullptr || offset == 0 || !Readable(room + offset, sizeof(uintptr_t)))
            {
                return 0;
            }

            const uintptr_t was = Read<uintptr_t>(room, offset);
            WritePointer(room, offset, tracked->model);

            WriteInt(tracked->model, GameAddress::kModelReferencesOffset, Read<int>(tracked->model, GameAddress::kModelReferencesOffset) + 1);

            for (RoomSwap& swap : g_roomSwaps)
            {
                if (swap.owner == context && swap.room == room && swap.slot == slot)
                {
                    GiveBackRoomModel(swap.applied);
                    swap.applied = reinterpret_cast<uintptr_t>(tracked->model);
                    return 1;
                }
            }

            RoomSwap swap;
            swap.owner = context;
            swap.roomIndex = roomIndex;
            swap.slot = slot;
            swap.room = room;
            swap.was = was;
            swap.applied = reinterpret_cast<uintptr_t>(tracked->model);
            g_roomSwaps.push_back(swap);
            return 1;
        }

        int __cdecl ApiDraw(AmodMod mod, AmodModelId model)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;
            const RenderRoutine render = Routine<RenderRoutine>(GameAddress::kModelRender);

            if (tracked == nullptr || render == nullptr)
            {
                return 0;
            }
            render(tracked->model);
            return 1;
        }

        int __cdecl ApiDrawFrames(AmodMod mod, AmodModelId model, int from, int to, float blend)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;
            const RenderAnimatedRoutine render = Routine<RenderAnimatedRoutine>(GameAddress::kModelRenderAnimated);

            if (tracked == nullptr || render == nullptr || from < 0 || to < 0)
            {
                return 0;
            }

            const int frames = FrameCountOf(tracked->model);
            if (from >= frames || to >= frames)
            {
                return 0;
            }

            render(tracked->model, from, to, blend);
            return 1;
        }

        const Field kFields[] = {
            { "name",     GameAddress::kModelNameOffset,           Kind::Text,   0, 0, 0, false },
            { "kind",     GameAddress::kModelKindOffset,           Kind::Int,    0, 0, 0, false },
            { "verts",    GameAddress::kModelVertexCountOffset,    Kind::Int,    0, 0, 0, false },
            { "prims",    GameAddress::kModelPrimitiveCountOffset, Kind::Int,    0, 0, 0, false },
            { "refs",     GameAddress::kModelReferencesOffset,     Kind::Int,    0, 0, 0, false },
            { "centre",   GameAddress::kModelCentreOffset,         Kind::Floats, 3, 0, 0, true },
            { "extents",  GameAddress::kModelExtentsOffset,        Kind::Floats, 3, 0, 0, true }, };

        int __cdecl ApiGetProperty(AmodMod mod, AmodModelId model, const char* key, AmodValue* value)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;
            const Field* const field = ApiProperty::Find(kFields, sizeof(kFields) / sizeof(kFields[0]), key);

            return (tracked != nullptr && field != nullptr)
                ? ApiProperty::Get(tracked->model, *field, value)
                : 0;
        }

        int __cdecl ApiSetProperty(AmodMod mod, AmodModelId model, const char* key, const AmodValue* value)
        {
            ModContext* const context = Caller(mod);
            Tracked* const tracked = context != nullptr ? TrackedById(model) : nullptr;
            const Field* const field = ApiProperty::Find(kFields, sizeof(kFields) / sizeof(kFields[0]), key);

            if (tracked == nullptr || field == nullptr || value == nullptr || !field->writable || !EnsureSnapshot(*tracked, context))
            {
                return 0;
            }
            return ApiProperty::Set(tracked->model, *field, *value);
        }

        int __cdecl ApiHasProperty(AmodMod mod, const char* key)
        {
            return (Caller(mod) != nullptr && ApiProperty::Find(kFields, sizeof(kFields) / sizeof(kFields[0]), key) != nullptr)
                ? 1
                : 0;
        }

        int __cdecl ApiPropertyCount(AmodMod mod)
        {
            return Caller(mod) != nullptr ? static_cast<int>(sizeof(kFields) / sizeof(kFields[0])) : 0;
        }

        const char* __cdecl ApiPropertyNameAt(AmodMod mod, int index)
        {
            const int total = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));
            return (Caller(mod) != nullptr && index >= 0 && index < total) ? kFields[index].key : "";
        }

        int __cdecl ApiQueueRoom(AmodMod mod)
        {
            const int left = static_cast<int>(GameAddress::kSceneDrawCapacity) - QueueUsed() - static_cast<int>(g_queued.size());
            return (Caller(mod) != nullptr && left > 0) ? left : 0;
        }

        int __cdecl ApiEnqueue(AmodMod mod, const AmodModelDraw* draw)
        {
            ModContext* const context = Caller(mod);
            const Tracked* const tracked = (context != nullptr && draw != nullptr && draw->structSize >= sizeof(AmodModelDraw))
                ? TrackedById(draw->model)
                : nullptr;

            if (tracked == nullptr || g_originalDraw == nullptr || ApiQueueRoom(mod) <= 0)
            {
                static bool complained = false;
                if (!complained)
                {
                    complained = true;
                    FORGE_LOG_WARN(
                        "A draw was refused: model %s, hook %s, room %d of %d, list holds %d",
                        tracked != nullptr ? "known" : "unknown",
                        g_originalDraw != nullptr ? "installed" : "missing", ApiQueueRoom(mod),
                        static_cast<int>(GameAddress::kSceneDrawCapacity), QueueUsed());
                }
                return 0;
            }

            QueuedDraw queued = {};
            queued.owner = context;
            queued.blend = (draw->blend >= AMOD_DRAW_BLEND_NONE && draw->blend <= AMOD_DRAW_BLEND_MUL)
                ? draw->blend
                : AMOD_DRAW_BLEND_NONE;

            uint8_t* const item = queued.item;
            item[GameAddress::kDrawItemFlagsOffset] = static_cast<uint8_t>(
                ((draw->flags & AMOD_DRAW_HAS_MATRIX) != 0 ? GameAddress::kDrawItemHasMatrix : 0) | ((draw->flags & AMOD_DRAW_TURNS_ABOUT_AXIS) != 0
                    ? GameAddress::kDrawItemTurnsAboutAxis
                    : 0));

            WritePointer(item, GameAddress::kDrawItemModelOffset, tracked->model);
            WriteInt(item, GameAddress::kDrawItemTextureOffset, static_cast<int>(draw->texture));

            memcpy(item + GameAddress::kDrawItemPositionOffset, draw->position, sizeof(draw->position));
            memcpy(item + GameAddress::kDrawItemMatrixOffset, draw->matrix, sizeof(draw->matrix));
            memcpy(item + GameAddress::kDrawItemAngleXOffset, &draw->rotation[0], sizeof(float));
            memcpy(item + GameAddress::kDrawItemAngleYOffset, &draw->rotation[1], sizeof(float));
            memcpy(item + GameAddress::kDrawItemAngleZOffset, &draw->rotation[2], sizeof(float));
            memcpy(item + GameAddress::kDrawItemAxisOffset, draw->axis, sizeof(draw->axis));
            memcpy(item + GameAddress::kDrawItemAxisAngleOffset, &draw->angle, sizeof(float));
            memcpy(item + GameAddress::kDrawItemFrameOffset, &draw->frame, sizeof(float));

            g_queued.push_back(queued);
            return 1;
        }

        struct ResolvedTexture
        {
            std::string name;
            uint32_t id;
        };

        std::vector<ResolvedTexture> g_textures;

        uint32_t __cdecl ApiResolveTexture(AmodMod mod, const char* name)
        {
            if (Caller(mod) == nullptr || name == nullptr || name[0] == '\0')
            {
                return 0;
            }

            for (const ResolvedTexture& known : g_textures)
            {
                if (known.name == name)
                {
                    return known.id;
                }
            }

            void** const at = static_cast<void**>(GameModule::Resolve(GameAddress::kResolveTextureSlot));
            const ResolveRoutine resolve = at != nullptr ? reinterpret_cast<ResolveRoutine>(*at) : nullptr;

            const uint32_t id = resolve != nullptr ? resolve(name) : 0;
            if (id != 0)
            {
                ResolvedTexture known;
                known.name = name;
                known.id = id;
                g_textures.push_back(known);
            }
            return id;
        }

        uint32_t TextureIdByName(const char* name)
        {
            for (const ResolvedTexture& known : g_textures)
            {
                if (known.name == name)
                {
                    return known.id;
                }
            }

            void** const at = static_cast<void**>(GameModule::Resolve(GameAddress::kResolveTextureSlot));
            const ResolveRoutine resolve = at != nullptr ? reinterpret_cast<ResolveRoutine>(*at) : nullptr;
            const uint32_t id = resolve != nullptr ? resolve(name) : 0;

            if (id != 0)
            {
                ResolvedTexture known;
                known.name = name;
                known.id = id;
                g_textures.push_back(known);
            }
            return id;
        }

        bool ReadU32(const uint8_t* bytes, size_t size, size_t& at, uint32_t& value)
        {
            if (at + sizeof(uint32_t) > size)
            {
                return false;
            }
            memcpy(&value, bytes + at, sizeof(uint32_t));
            at += sizeof(uint32_t);
            return true;
        }

        void ReadMaterialsFromFile(const char* path, Tracked& tracked)
        {
            const PackageOpenRoutine open = Routine<PackageOpenRoutine>(GameAddress::kPackageOpenFile);
            const PackageFreeRoutine free = Routine<PackageFreeRoutine>(GameAddress::kPackageFree);

            if (open == nullptr || free == nullptr || path == nullptr)
            {
                return;
            }

            uint8_t* const handle = open(path);
            if (handle == nullptr)
            {
                return;
            }

            const uint8_t* const bytes = Read<const uint8_t*>(handle, 0x108);
            const size_t size = static_cast<size_t>(Read<uint32_t>(handle, 0x104));

            if (bytes != nullptr && size >= 0x14)
            {
                const int frames = Read<int>(bytes, 0x04);
                const int vertices = Read<int>(bytes, 0x08);
                const int indices = Read<int>(bytes, 0x10);

                size_t at = 0x14 + static_cast<size_t>(frames) *
                    (static_cast<size_t>(vertices) * GameAddress::kMeshVertexSize +
                     static_cast<size_t>(indices) * 2);

                uint32_t tag = 0;
                uint32_t count = 0;

                if (frames > 0 && vertices > 0 && indices > 0 && ReadU32(bytes, size, at, tag) &&
                    tag == kMaterialTag && ReadU32(bytes, size, at, count) && count < 0x1000)
                {
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        uint32_t first = 0;
                        uint32_t primitives = 0;
                        uint32_t length = 0;

                        if (!ReadU32(bytes, size, at, first) ||
                            !ReadU32(bytes, size, at, primitives) ||
                            !ReadU32(bytes, size, at, length) || length > 0x100 ||
                            at + length > size)
                        {
                            break;
                        }

                        const std::string name(reinterpret_cast<const char*>(bytes + at), length);
                        at += length;

                        Material material;
                        material.firstIndex = static_cast<int>(first);
                        material.primitiveCount = static_cast<int>(primitives);
                        material.texture = TextureIdByName(name.c_str());
                        tracked.materials.push_back(material);
                    }

                    if (!tracked.materials.empty())
                    {
                        FORGE_LOG_INFO("'%s' carries %zu material(s)", path, tracked.materials.size());
                    }
                }
            }

            free(handle);
        }

        int __cdecl ApiAddMaterial(AmodMod mod, AmodModelId model, int firstIndex,
                                   int primitiveCount, uint32_t texture)
        {
            ModContext* const context = Caller(mod);
            Tracked* const tracked = context != nullptr ? TrackedById(model) : nullptr;

            if (tracked == nullptr)
            {
                Fail(mod, AMOD_FAIL_NO_SUCH, "there is no such model");
                return -1;
            }
            if (firstIndex < 0 || primitiveCount <= 0)
            {
                Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "a material covers at least one triangle");
                return -1;
            }

            Material material;
            material.firstIndex = firstIndex;
            material.primitiveCount = primitiveCount;
            material.texture = texture;

            tracked->materials.push_back(material);
            tracked->materialsOwner = context;
            return static_cast<int>(tracked->materials.size()) - 1;
        }

        int __cdecl ApiMaterialCount(AmodMod mod, AmodModelId model)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;
            return tracked != nullptr ? static_cast<int>(tracked->materials.size()) : 0;
        }

        int __cdecl ApiGetMaterial(AmodMod mod, AmodModelId model, int index, AmodModelMaterial* out)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;

            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetMaterial was given nowhere to put it");
            }
            if (tracked == nullptr || index < 0 ||
                index >= static_cast<int>(tracked->materials.size()))
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "that model has no material %d", index);
            }

            const Material& material = tracked->materials[static_cast<size_t>(index)];

            AmodModelMaterial filled = {};
            filled.structSize = sizeof(filled);
            filled.firstIndex = material.firstIndex;
            filled.primitiveCount = material.primitiveCount;
            filled.texture = material.texture;

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiClearMaterials(AmodMod mod, AmodModelId model)
        {
            Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;

            if (tracked == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no such model");
            }

            tracked->materials.clear();
            tracked->materialsOwner = nullptr;
            return 1;
        }

        int ReadMatrix(AmodMod mod, uintptr_t offset, float* out)
        {
            const uint8_t* const scene = Caller(mod) != nullptr ? Scene() : nullptr;
            if (scene == nullptr || out == nullptr || !Readable(scene + offset, 16 * sizeof(float)))
            {
                return 0;
            }

            memcpy(out, scene + offset, 16 * sizeof(float));
            return 1;
        }

        int __cdecl ApiGetViewMatrix(AmodMod mod, float* out)
        {
            return ReadMatrix(mod, GameAddress::kSceneViewOffset, out);
        }

        int __cdecl ApiGetProjectionMatrix(AmodMod mod, float* out)
        {
            return ReadMatrix(mod, GameAddress::kSceneProjectionOffset, out);
        }

        int __cdecl ApiGetAnimationCount(AmodMod mod)
        {
            if (Caller(mod) == nullptr)
            {
                return 0;
            }

            int total = 0;
            for (uint8_t* walk = AnimListHead(); IsAnimation(walk) && total < kModelWalkLimit;
                walk = NextAnim(walk))
            {
                ++total;
            }
            return total;
        }

        AmodAnimationId __cdecl ApiGetAnimationAt(AmodMod mod, int index)
        {
            if (Caller(mod) == nullptr || index < 0)
            {
                return AMOD_INVALID_ANIMATION;
            }

            int at = 0;
            for (uint8_t* walk = AnimListHead(); IsAnimation(walk) && at < kModelWalkLimit;
                walk = NextAnim(walk))
            {
                if (at++ == index)
                {
                    return TrackAnim(walk);
                }
            }
            return AMOD_INVALID_ANIMATION;
        }

        AmodAnimationId __cdecl ApiFindAnimation(AmodMod mod, const char* name)
        {
            const std::string wanted = Normalised(name);
            if (Caller(mod) == nullptr || wanted.empty())
            {
                return AMOD_INVALID_ANIMATION;
            }

            int seen = 0;
            for (uint8_t* walk = AnimListHead(); IsAnimation(walk) && seen < kModelWalkLimit;
                walk = NextAnim(walk), ++seen)
            {
                if (NameAt(walk, GameAddress::kAnimNameOffset) && Normalised(ReadText(walk, GameAddress::kAnimNameOffset)) == wanted)
                {
                    return TrackAnim(walk);
                }
            }
            return AMOD_INVALID_ANIMATION;
        }

        int __cdecl ApiGetAnimation(AmodMod mod, AmodAnimationId animation, AmodAnimation* out)
        {
            const TrackedAnim* const tracked = Caller(mod) != nullptr ? AnimById(animation) : nullptr;
            if (tracked == nullptr || out == nullptr)
            {
                return 0;
            }

            AmodAnimation filled = {};
            filled.structSize = sizeof(filled);
            filled.name = NameAt(tracked->animation, GameAddress::kAnimNameOffset)
                ? ReadText(tracked->animation, GameAddress::kAnimNameOffset)
                : "";
            filled.framesetCount = Read<int>(tracked->animation, GameAddress::kAnimFramesetCountOffset);
            filled.references = Read<int>(tracked->animation, GameAddress::kAnimReferencesOffset);

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiGetFrameset(AmodMod mod, AmodAnimationId animation, int index, AmodFrameset* out)
        {
            const TrackedAnim* const tracked = Caller(mod) != nullptr ? AnimById(animation) : nullptr;
            if (tracked == nullptr || out == nullptr || index < 0)
            {
                return 0;
            }

            const int total = Read<int>(tracked->animation, GameAddress::kAnimFramesetCountOffset);
            uint8_t* const framesets = reinterpret_cast<uint8_t*>(Read<uintptr_t>(tracked->animation, GameAddress::kAnimFramesetsOffset));

            if (framesets == nullptr || index >= total)
            {
                return 0;
            }

            const uint8_t* const frameset = framesets + static_cast<size_t>(index) * GameAddress::kFramesetStride;
            if (!Readable(frameset, GameAddress::kFramesetStride))
            {
                return 0;
            }

            AmodFrameset filled = {};
            filled.structSize = sizeof(filled);
            filled.name = ReadText(frameset, GameAddress::kFramesetNameOffset);
            filled.follow = ReadText(frameset, GameAddress::kFramesetFollowOffset);
            filled.fps = Read<float>(frameset, GameAddress::kFramesetFpsOffset);
            filled.startFrame = Read<int>(frameset, GameAddress::kFramesetStartFrameOffset);
            filled.from = Read<int>(frameset, GameAddress::kFramesetFromOffset);
            filled.to = Read<int>(frameset, GameAddress::kFramesetToOffset);
            filled.loop = Read<int>(frameset, GameAddress::kFramesetLoopOffset);
            filled.stops = Read<int>(frameset, GameAddress::kFramesetBreakOffset);
            filled.backwards = Read<float>(frameset, GameAddress::kFramesetDirectionOffset) < 0.0f;

            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        AmodAnimationId __cdecl ApiLoadAnimation(AmodMod mod, const char* path)
        {
            ModContext* const context = Caller(mod);
            const LoadOrFindAnimRoutine load = Routine<LoadOrFindAnimRoutine>(GameAddress::kLoadOrFindAnim);

            if (context == nullptr || load == nullptr || path == nullptr || path[0] == '\0')
            {
                return AMOD_INVALID_ANIMATION;
            }

            const AmodAnimationId id = TrackAnim(load(path));
            if (id == AMOD_INVALID_ANIMATION)
            {
                return AMOD_INVALID_ANIMATION;
            }

            Held held;
            held.owner = context;
            held.id = id;
            g_heldAnims.push_back(held);
            return id;
        }

        void ReleaseAnimation(uint8_t* animation)
        {
            void** const at = static_cast<void**>(GameModule::Resolve(GameAddress::kAnimReleaseSlot));
            const AnimReleaseRoutine release = at != nullptr ? reinterpret_cast<AnimReleaseRoutine>(*at) : nullptr;

            if (release != nullptr)
            {
                release(animation);
            }
        }

        int __cdecl ApiReleaseAnimation(AmodMod mod, AmodAnimationId animation)
        {
            ModContext* const context = Caller(mod);
            const TrackedAnim* const tracked = context != nullptr ? AnimById(animation) : nullptr;
            if (tracked == nullptr)
            {
                return 0;
            }

            for (auto it = g_heldAnims.begin(); it != g_heldAnims.end(); ++it)
            {
                if (it->owner == context && it->id == animation)
                {
                    uint8_t* const target = tracked->animation;
                    g_heldAnims.erase(it);
                    ReleaseAnimation(target);
                    return 1;
                }
            }
            return 0;
        }

        // Animation name for error messages.
        const char* AnimName(const uint8_t* animation)
        {
            return (animation != nullptr && NameAt(animation, GameAddress::kAnimNameOffset))
                ? ReadText(animation, GameAddress::kAnimNameOffset)
                : "";
        }

        // AnimBox: SELF or ENTITY face.
        uint8_t* AnimBoxOf(const uint8_t* animation, int which)
        {
            const uintptr_t offset = which == AMOD_ANIM_BOX_ENTITY ? GameAddress::kAnimBoxEntityOffset : GameAddress::kAnimBoxOffset;
            uint8_t* const box = reinterpret_cast<uint8_t*>(Read<uintptr_t>(animation, offset));
            return (box != nullptr && Readable(box, GameAddress::kAnimBoxSize)) ? box : nullptr;
        }

        // Frame <0 = default; missing frame box reads as default.
        uint8_t* AnimBoxFace(uint8_t* animBox, int frame, bool claim = false)
        {
            if (frame < 0)
            {
                return animBox + GameAddress::kAnimBoxDefaultOffset;
            }

            const int total = Read<int>(animBox, GameAddress::kAnimBoxFrameCountOffset);
            uint8_t* const frames = reinterpret_cast<uint8_t*>(Read<uintptr_t>(animBox, GameAddress::kAnimBoxFramesOffset));

            if (frames == nullptr || frame >= total || total <= 0 || !Readable(frames, static_cast<size_t>(total) * GameAddress::kAnimBoxFrameStride))
            {
                return nullptr;
            }

            uint8_t* const entry = frames + static_cast<size_t>(frame) * GameAddress::kAnimBoxFrameStride;
            if (entry[GameAddress::kAnimBoxFrameUsedOffset] == 0)
            {
                if (!claim)
                {
                    return animBox + GameAddress::kAnimBoxDefaultOffset;
                }
                memcpy(entry + GameAddress::kAnimBoxFrameBoxOffset,
                    animBox + GameAddress::kAnimBoxDefaultOffset, GameAddress::kRoomCollisionBoxStride);
                entry[GameAddress::kAnimBoxFrameUsedOffset] = 1;
            }
            return entry + GameAddress::kAnimBoxFrameBoxOffset;
        }

        void ReadBox(const uint8_t* box, AmodBox& out)
        {
            out.min[0] = Read<float>(box, GameAddress::kBoxMinXOffset);
            out.min[1] = Read<float>(box, GameAddress::kBoxMinYOffset);
            out.min[2] = Read<float>(box, GameAddress::kBoxMinZOffset);
            out.max[0] = Read<float>(box, GameAddress::kBoxMaxXOffset);
            out.max[1] = Read<float>(box, GameAddress::kBoxMaxYOffset);
            out.max[2] = Read<float>(box, GameAddress::kBoxMaxZOffset);
        }

        void WriteBox(uint8_t* box, const AmodBox& from)
        {
            memcpy(box + GameAddress::kBoxMinXOffset, &from.min[0], sizeof(float));
            memcpy(box + GameAddress::kBoxMinYOffset, &from.min[1], sizeof(float));
            memcpy(box + GameAddress::kBoxMinZOffset, &from.min[2], sizeof(float));
            memcpy(box + GameAddress::kBoxMaxXOffset, &from.max[0], sizeof(float));
            memcpy(box + GameAddress::kBoxMaxYOffset, &from.max[1], sizeof(float));
            memcpy(box + GameAddress::kBoxMaxZOffset, &from.max[2], sizeof(float));
        }

        // Snapshot by anim+face; frame array may move on grow.
        struct AnimBoxSnapshot
        {
            ModContext* owner;
            uint8_t* animation;
            int which;
            int frame;
            uint8_t bytes[GameAddress::kRoomCollisionBoxStride];
        };

        std::vector<AnimBoxSnapshot> g_animBoxSnapshots;

        void KeepAnimBox(ModContext* owner, uint8_t* animation, int which, int frame, const uint8_t* face)
        {
            for (const AnimBoxSnapshot& kept : g_animBoxSnapshots)
            {
                if (kept.animation == animation && kept.which == which && kept.frame == frame)
                {
                    return;
                }
            }

            AnimBoxSnapshot kept;
            kept.owner = owner;
            kept.animation = animation;
            kept.which = which;
            kept.frame = frame;
            memcpy(kept.bytes, face, sizeof(kept.bytes));
            g_animBoxSnapshots.push_back(kept);
        }

        struct AnimBoxMade
        {
            ModContext* owner;
            uint8_t* animation;
            int which;

            bool createdSet;        // the whole 0x24 is ours, and +0x10C/+0x110 was null before
            uint8_t* madeFrames;    // the array we allocated
            uint8_t* wasFrames;     // and what stood there before it
            int wasCount;
        };

        std::vector<AnimBoxMade> g_animBoxesMade;

        uintptr_t AnimBoxSlot(int which)
        {
            return which == AMOD_ANIM_BOX_ENTITY ? GameAddress::kAnimBoxEntityOffset : GameAddress::kAnimBoxOffset;
        }

        AnimBoxMade& MadeFor(ModContext* owner, uint8_t* animation, int which)
        {
            for (AnimBoxMade& made : g_animBoxesMade)
            {
                if (made.animation == animation && made.which == which)
                {
                    return made;
                }
            }

            AnimBoxMade fresh = {};
            fresh.owner = owner;
            fresh.animation = animation;
            fresh.which = which;
            g_animBoxesMade.push_back(fresh);
            return g_animBoxesMade.back();
        }

        int __cdecl ApiEnsureAnimBox(AmodMod mod, AmodAnimationId animation, int which, int frames)
        {
            ModContext* const caller = Caller(mod);
            const TrackedAnim* const tracked = caller != nullptr ? AnimById(animation) : nullptr;

            if (tracked == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "animation %u is not one this loader is holding", animation);
            }
            if (frames < 0 || frames > 0x1000)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "%d is not a sane number of frames", frames);
            }

            uint8_t* const anim = tracked->animation;
            const uintptr_t slot = AnimBoxSlot(which);
            uint8_t* animBox = AnimBoxOf(anim, which);

            // EnsureAnimBox: create set when script has $frame_bbox but no block.
            if (animBox == nullptr)
            {
                animBox = static_cast<uint8_t*>(GameFunctions::Allocate(static_cast<int>(GameAddress::kAnimBoxSize), ".\\AnimBox.cpp", 0xc));
                if (animBox == nullptr)
                {
                    return Fail(mod, AMOD_FAIL_REFUSED, "the game would not give room for a box set");
                }

                memset(animBox, 0, GameAddress::kAnimBoxSize);
                AnimBoxMade& made = MadeFor(caller, anim, which);
                made.createdSet = true;
                WritePointer(anim, slot, animBox);
            }

            const int have = Read<int>(animBox, GameAddress::kAnimBoxFrameCountOffset);
            if (frames <= have)
            {
                return 1;
            }

            uint8_t* const grown = static_cast<uint8_t*>(GameFunctions::Allocate(static_cast<int>(static_cast<size_t>(frames) * GameAddress::kAnimBoxFrameStride), ".\\AnimBox.cpp", 0x1a));
            if (grown == nullptr)
            {
                return Fail(mod, AMOD_FAIL_REFUSED, "the game would not give room for %d frame(s)", frames);
            }

            memset(grown, 0, static_cast<size_t>(frames) * GameAddress::kAnimBoxFrameStride);

            uint8_t* const had = reinterpret_cast<uint8_t*>(Read<uintptr_t>(animBox, GameAddress::kAnimBoxFramesOffset));
            if (had != nullptr && have > 0 && Readable(had, static_cast<size_t>(have) * GameAddress::kAnimBoxFrameStride))
            {
                memcpy(grown, had, static_cast<size_t>(have) * GameAddress::kAnimBoxFrameStride);
            }

            // Do not free the game's array; rollback restores it.
            AnimBoxMade& made = MadeFor(caller, anim, which);
            if (made.madeFrames == nullptr)
            {
                made.wasFrames = had;
                made.wasCount = have;
            }
            else
            {
                GameFunctions::Free(made.madeFrames);
            }
            made.madeFrames = grown;

            WritePointer(animBox, GameAddress::kAnimBoxFramesOffset, grown);
            WriteInt(animBox, GameAddress::kAnimBoxFrameCountOffset, frames);
            return 1;
        }

        int __cdecl ApiGetAnimBox(AmodMod mod, AmodAnimationId animation, int which, int frame, AmodBox* out)
        {
            const TrackedAnim* const tracked = Caller(mod) != nullptr ? AnimById(animation) : nullptr;
            uint8_t* const animBox = tracked != nullptr ? AnimBoxOf(tracked->animation, which) : nullptr;
            const uint8_t* const face = animBox != nullptr ? AnimBoxFace(animBox, frame) : nullptr;

            // Fail codes distinguish no such / refused / not offered.
            if (tracked == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "animation %u is not one this loader is holding", animation);
            }
            if (animBox == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_DECLARED, "'%s' declares no %s block",
                    AnimName(tracked->animation),
                    which == AMOD_ANIM_BOX_ENTITY ? "$anim_bbox_entity" : "$anim_bbox");
            }
            if (face == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "'%s' has no frame %d; it has %d",
                    AnimName(tracked->animation), frame,
                    Read<int>(animBox, GameAddress::kAnimBoxFrameCountOffset));
            }
            if (out == nullptr)
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "GetAnimBox was given nowhere to put the box");
            }

            AmodBox filled = {};
            filled.structSize = sizeof(filled);
            ReadBox(face, filled);
            return CopyOut(out, out->structSize, &filled, sizeof(filled));
        }

        int __cdecl ApiSetAnimBox(AmodMod mod, AmodAnimationId animation, int which, int frame, const AmodBox* box)
        {
            const TrackedAnim* const tracked = Caller(mod) != nullptr ? AnimById(animation) : nullptr;
            uint8_t* const animBox = tracked != nullptr ? AnimBoxOf(tracked->animation, which) : nullptr;
            uint8_t* const face = animBox != nullptr ? AnimBoxFace(animBox, frame, true) : nullptr;

            if (tracked == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "animation %u is not one this loader is holding", animation);
            }
            if (animBox == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_DECLARED, "'%s' declares no %s block, so there is nothing to write over",
                    AnimName(tracked->animation),
                    which == AMOD_ANIM_BOX_ENTITY ? "$anim_bbox_entity" : "$anim_bbox");
            }
            if (face == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "'%s' has no frame %d", AnimName(tracked->animation), frame);
            }
            if (box == nullptr || box->structSize < sizeof(AmodBox))
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "SetAnimBox was given no box to write");
            }

            KeepAnimBox(Caller(mod), tracked->animation, which, frame, face);
            WriteBox(face, *box);
            return 1;
        }

        int __cdecl ApiGetAnimBoxFrames(AmodMod mod, AmodAnimationId animation, int which)
        {
            const TrackedAnim* const tracked = Caller(mod) != nullptr ? AnimById(animation) : nullptr;
            const uint8_t* const animBox = tracked != nullptr ? AnimBoxOf(tracked->animation, which) : nullptr;
            return animBox != nullptr ? Read<int>(animBox, GameAddress::kAnimBoxFrameCountOffset) : 0;
        }

        int __cdecl ApiOverrideAnimation(AmodMod mod, const char* path, AmodAnimationId animation)
        {
            ModContext* const context = Caller(mod);
            const std::string wanted = Normalised(path);

            if (context == nullptr || AnimById(animation) == nullptr || wanted.empty() || g_originalLoadAnim == nullptr)
            {
                return 0;
            }

            for (Substitute& entry : g_animSubstitutes)
            {
                if (entry.path == wanted)
                {
                    entry.owner = context;
                    entry.id = animation;
                    return 1;
                }
            }

            Substitute entry;
            entry.owner = context;
            entry.path = wanted;
            entry.id = animation;
            g_animSubstitutes.push_back(entry);
            return 1;
        }

        int __cdecl ApiClearAnimationOverride(AmodMod mod, const char* path)
        {
            ModContext* const context = Caller(mod);
            const std::string wanted = Normalised(path);

            for (auto it = g_animSubstitutes.begin(); it != g_animSubstitutes.end(); ++it)
            {
                if (it->owner == context && it->path == wanted)
                {
                    g_animSubstitutes.erase(it);
                    return 1;
                }
            }
            return 0;
        }

        // moving what a model is made of

        uint8_t* FrameVertices(const uint8_t* model, int frame, int& count)
        {
            const uint8_t* const mesh = ModelMesh(model);
            const uint8_t* const at = mesh != nullptr ? MeshFrame(mesh, frame) : nullptr;
            if (at == nullptr)
            {
                return nullptr;
            }

            count = Read<int>(at, GameAddress::kMeshFrameVertexCountOffset);
            uint8_t* const vertices = reinterpret_cast<uint8_t*>(Read<uintptr_t>(at, GameAddress::kMeshFrameVerticesOffset));

            const size_t bytes = static_cast<size_t>(count) * GameAddress::kMeshVertexSize;
            return (vertices != nullptr && count > 0 && Readable(vertices, bytes)) ? vertices
                : nullptr;
        }

        // RebuildNormals after vertex moves.
        bool RebuildNormals(const uint8_t* model, int frame)
        {
            const uint8_t* const mesh = ModelMesh(model);
            const uint8_t* const at = mesh != nullptr ? MeshFrame(mesh, frame) : nullptr;

            int count = 0;
            uint8_t* const vertices = FrameVertices(model, frame, count);
            const uint16_t* const indices = at != nullptr
                ? reinterpret_cast<const uint16_t*>(Read<uintptr_t>(at, GameAddress::kMeshFrameIndicesOffset))
                : nullptr;
            const int total = at != nullptr ? Read<int>(at, GameAddress::kMeshFrameIndexCountOffset) : 0;

            if (vertices == nullptr || indices == nullptr || total < 3 ||
                !Readable(indices, static_cast<size_t>(total) * sizeof(uint16_t)))
            {
                return false;
            }

            std::vector<float> built(static_cast<size_t>(count) * 3, 0.0f);

            for (int corner = 0; corner + 2 < total; corner += 3)
            {
                const uint16_t a = indices[corner];
                const uint16_t b = indices[corner + 1];
                const uint16_t c = indices[corner + 2];

                if (a >= count || b >= count || c >= count)
                {
                    continue;
                }

                const float* const pa = reinterpret_cast<const float*>(vertices + static_cast<size_t>(a) * GameAddress::kMeshVertexSize);
                const float* const pb = reinterpret_cast<const float*>(vertices + static_cast<size_t>(b) * GameAddress::kMeshVertexSize);
                const float* const pc = reinterpret_cast<const float*>(vertices + static_cast<size_t>(c) * GameAddress::kMeshVertexSize);

                const float u[3] = { pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2] };
                const float v[3] = { pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2] };

                // Normals not unit-length: larger faces pull harder.
                const float face[3] =
                {
                    u[1] * v[2] - u[2] * v[1],
                    u[2] * v[0] - u[0] * v[2],
                    u[0] * v[1] - u[1] * v[0]
                };

                for (const uint16_t which : { a, b, c })
                {
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        built[static_cast<size_t>(which) * 3 + axis] += face[axis];
                    }
                }
            }

            for (int vertex = 0; vertex < count; ++vertex)
            {
                float* const normal = reinterpret_cast<float*>(vertices + static_cast<size_t>(vertex) * GameAddress::kMeshVertexSize) + 3;
                const float* const sum = &built[static_cast<size_t>(vertex) * 3];
                const float length = sqrtf(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);

                // Unused vertices keep their old normals.
                if (length > 0.000001f)
                {
                    normal[0] = sum[0] / length;
                    normal[1] = sum[1] / length;
                    normal[2] = sum[2] / length;
                }
            }
            return true;
        }

        int __cdecl ApiRebuildNormals(AmodMod mod, AmodModelId model, int frame)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;
            if (tracked == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no model %u to work the normals out for", model);
            }

            const uint8_t* const mesh = ModelMesh(tracked->model);
            const int frames = mesh != nullptr ? Read<int>(mesh, GameAddress::kMeshFrameCountOffset) : 0;

            if (frames <= 0)
            {
                return Fail(mod, AMOD_FAIL_NOT_DECLARED, "that model has no mesh of its own to work from");
            }

            int done = 0;
            for (int at = 0; at < frames; ++at)
            {
                if (frame < 0 || frame == at)
                {
                    done += RebuildNormals(tracked->model, at) ? 1 : 0;
                }
            }

            return done > 0 ? 1 : Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "frame %d is not one this model has", frame);
        }

        bool Inside(const AmodVertexFilter& filter, const float* position)
        {
            if (filter.kind == AMOD_VERTICES_IN_BOX)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (position[axis] < filter.min[axis] || position[axis] > filter.max[axis])
                    {
                        return false;
                    }
                }
                return true;
            }

            if (filter.kind == AMOD_VERTICES_IN_SPHERE)
            {
                float squared = 0.0f;
                for (int axis = 0; axis < 3; ++axis)
                {
                    const float away = position[axis] - filter.centre[axis];
                    squared += away * away;
                }
                return squared <= filter.radius * filter.radius;
            }
            return true;
        }

        void Pick(const uint8_t* vertices, int count, const AmodVertexFilter& filter, std::vector<uint16_t>& picked)
        {
            picked.clear();

            if (filter.kind == AMOD_VERTICES_LISTED)
            {
                for (int at = 0; at < filter.indexCount && filter.indices != nullptr; ++at)
                {
                    if (filter.indices[at] < count)
                    {
                        picked.push_back(filter.indices[at]);
                    }
                }
                return;
            }

            for (int at = 0; at < count; ++at)
            {
                const float* const position = reinterpret_cast<const float*>(vertices + static_cast<size_t>(at) * GameAddress::kMeshVertexSize);

                if (filter.kind == AMOD_VERTICES_ALL || Inside(filter, position))
                {
                    picked.push_back(static_cast<uint16_t>(at));
                }
            }
        }

        // Row-vector matrices (game layout).
        void Turn(const float* matrix, float* vector, bool move)
        {
            const float x = vector[0];
            const float y = vector[1];
            const float z = vector[2];

            vector[0] = x * matrix[0] + y * matrix[4] + z * matrix[8] + (move ? matrix[12] : 0.0f);
            vector[1] = x * matrix[1] + y * matrix[5] + z * matrix[9] + (move ? matrix[13] : 0.0f);
            vector[2] = x * matrix[2] + y * matrix[6] + z * matrix[10] + (move ? matrix[14] : 0.0f);
        }

        int __cdecl ApiGetVertex(AmodMod mod, AmodModelId model, int frame, int index, AmodVertex* out)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;
            if (tracked == nullptr || out == nullptr || index < 0)
            {
                return 0;
            }

            int count = 0;
            const uint8_t* const vertices = FrameVertices(tracked->model, frame < 0 ? 0 : frame, count);

            if (vertices != nullptr)
            {
                if (index >= count)
                {
                    return 0;
                }
                memcpy(out, vertices + static_cast<size_t>(index) * GameAddress::kMeshVertexSize, sizeof(AmodVertex));
                return 1;
            }

            const Geometry* const geometry = FileGeometryOf(tracked->model);
            const int at = frame < 0 ? 0 : frame;

            if (geometry == nullptr || index >= geometry->vertexCount || at >= geometry->frameCount)
            {
                return 0;
            }

            memcpy(out, geometry->vertices.data() + (static_cast<size_t>(at) * geometry->vertexCount + index) * GameAddress::kMeshVertexSize, sizeof(AmodVertex));
            return 1;
        }

        int __cdecl ApiSelectVertices(AmodMod mod, AmodModelId model, int frame, const AmodVertexFilter* filter, uint16_t* out, int max)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;
            if (tracked == nullptr || filter == nullptr || filter->structSize < sizeof(AmodVertexFilter))
            {
                return 0;
            }

            std::vector<uint16_t> picked;
            int count = 0;
            const uint8_t* const vertices = FrameVertices(tracked->model, frame < 0 ? 0 : frame, count);

            if (vertices != nullptr)
            {
                Pick(vertices, count, *filter, picked);
            }
            else
            {
                const Geometry* const geometry = FileGeometryOf(tracked->model);
                if (geometry == nullptr)
                {
                    return 0;
                }
                Pick(geometry->vertices.data(), geometry->vertexCount, *filter, picked);
            }

            const int wanted = (out != nullptr && max > 0)
                ? (max < static_cast<int>(picked.size())
                    ? max
                    : static_cast<int>(picked.size()))
                : 0;

            memcpy(out, picked.data(), static_cast<size_t>(wanted) * sizeof(uint16_t));
            return static_cast<int>(picked.size());
        }

        int __cdecl ApiTransformVertices(AmodMod mod, AmodModelId model, int frame, const AmodVertexFilter* filter, const float* matrix)
        {
            ModContext* const context = Caller(mod);
            Tracked* const tracked = context != nullptr ? TrackedById(model) : nullptr;

            Geometry geometry;
            if (tracked == nullptr || filter == nullptr || matrix == nullptr ||
                filter->structSize < sizeof(AmodVertexFilter) || !ReadGeometry(tracked->model, geometry) || frame >= geometry.frameCount)
            {
                return 0;
            }

            if (!EnsureSnapshot(*tracked, context))
            {
                return 0;
            }

            const size_t stride = GameAddress::kMeshVertexSize;
            const size_t perFrame = static_cast<size_t>(geometry.vertexCount) * stride;
            const int first = frame < 0 ? 0 : frame;
            const int last = frame < 0 ? geometry.frameCount - 1 : frame;

            std::vector<uint16_t> picked;
            for (int at = first; at <= last; ++at)
            {
                uint8_t* const vertices = geometry.vertices.data() + static_cast<size_t>(at) * perFrame;

                Pick(vertices, geometry.vertexCount, *filter, picked);

                for (const uint16_t index : picked)
                {
                    float* const position = reinterpret_cast<float*>(vertices + static_cast<size_t>(index) * stride);

                    Turn(matrix, position, true);
                    Turn(matrix, position + 3, false);

                    const float length = position[3] * position[3] + position[4] * position[4] + position[5] * position[5];
                    if (length > 0.0f)
                    {
                        const float scale = 1.0f / sqrtf(length);
                        position[3] *= scale;
                        position[4] *= scale;
                        position[5] *= scale;
                    }
                }
            }

            uint8_t* const mesh = BuildMesh(geometry);
            return (mesh != nullptr && InstallMesh(tracked->model, mesh)) ? 1 : 0;
        }

        // colour without a texture

        constexpr uintptr_t kBufferVertexStride = 0x24;
        constexpr uintptr_t kBufferDiffuseOffset = 0x18;
        constexpr uintptr_t kBufferLockVertices = 0x20;

        struct Painted
        {
            ModContext* owner;
            uint8_t* model;
            const void* buffer;             // what it was last written into
            std::vector<uint32_t> colours;
        };

        std::vector<Painted> g_painted;

        void* LockVertices(const uint8_t* model)
        {
            void* const buffer = reinterpret_cast<void*>(Read<uintptr_t>(model, GameAddress::kModelVertexBufferOffset));
            if (buffer == nullptr)
            {
                return nullptr;
            }

            void** const vtable = *static_cast<void***>(buffer);
            const auto lock = reinterpret_cast<void* (__thiscall*)(void*)>(vtable[kBufferLockVertices / sizeof(void*)]);

            return lock(buffer);
        }

        bool PaintVertices(const uint8_t* model, const std::vector<uint32_t>& colours)
        {
            if (!IsModel(model) || !Listed(model))
            {
                return false;
            }

            const int total = Read<int>(model, GameAddress::kModelVertexCountOffset);
            uint8_t* const vertices = static_cast<uint8_t*>(LockVertices(model));

            if (vertices == nullptr || total <= 0 || !Readable(vertices, static_cast<size_t>(total) * kBufferVertexStride))
            {
                return false;
            }

            for (int at = 0; at < total; ++at)
            {
                const uint32_t colour = at < static_cast<int>(colours.size()) ? colours[at] : 0xFFFFFFFFu;
                memcpy(vertices + static_cast<size_t>(at) * kBufferVertexStride + kBufferDiffuseOffset, &colour, sizeof(colour));
            }
            return true;
        }

        bool PaintModel(Painted& painted)
        {
            if (!PaintVertices(painted.model, painted.colours))
            {
                return false;
            }

            painted.buffer = reinterpret_cast<const void*>(Read<uintptr_t>(painted.model, GameAddress::kModelVertexBufferOffset));
            return true;
        }

        // Animated: recolour after lerp fills diffuse each draw.
        typedef void(__thiscall* DrawVerticesRoutine)(void* renderer, void* buffer);
        constexpr size_t kDrawVerticesSlot = GameAddress::kRenderDeviceDrawVertices / sizeof(void*);

        void** g_drawVtable = nullptr;
        DrawVerticesRoutine g_originalDrawVertices = nullptr;

        // Install draw watch when first colour is set.
        bool WatchDraw();

        void __fastcall DrawVerticesThunk(void* renderer, void* unused, void* buffer)
        {
            (void)unused;

            for (const Painted& painted : g_painted)
            {
                if (!painted.colours.empty() && IsModel(painted.model) &&
                    reinterpret_cast<void*>(Read<uintptr_t>(painted.model, GameAddress::kModelVertexBufferOffset)) == buffer)
                {
                    PaintVertices(painted.model, painted.colours);
                    break;
                }
            }

            if (g_originalDrawVertices != nullptr)
            {
                g_originalDrawVertices(renderer, buffer);
            }
        }

        void RepaintIfRebuilt(const uint8_t* model)
        {
            for (Painted& painted : g_painted)
            {
                if (painted.model != model)
                {
                    continue;
                }

                const void* const now = reinterpret_cast<const void*>(Read<uintptr_t>(model, GameAddress::kModelVertexBufferOffset));

                if (now != painted.buffer && now != nullptr)
                {
                    PaintModel(painted);
                }
                return;
            }
        }

        void RepaintAll(const uint8_t* model)
        {
            for (Painted& painted : g_painted)
            {
                if (painted.model == model)
                {
                    PaintModel(painted);
                }
            }
        }

        void ForgetPaint(const uint8_t* model)
        {
            for (auto it = g_painted.begin(); it != g_painted.end();)
            {
                it = it->model == model ? g_painted.erase(it) : ++it;
            }
        }

        Painted* PaintedOf(const uint8_t* model)
        {
            for (Painted& painted : g_painted)
            {
                if (painted.model == model)
                {
                    return &painted;
                }
            }
            return nullptr;
        }

        // The device methods the game's own draw uses, by their place in IDirect3DDevice8.
        typedef long(__stdcall* DeviceSetShaderRoutine)(void* device, uint32_t handle);
        typedef long(__stdcall* DeviceSetStreamRoutine)(void* device, uint32_t stream, void* buffer, uint32_t stride);
        typedef long(__stdcall* DeviceSetIndicesRoutine)(void* device, void* buffer, uint32_t baseVertex);
        typedef long(__stdcall* DeviceDrawIndexedRoutine)(void* device, uint32_t type, uint32_t minIndex, uint32_t vertexCount, uint32_t startIndex, uint32_t primitives);
        typedef long(__stdcall* DeviceSetTextureRoutine)(void* device, uint32_t stage, void* texture);
        typedef long(__stdcall* DeviceGetTextureRoutine)(void* device, uint32_t stage, void** texture);
        typedef unsigned long(__stdcall* DeviceReleaseRoutine)(void* object);

        constexpr size_t kDeviceRelease = 0x008;
        constexpr size_t kDeviceGetTexture = 0x0F0;
        constexpr size_t kDeviceSetTexture = 0x0F4;
        constexpr size_t kDeviceDrawIndexed = 0x11C;
        constexpr size_t kDeviceSetShader = 0x130;
        constexpr size_t kDeviceSetStream = 0x14C;
        constexpr size_t kDeviceSetIndices = 0x154;

        constexpr uint32_t kTriangleList = 4;

        template <typename T>
        T DeviceCall(void* device, size_t offset)
        {
            void** const vtable = *static_cast<void***>(device);
            return reinterpret_cast<T>(vtable[offset / sizeof(void*)]);
        }

        Tracked* TrackedByRecord(const uint8_t* record)
        {
            for (Tracked& entry : g_tracked)
            {
                if (entry.materials.empty() || entry.model == nullptr || !entry.resident)
                {
                    continue;
                }
                if (Read<uint8_t*>(entry.model, GameAddress::kModelVertexBufferOffset) == record)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        void* SurfaceOf(uint32_t texture)
        {
            const uint8_t* const object = static_cast<const uint8_t*>(TextureApi::TextureFor(texture));
            return object != nullptr
                ? Read<void*>(object, GameAddress::kTextureSurfaceOffset)
                : nullptr;
        }

        void __fastcall VerticesRenderDetour(uint8_t* record, void* unused, void* device)
        {
            Tracked* const tracked = record != nullptr ? TrackedByRecord(record) : nullptr;

            if (tracked == nullptr || device == nullptr ||
                Read<int>(record, GameAddress::kVertexRecordKindOffset) != 0)
            {
                g_originalVerticesRender(record, unused, device);
                return;
            }

            if (record[0x30] != 0 || record[0x31] != 0)
            {
                const VerticesUploadRoutine upload = Routine<VerticesUploadRoutine>(GameAddress::kVerticesUpload);

                if (upload == nullptr || upload(record) == 0)
                {
                    g_originalVerticesRender(record, unused, device);
                    return;
                }
            }

            void* const vertices = Read<void*>(record, GameAddress::kVertexRecordBufferOffset);
            void* const indices = Read<void*>(record, GameAddress::kVertexRecordIndicesOffset);
            const uint32_t vertexCount = Read<uint32_t>(record, GameAddress::kVertexRecordVertexCountOffset);

            if (vertices == nullptr || indices == nullptr)
            {
                g_originalVerticesRender(record, unused, device);
                return;
            }

            void* was = nullptr;
            DeviceCall<DeviceGetTextureRoutine>(device, kDeviceGetTexture)(device, 0, &was);

            DeviceCall<DeviceSetShaderRoutine>(device, kDeviceSetShader)(device, GameAddress::kModelVertexFvf);
            DeviceCall<DeviceSetStreamRoutine>(device, kDeviceSetStream)(device, 0, vertices, GameAddress::kModelVertexStride);
            DeviceCall<DeviceSetIndicesRoutine>(device, kDeviceSetIndices)(device, indices, 0);

            const DeviceSetTextureRoutine setTexture = DeviceCall<DeviceSetTextureRoutine>(device, kDeviceSetTexture);
            const DeviceDrawIndexedRoutine draw = DeviceCall<DeviceDrawIndexedRoutine>(device, kDeviceDrawIndexed);

            for (const Material& material : tracked->materials)
            {
                if (material.primitiveCount <= 0)
                {
                    continue;
                }

                setTexture(device, 0, SurfaceOf(material.texture));
                draw(device, kTriangleList, 0, vertexCount, static_cast<uint32_t>(material.firstIndex), static_cast<uint32_t>(material.primitiveCount));
            }

            // Whatever Model_DrawNode bound is put back; the game draws on assuming it is there.
            setTexture(device, 0, was);
            if (was != nullptr)
            {
                DeviceCall<DeviceReleaseRoutine>(was, kDeviceRelease)(was);
            }
        }

        void __cdecl RenderAnimatedDetour(uint8_t* model, int from, int to, float blend)
        {
            const Painted* const painted = IsModel(model) ? PaintedOf(model) : nullptr;
            const uint8_t* const mesh = painted != nullptr ? ModelMesh(model) : nullptr;
            void* const device = RenderDevice();

            const int frames = mesh != nullptr
                ? Read<int>(mesh, GameAddress::kMeshFrameCountOffset)
                : 0;

            if (painted == nullptr || mesh == nullptr || device == nullptr || frames <= 0 ||
                Read<int>(model, GameAddress::kModelKindOffset) != GameAddress::kModelKindAnimated)
            {
                if (g_originalRenderAnimated != nullptr)
                {
                    g_originalRenderAnimated(model, from, to, blend);
                }
                return;
            }

            const uint8_t* const first = MeshFrame(mesh, from % frames);
            const uint8_t* const second = MeshFrame(mesh, to % frames);
            float* out = static_cast<float*>(LockVertices(model));
            const int count = Read<int>(mesh, 0x04);   // the working copy's own vertex count

            if (first == nullptr || second == nullptr || out == nullptr || count <= 0)
            {
                if (g_originalRenderAnimated != nullptr)
                {
                    g_originalRenderAnimated(model, from, to, blend);
                }
                return;
            }

            const float* a = reinterpret_cast<const float*>(Read<uintptr_t>(first, GameAddress::kMeshFrameVerticesOffset));
            const float* b = reinterpret_cast<const float*>(Read<uintptr_t>(second, GameAddress::kMeshFrameVerticesOffset));

            const size_t bytes = static_cast<size_t>(count) * GameAddress::kMeshVertexSize;
            if (a == nullptr || b == nullptr || !Readable(a, bytes) || !Readable(b, bytes))
            {
                if (g_originalRenderAnimated != nullptr)
                {
                    g_originalRenderAnimated(model, from, to, blend);
                }
                return;
            }

            for (int at = 0; at < count; ++at)
            {
                for (int part = 0; part < 6; ++part)      // position and normal
                {
                    out[part] = a[part] + (b[part] - a[part]) * blend;
                }

                const uint32_t colour = at < static_cast<int>(painted->colours.size())
                    ? painted->colours[at]
                    : 0xFFFFFFFFu;
                memcpy(&out[6], &colour, sizeof(colour));

                out[7] = a[6];                            // the texture corner, from the first
                out[8] = a[7];

                out += 9;
                a += 8;
                b += 8;
            }

            void** const vtable = *static_cast<void***>(device);
            const auto draw = reinterpret_cast<void(__thiscall*)(void*, void*)>(vtable[GameAddress::kRenderDeviceDrawVertices / sizeof(void*)]);

            draw(device, reinterpret_cast<void*>(Read<uintptr_t>(model, GameAddress::kModelVertexBufferOffset)));
        }

        int __cdecl ApiSetColours(AmodMod mod, AmodModelId model, const uint32_t* colours, int count)
        {
            ModContext* const context = Caller(mod);
            const Tracked* const tracked = context != nullptr ? TrackedById(model) : nullptr;
            if (tracked == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "there is no model %u to colour", model);
            }

            ForgetPaint(tracked->model);

            if (colours == nullptr || count <= 0)
            {

                Painted white;
                white.owner = context;
                white.model = tracked->model;
                white.buffer = nullptr;
                return PaintModel(white) ? 1 : 0;
            }

            // Recolour animated models every draw.
            if (Read<int>(tracked->model, GameAddress::kModelKindOffset) == AMOD_MODEL_KIND_ANIMATED && !WatchDraw())
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the renderer is not up yet, so an animated model cannot be coloured");
            }

            Painted painted;
            painted.owner = context;
            painted.model = tracked->model;
            painted.buffer = nullptr;
            painted.colours.assign(colours, colours + count);
            g_painted.push_back(painted);

            return PaintModel(g_painted.back()) ? 1 : 0;
        }

        int __cdecl ApiClearColours(AmodMod mod, AmodModelId model)
        {
            return ApiSetColours(mod, model, nullptr, 0);
        }

        // what an entity is playing

        uint8_t* EntityAnimState(int roomIndex, int index)
        {
            uint8_t* const entity = WorldApi::LiveEntityAt(roomIndex, index);
            return (entity != nullptr && Readable(entity, GameAddress::kEntitySize))
                ? entity + GameAddress::kEntityAnimStateOffset
                : nullptr;
        }

        int __cdecl ApiGetEntityAnim(AmodMod mod, int roomIndex, int index, AmodAnimState* out)
        {
            const uint8_t* const state = Caller(mod) != nullptr ? EntityAnimState(roomIndex, index) : nullptr;
            return state != nullptr ? MmdApi::ReadAnimState(state, out) : 0;
        }

        int PlayOn(AmodMod mod, int roomIndex, int index, const char* frameset)
        {
            uint8_t* const state = Caller(mod) != nullptr ? EntityAnimState(roomIndex, index) : nullptr;
            const AnimPlayRoutine play = Routine<AnimPlayRoutine>(GameAddress::kAnimPlay);

            if (state == nullptr || play == nullptr)
            {
                return 0;
            }

            uint8_t* const animation = reinterpret_cast<uint8_t*>(Read<uintptr_t>(state, GameAddress::kAnimStateAnimationOffset));
            if (!IsAnimation(animation))
            {
                return 0;
            }

            float position[3];
            memcpy(position, state + GameAddress::kAnimStatePositionOffset, sizeof(position));

            play(animation, frameset, state, position, 0);
            return 1;
        }

        int __cdecl ApiPlayEntityAnim(AmodMod mod, int roomIndex, int index, const char* frameset)
        {
            return (frameset != nullptr && frameset[0] != '\0')
                ? PlayOn(mod, roomIndex, index, frameset)
                : 0;
        }

        int __cdecl ApiStopEntityAnim(AmodMod mod, int roomIndex, int index)
        {
            return PlayOn(mod, roomIndex, index, nullptr);
        }

        // hanging one model off another

        struct DrawHandler
        {
            uint32_t id;
            ModContext* owner;
            uint8_t* model;
            AmodModelDrawHandler handler;
            void* user;

            bool before;
        };

        std::vector<DrawHandler> g_drawHandlers;
        uint32_t g_nextDrawHandlerId = 1;

        int g_drawDepth = 0;

        typedef void(__thiscall* SetTextureRoutine)(void* device, uint32_t texture);
        typedef void(__thiscall* MultiplyWorldRoutine)(void* device, const float* matrix);

        void* RenderDevice()
        {
            void** const at = static_cast<void**>(GameModule::Resolve(GameAddress::kRenderDevice));
            return at != nullptr ? *at : nullptr;
        }

        constexpr size_t kSetTextureSlot = GameAddress::kRenderDeviceSetTexture / sizeof(void*);

        void** g_deviceVtable = nullptr;
        SetTextureRoutine g_originalSetTexture = nullptr;
        uint32_t g_currentTexture = 0;
        bool g_textureSeen = false;

        void __fastcall SetTextureThunk(void* device, void* unused, uint32_t texture)
        {
            (void)unused;
            g_currentTexture = texture;
            g_textureSeen = true;
            if (g_originalSetTexture != nullptr)
            {
                g_originalSetTexture(device, texture);
            }
        }

        bool WriteVtableSlot(void** vtable, size_t slot, void* value)
        {
            DWORD previous = 0;
            if (VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &previous) == 0)
            {
                return false;
            }

            vtable[slot] = value;

            DWORD ignored = 0;
            VirtualProtect(&vtable[slot], sizeof(void*), previous, &ignored);
            return true;
        }

        bool WatchTexture()
        {
            if (g_originalSetTexture != nullptr)
            {
                return true;
            }

            void* const device = RenderDevice();
            if (device == nullptr)
            {
                return false;
            }

            void** const vtable = *static_cast<void***>(device);
            if (vtable == nullptr || !Readable(&vtable[kSetTextureSlot], sizeof(void*)))
            {
                return false;
            }

            const SetTextureRoutine original = reinterpret_cast<SetTextureRoutine>(vtable[kSetTextureSlot]);

            if (original == nullptr || !WriteVtableSlot(vtable, kSetTextureSlot, reinterpret_cast<void*>(&SetTextureThunk)))
            {
                return false;
            }

            g_deviceVtable = vtable;
            g_originalSetTexture = original;
            FORGE_LOG_INFO("Watching the texture the renderer is on, so a mod's own draw can put "
                "it back");
            return true;
        }

        // Vtable slot swap on DrawVertices (filled buffer to GPU).
        bool WatchDraw()
        {
            if (g_originalDrawVertices != nullptr)
            {
                return true;
            }

            void* const device = RenderDevice();
            void** const vtable = device != nullptr ? *static_cast<void***>(device) : nullptr;

            if (vtable == nullptr || !Readable(&vtable[kDrawVerticesSlot], sizeof(void*)))
            {
                return false;
            }

            const DrawVerticesRoutine original = reinterpret_cast<DrawVerticesRoutine>(vtable[kDrawVerticesSlot]);
            if (original == nullptr || !WriteVtableSlot(vtable, kDrawVerticesSlot, reinterpret_cast<void*>(&DrawVerticesThunk)))
            {
                return false;
            }

            g_drawVtable = vtable;
            g_originalDrawVertices = original;
            FORGE_LOG_INFO("Watching the buffer draw, so an animated model can be coloured after "
                "the game has filled it in");
            return true;
        }

        void UnwatchDraw()
        {
            if (g_drawVtable != nullptr && g_originalDrawVertices != nullptr)
            {
                WriteVtableSlot(g_drawVtable, kDrawVerticesSlot, reinterpret_cast<void*>(g_originalDrawVertices));
            }
            g_drawVtable = nullptr;
            g_originalDrawVertices = nullptr;
        }

        void UnwatchTexture()
        {
            if (g_deviceVtable != nullptr && g_originalSetTexture != nullptr)
            {
                WriteVtableSlot(g_deviceVtable, kSetTextureSlot, reinterpret_cast<void*>(g_originalSetTexture));
            }
            g_deviceVtable = nullptr;
            g_originalSetTexture = nullptr;
            g_currentTexture = 0;
            g_textureSeen = false;
        }

        bool Invert(const float* matrix, float* out)
        {
            float work[4][8] = {};
            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 4; ++column)
                {
                    work[row][column] = matrix[row * 4 + column];
                }
                work[row][4 + row] = 1.0f;
            }

            for (int column = 0; column < 4; ++column)
            {
                int pivot = column;
                for (int row = column + 1; row < 4; ++row)
                {
                    if (fabsf(work[row][column]) > fabsf(work[pivot][column]))
                    {
                        pivot = row;
                    }
                }

                if (fabsf(work[pivot][column]) < 1.0e-12f)
                {
                    return false;
                }

                for (int at = 0; at < 8; ++at)
                {
                    const float kept = work[column][at];
                    work[column][at] = work[pivot][at];
                    work[pivot][at] = kept;
                }

                const float lead = 1.0f / work[column][column];
                for (int at = 0; at < 8; ++at)
                {
                    work[column][at] *= lead;
                }

                for (int row = 0; row < 4; ++row)
                {
                    if (row == column)
                    {
                        continue;
                    }
                    const float factor = work[row][column];
                    for (int at = 0; at < 8; ++at)
                    {
                        work[row][at] -= factor * work[column][at];
                    }
                }
            }

            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 4; ++column)
                {
                    out[row * 4 + column] = work[row][4 + column];
                }
            }
            return true;
        }

        void FireDrawHandlers(uint8_t* model, int frameA, int frameB, float blend, const uint8_t* node, bool before)
        {
            if (g_drawDepth != 0 || g_drawHandlers.empty() || !IsModel(model))
            {
                return;
            }

            bool any = false;
            for (const DrawHandler& entry : g_drawHandlers)
            {
                any = any || (entry.model == model && entry.before == before && entry.handler != nullptr);
            }
            if (!any)
            {
                return;
            }

            AmodModelDrawInfo info = {};
            info.structSize = sizeof(info);
            info.frameA = frameA;
            info.frameB = frameB;
            info.blend = blend;

            if (node != nullptr)
            {
                info.item = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
                info.texture = Read<uint32_t>(node, GameAddress::kDrawNodeTextureOffset);
                memcpy(info.matrix, node + GameAddress::kDrawNodeMatrixOffset, sizeof(info.matrix));
                memcpy(info.position, node + GameAddress::kDrawNodeMatrixOffset + 12 * sizeof(float), sizeof(info.position));
            }

            ++g_drawDepth;
            const std::vector<DrawHandler> firing = g_drawHandlers;
            for (const DrawHandler& entry : firing)
            {
                if (entry.model == model && entry.before == before && entry.handler != nullptr)
                {
                    info.model = Track(model, nullptr);
                    entry.handler(&info, entry.user);
                }
            }
            --g_drawDepth;
        }

        void __stdcall ModelDrawNodeDetour(uint8_t* node)
        {
            // Read node before draw so pre/post handlers share the same numbers.
            const bool readable = Readable(node, GameAddress::kDrawNodeNextOffset);
            uint8_t* const model = readable ? reinterpret_cast<uint8_t*>(Read<uintptr_t>(node, GameAddress::kDrawNodeModelOffset)) : nullptr;
            const bool animated = readable && (node[GameAddress::kDrawNodeFlagsOffset] & GameAddress::kDrawNodeIsAnimated) != 0;
            const int frameA = animated ? Read<int>(node, GameAddress::kDrawNodeFrameAOffset) : -1;
            const int frameB = animated ? Read<int>(node, GameAddress::kDrawNodeFrameBOffset) : -1;
            const float blend = animated ? Read<float>(node, GameAddress::kDrawNodeBlendOffset) : 0.0f;

            if (readable)
            {
                g_currentTexture = Read<uint32_t>(node, GameAddress::kDrawNodeTextureOffset);
                g_textureSeen = true;

                FireDrawHandlers(model, frameA, frameB, blend, node, true);
            }

            if (g_originalDrawNode != nullptr)
            {
                ++g_drawDepth;
                g_originalDrawNode(node);
                --g_drawDepth;
            }

            if (!readable)
            {
                return;
            }

            RepaintIfRebuilt(model);
            FireDrawHandlers(model, frameA, frameB, blend, node, false);
        }

        int __cdecl ApiDrawWith(AmodMod mod, AmodModelId model, uint32_t texture, const float* matrix)
        {
            const Tracked* const tracked = Caller(mod) != nullptr ? TrackedById(model) : nullptr;
            void* const device = RenderDevice();
            const RenderRoutine render = Routine<RenderRoutine>(GameAddress::kModelRender);

            float inverse[16];
            if (tracked == nullptr || device == nullptr || render == nullptr || matrix == nullptr || g_drawDepth == 0 || !Invert(matrix, inverse))
            {
                return 0;
            }

            WatchTexture();

            void** const vtable = *static_cast<void***>(device);
            const auto multiply = reinterpret_cast<MultiplyWorldRoutine>(vtable[GameAddress::kRenderDeviceMultiplyWorld / sizeof(void*)]);
            const auto setTexture = reinterpret_cast<SetTextureRoutine>(vtable[kSetTextureSlot]);

            const bool watched = g_textureSeen;
            const uint32_t was = g_currentTexture;

            multiply(device, matrix);
            setTexture(device, texture);
            render(tracked->model);
            multiply(device, inverse);

            if (watched)
            {
                setTexture(device, was);
            }
            return 1;
        }

        void ReleaseDrawHandler(void* payload)
        {
            const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload));
            for (auto it = g_drawHandlers.begin(); it != g_drawHandlers.end(); ++it)
            {
                if (it->id == id)
                {
                    g_drawHandlers.erase(it);
                    return;
                }
            }
        }

        // Move paint and handlers onto the replacement model.
        void CarryOverDrawings(const uint8_t* was, uint8_t* fresh)
        {
            for (Painted& painted : g_painted)
            {
                if (painted.model == was)
                {
                    painted.model = fresh;
                    painted.buffer = nullptr;
                    PaintModel(painted);
                }
            }

            for (DrawHandler& handler : g_drawHandlers)
            {
                if (handler.model == was)
                {
                    handler.model = fresh;
                }
            }
        }

        // Shared add for pre/post draw; only the before flag differs.
        AmodToken AddDrawHandler(AmodMod mod, AmodModelId model, AmodModelDrawHandler handler, void* user, bool before)
        {
            ModContext* const context = Caller(mod);
            const Tracked* const tracked = context != nullptr ? TrackedById(model) : nullptr;

            if (tracked == nullptr || handler == nullptr || g_originalDrawNode == nullptr)
            {
                Fail(mod, handler == nullptr ? AMOD_FAIL_BAD_ARGUMENT : AMOD_FAIL_NO_SUCH,
                    "there is no model %u to watch being drawn", model);
                return AMOD_INVALID_TOKEN;
            }

            WatchTexture();

            DrawHandler entry;
            entry.id = g_nextDrawHandlerId++;
            entry.owner = context;
            entry.model = tracked->model;
            entry.handler = handler;
            entry.user = user;
            entry.before = before;
            g_drawHandlers.push_back(entry);

            return context->owned.Add(&ReleaseDrawHandler, reinterpret_cast<void*>(static_cast<uintptr_t>(entry.id)));
        }

        AmodToken __cdecl ApiAddDrawHandler(AmodMod mod, AmodModelId model, AmodModelDrawHandler handler, void* user)
        {
            return AddDrawHandler(mod, model, handler, user, false);
        }

        AmodToken __cdecl ApiAddPreDrawHandler(AmodMod mod, AmodModelId model, AmodModelDrawHandler handler, void* user)
        {
            return AddDrawHandler(mod, model, handler, user, true);
        }

        void __cdecl ApiRemoveDrawHandler(AmodMod mod, AmodToken token)
        {
            ModContext* const context = Caller(mod);
            if (context != nullptr)
            {
                context->owned.Remove(token);
            }
        }

        bool InstallRenderHooks()
        {
            void* const node = GameSymbols::Address(GameSymbol::ModelDrawNode);
            if (node == nullptr)
            {
                return false;
            }

            std::string error;
            if (!g_drawNodeHook.Install(node, reinterpret_cast<void*>(&ModelDrawNodeDetour), kModelDrawNodePrologue, sizeof(kModelDrawNodePrologue), error))
            {
                FORGE_LOG_ERROR("Cannot hook Model_DrawNode: %s", error.c_str());
                return false;
            }

            g_originalDrawNode = reinterpret_cast<DrawNodeRoutine>(g_drawNodeHook.Trampoline());

            void* const animated = GameSymbols::Address(GameSymbol::ModelRenderAnimated);
            if (animated != nullptr && g_renderAnimHook.Install(animated, reinterpret_cast<void*>(&RenderAnimatedDetour), kModelRenderAnimatedPrologue, 
                sizeof(kModelRenderAnimatedPrologue), error))
            {
                g_originalRenderAnimated = reinterpret_cast<RenderAnimatedRoutine>(g_renderAnimHook.Trampoline());
            }
            else
            {
                FORGE_LOG_WARN("Model_RenderAnimated is not hooked, so an animated model cannot be "
                    "coloured: %s", error.c_str());
            }

            std::string mismatch;
            void* const vertices = GameModule::Resolve(GameAddress::kVerticesRender);
            if (vertices != nullptr && GameModule::VerifyBytes(vertices, kVerticesRenderPrologue, sizeof(kVerticesRenderPrologue), mismatch) &&
                g_verticesHook.Install(vertices, reinterpret_cast<void*>(&VerticesRenderDetour), kVerticesRenderPrologue, sizeof(kVerticesRenderPrologue), error))
            {
                g_originalVerticesRender = reinterpret_cast<VerticesRenderRoutine>(g_verticesHook.Trampoline());
            }
            else
            {
                FORGE_LOG_WARN("Vertices_Render is not hooked, so a model wears one texture: %s",
                    error.c_str());
            }
            return true;
        }
    }

    bool MmdApi::Install()
    {
        g_table.structSize = sizeof(AmodMmdApi);
        g_table.GetCount = &ApiGetCount;
        g_table.GetAt = &ApiGetAt;
        g_table.Find = &ApiFind;
        g_table.Get = &ApiGet;
        g_table.IsValid = &ApiIsValid;
        g_table.Load = &ApiLoad;
        g_table.Release = &ApiRelease;
        g_table.ReadVertices = &ApiReadVertices;
        g_table.ReadIndices = &ApiReadIndices;
        g_table.WriteVertices = &ApiWriteVertices;
        g_table.WriteIndices = &ApiWriteIndices;
        g_table.Create = &ApiCreate;
        g_table.Replace = &ApiReplace;
        g_table.Restore = &ApiRestore;
        g_table.Save = &ApiSave;
        g_table.SetColours = &ApiSetColours;
        g_table.ClearColours = &ApiClearColours;
        g_table.GetRoomModel = &ApiGetRoomModel;
        g_table.SetRoomModel = &ApiSetRoomModel;
        g_table.Draw = &ApiDraw;
        g_table.DrawFrames = &ApiDrawFrames;
        g_table.GetProperty = &ApiGetProperty;
        g_table.SetProperty = &ApiSetProperty;
        g_table.HasProperty = &ApiHasProperty;
        g_table.PropertyCount = &ApiPropertyCount;
        g_table.PropertyNameAt = &ApiPropertyNameAt;

        g_table.ResolveTexture = &ApiResolveTexture;
        g_table.GetViewMatrix = &ApiGetViewMatrix;
        g_table.GetProjectionMatrix = &ApiGetProjectionMatrix;
        g_table.GetAnimationCount = &ApiGetAnimationCount;
        g_table.GetAnimationAt = &ApiGetAnimationAt;
        g_table.FindAnimation = &ApiFindAnimation;
        g_table.GetAnimation = &ApiGetAnimation;
        g_table.GetFrameset = &ApiGetFrameset;
        g_table.LoadAnimation = &ApiLoadAnimation;
        g_table.ReleaseAnimation = &ApiReleaseAnimation;
        g_table.EnsureAnimBox = &ApiEnsureAnimBox;
        g_table.RebuildNormals = &ApiRebuildNormals;
        g_table.AddPreDrawHandler = &ApiAddPreDrawHandler;
        g_table.GetAnimBox = &ApiGetAnimBox;
        g_table.SetAnimBox = &ApiSetAnimBox;
        g_table.GetAnimBoxFrames = &ApiGetAnimBoxFrames;
        g_table.GetVertex = &ApiGetVertex;
        g_table.SelectVertices = &ApiSelectVertices;
        g_table.TransformVertices = &ApiTransformVertices;
        g_table.GetEntityAnim = &ApiGetEntityAnim;
        g_table.PlayEntityAnim = &ApiPlayEntityAnim;
        g_table.StopEntityAnim = &ApiStopEntityAnim;

        if (InstallHooks())
        {
            g_table.Override = &ApiOverride;
            g_table.ClearOverride = &ApiClearOverride;
        }

        if (InstallRenderHooks())
        {
            g_table.AddDrawHandler = &ApiAddDrawHandler;
            g_table.RemoveDrawHandler = &ApiRemoveDrawHandler;
            g_table.Cancel = &ApiRemoveDrawHandler;
            g_table.AddMaterial = &ApiAddMaterial;
            g_table.MaterialCount = &ApiMaterialCount;
            g_table.GetMaterial = &ApiGetMaterial;
            g_table.ClearMaterials = &ApiClearMaterials;
            g_table.DrawWith = &ApiDrawWith;
        }

        if (InstallDrawHooks())
        {
            g_table.Enqueue = &ApiEnqueue;
            g_table.QueueRoom = &ApiQueueRoom;
            g_table.OverrideAnimation = &ApiOverrideAnimation;
            g_table.ClearAnimationOverride = &ApiClearAnimationOverride;
            FORGE_LOG_INFO("The scene's draw list is a mod's to fill, %d slot(s)", static_cast<int>(GameAddress::kSceneDrawCapacity));
        }

        return true;
    }

    void MmdApi::Uninstall()
    {
        g_loadHook.Uninstall();
        g_releaseHook.Uninstall();
        g_drawHook.Uninstall();
        g_loadAnimHook.Uninstall();
        g_drawNodeHook.Uninstall();
        g_renderAnimHook.Uninstall();
        g_verticesHook.Uninstall();
        g_originalVerticesRender = nullptr;
        g_originalDrawNode = nullptr;
        g_originalRenderAnimated = nullptr;
        ForgetFileGeometry(nullptr);
        UnwatchTexture();
        UnwatchDraw();
        g_originalLoad = nullptr;
        g_originalRelease = nullptr;
        g_originalDraw = nullptr;
        g_originalLoadAnim = nullptr;
        g_drawHandlers.clear();
        g_drawDepth = 0;

        g_tracked.clear();
        g_held.clear();
        g_substitutes.clear();
        g_roomSwaps.clear();
        g_animations.clear();
        g_heldAnims.clear();
        g_animSubstitutes.clear();
        g_queued.clear();
        g_textures.clear();
        g_painted.clear();
        g_animBoxSnapshots.clear();
        g_animBoxesMade.clear();
        g_table = AmodMmdApi{};
    }

    const AmodMmdApi* MmdApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    // The anim state is entity+0x4A0 whoever asks for it, so monsters read it through here too.
    int MmdApi::ReadAnimState(const uint8_t* state, AmodAnimState* out)
    {
        if (state == nullptr || out == nullptr)
        {
            return 0;
        }

        uint8_t* const animation = reinterpret_cast<uint8_t*>(Read<uintptr_t>(state, GameAddress::kAnimStateAnimationOffset));
        const uint8_t* const frameset = reinterpret_cast<const uint8_t*>(Read<uintptr_t>(state, GameAddress::kAnimStateFramesetOffset));
        const uint32_t flags = Read<uint32_t>(state, GameAddress::kAnimStateFlagsOffset);

        AmodAnimState filled = {};
        filled.structSize = sizeof(filled);
        filled.animation = IsAnimation(animation) ? TrackAnim(animation) : AMOD_INVALID_ANIMATION;
        filled.frameset = (frameset != nullptr && Readable(frameset, GameAddress::kFramesetStride))
            ? ReadText(frameset, GameAddress::kFramesetNameOffset)
            : "";
        filled.frameA = Read<int>(state, GameAddress::kAnimStateFrameAOffset);
        filled.frameB = Read<int>(state, GameAddress::kAnimStateFrameBOffset);
        filled.blend = Read<float>(state, GameAddress::kAnimStateBlendOffset);
        filled.speed = Read<float>(state, GameAddress::kAnimStateSpeedOffset);
        filled.backwards = Read<int>(state, GameAddress::kAnimStateDirectionOffset) < 0 ? 1 : 0;
        filled.stopped = (flags & GameAddress::kAnimFlagStopped) != 0 ? 1 : 0;
        filled.finished = (flags & GameAddress::kAnimFlagFinished) != 0 ? 1 : 0;

        return CopyOut(out, out->structSize, &filled, sizeof(filled));
    }

    AmodModelId MmdApi::IdFor(void* model)
    {
        return Track(static_cast<uint8_t*>(model), nullptr);
    }

    void* MmdApi::ModelFor(AmodModelId id)
    {
        Tracked* const tracked = TrackedById(id);
        return tracked != nullptr ? tracked->model : nullptr;
    }

    void* MmdApi::AnimationFor(AmodAnimationId id)
    {
        TrackedAnim* const tracked = AnimById(id);
        return tracked != nullptr ? tracked->animation : nullptr;
    }

    void MmdApi::ForgetMod(ModContext* owner)
    {
        // A model the mod cut into materials goes back to being drawn whole.
        for (Tracked& tracked : g_tracked)
        {
            if (tracked.materialsOwner == owner)
            {
                tracked.materials.clear();
                tracked.materialsOwner = nullptr;
            }
        }

        for (auto it = g_substitutes.begin(); it != g_substitutes.end();)
        {
            it = it->owner == owner ? g_substitutes.erase(it) : ++it;
        }
        for (auto it = g_animSubstitutes.begin(); it != g_animSubstitutes.end();)
        {
            it = it->owner == owner ? g_animSubstitutes.erase(it) : ++it;
        }
        for (auto it = g_queued.begin(); it != g_queued.end();)
        {
            it = it->owner == owner ? g_queued.erase(it) : ++it;
        }
        for (auto it = g_drawHandlers.begin(); it != g_drawHandlers.end();)
        {
            it = it->owner == owner ? g_drawHandlers.erase(it) : ++it;
        }

        for (auto it = g_painted.begin(); it != g_painted.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            Painted white;
            white.owner = nullptr;
            white.model = it->model;
            white.buffer = nullptr;
            PaintModel(white);
            it = g_painted.erase(it);
        }

        for (auto it = g_roomSwaps.begin(); it != g_roomSwaps.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            if (it->room != nullptr && Readable(it->room + RoomModelOffset(it->slot), sizeof(uintptr_t)) && Read<uintptr_t>(it->room, 
                RoomModelOffset(it->slot)) == it->applied)
            {
                WritePointer(it->room, RoomModelOffset(it->slot), reinterpret_cast<const void*>(it->was));
                GiveBackRoomModel(it->applied);
            }
            it = g_roomSwaps.erase(it);
        }

        for (Tracked& tracked : g_tracked)
        {
            if (tracked.editor == owner)
            {
                RestoreSnapshot(tracked);
            }
        }

        const ReleaseModelRoutine release = g_originalRelease != nullptr ? g_originalRelease
            : Routine<ReleaseModelRoutine>(GameAddress::kModelRelease);

        std::vector<uint32_t> giveBack;
        for (auto it = g_held.begin(); it != g_held.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }
            giveBack.push_back(it->id);
            it = g_held.erase(it);
        }

        for (const uint32_t id : giveBack)
        {
            const Tracked* const tracked = release != nullptr ? TrackedById(id) : nullptr;
            if (tracked == nullptr)
            {
                continue;
            }

            uint8_t* const target = tracked->model;
            if (Read<int>(target, GameAddress::kModelReferencesOffset) == 0)
            {
                Forget(target);
            }
            release(target);
        }

        for (auto it = g_animBoxSnapshots.begin(); it != g_animBoxSnapshots.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            uint8_t* const animBox = IsAnimation(it->animation) && AnimListed(it->animation) ? AnimBoxOf(it->animation, it->which) : nullptr;
            uint8_t* const face = animBox != nullptr ? AnimBoxFace(animBox, it->frame) : nullptr;
            if (face != nullptr)
            {
                memcpy(face, it->bytes, sizeof(it->bytes));
            }
            it = g_animBoxSnapshots.erase(it);
        }

        // Restore anim-box bytes before freeing arrays that hold them.
        for (auto it = g_animBoxesMade.begin(); it != g_animBoxesMade.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }

            const bool alive = IsAnimation(it->animation) && AnimListed(it->animation);
            uint8_t* const animBox = alive ? AnimBoxOf(it->animation, it->which) : nullptr;

            if (animBox != nullptr && it->madeFrames != nullptr &&
                reinterpret_cast<uint8_t*>(Read<uintptr_t>(animBox, GameAddress::kAnimBoxFramesOffset)) == it->madeFrames)
            {
                WritePointer(animBox, GameAddress::kAnimBoxFramesOffset, it->wasFrames);
                WriteInt(animBox, GameAddress::kAnimBoxFrameCountOffset, it->wasCount);
            }
            if (it->madeFrames != nullptr)
            {
                GameFunctions::Free(it->madeFrames);
            }

            if (it->createdSet && animBox != nullptr)
            {
                WritePointer(it->animation, AnimBoxSlot(it->which), nullptr);
                GameFunctions::Free(animBox);
            }
            it = g_animBoxesMade.erase(it);
        }

        std::vector<uint32_t> animsBack;
        for (auto it = g_heldAnims.begin(); it != g_heldAnims.end();)
        {
            if (it->owner != owner)
            {
                ++it;
                continue;
            }
            animsBack.push_back(it->id);
            it = g_heldAnims.erase(it);
        }

        for (const uint32_t id : animsBack)
        {
            const TrackedAnim* const tracked = AnimById(id);
            if (tracked != nullptr)
            {
                ReleaseAnimation(tracked->animation);
            }
        }

        std::vector<uint32_t> built;
        for (const Tracked& tracked : g_tracked)
        {
            if (tracked.creator == owner)
            {
                built.push_back(tracked.id);
            }
        }

        for (const uint32_t id : built)
        {
            Tracked* const tracked = release != nullptr ? TrackedById(id) : nullptr;
            if (tracked == nullptr)
            {
                continue;
            }

            uint8_t* const target = tracked->model;
            tracked->creator = nullptr;

            if (Read<int>(target, GameAddress::kModelReferencesOffset) == 0)
            {
                Forget(target);
            }
            release(target);
        }
    }
}
