#include "Dialogs.h"

#include "CppHighlighter.h"
#include "GameLauncher.h"
#include "ModCatalog.h"
#include "ModCompiler.h"
#include "ModPackages.h"
#include "PathUtil.h"
#include "SharedConfig.h"
#include "Theme.h"
#include "UiFormat.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <memory>

namespace {

bool HasStr(const std::vector<std::string>& list, const std::string& val) {
    return std::find(list.begin(), list.end(), val) != list.end();
}

bool IsValidModId(const std::string& id) {
    if (id.empty()) return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

QString JoinIds(const std::vector<std::string>& list) {
    QString out;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) out += QStringLiteral(", ");
        out += QString::fromStdString(list[i]);
    }
    return out;
}

std::vector<std::string> SplitIds(const QString& text) {
    std::vector<std::string> out;
    for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const std::string trimmed = part.trimmed().toStdString();
        if (!trimmed.empty() && !HasStr(out, trimmed)) out.push_back(trimmed);
    }
    return out;
}

struct ManifestForm {
    QLineEdit* nameEd = nullptr;
    QLineEdit* idEd = nullptr;
    QLineEdit* verEd = nullptr;
    QSpinBox* apiSpin = nullptr;
    QLineEdit* gameVerEd = nullptr;
    QLineEdit* authEd = nullptr;
    QLineEdit* descEd = nullptr;
    QLineEdit* entryEd = nullptr;
    QLineEdit* iconEd = nullptr;
    QLineEdit* loadAfterEd = nullptr;
    QLineEdit* conflictsEd = nullptr;
    QCheckBox* capLauncher = nullptr;
    QCheckBox* capInterop = nullptr;
    QListWidget* subsList = nullptr;

    void populate(QWidget* parent, QFormLayout* form, QVBoxLayout* root, const ModProject::Manifest& initial) {
        nameEd = new QLineEdit(QString::fromStdString(initial.name), parent);

        auto* idRow = new QHBoxLayout();
        idEd = new QLineEdit(QString::fromStdString(initial.id), parent);
        idEd->setFont(Theme::MonoFont(9));
        auto* slugBtn = new QPushButton(UiFormat::Label("mod.idFromName"), parent);
        QObject::connect(slugBtn, &QPushButton::clicked, parent, [this]() {
            idEd->setText(QString::fromStdString(ModProject::SlugOf(nameEd->text().toStdString())));
        });
        idRow->addWidget(idEd, 1);
        idRow->addWidget(slugBtn);

        verEd = new QLineEdit(QString::fromStdString(initial.version), parent);
        gameVerEd = new QLineEdit(QString::fromStdString(initial.gameVersion), parent);
        authEd = new QLineEdit(QString::fromStdString(initial.author), parent);
        descEd = new QLineEdit(QString::fromStdString(initial.description), parent);

        entryEd = new QLineEdit(QString::fromStdString(initial.entry), parent);
        entryEd->setFont(Theme::MonoFont(9));

        iconEd = new QLineEdit(QString::fromStdString(initial.icon), parent);
        iconEd->setFont(Theme::MonoFont(9));
        iconEd->setPlaceholderText(QStringLiteral("files/icon.png"));

        loadAfterEd = new QLineEdit(JoinIds(initial.loadAfter), parent);
        loadAfterEd->setFont(Theme::MonoFont(9));
        loadAfterEd->setPlaceholderText(QStringLiteral("mod_a, mod_b"));

        conflictsEd = new QLineEdit(JoinIds(initial.conflicts), parent);
        conflictsEd->setFont(Theme::MonoFont(9));
        conflictsEd->setPlaceholderText(QStringLiteral("mod_c"));

        auto* apiRow = new QHBoxLayout();
        apiSpin = new QSpinBox(parent);
        apiSpin->setRange(1, 9999);
        apiSpin->setValue(initial.apiVersion > 0 ? initial.apiVersion : ModProject::CurrentApiVersion());
        const int sdkApi = ModProject::CurrentApiVersion();
        auto* sdkApiBtn = new QPushButton(UiFormat::Label("app.dlg.manifestApiFromSdk").arg(sdkApi), parent);
        QObject::connect(sdkApiBtn, &QPushButton::clicked, parent, [this, sdkApi]() { apiSpin->setValue(sdkApi); });
        apiRow->addWidget(apiSpin);
        apiRow->addWidget(sdkApiBtn);
        apiRow->addStretch(1);

        form->addRow(UiFormat::Label("mod.name"), nameEd);
        form->addRow(UiFormat::Label("mod.id"), idRow);
        form->addRow(UiFormat::Label("mod.version"), verEd);
        form->addRow(UiFormat::Label("app.dlg.manifestApi"), apiRow);
        form->addRow(UiFormat::Label("mod.gameVersion"), gameVerEd);
        form->addRow(UiFormat::Label("mod.author"), authEd);
        form->addRow(UiFormat::Label("mod.description"), descEd);
        form->addRow(UiFormat::Label("mod.entry"), entryEd);
        form->addRow(UiFormat::Label("mod.icon"), iconEd);
        form->addRow(UiFormat::Label("mod.loadAfter"), loadAfterEd);
        form->addRow(UiFormat::Label("mod.conflicts"), conflictsEd);
        root->addLayout(form);

        auto* capLbl = new QLabel(UiFormat::Label("mod.capabilities"), parent);
        capLbl->setStyleSheet(QStringLiteral("font-weight: 600; color: #8c93a8;"));
        root->addWidget(capLbl);
        auto* capRow = new QHBoxLayout();
        capLauncher = new QCheckBox(UiFormat::Label("mod.cap.launcher"), parent);
        capInterop = new QCheckBox(UiFormat::Label("mod.cap.interop"), parent);
        capLauncher->setChecked(HasStr(initial.capabilities, "launcher"));
        capInterop->setChecked(HasStr(initial.capabilities, "interop"));
        capRow->addWidget(capLauncher);
        capRow->addWidget(capInterop);
        capRow->addStretch(1);
        root->addLayout(capRow);

        auto* reqLbl = new QLabel(UiFormat::Label("mod.requires"), parent);
        reqLbl->setStyleSheet(QStringLiteral("font-weight: 600; color: #8c93a8;"));
        root->addWidget(reqLbl);

        subsList = new QListWidget(parent);
        subsList->setMinimumHeight(110);
        subsList->setStyleSheet(QStringLiteral("QListWidget { border: 1px solid #2e3344; border-radius: 5px; }"));
        for (const std::string& sub : ModProject::Subsystems()) {
            auto* item = new QListWidgetItem(QString::fromStdString(sub), subsList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(HasStr(initial.requires_, sub) ? Qt::Checked : Qt::Unchecked);
        }
        root->addWidget(subsList, 1);
    }

    bool collect(QDialog* dlg, ModProject::Manifest& out) const {
        const std::string name = nameEd->text().trimmed().toStdString();
        if (name.empty()) {
            QMessageBox::warning(dlg, dlg->windowTitle(), UiFormat::Label("app.dlg.newProjNameEmpty"));
            return false;
        }
        const std::string id = idEd->text().trimmed().toStdString();
        if (id.empty()) {
            QMessageBox::warning(dlg, dlg->windowTitle(), UiFormat::Label("app.dlg.manifestIdEmpty"));
            return false;
        }
        if (!IsValidModId(id)) {
            QMessageBox::warning(dlg, dlg->windowTitle(), UiFormat::Label("app.dlg.manifestIdInvalid"));
            return false;
        }
        const std::string ver = verEd->text().trimmed().toStdString();
        if (ver.empty()) {
            QMessageBox::warning(dlg, dlg->windowTitle(), UiFormat::Label("app.dlg.manifestVersionEmpty"));
            return false;
        }

        const auto loadAfter = SplitIds(loadAfterEd->text());
        const auto conflicts = SplitIds(conflictsEd->text());
        if (HasStr(loadAfter, id) || HasStr(conflicts, id)) {
            QMessageBox::warning(dlg, dlg->windowTitle(), UiFormat::Label("app.dlg.manifestSelfRef"));
            return false;
        }

        out.id = id;
        out.name = name;
        out.version = ver;
        out.apiVersion = apiSpin->value();
        out.gameVersion = gameVerEd->text().trimmed().toStdString();
        out.author = authEd->text().trimmed().toStdString();
        out.description = descEd->text().trimmed().toStdString();
        out.entry = entryEd->text().trimmed().toStdString();
        out.icon = iconEd->text().trimmed().toStdString();
        out.loadAfter = loadAfter;
        out.conflicts = conflicts;

        out.capabilities.clear();
        if (capLauncher->isChecked()) out.capabilities.push_back("launcher");
        if (capInterop->isChecked()) out.capabilities.push_back("interop");

        out.requires_.clear();
        for (int i = 0; i < subsList->count(); ++i) {
            if (subsList->item(i)->checkState() == Qt::Checked) {
                out.requires_.push_back(subsList->item(i)->text().toStdString());
            }
        }
        return true;
    }
};

} // namespace

ManifestDialog::ManifestDialog(const ModProject::Manifest& manifest, QWidget* parent)
    : QDialog(parent), m_manifest(manifest) {
    setWindowTitle(UiFormat::Label("mod.manifestTitle"));
    resize(560, 680);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(8);

    auto* form = new QFormLayout();
    form->setSpacing(6);

    auto mf = std::make_shared<ManifestForm>();
    mf->populate(this, form, root, m_manifest);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Save)->setText(UiFormat::Label("common.save"));
    buttons->button(QDialogButtonBox::Cancel)->setText(UiFormat::Label("common.cancel"));
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, mf]() {
        if (mf->collect(this, m_manifest)) accept();
    });
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(UiFormat::Label("app.dlg.settingsTitle"));
    resize(620, 360);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* info = new QLabel(
        QStringLiteral("%1 <code>%2</code><br>%3 <code>%4</code>")
            .arg(UiFormat::Label("app.dlg.settingsConfig").toHtmlEscaped(),
                 QString::fromStdString(SharedConfig::File()).toHtmlEscaped(),
                 UiFormat::Label("app.dlg.settingsPackages").toHtmlEscaped(),
                 QString::fromStdString(ModPackages::Dir()).toHtmlEscaped()),
        this);
    info->setTextFormat(Qt::RichText);
    info->setStyleSheet(QStringLiteral("color: #8c93a8;"));
    root->addWidget(info);

    auto* form = new QFormLayout();
    form->setSpacing(8);

    auto makeDirPicker = [&](const QString& initial, bool fileMode = false, const QString& filter = {}) {
        auto* row = new QHBoxLayout();
        auto* ed = new QLineEdit(initial, this);
        ed->setFont(Theme::MonoFont(9));
        auto* btn = new QPushButton(UiFormat::Label("common.browse"), this);
        connect(btn, &QPushButton::clicked, this, [this, ed, fileMode, filter]() {
            const QString picked = fileMode
                ? QFileDialog::getOpenFileName(this, UiFormat::Label("app.dlg.settingsPickFile"), ed->text(), filter)
                : QFileDialog::getExistingDirectory(this, UiFormat::Label("app.dlg.settingsPickDir"), ed->text());
            if (!picked.isEmpty()) ed->setText(picked);
        });
        row->addWidget(ed, 1);
        row->addWidget(btn);
        return std::pair<QHBoxLayout*, QLineEdit*>{ row, ed };
    };

    auto [gameRow, gameEd] = makeDirPicker(QString::fromStdString(SharedConfig::GamePath()));
    auto [sdkRow, sdkEd] = makeDirPicker(QString::fromStdString(SharedConfig::Get("sdkPath", ModCompiler::SdkInclude())));
    auto [mingwRow, mingwEd] = makeDirPicker(QString::fromStdString(SharedConfig::Get("mingwPath", ModCompiler::BundledDir())));
    auto [packRow, packEd] = makeDirPicker(QString::fromStdString(SharedConfig::Get("amodPackPath", ModProject::AmodPackPath())), true, 
        QStringLiteral("AmodPack (AmodPack.exe);;All files (*.*)"));

    auto* langCombo = new QComboBox(this);
    langCombo->addItem(QStringLiteral("English (en)"), QStringLiteral("en"));
    langCombo->addItem(QStringLiteral("Русский (ru)"), QStringLiteral("ru"));
    const QString curLang = QString::fromStdString(SharedConfig::Get("language", "en"));
    langCombo->setCurrentIndex(curLang == QLatin1String("ru") ? 1 : 0);

    form->addRow(UiFormat::Label("app.dlg.settingsGameDir"), gameRow);
    form->addRow(UiFormat::Label("app.dlg.settingsSdkDir"), sdkRow);
    form->addRow(UiFormat::Label("app.dlg.settingsMingwDir"), mingwRow);
    form->addRow(UiFormat::Label("app.dlg.settingsAmodPack"), packRow);
    form->addRow(UiFormat::Label("app.dlg.settingsLang"), langCombo);
    root->addLayout(form);

    const auto tc = ModCompiler::Find();
    const QString okTxt = UiFormat::Label("app.dlg.found");
    const QString missTxt = UiFormat::Label("app.dlg.notFound");
    const QString diag = UiFormat::Label("app.dlg.settingsDiag")
        .arg(tc.ok ? okTxt : missTxt,
             QString::fromStdString(tc.machine.empty() ? "—" : tc.machine),
             GameLauncher::HasAzForge(GameLauncher::GameDir()) ? okTxt : missTxt);
    auto* diagLbl = new QLabel(diag, this);
    diagLbl->setStyleSheet(QStringLiteral("color: #8c93a8;"));
    root->addWidget(diagLbl);
    root->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Save)->setText(UiFormat::Label("common.save"));
    buttons->button(QDialogButtonBox::Cancel)->setText(UiFormat::Label("common.cancel"));
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [=, this]() {
        SharedConfig::SetGamePath(gameEd->text().trimmed().toStdString());
        SharedConfig::Set("sdkPath", sdkEd->text().trimmed().toStdString());
        if (!mingwEd->text().trimmed().isEmpty()) {
            SharedConfig::Set("mingwPath", mingwEd->text().trimmed().toStdString());
        }
        if (!packEd->text().trimmed().isEmpty()) {
            SharedConfig::Set("amodPackPath", packEd->text().trimmed().toStdString());
        }
        const std::string newLang = langCombo->currentData().toString().toStdString();
        if (newLang != SharedConfig::Get("language", "en")) {
            SharedConfig::Set("language", newLang);
            m_reloadNeeded = true;
        }
        accept();
    });
}

NewProjectDialog::NewProjectDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(UiFormat::Label("app.dlg.newProjTitle"));
    resize(580, 740);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(8);

    auto* form = new QFormLayout();
    form->setSpacing(6);

    auto* parentRow = new QHBoxLayout();
    QString defaultParent = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (defaultParent.isEmpty()) defaultParent = QString::fromStdString(PathUtil::ExeDir());
    auto* parentEd = new QLineEdit(defaultParent, this);
    auto* browseBtn = new QPushButton(UiFormat::Label("common.browse"), this);
    connect(browseBtn, &QPushButton::clicked, this, [this, parentEd]() {
        const QString dir = QFileDialog::getExistingDirectory(this, UiFormat::Label("app.dlg.settingsPickDir"), parentEd->text());
        if (!dir.isEmpty()) parentEd->setText(dir);
    });
    parentRow->addWidget(parentEd, 1);
    parentRow->addWidget(browseBtn);

    auto* previewLbl = new QLabel(this);
    previewLbl->setFont(Theme::MonoFont(9));
    previewLbl->setStyleSheet(QStringLiteral("color: #8c93a8;"));

    form->addRow(UiFormat::Label("app.dlg.newProjParent"), parentRow);
    form->addRow(UiFormat::Label("app.dlg.newProjPath"), previewLbl);

    ModProject::Manifest initial;
    initial.name = "MyMod";
    initial.id = ModProject::SlugOf(initial.name);
    initial.apiVersion = ModProject::CurrentApiVersion();
    initial.entry = "bin/" + initial.id + ".dll";

    auto mf = std::make_shared<ManifestForm>();
    mf->populate(this, form, root, initial);

    auto idManual = std::make_shared<bool>(false);
    auto entryManual = std::make_shared<bool>(false);
    auto syncing = std::make_shared<bool>(false);

    auto updatePreview = [parentEd, previewLbl, mf]() {
        const std::string slug = mf->idEd->text().trimmed().isEmpty()
            ? ModProject::SlugOf(mf->nameEd->text().toStdString())
            : mf->idEd->text().trimmed().toStdString();
        const auto full = PathUtil::MakePath(parentEd->text().toStdString()) / slug;
        previewLbl->setText(QString::fromStdString(PathUtil::U8(full)));
    };

    connect(mf->nameEd, &QLineEdit::textChanged, this, [mf, idManual, entryManual, syncing, updatePreview]() {
        if (*syncing) return;
        *syncing = true;
        if (!*idManual) {
            const std::string slug = ModProject::SlugOf(mf->nameEd->text().toStdString());
            mf->idEd->setText(QString::fromStdString(slug));
            if (!*entryManual) mf->entryEd->setText(QString::fromStdString("bin/" + slug + ".dll"));
        }
        *syncing = false;
        updatePreview();
    });
    connect(mf->idEd, &QLineEdit::textEdited, this, [mf, idManual, entryManual, syncing, updatePreview]() {
        *idManual = true;
        if (!*syncing && !*entryManual) {
            *syncing = true;
            const std::string id = mf->idEd->text().trimmed().toStdString();
            mf->entryEd->setText(QString::fromStdString(id.empty() ? std::string() : ("bin/" + id + ".dll")));
            *syncing = false;
        }
        updatePreview();
    });
    connect(mf->idEd, &QLineEdit::textChanged, this, updatePreview);
    connect(mf->entryEd, &QLineEdit::textEdited, this, [entryManual]() { *entryManual = true; });
    connect(parentEd, &QLineEdit::textChanged, this, updatePreview);
    updatePreview();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Ok)->setText(UiFormat::Label("common.create"));
    buttons->button(QDialogButtonBox::Cancel)->setText(UiFormat::Label("common.cancel"));
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, parentEd, mf]() {
        if (!mf->collect(this, m_manifest)) return;
        m_dir = PathUtil::U8(PathUtil::MakePath(parentEd->text().trimmed().toStdString()) / m_manifest.id);
        accept();
    });
}

PackagesDialog::PackagesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(UiFormat::Label("app.dlg.pkgTitle"));
    resize(720, 460);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* top = new QHBoxLayout();
    auto* dirLbl = new QLabel(
        QStringLiteral("%1 <code>%2</code>").arg(
            UiFormat::Label("app.dlg.pkgFolder").toHtmlEscaped(),
            QString::fromStdString(ModPackages::Dir()).toHtmlEscaped()),
        this);
    dirLbl->setTextFormat(Qt::RichText);
    dirLbl->setStyleSheet(QStringLiteral("color: #8c93a8;"));
    auto* openBtn = new QPushButton(UiFormat::Label("app.dlg.pkgOpenFolder"), this);
    connect(openBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(ModPackages::Dir())));
    });
    top->addWidget(dirLbl, 1);
    top->addWidget(openBtn);
    root->addLayout(top);

    auto* tree = new QTreeWidget(this);
    tree->setColumnCount(4);
    tree->setHeaderLabels({
        UiFormat::Label("app.dlg.pkgColPackage"),
        UiFormat::Label("app.dlg.pkgColVersion"),
        UiFormat::Label("app.dlg.pkgColNamespaces"),
        UiFormat::Label("app.dlg.pkgColStatus")
    });
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);

    for (const ModPackages::Package& pkg : ModPackages::All()) {
        QStringList ns;
        for (const std::string& n : pkg.namespaces) ns << QString::fromStdString(n);
        auto* item = new QTreeWidgetItem(tree);
        item->setText(0, QString::fromStdString(pkg.id));
        item->setText(1, QString::fromStdString(pkg.version.empty() ? "—" : pkg.version));
        item->setText(2, ns.join(QStringLiteral(", ")));
        if (!pkg.error.empty()) {
            item->setText(3, QString::fromStdString(pkg.error));
            item->setForeground(3, Theme::Colors().danger);
        } else {
            item->setText(3, UiFormat::Label("app.dlg.pkgLoaded"));
            item->setForeground(3, Theme::Colors().action);
        }
    }
    root->addWidget(tree, 1);

    auto* bottom = new QHBoxLayout();
    auto* reloadBtn = new QPushButton(UiFormat::Label("app.dlg.pkgReload"), this);
    auto* closeBtn = new QPushButton(UiFormat::Label("common.close"), this);
    closeBtn->setProperty("primary", true);
    bottom->addWidget(reloadBtn);
    bottom->addStretch(1);
    bottom->addWidget(closeBtn);
    root->addLayout(bottom);

    connect(reloadBtn, &QPushButton::clicked, this, [this]() {
        m_reloadRequested = true;
        accept();
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

FullCodeDialog::FullCodeDialog(const QString& code, const QString& title, QWidget* parent) : QDialog(parent) {
    setWindowTitle(title);
    resize(860, 620);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    auto* editor = new QPlainTextEdit(this);
    editor->setReadOnly(true);
    editor->setFont(Theme::MonoFont(9));
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    new CppHighlighter(editor);
    editor->setPlainText(code);
    root->addWidget(editor, 1);

    auto* closeBtn = new QPushButton(UiFormat::Label("common.close"), this);
    closeBtn->setProperty("primary", true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    root->addWidget(closeBtn, 0, Qt::AlignRight);
}
