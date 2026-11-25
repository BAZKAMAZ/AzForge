#include "BuildWorker.h"
#include "Localization.h"
#include "MainWindow.h"
#include "ModCatalog.h"
#include "ModCodegen.h"
#include "ModCompiler.h"
#include "ModLogic.h"
#include "ModPackages.h"
#include "ModProject.h"
#include "PathUtil.h"
#include "SharedConfig.h"
#include "Theme.h"

#include <QApplication>
#include <QMetaType>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int RunSmokeBuild(const std::string& projectDir) {
    const std::string lang = SharedConfig::Get("language", "en");
    Lang::Load(PathUtil::U8(PathUtil::ResourcePath("lang/" + lang + ".txt")));

    std::string err;
    if (!ModCatalog::Load(&err)) {
        std::fprintf(stderr, "[FAIL] ModCatalog::Load: %s\n", err.c_str());
        return 1;
    }
    std::printf("[OK] Catalog loaded: %zu subsystems, %zu events, %zu conditions, %zu actions, %zu expressions, %zu packages\n",
                ModCatalog::Subsystems().size(), ModCatalog::Events().size(), ModCatalog::Conditions().size(),
                ModCatalog::Actions().size(), ModCatalog::Expressions().size(), ModPackages::All().size());

    ModProject::Manifest manifest;
    if (!ModProject::LoadManifest(projectDir, manifest, &err)) {
        std::fprintf(stderr, "[FAIL] LoadManifest: %s\n", err.c_str());
        return 2;
    }
    const auto sheets = ModLogic::Load(projectDir);
    std::printf("[OK] Loaded project '%s' (%s) with %zu sheet(s)\n",
                manifest.name.c_str(), manifest.id.c_str(), sheets.size());

    std::string source;
    std::vector<std::string> warnings;
    if (!ModCodegen::Write(projectDir, sheets, &source, &err, &warnings)) {
        std::fprintf(stderr, "[FAIL] ModCodegen::Write: %s\n", err.c_str());
        return 3;
    }
    std::printf("[OK] Generated %s (%zu warnings)\n", source.c_str(), warnings.size());
    for (const auto& w : warnings) std::printf("  WARN: %s\n", w.c_str());

    ModCompiler::Extras extras;
    if (!ModCompiler::Gather("tcc", ModCodegen::UsedGroups(sheets), extras, &err)) {
        std::fprintf(stderr, "[FAIL] ModCompiler::Gather: %s\n", err.c_str());
        return 4;
    }

    const std::string dll = PathUtil::U8(PathUtil::MakePath(projectDir) / "bin" / (manifest.id + ".dll"));
    std::string log;
    if (!ModCompiler::Compile(source, dll, extras, &log, &err)) {
        std::fprintf(stderr, "[FAIL] ModCompiler::Compile: %s\n%s\n", err.c_str(), log.c_str());
        return 5;
    }
    std::printf("[OK] Compiled 32-bit DLL: %s\n", dll.c_str());

    std::string amod, packLog;
    if (!ModProject::Build(projectDir, &amod, &packLog, &err)) {
        std::fprintf(stderr, "[FAIL] ModProject::Build: %s\n%s\n", err.c_str(), packLog.c_str());
        return 6;
    }
    std::printf("[OK] Packed & validated .amod: %s\n%s\n", amod.c_str(), packLog.c_str());
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--smoke-build") {
            return RunSmokeBuild(argv[i + 1]);
        }
    }

    qRegisterMetaType<BuildResult>("BuildResult");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AFModConstructor"));
    app.setOrganizationName(QStringLiteral("AzForge"));

    Theme::Apply(app);

    const std::string lang = SharedConfig::Get("language", "en");
    const auto langPath = PathUtil::ResourcePath("lang/" + lang + ".txt");
    Lang::Load(PathUtil::U8(langPath));

    std::string catalogErr;
    ModCatalog::Load(&catalogErr);

    MainWindow window;

    std::string initialProject;
    if (argc >= 2 && argv[1][0] != '-') {
        initialProject = argv[1];
    } else {
        const auto recent = SharedConfig::GetList("afmcRecentProjects");
        for (const std::string& dir : recent) {
            if (ModProject::IsModProject(dir)) {
                initialProject = dir;
                break;
            }
        }
    }

    if (!initialProject.empty()) {
        window.openProject(initialProject);
    }

    window.show();
    return app.exec();
}
