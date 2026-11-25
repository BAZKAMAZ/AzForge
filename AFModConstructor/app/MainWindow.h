#pragma once

#include "BuildWorker.h"
#include "ModLogic.h"
#include "ModProject.h"
#include "UiFormat.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeWidget>

#include <string>
#include <vector>

class EventSheetView;
class InspectorDock;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openProject(const std::string& projectDir);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupMenus();
    void setupToolbar();
    void setupCentral();
    void setupDocks();
    void setupStatusBar();
    void retranslateUi();

    void reloadCatalogAndUi();
    void rebuildSheetTabs();
    void rebuildProjectTree();
    void rebuildOutlineTree();
    void rebuildSubsystemFilter();
    void rebuildProblems();
    void updateWindowTitle();
    void updateToolbarState();
    void updateRecentMenu();
    void addRecentProject(const std::string& dir);

    ModLogic::Sheet* currentSheet();
    const ModLogic::Sheet* currentSheet() const;
    bool saveCurrentSheet();
    bool saveAllSheets();
    bool confirmDiscardIfDirty();

    void pushUndoSnapshot();
    void undo();
    void redo();

    void openAddPicker(const std::vector<int>& path, UiFormat::Slot slot);
    void openReplacePicker(const std::vector<int>& path, UiFormat::Slot slot, int index);
    void createNewSheet();
    void deleteSheet(int index);
    void addGroup();

    void startBuild(bool install);
    void launchGame();

    std::string m_projectDir;
    ModProject::Manifest m_manifest;
    bool m_haveManifest = false;

    std::vector<ModLogic::Sheet> m_sheets;
    std::vector<bool> m_dirtyFlags;
    int m_sheetIndex = -1;

    std::vector<ModLogic::Sheet> m_undo;
    std::vector<ModLogic::Sheet> m_redo;

    QMenu* m_projMenu = nullptr;
    QMenu* m_recentMenu = nullptr;
    QMenu* m_editMenu = nullptr;
    QMenu* m_sheetMenu = nullptr;
    QMenu* m_buildMenu = nullptr;
    QMenu* m_pkgMenu = nullptr;
    QMenu* m_viewMenu = nullptr;

    QAction* m_actNew = nullptr;
    QAction* m_actOpen = nullptr;
    QAction* m_actSave = nullptr;
    QAction* m_actSaveAll = nullptr;
    QAction* m_actManifest = nullptr;
    QAction* m_actSettings = nullptr;
    QAction* m_actExit = nullptr;

    QAction* m_actUndo = nullptr;
    QAction* m_actRedo = nullptr;
    QAction* m_actCopy = nullptr;
    QAction* m_actPaste = nullptr;
    QAction* m_actToggleEn = nullptr;
    QAction* m_actToggleInv = nullptr;
    QAction* m_actDelete = nullptr;

    QAction* m_actAddEvent = nullptr;
    QAction* m_actAddGroup = nullptr;
    QAction* m_actAddSubEvent = nullptr;
    QAction* m_actNewSheet = nullptr;
    QAction* m_actExpandAll = nullptr;
    QAction* m_actCollapseAll = nullptr;

    QAction* m_actBuildOnly = nullptr;
    QAction* m_actBuildInstall = nullptr;
    QAction* m_actRunGame = nullptr;
    QAction* m_actShowCode = nullptr;

    QAction* m_actManagePkgs = nullptr;
    QAction* m_actReloadCatalog = nullptr;
    QAction* m_actSyntaxTheme = nullptr;
    QAction* m_actResetLayout = nullptr;

    QPushButton* m_btnOpen = nullptr;
    QPushButton* m_btnSave = nullptr;
    QPushButton* m_btnUndo = nullptr;
    QPushButton* m_btnRedo = nullptr;
    QPushButton* m_btnManifest = nullptr;
    QPushButton* m_btnAddEvent = nullptr;
    QPushButton* m_btnAddGroup = nullptr;
    QPushButton* m_btnToggleEn = nullptr;
    QPushButton* m_btnToggleInv = nullptr;
    QPushButton* m_btnBuildInstall = nullptr;
    QPushButton* m_btnRunGame = nullptr;
    QLabel* m_installedBadge = nullptr;

    QTabBar* m_sheetTabs = nullptr;
    QPushButton* m_btnAddSheet = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QComboBox* m_subFilterCombo = nullptr;
    QCheckBox* m_brokenOnlyCb = nullptr;
    QPushButton* m_btnExpand = nullptr;
    QPushButton* m_btnCollapse = nullptr;
    EventSheetView* m_sheetView = nullptr;

    QDockWidget* m_leftDock = nullptr;
    QTabWidget* m_leftTabs = nullptr;
    QTreeWidget* m_outlineTree = nullptr;
    QTreeWidget* m_projectTree = nullptr;

    QDockWidget* m_rightDock = nullptr;
    InspectorDock* m_inspector = nullptr;

    QDockWidget* m_bottomDock = nullptr;
    QTabWidget* m_bottomTabs = nullptr;
    QPlainTextEdit* m_buildLog = nullptr;
    QTreeWidget* m_problemsTree = nullptr;

    QLabel* m_sbSelection = nullptr;
    QLabel* m_sbCatalog = nullptr;
    QLabel* m_sbToolchain = nullptr;

    QPointer<BuildWorker> m_buildWorker;
    bool m_suppressTabSignal = false;
};
