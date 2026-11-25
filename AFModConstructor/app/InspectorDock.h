#pragma once

#include "ModLogic.h"
#include "UiFormat.h"

#include <QCheckBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

class InspectorDock : public QWidget {
    Q_OBJECT
public:
    explicit InspectorDock(QWidget* parent = nullptr);

    void setContext(ModLogic::Sheet* sheet, const UiFormat::Selection& sel);
    void focusFirstParam();
    void showTab(int index); // 0 = Params, 1 = Vars, 2 = C-Code
    void retranslateUi();

signals:
    void aboutToEdit();
    void sheetEdited();
    void requestReplace(const std::vector<int>& path, UiFormat::Slot slot, int index);
    void requestDeleteSelected();
    void requestShowFullCode();

private:
    void clearParamLayout();
    void rebuildParamsForm();
    void rebuildVarsTable();
    void rebuildCodePreview();
    void commitVarsFromTable();

    ModLogic::Sheet* m_sheet = nullptr;
    UiFormat::Selection m_sel;
    bool m_rebuilding = false;

    QTabWidget* m_tabs = nullptr;

    QScrollArea* m_paramScroll = nullptr;
    QWidget* m_paramHost = nullptr;
    QVBoxLayout* m_paramLayout = nullptr;
    QWidget* m_firstInput = nullptr;

    QPushButton* m_addVarBtn = nullptr;
    QPushButton* m_delVarBtn = nullptr;
    QTableWidget* m_varsTable = nullptr;
    QLabel* m_varsWarn = nullptr;

    QLabel* m_codeLabel = nullptr;
    QPushButton* m_fullCodeBtn = nullptr;
    QPlainTextEdit* m_codePreview = nullptr;
};
