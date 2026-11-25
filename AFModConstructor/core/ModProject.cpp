#include "ModProject.h"

#include "GameLauncher.h"
#include "ModCompiler.h"
#include "PathUtil.h"
#include "SharedConfig.h"
#include "Zip.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

bool Fail(std::string* error, const std::string& text) {
    if (error) *error = text;
    return false;
}

std::string Translit(const std::string& utf8) {
    static const char* const RuTable[32] = {
        "a","b","v","g","d","e","zh","z","i","y","k","l","m","n","o","p",
        "r","s","t","u","f","h","c","ch","sh","sch","","y","","e","yu","ya"
    };
    std::string out;
    for (size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) { out += static_cast<char>(c); ++i; continue; }
        if (i + 1 < utf8.size() && (c == 0xD0 || c == 0xD1)) {
            const unsigned char d = static_cast<unsigned char>(utf8[i + 1]);
            int index = -1;
            if (c == 0xD0 && d >= 0x90 && d <= 0x9F) index = d - 0x90;
            else if (c == 0xD0 && d >= 0xA0 && d <= 0xAF) index = 16 + (d - 0xA0);
            else if (c == 0xD0 && d >= 0xB0 && d <= 0xBF) index = d - 0xB0;
            else if (c == 0xD1 && d >= 0x80 && d <= 0x8F) index = 16 + (d - 0x80);
            else if ((c == 0xD0 && d == 0x81) || (c == 0xD1 && d == 0x91)) { out += "yo"; i += 2; continue; }
            if (index >= 0) {
                out += RuTable[index];
                i += 2;
                continue;
            }
        }
        i += (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
    }
    return out;
}

bool WriteText(const fs::path& path, const std::string& text, std::string* error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return Fail(error, "cannot open " + PathUtil::U8(path));
    out << text;
    return true;
}

void CopyIfAny(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (!fs::is_directory(from, ec) || fs::is_empty(from, ec)) return;
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
}

} // namespace

const std::vector<std::string>& ModProject::Subsystems() {
    static const std::vector<std::string> subsystems = {
        "azm", "block_expression", "bonuses", "box_menu", "camera", "config", "console", "ctrls",
        "demo", "files", "frame", "hud", "input", "interop", "inventory", "launcher", "level_editor",
        "mmd", "mod_screen", "monsters", "objects_menu", "particles", "player", "render", "sav",
        "scores", "shader", "sound", "sprite_font", "stage", "textures", "world"
    };
    return subsystems;
}

int ModProject::CurrentApiVersion() {
    const std::string sdk = ModCompiler::SdkInclude();
    if (!sdk.empty()) {
        std::ifstream in(PathUtil::MakePath(sdk) / "ForgeTypes.h", std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            const size_t pos = line.find("#define AMOD_API_VERSION");
            if (pos == std::string::npos) continue;
            const size_t after = pos + 24;
            if (after < line.size() && line[after] != ' ' && line[after] != '\t') continue;
            const size_t numStart = line.find_first_of("0123456789", after);
            if (numStart != std::string::npos) {
                const int parsed = std::atoi(line.c_str() + numStart);
                if (parsed > 0) return parsed;
            }
        }
    }
    return 1;
}

std::string ModProject::SlugOf(const std::string& name) {
    std::string slug;
    for (char c : Translit(name)) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) slug += static_cast<char>(std::tolower(u));
        else if (!slug.empty() && slug.back() != '_') slug += '_';
    }
    while (!slug.empty() && slug.back() == '_') slug.pop_back();
    if (slug.empty() || std::isdigit(static_cast<unsigned char>(slug.front()))) {
        slug = "mod" + (slug.empty() ? "" : "_" + slug);
    }
    return slug;
}

bool ModProject::IsModProject(const std::string& projectDir) {
    std::error_code ec;
    return fs::exists(PathUtil::MakePath(projectDir) / "mod.json", ec);
}

bool ModProject::Create(const std::string& projectDir, const std::string& name, Tier tier, std::string* error) {
    Manifest manifest;
    manifest.id = SlugOf(name);
    manifest.name = name;
    manifest.apiVersion = CurrentApiVersion();
    if (tier != Tier::Data) manifest.entry = "bin/" + manifest.id + ".dll";
    return Create(projectDir, manifest, tier, error);
}

bool ModProject::Create(const std::string& projectDir, const Manifest& manifest, Tier tier, std::string* error) {
    std::error_code ec;
    const fs::path root = PathUtil::MakePath(projectDir);
    fs::create_directories(root / "files", ec);
    fs::create_directories(root / "logic", ec);
    if (tier != Tier::Data) fs::create_directories(root / "src", ec);
    if (ec) return Fail(error, "cannot create mod folders");

    if (fs::exists(root / "mod.json", ec)) return true;
    return SaveManifest(projectDir, manifest, error);
}

bool ModProject::LoadManifest(const std::string& projectDir, Manifest& out, std::string* error) {
    std::ifstream in(PathUtil::MakePath(projectDir) / "mod.json", std::ios::binary);
    if (!in) return Fail(error, "missing mod.json");
    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        return Fail(error, std::string("mod.json: ") + e.what());
    }

    out.id = j.value("id", "");
    out.name = j.value("name", "");
    out.version = j.value("version", "1.0.0");
    out.author = j.value("author", "");
    out.description = j.value("description", "");
    out.apiVersion = j.value("api_version", CurrentApiVersion());
    out.gameVersion = j.value("game_version", "1.0");
    out.entry = j.value("entry", "");
    out.icon = j.value("icon", "");
    out.requires_.clear();
    out.capabilities.clear();
    out.loadAfter.clear();
    out.conflicts.clear();
    if (j.contains("requires") && j["requires"].is_array()) {
        for (const auto& v : j["requires"]) out.requires_.push_back(v.get<std::string>());
    }
    if (j.contains("capabilities") && j["capabilities"].is_array()) {
        for (const auto& v : j["capabilities"]) out.capabilities.push_back(v.get<std::string>());
    }
    if (j.contains("load_after") && j["load_after"].is_array()) {
        for (const auto& v : j["load_after"]) out.loadAfter.push_back(v.get<std::string>());
    }
    if (j.contains("conflicts") && j["conflicts"].is_array()) {
        for (const auto& v : j["conflicts"]) out.conflicts.push_back(v.get<std::string>());
    }
    return !out.id.empty();
}

bool ModProject::SaveManifest(const std::string& projectDir, const Manifest& manifest, std::string* error) {
    if (manifest.id.empty()) return Fail(error, "mod has no id");

    json j;
    j["id"] = manifest.id;
    j["name"] = manifest.name;
    j["version"] = manifest.version;
    j["author"] = manifest.author;
    j["description"] = manifest.description;
    j["api_version"] = manifest.apiVersion;
    j["requires"] = manifest.requires_;
    j["capabilities"] = manifest.capabilities;
    j["game_version"] = manifest.gameVersion;
    if (!manifest.entry.empty()) j["entry"] = manifest.entry;
    if (!manifest.icon.empty()) j["icon"] = manifest.icon;
    if (!manifest.loadAfter.empty()) j["load_after"] = manifest.loadAfter;
    if (!manifest.conflicts.empty()) j["conflicts"] = manifest.conflicts;

    return WriteText(PathUtil::MakePath(projectDir) / "mod.json", j.dump(4), error);
}

std::string ModProject::AmodPackPath() {
    std::error_code ec;
    const std::string configured = SharedConfig::Get("amodPackPath");
    if (!configured.empty() && fs::exists(PathUtil::MakePath(configured), ec)) return configured;

    const fs::path bundled = PathUtil::ResourcePath("tools/AmodPack.exe");
    if (fs::exists(bundled, ec)) return PathUtil::U8(bundled);

    const fs::path beside = PathUtil::ResourcePath("AmodPack.exe");
    if (fs::exists(beside, ec)) return PathUtil::U8(beside);

    const std::string forge = SharedConfig::Get("forgePath");
    if (!forge.empty()) {
        const fs::path release = PathUtil::MakePath(forge) / "Release" / "AmodPack.exe";
        if (fs::exists(release, ec)) return PathUtil::U8(release);
    }

    const std::string sdk = SharedConfig::Get("sdkPath");
    if (!sdk.empty()) {
        const fs::path sdkRoot = PathUtil::MakePath(sdk).parent_path().parent_path();
        const fs::path release = sdkRoot / "Release" / "AmodPack.exe";
        if (fs::exists(release, ec)) return PathUtil::U8(release);
    }

    return {};
}

bool ModProject::Build(const std::string& projectDir, std::string* outPath, std::string* log, std::string* error) {
    Manifest manifest;
    if (!LoadManifest(projectDir, manifest, error)) return false;

    std::error_code ec;
    const fs::path root = PathUtil::MakePath(projectDir);
    const fs::path stage = root / "build" / "amod";
    fs::remove_all(stage, ec);
    fs::create_directories(stage, ec);
    if (ec) return Fail(error, "failed to create build/amod");

    fs::copy_file(root / "mod.json", stage / "mod.json", fs::copy_options::overwrite_existing, ec);
    if (ec) return Fail(error, "failed to copy mod.json");

    CopyIfAny(root / "files", stage / "files");
    CopyIfAny(root / "logic", stage / "logic");
    CopyIfAny(root / "bin", stage / "bin");

    if (!manifest.entry.empty() && !fs::exists(stage / PathUtil::MakePath(manifest.entry), ec)) {
        return Fail(error, "missing entry binary: " + manifest.entry);
    }

    const std::string amod = PathUtil::U8(root / "build" / (manifest.id + ".amod"));
    const std::string amodPack = AmodPackPath();

    if (!amodPack.empty()) {
        std::string out;
        int code = 1;
        const std::string packCmd = ModCompiler::Quote(ModCompiler::ShortPath(amodPack)) +
                                    " pack " + ModCompiler::Quote(ModCompiler::ShortPath(PathUtil::U8(stage))) +
                                    " -o " + ModCompiler::Quote(ModCompiler::ShortPath(amod));
        if (!ModCompiler::Run(packCmd, &out, &code) || code != 0) {
            if (log) *log += out;
            return Fail(error, "amodpack pack failed");
        }
        if (log) *log += out;

        std::string valOut;
        int valCode = 1;
        const std::string valCmd = ModCompiler::Quote(ModCompiler::ShortPath(amodPack)) +
                                   " validate " + ModCompiler::Quote(ModCompiler::ShortPath(amod));
        if (!ModCompiler::Run(valCmd, &valOut, &valCode) || valCode != 0) {
            if (log) *log += valOut;
            return Fail(error, "amodpack validate failed");
        }
        if (log) *log += valOut;
    } else {
        if (!Zip::WriteFolder(PathUtil::U8(stage), amod, error)) return false;
        if (log) *log += "[pack]     packed with built-in zip (AmodPack.exe not found)\n";
    }

    if (outPath) *outPath = amod;
    return true;
}

bool ModProject::Install(const std::string& amodPath, std::string* log, std::string* error) {
    const std::string gameDir = GameLauncher::GameDir();
    if (gameDir.empty()) return Fail(error, "install.noGame");

    std::error_code ec;
    const fs::path mods = PathUtil::MakePath(gameDir) / "mods";
    fs::create_directories(mods, ec);

    const fs::path src = PathUtil::MakePath(amodPath);
    const fs::path target = mods / src.filename();
    if (fs::exists(target, ec)) {
        fs::permissions(target, fs::perms::owner_write, fs::perm_options::add, ec);
        fs::remove(target, ec);
        if (fs::exists(target, ec)) return Fail(error, "install.busy");
    }

    fs::copy_file(src, target, fs::copy_options::overwrite_existing, ec);
    if (ec) return Fail(error, "install.failed");
    if (log) *log += "[install]  " + PathUtil::U8(target) + "\n";
    return true;
}
