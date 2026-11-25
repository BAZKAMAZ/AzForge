#pragma once

#include "ModCatalog.h"

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTreeWidget>

#include <string>

class ExprPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExprPickerDialog(QWidget* parent = nullptr);

    const ModCatalog::ExpressionDef* chosenExpression() const { return m_chosen; }

private:
    void populateGroups();
    void populateList();
    void updateHint();
    void acceptCurrent();

    QLineEdit* m_search = nullptr;
    QListWidget* m_groups = nullptr;
    QTreeWidget* m_list = nullptr;
    QLabel* m_hint = nullptr;

    std::string m_currentGroup = "system";
    const ModCatalog::ExpressionDef* m_chosen = nullptr;
};
