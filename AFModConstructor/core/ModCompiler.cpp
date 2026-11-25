#include "ModCompiler.h"

#include "ModPackages.h"
#include "ModRuntime.h"
#include "PathUtil.h"
#include "SharedConfig.h"

#include <windows.h>

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool Machine32(const std::string& machine) {
    return machine.rfind("i686", 0) == 0 || machine.rfind("i586", 0) == 0 || machine == "mingw32";
}

bool Ask(const std::string& gcc, std::string& machine) {
    std::error_code ec;
    if (!fs::exists(PathUtil::MakePath(gcc), ec)) return false;
    std::string output;
    int code = 1;
    if (!ModCompiler::Run(ModCompiler::Quote(ModCompiler::ShortPath(gcc)) + " -dumpmachine", &output, &code) || code != 0)
        return false;
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' '))
        output.pop_back();
    machine = output;
    return true;
}

} // namespace

bool ModCompiler::Run(const std::string& commandLine, std::string* output, int* exitCode) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) return false;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};
    std::vector<char> line(commandLine.begin(), commandLine.end());
    line.push_back('\0');

    if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(readEnd);
        CloseHandle(writeEnd);
        return false;
    }
    CloseHandle(writeEnd);

    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        if (output) output->append(buffer, read);
    }
    CloseHandle(readEnd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    if (exitCode) *exitCode = static_cast<int>(code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

std::string ModCompiler::Quote(const std::string& text) { return "\"" + text + "\""; }

std::string ModCompiler::ShortPath(const std::string& path) {
    const fs::path p = PathUtil::MakePath(path);
    const std::wstring wide = p.wstring();
    std::wstring source = wide;
    std::wstring tail;

    std::error_code ec;
    if (!fs::exists(p, ec)) {
        source = p.parent_path().wstring();
        tail = L"\\" + p.filename().wstring();
    }

    const DWORD length = GetShortPathNameW(source.c_str(), nullptr, 0);
    std::wstring result = source;
    if (length > 0) {
        std::vector<wchar_t> buffer(length);
        GetShortPathNameW(source.c_str(), buffer.data(), length);
        result.assign(buffer.data());
    }
    result += tail;

    const int size = WideCharToMultiByte(CP_ACP, 0, result.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return path;
    std::string narrow(static_cast<size_t>(size - 1), 0);
    WideCharToMultiByte(CP_ACP, 0, result.c_str(), -1, narrow.data(), size, nullptr, nullptr);
    return narrow;
}

std::string ModCompiler::BundledDir() {
    return PathUtil::U8(PathUtil::ResourcePath("compiler"));
}

ModCompiler::Toolchain ModCompiler::Find() {
    std::vector<std::string> candidates;
    candidates.push_back(PathUtil::U8(PathUtil::MakePath(BundledDir()) / "bin" / "gcc.exe"));

    const std::string configured = SharedConfig::Get("mingwPath");
    if (!configured.empty()) {
        candidates.push_back(configured);
        candidates.push_back(PathUtil::U8(PathUtil::MakePath(configured) / "gcc.exe"));
        candidates.push_back(PathUtil::U8(PathUtil::MakePath(configured) / "bin" / "gcc.exe"));
        candidates.push_back(PathUtil::U8(PathUtil::MakePath(configured) / "bin" / "i686-w64-mingw32-gcc.exe"));
    }
    candidates.push_back("C:/MinGW/bin/gcc.exe");
    candidates.push_back("C:/mingw32/bin/gcc.exe");
    candidates.push_back("C:/msys64/mingw32/bin/gcc.exe");
    candidates.push_back("C:/Program Files (x86)/mingw-w64/i686/bin/gcc.exe");

    Toolchain found;
    for (const std::string& candidate : candidates) {
        std::string machine;
        if (!Ask(candidate, machine)) continue;
        found.gcc = candidate;
        found.machine = machine;
        found.ok = Machine32(machine);
        if (found.ok) return found;
    }
    return found;
}

std::string ModCompiler::SdkInclude() {
    std::error_code ec;

    const std::string configured = SharedConfig::Get("sdkPath");
    if (!configured.empty() && fs::exists(PathUtil::MakePath(configured) / "ForgeApi.h", ec)) return configured;

    const fs::path beside = PathUtil::ResourcePath("sdk/include");
    if (fs::exists(beside / "ForgeApi.h", ec)) return PathUtil::U8(beside);

    const std::string forge = SharedConfig::Get("forgePath");
    if (!forge.empty()) {
        const fs::path inside = PathUtil::MakePath(forge) / "sdk" / "include";
        if (fs::exists(inside / "ForgeApi.h", ec)) return PathUtil::U8(inside);
    }
    return {};
}

bool ModCompiler::Gather(const std::string& tier, const std::set<std::string>& groups, Extras& out, std::string* error) {
    ModPackages::Scan(nullptr);
    const bool anyHelper = groups.count("*") != 0;

    for (const ModPackages::Package& package : ModPackages::All()) {
        if (!package.enabled || !package.error.empty()) continue;
        if (!ModPackages::NeedsUnpack(package)) continue;

        bool used = anyHelper;
        for (auto group = groups.begin(); !used && group != groups.end(); ++group) {
            used = ModPackages::Owns(package, *group);
        }
        if (!used) continue;

        bool fits = false;
        for (const std::string& known : package.tiers) fits = fits || known == tier;
        if (!fits) {
            if (error) *error = "package.tier:" + package.id + ":" + tier;
            return false;
        }

        std::string folder, why;
        if (!ModPackages::Unpack(package, folder, &why)) {
            if (error) *error = "package.unpack:" + package.id + ":" + why;
            return false;
        }

        const fs::path root = PathUtil::MakePath(folder);
        for (const std::string& item : package.include) out.include.push_back(PathUtil::U8(root / PathUtil::MakePath(item)));
        for (const std::string& item : package.sources) out.sources.push_back(PathUtil::U8(root / PathUtil::MakePath(item)));
        for (const std::string& item : package.libDirs) out.libDirs.push_back(PathUtil::U8(root / PathUtil::MakePath(item)));
        for (const std::string& item : package.libs) out.libs.push_back(item);
    }
    return true;
}

bool ModCompiler::Compile(const std::string& sourceFile, const std::string& outDll, std::string* log, std::string* error) {
    return Compile(sourceFile, outDll, Extras(), log, error);
}

bool ModCompiler::Compile(const std::string& sourceFile, const std::string& outDll, const Extras& extras, std::string* log, std::string* error) {
    const Toolchain toolchain = Find();
    if (toolchain.gcc.empty()) {
        if (error) *error = "mingw.notFound";
        return false;
    }
    if (!toolchain.ok) {
        if (error) *error = "mingw.not32:" + toolchain.machine;
        return false;
    }

    const std::string sdk = SdkInclude();
    if (sdk.empty()) {
        if (error) *error = "sdk.notFound";
        return false;
    }

    if (!ModRuntime::Build(log, error)) return false;

    std::error_code ec;
    fs::create_directories(PathUtil::MakePath(outDll).parent_path(), ec);

    const std::string binDir = PathUtil::U8(PathUtil::MakePath(toolchain.gcc).parent_path());
    const std::string strict = " -Werror=int-conversion -Werror=incompatible-pointer-types"
                               " -Werror=implicit-function-declaration";

    std::string command = Quote(ShortPath(toolchain.gcc)) + " -shared -O2 -std=c99 -Wall -static-libgcc" +
                          strict + " -ffunction-sections -fdata-sections" +
                          " -B" + Quote(ShortPath(binDir)) + " -I" + Quote(ShortPath(sdk)) +
                          " -I" + Quote(ShortPath(PathUtil::U8(PathUtil::MakePath(ModRuntime::Header()).parent_path())));
    for (const std::string& path : extras.include) command += " -I" + Quote(ShortPath(path));

    command += " -x c " + Quote(ShortPath(sourceFile));
    for (const std::string& path : extras.sources) command += " " + Quote(ShortPath(path));

    command += " -x none " + Quote(ShortPath(ModRuntime::Library()));
    command += " -o " + Quote(ShortPath(outDll)) + " -Wl,--gc-sections";
    for (const std::string& path : extras.libDirs) command += " -L" + Quote(ShortPath(path));
    for (const std::string& name : extras.libs) command += " -l" + name;

    std::string output;
    int code = 1;
    if (!Run(command, &output, &code)) {
        if (error) *error = "compiler.notStarted";
        return false;
    }
    if (log) *log += output;
    if (code != 0) {
        if (error) *error = "compiler.failed";
        return false;
    }
    return fs::exists(PathUtil::MakePath(outDll), ec);
}
