#include "ModRuntime.h"

#include "ModCatalog.h"
#include "ModCompiler.h"
#include "PathUtil.h"

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* const Builtins =
    "const AmodApi* g_api = 0;\n"
    "float mod_delta = 0.0f;\n"
    "\n"
    "AmodValue as_value(const char* text) {\n"
    "    AmodValue v;\n"
    "    char* end = 0;\n"
    "    double number;\n"
    "    memset(&v, 0, sizeof v);\n"
    "    number = strtod(text, &end);\n"
    "    if (text[0] != 0 && end != 0 && *end == 0) {\n"
    "        v.kind = AMOD_VALUE_FLOAT;\n"
    "        v.count = 1;\n"
    "        v.numbers[0] = (float)number;\n"
    "    } else {\n"
    "        v.kind = AMOD_VALUE_STRING;\n"
    "        v.text = text;\n"
    "    }\n"
    "    return v;\n"
    "}\n"
    "\n"
    "int ctrl_index(const char* name) {\n"
    "    return (API != 0 && API->ctrls != 0) ? API->ctrls->Find(SELF, name, 0) : -1;\n"
    "}\n"
    "\n"
    "const char* mod_text(const char* format, ...) {\n"
    "    static char buffers[4][512];\n"
    "    static int turn = 0;\n"
    "    char* const out = buffers[turn];\n"
    "    va_list args;\n"
    "    turn = (turn + 1) % 4;\n"
    "    va_start(args, format);\n"
    "    vsnprintf(out, sizeof buffers[0], format, args);\n"
    "    va_end(args);\n"
    "    return out;\n"
    "}\n"
    "\n"
    "void mod_set_text(char* slot, int size, const char* text) {\n"
    "    if (slot == 0 || size <= 0) return;\n"
    "    if (text == 0) text = \"\";\n"
    "    strncpy(slot, text, (size_t)size - 1);\n"
    "    slot[size - 1] = 0;\n"
    "}\n";

const char* const BuiltinDeclarations =
    "extern const AmodApi* g_api;\n"
    "#define API g_api\n"
    "#define SELF g_api->self\n"
    "\n"
    "extern float mod_delta;\n"
    "\n"
    "AmodValue as_value(const char* text);\n"
    "int ctrl_index(const char* name);\n"
    "void mod_set_text(char* slot, int size, const char* text);\n"
    "\n"
    "#if defined(__GNUC__)\n"
    "#define MOD_FORMAT __attribute__((format(printf, 1, 2)))\n"
    "#else\n"
    "#define MOD_FORMAT\n"
    "#endif\n"
    "\n"
    "MOD_FORMAT const char* mod_text(const char* format, ...);\n";

bool Blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool FunctionHead(const std::string& text, size_t at, size_t& braceAt, std::string& head) {
    if (text.compare(at, 7, "static ") != 0) return false;
    const size_t brace = text.find('{', at);
    const size_t semicolon = text.find(';', at);
    if (brace == std::string::npos) return false;
    if (semicolon != std::string::npos && semicolon < brace) return false;

    size_t end = brace;
    while (end > at && Blank(text[end - 1])) --end;
    if (end == at || text[end - 1] != ')') return false;

    head = text.substr(at, end - at);
    braceAt = brace;
    return true;
}

std::string WithoutStatic(std::string head) {
    if (head.compare(0, 7, "static ") == 0) head.erase(0, 7);
    if (head.find("MOD_UNUSED ") == 0) head.erase(0, 11);
    return head;
}

std::vector<std::pair<std::string, std::string>> Pieces() {
    std::vector<std::pair<std::string, std::string>> out;
    out.push_back({ "_builtins", Builtins });
    for (const ModCatalog::SubsystemDef& subsystem : ModCatalog::Subsystems()) {
        if (subsystem.prologue.empty()) continue;
        out.push_back({ subsystem.id, subsystem.prologue });
    }
    return out;
}

std::string Gather() {
    std::string out;
    for (const auto& piece : Pieces()) out += piece.second;
    return out;
}

void Split(const std::string& source, std::string& code, std::string& declarations) {
    code.clear();
    declarations.clear();

    size_t at = 0;
    while (at < source.size()) {
        const size_t lineEnd = source.find('\n', at);
        const size_t next = lineEnd == std::string::npos ? source.size() : lineEnd + 1;

        size_t brace = 0;
        std::string head;
        if (FunctionHead(source, at, brace, head)) {
            declarations += WithoutStatic(head) + ";\n";
            code += WithoutStatic(head);
            at += head.size();
            continue;
        }
        code.append(source, at, next - at);
        at = next;
    }
}

std::string Hash(const std::string& text) {
    uint64_t value = 1469598103934665603ull;
    for (char c : text) {
        value ^= static_cast<unsigned char>(c);
        value *= 1099511628211ull;
    }
    char out[32] = {};
    std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(value));
    return out;
}

std::string Read(const std::string& path) {
    std::ifstream in(PathUtil::MakePath(path), std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool Write(const std::string& path, const std::string& text, std::string* error) {
    std::ofstream out(PathUtil::MakePath(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    out << text;
    return true;
}

fs::path CompilerDir() { return PathUtil::MakePath(ModCompiler::BundledDir()); }
std::string StampPath() { return PathUtil::U8(CompilerDir() / "lib" / "_as-cm10.stamp"); }

} // namespace

std::string ModRuntime::Library() { return PathUtil::U8(CompilerDir() / "lib" / "_as-cm10.a"); }
std::string ModRuntime::Header() { return PathUtil::U8(CompilerDir() / "include" / "_as-cm10.h"); }
std::string ModRuntime::Stamp() { return Read(StampPath()); }

bool ModRuntime::Build(std::string* log, std::string* error) {
    const std::string source = Gather();
    const std::string stamp = Hash(source);

    std::error_code ec;
    if (Stamp() == stamp && fs::exists(PathUtil::MakePath(Library()), ec) &&
        fs::exists(PathUtil::MakePath(Header()), ec)) {
        return true;
    }

    const ModCompiler::Toolchain toolchain = ModCompiler::Find();
    if (toolchain.gcc.empty() || !toolchain.ok) {
        if (error) *error = "mingw.notFound";
        return false;
    }

    const std::string sdk = ModCompiler::SdkInclude();
    if (sdk.empty()) {
        if (error) *error = "sdk.notFound";
        return false;
    }

    std::string declarations;
    for (const auto& piece : Pieces()) {
        std::string body, heads;
        Split(piece.second, body, heads);
        declarations += heads;
    }

    const std::string header =
        "/* Written by AFModConstructor: runtime helpers from catalog prologues */\n"
        "#ifndef AS_CM10_H\n"
        "#define AS_CM10_H\n"
        "\n"
        "#include <ForgeApi.h>\n"
        "#include <stdlib.h>\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "#include <math.h>\n"
        "#include <time.h>\n"
        "#include <stdarg.h>\n"
        "\n"
        "#if defined(__GNUC__)\n"
        "#define MOD_UNUSED __attribute__((unused))\n"
        "#else\n"
        "#define MOD_UNUSED\n"
        "#endif\n"
        "\n" + std::string(BuiltinDeclarations) +
        "\n/* catalog subsystem helpers */\n" + declarations +
        "\n#endif /* AS_CM10_H */\n";

    fs::create_directories(CompilerDir() / "lib", ec);
    fs::create_directories(CompilerDir() / "include", ec);
    if (!Write(Header(), header, error)) return false;

    const std::string folder = PathUtil::U8(CompilerDir() / "lib" / "as-cm10");
    fs::create_directories(PathUtil::MakePath(folder), ec);

    const std::string bin = PathUtil::U8(PathUtil::MakePath(toolchain.gcc).parent_path());
    std::string objects;
    std::string output;
    int code32 = 1;

    for (const auto& piece : Pieces()) {
        std::string body, unused;
        Split(piece.second, body, unused);

        const std::string csource = PathUtil::U8(PathUtil::MakePath(folder) / ("as_" + piece.first + ".c"));
        const std::string object = PathUtil::U8(PathUtil::MakePath(folder) / ("as_" + piece.first + ".o"));
        if (!Write(csource, "#include \"_as-cm10.h\"\n\n" + body, error)) return false;

        const std::string command = ModCompiler::Quote(ModCompiler::ShortPath(toolchain.gcc)) +
            " -c -O2 -std=c99 -Wall" +
            " -B" + ModCompiler::Quote(ModCompiler::ShortPath(bin)) +
            " -I" + ModCompiler::Quote(ModCompiler::ShortPath(sdk)) +
            " -I" + ModCompiler::Quote(ModCompiler::ShortPath(PathUtil::U8(PathUtil::MakePath(Header()).parent_path()))) +
            " -x c " + ModCompiler::Quote(ModCompiler::ShortPath(csource)) +
            " -o " + ModCompiler::Quote(ModCompiler::ShortPath(object));

        output.clear();
        if (!ModCompiler::Run(command, &output, &code32) || code32 != 0) {
            if (log) *log += "_as-cm10 / " + piece.first + ":\n" + output;
            if (error) *error = "runtime.compile";
            return false;
        }
        if (log && !output.empty()) *log += output;
        objects += " " + ModCompiler::Quote(ModCompiler::ShortPath(object));
    }

    fs::remove(PathUtil::MakePath(Library()), ec);
    const std::string archiver = PathUtil::U8(PathUtil::MakePath(bin) / "ar.exe");
    const std::string archive = ModCompiler::Quote(ModCompiler::ShortPath(archiver)) + " rcs " +
        ModCompiler::Quote(ModCompiler::ShortPath(Library())) + objects;

    output.clear();
    if (!ModCompiler::Run(archive, &output, &code32) || code32 != 0) {
        if (log) *log += output;
        if (error) *error = "runtime.archive";
        return false;
    }

    Write(StampPath(), stamp, nullptr);
    return true;
}
