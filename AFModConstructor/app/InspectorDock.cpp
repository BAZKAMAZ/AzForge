#include "InspectorDock.h"

#include "CppHighlighter.h"
#include "ExprPickerDialog.h"
#include "IconCache.h"
#include "Localization.h"
#include "ModCatalog.h"
#include "ModCodegen.h"
#include "Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>

#include <algorithm>

namespace {

void ClearLayoutImmediate(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            // Hide and detach immediately so widgets never overlap during nested modal exec() loops
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        } else if (QLayout* sub = item->layout()) {
            ClearLayoutImmediate(sub);
        }
        delete item;
    }
}

} // namespace

InspectorDock::InspectorDock(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs);

    auto* paramTab = new QWidget(this);
    auto* paramTabLay = new QVBoxLayout(paramTab);
    paramTabLay->setContentsMargins(0, 0, 0, 0);

    m_paramScroll = new QScrollArea(paramTab);
    m_paramScroll->setWidgetResizable(true);
    m_paramScroll->setFrameShape(QFrame::NoFrame);
    m_paramHost = new QWidget(m_paramScroll);
    m_paramLayout = new QVBoxLayout(m_paramHost);
    m_paramLayout->setContentsMargins(10, 10, 10, 10);
    m_paramLayout->setSpacing(8);
    m_paramLayout->addStretch(1);
    m_paramScroll->setWidget(m_paramHost);
    paramTabLay->addWidget(m_paramScroll);
    m_tabs->addTab(paramTab, QString());

    auto* varsTab = new QWidget(this);
    auto* varsLay = new QVBoxLayout(varsTab);
    varsLay->setContentsMargins(8, 8, 8, 8);
    varsLay->setSpacing(6);

    auto* varsTop = new QHBoxLayout();
    m_addVarBtn = new QPushButton(varsTab);
    m_delVarBtn = new QPushButton(varsTab);
    varsTop->addWidget(m_addVarBtn);
    varsTop->addWidget(m_delVarBtn);
    varsTop->addStretch(1);
    varsLay->addLayout(varsTop);

    m_varsTable = new QTableWidget(0, 3, varsTab);
    m_varsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_varsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_varsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_varsTable->verticalHeader()->setVisible(false);
    m_varsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_varsTable->setAlternatingRowColors(true);
    varsLay->addWidget(m_varsTable, 1);

    m_varsWarn = new QLabel(varsTab);
    m_varsWarn->setWordWrap(true);
    m_varsWarn->setStyleSheet(QStringLiteral("color: #e56767;"));
    m_varsWarn->hide();
    varsLay->addWidget(m_varsWarn);

    connect(m_addVarBtn, &QPushButton::clicked, this, [this]() {
        if (!m_sheet) return;
        emit aboutToEdit();
        m_sheet->vars.push_back({ "var" + std::to_string(m_sheet->vars.size() + 1), "int", "0" });
        rebuildVarsTable();
        rebuildCodePreview();
        emit sheetEdited();
    });
    connect(m_delVarBtn, &QPushButton::clicked, this, [this]() {
        if (!m_sheet) return;
        const int row = m_varsTable->currentRow();
        if (row < 0 || row >= static_cast<int>(m_sheet->vars.size())) return;
        emit aboutToEdit();
        m_sheet->vars.erase(m_sheet->vars.begin() + row);
        rebuildVarsTable();
        rebuildCodePreview();
        emit sheetEdited();
    });
    connect(m_varsTable, &QTableWidget::cellChanged, this, [this]() {
        if (!m_rebuilding) commitVarsFromTable();
    });

    m_tabs->addTab(varsTab, QString());

    auto* codeTab = new QWidget(this);
    auto* codeLay = new QVBoxLayout(codeTab);
    codeLay->setContentsMargins(8, 8, 8, 8);
    codeLay->setSpacing(6);

    auto* codeBar = new QHBoxLayout();
    m_codeLabel = new QLabel(codeTab);
    m_codeLabel->setStyleSheet(QStringLiteral("color: #8c93a8;"));
    m_fullCodeBtn = new QPushButton(codeTab);
    codeBar->addWidget(m_codeLabel, 1);
    codeBar->addWidget(m_fullCodeBtn);
    codeLay->addLayout(codeBar);

    m_codePreview = new QPlainTextEdit(codeTab);
    m_codePreview->setReadOnly(true);
    m_codePreview->setFont(Theme::MonoFont(9));
    m_codePreview->setLineWrapMode(QPlainTextEdit::NoWrap);
    new CppHighlighter(m_codePreview);
    codeLay->addWidget(m_codePreview, 1);

    connect(m_fullCodeBtn, &QPushButton::clicked, this, &InspectorDock::requestShowFullCode);
    m_tabs->addTab(codeTab, QString());

    retranslateUi();
}

void InspectorDock::retranslateUi() {
    m_tabs->setTabText(0, UiFormat::Label("mod.params"));
    m_tabs->setTabText(1, UiFormat::Label("mod.pane.vars"));
    m_tabs->setTabText(2, UiFormat::Label("app.inspector.codeTab"));

    m_addVarBtn->setText(QStringLiteral("+ ") + UiFormat::Label("common.add"));
    m_delVarBtn->setText(UiFormat::Label("common.delete"));
    m_varsTable->setHorizontalHeaderLabels({
        UiFormat::Label("app.vars.name"),
        UiFormat::Label("app.vars.type"),
        UiFormat::Label("app.vars.value")
    });

    m_codeLabel->setText(UiFormat::Label("app.inspector.codeCaption"));
    m_fullCodeBtn->setText(UiFormat::Label("app.inspector.fullFile"));

    rebuildParamsForm();
    rebuildVarsTable();
    rebuildCodePreview();
}

void InspectorDock::showTab(int index) {
    if (index >= 0 && index < m_tabs->count()) m_tabs->setCurrentIndex(index);
}

void InspectorDock::focusFirstParam() {
    m_tabs->setCurrentIndex(0);
    if (m_firstInput) {
        m_firstInput->setFocus();
        if (auto* le = qobject_cast<QLineEdit*>(m_firstInput)) le->selectAll();
    }
}

void InspectorDock::setContext(ModLogic::Sheet* sheet, const UiFormat::Selection& sel) {
    m_sheet = sheet;
    m_sel = sel;
    rebuildParamsForm();
    rebuildVarsTable();
    rebuildCodePreview();
}

void InspectorDock::clearParamLayout() {
    m_firstInput = nullptr;
    ClearLayoutImmediate(m_paramLayout);
    // Catch any stray direct child widgets on m_paramHost
    for (QObject* child : m_paramHost->children()) {
        if (auto* w = qobject_cast<QWidget*>(child)) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
    }
}

void InspectorDock::rebuildParamsForm() {
    m_rebuilding = true;
    clearParamLayout();

    auto finishEmpty = [&](const QString& text) {
        auto* lbl = new QLabel(text, m_paramHost);
        lbl->setWordWrap(true);
        lbl->setStyleSheet(QStringLiteral("color: #8c93a8; padding: 8px 2px;"));
        m_paramLayout->addWidget(lbl);
        m_paramLayout->addStretch(1);
        m_paramHost->updateGeometry();
        m_rebuilding = false;
    };

    if (!m_sheet || !m_sel.valid()) {
        finishEmpty(UiFormat::Label("app.inspector.empty"));
        return;
    }

    ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, m_sel.path);
    if (!rule) {
        finishEmpty(UiFormat::Label("app.inspector.notFound"));
        return;
    }

    if (rule->group) {
        auto* head = new QLabel(
            QStringLiteral("📁 %1 #%2").arg(UiFormat::Label("mod.group"), UiFormat::RuleNumber(m_sel.path)),
            m_paramHost);
        head->setStyleSheet(QStringLiteral("font-weight: 600; color: #9b87f5;"));
        m_paramLayout->addWidget(head);

        auto* nameLbl = new QLabel(UiFormat::Label("app.group.name"), m_paramHost);
        nameLbl->setStyleSheet(QStringLiteral("color: #8c93a8;"));
        auto* nameEdit = new QLineEdit(QString::fromStdString(rule->name), m_paramHost);
        m_firstInput = nameEdit;
        connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit]() {
            if (m_rebuilding || !m_sheet) return;
            ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, m_sel.path);
            if (!r) return;
            const std::string val = nameEdit->text().toStdString();
            if (r->name == val) return;
            emit aboutToEdit();
            r->name = val;
            rebuildCodePreview();
            emit sheetEdited();
        });
        m_paramLayout->addWidget(nameLbl);
        m_paramLayout->addWidget(nameEdit);

        auto* enabledCb = new QCheckBox(UiFormat::Label("mod.rule.enabled"), m_paramHost);
        enabledCb->setChecked(rule->enabled);
        connect(enabledCb, &QCheckBox::toggled, this, [this](bool on) {
            if (m_rebuilding || !m_sheet) return;
            ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, m_sel.path);
            if (!r || r->enabled == on) return;
            emit aboutToEdit();
            r->enabled = on;
            rebuildCodePreview();
            emit sheetEdited();
        });
        m_paramLayout->addWidget(enabledCb);
        m_paramLayout->addStretch(1);
        m_paramHost->updateGeometry();
        m_rebuilding = false;
        return;
    }

    std::string entryId;
    std::string groupId;
    QString title;
    QString sdkCall;
    QString warnText;
    const std::vector<ModCatalog::ParamDef>* defs = nullptr;
    std::vector<ModLogic::Param>* args = nullptr;
    bool canVeto = false;

    bool insideNonGroupRule = false;
    for (size_t d = 1; d < m_sel.path.size(); ++d) {
        std::vector<int> head(m_sel.path.begin(), m_sel.path.begin() + static_cast<ptrdiff_t>(d));
        if (const ModLogic::Rule* anc = UiFormat::RuleAt(*m_sheet, head)) {
            if (!anc->group) { insideNonGroupRule = true; break; }
        }
    }

    if (m_sel.slot == UiFormat::Slot::Event) {
        entryId = rule->event;
        if (entryId.empty()) {
            title = QStringLiteral("%1 #%2 (%3)")
                .arg(UiFormat::Label("app.rule.label"),
                     UiFormat::RuleNumber(m_sel.path),
                     UiFormat::Label("app.rule.perFrame"));
        } else if (const ModCatalog::EventDef* def = ModCatalog::FindEvent(entryId)) {
            groupId = def->group;
            title = UiFormat::Label(def->label);
            sdkCall = QString::fromStdString(def->call);
            defs = &def->filters;
            args = &rule->filters;
            canVeto = def->canVeto;
        } else {
            title = QString::fromStdString(entryId) + QStringLiteral(" ") +
                    UiFormat::Label("mod.unknown");
        }
        if (insideNonGroupRule && !entryId.empty()) {
            warnText = UiFormat::Label("app.problems.nestedEvent")
                .arg(QString::fromStdString(entryId));
        }
    } else if (m_sel.slot == UiFormat::Slot::Condition) {
        if (m_sel.index < 0 || m_sel.index >= static_cast<int>(rule->conditions.size())) {
            finishEmpty(UiFormat::Label("app.inspector.empty"));
            return;
        }
        ModLogic::Condition& c = rule->conditions[m_sel.index];
        entryId = c.id;
        args = &c.args;
        if (const ModCatalog::ConditionDef* def = ModCatalog::FindCondition(c.id)) {
            groupId = def->group;
            title = UiFormat::Label(def->label);
            sdkCall = QString::fromStdString(def->emitCode.empty() ? def->loop : def->emitCode);
            defs = &def->params;
        } else {
            title = QString::fromStdString(c.id) + QStringLiteral(" ") +
                    UiFormat::Label("mod.unknown");
        }
    } else {
        if (m_sel.index < 0 || m_sel.index >= static_cast<int>(rule->actions.size())) {
            finishEmpty(UiFormat::Label("app.inspector.empty"));
            return;
        }
        ModLogic::Action& a = rule->actions[m_sel.index];
        entryId = a.id;
        args = &a.args;
        if (const ModCatalog::ActionDef* def = ModCatalog::FindAction(a.id)) {
            groupId = def->group;
            title = UiFormat::Label(def->label);
            sdkCall = QString::fromStdString(def->call.empty() ? def->emitCode : def->call);
            defs = &def->params;
            const std::string miss = UiFormat::MissingContext(*m_sheet, m_sel.path, *def);
            if (!miss.empty()) {
                warnText = UiFormat::Label(miss);
            }
        } else {
            title = QString::fromStdString(a.id) + QStringLiteral(" ") +
                    UiFormat::Label("mod.unknown");
        }
    }

    auto* headRow = new QWidget(m_paramHost);
    auto* headLay = new QHBoxLayout(headRow);
    headLay->setContentsMargins(0, 0, 0, 6);
    headLay->setSpacing(8);
    if (!groupId.empty()) {
        auto* iconLbl = new QLabel(headRow);
        iconLbl->setPixmap(IconCache::Instance().SubsystemPixmap(groupId, 24));
        headLay->addWidget(iconLbl, 0, Qt::AlignTop);
    }
    auto* titleBox = new QVBoxLayout();
    titleBox->setSpacing(2);
    auto* titleLbl = new QLabel(title, headRow);
    titleLbl->setWordWrap(true);
    titleLbl->setStyleSheet(QStringLiteral("font-weight: 600; color: #ffffff;"));
    titleBox->addWidget(titleLbl);
    if (!entryId.empty()) {
        auto* idLbl = new QLabel(QString::fromStdString(entryId), headRow);
        idLbl->setFont(Theme::MonoFont(8));
        idLbl->setStyleSheet(QStringLiteral("color: #9b87f5;"));
        titleBox->addWidget(idLbl);
    }
    if (!sdkCall.isEmpty()) {
        auto* callLbl = new QLabel(sdkCall, headRow);
        callLbl->setWordWrap(true);
        callLbl->setFont(Theme::MonoFont(8));
        callLbl->setStyleSheet(QStringLiteral("color: #7e869c;"));
        titleBox->addWidget(callLbl);
    }
    headLay->addLayout(titleBox, 1);
    m_paramLayout->addWidget(headRow);

    if (!warnText.isEmpty()) {
        auto* warnLbl = new QLabel(QStringLiteral("⚠ ") + warnText, m_paramHost);
        warnLbl->setWordWrap(true);
        warnLbl->setStyleSheet(QStringLiteral(
            "background: #2c2418; color: #f0c36d; border: 1px solid #5c4824; border-radius: 5px; padding: 6px 8px;"
        ));
        m_paramLayout->addWidget(warnLbl);
    }

    auto* flagsRow = new QWidget(m_paramHost);
    auto* flagsLay = new QHBoxLayout(flagsRow);
    flagsLay->setContentsMargins(0, 0, 0, 4);
    flagsLay->setSpacing(10);

    auto* enabledCb = new QCheckBox(UiFormat::Label("mod.rule.enabled"), flagsRow);
    if (m_sel.slot == UiFormat::Slot::Event) enabledCb->setChecked(rule->enabled);
    else if (m_sel.slot == UiFormat::Slot::Condition) enabledCb->setChecked(rule->conditions[m_sel.index].enabled);
    else enabledCb->setChecked(rule->actions[m_sel.index].enabled);
    connect(enabledCb, &QCheckBox::toggled, this, [this](bool on) {
        if (m_rebuilding || !m_sheet) return;
        ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, m_sel.path);
        if (!r) return;
        emit aboutToEdit();
        if (m_sel.slot == UiFormat::Slot::Event) r->enabled = on;
        else if (m_sel.slot == UiFormat::Slot::Condition && m_sel.index < static_cast<int>(r->conditions.size()))
            r->conditions[m_sel.index].enabled = on;
        else if (m_sel.slot == UiFormat::Slot::Action && m_sel.index < static_cast<int>(r->actions.size()))
            r->actions[m_sel.index].enabled = on;
        rebuildCodePreview();
        emit sheetEdited();
    });
    flagsLay->addWidget(enabledCb);

    if (m_sel.slot == UiFormat::Slot::Condition) {
        auto* invCb = new QCheckBox(UiFormat::Label("mod.invert"), flagsRow);
        invCb->setChecked(rule->conditions[m_sel.index].inverted);
        connect(invCb, &QCheckBox::toggled, this, [this](bool on) {
            if (m_rebuilding || !m_sheet) return;
            ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, m_sel.path);
            if (!r || m_sel.index >= static_cast<int>(r->conditions.size())) return;
            emit aboutToEdit();
            r->conditions[m_sel.index].inverted = on;
            rebuildCodePreview();
            emit sheetEdited();
        });
        flagsLay->addWidget(invCb);
    }

    if (m_sel.slot == UiFormat::Slot::Event && canVeto) {
        auto* vetoCb = new QCheckBox(UiFormat::Label("mod.rule.veto"), flagsRow);
        vetoCb->setChecked(rule->veto);
        connect(vetoCb, &QCheckBox::toggled, this, [this](bool on) {
            if (m_rebuilding || !m_sheet) return;
            ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, m_sel.path);
            if (!r) return;
            emit aboutToEdit();
            r->veto = on;
            rebuildCodePreview();
            emit sheetEdited();
        });
        flagsLay->addWidget(vetoCb);
    }

    flagsLay->addStretch(1);
    m_paramLayout->addWidget(flagsRow);

    if (defs && !defs->empty() && args) {
        for (const ModCatalog::ParamDef& def : *defs) {
            auto* fieldBox = new QWidget(m_paramHost);
            auto* fieldLay = new QVBoxLayout(fieldBox);
            fieldLay->setContentsMargins(0, 2, 0, 2);
            fieldLay->setSpacing(3);

            auto* lbl = new QLabel(
                QStringLiteral("%1  <span style='color:#6b738f; font-family:Consolas;'>%2</span>")
                    .arg(UiFormat::Label(def.label).toHtmlEscaped(),
                         QString::fromStdString(def.key).toHtmlEscaped()),
                fieldBox);
            lbl->setTextFormat(Qt::RichText);
            fieldLay->addWidget(lbl);

            const std::string curVal = ModLogic::ParamValueOr(*args, def.key, def.preset);
            const std::string paramKey = def.key;
            const std::string paramKind = def.kind;

            auto commitValue = [this, paramKey](const std::string& newVal) {
                if (m_rebuilding || !m_sheet) return;
                ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, m_sel.path);
                if (!r) return;
                std::vector<ModLogic::Param>* targetArgs = nullptr;
                if (m_sel.slot == UiFormat::Slot::Event) targetArgs = &r->filters;
                else if (m_sel.slot == UiFormat::Slot::Condition && m_sel.index < static_cast<int>(r->conditions.size()))
                    targetArgs = &r->conditions[m_sel.index].args;
                else if (m_sel.slot == UiFormat::Slot::Action && m_sel.index < static_cast<int>(r->actions.size()))
                    targetArgs = &r->actions[m_sel.index].args;
                if (!targetArgs) return;

                std::string& slot = ModLogic::ParamValue(*targetArgs, paramKey);
                if (slot == newVal) return;
                emit aboutToEdit();
                slot = newVal;
                rebuildCodePreview();
                emit sheetEdited();
            };

            if (def.kind == "fragment") {
                auto* edit = new QPlainTextEdit(QString::fromStdString(curVal), fieldBox);
                edit->setFont(Theme::MonoFont(9));
                edit->setMinimumHeight(110);
                edit->setMaximumHeight(180);
                if (!m_firstInput) m_firstInput = edit;
                connect(edit, &QPlainTextEdit::textChanged, this, [edit, commitValue]() {
                    commitValue(edit->toPlainText().toStdString());
                });
                fieldLay->addWidget(edit);

                if (!def.options.empty()) {
                    auto* addBlk = new QPushButton(QStringLiteral("+ ") + UiFormat::Label("mod.addBlock"), fieldBox);
                    auto* menu = new QMenu(addBlk);
                    for (const ModCatalog::Option& opt : def.options) {
                        const QString optText = UiFormat::Label(opt.label);
                        const std::string optVal = opt.value;
                        menu->addAction(optText, this, [edit, optVal]() {
                            QString t = edit->toPlainText();
                            if (!t.isEmpty() && !t.endsWith('\n')) t += '\n';
                            t += QString::fromStdString(optVal);
                            edit->setPlainText(t);
                        });
                    }
                    addBlk->setMenu(menu);
                    fieldLay->addWidget(addBlk, 0, Qt::AlignLeft);
                }
            } else if (!def.options.empty()) {
                auto* row = new QHBoxLayout();
                row->setSpacing(4);
                auto* combo = new QComboBox(fieldBox);
                combo->setEditable(true);
                combo->setInsertPolicy(QComboBox::NoInsert);
                combo->setFont(Theme::MonoFont(9));
                for (const ModCatalog::Option& opt : def.options) {
                    const QString lblOpt = UiFormat::Label(opt.label);
                    const QString valQ = QString::fromStdString(opt.value);
                    const QString display = (lblOpt == valQ) ? valQ : QStringLiteral("%1 (%2)").arg(lblOpt, valQ);
                    combo->addItem(display, valQ);
                }
                const int foundIdx = combo->findData(QString::fromStdString(curVal));
                if (foundIdx >= 0) combo->setCurrentIndex(foundIdx);
                combo->setEditText(QString::fromStdString(curVal));
                if (!m_firstInput) m_firstInput = combo->lineEdit();

                connect(combo, qOverload<int>(&QComboBox::activated), this, [combo, commitValue](int idx) {
                    const QString data = combo->itemData(idx).toString();
                    combo->setEditText(data);
                    commitValue(data.toStdString());
                });
                connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [combo, commitValue]() {
                    commitValue(combo->currentText().toStdString());
                });

                auto* fxBtn = new QPushButton(QStringLiteral("fx"), fieldBox);
                fxBtn->setFixedWidth(34);
                fxBtn->setFont(Theme::MonoFont(9));
                fxBtn->setToolTip(UiFormat::Label("mod.expr.title"));
                connect(fxBtn, &QPushButton::clicked, this, [this, combo, paramKind, commitValue]() {
                    ExprPickerDialog dlg(this);
                    if (dlg.exec() != QDialog::Accepted || !dlg.chosenExpression()) return;
                    const auto* e = dlg.chosenExpression();
                    std::string val = combo->currentText().toStdString();
                    const bool started = !val.empty() && val[0] == '=' && (val.size() < 2 || val[1] != '=');
                    const bool asCode = (paramKind == "number") || started;
                    const char mark = e->kind == "int" ? '#' : (e->kind == "text" ? '$' : '~');
                    const std::string piece = asCode ? e->text : (std::string("{") + mark + e->text + "}");
                    int caret = combo->lineEdit() ? combo->lineEdit()->cursorPosition() : static_cast<int>(val.size());
                    size_t at = std::min(static_cast<size_t>(std::max(0, caret)), val.size());
                    if (asCode && val.empty()) val = "=" + piece;
                    else if (asCode) {
                        if (val[0] != '=') { val.insert(val.begin(), '='); ++at; }
                        val.insert(std::max<size_t>(at, 1), piece);
                    } else {
                        val.insert(at, piece);
                    }
                    combo->setEditText(QString::fromStdString(val));
                    commitValue(val);
                });

                row->addWidget(combo, 1);
                row->addWidget(fxBtn);
                fieldLay->addLayout(row);
            } else {
                auto* row = new QHBoxLayout();
                row->setSpacing(4);
                auto* edit = new QLineEdit(QString::fromStdString(curVal), fieldBox);
                edit->setFont(Theme::MonoFont(9));
                if (!m_firstInput) m_firstInput = edit;
                connect(edit, &QLineEdit::editingFinished, this, [edit, commitValue]() {
                    commitValue(edit->text().toStdString());
                });

                auto* fxBtn = new QPushButton(QStringLiteral("fx"), fieldBox);
                fxBtn->setFixedWidth(34);
                fxBtn->setFont(Theme::MonoFont(9));
                fxBtn->setToolTip(UiFormat::Label("mod.expr.title"));
                connect(fxBtn, &QPushButton::clicked, this, [this, edit, paramKind, commitValue]() {
                    ExprPickerDialog dlg(this);
                    if (dlg.exec() != QDialog::Accepted || !dlg.chosenExpression()) return;
                    const auto* e = dlg.chosenExpression();
                    std::string val = edit->text().toStdString();
                    const bool started = !val.empty() && val[0] == '=' && (val.size() < 2 || val[1] != '=');
                    const bool asCode = (paramKind == "number") || started;
                    const char mark = e->kind == "int" ? '#' : (e->kind == "text" ? '$' : '~');
                    const std::string piece = asCode ? e->text : (std::string("{") + mark + e->text + "}");
                    size_t at = std::min(static_cast<size_t>(std::max(0, edit->cursorPosition())), val.size());
                    if (asCode && val.empty()) val = "=" + piece;
                    else if (asCode) {
                        if (val[0] != '=') { val.insert(val.begin(), '='); ++at; }
                        val.insert(std::max<size_t>(at, 1), piece);
                    } else {
                        val.insert(at, piece);
                    }
                    edit->setText(QString::fromStdString(val));
                    commitValue(val);
                });

                row->addWidget(edit, 1);
                row->addWidget(fxBtn);
                fieldLay->addLayout(row);
            }

            m_paramLayout->addWidget(fieldBox);
        }
    } else {
        auto* none = new QLabel(UiFormat::Label("app.inspector.noParams"), m_paramHost);
        none->setStyleSheet(QStringLiteral("color: #8c93a8;"));
        m_paramLayout->addWidget(none);
    }

    auto* btnRow = new QWidget(m_paramHost);
    auto* btnLay = new QHBoxLayout(btnRow);
    btnLay->setContentsMargins(0, 6, 0, 0);
    btnLay->setSpacing(6);

    if (m_sel.slot != UiFormat::Slot::Event || !insideNonGroupRule) {
        auto* replaceBtn = new QPushButton(UiFormat::Label("mod.replace"), btnRow);
        connect(replaceBtn, &QPushButton::clicked, this, [this]() {
            emit requestReplace(m_sel.path, m_sel.slot, m_sel.index);
        });
        btnLay->addWidget(replaceBtn, 1);
    }

    const QString delText = m_sel.slot == UiFormat::Slot::Condition
        ? UiFormat::Label("app.sheet.deleteCond")
        : (m_sel.slot == UiFormat::Slot::Action
            ? UiFormat::Label("app.sheet.deleteAct")
            : UiFormat::Label("app.sheet.deleteRule"));
    auto* delBtn = new QPushButton(delText, btnRow);
    connect(delBtn, &QPushButton::clicked, this, &InspectorDock::requestDeleteSelected);
    btnLay->addWidget(delBtn, 1);

    m_paramLayout->addWidget(btnRow);
    m_paramLayout->addStretch(1);

    m_paramHost->updateGeometry();
    m_rebuilding = false;
}

void InspectorDock::rebuildVarsTable() {
    m_rebuilding = true;
    m_varsTable->setRowCount(0);
    m_varsWarn->hide();

    if (!m_sheet) {
        m_rebuilding = false;
        return;
    }

    static const char* const VarKinds[] = { "int", "float", "text", "bool", "list" };
    bool anyBad = false;

    for (int i = 0; i < static_cast<int>(m_sheet->vars.size()); ++i) {
        const ModLogic::Var& v = m_sheet->vars[i];
        m_varsTable->insertRow(i);

        auto* nameItem = new QTableWidgetItem(QString::fromStdString(v.name));
        nameItem->setFont(Theme::MonoFont(9));
        if (!v.name.empty() && !UiFormat::IsValidIdentifier(v.name)) {
            nameItem->setForeground(Theme::Colors().danger);
            anyBad = true;
        }
        m_varsTable->setItem(i, 0, nameItem);

        auto* typeCombo = new QComboBox(m_varsTable);
        for (const char* k : VarKinds) {
            typeCombo->addItem(UiFormat::VarTypeLabel(k), QString::fromLatin1(k));
        }
        const int found = typeCombo->findData(QString::fromStdString(v.type));
        typeCombo->setCurrentIndex(found >= 0 ? found : 0);
        connect(typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, i, typeCombo]() {
            if (m_rebuilding || !m_sheet || i >= static_cast<int>(m_sheet->vars.size())) return;
            emit aboutToEdit();
            ModLogic::Var& var = m_sheet->vars[i];
            var.type = typeCombo->currentData().toString().toStdString();
            if (var.type == "text") var.value.clear();
            else if (var.type == "bool") var.value = "0";
            else if (var.type == "list") var.value = "16";
            else if (var.value.find_first_not_of("-0123456789.") != std::string::npos) var.value = "0";
            rebuildVarsTable();
            rebuildCodePreview();
            emit sheetEdited();
        });
        m_varsTable->setCellWidget(i, 1, typeCombo);

        auto* valItem = new QTableWidgetItem(QString::fromStdString(v.value));
        valItem->setFont(Theme::MonoFont(9));
        m_varsTable->setItem(i, 2, valItem);
    }

    if (anyBad) {
        m_varsWarn->setText(UiFormat::Label("mod.varBadName"));
        m_varsWarn->show();
    }
    m_rebuilding = false;
}

void InspectorDock::commitVarsFromTable() {
    if (!m_sheet) return;
    const int rows = std::min(m_varsTable->rowCount(), static_cast<int>(m_sheet->vars.size()));
    bool changed = false;
    bool anyBad = false;
    for (int i = 0; i < rows; ++i) {
        QTableWidgetItem* ni = m_varsTable->item(i, 0);
        QTableWidgetItem* vi = m_varsTable->item(i, 2);
        if (!ni || !vi) continue;
        const std::string newName = ni->text().trimmed().toStdString();
        const std::string newVal = vi->text().toStdString();
        if (m_sheet->vars[i].name != newName || m_sheet->vars[i].value != newVal) {
            if (!changed) emit aboutToEdit();
            m_sheet->vars[i].name = newName;
            m_sheet->vars[i].value = newVal;
            changed = true;
        }
        const bool bad = !newName.empty() && !UiFormat::IsValidIdentifier(newName);
        anyBad = anyBad || bad;
        m_rebuilding = true;
        ni->setForeground(bad ? Theme::Colors().danger : Theme::Colors().text);
        m_rebuilding = false;
    }
    if (anyBad) {
        m_varsWarn->setText(UiFormat::Label("mod.varBadName"));
        m_varsWarn->show();
    } else {
        m_varsWarn->hide();
    }
    if (changed) {
        rebuildCodePreview();
        emit sheetEdited();
    }
}

void InspectorDock::rebuildCodePreview() {
    if (!m_sheet || !m_sel.valid()) {
        m_codePreview->setPlainText(QStringLiteral("/* ") +
            UiFormat::Label("app.inspector.codeEmpty") +
            QStringLiteral(" */"));
        return;
    }
    const ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, m_sel.path);
    if (!rule) {
        m_codePreview->setPlainText(QStringLiteral("/* ") +
            UiFormat::Label("app.inspector.codeEmpty") +
            QStringLiteral(" */"));
        return;
    }
    std::vector<std::string> warnings;
    const std::string code = ModCodegen::GenerateRulePreview(*m_sheet, *rule, &warnings);
    QString text = QString::fromStdString(code);
    if (!warnings.empty()) {
        text += QStringLiteral("\n/* Warnings:\n");
        for (const std::string& w : warnings) text += QStringLiteral(" * ") + QString::fromStdString(w) + '\n';
        text += QStringLiteral(" */\n");
    }
    m_codePreview->setPlainText(text);
}
