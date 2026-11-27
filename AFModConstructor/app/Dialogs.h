#pragma once

#include "ModProject.h"

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QString>

#include <string>

class ManifestDialog : public QDialog {
    Q_OBJECT
public:
    ManifestDialog(const ModProject::Manifest& manifest, QWidget* parent = nullptr);
    ModProject::Manifest result() const { return m_manifest; }

private:
    ModProject::Manifest m_manifest;
};

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    bool reloadNeeded() const { return m_reloadNeeded; }

private:
    bool m_reloadNeeded = false;
};

class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(QWidget* parent = nullptr);
    std::string projectDir() const { return m_dir; }
    ModProject::Manifest manifest() const { return m_manifest; }

private:
    std::string m_dir;
    ModProject::Manifest m_manifest;
};

class PackagesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PackagesDialog(QWidget* parent = nullptr);
    bool reloadRequested() const { return m_reloadRequested; }

private:
    bool m_reloadRequested = false;
};

class FullCodeDialog : public QDialog {
    Q_OBJECT
public:
    FullCodeDialog(const QString& code, const QString& title, QWidget* parent = nullptr);
};

class ValueEditorDialog : public QDialog {
    Q_OBJECT
public:
    ValueEditorDialog(const QString& title, const std::string& initialValue, const std::string& paramKind, bool allowExpressions, bool preserveNewlines, QWidget* parent = nullptr);
    std::string value() const { return m_value; }

private:
    std::string m_value;
};
