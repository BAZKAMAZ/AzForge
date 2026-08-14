#include <windows.h>

#include "src/Api/FilesApi.h"
#include "src/Api/ApiRead.h"
#include "src/Api/ModContext.h"
#include "src/Core/ForgeLog.h"
#include "src/Game/GameAddresses.h"
#include "src/Game/GameModule.h"
#include "src/Game/PackageFile.h"

#include <string.h>
#include <string>

namespace Forge
{
    namespace
    {
        using namespace ApiRead;

        AmodFilesApi g_table = {};

        // BOTH END IN RET 4, so both are __stdcall: the callee cleans, and the one argument comes
        // off the stack, not out of ECX. Package_Free was declared __cdecl here, which had this
        // side cleaning the stack as well - four bytes of drift, and a runtime check that says so.
        typedef void* (__stdcall* PackageOpenRoutine)(const char* path);
        typedef void(__stdcall* PackageFreeRoutine)(void* file);

        // One path a mod is answering for. The bytes live here and nowhere on disk: unloading the
        // mod takes its answers with it, so the game's own files are never any different for it.
        struct Answer
        {
            ModContext* owner;
            std::string path;
            std::vector<uint8_t> bytes;
        };

        std::vector<Answer> g_answers;

        // Room files come in as "levels\orange\002.room" one time and "levels/orange/002.room"
        // the next, and the game does not care which. Neither should this.
        std::string Flattened(const char* path)
        {
            std::string out = path != nullptr ? path : "";

            for (char& letter : out)
            {
                if (letter == '\\')
                {
                    letter = '/';
                }
                if (letter >= 'A' && letter <= 'Z')
                {
                    letter = static_cast<char>(letter - 'A' + 'a');
                }
            }

            return out;
        }

        Answer* Standing(const std::string& path)
        {
            // Last served wins, so a mod loaded later can stand in front of one loaded earlier
            for (size_t at = g_answers.size(); at > 0; --at)
            {
                if (g_answers[at - 1].path == path)
                {
                    return &g_answers[at - 1];
                }
            }

            return nullptr;
        }

        int __cdecl ApiReadFile(AmodMod mod, const char* path, void* buffer, int capacity)
        {
            if (Caller(mod) == nullptr || path == nullptr || path[0] == '\0')
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Read was given no path");
            }

            // A mod's own answer first, so a mod that served a file reads back what it served
            const Answer* const answer = Standing(Flattened(path));

            if (answer != nullptr)
            {
                const int size = static_cast<int>(answer->bytes.size());

                if (buffer != nullptr && capacity > 0)
                {
                    memcpy(buffer, answer->bytes.data(), static_cast<size_t>(capacity < size ? capacity : size));
                }
                return size;
            }

            const PackageOpenRoutine open = reinterpret_cast<PackageOpenRoutine>(GameModule::Resolve(GameAddress::kPackageOpenFile));
            const PackageFreeRoutine release = reinterpret_cast<PackageFreeRoutine>(GameModule::Resolve(GameAddress::kPackageFree));

            if (open == nullptr || release == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NOT_HERE, "the game's own file reader is not reachable");
            }

            // The game's door, which is the only way into data.pak
            GamePackageFile* const file = static_cast<GamePackageFile*>(open(path));

            if (file == nullptr)
            {
                return Fail(mod, AMOD_FAIL_NO_SUCH, "the game has no file '%s'", path);
            }

            const int size = static_cast<int>(file->size);

            if (buffer != nullptr && capacity > 0 && file->data != nullptr)
            {
                memcpy(buffer, file->data, static_cast<size_t>(capacity < size ? capacity : size));
            }

            release(file);
            return size;
        }

        int __cdecl ApiServe(AmodMod mod, const char* path, const void* bytes, int size)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || path == nullptr || path[0] == '\0')
            {
                return Fail(mod, AMOD_FAIL_BAD_ARGUMENT, "Serve was given no path");
            }
            if (bytes == nullptr || size <= 0)
            {
                return g_table.Clear(mod, path);
            }

            const std::string flattened = Flattened(path);

            for (Answer& answer : g_answers)
            {
                if (answer.owner == caller && answer.path == flattened)
                {
                    answer.bytes.assign(static_cast<const uint8_t*>(bytes),
                        static_cast<const uint8_t*>(bytes) + size);
                    return 1;
                }
            }

            Answer fresh;
            fresh.owner = caller;
            fresh.path = flattened;
            fresh.bytes.assign(static_cast<const uint8_t*>(bytes),
                static_cast<const uint8_t*>(bytes) + size);

            g_answers.push_back(fresh);

            FORGE_LOG_INFO("'%s' is answered by a mod from now on (%d bytes)", path, size);
            return 1;
        }

        int __cdecl ApiClear(AmodMod mod, const char* path)
        {
            ModContext* const caller = Caller(mod);

            if (caller == nullptr || path == nullptr)
            {
                return 0;
            }

            const std::string flattened = Flattened(path);

            for (auto it = g_answers.begin(); it != g_answers.end(); ++it)
            {
                if (it->owner == caller && it->path == flattened)
                {
                    g_answers.erase(it);
                    return 1;
                }
            }

            return 0;
        }

        int __cdecl ApiServedCount(AmodMod mod)
        {
            ModContext* const caller = Caller(mod);
            int mine = 0;

            for (const Answer& answer : g_answers)
            {
                mine += answer.owner == caller ? 1 : 0;
            }

            return caller != nullptr ? mine : 0;
        }

        const char* __cdecl ApiServedAt(AmodMod mod, int index)
        {
            ModContext* const caller = Caller(mod);
            int mine = 0;

            for (const Answer& answer : g_answers)
            {
                if (answer.owner != caller)
                {
                    continue;
                }
                if (mine == index)
                {
                    return answer.path.c_str();
                }
                ++mine;
            }

            return "";
        }
    }

    bool FilesApi::Install()
    {
        g_table.structSize = sizeof(AmodFilesApi);
        g_table.Read = &ApiReadFile;
        g_table.Serve = &ApiServe;
        g_table.Clear = &ApiClear;
        g_table.ServedCount = &ApiServedCount;
        g_table.ServedAt = &ApiServedAt;

        FORGE_LOG_INFO("Files interface installed");
        return true;
    }

    void FilesApi::Uninstall()
    {
        g_answers.clear();
        g_table = AmodFilesApi{};
    }

    void FilesApi::ForgetMod(ModContext* owner)
    {
        // Nothing to undo anywhere else: an answer was only ever held here
        for (auto it = g_answers.begin(); it != g_answers.end();)
        {
            it = it->owner == owner ? g_answers.erase(it) : ++it;
        }
    }

    const AmodFilesApi* FilesApi::Table()
    {
        return g_table.structSize != 0 ? &g_table : nullptr;
    }

    bool FilesApi::Served(const char* path, std::vector<uint8_t>& out, const char*& byWhom)
    {
        const Answer* const answer = Standing(Flattened(path));

        if (answer == nullptr)
        {
            return false;
        }

        out = answer->bytes;
        byWhom = answer->owner != nullptr ? answer->owner->id.c_str() : "a mod";
        return true;
    }
}
