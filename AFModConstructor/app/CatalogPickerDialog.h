#pragma once

#include "ModCatalog.h"
#include "ModLogic.h"
#include "UiFormat.h"

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>

#include <string>
#include <vector>

class CatalogPickerDialog : public QDialog {
    Q_OBJECT
public:
    CatalogPickerDialog(UiFormat::Slot slot, const ModLogic::Sheet* sheet, const std::vector<int>& contextPath, const std::string& initialId = {}, QWidget* parent = nullptr);

    std::string chosenId() const { return m_chosenId; }
    // True when Slot::Event picked a Condition (per-frame rule).
    bool pickedConditionAsEvent() const { return m_pickedConditionAsEvent; }

private:
    void populateTiles();
    void populateEntries();
    void showStep(int step);
    void confirmEntry();
    int entryCountForSubsystem(const std::string& subId) const;

    UiFormat::Slot m_slot;
    const ModLogic::Sheet* m_sheet = nullptr;
    std::vector<int> m_contextPath;

    QLabel* m_stepLabel = nullptr;
    QLineEdit* m_search = nullptr;
    QStackedWidget* m_stack = nullptr;
    QListWidget* m_tiles = nullptr;
    QTreeWidget* m_entries = nullptr;
    QLabel* m_statusHint = nullptr;
    QPushButton* m_backBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;

    std::string m_selectedGroup = "system";
    std::string m_chosenId;
    bool m_pickedConditionAsEvent = false;
    int m_step = 0; // 0 = subsystem tiles, 1 = entries list
};
