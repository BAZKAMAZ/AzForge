#include "BuildWorker.h"

#include "ModCodegen.h"
#include "ModCompiler.h"
#include "PathUtil.h"

#include <QStringList>

namespace {

QString DescribeError(const std::string& err) {
    const size_t colon = err.find(':');
    const std::string key = (colon == std::string::npos) ? err : err.substr(0, colon);
    const std::string detail = (colon == std::string::npos) ? std::string() : err.substr(colon + 1);

    if (key == "mingw.notFound") return QStringLiteral("32-bit MinGW toolchain not found");
    if (key == "mingw.not32") return QStringLiteral("32-bit MinGW required, found: ") + QString::fromStdString(detail);
    if (key == "sdk.notFound") return QStringLiteral("AzForge SDK headers (ForgeApi.h) not found");
    if (key == "compiler.notStarted") return QStringLiteral("failed to launch compiler process");
    if (key == "compiler.failed") return QStringLiteral("C compilation failed");
    if (key == "runtime.compile") return QStringLiteral("failed to compile _as-cm10 runtime helper library");
    if (key == "runtime.archive") return QStringLiteral("failed to archive _as-cm10.a");
    if (key == "package.tier") return QStringLiteral("package tier mismatch: ") + QString::fromStdString(detail);
    if (key == "package.unpack") return QStringLiteral("failed to unpack package: ") + QString::fromStdString(detail);
    if (key == "install.noGame") return QStringLiteral("game path is not configured");
    if (key == "install.busy") return QStringLiteral("mod file is locked by a running game process");
    if (key == "install.failed") return QStringLiteral("failed to copy .amod into game/mods");
    return QString::fromStdString(err);
}

} // namespace

BuildWorker::BuildWorker(std::string projectDir, ModProject::Manifest manifest, std::vector<ModLogic::Sheet> sheets, bool install, QObject* parent)
    : QThread(parent),
      m_dir(std::move(projectDir)),
      m_manifest(std::move(manifest)),
      m_sheets(std::move(sheets)),
      m_install(install) {}

void BuildWorker::run() {
    BuildResult res;
    res.updatedManifest = m_manifest;

    auto log = [&](const char* stage, const QString& msg) {
        const QString line = QStringLiteral("[%1] %2").arg(QString::fromLatin1(stage).leftJustified(8), msg);
        if (!res.log.isEmpty() && !res.log.endsWith('\n')) res.log += '\n';
        res.log += line;
        emit stepMessage(line);
    };

    auto logRaw = [&](const QString& text) {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) return;
        for (const QString& l : trimmed.split('\n')) {
            const QString clean = l.endsWith('\r') ? l.chopped(1) : l;
            if (!res.log.isEmpty() && !res.log.endsWith('\n')) res.log += '\n';
            res.log += clean;
            emit stepMessage(clean);
        }
    };

    auto fail = [&](const char* stage, const QString& reason) {
        res.ok = false;
        res.status = reason;
        log(stage, QStringLiteral("ERROR: ") + reason);
        emit buildFinished(res);
    };

    // Codegen
    std::string source, error;
    if (!ModCodegen::Write(m_dir, m_sheets, &source, &error, &res.warnings)) {
        fail("codegen", DescribeError(error));
        return;
    }
    log("codegen", QStringLiteral("%1 (%2 warning(s))")
        .arg(QString::fromStdString(source)).arg(res.warnings.size()));
    for (const std::string& w : res.warnings) {
        log("warn", QString::fromStdString(w));
    }

    // Packages
    const auto groups = ModCodegen::UsedGroups(m_sheets);
    ModCompiler::Extras extras;
    if (!ModCompiler::Gather("tcc", groups, extras, &error)) {
        fail("packages", DescribeError(error));
        return;
    }
    if (!extras.sources.empty() || !extras.libs.empty()) {
        log("packages", QStringLiteral("linked %1 extra source(s), %2 lib(s)")
            .arg(extras.sources.size()).arg(extras.libs.size()));
    }

    // Compile 32-bit DLL (+ cached _as-cm10.a)
    const std::string dll = PathUtil::U8(PathUtil::MakePath(m_dir) / "bin" / (m_manifest.id + ".dll"));
    log("compile", QStringLiteral("building %1").arg(QString::fromStdString(dll)));
    std::string compilerLog;
    if (!ModCompiler::Compile(source, dll, extras, &compilerLog, &error)) {
        logRaw(QString::fromStdString(compilerLog));
        fail("compile", DescribeError(error));
        return;
    }
    logRaw(QString::fromStdString(compilerLog));

    const std::string expectedEntry = "bin/" + m_manifest.id + ".dll";
    if (res.updatedManifest.entry != expectedEntry) {
        res.updatedManifest.entry = expectedEntry;
        res.manifestChanged = true;
        ModProject::SaveManifest(m_dir, res.updatedManifest, nullptr);
    }

    // Pack & validate .amod
    std::string amod, packLog;
    if (!ModProject::Build(m_dir, &amod, &packLog, &error)) {
        logRaw(QString::fromStdString(packLog));
        fail("pack", DescribeError(error));
        return;
    }
    logRaw(QString::fromStdString(packLog));
    res.amodPath = QString::fromStdString(amod);

    if (!m_install) {
        res.ok = true;
        res.status = QStringLiteral("Built: ") + res.amodPath;
        log("done", res.amodPath);
        emit buildFinished(res);
        return;
    }

    // Install into <game>/mods
    std::string installLog;
    if (!ModProject::Install(amod, &installLog, &error)) {
        fail("install", DescribeError(error));
        return;
    }
    logRaw(QString::fromStdString(installLog));

    res.ok = true;
    res.installed = true;
    res.status = QStringLiteral("Built and installed");
    log("done", QStringLiteral("build succeeded"));
    emit buildFinished(res);
}
