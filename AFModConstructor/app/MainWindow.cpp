#include "MainWindow.h"

#include "CatalogPickerDialog.h"
#include "CppHighlighter.h"
#include "Dialogs.h"
#include "EventSheetView.h"
#include "GameLauncher.h"
#include "IconCache.h"
#include "InspectorDock.h"
#include "Localization.h"
#include "ModCatalog.h"
#include "ModCodegen.h"
#include "ModCompiler.h"
#include "ModPackages.h"
#include "PathUtil.h"
#include "SharedConfig.h"
#include "Theme.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QUrl>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

    void PopulateOutlineRecursive(const std::vector<ModLogic::Rule>& rules, std::vector<int>& path, QTreeWidgetItem* parentItem, QTreeWidget* tree) {
        for (int i = 0; i < static_cast<int>(rules.size()); ++i) {
            const ModLogic::Rule& r = rules[i];
            path.push_back(i);

            QTreeWidgetItem* item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(tree);
            const QString num = UiFormat::RuleNumber(path);

            QVariantList pathVar;
            for (int idx : path) pathVar.push_back(idx);
            item->setData(0, Qt::UserRole, pathVar);

            if (r.group) {
                const QString title = r.name.empty() ? UiFormat::Label("mod.group") : QString::fromStdString(r.name);
                item->setText(0, QStringLiteral("📁 %1 · %2 (%3)").arg(num, title).arg(r.children.size()));
                item->setForeground(0, r.enabled ? Theme::Colors().accent : Theme::Colors().textMuted);

            } else if (!r.event.empty()) {
                const auto* def = ModCatalog::FindEvent(r.event);
                const QString evText = def ? UiFormat::LinePlain(def->label, def->filters, r.filters) : QString::fromStdString(r.event);
                item->setIcon(0, IconCache::Instance().SubsystemIcon(def ? def->group : "", 15));
                item->setText(0, QStringLiteral("%1 · %2").arg(num, evText));
                item->setForeground(0, r.enabled ? Theme::Colors().text : Theme::Colors().textMuted);
            } else {
                QString summary;
                if (!r.conditions.empty()) {
                    const auto* cd = ModCatalog::FindCondition(r.conditions[0].id);
                    summary = cd ? UiFormat::LinePlain(cd->label, cd->params, r.conditions[0].args) : QString::fromStdString(r.conditions[0].id);
                    item->setIcon(0, IconCache::Instance().SubsystemIcon(cd ? cd->group : "", 15));
        
                } else if (!r.actions.empty()) {
                    const auto* ad = ModCatalog::FindAction(r.actions[0].id);
                    summary = ad ? UiFormat::LinePlain(ad->label, ad->params, r.actions[0].args) : QString::fromStdString(r.actions[0].id);
                    item->setIcon(0, IconCache::Instance().SubsystemIcon(ad ? ad->group : "", 15));
                } else {
                    summary = UiFormat::Label("app.sheet.emptySubEvent");
                }
                item->setText(0, QStringLiteral("%1 · %2").arg(num, summary));
                item->setForeground(0, r.enabled ? Theme::Colors().textMuted : Theme::Colors().textNumber);
            }

            if (!r.children.empty()) {
                PopulateOutlineRecursive(r.children, path, item, tree);
                item->setExpanded(r.group ? !r.collapsed : true);
            }
            path.pop_back();
        }
    }

    void CollectBrokenRecursive(const ModLogic::Sheet& sheet, const std::vector<ModLogic::Rule>& rules, std::vector<int>& path, bool insideRule, const QString& sheetName, QTreeWidget* tree) {
        for (int i = 0; i < static_cast<int>(rules.size()); ++i) {
            const ModLogic::Rule& r = rules[i];
            path.push_back(i);
            const QString num = UiFormat::RuleNumber(path);
            QVariantList pathVar;
            for (int idx : path) pathVar.push_back(idx);

            auto addIssue = [&](const QString& msg, bool isErr) {
                auto* it = new QTreeWidgetItem(tree);
                it->setText(0, isErr ? UiFormat::Label("app.problems.error")
                                    : UiFormat::Label("app.problems.warning"));
                it->setForeground(0, isErr ? Theme::Colors().danger : Theme::Colors().warning);
                it->setText(1, QStringLiteral("%1 / %2").arg(sheetName, num));
                it->setText(2, msg);
                it->setData(0, Qt::UserRole, pathVar);
            };

            if (!r.group) {
                if (!r.event.empty()) {
                    if (!ModCatalog::FindEvent(r.event)) {
                        addIssue(UiFormat::Label("app.problems.unknownEvent").arg(QString::fromStdString(r.event)), true);
                    } else if (insideRule) {
                        addIssue(UiFormat::Label("app.problems.nestedEvent")
                                    .arg(QString::fromStdString(r.event)), false);
                    }
                }
                for (const auto& c : r.conditions) {
                    if (!ModCatalog::FindCondition(c.id)) {
                        addIssue(UiFormat::Label("app.problems.unknownCond").arg(QString::fromStdString(c.id)), true);
                    }
                }
                for (const auto& a : r.actions) {
                    const auto* ad = ModCatalog::FindAction(a.id);
                    if (!ad) {
                        addIssue(UiFormat::Label("app.problems.unknownAct").arg(QString::fromStdString(a.id)), true);
                    } else {
                        const std::string miss = UiFormat::MissingContext(sheet, path, *ad);
                        if (!miss.empty()) {
                            addIssue(QStringLiteral("%1 — %2").arg(UiFormat::Label(ad->label), UiFormat::Label(miss)), false);
                        }
                    }
                }
            }
            
            CollectBrokenRecursive(sheet, r.children, path, insideRule || !r.group, sheetName, tree);
            path.pop_back();
        }
    }

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("AFModConstructor"));
    resize(1380, 860);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);

    setupMenus();
    setupToolbar();
    setupCentral();
    setupDocks();
    setupStatusBar();
    retranslateUi();

    QSettings settings(QStringLiteral("AzForge"), QStringLiteral("AFModConstructor"));
    if (settings.contains(QStringLiteral("geometry"))) {
        restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    }
    if (settings.contains(QStringLiteral("windowState"))) {
        restoreState(settings.value(QStringLiteral("windowState")).toByteArray());
    }
}

MainWindow::~MainWindow() {
    if (m_buildWorker && m_buildWorker->isRunning()) {
        m_buildWorker->wait(3000);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmDiscardIfDirty()) {
        event->ignore();
        return;
    }
    QSettings settings(QStringLiteral("AzForge"), QStringLiteral("AFModConstructor"));
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("windowState"), saveState());
    event->accept();
}

void MainWindow::setupMenus() {
    m_projMenu = menuBar()->addMenu(QString());
    m_actNew = m_projMenu->addAction(QString(), QKeySequence::New, this, [this]() {
        if (!confirmDiscardIfDirty()) return;
        NewProjectDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) return;
        std::string err;
        if (!ModProject::Create(dlg.projectDir(), dlg.manifest(), ModProject::Tier::Tcc, &err)) {
            QMessageBox::critical(this, windowTitle(), QString::fromStdString(err));
            return;
        }
        ModLogic::Sheet mainSheet;
        ModLogic::Create(dlg.projectDir(), "main", mainSheet, nullptr);
        openProject(dlg.projectDir());
    });

    m_actOpen = m_projMenu->addAction(QString(), QKeySequence::Open, this, [this]() {
        if (!confirmDiscardIfDirty()) return;
        const QString dir = QFileDialog::getExistingDirectory(
            this, UiFormat::Label("app.dlg.openProjectTitle"));
        if (!dir.isEmpty()) openProject(dir.toStdString());
    });

    m_recentMenu = m_projMenu->addMenu(QString());
    m_projMenu->addSeparator();

    m_actSave = m_projMenu->addAction(QString(), QKeySequence::Save, this, [this]() { saveCurrentSheet(); });
    m_actSaveAll = m_projMenu->addAction(QString(), QKeySequence(QStringLiteral("Ctrl+Shift+S")), this, [this]() {
        saveAllSheets();
    });
    m_projMenu->addSeparator();

    m_actManifest = m_projMenu->addAction(QString(), this, [this]() {
        if (m_projectDir.empty()) return;
        ManifestDialog dlg(m_manifest, this);
        if (dlg.exec() != QDialog::Accepted) return;
        std::string err;
        const auto updated = dlg.result();
        if (!ModProject::SaveManifest(m_projectDir, updated, &err)) {
            QMessageBox::warning(this, UiFormat::Label("mod.manifestTitle"), QString::fromStdString(err));
            return;
        }
        m_manifest = updated;
        m_haveManifest = true;
        rebuildProblems();
        updateWindowTitle();
        updateToolbarState();
    });

    m_actSettings = m_projMenu->addAction(QString(), this, [this]() {
        SettingsDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            if (dlg.reloadNeeded()) reloadCatalogAndUi();
            else updateToolbarState();
        }
    });
    m_projMenu->addSeparator();
    m_actExit = m_projMenu->addAction(QString(), QKeySequence::Quit, this, &QWidget::close);

    m_editMenu = menuBar()->addMenu(QString());
    m_actUndo = m_editMenu->addAction(QString(), QKeySequence::Undo, this, &MainWindow::undo);
    m_actRedo = m_editMenu->addAction(QString(), QKeySequence::Redo, this, &MainWindow::redo);
    m_editMenu->addSeparator();
    m_actCopy = m_editMenu->addAction(QString(), QKeySequence::Copy, this, [this]() { m_sheetView->copySelected(); });
    m_actPaste = m_editMenu->addAction(QString(), QKeySequence::Paste, this, [this]() { m_sheetView->pasteClipboard(); });
    m_editMenu->addSeparator();
    m_actToggleEn = m_editMenu->addAction(QString(), this, [this]() { m_sheetView->toggleEnabled(); });
    m_actToggleInv = m_editMenu->addAction(QString(), this, [this]() { m_sheetView->toggleInverted(); });
    m_actDelete = m_editMenu->addAction(QString(), QKeySequence::Delete, this, [this]() {
        QWidget* fw = QApplication::focusWidget();
        if (qobject_cast<QLineEdit*>(fw) || qobject_cast<QPlainTextEdit*>(fw) || qobject_cast<QAbstractSpinBox*>(fw)) {
            return;
        }
        m_sheetView->deleteSelected();
    });

    m_sheetMenu = menuBar()->addMenu(QString());
    m_actAddEvent = m_sheetMenu->addAction(QString(), this, [this]() { openAddPicker({}, UiFormat::Slot::Event); });
    m_actAddGroup = m_sheetMenu->addAction(QString(), this, &MainWindow::addGroup);
    m_actAddSubEvent = m_sheetMenu->addAction(QString(), this, [this]() { m_sheetView->addSubEvent(); });
    m_sheetMenu->addSeparator();
    m_actNewSheet = m_sheetMenu->addAction(QString(), this, &MainWindow::createNewSheet);
    m_sheetMenu->addSeparator();
    m_actExpandAll = m_sheetMenu->addAction(QString(), this, [this]() {
        m_sheetView->setAllGroupsCollapsed(false);
        rebuildOutlineTree();
    });
    m_actCollapseAll = m_sheetMenu->addAction(QString(), this, [this]() {
        m_sheetView->setAllGroupsCollapsed(true);
        rebuildOutlineTree();
    });

    m_buildMenu = menuBar()->addMenu(QString());
    m_actBuildOnly = m_buildMenu->addAction(QString(), QKeySequence(Qt::Key_F7), this, [this]() { startBuild(false); });
    m_actBuildInstall = m_buildMenu->addAction(QString(), QKeySequence(QStringLiteral("Ctrl+B")), this, [this]() { startBuild(true); });
    m_actRunGame = m_buildMenu->addAction(QString(), QKeySequence(Qt::Key_F5), this, &MainWindow::launchGame);
    m_buildMenu->addSeparator();
    m_actShowCode = m_buildMenu->addAction(QString(), this, [this]() {
        if (m_sheets.empty()) return;
        const std::string code = ModCodegen::Generate(m_sheets, nullptr);
        FullCodeDialog dlg(QString::fromStdString(code), UiFormat::Label("app.dlg.fullCodeTitle"), this);
        dlg.exec();
    });

    m_pkgMenu = menuBar()->addMenu(QString());
    m_actManagePkgs = m_pkgMenu->addAction(QString(), this, [this]() {
        PackagesDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted && dlg.reloadRequested()) {
            reloadCatalogAndUi();
        }
    });
    m_actReloadCatalog = m_pkgMenu->addAction(QString(), this, &MainWindow::reloadCatalogAndUi);

    m_viewMenu = menuBar()->addMenu(QString());
}

void MainWindow::setupToolbar() {
    auto* tb = addToolBar(QStringLiteral("MainToolBar"));
    tb->setObjectName(QStringLiteral("MainToolBar"));
    tb->setMovable(false);

    m_btnOpen = new QPushButton(tb);
    connect(m_btnOpen, &QPushButton::clicked, m_actOpen, &QAction::trigger);
    tb->addWidget(m_btnOpen);

    m_btnSave = new QPushButton(tb);
    connect(m_btnSave, &QPushButton::clicked, m_actSave, &QAction::trigger);
    tb->addWidget(m_btnSave);

    m_btnUndo = new QPushButton(QStringLiteral("↶"), tb);
    m_btnUndo->setFixedWidth(34);
    connect(m_btnUndo, &QPushButton::clicked, this, &MainWindow::undo);
    tb->addWidget(m_btnUndo);

    m_btnRedo = new QPushButton(QStringLiteral("↷"), tb);
    m_btnRedo->setFixedWidth(34);
    connect(m_btnRedo, &QPushButton::clicked, this, &MainWindow::redo);
    tb->addWidget(m_btnRedo);

    m_btnManifest = new QPushButton(tb);
    connect(m_btnManifest, &QPushButton::clicked, m_actManifest, &QAction::trigger);
    tb->addWidget(m_btnManifest);

    tb->addSeparator();

    m_btnAddEvent = new QPushButton(tb);
    connect(m_btnAddEvent, &QPushButton::clicked, m_actAddEvent, &QAction::trigger);
    tb->addWidget(m_btnAddEvent);

    m_btnAddGroup = new QPushButton(tb);
    connect(m_btnAddGroup, &QPushButton::clicked, m_actAddGroup, &QAction::trigger);
    tb->addWidget(m_btnAddGroup);

    m_btnToggleEn = new QPushButton(tb);
    connect(m_btnToggleEn, &QPushButton::clicked, m_actToggleEn, &QAction::trigger);
    tb->addWidget(m_btnToggleEn);

    m_btnToggleInv = new QPushButton(tb);
    connect(m_btnToggleInv, &QPushButton::clicked, m_actToggleInv, &QAction::trigger);
    tb->addWidget(m_btnToggleInv);

    auto* spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    m_installedBadge = new QLabel(tb);
    tb->addWidget(m_installedBadge);

    m_btnBuildInstall = new QPushButton(tb);
    m_btnBuildInstall->setProperty("primary", true);
    connect(m_btnBuildInstall, &QPushButton::clicked, this, [this]() { startBuild(true); });
    tb->addWidget(m_btnBuildInstall);

    m_btnRunGame = new QPushButton(tb);
    connect(m_btnRunGame, &QPushButton::clicked, this, &MainWindow::launchGame);
    tb->addWidget(m_btnRunGame);
}

void MainWindow::setupCentral() {
    auto* center = new QWidget(this);
    auto* lay = new QVBoxLayout(center);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* tabRow = new QWidget(center);
    tabRow->setStyleSheet(QStringLiteral("background: #191c24; border-bottom: 1px solid #2a2f3e;"));
    auto* tabLay = new QHBoxLayout(tabRow);
    tabLay->setContentsMargins(6, 0, 6, 0);
    tabLay->setSpacing(4);

    m_sheetTabs = new QTabBar(tabRow);
    m_sheetTabs->setExpanding(false);
    m_sheetTabs->setDrawBase(false);
    connect(m_sheetTabs, &QTabBar::currentChanged, this, [this](int idx) {
        if (m_suppressTabSignal || idx < 0 || idx >= static_cast<int>(m_sheets.size())) return;
        m_sheetIndex = idx;
        m_undo.clear();
        m_redo.clear();
        m_sheetView->setSheet(currentSheet());
        m_inspector->setContext(currentSheet(), m_sheetView->selection());
        rebuildOutlineTree();
        rebuildProblems();
        updateToolbarState();
    });

    m_btnAddSheet = new QPushButton(tabRow);
    m_btnAddSheet->setFixedHeight(26);
    connect(m_btnAddSheet, &QPushButton::clicked, this, &MainWindow::createNewSheet);

    tabLay->addWidget(m_sheetTabs, 1);
    tabLay->addWidget(m_btnAddSheet);
    lay->addWidget(tabRow);

    auto* filterBar = new QWidget(center);
    filterBar->setStyleSheet(QStringLiteral("background: #1c1f28; border-bottom: 1px solid #2a2f3e;"));
    auto* filterLay = new QHBoxLayout(filterBar);
    filterLay->setContentsMargins(8, 5, 8, 5);
    filterLay->setSpacing(8);

    m_filterEdit = new QLineEdit(filterBar);
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setMaximumWidth(320);

    m_subFilterCombo = new QComboBox(filterBar);
    m_subFilterCombo->setMinimumWidth(160);

    m_brokenOnlyCb = new QCheckBox(filterBar);

    m_btnExpand = new QPushButton(filterBar);
    m_btnCollapse = new QPushButton(filterBar);
    m_btnExpand->setFixedHeight(26);
    m_btnCollapse->setFixedHeight(26);

    filterLay->addWidget(m_filterEdit, 1);
    filterLay->addWidget(m_subFilterCombo);
    filterLay->addWidget(m_brokenOnlyCb);
    filterLay->addStretch(1);
    filterLay->addWidget(m_btnExpand);
    filterLay->addWidget(m_btnCollapse);
    lay->addWidget(filterBar);

    m_sheetView = new EventSheetView(center);
    lay->addWidget(m_sheetView, 1);
    setCentralWidget(center);

    auto applyFilter = [this]() {
        m_sheetView->setFilter(
            m_filterEdit->text(),
            m_subFilterCombo->currentData().toString().toStdString(),
            m_brokenOnlyCb->isChecked());
    };
    connect(m_filterEdit, &QLineEdit::textChanged, this, applyFilter);
    connect(m_subFilterCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, applyFilter);
    connect(m_brokenOnlyCb, &QCheckBox::toggled, this, applyFilter);
    connect(m_btnExpand, &QPushButton::clicked, this, [this]() {
        m_sheetView->setAllGroupsCollapsed(false);
        rebuildOutlineTree();
    });
    connect(m_btnCollapse, &QPushButton::clicked, this, [this]() {
        m_sheetView->setAllGroupsCollapsed(true);
        rebuildOutlineTree();
    });

    connect(m_sheetView, &EventSheetView::aboutToEdit, this, &MainWindow::pushUndoSnapshot);
    connect(m_sheetView, &EventSheetView::sheetEdited, this, [this]() {
        if (m_sheetIndex >= 0 && m_sheetIndex < static_cast<int>(m_dirtyFlags.size())) {
            m_dirtyFlags[m_sheetIndex] = true;
        }
        rebuildSheetTabs();
        rebuildOutlineTree();
        rebuildProblems();
        m_inspector->setContext(currentSheet(), m_sheetView->selection());
        updateWindowTitle();
        updateToolbarState();
    });
    connect(m_sheetView, &EventSheetView::selectionChanged, this, [this](const UiFormat::Selection& sel) {
        m_inspector->setContext(currentSheet(), sel);
        if (sel.valid()) {
            const int cnt = sel.count();
            if (sel.slot == UiFormat::Slot::Event && cnt > 1) {
                m_sbSelection->setText(UiFormat::Label("app.status.multiRules").arg(cnt));
            } else {
                QString kind;
                if (sel.slot == UiFormat::Slot::Condition) {
                    kind = cnt > 1
                        ? UiFormat::Label("app.status.multiConds").arg(cnt)
                        : UiFormat::Label("app.status.condSuffix").arg(sel.index + 1);
                } else if (sel.slot == UiFormat::Slot::Action) {
                    kind = cnt > 1
                        ? UiFormat::Label("app.status.multiActs").arg(cnt)
                        : UiFormat::Label("app.status.actSuffix").arg(sel.index + 1);
                }
                m_sbSelection->setText(UiFormat::Label("app.status.rule").arg(UiFormat::RuleNumber(sel.path)) + kind);
            }
        } else {
            m_sbSelection->setText(UiFormat::Label("app.status.noneSelected"));
        }
    });
    connect(m_sheetView, &EventSheetView::requestAdd, this, &MainWindow::openAddPicker);
    connect(m_sheetView, &EventSheetView::requestReplace, this, &MainWindow::openReplacePicker);
    connect(m_sheetView, &EventSheetView::requestFocusInspector, this, [this]() {
        m_rightDock->show();
        m_rightDock->raise();
        m_inspector->focusFirstParam();
    });
}

void MainWindow::setupDocks() {
    m_leftDock = new QDockWidget(this);
    m_leftDock->setObjectName(QStringLiteral("LeftNavDock"));
    m_leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_leftTabs = new QTabWidget(m_leftDock);

    m_outlineTree = new QTreeWidget(m_leftTabs);
    m_outlineTree->setHeaderHidden(true);
    m_outlineTree->setIndentation(14);
    connect(m_outlineTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        if (!item) return;
        const QVariantList pv = item->data(0, Qt::UserRole).toList();
        std::vector<int> path;
        for (const QVariant& v : pv) path.push_back(v.toInt());
        if (!path.empty()) m_sheetView->scrollToRule(path);
    });
    m_leftTabs->addTab(m_outlineTree, QString());

    m_projectTree = new QTreeWidget(m_leftTabs);
    m_projectTree->setHeaderHidden(true);
    m_projectTree->setIndentation(14);
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_projectTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        if (!item) return;
        const int sheetIdx = item->data(0, Qt::UserRole).toInt();
        if (sheetIdx >= 0 && sheetIdx < static_cast<int>(m_sheets.size())) {
            m_sheetTabs->setCurrentIndex(sheetIdx);
        } else if (item->data(0, Qt::UserRole + 1).toString() == QLatin1String("manifest")) {
            m_actManifest->trigger();
        }
    });
    connect(m_projectTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_projectTree->itemAt(pos);
        if (!item) return;
        const int idx = item->data(0, Qt::UserRole).toInt();
        if (idx < 0 || idx >= static_cast<int>(m_sheets.size())) return;
        QMenu menu(this);
        menu.addAction(UiFormat::Label("common.delete"), this, [this, idx]() { deleteSheet(idx); });
        menu.exec(m_projectTree->viewport()->mapToGlobal(pos));
    });
    m_leftTabs->addTab(m_projectTree, QString());

    m_leftDock->setWidget(m_leftTabs);
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);
    m_leftDock->setMinimumWidth(220);

    m_rightDock = new QDockWidget(this);
    m_rightDock->setObjectName(QStringLiteral("RightInspectorDock"));
    m_rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_inspector = new InspectorDock(m_rightDock);
    connect(m_inspector, &InspectorDock::aboutToEdit, this, &MainWindow::pushUndoSnapshot);
    connect(m_inspector, &InspectorDock::sheetEdited, this, [this]() {
        if (m_sheetIndex >= 0 && m_sheetIndex < static_cast<int>(m_dirtyFlags.size())) {
            m_dirtyFlags[m_sheetIndex] = true;
        }
        m_sheetView->refreshLayout();
        rebuildSheetTabs();
        rebuildOutlineTree();
        rebuildProblems();
        updateWindowTitle();
        updateToolbarState();
    });
    connect(m_inspector, &InspectorDock::requestReplace, this, &MainWindow::openReplacePicker);
    connect(m_inspector, &InspectorDock::requestDeleteSelected, this, [this]() { m_sheetView->deleteSelected(); });
    connect(m_inspector, &InspectorDock::requestShowFullCode, this, [this]() {
        if (m_sheets.empty()) return;
        const std::string code = ModCodegen::Generate(m_sheets, nullptr);
        FullCodeDialog dlg(QString::fromStdString(code),
                           UiFormat::Label("app.dlg.fullCodeTitle"), this);
        dlg.exec();
    });

    m_rightDock->setWidget(m_inspector);
    addDockWidget(Qt::RightDockWidgetArea, m_rightDock);
    m_rightDock->setMinimumWidth(290);

    m_bottomDock = new QDockWidget(this);
    m_bottomDock->setObjectName(QStringLiteral("BottomBuildDock"));
    m_bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    m_bottomTabs = new QTabWidget(m_bottomDock);

    m_buildLog = new QPlainTextEdit(m_bottomTabs);
    m_buildLog->setReadOnly(true);
    m_buildLog->setFont(Theme::MonoFont(9));
    m_buildLog->setPlaceholderText(QStringLiteral("ModCodegen / MinGW / AmodPack build log..."));
    m_bottomTabs->addTab(m_buildLog, QString());

    m_problemsTree = new QTreeWidget(m_bottomTabs);
    m_problemsTree->setColumnCount(3);
    m_problemsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_problemsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_problemsTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_problemsTree->setRootIsDecorated(false);
    connect(m_problemsTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        if (!it) return;
        if (it->text(1) == QLatin1String("mod.json")) {
            m_actManifest->trigger();
            return;
        }
        const QVariantList pv = it->data(0, Qt::UserRole).toList();
        std::vector<int> path;
        for (const QVariant& v : pv) path.push_back(v.toInt());
        if (!path.empty()) m_sheetView->scrollToRule(path);
    });
    m_bottomTabs->addTab(m_problemsTree, QString());

    m_bottomDock->setWidget(m_bottomTabs);
    addDockWidget(Qt::BottomDockWidgetArea, m_bottomDock);
    m_bottomDock->setMinimumHeight(140);

    resizeDocks({ m_leftDock, m_rightDock }, { 250, 320 }, Qt::Horizontal);
    resizeDocks({ m_bottomDock }, { 170 }, Qt::Vertical);

    m_viewMenu->addAction(m_leftDock->toggleViewAction());
    m_viewMenu->addAction(m_rightDock->toggleViewAction());
    m_viewMenu->addAction(m_bottomDock->toggleViewAction());
    m_viewMenu->addSeparator();
    m_actSyntaxTheme = m_viewMenu->addAction(QString(), this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(CppSyntaxTheme::FilePath())));
    });
    m_actResetLayout = m_viewMenu->addAction(QString(), this, [this]() {
        m_leftDock->setFloating(false);
        m_rightDock->setFloating(false);
        m_bottomDock->setFloating(false);
        addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);
        addDockWidget(Qt::RightDockWidgetArea, m_rightDock);
        addDockWidget(Qt::BottomDockWidgetArea, m_bottomDock);
        m_leftDock->show();
        m_rightDock->show();
        m_bottomDock->show();
        resizeDocks({ m_leftDock, m_rightDock }, { 250, 320 }, Qt::Horizontal);
        resizeDocks({ m_bottomDock }, { 170 }, Qt::Vertical);
    });
}

void MainWindow::setupStatusBar() {
    m_sbSelection = new QLabel(this);
    m_sbCatalog = new QLabel(this);
    m_sbToolchain = new QLabel(this);

    statusBar()->addWidget(m_sbSelection, 1);
    statusBar()->addPermanentWidget(m_sbCatalog);
    statusBar()->addPermanentWidget(m_sbToolchain);
}

void MainWindow::retranslateUi() {
    m_projMenu->setTitle(UiFormat::Label("app.menu.project"));
    m_actNew->setText(UiFormat::Label("app.menu.project.new"));
    m_actOpen->setText(UiFormat::Label("app.menu.project.open"));
    m_recentMenu->setTitle(UiFormat::Label("app.menu.project.recent"));
    m_actSave->setText(UiFormat::Label("app.menu.project.save"));
    m_actSaveAll->setText(UiFormat::Label("app.menu.project.saveAll"));
    m_actManifest->setText(UiFormat::Label("mod.manifestTitle") + QStringLiteral("…"));
    m_actSettings->setText(UiFormat::Label("app.menu.project.settings"));
    m_actExit->setText(UiFormat::Label("app.menu.project.exit"));

    m_editMenu->setTitle(UiFormat::Label("app.menu.edit"));
    m_actUndo->setText(UiFormat::Label("mod.undo"));
    m_actRedo->setText(UiFormat::Label("mod.redo"));
    m_actCopy->setText(UiFormat::Label("common.copy"));
    m_actPaste->setText(UiFormat::Label("common.paste"));
    m_actToggleEn->setText(UiFormat::Label("app.menu.edit.toggleEnabled"));
    m_actToggleInv->setText(UiFormat::Label("app.menu.edit.toggleInvert"));
    m_actDelete->setText(UiFormat::Label("app.menu.edit.delete"));

    m_sheetMenu->setTitle(UiFormat::Label("app.menu.sheet"));
    m_actAddEvent->setText(UiFormat::Label("app.menu.sheet.addEvent"));
    m_actAddGroup->setText(UiFormat::Label("app.menu.sheet.addGroup"));
    m_actAddSubEvent->setText(UiFormat::Label("app.menu.sheet.addSubEvent"));
    m_actNewSheet->setText(UiFormat::Label("app.menu.sheet.newSheet"));
    m_actExpandAll->setText(UiFormat::Label("app.menu.sheet.expandAll"));
    m_actCollapseAll->setText(UiFormat::Label("app.menu.sheet.collapseAll"));

    m_buildMenu->setTitle(UiFormat::Label("app.menu.build"));
    m_actBuildOnly->setText(UiFormat::Label("app.menu.build.packOnly"));
    m_actBuildInstall->setText(UiFormat::Label("app.menu.build.buildInstall"));
    m_actRunGame->setText(UiFormat::Label("app.menu.build.runGame"));
    m_actShowCode->setText(UiFormat::Label("app.menu.build.showCode"));

    m_pkgMenu->setTitle(UiFormat::Label("app.menu.packages"));
    m_actManagePkgs->setText(UiFormat::Label("app.menu.packages.manage"));
    m_actReloadCatalog->setText(UiFormat::Label("app.menu.packages.reload"));

    m_viewMenu->setTitle(UiFormat::Label("app.menu.view"));
    m_actSyntaxTheme->setText(UiFormat::Label("app.menu.view.syntaxTheme"));
    m_actResetLayout->setText(UiFormat::Label("app.menu.view.resetLayout"));

    m_btnOpen->setText(UiFormat::Label("app.toolbar.open"));
    m_btnSave->setText(UiFormat::Label("common.save"));
    m_btnUndo->setToolTip(UiFormat::Label("mod.undo"));
    m_btnRedo->setToolTip(UiFormat::Label("mod.redo"));
    m_btnManifest->setText(UiFormat::Label("mod.editManifest"));
    m_btnAddEvent->setText(UiFormat::Label("app.toolbar.addEvent"));
    m_btnAddGroup->setText(UiFormat::Label("app.toolbar.addGroup"));
    m_btnToggleEn->setText(UiFormat::Label("app.toolbar.toggleEn"));
    m_btnToggleInv->setText(UiFormat::Label("app.toolbar.toggleInv"));
    const bool building = m_buildWorker && m_buildWorker->isRunning();
    m_btnBuildInstall->setText(building ? UiFormat::Label("app.toolbar.building")
                                        : UiFormat::Label("app.toolbar.buildInstall"));
    m_btnRunGame->setText(UiFormat::Label("app.toolbar.runGame"));

    m_btnAddSheet->setText(UiFormat::Label("app.toolbar.addSheet"));
    m_filterEdit->setPlaceholderText(UiFormat::Label("app.filter.placeholder"));
    m_brokenOnlyCb->setText(UiFormat::Label("app.filter.brokenOnly"));
    m_btnExpand->setText(UiFormat::Label("app.filter.expand"));
    m_btnCollapse->setText(UiFormat::Label("app.filter.collapse"));

    m_leftDock->setWindowTitle(UiFormat::Label("app.dock.nav"));
    m_leftTabs->setTabText(0, UiFormat::Label("app.dock.outline"));
    m_leftTabs->setTabText(1, UiFormat::Label("app.dock.project"));
    m_rightDock->setWindowTitle(UiFormat::Label("app.dock.inspector"));
    m_bottomDock->setWindowTitle(UiFormat::Label("app.dock.build"));
    m_bottomTabs->setTabText(0, UiFormat::Label("app.dock.buildOutput"));

    m_problemsTree->setHeaderLabels({
        UiFormat::Label("app.problems.colType"),
        UiFormat::Label("app.problems.colWhere"),
        UiFormat::Label("app.problems.colDesc")
    });

    if (!m_sheetView->selection().valid()) {
        m_sbSelection->setText(UiFormat::Label("app.status.ready"));
    }

    m_inspector->retranslateUi();
    rebuildSubsystemFilter();
    rebuildOutlineTree();
    rebuildProblems();
    updateRecentMenu();
    updateToolbarState();
    m_sheetView->refreshLayout();
}

void MainWindow::reloadCatalogAndUi() {
    const std::string lang = SharedConfig::Get("language", "en");
    const auto langFile = PathUtil::ResourcePath("lang/" + lang + ".txt");
    Lang::Load(PathUtil::U8(langFile));

    IconCache::Instance().Clear();
    CppSyntaxWatcher::Instance().reload();
    std::string err;
    ModCatalog::Load(&err);

    retranslateUi();
}

bool MainWindow::openProject(const std::string& projectDir) {
    if (projectDir.empty()) return false;

    ModProject::Manifest manifest;
    std::string err;
    if (!ModProject::LoadManifest(projectDir, manifest, &err)) {
        QMessageBox::warning(this, UiFormat::Label("app.menu.project.open"),
                             UiFormat::Label("app.dlg.noManifestMsg") +
                             QStringLiteral("\n") + QString::fromStdString(err));
        return false;
    }

    m_projectDir = projectDir;
    m_manifest = std::move(manifest);
    m_haveManifest = true;

    m_sheets = ModLogic::Load(m_projectDir);
    m_dirtyFlags.assign(m_sheets.size(), false);
    m_sheetIndex = m_sheets.empty() ? -1 : 0;
    m_undo.clear();
    m_redo.clear();

    m_sheetView->setSheet(currentSheet());
    m_inspector->setContext(currentSheet(), m_sheetView->selection());

    rebuildSheetTabs();
    rebuildProjectTree();
    rebuildOutlineTree();
    rebuildProblems();
    addRecentProject(m_projectDir);
    updateWindowTitle();
    updateToolbarState();

    m_buildLog->setPlainText(
        QStringLiteral("[project]  %1 (%2 v%3, api %4)\n[path]     %5")
            .arg(QString::fromStdString(m_manifest.name),
                 QString::fromStdString(m_manifest.id),
                 QString::fromStdString(m_manifest.version))
            .arg(m_manifest.apiVersion)
            .arg(QString::fromStdString(m_projectDir)));
    return true;
}

ModLogic::Sheet* MainWindow::currentSheet() {
    if (m_sheetIndex < 0 || m_sheetIndex >= static_cast<int>(m_sheets.size())) return nullptr;
    return &m_sheets[m_sheetIndex];
}

const ModLogic::Sheet* MainWindow::currentSheet() const {
    if (m_sheetIndex < 0 || m_sheetIndex >= static_cast<int>(m_sheets.size())) return nullptr;
    return &m_sheets[m_sheetIndex];
}

bool MainWindow::saveCurrentSheet() {
    ModLogic::Sheet* s = currentSheet();
    if (!s) return true;
    std::string err;
    if (!ModLogic::Save(*s, &err)) {
        QMessageBox::warning(this, UiFormat::Label("common.save"), QString::fromStdString(err));
        return false;
    }
    m_dirtyFlags[m_sheetIndex] = false;
    rebuildSheetTabs();
    updateWindowTitle();
    statusBar()->showMessage(UiFormat::Label("app.status.saved").arg(QString::fromStdString(s->file)), 3000);
    return true;
}

bool MainWindow::saveAllSheets() {
    for (size_t i = 0; i < m_sheets.size(); ++i) {
        if (!m_dirtyFlags[i]) continue;
        std::string err;
        if (!ModLogic::Save(m_sheets[i], &err)) {
            QMessageBox::warning(this, UiFormat::Label("common.save"), QString::fromStdString(err));
            return false;
        }
        m_dirtyFlags[i] = false;
    }
    rebuildSheetTabs();
    updateWindowTitle();
    statusBar()->showMessage(UiFormat::Label("app.status.savedAll"), 3000);
    return true;
}

bool MainWindow::confirmDiscardIfDirty() {
    const bool anyDirty = std::any_of(m_dirtyFlags.begin(), m_dirtyFlags.end(), [](bool d) { return d; });
    if (!anyDirty) return true;

    const auto ans = QMessageBox::question(
        this,
        UiFormat::Label("app.dlg.unsavedTitle"),
        UiFormat::Label("app.dlg.unsavedMsg"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (ans == QMessageBox::Cancel) return false;
    if (ans == QMessageBox::Save) return saveAllSheets();
    return true;
}

void MainWindow::pushUndoSnapshot() {
    if (const ModLogic::Sheet* s = currentSheet()) {
        m_undo.push_back(*s);
        if (m_undo.size() > 64) m_undo.erase(m_undo.begin());
        m_redo.clear();
    }
}

void MainWindow::undo() {
    ModLogic::Sheet* s = currentSheet();
    if (!s || m_undo.empty()) return;
    m_redo.push_back(*s);
    *s = m_undo.back();
    m_undo.pop_back();
    m_dirtyFlags[m_sheetIndex] = true;
    m_sheetView->setSelection({}, false);
    m_sheetView->refreshLayout();
    m_inspector->setContext(s, m_sheetView->selection());
    rebuildSheetTabs();
    rebuildOutlineTree();
    rebuildProblems();
    updateWindowTitle();
    updateToolbarState();
}

void MainWindow::redo() {
    ModLogic::Sheet* s = currentSheet();
    if (!s || m_redo.empty()) return;
    m_undo.push_back(*s);
    *s = m_redo.back();
    m_redo.pop_back();
    m_dirtyFlags[m_sheetIndex] = true;
    m_sheetView->setSelection({}, false);
    m_sheetView->refreshLayout();
    m_inspector->setContext(s, m_sheetView->selection());
    rebuildSheetTabs();
    rebuildOutlineTree();
    rebuildProblems();
    updateWindowTitle();
    updateToolbarState();
}

void MainWindow::rebuildSheetTabs() {
    m_suppressTabSignal = true;
    while (m_sheetTabs->count() > 0) m_sheetTabs->removeTab(0);
    for (size_t i = 0; i < m_sheets.size(); ++i) {
        const bool dirty = i < m_dirtyFlags.size() && m_dirtyFlags[i];
        const QString title = (dirty ? QStringLiteral("* ") : QString()) +
                              QString::fromStdString(m_sheets[i].name) + QStringLiteral(".json");
        m_sheetTabs->addTab(title);
    }
    if (m_sheetIndex >= 0 && m_sheetIndex < m_sheetTabs->count()) {
        m_sheetTabs->setCurrentIndex(m_sheetIndex);
    }
    m_suppressTabSignal = false;
}

void MainWindow::rebuildProjectTree() {
    m_projectTree->clear();
    if (m_projectDir.empty()) return;

    auto* root = new QTreeWidgetItem(m_projectTree);
    root->setText(0, QString::fromStdString(m_manifest.name.empty() ? m_manifest.id : m_manifest.name));
    root->setData(0, Qt::UserRole, -1);
    QFont bf = root->font(0);
    bf.setBold(true);
    root->setFont(0, bf);

    auto* manItem = new QTreeWidgetItem(root);
    manItem->setText(0, QStringLiteral("📄 mod.json (API %1)").arg(m_manifest.apiVersion));
    manItem->setData(0, Qt::UserRole, -1);
    manItem->setData(0, Qt::UserRole + 1, QStringLiteral("manifest"));

    auto* logicFolder = new QTreeWidgetItem(root);
    logicFolder->setText(0, QStringLiteral("📂 logic/"));
    logicFolder->setData(0, Qt::UserRole, -1);
    for (int i = 0; i < static_cast<int>(m_sheets.size()); ++i) {
        auto* si = new QTreeWidgetItem(logicFolder);
        si->setText(0, QStringLiteral("⚡ %1.json (%2)").arg(
            QString::fromStdString(m_sheets[i].name)).arg(m_sheets[i].rules.size()));
        si->setData(0, Qt::UserRole, i);
    }

    std::error_code ec;
    const fs::path pRoot = PathUtil::MakePath(m_projectDir);
    for (const char* folderName : { "files", "bin" }) {
        auto* fItem = new QTreeWidgetItem(root);
        fItem->setText(0, QStringLiteral("📂 %1/").arg(QString::fromLatin1(folderName)));
        fItem->setData(0, Qt::UserRole, -1);
        const fs::path sub = pRoot / folderName;
        if (fs::is_directory(sub, ec)) {
            int count = 0;
            for (const auto& e : fs::recursive_directory_iterator(sub, ec)) {
                if (!e.is_regular_file(ec)) continue;
                auto* fi = new QTreeWidgetItem(fItem);
                fi->setText(0, QString::fromStdString(PathUtil::U8(fs::relative(e.path(), sub, ec))));
                fi->setData(0, Qt::UserRole, -1);
                if (++count >= 60) break;
            }
        }
    }

    root->setExpanded(true);
    logicFolder->setExpanded(true);
}

void MainWindow::rebuildOutlineTree() {
    m_outlineTree->clear();
    const ModLogic::Sheet* s = currentSheet();
    if (!s) return;
    std::vector<int> path;
    PopulateOutlineRecursive(s->rules, path, nullptr, m_outlineTree);
}

void MainWindow::rebuildSubsystemFilter() {
    const QString prev = m_subFilterCombo->currentData().toString();
    m_subFilterCombo->blockSignals(true);
    m_subFilterCombo->clear();
    m_subFilterCombo->addItem(UiFormat::Label("app.filter.allSubsystems"), QString());
    for (const ModCatalog::SubsystemDef& sub : ModCatalog::Subsystems()) {
        m_subFilterCombo->addItem(
            IconCache::Instance().SubsystemIcon(sub.id, 16),
            UiFormat::Label(sub.label),
            QString::fromStdString(sub.id));
    }
    const int idx = m_subFilterCombo->findData(prev);
    m_subFilterCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_subFilterCombo->blockSignals(false);
}

void MainWindow::rebuildProblems() {
    m_problemsTree->clear();
    if (m_haveManifest) {
        const int sdkApi = ModProject::CurrentApiVersion();
        if (m_manifest.apiVersion > sdkApi) {
            auto* it = new QTreeWidgetItem(m_problemsTree);
            it->setText(0, UiFormat::Label("app.problems.error"));
            it->setForeground(0, Theme::Colors().danger);
            it->setText(1, QStringLiteral("mod.json"));
            it->setText(2, UiFormat::Label("app.problems.apiMismatch")
                .arg(m_manifest.apiVersion).arg(sdkApi));
        }
    }
    if (const ModLogic::Sheet* s = currentSheet()) {
        std::vector<int> path;
        CollectBrokenRecursive(*s, s->rules, path, false, QString::fromStdString(s->name), m_problemsTree);
    }

    if (!m_sheets.empty()) {
        std::vector<std::string> warnings;
        ModCodegen::Generate(m_sheets, &warnings);
        for (const std::string& w : warnings) {
            auto* it = new QTreeWidgetItem(m_problemsTree);
            it->setText(0, UiFormat::Label("app.problems.warning"));
            it->setForeground(0, Theme::Colors().warning);
            it->setText(1, QStringLiteral("Codegen"));
            it->setText(2, QString::fromStdString(w));
        }
    }

    const int n = m_problemsTree->topLevelItemCount();
    m_bottomTabs->setTabText(1, QStringLiteral("%1 (%2)")
        .arg(UiFormat::Label("app.dock.problems")).arg(n));
}

void MainWindow::updateWindowTitle() {
    const bool anyDirty = std::any_of(m_dirtyFlags.begin(), m_dirtyFlags.end(), [](bool d) { return d; });
    if (!m_haveManifest) {
        setWindowTitle(QStringLiteral("AFModConstructor"));
        return;
    }
    setWindowTitle(QStringLiteral("AFModConstructor — %1 (%2 v%3)%4")
        .arg(QString::fromStdString(m_manifest.name),
             QString::fromStdString(m_manifest.id),
             QString::fromStdString(m_manifest.version),
             anyDirty ? QStringLiteral(" *") : QString()));
}

void MainWindow::updateToolbarState() {
    const bool hasProj = m_haveManifest && !m_projectDir.empty();
    const bool hasSheet = currentSheet() != nullptr;
    const bool building = m_buildWorker && m_buildWorker->isRunning();

    m_actSave->setEnabled(hasSheet);
    m_btnSave->setEnabled(hasSheet);
    m_actManifest->setEnabled(hasProj);
    m_btnManifest->setEnabled(hasProj);
    m_actUndo->setEnabled(!m_undo.empty());
    m_btnUndo->setEnabled(!m_undo.empty());
    m_actRedo->setEnabled(!m_redo.empty());
    m_btnRedo->setEnabled(!m_redo.empty());
    m_actAddEvent->setEnabled(hasSheet);
    m_btnAddEvent->setEnabled(hasSheet);
    m_actAddGroup->setEnabled(hasSheet);
    m_btnAddGroup->setEnabled(hasSheet);
    m_actBuildOnly->setEnabled(hasProj && !building);
    m_actBuildInstall->setEnabled(hasProj && !building);
    m_btnBuildInstall->setEnabled(hasProj && !building);

    const std::string gameDir = GameLauncher::GameDir();
    const bool haveGame = !gameDir.empty();
    m_actRunGame->setEnabled(haveGame);
    m_btnRunGame->setEnabled(haveGame);

    std::error_code ec;
    const bool installed = haveGame && hasProj &&
                           fs::exists(PathUtil::MakePath(gameDir) / "mods" / (m_manifest.id + ".amod"), ec);
    if (!haveGame) {
        m_installedBadge->setText(QStringLiteral("○ ") + UiFormat::Label("mod.noGamePath"));
        m_installedBadge->setStyleSheet(QStringLiteral(
            "padding: 4px 10px; border-radius: 4px; background: #16181f; border: 1px solid #3a3345; color: #e5b85c;"));
    } else if (installed) {
        m_installedBadge->setText(QStringLiteral("● ") + UiFormat::Label("mod.inGame"));
        m_installedBadge->setStyleSheet(QStringLiteral(
            "padding: 4px 10px; border-radius: 4px; background: #16181f; border: 1px solid #2b4538; color: #6fd49b;"));
    } else {
        m_installedBadge->setText(QStringLiteral("○ ") + UiFormat::Label("mod.notInGame"));
        m_installedBadge->setStyleSheet(QStringLiteral(
            "padding: 4px 10px; border-radius: 4px; background: #16181f; border: 1px solid #2e3344; color: #8c93a8;"));
    }

    m_sbCatalog->setText(UiFormat::Label("app.status.catalog")
        .arg(ModCatalog::Subsystems().size())
        .arg(ModPackages::All().size()));

    const auto tc = ModCompiler::Find();
    const bool hasPack = !ModProject::AmodPackPath().empty();
    m_sbToolchain->setText(QStringLiteral("MinGW: %1 · AmodPack: %2").arg(tc.ok 
            ? QString::fromStdString(tc.machine) : QStringLiteral("missing"),
             hasPack ? QStringLiteral("OK") : QStringLiteral("Zip")));
}

void MainWindow::addRecentProject(const std::string& dir) {
    std::vector<std::string> recent = SharedConfig::GetList("afmcRecentProjects");
    recent.erase(std::remove(recent.begin(), recent.end(), dir), recent.end());
    recent.insert(recent.begin(), dir);
    if (recent.size() > 8) recent.resize(8);
    SharedConfig::SetList("afmcRecentProjects", recent);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu() {
    m_recentMenu->clear();
    const auto recent = SharedConfig::GetList("afmcRecentProjects");
    for (const std::string& dir : recent) {
        m_recentMenu->addAction(QString::fromStdString(dir), this, [this, dir]() {
            if (confirmDiscardIfDirty()) openProject(dir);
        });
    }

    if (m_recentMenu->isEmpty()) {
        m_recentMenu->addAction(UiFormat::Label("app.status.empty"))->setEnabled(false);
    }
}

void MainWindow::openAddPicker(const std::vector<int>& path, UiFormat::Slot slot) {
    ModLogic::Sheet* sheet = currentSheet();
    if (!sheet) return;

    CatalogPickerDialog dlg(slot, sheet, path, {}, this);
    if (dlg.exec() != QDialog::Accepted || dlg.chosenId().empty()) return;

    pushUndoSnapshot();
    const std::string id = dlg.chosenId();

    if (slot == UiFormat::Slot::Event) {
        if (dlg.pickedConditionAsEvent()) {
            ModLogic::Rule rule;
            rule.conditions.push_back(ModLogic::MakeCondition(id));
            sheet->rules.push_back(std::move(rule));
            const std::vector<int> newPath{ static_cast<int>(sheet->rules.size()) - 1 };
            m_sheetView->refreshLayout();
            m_sheetView->setSelection({ newPath, UiFormat::Slot::Condition, 0, {}, {} }, true);
        } else {
            sheet->rules.push_back(ModLogic::MakeRule(id));
            const std::vector<int> newPath{ static_cast<int>(sheet->rules.size()) - 1 };
            m_sheetView->refreshLayout();
            m_sheetView->setSelection({ newPath, UiFormat::Slot::Event, -1, {}, { newPath } }, true);
        }
    } else if (ModLogic::Rule* rule = UiFormat::RuleAt(*sheet, path)) {
        if (slot == UiFormat::Slot::Condition) {
            rule->conditions.push_back(ModLogic::MakeCondition(id));
            const int idx = static_cast<int>(rule->conditions.size()) - 1;
            m_sheetView->refreshLayout();
            m_sheetView->setSelection({ path, UiFormat::Slot::Condition, idx, {}, {} }, true);
        } else {
            rule->actions.push_back(ModLogic::MakeAction(id));
            const int idx = static_cast<int>(rule->actions.size()) - 1;
            m_sheetView->refreshLayout();
            m_sheetView->setSelection({ path, UiFormat::Slot::Action, idx, {}, {} }, true);
        }
    }

    m_dirtyFlags[m_sheetIndex] = true;
    rebuildSheetTabs();
    rebuildOutlineTree();
    rebuildProblems();
    updateWindowTitle();
    updateToolbarState();
    m_inspector->focusFirstParam();
}

void MainWindow::openReplacePicker(const std::vector<int>& path, UiFormat::Slot slot, int index) {
    ModLogic::Sheet* sheet = currentSheet();
    if (!sheet) return;
    ModLogic::Rule* rule = UiFormat::RuleAt(*sheet, path);
    if (!rule) return;

    std::string curId;
    if (slot == UiFormat::Slot::Event) curId = rule->event;
    else if (slot == UiFormat::Slot::Condition && index >= 0 && index < static_cast<int>(rule->conditions.size()))
        curId = rule->conditions[index].id;
    else if (slot == UiFormat::Slot::Action && index >= 0 && index < static_cast<int>(rule->actions.size()))
        curId = rule->actions[index].id;

    CatalogPickerDialog dlg(slot, sheet, path, curId, this);
    if (dlg.exec() != QDialog::Accepted || dlg.chosenId().empty()) return;

    pushUndoSnapshot();
    const std::string id = dlg.chosenId();

    if (slot == UiFormat::Slot::Event) {
        if (dlg.pickedConditionAsEvent()) {
            rule->event.clear();
            rule->filters.clear();
            rule->conditions.insert(rule->conditions.begin(), ModLogic::MakeCondition(id));
            m_sheetView->refreshLayout();
            m_sheetView->setSelection({ path, UiFormat::Slot::Condition, 0, {}, {} }, true);
        } else {
            const ModLogic::Rule fresh = ModLogic::MakeRule(id);
            rule->event = fresh.event;
            rule->filters = fresh.filters;
            m_sheetView->refreshLayout();
            m_sheetView->setSelection({ path, UiFormat::Slot::Event, -1, {}, { path } }, true);
        }
    } else if (slot == UiFormat::Slot::Condition && index >= 0 && index < static_cast<int>(rule->conditions.size())) {
        const bool inv = rule->conditions[index].inverted;
        const bool en = rule->conditions[index].enabled;
        rule->conditions[index] = ModLogic::MakeCondition(id);
        rule->conditions[index].inverted = inv;
        rule->conditions[index].enabled = en;
        m_sheetView->refreshLayout();
        m_sheetView->setSelection({ path, UiFormat::Slot::Condition, index, {}, {} }, true);
    } else if (slot == UiFormat::Slot::Action && index >= 0 && index < static_cast<int>(rule->actions.size())) {
        const bool en = rule->actions[index].enabled;
        rule->actions[index] = ModLogic::MakeAction(id);
        rule->actions[index].enabled = en;
        m_sheetView->refreshLayout();
        m_sheetView->setSelection({ path, UiFormat::Slot::Action, index, {}, {} }, true);
    }

    m_dirtyFlags[m_sheetIndex] = true;
    rebuildSheetTabs();
    rebuildOutlineTree();
    rebuildProblems();
    updateWindowTitle();
    updateToolbarState();
    m_inspector->focusFirstParam();
}

void MainWindow::createNewSheet() {
    if (m_projectDir.empty()) return;
    bool ok = false;

    const QString name = QInputDialog::getText(this, UiFormat::Label("app.dlg.newSheetTitle"), UiFormat::Label("app.dlg.newSheetPrompt"),
        QLineEdit::Normal, QStringLiteral("sheet"), &ok);

    if (!ok || name.trimmed().isEmpty()) return;

    ModLogic::Sheet sheet;
    std::string err;
    if (!ModLogic::Create(m_projectDir, name.trimmed().toStdString(), sheet, &err)) {
        QMessageBox::warning(this, UiFormat::Label("app.dlg.newSheetTitle"), QString::fromStdString(err));
        return;
    }
    m_sheets.push_back(std::move(sheet));
    m_dirtyFlags.push_back(false);
    m_sheetIndex = static_cast<int>(m_sheets.size()) - 1;
    rebuildSheetTabs();
    rebuildProjectTree();
    m_sheetView->setSheet(currentSheet());
    m_inspector->setContext(currentSheet(), m_sheetView->selection());
    rebuildOutlineTree();
    updateToolbarState();
}

void MainWindow::deleteSheet(int index) {
    if (index < 0 || index >= static_cast<int>(m_sheets.size())) return;
    const auto ans = QMessageBox::question(this, UiFormat::Label("app.dlg.deleteSheetTitle"), 
        UiFormat::Label("app.dlg.deleteSheetMsg").arg(QString::fromStdString(m_sheets[index].name)));

    if (ans != QMessageBox::Yes) return;

    std::string err;
    if (!ModLogic::Remove(m_sheets[index], &err)) {
        QMessageBox::warning(this, UiFormat::Label("app.dlg.deleteSheetTitle"), QString::fromStdString(err));
        return;
    }
    m_sheets.erase(m_sheets.begin() + index);
    m_dirtyFlags.erase(m_dirtyFlags.begin() + index);
    if (m_sheetIndex >= static_cast<int>(m_sheets.size())) m_sheetIndex = static_cast<int>(m_sheets.size()) - 1;
    rebuildSheetTabs();
    rebuildProjectTree();
    m_sheetView->setSheet(currentSheet());
    m_inspector->setContext(currentSheet(), m_sheetView->selection());
    rebuildOutlineTree();
    updateToolbarState();
}

void MainWindow::addGroup() {
    ModLogic::Sheet* sheet = currentSheet();
    if (!sheet) return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        UiFormat::Label("app.dlg.newGroupTitle"),
        UiFormat::Label("app.dlg.newGroupPrompt"),
        QLineEdit::Normal, UiFormat::Label("mod.group"), &ok);
    if (!ok) return;

    pushUndoSnapshot();
    ModLogic::Rule grp;
    grp.group = true;
    grp.name = name.trimmed().isEmpty() ? UiFormat::Label("mod.group").toStdString()
                                        : name.trimmed().toStdString();

    sheet->rules.push_back(std::move(grp));
    const std::vector<int> newPath{ static_cast<int>(sheet->rules.size()) - 1 };
    m_dirtyFlags[m_sheetIndex] = true;
    m_sheetView->refreshLayout();
    m_sheetView->setSelection({ newPath, UiFormat::Slot::Event, -1, {}, { newPath } }, true);
    rebuildSheetTabs();
    rebuildOutlineTree();
    updateWindowTitle();
}

void MainWindow::startBuild(bool install) {
    if (m_projectDir.empty() || !m_haveManifest) return;
    if (m_buildWorker && m_buildWorker->isRunning()) return;
    if (!saveAllSheets()) return;

    m_bottomDock->show();
    m_bottomTabs->setCurrentIndex(0);
    m_buildLog->clear();
    m_btnBuildInstall->setText(UiFormat::Label("app.toolbar.building"));

    m_buildWorker = new BuildWorker(m_projectDir, m_manifest, m_sheets, install, this);
    connect(m_buildWorker, &BuildWorker::stepMessage, this, [this](const QString& line) {
        m_buildLog->appendPlainText(line);
    });
    connect(m_buildWorker, &BuildWorker::buildFinished, this, [this](const BuildResult& res) {
        m_btnBuildInstall->setText(UiFormat::Label("app.toolbar.buildInstall"));
        if (res.manifestChanged) m_manifest = res.updatedManifest;
        rebuildProjectTree();
        rebuildProblems();
        updateToolbarState();
        statusBar()->showMessage(res.status, 5000);
        m_buildWorker->deleteLater();
        m_buildWorker = nullptr;
    });

    updateToolbarState();
    m_buildWorker->start();
}

void MainWindow::launchGame() {
    const std::string gameDir = GameLauncher::GameDir();
    if (!GameLauncher::HasAzForge(gameDir)) {
        const auto ans = QMessageBox::warning(
            this,
            UiFormat::Label("app.dlg.noAzForgeTitle"),
            UiFormat::Label("app.dlg.noAzForgeMsg"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ans != QMessageBox::Yes) return;
    }

    std::string err;
    if (!GameLauncher::Launch(&err)) {
        QMessageBox::critical(this, UiFormat::Label("app.dlg.launchError"), QString::fromStdString(err));
        return;
    }
    m_buildLog->appendPlainText(QStringLiteral("[run]      %1/game.exe").arg(QString::fromStdString(gameDir)));
    statusBar()->showMessage(UiFormat::Label("app.status.gameLaunched"), 3000);
}
