// amodpack — command line tool for the .amod container.
//
// The mod format is a plain zip, so packing and unpacking could be done with any archiver.
// This tool exists because a container is only installable once its manifest is valid, and
// checking that at pack time is what keeps a broken mod from reaching a player. It shares
// the manifest and archive code with the loader, so both agree by construction on what a
// well-formed mod is.
//
// It is also what a mod project calls as a post-build step, which is what lets mods be
// written in plain Visual Studio without any part of the AzForge tooling installed.

#include "pch.h"

#include "src/Core/ForgeText.h"
#include "src/Core/ForgeVersion.h"
#include "src/Mods/ModManifest.h"
#include "src/Zip/ZipArchive.h"
#include "src/Zip/ZipWriter.h"

#include <algorithm>
#include <filesystem>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace
{
    constexpr int kExitOk = 0;
    constexpr int kExitError = 1;
    constexpr int kExitUsage = 2;

    constexpr wchar_t kDisabledSuffix[] = L".disabled";

    void Print(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }

    std::string PathText(const std::filesystem::path& path)
    {
        return Forge::PathToUtf8(path);
    }

    bool ReadWholeFile(const std::filesystem::path& path, std::vector<uint8_t>& out, std::string& error)
    {
        out.clear();

        const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            error = "cannot open " + PathText(path);
            return false;
        }

        LARGE_INTEGER size = {};
        if (!GetFileSizeEx(handle, &size))
        {
            CloseHandle(handle);
            error = "cannot measure " + PathText(path);
            return false;
        }

        if (size.QuadPart > 0x7FFFFFFF)
        {
            CloseHandle(handle);
            error = PathText(path) + " is too large for the container";
            return false;
        }

        out.resize(static_cast<size_t>(size.QuadPart));
        if (!out.empty())
        {
            DWORD read = 0;
            if (!ReadFile(handle, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) ||
                read != out.size())
            {
                CloseHandle(handle);
                error = "cannot read " + PathText(path);
                return false;
            }
        }

        CloseHandle(handle);
        return true;
    }

    bool WriteWholeFile(const std::filesystem::path& path, const std::vector<uint8_t>& data,
                        std::string& error)
    {
        std::error_code status;
        std::filesystem::create_directories(path.parent_path(), status);

        const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            error = "cannot create " + PathText(path);
            return false;
        }

        if (!data.empty())
        {
            DWORD written = 0;
            if (!WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) ||
                written != data.size())
            {
                CloseHandle(handle);
                error = "cannot write " + PathText(path);
                return false;
            }
        }

        CloseHandle(handle);
        return true;
    }

    // Reads and validates mod.json, either from a folder or from an archive.
    bool LoadManifestFromFolder(const std::filesystem::path& folder, Forge::ModManifest& manifest,
                                std::string& error)
    {
        std::vector<uint8_t> bytes;
        if (!ReadWholeFile(folder / Forge::FromUtf8(Forge::kModManifestName), bytes, error))
        {
            return false;
        }

        std::string document(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (document.size() >= 3 &&
            static_cast<unsigned char>(document[0]) == 0xEF &&
            static_cast<unsigned char>(document[1]) == 0xBB &&
            static_cast<unsigned char>(document[2]) == 0xBF)
        {
            document.erase(0, 3);
        }

        return Forge::ParseModManifest(document, manifest, error);
    }

    bool LoadManifestFromArchive(const std::filesystem::path& file, Forge::ModManifest& manifest,
                                 std::string& error)
    {
        Forge::ZipArchive archive;
        if (!archive.Open(file, error))
        {
            return false;
        }

        std::vector<uint8_t> bytes;
        if (!archive.ReadByName(Forge::kModManifestName, bytes, error))
        {
            return false;
        }

        std::string document(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (document.size() >= 3 &&
            static_cast<unsigned char>(document[0]) == 0xEF &&
            static_cast<unsigned char>(document[1]) == 0xBB &&
            static_cast<unsigned char>(document[2]) == 0xBF)
        {
            document.erase(0, 3);
        }

        return Forge::ParseModManifest(document, manifest, error);
    }

    std::wstring FindOption(const std::vector<std::wstring>& args, const wchar_t* name)
    {
        for (size_t i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] == name)
            {
                return args[i + 1];
            }
        }
        return std::wstring();
    }

    std::vector<std::wstring> PositionalArguments(const std::vector<std::wstring>& args)
    {
        std::vector<std::wstring> positional;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (!args[i].empty() && args[i][0] == L'-')
            {
                ++i; // every option this tool accepts takes a value
                continue;
            }
            positional.push_back(args[i]);
        }
        return positional;
    }

    std::filesystem::path ModsDirectory(const std::vector<std::wstring>& args)
    {
        const std::wstring game = FindOption(args, L"--game");
        const std::filesystem::path root = game.empty() ? std::filesystem::current_path()
                                                        : std::filesystem::path(game);
        return root / L"mods";
    }

    struct InstalledMod
    {
        std::filesystem::path file;
        Forge::ModManifest manifest;
        bool enabled = false;
    };

    // Enumerates both enabled archives and the ones renamed with the disabled suffix.
    std::vector<InstalledMod> CollectInstalled(const std::filesystem::path& modsDirectory)
    {
        std::vector<InstalledMod> installed;

        std::error_code status;
        if (!std::filesystem::is_directory(modsDirectory, status))
        {
            return installed;
        }

        std::error_code iteration;
        for (const std::filesystem::directory_entry& item :
             std::filesystem::directory_iterator(modsDirectory, iteration))
        {
            std::error_code kind;
            if (!item.is_regular_file(kind) || kind)
            {
                continue;
            }

            const std::wstring name = item.path().filename().wstring();
            const bool disabled = name.size() > wcslen(kDisabledSuffix) &&
                                  name.compare(name.size() - wcslen(kDisabledSuffix),
                                               wcslen(kDisabledSuffix), kDisabledSuffix) == 0;

            std::wstring stem = disabled ? name.substr(0, name.size() - wcslen(kDisabledSuffix)) : name;
            std::wstring extension = std::filesystem::path(stem).extension().wstring();
            for (wchar_t& c : extension)
            {
                if (c >= L'A' && c <= L'Z')
                {
                    c = static_cast<wchar_t>(c - L'A' + L'a');
                }
            }
            if (extension != Forge::kModArchiveExtension)
            {
                continue;
            }

            InstalledMod entry;
            entry.file = item.path();
            entry.enabled = !disabled;

            std::string error;
            if (!LoadManifestFromArchive(item.path(), entry.manifest, error))
            {
                Print("  ! %s: %s\n", PathText(item.path().filename()).c_str(), error.c_str());
                continue;
            }

            installed.push_back(std::move(entry));
        }

        std::sort(installed.begin(), installed.end(), [](const InstalledMod& a, const InstalledMod& b)
        {
            return a.manifest.id < b.manifest.id;
        });

        return installed;
    }

    int CommandPack(const std::vector<std::wstring>& args)
    {
        const std::vector<std::wstring> positional = PositionalArguments(args);
        if (positional.empty())
        {
            Print("pack needs the folder that holds mod.json\n");
            return kExitUsage;
        }

        std::error_code status;
        const std::filesystem::path folder = std::filesystem::absolute(positional[0], status);
        if (!std::filesystem::is_directory(folder, status))
        {
            Print("%s is not a folder\n", PathText(folder).c_str());
            return kExitError;
        }

        Forge::ModManifest manifest;
        std::string error;
        if (!LoadManifestFromFolder(folder, manifest, error))
        {
            Print("%s is not a valid mod: %s\n", PathText(folder).c_str(), error.c_str());
            return kExitError;
        }

        const std::wstring requested = FindOption(args, L"-o");
        const std::filesystem::path output =
            requested.empty()
                ? folder.parent_path() / (Forge::FromUtf8(manifest.id) + Forge::kModArchiveExtension)
                : std::filesystem::path(requested);

        std::error_code creation;
        std::filesystem::create_directories(output.parent_path(), creation);

        // Files are collected first so the container is only created once the whole set is
        // known to be readable and the declared entry point is known to be present.
        std::vector<std::pair<std::string, std::filesystem::path>> files;
        std::error_code walk;
        for (const std::filesystem::directory_entry& item :
             std::filesystem::recursive_directory_iterator(folder, walk))
        {
            std::error_code kind;
            if (!item.is_regular_file(kind) || kind)
            {
                continue;
            }

            if (std::filesystem::equivalent(item.path(), output, kind))
            {
                continue;
            }

            const std::filesystem::path relative = std::filesystem::relative(item.path(), folder, kind);
            files.emplace_back(Forge::NormalizeSlashes(Forge::ToUtf8(relative.wstring())), item.path());
        }

        std::sort(files.begin(), files.end());

        if (!manifest.entry.empty())
        {
            const std::string wanted = Forge::ToLowerAscii(Forge::NormalizeSlashes(manifest.entry));
            const bool present = std::any_of(files.begin(), files.end(),
                                             [&wanted](const auto& item)
                                             {
                                                 return Forge::ToLowerAscii(item.first) == wanted;
                                             });
            if (!present)
            {
                Print("the manifest declares entry '%s', which the folder does not contain\n",
                      manifest.entry.c_str());
                return kExitError;
            }
        }

        Forge::ZipWriter writer;
        if (!writer.Open(output, error))
        {
            Print("%s\n", error.c_str());
            return kExitError;
        }

        for (const auto& item : files)
        {
            std::vector<uint8_t> bytes;
            if (!ReadWholeFile(item.second, bytes, error))
            {
                Print("%s\n", error.c_str());
                return kExitError;
            }

            if (!writer.Add(item.first, bytes.data(), bytes.size(), error))
            {
                Print("%s\n", error.c_str());
                return kExitError;
            }
        }

        if (!writer.Finish(error))
        {
            Print("%s\n", error.c_str());
            return kExitError;
        }

        Print("%s %s -> %s (%zu file(s))\n",
              manifest.id.c_str(), manifest.version.c_str(),
              PathText(output).c_str(), files.size());
        return kExitOk;
    }

    int CommandUnpack(const std::vector<std::wstring>& args)
    {
        const std::vector<std::wstring> positional = PositionalArguments(args);
        if (positional.empty())
        {
            Print("unpack needs an .amod file\n");
            return kExitUsage;
        }

        const std::filesystem::path file = positional[0];

        Forge::ZipArchive archive;
        std::string error;
        if (!archive.Open(file, error))
        {
            Print("%s: %s\n", PathText(file).c_str(), error.c_str());
            return kExitError;
        }

        const std::wstring requested = FindOption(args, L"-o");
        const std::filesystem::path destination =
            requested.empty() ? std::filesystem::path(file).replace_extension() : std::filesystem::path(requested);

        for (const Forge::ZipEntry& entry : archive.Entries())
        {
            std::vector<uint8_t> bytes;
            if (!archive.Read(entry, bytes, error))
            {
                Print("%s\n", error.c_str());
                return kExitError;
            }

            const std::filesystem::path target = destination / Forge::FromUtf8(entry.name);
            if (!WriteWholeFile(target, bytes, error))
            {
                Print("%s\n", error.c_str());
                return kExitError;
            }
        }

        Print("%s -> %s (%zu file(s))\n",
              PathText(file.filename()).c_str(), PathText(destination).c_str(),
              archive.Entries().size());
        return kExitOk;
    }

    int CommandInfo(const std::vector<std::wstring>& args)
    {
        const std::vector<std::wstring> positional = PositionalArguments(args);
        if (positional.empty())
        {
            Print("info needs an .amod file\n");
            return kExitUsage;
        }

        const std::filesystem::path file = positional[0];

        Forge::ModManifest manifest;
        std::string error;
        if (!LoadManifestFromArchive(file, manifest, error))
        {
            Print("%s: %s\n", PathText(file).c_str(), error.c_str());
            return kExitError;
        }

        Forge::ZipArchive archive;
        if (!archive.Open(file, error))
        {
            Print("%s: %s\n", PathText(file).c_str(), error.c_str());
            return kExitError;
        }

        Print("id           : %s\n", manifest.id.c_str());
        Print("name         : %s\n", manifest.name.c_str());
        Print("version      : %s\n", manifest.version.c_str());
        Print("api_version  : %d\n", manifest.apiVersion);
        if (!manifest.author.empty())      Print("author       : %s\n", manifest.author.c_str());
        if (!manifest.description.empty()) Print("description  : %s\n", manifest.description.c_str());
        if (!manifest.gameVersion.empty()) Print("game_version : %s\n", manifest.gameVersion.c_str());
        if (!manifest.entry.empty())       Print("entry        : %s\n", manifest.entry.c_str());

        for (const std::string& other : manifest.loadAfter)
        {
            Print("load_after   : %s\n", other.c_str());
        }
        for (const std::string& other : manifest.conflicts)
        {
            Print("conflicts    : %s\n", other.c_str());
        }

        Print("\ncontents (%zu):\n", archive.Entries().size());
        for (const Forge::ZipEntry& entry : archive.Entries())
        {
            Print("  %10u  %s%s\n", entry.uncompressedSize, entry.name.c_str(),
                  entry.compressionMethod == 0 ? "" : "  (deflated)");
        }

        return kExitOk;
    }

    int CommandValidate(const std::vector<std::wstring>& args)
    {
        const std::vector<std::wstring> positional = PositionalArguments(args);
        if (positional.empty())
        {
            Print("validate needs an .amod file or a mod folder\n");
            return kExitUsage;
        }

        const std::filesystem::path subject = positional[0];

        Forge::ModManifest manifest;
        std::string error;

        std::error_code status;
        const bool folder = std::filesystem::is_directory(subject, status);
        const bool parsed = folder ? LoadManifestFromFolder(subject, manifest, error)
                                   : LoadManifestFromArchive(subject, manifest, error);
        if (!parsed)
        {
            Print("invalid: %s\n", error.c_str());
            return kExitError;
        }

        if (!folder)
        {
            // Extracting everything is what proves the container is intact: each entry is
            // checked against the CRC recorded for it.
            Forge::ZipArchive archive;
            if (!archive.Open(subject, error))
            {
                Print("invalid: %s\n", error.c_str());
                return kExitError;
            }

            for (const Forge::ZipEntry& entry : archive.Entries())
            {
                std::vector<uint8_t> bytes;
                if (!archive.Read(entry, bytes, error))
                {
                    Print("invalid: %s\n", error.c_str());
                    return kExitError;
                }
            }
        }

        Print("valid: %s %s (api %d)\n", manifest.id.c_str(), manifest.version.c_str(),
              manifest.apiVersion);
        return kExitOk;
    }

    int CommandList(const std::vector<std::wstring>& args)
    {
        const std::filesystem::path mods = ModsDirectory(args);
        const std::vector<InstalledMod> installed = CollectInstalled(mods);

        Print("%s\n", PathText(mods).c_str());
        if (installed.empty())
        {
            Print("  (no mods installed)\n");
            return kExitOk;
        }

        for (const InstalledMod& item : installed)
        {
            Print("  [%s] %-28s %-10s %s\n",
                  item.enabled ? "on " : "off",
                  item.manifest.id.c_str(),
                  item.manifest.version.c_str(),
                  item.manifest.name.c_str());
        }

        return kExitOk;
    }

    int CommandInstall(const std::vector<std::wstring>& args)
    {
        const std::vector<std::wstring> positional = PositionalArguments(args);
        if (positional.empty())
        {
            Print("install needs an .amod file\n");
            return kExitUsage;
        }

        const std::filesystem::path source = positional[0];

        Forge::ModManifest manifest;
        std::string error;
        if (!LoadManifestFromArchive(source, manifest, error))
        {
            Print("%s: %s\n", PathText(source).c_str(), error.c_str());
            return kExitError;
        }

        const std::filesystem::path mods = ModsDirectory(args);
        std::error_code creation;
        std::filesystem::create_directories(mods, creation);

        for (const InstalledMod& item : CollectInstalled(mods))
        {
            if (item.manifest.id == manifest.id)
            {
                std::error_code removal;
                std::filesystem::remove(item.file, removal);
                Print("replacing %s %s\n", item.manifest.id.c_str(), item.manifest.version.c_str());
            }
        }

        const std::filesystem::path target =
            mods / (Forge::FromUtf8(manifest.id) + Forge::kModArchiveExtension);

        std::error_code copy;
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, copy);
        if (copy)
        {
            Print("cannot install into %s: %s\n", PathText(mods).c_str(), copy.message().c_str());
            return kExitError;
        }

        Print("installed %s %s -> %s\n", manifest.id.c_str(), manifest.version.c_str(),
              PathText(target).c_str());
        return kExitOk;
    }

    int CommandSetEnabled(const std::vector<std::wstring>& args, bool enabled)
    {
        const std::vector<std::wstring> positional = PositionalArguments(args);
        if (positional.empty())
        {
            Print("%s needs a mod id\n", enabled ? "enable" : "disable");
            return kExitUsage;
        }

        const std::string id = Forge::ToUtf8(positional[0]);
        const std::filesystem::path mods = ModsDirectory(args);

        for (const InstalledMod& item : CollectInstalled(mods))
        {
            if (item.manifest.id != id)
            {
                continue;
            }

            if (item.enabled == enabled)
            {
                Print("%s is already %s\n", id.c_str(), enabled ? "enabled" : "disabled");
                return kExitOk;
            }

            // The loader only scans files whose extension is .amod, so appending the suffix
            // is all it takes to keep an installed mod out of the load order.
            std::filesystem::path target = item.file;
            if (enabled)
            {
                target = item.file.parent_path() /
                         item.file.filename().wstring().substr(
                             0, item.file.filename().wstring().size() - wcslen(kDisabledSuffix));
            }
            else
            {
                target = item.file.wstring() + kDisabledSuffix;
            }

            std::error_code rename;
            std::filesystem::rename(item.file, target, rename);
            if (rename)
            {
                Print("cannot rename %s: %s\n", PathText(item.file).c_str(), rename.message().c_str());
                return kExitError;
            }

            Print("%s %s\n", enabled ? "enabled" : "disabled", id.c_str());
            return kExitOk;
        }

        Print("%s is not installed in %s\n", id.c_str(), PathText(mods).c_str());
        return kExitError;
    }

    int CommandRemove(const std::vector<std::wstring>& args)
    {
        const std::vector<std::wstring> positional = PositionalArguments(args);
        if (positional.empty())
        {
            Print("remove needs a mod id\n");
            return kExitUsage;
        }

        const std::string id = Forge::ToUtf8(positional[0]);
        const std::filesystem::path mods = ModsDirectory(args);

        for (const InstalledMod& item : CollectInstalled(mods))
        {
            if (item.manifest.id != id)
            {
                continue;
            }

            std::error_code removal;
            std::filesystem::remove(item.file, removal);
            if (removal)
            {
                Print("cannot remove %s: %s\n", PathText(item.file).c_str(), removal.message().c_str());
                return kExitError;
            }

            Print("removed %s\n", id.c_str());
            return kExitOk;
        }

        Print("%s is not installed in %s\n", id.c_str(), PathText(mods).c_str());
        return kExitError;
    }

    void PrintUsage()
    {
        Print("amodpack " AZFORGE_VERSION_STRING " - .amod container tool (mod API %d)\n\n",
              Forge::kApiVersionCurrent);
        Print("  amodpack pack     <folder> [-o <file.amod>]   build a container from a mod folder\n");
        Print("  amodpack unpack   <file.amod> [-o <folder>]   extract a container\n");
        Print("  amodpack info     <file.amod>                 show the manifest and contents\n");
        Print("  amodpack validate <file.amod|folder>          check a container end to end\n");
        Print("  amodpack list     [--game <dir>]              list installed mods\n");
        Print("  amodpack install  <file.amod> [--game <dir>]  copy a container into mods/\n");
        Print("  amodpack remove   <id> [--game <dir>]         delete an installed mod\n");
        Print("  amodpack enable   <id> [--game <dir>]         put a mod back into the load order\n");
        Print("  amodpack disable  <id> [--game <dir>]         keep a mod installed but unloaded\n\n");
        Print("--game defaults to the current directory; mods live in <game>\\mods.\n");
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

    if (command == L"pack")     return CommandPack(args);
    if (command == L"unpack")   return CommandUnpack(args);
    if (command == L"info")     return CommandInfo(args);
    if (command == L"validate") return CommandValidate(args);
    if (command == L"list")     return CommandList(args);
    if (command == L"install")  return CommandInstall(args);
    if (command == L"remove")   return CommandRemove(args);
    if (command == L"enable")   return CommandSetEnabled(args, true);
    if (command == L"disable")  return CommandSetEnabled(args, false);

    PrintUsage();
    return kExitUsage;
}
