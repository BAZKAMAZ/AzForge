#include "ExprPickerDialog.h"

#include "IconCache.h"
#include "Localization.h"
#include "Theme.h"
#include "UiFormat.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <map>

ExprPickerDialog::ExprPickerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(UiFormat::Label("mod.expr.title"));
    resize(740, 500);
    setMinimumSize(540, 380);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* top = new QHBoxLayout();
    auto* prompt = new QLabel(UiFormat::Label("mod.expr.pick"), this);
    prompt->setStyleSheet(QStringLiteral("color: #8c93a8;"));
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(UiFormat::Label("mod.pick.search"));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(260);
    top->addWidget(prompt, 1);
    top->addWidget(m_search);
    root->addLayout(top);

    auto* split = new QSplitter(Qt::Horizontal, this);

    m_groups = new QListWidget(split);
    m_groups->setIconSize(QSize(18, 18));
    m_groups->setMinimumWidth(180);
    m_groups->setMaximumWidth(240);

    m_list = new QTreeWidget(split);
    m_list->setColumnCount(2);
    m_list->setHeaderLabels({
        UiFormat::Label("app.expr.colExpr"),
        UiFormat::Label("app.expr.colCode")
    });
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->setRootIsDecorated(false);
    m_list->setIndentation(14);
    m_list->setAlternatingRowColors(true);

    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    root->addWidget(split, 1);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color: #8c93a8; padding: 4px 2px;"));
    root->addWidget(m_hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Ok)->setText(UiFormat::Label("common.ok"));
    buttons->button(QDialogButtonBox::Cancel)->setText(UiFormat::Label("common.cancel"));
    root->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &ExprPickerDialog::populateList);
    connect(m_groups, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* cur) {
        if (!cur) return;
        m_currentGroup = cur->data(Qt::UserRole).toString().toStdString();
        populateList();
    });
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this]() { updateHint(); });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (item && !item->data(0, Qt::UserRole).toString().isEmpty()) acceptCurrent();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &ExprPickerDialog::acceptCurrent);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populateGroups();
    populateList();
}

void ExprPickerDialog::populateGroups() {
    m_groups->clear();
    int selectRow = 0;
    int row = 0;
    for (const ModCatalog::SubsystemDef& sub : ModCatalog::Subsystems()) {
        int count = 0;
        for (const ModCatalog::ExpressionDef& e : ModCatalog::Expressions()) {
            if (e.group == sub.id) ++count;
        }
        if (count == 0) continue;

        auto* item = new QListWidgetItem(
            IconCache::Instance().SubsystemIcon(sub.id, 18),
            QStringLiteral("%1 (%2)").arg(UiFormat::Label(sub.label)).arg(count),
            m_groups);
        item->setData(Qt::UserRole, QString::fromStdString(sub.id));
        if (sub.id == m_currentGroup) selectRow = row;
        ++row;
    }
    if (m_groups->count() > 0) {
        m_groups->setCurrentRow(selectRow);
        m_currentGroup = m_groups->currentItem()->data(Qt::UserRole).toString().toStdString();
    }
}

void ExprPickerDialog::populateList() {
    m_list->clear();
    const QString query = m_search->text().trimmed().toLower();
    const bool searching = !query.isEmpty();

    std::map<std::string, std::vector<const ModCatalog::ExpressionDef*>> bySection;
    for (const ModCatalog::ExpressionDef& e : ModCatalog::Expressions()) {
        const QString title = UiFormat::Label(e.label);
        const QString code = QString::fromStdString(e.text);
        if (searching) {
            if (!title.toLower().contains(query) && !code.toLower().contains(query) &&
                !QString::fromStdString(e.id).toLower().contains(query)) {
                continue;
            }
        } else if (e.group != m_currentGroup) {
            continue;
        }
        bySection[e.section].push_back(&e);
    }

    for (const auto& [secKey, list] : bySection) {
        auto* header = new QTreeWidgetItem(m_list);
        header->setText(0, secKey.empty() ? UiFormat::Label("mod.sec.general") : UiFormat::Label(secKey));
        header->setForeground(0, Theme::Colors().accent);
        QFont f = header->font(0);
        f.setBold(true);
        header->setFont(0, f);
        header->setFlags(Qt::ItemIsEnabled);
        header->setFirstColumnSpanned(true);

        for (const ModCatalog::ExpressionDef* e : list) {
            auto* child = new QTreeWidgetItem(header);
            child->setIcon(0, IconCache::Instance().SubsystemIcon(e->group, 16));
            child->setText(0, UiFormat::Label(e->label));
            child->setText(1, QString::fromStdString(e->text));
            child->setFont(1, Theme::MonoFont(9));
            child->setForeground(1, Theme::Colors().exprText);
            child->setData(0, Qt::UserRole, QString::fromStdString(e->id));
        }
        header->setExpanded(true);
    }
    updateHint();
}

void ExprPickerDialog::updateHint() {
    QTreeWidgetItem* item = m_list->currentItem();
    if (!item) {
        m_hint->clear();
        return;
    }
    const std::string id = item->data(0, Qt::UserRole).toString().toStdString();
    if (const ModCatalog::ExpressionDef* def = ModCatalog::FindExpression(id)) {
        m_hint->setText(def->hint.empty()
            ? QString::fromStdString(def->text)
            : UiFormat::Label(def->hint));
    } else {
        m_hint->clear();
    }
}

void ExprPickerDialog::acceptCurrent() {
    QTreeWidgetItem* item = m_list->currentItem();
    if (!item) return;
    const std::string id = item->data(0, Qt::UserRole).toString().toStdString();
    m_chosen = ModCatalog::FindExpression(id);
    if (m_chosen) accept();
}
