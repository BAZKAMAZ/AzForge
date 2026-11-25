#include "CatalogPickerDialog.h"

#include "IconCache.h"
#include "Localization.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <map>

namespace {

constexpr int TileW = 114;
constexpr int TileH = 104;
constexpr int TileIcon = 44;

QString WrapTileLabel(const QString& text) {
    const QString trimmed = text.trimmed();
    const int sp = trimmed.indexOf(QLatin1Char(' '));
    if (sp > 0 && sp + 1 < trimmed.size()) {
        return trimmed.left(sp) + QLatin1Char('\n') + trimmed.mid(sp + 1);
    }
    return trimmed;
}

class SubsystemTileDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& /*option*/, const QModelIndex& /*index*/) const override {
        return QSize(TileW, TileH);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);

        const auto& c = Theme::Colors();
        const bool selected = (opt.state & QStyle::State_Selected) != 0;
        const bool hovered = (opt.state & QStyle::State_MouseOver) != 0;
        const int count = index.data(Qt::UserRole + 1).toInt();
        const std::string subId = index.data(Qt::UserRole).toString().toStdString();

        const QRect card = opt.rect.adjusted(3, 3, -3, -3);
        p->setBrush(selected ? QColor(0x33, 0x2d, 0x52) : (hovered ? c.cardHover : c.cardBg));
        p->setPen(QPen(selected ? c.accent : (hovered ? QColor(0x55, 0x60, 0x82) : c.border),
                       selected ? 1.5 : 1.0));
        p->drawRoundedRect(card, 7, 7);

        const QPixmap pm = IconCache::Instance().SubsystemPixmap(subId, TileIcon);
        if (!pm.isNull()) {
            const int ix = card.left() + (card.width() - pm.width()) / 2;
            const int iy = card.top() + 10;
            if (count == 0) p->setOpacity(0.42);
            p->drawPixmap(ix, iy, pm);
            p->setOpacity(1.0);
        }

        QFont f = opt.font;
        f.setPointSizeF(9.0);
        p->setFont(f);
        p->setPen(count == 0 ? c.textMuted : (selected ? Qt::white : c.text));
        const QRect textRect(card.left() + 4, card.top() + 58, card.width() - 8, card.height() - 62);
        const QString label = WrapTileLabel(index.data(Qt::DisplayRole).toString());
        p->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, label);

        p->restore();
    }
};

} // namespace

CatalogPickerDialog::CatalogPickerDialog(UiFormat::Slot slot, const ModLogic::Sheet* sheet, const std::vector<int>& contextPath, const std::string& initialId, QWidget* parent)
    : QDialog(parent), m_slot(slot), m_sheet(sheet), m_contextPath(contextPath) {
    const QString title = slot == UiFormat::Slot::Event
        ? UiFormat::Label("mod.pick.event")
        : (slot == UiFormat::Slot::Condition ? UiFormat::Label("mod.pick.condition") : UiFormat::Label("mod.pick.action"));

    setWindowTitle(title);
    resize(920, 590);
    setMinimumSize(660, 440);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    // Header bar
    auto* top = new QHBoxLayout();
    m_stepLabel = new QLabel(this);
    m_stepLabel->setStyleSheet(QStringLiteral("color: #8c93a8; font-weight: 600;"));
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(UiFormat::Label("mod.pick.search"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(260);
    top->addWidget(m_stepLabel, 1);
    top->addWidget(m_search);
    root->addLayout(top);

    // Body stack: 0 = tiles grid, 1 = entries list
    m_stack = new QStackedWidget(this);

    m_tiles = new QListWidget(m_stack);
    m_tiles->setViewMode(QListView::IconMode);
    m_tiles->setResizeMode(QListView::Adjust);
    m_tiles->setMovement(QListView::Static);
    m_tiles->setGridSize(QSize(TileW + 4, TileH + 4));
    m_tiles->setUniformItemSizes(true);
    m_tiles->setMouseTracking(true);
    m_tiles->setItemDelegate(new SubsystemTileDelegate(m_tiles));
    m_tiles->setStyleSheet(QStringLiteral(
        "QListWidget { background: #181b23; border: 1px solid #2b3040; border-radius: 6px; padding: 8px; outline: none; }"
        "QListWidget::item { background: transparent; border: none; }"
    ));

    m_entries = new QTreeWidget(m_stack);
    m_entries->setColumnCount(2);
    m_entries->setHeaderLabels({
        UiFormat::Label("app.picker.colOperation"),
        UiFormat::Label("app.picker.colHint")
    });
    m_entries->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_entries->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_entries->setRootIsDecorated(false);
    m_entries->setIndentation(14);
    m_entries->setAlternatingRowColors(true);
    m_entries->setStyleSheet(QStringLiteral(
        "QTreeWidget { background: #181b23; border: 1px solid #2b3040; border-radius: 6px; }"
    ));

    m_stack->addWidget(m_tiles);
    m_stack->addWidget(m_entries);
    root->addWidget(m_stack, 1);

    m_statusHint = new QLabel(this);
    m_statusHint->setStyleSheet(QStringLiteral("color: #8c93a8;"));
    root->addWidget(m_statusHint);

    auto* bottom = new QHBoxLayout();
    m_backBtn = new QPushButton(QStringLiteral("← ") + UiFormat::Label("common.back"), this);
    auto* cancelBtn = new QPushButton(UiFormat::Label("common.cancel"), this);
    m_nextBtn = new QPushButton(UiFormat::Label("mod.pick.next"), this);
    m_nextBtn->setProperty("primary", true);
    m_nextBtn->setDefault(true);

    bottom->addWidget(m_backBtn);
    bottom->addStretch(1);
    bottom->addWidget(cancelBtn);
    bottom->addWidget(m_nextBtn);
    root->addLayout(bottom);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!text.trimmed().isEmpty()) {
            m_stack->setCurrentIndex(1);
            m_stepLabel->setText(UiFormat::Label("mod.pick.search"));
            m_backBtn->setEnabled(true);
            m_nextBtn->setText(UiFormat::Label("common.choose"));
            populateEntries();
        } else {
            showStep(m_step);
        }
    });

    connect(m_tiles, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* cur) {
        if (!cur) return;
        m_selectedGroup = cur->data(Qt::UserRole).toString().toStdString();
    });
    connect(m_tiles, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        m_selectedGroup = item->data(Qt::UserRole).toString().toStdString();
        showStep(1);
    });

    connect(m_entries, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* cur) {
        if (!cur) { m_statusHint->clear(); return; }
        const QString warn = cur->data(0, Qt::UserRole + 2).toString();
        if (!warn.isEmpty()) {
            m_statusHint->setStyleSheet(QStringLiteral("color: #e5b85c;"));
            m_statusHint->setText(QStringLiteral("⚠ ") + warn);
        } else {
            m_statusHint->setStyleSheet(QStringLiteral("color: #8c93a8;"));
            m_statusHint->setText(cur->text(1));
        }
    });
    connect(m_entries, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (item && !item->data(0, Qt::UserRole).toString().isEmpty()) confirmEntry();
    });

    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        m_search->clear();
        showStep(0);
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        if (m_stack->currentIndex() == 0) {
            showStep(1);
        } else {
            confirmEntry();
        }
    });

    populateTiles();

    if (!initialId.empty()) {
        const std::string grp = UiFormat::GroupOf(slot, initialId);
        if (!grp.empty()) m_selectedGroup = grp;
        m_chosenId = initialId;
        showStep(1);
    } else {
        showStep(0);
    }
}

int CatalogPickerDialog::entryCountForSubsystem(const std::string& subId) const {
    int count = 0;
    if (m_slot == UiFormat::Slot::Event) {
        for (const auto& e : ModCatalog::Events()) if (e.group == subId) ++count;
        for (const auto& c : ModCatalog::Conditions()) if (c.group == subId) ++count;
    } else if (m_slot == UiFormat::Slot::Condition) {
        for (const auto& c : ModCatalog::Conditions()) if (c.group == subId) ++count;
    } else {
        for (const auto& a : ModCatalog::Actions()) if (a.group == subId) ++count;
    }
    return count;
}

void CatalogPickerDialog::populateTiles() {
    m_tiles->clear();
    int selectRow = 0;
    int row = 0;
    for (const ModCatalog::SubsystemDef& sub : ModCatalog::Subsystems()) {
        const int count = entryCountForSubsystem(sub.id);
        const QString name = UiFormat::Label(sub.label);
        auto* item = new QListWidgetItem(name, m_tiles);
        item->setData(Qt::UserRole, QString::fromStdString(sub.id));
        item->setData(Qt::UserRole + 1, count);
        item->setToolTip(QStringLiteral("%1 (%2)").arg(name).arg(count));
        if (sub.id == m_selectedGroup) selectRow = row;
        ++row;
    }
    if (m_tiles->count() > 0) {
        m_tiles->setCurrentRow(selectRow);
        m_selectedGroup = m_tiles->currentItem()->data(Qt::UserRole).toString().toStdString();
    }
}

void CatalogPickerDialog::showStep(int step) {
    m_step = step;
    m_stack->setCurrentIndex(step);
    m_backBtn->setEnabled(step == 1);
    if (step == 0) {
        m_stepLabel->setText(UiFormat::Label("mod.pick.where"));
        m_nextBtn->setText(UiFormat::Label("mod.pick.next"));
        m_statusHint->clear();
    } else {
        const ModCatalog::SubsystemDef* sub = ModCatalog::FindSubsystem(m_selectedGroup);
        const QString subName = sub ? UiFormat::Label(sub->label) : QString::fromStdString(m_selectedGroup);
        m_stepLabel->setText(UiFormat::Label("mod.pick.what") + QStringLiteral(" — ") + subName);
        m_nextBtn->setText(UiFormat::Label("common.choose"));
        populateEntries();
    }
}

void CatalogPickerDialog::populateEntries() {
    m_entries->clear();
    const QString query = m_search->text().trimmed().toLower();
    const bool searching = !query.isEmpty();

    struct Entry {
        std::string id;
        QString text;
        std::string group;
        std::string section;
        QString call;
        QString wrongPlace;
        bool isCondAsEvent = false;
    };

    std::map<std::string, std::vector<Entry>> grouped;

    auto matches = [&](const QString& text, const std::string& id, const std::string& call, const std::string& group) {
        if (!searching) return group == m_selectedGroup;
        return text.toLower().contains(query) ||
               QString::fromStdString(id).toLower().contains(query) ||
               QString::fromStdString(call).toLower().contains(query);
    };

    if (m_slot == UiFormat::Slot::Event) {
        for (const auto& e : ModCatalog::Events()) {
            const QString text = UiFormat::Label(e.label);
            if (!matches(text, e.id, e.call, e.group)) continue;
            grouped[e.section].push_back({ e.id, text, e.group, e.section, QString::fromStdString(e.call), {}, false });
        }
        for (const auto& c : ModCatalog::Conditions()) {
            const QString text = UiFormat::Label(c.label);
            if (!matches(text, c.id, {}, c.group)) continue;
            grouped["mod.sec.checks"].push_back({ c.id, text, c.group, "mod.sec.checks", {}, {}, true });
        }
    } else if (m_slot == UiFormat::Slot::Condition) {
        for (const auto& c : ModCatalog::Conditions()) {
            const QString text = UiFormat::Label(c.label);
            if (!matches(text, c.id, {}, c.group)) continue;
            grouped[c.section].push_back({ c.id, text, c.group, c.section, {}, {}, false });
        }
    } else {
        for (const auto& a : ModCatalog::Actions()) {
            const QString text = UiFormat::Label(a.label);
            if (!matches(text, a.id, a.call, a.group)) continue;
            QString wrong;
            if (m_sheet && !m_contextPath.empty()) {
                const std::string missKey = UiFormat::MissingContext(*m_sheet, m_contextPath, a);
                if (!missKey.empty()) wrong = UiFormat::Label(missKey);
            }
            grouped[a.section].push_back({ a.id, text, a.group, a.section, QString::fromStdString(a.call), wrong, false });
        }
    }

    QTreeWidgetItem* toSelect = nullptr;
    for (const auto& [secKey, list] : grouped) {
        auto* header = new QTreeWidgetItem(m_entries);
        header->setText(0, secKey.empty() ? UiFormat::Label("mod.sec.general") : UiFormat::Label(secKey));
        header->setForeground(0, Theme::Colors().accent);
        QFont f = header->font(0);
        f.setBold(true);
        header->setFont(0, f);
        header->setFlags(Qt::ItemIsEnabled);
        header->setFirstColumnSpanned(true);

        for (const Entry& e : list) {
            auto* item = new QTreeWidgetItem(header);
            item->setIcon(0, IconCache::Instance().SubsystemIcon(e.group, 16));
            item->setText(0, e.text);
            item->setText(1, e.wrongPlace.isEmpty() ? e.call : QStringLiteral("⚠ ") + e.wrongPlace);
            item->setFont(1, Theme::MonoFont(9));
            if (!e.wrongPlace.isEmpty()) {
                item->setForeground(0, Theme::Colors().textMuted);
                item->setForeground(1, Theme::Colors().warning);
                item->setToolTip(0, e.wrongPlace);
                item->setToolTip(1, e.wrongPlace);
            } else {
                item->setForeground(1, Theme::Colors().textMuted);
            }
            item->setData(0, Qt::UserRole, QString::fromStdString(e.id));
            item->setData(0, Qt::UserRole + 1, e.isCondAsEvent);
            item->setData(0, Qt::UserRole + 2, e.wrongPlace);
            if (!m_chosenId.empty() && e.id == m_chosenId) toSelect = item;
            if (!toSelect) toSelect = item;
        }
        header->setExpanded(true);
    }

    if (toSelect) m_entries->setCurrentItem(toSelect);
}

void CatalogPickerDialog::confirmEntry() {
    QTreeWidgetItem* item = m_entries->currentItem();
    if (!item) return;
    const std::string id = item->data(0, Qt::UserRole).toString().toStdString();
    if (id.empty()) return;
    m_chosenId = id;
    m_pickedConditionAsEvent = item->data(0, Qt::UserRole + 1).toBool();
    accept();
}
