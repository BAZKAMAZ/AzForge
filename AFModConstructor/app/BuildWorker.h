#pragma once

#include "ModLogic.h"
#include "ModProject.h"

#include <QObject>
#include <QString>
#include <QThread>

#include <string>
#include <vector>

struct BuildResult {
    bool ok = false;
    bool installed = false;
    QString status;
    QString log;
    QString amodPath;
    std::vector<std::string> warnings;
    ModProject::Manifest updatedManifest;
    bool manifestChanged = false;
};

class BuildWorker : public QThread {
    Q_OBJECT
public:
    BuildWorker(std::string projectDir, ModProject::Manifest manifest, std::vector<ModLogic::Sheet> sheets, bool install, QObject* parent = nullptr);

signals:
    void stepMessage(const QString& line);
    void buildFinished(const BuildResult& result);

protected:
    void run() override;

private:
    std::string m_dir;
    ModProject::Manifest m_manifest;
    std::vector<ModLogic::Sheet> m_sheets;
    bool m_install = true;
};
