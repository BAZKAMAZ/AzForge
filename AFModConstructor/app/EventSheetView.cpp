#include "EventSheetView.h"

#include "IconCache.h"
#include "Localization.h"
#include "ModCatalog.h"
#include "Theme.h"

#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDrag>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>

#include <algorithm>

namespace {

constexpr int HeaderH = 28;
constexpr int NumW = 48;
constexpr int LineH = 25;
constexpr int GroupH = 30;
constexpr int RowPadY = 5;
constexpr int RowGap = 5;
constexpr int IndentStep = 20;
constexpr int OuterMarginX = 8;
constexpr int IconSize = 16;
constexpr const char* MimeType = "application/x-afmc-line";
constexpr const char* TriggerOnce = "system.triggerOnce";

QByteArray EncodeDrag(const std::vector<int>& path, int slot, const std::vector<int>& indices, const std::vector<std::vector<int>>& paths) {
    QByteArray ba;
    QDataStream ds(&ba, QIODevice::WriteOnly);
    ds << static_cast<qint32>(2); // payload version 2 (multi-item)
    ds << static_cast<qint32>(slot);
    ds << static_cast<qint32>(path.size());
    for (int v : path) ds << static_cast<qint32>(v);
    ds << static_cast<qint32>(indices.size());
    for (int idx : indices) ds << static_cast<qint32>(idx);
    ds << static_cast<qint32>(paths.size());
    for (const auto& p : paths) {
        ds << static_cast<qint32>(p.size());
        for (int v : p) ds << static_cast<qint32>(v);
    }
    return ba;
}

bool DecodeDrag(const QByteArray& ba, std::vector<int>& path, int& slot, std::vector<int>& indices, std::vector<std::vector<int>>& paths) {
    QDataStream ds(ba);
    qint32 version = 0, s = 0, count = 0;
    ds >> version >> s >> count;
    if (version != 2 || count <= 0 || count > 64) return false;
    path.resize(static_cast<size_t>(count));
    for (qint32 i = 0; i < count; ++i) {
        qint32 v = 0;
        ds >> v;
        path[static_cast<size_t>(i)] = v;
    }
    qint32 idxCount = 0;
    ds >> idxCount;
    if (idxCount < 0 || idxCount > 4096) return false;
    indices.resize(static_cast<size_t>(idxCount));
    for (qint32 i = 0; i < idxCount; ++i) {
        qint32 v = 0;
        ds >> v;
        indices[static_cast<size_t>(i)] = v;
    }
    qint32 pathCount = 0;
    ds >> pathCount;
    if (pathCount < 0 || pathCount > 4096) return false;
    paths.resize(static_cast<size_t>(pathCount));
    for (qint32 i = 0; i < pathCount; ++i) {
        qint32 len = 0;
        ds >> len;
        if (len <= 0 || len > 64) return false;
        paths[static_cast<size_t>(i)].resize(static_cast<size_t>(len));
        for (qint32 j = 0; j < len; ++j) {
            qint32 v = 0;
            ds >> v;
            paths[static_cast<size_t>(i)][static_cast<size_t>(j)] = v;
        }
    }
    slot = s;
    return ds.status() == QDataStream::Ok;
}

bool IsInsidePath(const std::vector<int>& outer, const std::vector<int>& inner) {
    if (outer.size() > inner.size()) return false;
    for (size_t i = 0; i < outer.size(); ++i) {
        if (outer[i] != inner[i]) return false;
    }
    return true;
}

void AdjustPathAfterErase(std::vector<int>& tracked, const std::vector<int>& erased) {
    if (erased.empty() || tracked.size() < erased.size()) return;
    const size_t k = erased.size() - 1;
    for (size_t i = 0; i < k; ++i) {
        if (tracked[i] != erased[i]) return;
    }
    if (tracked[k] > erased[k]) --tracked[k];
}

void SetCollapseRecursive(std::vector<ModLogic::Rule>& rules, bool collapsed) {
    for (ModLogic::Rule& r : rules) {
        if (r.group) r.collapsed = collapsed;
        SetCollapseRecursive(r.children, collapsed);
    }
}

} // namespace

EventSheetView::EventSheetView(QWidget* parent) : QAbstractScrollArea(parent) {
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    viewport()->setAcceptDrops(true);
    verticalScrollBar()->setSingleStep(36);
    horizontalScrollBar()->setSingleStep(24);
}

void EventSheetView::setSheet(ModLogic::Sheet* sheet) {
    closeInlineEdit(false);
    m_sheet = sheet;
    m_sel = {};
    verticalScrollBar()->setValue(0);
    rebuildLayout();
    viewport()->update();
}

void EventSheetView::setSelection(const UiFormat::Selection& sel, bool emitSignal) {
    m_sel = sel;
    if (m_sel.valid() && m_sel.slot == UiFormat::Slot::Event) {
        m_sel.index = -1;
        m_sel.run.clear();
        if (m_sel.selectedPaths.empty()) {
            m_sel.selectedPaths.push_back(m_sel.path);
        }
    } else if (!m_sel.valid()) {
        m_sel.selectedPaths.clear();
        m_sel.run.clear();
        m_sel.index = -1;
    } else {
        m_sel.selectedPaths.clear();
    }
    viewport()->update();
    if (emitSignal) emit selectionChanged(m_sel);
}

void EventSheetView::setFilter(const QString& query, const std::string& subsystemId, bool brokenOnly) {
    closeInlineEdit(true);
    m_filterQuery = query.trimmed().toLower();
    m_filterSubsystem = subsystemId;
    m_filterBrokenOnly = brokenOnly;
    rebuildLayout();
    viewport()->update();
}

void EventSheetView::setAllGroupsCollapsed(bool collapsed) {
    if (!m_sheet) return;
    SetCollapseRecursive(m_sheet->rules, collapsed);
    rebuildLayout();
    viewport()->update();
}

void EventSheetView::scrollToRule(const std::vector<int>& path) {
    if (!m_sheet || path.empty()) return;

    // Expand any collapsed ancestor groups so target becomes visible
    bool expandedAny = false;
    for (size_t d = 1; d < path.size(); ++d) {
        std::vector<int> head(path.begin(), path.begin() + static_cast<ptrdiff_t>(d));
        if (ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, head)) {
            if (r->group && r->collapsed) {
                r->collapsed = false;
                expandedAny = true;
            }
        }
    }
    if (expandedAny) rebuildLayout();

    for (const VisualRow& row : m_rows) {
        if (row.path == path) {
            const int targetY = std::max(0, row.cardRect.top() - HeaderH - 12);
            verticalScrollBar()->setValue(targetY);
            setSelection({ path, UiFormat::Slot::Event, -1, {}, { path } }, true);
            break;
        }
    }
}

void EventSheetView::refreshLayout() {
    rebuildLayout();
    viewport()->update();
}

int EventSheetView::splitX(int viewportWidth) const {
    const int avail = std::max(320, viewportWidth - OuterMarginX * 2 - NumW);
    return OuterMarginX + NumW + static_cast<int>(avail * m_splitRatio);
}

QPoint EventSheetView::toContent(const QPoint& vpPos) const {
    return QPoint(vpPos.x() + horizontalScrollBar()->value(), vpPos.y() + verticalScrollBar()->value());
}

QPoint EventSheetView::fromContent(const QPoint& cPos) const {
    return QPoint(cPos.x() - horizontalScrollBar()->value(), cPos.y() - verticalScrollBar()->value());
}

bool EventSheetView::ruleMatchesFilter(const ModLogic::Rule& rule) const {
    const bool hasQ = !m_filterQuery.isEmpty();
    const bool hasSub = !m_filterSubsystem.empty();
    if (!hasQ && !hasSub && !m_filterBrokenOnly) return true;

    if (m_filterBrokenOnly && !UiFormat::IsRuleBroken(rule)) return false;

    if (rule.group) {
        if (hasQ && !hasSub && QString::fromStdString(rule.name).toLower().contains(m_filterQuery)) {
            return true;
        }
        for (const ModLogic::Rule& ch : rule.children) {
            if (ruleMatchesFilter(ch)) return true;
        }
        return false;
    }

    auto textMatch = [&](const std::string& id, const QString& lineText, const std::vector<ModLogic::Param>& args) {
        if (!hasQ) return true;
        if (QString::fromStdString(id).toLower().contains(m_filterQuery)) return true;
        if (lineText.toLower().contains(m_filterQuery)) return true;
        for (const auto& p : args) {
            if (QString::fromStdString(p.value).toLower().contains(m_filterQuery)) return true;
        }
        return false;
    };

    bool selfMatch = false;
    if (!rule.event.empty()) {
        const auto* def = ModCatalog::FindEvent(rule.event);
        const std::string grp = def ? def->group : "";
        const QString line = def ? UiFormat::LinePlain(def->label, def->filters, rule.filters)
                                 : QString::fromStdString(rule.event);

        if ((!hasSub || grp == m_filterSubsystem) && textMatch(rule.event, line, rule.filters)) {
            selfMatch = true;
        }
    }
    for (const auto& c : rule.conditions) {
        const auto* def = ModCatalog::FindCondition(c.id);
        const std::string grp = def ? def->group : "";
        const QString line = def ? UiFormat::LinePlain(def->label, def->params, c.args)
                                 : QString::fromStdString(c.id);

        if ((!hasSub || grp == m_filterSubsystem) && textMatch(c.id, line, c.args)) {
            selfMatch = true;
        }
    }
    for (const auto& a : rule.actions) {
        const auto* def = ModCatalog::FindAction(a.id);
        const std::string grp = def ? def->group : "";
        const QString line = def ? UiFormat::LinePlain(def->label, def->params, a.args)
                                 : QString::fromStdString(a.id);
        if ((!hasSub || grp == m_filterSubsystem) && textMatch(a.id, line, a.args)) {
            selfMatch = true;
        }
    }
    
    if (selfMatch) return true;

    for (const ModLogic::Rule& ch : rule.children) {
        if (ruleMatchesFilter(ch)) return true;
    }
    return false;
}

void EventSheetView::rebuildLayout() {
    m_rows.clear();
    m_totalHeight = HeaderH + 16;

    if (!m_sheet) {
        verticalScrollBar()->setRange(0, 0);
        return;
    }

    const int vpW = std::max(540, viewport()->width());
    const int contentW = vpW - OuterMarginX * 2;
    const int sx = splitX(vpW);
    const int condColW = sx - (OuterMarginX + NumW);

    int y = HeaderH + 8;
    std::vector<int> path;
    layoutRulesRecursive(m_sheet->rules, path, 0, y, contentW, condColW);

    m_totalHeight = y + 32;
    const int maxV = std::max(0, m_totalHeight - viewport()->height());
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setRange(0, maxV);
    horizontalScrollBar()->setRange(0, 0);
}

void EventSheetView::layoutRulesRecursive(const std::vector<ModLogic::Rule>& rules, std::vector<int>& path, int depth, int& yCursor, int contentWidth, int condColW) {
    const QFont regularFont = font();
    QFont boldFont = regularFont;
    boldFont.setBold(true);
    const QFont monoFont = Theme::MonoFont(9);
    const QFontMetrics fmReg(regularFont);
    const QFontMetrics fmBold(boldFont);
    const QFontMetrics fmMono(monoFont);

    const int vpW = std::max(540, viewport()->width());
    const int sx = splitX(vpW);

    auto splitTextTokens = [](const QString& text) {
        std::vector<QString> out;
        int i = 0;
        const int n = text.size();
        while (i < n) {
            const int start = i;
            while (i < n && text[i].isSpace()) ++i;
            while (i < n && !text[i].isSpace()) ++i;
            while (i < n && text[i].isSpace()) ++i;
            out.push_back(text.mid(start, i - start));
        }
        return out;
    };

    auto layoutLine = [&](VisualLine& line, int colLeft, int colWidth, int topY, const std::vector<ModCatalog::ParamDef>* defs, const std::vector<ModLogic::Param>* args) {
        const int invPad = line.inverted ? 16 : 0;
        const int textStartX = colLeft + 2 + 6 + invPad + IconSize + 6;
        const int maxRight = std::max(textStartX + 64, colLeft + colWidth - 6);
        const int fullPillCap = std::clamp(maxRight - textStartX - 4, 56, 220);
        const QFontMetrics& fmText = (line.slot == UiFormat::Slot::Event) ? fmBold : fmReg;

        int x = textStartX;
        int rowY = topY;

        for (size_t sIdx = 0; sIdx < line.segments.size(); ++sIdx) {
            const UiFormat::LineSegment& seg = line.segments[sIdx];
            if (!seg.isParam) {
                for (QString tok : splitTextTokens(seg.text)) {
                    if (x == textStartX) {
                        while (tok.startsWith(QLatin1Char(' '))) tok.remove(0, 1);
                        if (tok.isEmpty()) continue;
                    }
                    int w = fmText.horizontalAdvance(tok);
                    if (x > textStartX && x + w > maxRight) {
                        rowY += LineH;
                        x = textStartX;
                        while (tok.startsWith(QLatin1Char(' '))) tok.remove(0, 1);
                        if (tok.isEmpty()) continue;
                        w = fmText.horizontalAdvance(tok);
                    }
                    line.textRuns.push_back({ QRect(x, rowY, w + 6, LineH), tok, false });
                    x += w;
                }
                continue;
            }

            QString shown = seg.text;
            const int padW = seg.isEnum ? 24 : 12;
            const QFontMetrics& fmPill = (seg.isEnum || seg.hasOptionLabel) ? fmReg : fmMono;
            int textW = fmPill.horizontalAdvance(shown);
            if (textW + padW > fullPillCap) {
                shown = fmPill.elidedText(shown, Qt::ElideRight, std::max(28, fullPillCap - padW));
                textW = fmPill.horizontalAdvance(shown);
            }
            const int pw = std::max(24, textW + padW);

            int tailW = 0;
            if (sIdx + 1 < line.segments.size() && !line.segments[sIdx + 1].isParam) {
                const QString& nextText = line.segments[sIdx + 1].text;
                int pLen = 0;
                while (pLen < nextText.size() && !nextText[pLen].isSpace() && pLen < 4) ++pLen;
                if (pLen > 0) tailW = fmText.horizontalAdvance(nextText.left(pLen));
            }

            if (x > textStartX && x + pw + tailW > maxRight) {
                rowY += LineH;
                x = textStartX;
            }

            PillHit phit;
            phit.rect = QRect(x, rowY + 3, pw, LineH - 6);
            phit.paramKey = seg.paramKey;
            phit.rawValue = seg.rawValue;
            phit.displayText = shown;
            phit.paramLabel = seg.paramLabel;
            phit.isExpr = seg.isExpr;
            phit.isEnum = seg.isEnum;
            phit.hasOptionLabel = seg.hasOptionLabel;
            phit.canRemove = seg.canRemove;
            line.pills.push_back(std::move(phit));
            x += pw + 3;
        }

        if (defs && args && ModCatalog::HasAddableOptionalParams(*defs, *args)) {
            const int pw = 20;
            const int padLeft = (x > textStartX) ? 3 : 0;
            if (x > textStartX && x + padLeft + pw > maxRight) {
                rowY += LineH;
                x = textStartX;
            } else {
                x += padLeft;
            }
            PillHit addHit;
            addHit.rect = QRect(x, rowY + 3, pw, LineH - 6);
            addHit.displayText = QStringLiteral("+");
            addHit.paramLabel = UiFormat::Label("mod.addParam");
            addHit.isAddParam = true;
            line.pills.push_back(std::move(addHit));
            x += pw + 3;
        }

        if (line.veto) {
            const QString vt = QStringLiteral("[") + UiFormat::Label("mod.rule.veto") + QStringLiteral("]");
            const int vw = fmReg.horizontalAdvance(vt);
            int padLeft = (x > textStartX) ? 6 : 0;
            if (x > textStartX && x + padLeft + vw > maxRight) {
                rowY += LineH;
                x = textStartX;
                padLeft = 0;
            }
            line.textRuns.push_back({ QRect(x + padLeft, rowY, vw + 6, LineH), vt, true });
            x += padLeft + vw + 2;
        }

        if (!line.warnKey.empty()) {
            const QString wt = QStringLiteral("⚠");
            const int ww = fmReg.horizontalAdvance(wt) + 4;
            int padLeft = (x > textStartX) ? 6 : 0;
            if (x > textStartX && x + padLeft + ww > maxRight) {
                rowY += LineH;
                x = textStartX;
                padLeft = 0;
            }
            line.textRuns.push_back({ QRect(x + padLeft, rowY, ww + 6, LineH), wt, true });
        }

        line.rect = QRect(colLeft + 2, topY, std::max(40, colWidth - 4), (rowY - topY) + LineH);
    };

    for (int i = 0; i < static_cast<int>(rules.size()); ++i) {
        const ModLogic::Rule& rule = rules[i];
        if (!ruleMatchesFilter(rule)) continue;

        path.push_back(i);
        const int indent = depth * IndentStep;
        const int cardLeft = OuterMarginX + indent;
        const int cardRight = OuterMarginX + contentWidth;

        if (rule.group) {
            VisualRow row;
            row.path = path;
            row.number = UiFormat::RuleNumber(path);
            row.depth = depth;
            row.isGroup = true;
            row.groupCollapsed = rule.collapsed;
            row.enabled = rule.enabled;
            row.groupTitle = rule.name.empty() ? UiFormat::Label("mod.group")
                                               : QString::fromStdString(rule.name);
            row.groupCount = static_cast<int>(rule.children.size());
            row.accentColor = Theme::Colors().accent;
            row.cardRect = QRect(cardLeft, yCursor, std::max(200, cardRight - cardLeft), GroupH);
            row.numRect = QRect(cardLeft + 4, yCursor, NumW - 4, GroupH);
            row.foldRect = QRect(cardLeft + NumW + 4, yCursor + 5, 20, 20);
            m_rows.push_back(std::move(row));
            yCursor += GroupH + RowGap;

            if (!rule.collapsed || !m_filterQuery.isEmpty() || !m_filterSubsystem.empty() || m_filterBrokenOnly) {
                layoutRulesRecursive(rule.children, path, depth + 1, yCursor, contentWidth, condColW);
            }
            path.pop_back();
            continue;
        }

        VisualRow row;
        row.path = path;
        row.number = UiFormat::RuleNumber(path);
        row.depth = depth;
        row.isGroup = false;
        row.enabled = rule.enabled;

        // Determine subsystem stripe color from event or first condition or first action
        std::string leadGroup;
        if (!rule.event.empty()) {
            if (const auto* ev = ModCatalog::FindEvent(rule.event)) leadGroup = ev->group;
        }
        if (leadGroup.empty() && !rule.conditions.empty()) {
            if (const auto* cd = ModCatalog::FindCondition(rule.conditions[0].id)) leadGroup = cd->group;
        }
        if (leadGroup.empty() && !rule.actions.empty()) {
            if (const auto* ad = ModCatalog::FindAction(rule.actions[0].id)) leadGroup = ad->group;
        }
        row.accentColor = UiFormat::SubsystemColor(leadGroup);

        const int condStart = std::max(cardLeft + NumW, OuterMarginX + NumW + indent);
        const int actualSplitX = std::max(condStart + 140, sx);
        const int condColWActual = actualSplitX - condStart;
        const int actColWActual = std::max(140, cardRight - actualSplitX);

        bool insideNonGroupRule = false;
        for (size_t d = 1; d < path.size(); ++d) {
            std::vector<int> head(path.begin(), path.begin() + static_cast<ptrdiff_t>(d));
            if (const ModLogic::Rule* anc = UiFormat::RuleAt(*m_sheet, head)) {
                if (!anc->group) { insideNonGroupRule = true; break; }
            }
        }

        int ly = yCursor + RowPadY;
        if (!rule.event.empty()) {
            const ModCatalog::EventDef* def = ModCatalog::FindEvent(rule.event);
            VisualLine vl;
            vl.slot = UiFormat::Slot::Event;
            vl.index = -1;
            vl.entryId = rule.event;
            vl.groupId = def ? def->group : "";
            vl.enabled = rule.enabled;
            vl.unknown = (def == nullptr);
            vl.veto = def && def->canVeto && rule.veto;
            if (insideNonGroupRule) vl.warnKey = "app.problems.nestedEvent";
            if (def) {
                if (vl.warnKey.empty()) vl.warnKey = UiFormat::MissingParamsContext(*m_sheet, path, rule.filters);
                vl.segments = UiFormat::BuildSegments(def->label, def->filters, rule.filters);
            } else {
                vl.segments = { { QString::fromStdString(rule.event) + QStringLiteral("  ") +
                                  UiFormat::Label("mod.unknown"), false, {}, {}, {}, false } };
            }
            layoutLine(vl, condStart, condColWActual, ly, def ? &def->filters : nullptr, &rule.filters);
            ly += vl.rect.height();
            row.leftLines.push_back(std::move(vl));
        }

        for (int c = 0; c < static_cast<int>(rule.conditions.size()); ++c) {
            const ModLogic::Condition& cond = rule.conditions[c];
            const ModCatalog::ConditionDef* cd = ModCatalog::FindCondition(cond.id);
            VisualLine vl;
            vl.slot = UiFormat::Slot::Condition;
            vl.index = c;
            vl.entryId = cond.id;
            vl.groupId = cd ? cd->group : "";
            vl.inverted = cond.inverted;
            vl.enabled = rule.enabled && cond.enabled;
            vl.unknown = (cd == nullptr);
            if (cd) {
                vl.warnKey = UiFormat::MissingConditionContext(*m_sheet, path, *cd, cond.args);
                vl.segments = UiFormat::BuildSegments(cd->label, cd->params, cond.args);
            } else {
                vl.segments = { { QString::fromStdString(cond.id) + QStringLiteral("  ") +
                                  UiFormat::Label("mod.unknown"), false, {}, {}, {}, false } };
            }
            layoutLine(vl, condStart, condColWActual, ly, cd ? &cd->params : nullptr, &cond.args);
            ly += vl.rect.height();
            row.leftLines.push_back(std::move(vl));
        }
        row.addCondRect = QRect(condStart + 8, ly + 2, 100, LineH - 4);
        ly += LineH;

        int ry = yCursor + RowPadY;
        for (int a = 0; a < static_cast<int>(rule.actions.size()); ++a) {
            const ModLogic::Action& act = rule.actions[a];
            const ModCatalog::ActionDef* ad = ModCatalog::FindAction(act.id);
            VisualLine vl;
            vl.slot = UiFormat::Slot::Action;
            vl.index = a;
            vl.entryId = act.id;
            vl.groupId = ad ? ad->group : "";
            vl.enabled = rule.enabled && act.enabled;
            vl.unknown = (ad == nullptr);
            if (ad) {
                vl.warnKey = UiFormat::MissingContext(*m_sheet, path, *ad, act.args);
                vl.segments = UiFormat::BuildSegments(ad->label, ad->params, act.args);
            } else {
                vl.segments = { { QString::fromStdString(act.id) + QStringLiteral("  ") +
                                  UiFormat::Label("mod.unknown"), false, {}, {}, {}, false } };
            }
            layoutLine(vl, actualSplitX, actColWActual, ry, ad ? &ad->params : nullptr, &act.args);
            ry += vl.rect.height();
            row.rightLines.push_back(std::move(vl));
        }
        row.addActRect = QRect(actualSplitX + 8, ry + 2, 100, LineH - 4);
        ry += LineH;

        const int cardH = std::max(ly, ry) - yCursor + RowPadY;
        row.cardRect = QRect(cardLeft, yCursor, std::max(260, cardRight - cardLeft), cardH);
        row.numRect = QRect(cardLeft + 3, yCursor, NumW - 3, cardH);
        row.condColRect = QRect(condStart, yCursor, condColWActual, cardH);
        row.actColRect = QRect(actualSplitX, yCursor, actColWActual, cardH);

        m_rows.push_back(std::move(row));
        yCursor += cardH + RowGap;

        if (!rule.children.empty()) {
            layoutRulesRecursive(rule.children, path, depth + 1, yCursor, contentWidth, condColW);
        }
        path.pop_back();
    }
}

void EventSheetView::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto& c = Theme::Colors();
    const QRect vp = viewport()->rect();
    p.fillRect(vp, c.windowBg);

    const int scrollY = verticalScrollBar()->value();
    const QFont regFont = font();
    QFont boldFont = regFont;
    boldFont.setBold(true);
    const QFont monoFont = Theme::MonoFont(9);
    const QFontMetrics fmReg(regFont);
    const QFontMetrics fmBold(boldFont);
    const QFontMetrics fmMono(monoFont);

    if (!m_sheet) {
        p.setPen(c.textMuted);
        p.drawText(vp, Qt::AlignCenter,
                   UiFormat::Label("app.sheet.emptyNoProject"));
        return;
    }

    p.save();
    p.setClipRect(QRect(0, HeaderH, vp.width(), vp.height() - HeaderH));
    p.translate(0, -scrollY);

    const int visTop = scrollY + HeaderH;
    const int visBot = scrollY + vp.height();

    for (int rIdx = 0; rIdx < static_cast<int>(m_rows.size()); ++rIdx) {
        const VisualRow& row = m_rows[rIdx];
        if (row.cardRect.bottom() < visTop - 10) continue;
        if (row.cardRect.top() > visBot + 10) break;

        p.setPen(QPen(c.borderSoft, 1));
        for (int d = 0; d < row.depth; ++d) {
            const int gx = OuterMarginX + d * IndentStep + 9;
            p.drawLine(gx, row.cardRect.top() - RowGap, gx, row.cardRect.bottom() + RowGap);
        }

        const bool ruleSelected = isRulePicked(row.path);

        if (row.isGroup) {
            p.setBrush(ruleSelected ? c.cardSelect : c.groupBg);
            p.setPen(QPen(ruleSelected ? c.accent : c.border, 1));
            p.drawRoundedRect(row.cardRect, 5, 5);

            p.setBrush(c.panelBg);
            p.setPen(QPen(c.border, 1));
            p.drawRoundedRect(row.foldRect, 3, 3);
            p.setPen(c.text);
            p.drawText(row.foldRect, Qt::AlignCenter, row.groupCollapsed ? QStringLiteral("▸") : QStringLiteral("▾"));

            p.setPen(c.textNumber);
            p.setFont(monoFont);
            p.drawText(row.numRect.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, row.number);

            QFont gf = regFont;
            gf.setBold(true);
            gf.setStrikeOut(!row.enabled);
            p.setFont(gf);
            p.setPen(row.enabled ? c.accent : c.textMuted);
            const QRect titleR(row.foldRect.right() + 8, row.cardRect.top(),
                               row.cardRect.right() - row.foldRect.right() - 100, row.cardRect.height());
            p.drawText(titleR, Qt::AlignVCenter | Qt::AlignLeft, row.groupTitle);

            p.setFont(regFont);
            p.setPen(c.textMuted);
            const QString cntText = UiFormat::Label("mod.group.rules") + QStringLiteral(" ") + QString::number(row.groupCount);
            p.drawText(row.cardRect.adjusted(0, 0, -12, 0), Qt::AlignVCenter | Qt::AlignRight, cntText);
            continue;
        }

        p.setBrush(ruleSelected ? c.cardSelect : c.cardBg);
        p.setPen(QPen(ruleSelected ? c.accent : c.border, 1));
        p.drawRoundedRect(row.cardRect, 5, 5);

        QRect stripe(row.cardRect.left(), row.cardRect.top() + 1, 3, row.cardRect.height() - 2);
        p.fillRect(stripe, row.enabled ? row.accentColor : c.border);

        p.setPen(QPen(c.borderSoft, 1));
        p.drawLine(row.condColRect.left(), row.cardRect.top() + 1, row.condColRect.left(), row.cardRect.bottom() - 1);
        p.drawLine(row.actColRect.left(), row.cardRect.top() + 1, row.actColRect.left(), row.cardRect.bottom() - 1);

        p.setFont(monoFont);
        p.setPen(ruleSelected ? c.accent : c.textNumber);
        p.drawText(row.numRect.adjusted(5, RowPadY + 3, -2, 0), Qt::AlignTop | Qt::AlignLeft, row.number);

        auto drawLine = [&](const VisualLine& vl, int lIdx) {
            const bool picked = isLinePicked(row.path, vl.slot, vl.index);
            const bool hovered = (m_hover.rowIndex == rIdx && (m_hover.kind == HitResult::Kind::Line || m_hover.kind == HitResult::Kind::Pill) && m_hover.slot == vl.slot && 
                m_hover.lineIndex == lIdx);

            if (picked) {
                p.setBrush(QColor(0x3a, 0x33, 0x5c));
                p.setPen(QPen(c.accent, 1));
                p.drawRoundedRect(vl.rect, 4, 4);
            } else if (hovered) {
                p.setBrush(c.cardHover);
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(vl.rect, 4, 4);
            }

            p.save();
            p.setClipRect(vl.rect.adjusted(2, 0, -2, 0));

            int x = vl.rect.left() + 6;
            const int midY = vl.rect.top() + (LineH - IconSize) / 2;

            if (vl.inverted) {
                QFont xf = regFont;
                xf.setBold(true);
                p.setFont(xf);
                p.setPen(c.danger);
                p.drawText(QRect(x, vl.rect.top(), 14, LineH), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("✕"));
                x += 16;
            }

            const QPixmap iconPm = IconCache::Instance().SubsystemPixmap(vl.groupId, IconSize);
            if (!iconPm.isNull()) {
                if (!vl.enabled) p.setOpacity(0.45);
                p.drawPixmap(x, midY, iconPm);
                p.setOpacity(1.0);
            }

            QColor textCol = c.text;
            if (vl.unknown) textCol = c.danger;
            else if (!vl.enabled) textCol = c.textMuted;
            else if (!vl.warnKey.empty()) textCol = c.warning;
            else if (vl.slot == UiFormat::Slot::Event) textCol = c.accent;
            else if (vl.slot == UiFormat::Slot::Action) textCol = c.action;

            QFont lineFont = regFont;
            if (vl.slot == UiFormat::Slot::Event) lineFont.setBold(true);
            lineFont.setStrikeOut(!vl.enabled);

            for (const TextRun& run : vl.textRuns) {
                if (run.warningStyle) {
                    p.setFont(regFont);
                    p.setPen(c.warning);
                } else {
                    p.setFont(lineFont);
                    p.setPen(textCol);
                }
                p.drawText(run.rect, Qt::AlignVCenter | Qt::AlignLeft, run.text);
            }

            for (int pillIdx = 0; pillIdx < static_cast<int>(vl.pills.size()); ++pillIdx) {
                const PillHit& pill = vl.pills[pillIdx];
                const bool pillHovered = (m_hover.kind == HitResult::Kind::Pill &&
                                          m_hover.rowIndex == rIdx &&
                                          m_hover.slot == vl.slot &&
                                          m_hover.lineIndex == lIdx &&
                                          m_hover.pillIndex == pillIdx);
                p.setBrush(pillHovered ? c.pillHover : c.pillBg);
                QPen pillPen(pillHovered ? c.accent : c.pillBorder, 1);
                if (pill.isAddParam && !pillHovered) pillPen.setStyle(Qt::DashLine);
                p.setPen(pillPen);
                p.drawRoundedRect(pill.rect, 4, 4);

                QFont pf = (pill.isEnum || pill.hasOptionLabel || pill.isAddParam) ? regFont : monoFont;
                if (pill.isAddParam) pf.setBold(true);
                pf.setStrikeOut(!vl.enabled);
                p.setFont(pf);
                const QColor pillFg = !vl.enabled
                    ? c.textMuted
                    : (pill.isAddParam ? (pillHovered ? c.accent : c.textMuted) : (pill.isExpr ? c.exprText : c.pillText));
                p.setPen(pillFg);
                if (pill.isEnum) {
                    const QRect textRect = pill.rect.adjusted(6, 0, -14, 0);
                    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, pill.displayText);
                    const int cx = pill.rect.right() - 8;
                    const int cyMid = pill.rect.center().y();
                    QPen chevPen(pillHovered ? c.accent : c.textMuted, 1.5);
                    chevPen.setCapStyle(Qt::RoundCap);
                    p.setPen(chevPen);
                    p.drawLine(QPointF(cx - 3.0, cyMid - 1.2), QPointF(cx, cyMid + 2.0));
                    p.drawLine(QPointF(cx, cyMid + 2.0), QPointF(cx + 3.0, cyMid - 1.2));
                } else {
                    p.drawText(pill.rect, Qt::AlignCenter, pill.displayText);
                }
            }

            p.restore();
        };

        for (int l = 0; l < static_cast<int>(row.leftLines.size()); ++l) drawLine(row.leftLines[l], l);
        for (int l = 0; l < static_cast<int>(row.rightLines.size()); ++l) drawLine(row.rightLines[l], l);

        p.setFont(regFont);
        const bool hovAddC = (m_hover.kind == HitResult::Kind::AddCondition && m_hover.rowIndex == rIdx);
        p.setPen(hovAddC ? c.accent : c.textNumber);
        p.drawText(row.addCondRect, Qt::AlignVCenter | Qt::AlignLeft, UiFormat::Label("mod.addCondition"));

        const bool hovAddA = (m_hover.kind == HitResult::Kind::AddAction && m_hover.rowIndex == rIdx);
        p.setPen(hovAddA ? c.action : c.textNumber);
        p.drawText(row.addActRect, Qt::AlignVCenter | Qt::AlignLeft, UiFormat::Label("mod.addAction"));
    }

    if (m_dropTarget.valid) {
        p.setPen(QPen(c.accent, 2));
        if (m_dropTarget.nest) {
            QColor fill = c.accent;
            fill.setAlpha(42);
            p.setBrush(fill);
            p.drawRoundedRect(m_dropTarget.indicatorRect, 5, 5);

            const QString badge = UiFormat::Label("app.sheet.dropSubEvent");
            QFont bf = regFont;
            bf.setBold(true);
            bf.setPointSizeF(std::max(8.0, regFont.pointSizeF() - 1.0));
            const QFontMetrics bfm(bf);
            const int bw = bfm.horizontalAdvance(badge) + 14;
            const int bh = 18;
            const QRect br(m_dropTarget.indicatorRect.right() - bw - 6,
                           m_dropTarget.indicatorRect.top() + 4, bw, bh);
            p.setBrush(c.accent);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(br, 4, 4);
            p.setFont(bf);
            p.setPen(QColor(0x14, 0x16, 0x20));
            p.drawText(br, Qt::AlignCenter, badge);
        } else {
            p.drawLine(m_dropTarget.indicatorRect.topLeft(), m_dropTarget.indicatorRect.topRight());
            p.setBrush(c.accent);
            p.setPen(Qt::NoPen);
            p.drawEllipse(m_dropTarget.indicatorRect.topLeft(), 3, 3);
            p.drawEllipse(m_dropTarget.indicatorRect.topRight(), 3, 3);
        }
    }

    if (m_rows.empty()) {
        p.setFont(regFont);
        p.setPen(c.textMuted);
        p.drawText(QRect(0, HeaderH + 40, vp.width(), 40), Qt::AlignCenter,
                   m_sheet->rules.empty()
                       ? UiFormat::Label("app.sheet.emptyNoRules")
                       : UiFormat::Label("app.sheet.emptyFiltered"));
    }

    p.restore();

    const int sx = splitX(vp.width());
    p.fillRect(QRect(0, 0, vp.width(), HeaderH), c.panelBg);
    p.setPen(QPen(c.border, 1));
    p.drawLine(0, HeaderH - 1, vp.width(), HeaderH - 1);
    p.drawLine(OuterMarginX + NumW, 0, OuterMarginX + NumW, HeaderH - 1);
    p.drawLine(sx, 0, sx, HeaderH - 1);

    QFont hf = regFont;
    hf.setBold(true);
    p.setFont(hf);
    p.setPen(c.textMuted);
    p.drawText(QRect(OuterMarginX + 6, 0, NumW - 8, HeaderH), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("#"));
    p.drawText(QRect(OuterMarginX + NumW + 10, 0, sx - (OuterMarginX + NumW) - 12, HeaderH),
               Qt::AlignVCenter | Qt::AlignLeft,
               UiFormat::Label("app.sheet.colEvents"));
    p.drawText(QRect(sx + 10, 0, vp.width() - sx - 12, HeaderH),
               Qt::AlignVCenter | Qt::AlignLeft,
               UiFormat::Label("mod.pane.actions"));
}

void EventSheetView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    closeInlineEdit(true);
    rebuildLayout();
}

EventSheetView::HitResult EventSheetView::hitTest(const QPoint& vpPos) const {
    HitResult res;
    const int sx = splitX(viewport()->width());
    if (std::abs(vpPos.x() - sx) <= 4 && vpPos.y() <= HeaderH) {
        res.kind = HitResult::Kind::Splitter;
        return res;
    }
    if (vpPos.y() < HeaderH) return res;

    const QPoint pt = toContent(vpPos);

    for (int rIdx = 0; rIdx < static_cast<int>(m_rows.size()); ++rIdx) {
        const VisualRow& row = m_rows[rIdx];
        if (!row.cardRect.contains(pt)) continue;
        res.rowIndex = rIdx;

        if (row.isGroup) {
            if (row.foldRect.contains(pt)) { res.kind = HitResult::Kind::Fold; return res; }
            res.kind = HitResult::Kind::GroupHeader;
            return res;
        }
        if (row.numRect.contains(pt)) {
            res.kind = HitResult::Kind::Number;
            return res;
        }
        if (row.addCondRect.contains(pt)) {
            res.kind = HitResult::Kind::AddCondition;
            res.slot = UiFormat::Slot::Condition;
            return res;
        }
        if (row.addActRect.contains(pt)) {
            res.kind = HitResult::Kind::AddAction;
            res.slot = UiFormat::Slot::Action;
            return res;
        }

        auto checkLines = [&](const std::vector<VisualLine>& lines) -> bool {
            for (int l = 0; l < static_cast<int>(lines.size()); ++l) {
                if (!lines[l].rect.contains(pt)) continue;
                res.lineIndex = l;
                res.slot = lines[l].slot;
                for (int pi = 0; pi < static_cast<int>(lines[l].pills.size()); ++pi) {
                    if (lines[l].pills[pi].rect.contains(pt)) {
                        res.kind = HitResult::Kind::Pill;
                        res.pillIndex = pi;
                        return true;
                    }
                }
                res.kind = HitResult::Kind::Line;
                return true;
            }
            return false;
        };

        if (checkLines(row.leftLines)) return res;
        if (checkLines(row.rightLines)) return res;

        res.kind = HitResult::Kind::Number; // clicking empty card bg selects whole rule
        return res;
    }
    return res;
}

int EventSheetView::visualRowIndex(const std::vector<int>& path) const {
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (m_rows[i].path == path) return i;
    }
    return -1;
}

bool EventSheetView::isRulePicked(const std::vector<int>& path) const {
    if (!m_sel.valid() || m_sel.slot != UiFormat::Slot::Event) return false;
    if (m_sel.path == path) return true;
    return std::find(m_sel.selectedPaths.begin(), m_sel.selectedPaths.end(), path) != m_sel.selectedPaths.end();
}

bool EventSheetView::isLinePicked(const std::vector<int>& path, UiFormat::Slot slot, int index) const {
    if (slot == UiFormat::Slot::Event) return isRulePicked(path);
    if (m_sel.path != path || m_sel.slot != slot) return false;
    if (m_sel.index == index) return true;
    return std::find(m_sel.run.begin(), m_sel.run.end(), index) != m_sel.run.end();
}

void EventSheetView::pickRule(const std::vector<int>& path, bool shift, bool ctrl) {
    if (shift && m_sel.valid()) {
        const int aIdx = visualRowIndex(m_sel.path);
        const int bIdx = visualRowIndex(path);
        if (aIdx >= 0 && bIdx >= 0) {
            m_sel.slot = UiFormat::Slot::Event;
            m_sel.index = -1;
            m_sel.run.clear();
            m_sel.selectedPaths.clear();
            const int from = std::min(aIdx, bIdx);
            const int to = std::max(aIdx, bIdx);
            for (int r = from; r <= to; ++r) {
                m_sel.selectedPaths.push_back(m_rows[r].path);
            }
            viewport()->update();
            emit selectionChanged(m_sel);
            return;
        }
    }

    if (ctrl && m_sel.valid() && m_sel.slot == UiFormat::Slot::Event) {
        if (m_sel.selectedPaths.empty()) m_sel.selectedPaths.push_back(m_sel.path);
        auto it = std::find(m_sel.selectedPaths.begin(), m_sel.selectedPaths.end(), path);
        if (it != m_sel.selectedPaths.end()) {
            if (m_sel.selectedPaths.size() > 1) {
                m_sel.selectedPaths.erase(it);
                if (m_sel.path == path) m_sel.path = m_sel.selectedPaths.back();
            }
        } else {
            m_sel.selectedPaths.push_back(path);
            m_sel.path = path;
        }
        m_sel.index = -1;
        m_sel.run.clear();
        viewport()->update();
        emit selectionChanged(m_sel);
        return;
    }

    m_sel.path = path;
    m_sel.slot = UiFormat::Slot::Event;
    m_sel.index = -1;
    m_sel.run.clear();
    m_sel.selectedPaths = { path };
    viewport()->update();
    emit selectionChanged(m_sel);
}

void EventSheetView::pickLine(const std::vector<int>& path, UiFormat::Slot slot, int index, bool shift, bool ctrl) {
    if (slot == UiFormat::Slot::Event || index < 0) {
        pickRule(path, shift, ctrl);
        return;
    }

    // Shift-clicking a line in a different rule selects the range of rules between them
    if (shift && m_sel.valid() && path != m_sel.path) {
        pickRule(path, true, false);
        return;
    }

    const bool sameList = path == m_sel.path && slot == m_sel.slot && m_sel.index >= 0;
    if (shift && sameList) {
        m_sel.run.clear();
        m_sel.selectedPaths.clear();
        const int from = std::min(m_sel.index, index);
        const int to = std::max(m_sel.index, index);
        for (int at = from; at <= to; ++at) {
            if (at != m_sel.index) m_sel.run.push_back(at);
        }
    } else if (ctrl && sameList) {
        m_sel.selectedPaths.clear();
        if (index == m_sel.index) {
            if (!m_sel.run.empty()) {
                m_sel.index = m_sel.run.back();
                m_sel.run.pop_back();
            }
        } else {
            auto it = std::find(m_sel.run.begin(), m_sel.run.end(), index);
            if (it != m_sel.run.end()) m_sel.run.erase(it);
            else m_sel.run.push_back(index);
        }
    } else {
        m_sel.path = path;
        m_sel.slot = slot;
        m_sel.index = index;
        m_sel.run.clear();
        m_sel.selectedPaths.clear();
    }
    viewport()->update();
    emit selectionChanged(m_sel);
}

std::vector<int> EventSheetView::pickedIndices() const {
    std::vector<int> out = m_sel.run;
    if (m_sel.index >= 0) out.push_back(m_sel.index);
    std::sort(out.begin(), out.end(), std::greater<int>());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<int> EventSheetView::pickedIndicesAsc() const {
    std::vector<int> out = m_sel.run;
    if (m_sel.index >= 0) out.push_back(m_sel.index);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::vector<int>> EventSheetView::pickedRulePaths() const {
    if (!m_sel.valid()) return {};
    std::vector<std::vector<int>> raw = m_sel.selectedPaths;
    if (raw.empty()) raw.push_back(m_sel.path);

    // Return in visual row order where visible, appending any collapsed paths afterwards
    std::vector<std::vector<int>> ordered;
    for (const VisualRow& row : m_rows) {
        if (std::find(raw.begin(), raw.end(), row.path) != raw.end()) {
            ordered.push_back(row.path);
        }
    }
    for (const auto& p : raw) {
        if (std::find(ordered.begin(), ordered.end(), p) == ordered.end()) {
            ordered.push_back(p);
        }
    }
    return ordered;
}

std::vector<std::vector<int>> EventSheetView::topLevelPickedRulePaths() const {
    const std::vector<std::vector<int>> all = pickedRulePaths();
    std::vector<std::vector<int>> out;
    for (const auto& p : all) {
        bool covered = false;
        for (const auto& q : all) {
            if (q != p && IsInsidePath(q, p)) { covered = true; break; }
        }
        if (!covered) out.push_back(p);
    }
    return out;
}

void EventSheetView::mousePressEvent(QMouseEvent* event) {
    closeInlineEdit(true);
    setFocus();

    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    const HitResult hit = hitTest(event->pos());
    if (hit.kind == HitResult::Kind::Splitter) {
        m_draggingSplitter = true;
        return;
    }
    if (hit.rowIndex < 0 || hit.rowIndex >= static_cast<int>(m_rows.size())) return;

    const VisualRow& row = m_rows[hit.rowIndex];
    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
    const bool ctrl = (event->modifiers() & Qt::ControlModifier) != 0;
    m_collapseSelectionOnRelease = false;

    if (hit.kind == HitResult::Kind::Fold) {
        if (ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, row.path)) {
            r->collapsed = !r->collapsed;
            rebuildLayout();
            viewport()->update();
        }
        return;
    }

    if (hit.kind == HitResult::Kind::AddCondition) {
        pickRule(row.path, false, false);
        emit requestAdd(row.path, UiFormat::Slot::Condition);
        return;
    }
    if (hit.kind == HitResult::Kind::AddAction) {
        pickRule(row.path, false, false);
        emit requestAdd(row.path, UiFormat::Slot::Action);
        return;
    }

    if (hit.kind == HitResult::Kind::Pill) {
        const auto& lines = (hit.slot == UiFormat::Slot::Action) ? row.rightLines : row.leftLines;
        const VisualLine& vl = lines[hit.lineIndex];
        pickLine(row.path, vl.slot, vl.index, false, false);
        startInlineEdit(row, vl, vl.pills[hit.pillIndex]);
        return;
    }

    if (hit.kind == HitResult::Kind::Line) {
        const auto& lines = (hit.slot == UiFormat::Slot::Action) ? row.rightLines : row.leftLines;
        const VisualLine& vl = lines[hit.lineIndex];

        if (!shift && !ctrl && isLinePicked(row.path, vl.slot, vl.index) && m_sel.count() > 1) {
            m_collapseSelectionOnRelease = true;
            m_pendingClickPath = row.path;
            m_pendingClickSlot = vl.slot;
            m_pendingClickIndex = vl.index;
        } else {
            pickLine(row.path, vl.slot, vl.index, shift, ctrl);
        }

        m_dragPending = true;
        m_dragStartPos = event->pos();
        if (m_sel.slot == UiFormat::Slot::Event) {
            m_dragSource = { row.path, 0, {}, topLevelPickedRulePaths() };
        } else {
            m_dragSource = {
                row.path,
                m_sel.slot == UiFormat::Slot::Condition ? 1 : 2,
                pickedIndicesAsc(),
                {}
            };
        }
        return;
    }

    // Number, GroupHeader, or empty rule card background
    const QPoint pt = toContent(event->pos());
    if (!row.isGroup && !row.numRect.contains(pt) && !shift && !ctrl &&
        m_sel.path == row.path && m_sel.slot != UiFormat::Slot::Event) {
        // Clicking empty card space in the active rule keeps its selected condition/action focused
        viewport()->update();
        return;
    }
    if (!shift && !ctrl && isRulePicked(row.path) && m_sel.count() > 1) {
        m_collapseSelectionOnRelease = true;
        m_pendingClickPath = row.path;
        m_pendingClickSlot = UiFormat::Slot::Event;
        m_pendingClickIndex = -1;
    } else {
        pickRule(row.path, shift, ctrl);
    }
    m_dragPending = true;
    m_dragStartPos = event->pos();
    m_dragSource = { row.path, 0, {}, topLevelPickedRulePaths() };
}

void EventSheetView::mouseMoveEvent(QMouseEvent* event) {
    if (m_draggingSplitter) {
        const int avail = std::max(320, viewport()->width() - OuterMarginX * 2 - NumW);
        const int rel = event->pos().x() - (OuterMarginX + NumW);
        m_splitRatio = std::clamp(static_cast<double>(rel) / avail, 0.25, 0.72);
        rebuildLayout();
        viewport()->update();
        return;
    }

    if (m_dragPending && (event->buttons() & Qt::LeftButton)) {
        if ((event->pos() - m_dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_dragPending = false;
            m_collapseSelectionOnRelease = false;
            auto* drag = new QDrag(this);
            auto* mime = new QMimeData();
            mime->setData(QString::fromLatin1(MimeType),
                          EncodeDrag(m_dragSource.path, m_dragSource.slot, m_dragSource.indices, m_dragSource.paths));
            drag->setMimeData(mime);
            drag->exec(Qt::MoveAction);
            m_dropTarget = {};
            viewport()->update();
            return;
        }
    }

    const HitResult hit = hitTest(event->pos());
    if (hit.kind == HitResult::Kind::Splitter) {
        viewport()->setCursor(Qt::SplitHCursor);
    } else if (hit.kind == HitResult::Kind::Pill || hit.kind == HitResult::Kind::Fold ||
               hit.kind == HitResult::Kind::AddCondition || hit.kind == HitResult::Kind::AddAction) {
        viewport()->setCursor(Qt::PointingHandCursor);
    } else {
        viewport()->setCursor(Qt::ArrowCursor);
    }

    if (hit.kind != m_hover.kind || hit.rowIndex != m_hover.rowIndex ||
        hit.lineIndex != m_hover.lineIndex || hit.pillIndex != m_hover.pillIndex || hit.slot != m_hover.slot) {
        m_hover = hit;
        viewport()->update();
    }
}

void EventSheetView::mouseReleaseEvent(QMouseEvent* event) {
    m_draggingSplitter = false;
    m_dragPending = false;
    if (m_collapseSelectionOnRelease && event->button() == Qt::LeftButton) {
        m_collapseSelectionOnRelease = false;
        pickLine(m_pendingClickPath, m_pendingClickSlot, m_pendingClickIndex, false, false);
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void EventSheetView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !m_sheet) return;
    const HitResult hit = hitTest(event->pos());
    if (hit.rowIndex < 0 || hit.rowIndex >= static_cast<int>(m_rows.size())) return;

    const VisualRow& row = m_rows[hit.rowIndex];
    if (hit.kind == HitResult::Kind::GroupHeader) {
        if (ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, row.path)) {
            r->collapsed = !r->collapsed;
            rebuildLayout();
            viewport()->update();
        }
        return;
    }
    if (hit.kind == HitResult::Kind::Line) {
        emit requestFocusInspector();
    }
}

bool EventSheetView::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(event);
        const HitResult hit = hitTest(he->pos());
        if (hit.kind == HitResult::Kind::Pill && hit.rowIndex >= 0) {
            const VisualRow& row = m_rows[hit.rowIndex];
            const auto& lines = (hit.slot == UiFormat::Slot::Action) ? row.rightLines : row.leftLines;
            const PillHit& pill = lines[hit.lineIndex].pills[hit.pillIndex];
            if (pill.isAddParam) {
                QToolTip::showText(he->globalPos(), QStringLiteral("+ ") + pill.paramLabel, viewport());
                return true;
            }
            const QString tip = QStringLiteral("<b>%1</b> (%2)<br><code>%3</code><br><span style='color:#8c93a8'>%4</span>")
                .arg(pill.paramLabel.toHtmlEscaped(),
                     QString::fromStdString(pill.paramKey).toHtmlEscaped(),
                     QString::fromStdString(pill.rawValue).toHtmlEscaped(),
                     UiFormat::Label("app.sheet.pillHint").toHtmlEscaped());
            QToolTip::showText(he->globalPos(), tip, viewport());
            return true;
        }
        if (hit.kind == HitResult::Kind::Line && hit.rowIndex >= 0) {
            const VisualRow& row = m_rows[hit.rowIndex];
            const auto& lines = (hit.slot == UiFormat::Slot::Action) ? row.rightLines : row.leftLines;
            if (hit.lineIndex >= 0 && hit.lineIndex < static_cast<int>(lines.size())) {
                const VisualLine& vl = lines[hit.lineIndex];
                if (!vl.warnKey.empty()) {
                    QString warnMsg = UiFormat::Label(vl.warnKey);
                    if (vl.warnKey == "app.problems.nestedEvent") {
                        warnMsg = warnMsg.arg(QString::fromStdString(vl.entryId));
                    }
                    QToolTip::showText(he->globalPos(),
                                       QStringLiteral("<span style='color:#f0c36d'>⚠ %1</span>").arg(warnMsg.toHtmlEscaped()),
                                       viewport());
                    return true;
                }
            }
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void EventSheetView::startInlineEdit(const VisualRow& row, const VisualLine& line, const PillHit& pill) {
    closeInlineEdit(false);
    if (!m_sheet) return;

    const auto* defs = UiFormat::ParamsOf(line.slot, line.entryId);
    if (pill.isAddParam) {
        if (!defs) return;
        ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, row.path);
        if (!rule) return;
        std::vector<ModLogic::Param>* args = nullptr;
        if (line.slot == UiFormat::Slot::Event) args = &rule->filters;
        else if (line.slot == UiFormat::Slot::Condition && line.index >= 0 && line.index < static_cast<int>(rule->conditions.size()))
            args = &rule->conditions[line.index].args;
        else if (line.slot == UiFormat::Slot::Action && line.index >= 0 && line.index < static_cast<int>(rule->actions.size()))
            args = &rule->actions[line.index].args;
        if (!args) return;

        QMenu menu(viewport());
        for (const ModCatalog::ParamDef& d : *defs) {
            if (!ModCatalog::CanAddOptionalParam(d, *defs, *args)) continue;
            const std::string effKey = ModCatalog::EffectiveParamLabelKey(d, *defs, *args);
            const QString lbl = effKey.empty() ? QString::fromStdString(d.key) : UiFormat::Label(effKey);
            QAction* act = menu.addAction(QStringLiteral("%1 (%2)").arg(lbl, QString::fromStdString(d.key)));
            act->setData(QString::fromStdString(d.key));
        }
        if (menu.isEmpty()) return;
        const std::vector<int> savedPath = row.path;
        const UiFormat::Slot savedSlot = line.slot;
        const int savedIdx = line.index;
        QAction* chosen = menu.exec(viewport()->mapToGlobal(fromContent(pill.rect.bottomLeft()) + QPoint(0, 2)));
        if (chosen && m_sheet) {
            const std::string addKey = chosen->data().toString().toStdString();
            std::string addPreset;
            for (const ModCatalog::ParamDef& d : *defs) {
                if (d.key == addKey) { addPreset = d.preset; break; }
            }
            if (ModLogic::Rule* r2 = UiFormat::RuleAt(*m_sheet, savedPath)) {
                std::vector<ModLogic::Param>* args2 = nullptr;
                if (savedSlot == UiFormat::Slot::Event) args2 = &r2->filters;
                else if (savedSlot == UiFormat::Slot::Condition && savedIdx >= 0 && savedIdx < static_cast<int>(r2->conditions.size()))
                    args2 = &r2->conditions[savedIdx].args;
                else if (savedSlot == UiFormat::Slot::Action && savedIdx >= 0 && savedIdx < static_cast<int>(r2->actions.size()))
                    args2 = &r2->actions[savedIdx].args;
                if (args2) {
                    emit aboutToEdit();
                    ModLogic::ParamValue(*args2, addKey) = addPreset;
                    rebuildLayout();
                    viewport()->update();
                    emit sheetEdited();
                    emit selectionChanged(m_sel);
                }
            }
        }
        return;
    }

    const ModCatalog::ParamDef* pdef = nullptr;
    if (defs) {
        for (const auto& d : *defs) {
            if (d.key == pill.paramKey) { pdef = &d; break; }
        }
    }

    // Multiline fragments edit best in the right Inspector
    if (pdef && pdef->kind == "fragment") {
        emit requestFocusInspector();
        return;
    }

    m_inlinePath = row.path;
    m_inlineSlot = line.slot;
    m_inlineIndex = line.index;
    m_inlineParamKey = pill.paramKey;

    const QPoint topLeft = fromContent(pill.rect.topLeft());
    const int minW = (pdef && !pdef->options.empty()) ? 160 : 88;
    const int prefW = std::clamp(std::max(pill.rect.width() + 28, minW), minW, 340);
    const int x = std::min(topLeft.x(), std::max(4, viewport()->width() - prefW - 8));
    const QRect edRect(x, topLeft.y() - 2, prefW, pill.rect.height() + 4);

    if (pdef && !pdef->options.empty() && pdef->kind == "enum" && !pill.isExpr) {
        QMenu menu(viewport());
        for (const auto& opt : pdef->options) {
            QAction* act = menu.addAction(UiFormat::OptionLabel(*pdef, opt.value));
            act->setCheckable(true);
            act->setChecked(opt.value == pill.rawValue);
            act->setData(QString::fromStdString(opt.value));
        }
        QAction* chosen = menu.exec(viewport()->mapToGlobal(fromContent(pill.rect.bottomLeft()) + QPoint(0, 2)));
        if (chosen && m_sheet) {
            const std::string newVal = chosen->data().toString().toStdString();
            if (ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, m_inlinePath)) {
                std::vector<ModLogic::Param>* args = nullptr;
                if (m_inlineSlot == UiFormat::Slot::Event) args = &rule->filters;
                else if (m_inlineSlot == UiFormat::Slot::Condition &&
                         m_inlineIndex >= 0 && m_inlineIndex < static_cast<int>(rule->conditions.size()))
                    args = &rule->conditions[m_inlineIndex].args;
                else if (m_inlineSlot == UiFormat::Slot::Action &&
                         m_inlineIndex >= 0 && m_inlineIndex < static_cast<int>(rule->actions.size()))
                    args = &rule->actions[m_inlineIndex].args;
                if (args) {
                    std::string& slot = ModLogic::ParamValue(*args, m_inlineParamKey);
                    if (slot != newVal) {
                        emit aboutToEdit();
                        slot = newVal;
                        rebuildLayout();
                        viewport()->update();
                        emit sheetEdited();
                        emit selectionChanged(m_sel);
                    }
                }
            }
        }
        return;
    }

    if (pdef && !pdef->options.empty()) {
        auto* combo = new QComboBox(viewport());
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->setFont(Theme::MonoFont(9));
        for (const auto& opt : pdef->options) {
            const QString vq = QString::fromStdString(opt.value);
            combo->addItem(UiFormat::OptionLabel(*pdef, opt.value), vq);
        }
        const int idx = combo->findData(QString::fromStdString(pill.rawValue));
        if (idx >= 0) combo->setCurrentIndex(idx);
        combo->setEditText(QString::fromStdString(pill.rawValue));
        combo->setGeometry(edRect);
        combo->show();
        combo->setFocus();
        if (combo->lineEdit()) combo->lineEdit()->selectAll();

        connect(combo, qOverload<int>(&QComboBox::activated), this, [this, combo](int i) {
            combo->setEditText(combo->itemData(i).toString());
            closeInlineEdit(true);
        });
        if (combo->lineEdit()) {
            connect(combo->lineEdit(), &QLineEdit::returnPressed, this, [this]() { closeInlineEdit(true); });
        }
        m_inlineEditor = combo;
    } else {
        auto* edit = new QLineEdit(QString::fromStdString(pill.rawValue), viewport());
        edit->setFont(Theme::MonoFont(9));
        edit->setStyleSheet(QStringLiteral(
            "QLineEdit { background: #141720; color: #ffffff; border: 1px solid #9b87f5; border-radius: 4px; padding: 1px 6px; }"
        ));
        edit->setGeometry(edRect);
        edit->show();
        edit->setFocus();
        edit->selectAll();
        connect(edit, &QLineEdit::returnPressed, this, [this]() { closeInlineEdit(true); });
        connect(edit, &QLineEdit::editingFinished, this, [this]() { closeInlineEdit(true); });
        m_inlineEditor = edit;
    }
}

void EventSheetView::closeInlineEdit(bool commit) {
    if (!m_inlineEditor) return;
    QWidget* ed = m_inlineEditor;
    m_inlineEditor = nullptr;

    if (commit && m_sheet) {
        std::string newVal;
        if (auto* le = qobject_cast<QLineEdit*>(ed)) newVal = le->text().toStdString();
        else if (auto* cb = qobject_cast<QComboBox*>(ed)) newVal = cb->currentText().toStdString();

        if (ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, m_inlinePath)) {
            std::vector<ModLogic::Param>* args = nullptr;
            if (m_inlineSlot == UiFormat::Slot::Event) args = &rule->filters;
            else if (m_inlineSlot == UiFormat::Slot::Condition &&
                     m_inlineIndex >= 0 && m_inlineIndex < static_cast<int>(rule->conditions.size()))
                args = &rule->conditions[m_inlineIndex].args;
            else if (m_inlineSlot == UiFormat::Slot::Action &&
                     m_inlineIndex >= 0 && m_inlineIndex < static_cast<int>(rule->actions.size()))
                args = &rule->actions[m_inlineIndex].args;

            if (args) {
                std::string& slot = ModLogic::ParamValue(*args, m_inlineParamKey);
                if (slot != newVal) {
                    emit aboutToEdit();
                    slot = newVal;
                    rebuildLayout();
                    viewport()->update();
                    emit sheetEdited();
                    emit selectionChanged(m_sel);
                }
            }
        }
    }

    const bool hadFocus = ed->hasFocus() || ed->isAncestorOf(QApplication::focusWidget());
    ed->hide();
    ed->deleteLater();
    if (hadFocus) setFocus();
}

void EventSheetView::renameGroupDialog(const std::vector<int>& path) {
    if (!m_sheet) return;
    ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, path);
    if (!rule || !rule->group) return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        UiFormat::Label("mod.renameGroup"),
        UiFormat::Label("app.dlg.newGroupPrompt"),
        QLineEdit::Normal,
        QString::fromStdString(rule->name),
        &ok);
    if (!ok) return;
    emit aboutToEdit();
    rule->name = name.toStdString();
    rebuildLayout();
    viewport()->update();
    emit sheetEdited();
}

void EventSheetView::contextMenuEvent(QContextMenuEvent* event) {
    closeInlineEdit(true);
    if (!m_sheet) return;

    const HitResult hit = hitTest(viewport()->mapFromParent(event->pos()));
    if (hit.rowIndex < 0 || hit.rowIndex >= static_cast<int>(m_rows.size())) return;

    const VisualRow& row = m_rows[hit.rowIndex];
    ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, row.path);
    if (!rule) return;

    QMenu menu(this);

    const QString copyLabel = UiFormat::Label("common.copy") + QStringLiteral(" (Ctrl+C)");
    const QString pasteLabel = UiFormat::Label("common.paste") + QStringLiteral(" (Ctrl+V)");
    const QString delRuleLabel = UiFormat::Label("app.sheet.deleteRule") + QStringLiteral(" (Del)");
    const QString delCondLabel = UiFormat::Label("app.sheet.deleteCond") + QStringLiteral(" (Del)");
    const QString delActLabel = UiFormat::Label("app.sheet.deleteAct") + QStringLiteral(" (Del)");
    const QString enLabel = UiFormat::Label("mod.rule.enabled") + QStringLiteral(" (D)");
    const QString invLabel = UiFormat::Label("mod.invert") + QStringLiteral(" (I)");

    if (row.isGroup) {
        if (!isRulePicked(row.path)) {
            pickRule(row.path, false, false);
        }
        menu.addAction(UiFormat::Label("mod.renameGroup"), this, [this, p = row.path]() {
            renameGroupDialog(p);
        });
        menu.addAction(rule->collapsed ? UiFormat::Label("app.sheet.expand")
                                       : UiFormat::Label("app.sheet.collapse"),
                       this, [this, p = row.path]() {
            if (ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, p)) {
                r->collapsed = !r->collapsed;
                rebuildLayout();
                viewport()->update();
            }
        });
        QAction* en = menu.addAction(enLabel);
        en->setCheckable(true);
        en->setChecked(rule->enabled);
        connect(en, &QAction::triggered, this, &EventSheetView::toggleEnabled);
        menu.addSeparator();
        menu.addAction(copyLabel, this, &EventSheetView::copySelected);
        if (m_hasClip && m_clipSlot == UiFormat::Slot::Event) {
            menu.addAction(pasteLabel, this, &EventSheetView::pasteClipboard);
        }
        menu.addAction(UiFormat::Label("common.delete") + QStringLiteral(" (Del)"),
                       this, &EventSheetView::deleteSelected);
        menu.exec(event->globalPos());
        return;
    }

    if (hit.kind == HitResult::Kind::Line || hit.kind == HitResult::Kind::Pill) {
        const auto& lines = (hit.slot == UiFormat::Slot::Action) ? row.rightLines : row.leftLines;
        const VisualLine& vl = lines[hit.lineIndex];
        if (!isLinePicked(row.path, vl.slot, vl.index)) {
            pickLine(row.path, vl.slot, vl.index, false, false);
        }

        const auto* lineDefs = UiFormat::ParamsOf(vl.slot, vl.entryId);
        std::vector<ModLogic::Param>* lineArgs = nullptr;
        if (vl.slot == UiFormat::Slot::Event) lineArgs = &rule->filters;
        else if (vl.slot == UiFormat::Slot::Condition && vl.index >= 0 && vl.index < static_cast<int>(rule->conditions.size()))
            lineArgs = &rule->conditions[vl.index].args;
        else if (vl.slot == UiFormat::Slot::Action && vl.index >= 0 && vl.index < static_cast<int>(rule->actions.size()))
            lineArgs = &rule->actions[vl.index].args;

        bool addedParamMenu = false;
        if (hit.kind == HitResult::Kind::Pill && hit.pillIndex >= 0 && hit.pillIndex < static_cast<int>(vl.pills.size())) {
            const PillHit& ph = vl.pills[hit.pillIndex];
            if (ph.canRemove) {
                const std::string remKey = ph.paramKey;
                menu.addAction(UiFormat::Label("mod.removeParam") + QStringLiteral(": ") + ph.paramLabel,
                               this, [this, p = row.path, sl = vl.slot, idx = vl.index, remKey]() {
                    if (!m_sheet) return;
                    ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, p);
                    if (!r) return;
                    std::vector<ModLogic::Param>* a = nullptr;
                    if (sl == UiFormat::Slot::Event) a = &r->filters;
                    else if (sl == UiFormat::Slot::Condition && idx >= 0 && idx < static_cast<int>(r->conditions.size()))
                        a = &r->conditions[idx].args;
                    else if (sl == UiFormat::Slot::Action && idx >= 0 && idx < static_cast<int>(r->actions.size()))
                        a = &r->actions[idx].args;
                    if (a) {
                        emit aboutToEdit();
                        if (ModLogic::RemoveParam(*a, remKey)) {
                            rebuildLayout();
                            viewport()->update();
                            emit sheetEdited();
                            emit selectionChanged(m_sel);
                        }
                    }
                });
                addedParamMenu = true;
            }
        }
        if (lineDefs && lineArgs && ModCatalog::HasAddableOptionalParams(*lineDefs, *lineArgs)) {
            QMenu* addSub = menu.addMenu(QStringLiteral("+ ") + UiFormat::Label("mod.addParam"));
            for (const ModCatalog::ParamDef& d : *lineDefs) {
                if (!ModCatalog::CanAddOptionalParam(d, *lineDefs, *lineArgs)) continue;
                const std::string effKey = ModCatalog::EffectiveParamLabelKey(d, *lineDefs, *lineArgs);
                const QString lbl = effKey.empty() ? QString::fromStdString(d.key) : UiFormat::Label(effKey);
                const std::string addKey = d.key;
                const std::string addPreset = d.preset;
                addSub->addAction(QStringLiteral("%1 (%2)").arg(lbl, QString::fromStdString(d.key)),
                                  this, [this, p = row.path, sl = vl.slot, idx = vl.index, addKey, addPreset]() {
                    if (!m_sheet) return;
                    ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, p);
                    if (!r) return;
                    std::vector<ModLogic::Param>* a = nullptr;
                    if (sl == UiFormat::Slot::Event) a = &r->filters;
                    else if (sl == UiFormat::Slot::Condition && idx >= 0 && idx < static_cast<int>(r->conditions.size()))
                        a = &r->conditions[idx].args;
                    else if (sl == UiFormat::Slot::Action && idx >= 0 && idx < static_cast<int>(r->actions.size()))
                        a = &r->actions[idx].args;
                    if (a) {
                        emit aboutToEdit();
                        ModLogic::ParamValue(*a, addKey) = addPreset;
                        rebuildLayout();
                        viewport()->update();
                        emit sheetEdited();
                        emit selectionChanged(m_sel);
                    }
                });
            }
            addedParamMenu = true;
        }
        if (addedParamMenu && vl.slot != UiFormat::Slot::Event) menu.addSeparator();

        if (vl.slot == UiFormat::Slot::Condition) {
            QAction* inv = menu.addAction(invLabel);
            inv->setCheckable(true);
            inv->setChecked(rule->conditions[vl.index].inverted);
            connect(inv, &QAction::triggered, this, &EventSheetView::toggleInverted);

            QAction* en = menu.addAction(enLabel);
            en->setCheckable(true);
            en->setChecked(rule->conditions[vl.index].enabled);
            connect(en, &QAction::triggered, this, &EventSheetView::toggleEnabled);

            menu.addAction(UiFormat::Label("mod.replace"), this, [this, p = row.path, idx = vl.index]() {
                emit requestReplace(p, UiFormat::Slot::Condition, idx);
            });
            menu.addAction(UiFormat::Label("app.sheet.moveCondToSubEvent"),
                           this, &EventSheetView::moveConditionsToSubEvent);
            menu.addSeparator();
            menu.addAction(copyLabel, this, &EventSheetView::copySelected);
            if (m_hasClip) menu.addAction(pasteLabel, this, &EventSheetView::pasteClipboard);
            menu.addAction(delCondLabel, this, &EventSheetView::deleteSelected);
            menu.exec(event->globalPos());
            return;
        }

        if (vl.slot == UiFormat::Slot::Action) {
            QAction* en = menu.addAction(enLabel);
            en->setCheckable(true);
            en->setChecked(rule->actions[vl.index].enabled);
            connect(en, &QAction::triggered, this, &EventSheetView::toggleEnabled);

            menu.addAction(UiFormat::Label("mod.replace"), this, [this, p = row.path, idx = vl.index]() {
                emit requestReplace(p, UiFormat::Slot::Action, idx);
            });
            menu.addSeparator();
            menu.addAction(copyLabel, this, &EventSheetView::copySelected);
            if (m_hasClip) menu.addAction(pasteLabel, this, &EventSheetView::pasteClipboard);
            menu.addAction(delActLabel, this, &EventSheetView::deleteSelected);
            menu.exec(event->globalPos());
            return;
        }
    }

    bool insideNonGroupRule = false;
    for (size_t d = 1; d < row.path.size(); ++d) {
        std::vector<int> head(row.path.begin(), row.path.begin() + static_cast<ptrdiff_t>(d));
        if (const ModLogic::Rule* anc = UiFormat::RuleAt(*m_sheet, head)) {
            if (!anc->group) { insideNonGroupRule = true; break; }
        }
    }

    if (!isRulePicked(row.path)) {
        pickRule(row.path, false, false);
    }
    if (!insideNonGroupRule) {
        menu.addAction(UiFormat::Label("app.sheet.replaceEvent"), this, [this, p = row.path]() {
            emit requestReplace(p, UiFormat::Slot::Event, -1);
        });
    }
    if (!rule->event.empty() &&
        (insideNonGroupRule || !rule->conditions.empty() || !rule->actions.empty() || !rule->children.empty())) {
        menu.addAction(UiFormat::Label("app.sheet.clearEvent"),
                       this, [this, p = row.path]() {
            if (!m_sheet) return;
            if (ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, p)) {
                emit aboutToEdit();
                r->event.clear();
                r->filters.clear();
                r->veto = false;
                rebuildLayout();
                viewport()->update();
                emit sheetEdited();
            }
        });
    }
    menu.addAction(UiFormat::Label("mod.addCondition"), this, [this, p = row.path]() {
        emit requestAdd(p, UiFormat::Slot::Condition);
    });
    menu.addAction(UiFormat::Label("mod.addAction"), this, [this, p = row.path]() {
        emit requestAdd(p, UiFormat::Slot::Action);
    });
    menu.addAction(UiFormat::Label("mod.addSubEvent") + QStringLiteral(" (X)"),
                   this, &EventSheetView::addSubEvent);

    int onceIdx = -1;
    for (int c = 0; c < static_cast<int>(rule->conditions.size()); ++c) {
        if (rule->conditions[c].id == TriggerOnce) { onceIdx = c; break; }
    }

    QAction* onceAct = menu.addAction(UiFormat::Label("mod.cond.triggerOnce"));
    onceAct->setCheckable(true);
    onceAct->setChecked(onceIdx >= 0);
    connect(onceAct, &QAction::triggered, this, [this, p = row.path, onceIdx]() {
        if (!m_sheet) return;
        ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, p);
        if (!r) return;
        emit aboutToEdit();
        if (onceIdx >= 0 && onceIdx < static_cast<int>(r->conditions.size()))
            r->conditions.erase(r->conditions.begin() + onceIdx);
        else
            r->conditions.push_back(ModLogic::MakeCondition(TriggerOnce));
        rebuildLayout();
        viewport()->update();
        emit sheetEdited();
    });

    if (const auto* evDef = ModCatalog::FindEvent(rule->event); evDef && evDef->canVeto) {
        QAction* veto = menu.addAction(UiFormat::Label("mod.rule.veto"));
        veto->setCheckable(true);
        veto->setChecked(rule->veto);
        connect(veto, &QAction::triggered, this, [this, p = row.path]() {
            if (!m_sheet) return;
            if (ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, p)) {
                emit aboutToEdit();
                r->veto = !r->veto;
                rebuildLayout();
                viewport()->update();
                emit sheetEdited();
            }
        });
    }

    QAction* en = menu.addAction(enLabel);
    en->setCheckable(true);
    en->setChecked(rule->enabled);
    connect(en, &QAction::triggered, this, &EventSheetView::toggleEnabled);

    menu.addSeparator();
    menu.addAction(copyLabel, this, &EventSheetView::copySelected);
    if (m_hasClip) menu.addAction(pasteLabel, this, &EventSheetView::pasteClipboard);
    menu.addAction(delRuleLabel, this, &EventSheetView::deleteSelected);
    menu.exec(event->globalPos());
}

void EventSheetView::keyPressEvent(QKeyEvent* event) {
    if (m_inlineEditor) {
        if (event->key() == Qt::Key_Escape) {
            closeInlineEdit(false);
            return;
        }
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    const auto mods = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (mods == Qt::NoModifier) {
        if (event->key() == Qt::Key_D) { toggleEnabled(); return; }
        if (event->key() == Qt::Key_I) { toggleInverted(); return; }
        if (event->key() == Qt::Key_X) { addSubEvent(); return; }
        if (event->key() == Qt::Key_Delete) { deleteSelected(); return; }
        if (event->key() == Qt::Key_Backspace && m_sel.valid() && m_sel.slot != UiFormat::Slot::Event) {
            deleteSelected();
            return;
        }
        if (event->key() == Qt::Key_Escape && m_sel.valid()) {
            if (m_sel.count() > 1) {
                pickLine(m_sel.path, m_sel.slot, m_sel.index, false, false);
            } else {
                setSelection({}, true);
            }
            return;
        }
    }
    if (mods & Qt::ControlModifier) {
        if (event->key() == Qt::Key_C) { copySelected(); return; }
        if (event->key() == Qt::Key_V) { pasteClipboard(); return; }
        if (event->key() == Qt::Key_A && !m_rows.empty()) {
            UiFormat::Selection all;
            all.path = m_rows.front().path;
            all.slot = UiFormat::Slot::Event;
            all.index = -1;
            for (const VisualRow& row : m_rows) all.selectedPaths.push_back(row.path);
            setSelection(all, true);
            return;
        }
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void EventSheetView::toggleEnabled() {
    if (!m_sheet || !m_sel.valid()) return;
    ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, m_sel.path);
    if (!rule) return;

    emit aboutToEdit();
    if (m_sel.slot == UiFormat::Slot::Event || m_sel.index < 0) {
        const bool on = rule->enabled;
        for (const auto& rp : pickedRulePaths()) {
            if (ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, rp)) {
                r->enabled = !on;
            }
        }
    } else {
        const std::vector<int> lines = pickedIndices();
        const bool on = m_sel.slot == UiFormat::Slot::Condition
            ? (m_sel.index < static_cast<int>(rule->conditions.size()) && rule->conditions[m_sel.index].enabled)
            : (m_sel.index < static_cast<int>(rule->actions.size()) && rule->actions[m_sel.index].enabled);
        for (int at : lines) {
            if (m_sel.slot == UiFormat::Slot::Condition && at < static_cast<int>(rule->conditions.size()))
                rule->conditions[at].enabled = !on;
            if (m_sel.slot == UiFormat::Slot::Action && at < static_cast<int>(rule->actions.size()))
                rule->actions[at].enabled = !on;
        }
    }
    rebuildLayout();
    viewport()->update();
    emit sheetEdited();
}

void EventSheetView::toggleInverted() {
    if (!m_sheet || !m_sel.valid() || m_sel.slot != UiFormat::Slot::Condition) return;
    ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, m_sel.path);
    if (!rule || m_sel.index < 0 || m_sel.index >= static_cast<int>(rule->conditions.size())) return;

    emit aboutToEdit();
    const bool inv = rule->conditions[m_sel.index].inverted;
    for (int at : pickedIndices()) {
        if (at >= 0 && at < static_cast<int>(rule->conditions.size())) {
            rule->conditions[at].inverted = !inv;
        }
    }
    rebuildLayout();
    viewport()->update();
    emit sheetEdited();
}

void EventSheetView::addSubEvent() {
    if (!m_sheet || !m_sel.valid()) return;
    ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, m_sel.path);
    if (!rule || rule->group) return;

    emit aboutToEdit();
    rule->children.push_back(ModLogic::Rule{});
    std::vector<int> childPath = m_sel.path;
    childPath.push_back(static_cast<int>(rule->children.size()) - 1);
    rebuildLayout();
    setSelection({ childPath, UiFormat::Slot::Event, -1, {}, { childPath } }, true);
    emit sheetEdited();
}

void EventSheetView::moveConditionsToSubEvent() {
    if (!m_sheet || !m_sel.valid() || m_sel.slot != UiFormat::Slot::Condition) return;
    ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, m_sel.path);
    if (!rule || rule->group) return;

    const std::vector<int> asc = pickedIndicesAsc();
    std::vector<ModLogic::Condition> moved;
    for (int idx : asc) {
        if (idx >= 0 && idx < static_cast<int>(rule->conditions.size())) {
            moved.push_back(rule->conditions[idx]);
        }
    }
    if (moved.empty()) return;

    emit aboutToEdit();
    for (int idx : pickedIndices()) {
        if (idx >= 0 && idx < static_cast<int>(rule->conditions.size())) {
            rule->conditions.erase(rule->conditions.begin() + idx);
        }
    }
    ModLogic::Rule child;
    child.conditions = std::move(moved);
    rule->children.push_back(std::move(child));
    std::vector<int> childPath = m_sel.path;
    childPath.push_back(static_cast<int>(rule->children.size()) - 1);
    rebuildLayout();
    setSelection({ childPath, UiFormat::Slot::Event, -1, {}, { childPath } }, true);
    emit sheetEdited();
}

void EventSheetView::deleteSelected() {
    if (!m_sheet || !m_sel.valid()) return;
    ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, m_sel.path);
    if (!rule) return;

    if (m_sel.slot == UiFormat::Slot::Condition && m_sel.index >= 0) {
        emit aboutToEdit();
        for (int at : pickedIndices()) {
            if (at >= 0 && at < static_cast<int>(rule->conditions.size()))
                rule->conditions.erase(rule->conditions.begin() + at);
        }
        m_sel.index = -1;
        m_sel.run.clear();
        m_sel.slot = UiFormat::Slot::Event;
        m_sel.selectedPaths = { m_sel.path };
        rebuildLayout();
        viewport()->update();
        emit selectionChanged(m_sel);
        emit sheetEdited();
        return;
    }
    if (m_sel.slot == UiFormat::Slot::Action && m_sel.index >= 0) {
        emit aboutToEdit();
        for (int at : pickedIndices()) {
            if (at >= 0 && at < static_cast<int>(rule->actions.size()))
                rule->actions.erase(rule->actions.begin() + at);
        }
        m_sel.index = -1;
        m_sel.run.clear();
        m_sel.slot = UiFormat::Slot::Event;
        m_sel.selectedPaths = { m_sel.path };
        rebuildLayout();
        viewport()->update();
        emit selectionChanged(m_sel);
        emit sheetEdited();
        return;
    }

    std::vector<std::vector<int>> toErase = topLevelPickedRulePaths();
    if (toErase.empty()) return;
    // Reverse lexicographical order guarantees erasing an item never invalidates remaining paths
    std::sort(toErase.begin(), toErase.end(), std::greater<std::vector<int>>());

    emit aboutToEdit();
    for (const auto& rp : toErase) {
        if (rp.empty()) continue;
        std::vector<int> parent(rp.begin(), rp.end() - 1);
        const int last = rp.back();
        if (parent.empty()) {
            if (last >= 0 && last < static_cast<int>(m_sheet->rules.size()))
                m_sheet->rules.erase(m_sheet->rules.begin() + last);
        } else if (ModLogic::Rule* owner = UiFormat::RuleAt(*m_sheet, parent)) {
            if (last >= 0 && last < static_cast<int>(owner->children.size()))
                owner->children.erase(owner->children.begin() + last);
        }
    }
    m_sel = {};
    rebuildLayout();
    viewport()->update();
    emit selectionChanged(m_sel);
    emit sheetEdited();
}

void EventSheetView::copySelected() {
    if (!m_sheet || !m_sel.valid()) return;
    const ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, m_sel.path);
    if (!rule) return;

    m_clipRules.clear();
    m_clipConds.clear();
    m_clipActs.clear();

    if (m_sel.slot == UiFormat::Slot::Condition && m_sel.index >= 0) {
        for (int idx : pickedIndicesAsc()) {
            if (idx >= 0 && idx < static_cast<int>(rule->conditions.size())) {
                m_clipConds.push_back(rule->conditions[idx]);
            }
        }
        m_clipSlot = UiFormat::Slot::Condition;
        m_hasClip = !m_clipConds.empty();
    } else if (m_sel.slot == UiFormat::Slot::Action && m_sel.index >= 0) {
        for (int idx : pickedIndicesAsc()) {
            if (idx >= 0 && idx < static_cast<int>(rule->actions.size())) {
                m_clipActs.push_back(rule->actions[idx]);
            }
        }
        m_clipSlot = UiFormat::Slot::Action;
        m_hasClip = !m_clipActs.empty();
    } else {
        for (const auto& rp : topLevelPickedRulePaths()) {
            if (const ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, rp)) {
                m_clipRules.push_back(*r);
            }
        }
        m_clipSlot = UiFormat::Slot::Event;
        m_hasClip = !m_clipRules.empty();
    }
}

void EventSheetView::pasteClipboard() {
    if (!m_sheet || !m_hasClip) return;

    if (m_clipSlot == UiFormat::Slot::Event) {
        if (m_clipRules.empty()) return;
        emit aboutToEdit();
        std::vector<std::vector<int>> pastedPaths;
        if (!m_sel.valid()) {
            const int start = static_cast<int>(m_sheet->rules.size());
            m_sheet->rules.insert(m_sheet->rules.end(), m_clipRules.begin(), m_clipRules.end());
            for (int k = 0; k < static_cast<int>(m_clipRules.size()); ++k) {
                pastedPaths.push_back({ start + k });
            }
        } else {
            std::vector<int> parent = m_sel.path;
            const int at = parent.back();
            parent.pop_back();
            std::vector<ModLogic::Rule>* list = parent.empty() ? &m_sheet->rules : nullptr;
            if (!parent.empty()) {
                if (ModLogic::Rule* owner = UiFormat::RuleAt(*m_sheet, parent)) list = &owner->children;
            }
            if (list) {
                const int ins = std::min(at + 1, static_cast<int>(list->size()));
                list->insert(list->begin() + ins, m_clipRules.begin(), m_clipRules.end());
                for (int k = 0; k < static_cast<int>(m_clipRules.size()); ++k) {
                    std::vector<int> p = parent;
                    p.push_back(ins + k);
                    pastedPaths.push_back(std::move(p));
                }
            }
        }
        rebuildLayout();
        if (!pastedPaths.empty()) {
            setSelection({ pastedPaths.front(), UiFormat::Slot::Event, -1, {}, pastedPaths }, true);
        } else {
            viewport()->update();
        }
        emit sheetEdited();
        return;
    }

    ModLogic::Rule* rule = UiFormat::RuleAt(*m_sheet, m_sel.path);
    if (!rule || rule->group) return;

    if (m_clipSlot == UiFormat::Slot::Condition && !m_clipConds.empty()) {
        emit aboutToEdit();
        const int ins = (m_sel.slot == UiFormat::Slot::Condition && m_sel.index >= 0)
            ? std::min(m_sel.index + 1, static_cast<int>(rule->conditions.size()))
            : static_cast<int>(rule->conditions.size());
        rule->conditions.insert(rule->conditions.begin() + ins, m_clipConds.begin(), m_clipConds.end());
        std::vector<int> run;
        for (int k = 1; k < static_cast<int>(m_clipConds.size()); ++k) run.push_back(ins + k);
        rebuildLayout();
        setSelection({ m_sel.path, UiFormat::Slot::Condition, ins, run, {} }, true);
        emit sheetEdited();
    } else if (m_clipSlot == UiFormat::Slot::Action && !m_clipActs.empty()) {
        emit aboutToEdit();
        const int ins = (m_sel.slot == UiFormat::Slot::Action && m_sel.index >= 0)
            ? std::min(m_sel.index + 1, static_cast<int>(rule->actions.size()))
            : static_cast<int>(rule->actions.size());
        rule->actions.insert(rule->actions.begin() + ins, m_clipActs.begin(), m_clipActs.end());
        std::vector<int> run;
        for (int k = 1; k < static_cast<int>(m_clipActs.size()); ++k) run.push_back(ins + k);
        rebuildLayout();
        setSelection({ m_sel.path, UiFormat::Slot::Action, ins, run, {} }, true);
        emit sheetEdited();
    }
}

EventSheetView::DropTarget EventSheetView::computeDropTarget(const QPoint& vpPos, const DragPayload& drag, Qt::KeyboardModifiers mods) const {
    DropTarget dt;
    const HitResult hit = hitTest(vpPos);
    if (hit.rowIndex < 0 || hit.rowIndex >= static_cast<int>(m_rows.size())) return dt;
    const VisualRow& row = m_rows[hit.rowIndex];
    const QPoint pt = toContent(vpPos);

    if (drag.slot == 0) {
        const std::vector<std::vector<int>>& dragged = drag.paths.empty()
            ? std::vector<std::vector<int>>{ drag.path }
            : drag.paths;
        bool hasEventRule = false;
        for (const auto& dp : dragged) {
            if (dp == row.path || IsInsidePath(dp, row.path)) return dt;
            if (m_sheet) {
                if (const ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, dp)) {
                    if (!r->group && !r->event.empty()) hasEventRule = true;
                }
            }
        }

        // Rules that subscribe to an Event may only nest inside Groups (never inside sub-events)
        const bool canNest = row.isGroup || !hasEventRule;
        const int h = row.cardRect.height();
        const int edge = std::clamp(h / 4, 5, 10);
        const int relY = pt.y() - row.cardRect.top();

        dt.valid = true;
        dt.path = row.path;
        dt.slot = 0;
        dt.index = -1;
        if (!canNest) {
            dt.nest = false;
            dt.before = (relY < h / 2);
            dt.indicatorRect = dt.before
                ? QRect(row.cardRect.left(), row.cardRect.top() - 2, row.cardRect.width(), 2)
                : QRect(row.cardRect.left(), row.cardRect.bottom() + 2, row.cardRect.width(), 2);
        } else if (relY < edge) {
            dt.nest = false;
            dt.before = true;
            dt.indicatorRect = QRect(row.cardRect.left(), row.cardRect.top() - 2, row.cardRect.width(), 2);
        } else if (relY > h - edge) {
            dt.nest = false;
            dt.before = false;
            dt.indicatorRect = QRect(row.cardRect.left(), row.cardRect.bottom() + 2, row.cardRect.width(), 2);
        } else {
            dt.nest = true;
            dt.before = false;
            dt.indicatorRect = row.cardRect;
        }
        return dt;
    }

    if (drag.slot == 1) {
        // Dragging condition(s):
        // - Drop onto a Group, onto a rule's '#' column, or while holding Alt -> nest as a new child sub-event!
        const bool wantNest = row.isGroup || (mods & Qt::AltModifier) != 0 || row.numRect.contains(pt);
        if (wantNest) {
            dt.valid = true;
            dt.path = row.path;
            dt.slot = 1;
            dt.index = -1;
            dt.nest = true;
            dt.indicatorRect = row.cardRect;
            return dt;
        }

        // Otherwise allow dropping anywhere in the Events & Conditions column of a non-group rule
        if (!row.condColRect.contains(pt)) return dt;

        dt.valid = true;
        dt.path = row.path;
        dt.slot = 1;
        dt.nest = false;

        int condCount = 0;
        for (const VisualLine& vl : row.leftLines) {
            if (vl.slot == UiFormat::Slot::Condition) ++condCount;
        }

        if (hit.kind == HitResult::Kind::Line || hit.kind == HitResult::Kind::Pill) {
            if (hit.slot == UiFormat::Slot::Condition && hit.lineIndex >= 0 && hit.lineIndex < static_cast<int>(row.leftLines.size())) {
                const VisualLine& vl = row.leftLines[hit.lineIndex];
                if (pt.y() < vl.rect.center().y()) {
                    dt.index = vl.index;
                    dt.indicatorRect = QRect(vl.rect.left(), vl.rect.top() - 1, vl.rect.width(), 2);
                } else {
                    dt.index = vl.index + 1;
                    dt.indicatorRect = QRect(vl.rect.left(), vl.rect.bottom() + 1, vl.rect.width(), 2);
                }
                return dt;
            }
            if (hit.slot == UiFormat::Slot::Event && hit.lineIndex >= 0 && hit.lineIndex < static_cast<int>(row.leftLines.size())) {
                const VisualLine& vl = row.leftLines[hit.lineIndex];
                dt.index = 0;
                dt.indicatorRect = QRect(vl.rect.left(), vl.rect.bottom() + 1, vl.rect.width(), 2);
                return dt;
            }
        }

        dt.index = condCount;
        dt.indicatorRect = QRect(row.condColRect.left() + 6, row.addCondRect.top() - 1, row.condColRect.width() - 12, 2);
        return dt;
    }

    if (drag.slot == 2) {
        if (row.isGroup || !row.actColRect.contains(pt)) return dt;

        dt.valid = true;
        dt.path = row.path;
        dt.slot = 2;
        dt.nest = false;

        if ((hit.kind == HitResult::Kind::Line || hit.kind == HitResult::Kind::Pill) && hit.slot == UiFormat::Slot::Action && hit.lineIndex >= 0 &&
            hit.lineIndex < static_cast<int>(row.rightLines.size())) {
            const VisualLine& vl = row.rightLines[hit.lineIndex];
            if (pt.y() < vl.rect.center().y()) {
                dt.index = vl.index;
                dt.indicatorRect = QRect(vl.rect.left(), vl.rect.top() - 1, vl.rect.width(), 2);
            } else {
                dt.index = vl.index + 1;
                dt.indicatorRect = QRect(vl.rect.left(), vl.rect.bottom() + 1, vl.rect.width(), 2);
            }
            return dt;
        }

        dt.index = static_cast<int>(row.rightLines.size());
        dt.indicatorRect = QRect(row.actColRect.left() + 6, row.addActRect.top() - 1, row.actColRect.width() - 12, 2);
        return dt;
    }

    return dt;
}

bool EventSheetView::applyDrop(const DragPayload& drag, const DropTarget& target) {
    if (!m_sheet || !target.valid || drag.path.empty() || target.path.empty()) return false;

    auto listOf = [&](const std::vector<int>& rp) -> std::vector<ModLogic::Rule>* {
        if (rp.size() == 1) return &m_sheet->rules;
        std::vector<int> parent(rp.begin(), rp.end() - 1);
        ModLogic::Rule* owner = UiFormat::RuleAt(*m_sheet, parent);
        return owner ? &owner->children : nullptr;
    };

    if (drag.slot == 0 && target.slot == 0) {
        std::vector<std::vector<int>> movePaths = drag.paths.empty()
            ? std::vector<std::vector<int>>{ drag.path }
            : drag.paths;
        for (const auto& dp : movePaths) {
            if (dp.empty() || dp == target.path || IsInsidePath(dp, target.path)) return false;
        }

        std::vector<ModLogic::Rule> movedRules;
        for (const auto& dp : movePaths) {
            const ModLogic::Rule* r = UiFormat::RuleAt(*m_sheet, dp);
            if (!r) return false;
            movedRules.push_back(*r);
        }
        if (movedRules.empty()) return false;

        std::vector<std::vector<int>> eraseOrder = movePaths;
        std::sort(eraseOrder.begin(), eraseOrder.end(), std::greater<std::vector<int>>());

        emit aboutToEdit();

        std::vector<int> trackedTarget = target.path;
        for (const auto& ep : eraseOrder) {
            std::vector<ModLogic::Rule>* src = listOf(ep);
            if (src && ep.back() >= 0 && ep.back() < static_cast<int>(src->size())) {
                src->erase(src->begin() + ep.back());
                AdjustPathAfterErase(trackedTarget, ep);
            }
        }

        std::vector<std::vector<int>> newPaths;
        if (target.nest) {
            if (ModLogic::Rule* owner = UiFormat::RuleAt(*m_sheet, trackedTarget)) {
                const int startIdx = static_cast<int>(owner->children.size());
                owner->children.insert(owner->children.end(), movedRules.begin(), movedRules.end());
                owner->collapsed = false;
                for (int k = 0; k < static_cast<int>(movedRules.size()); ++k) {
                    std::vector<int> np = trackedTarget;
                    np.push_back(startIdx + k);
                    newPaths.push_back(std::move(np));
                }
            }
        } else {
            if (std::vector<ModLogic::Rule>* dst = listOf(trackedTarget)) {
                int at = trackedTarget.back() + (target.before ? 0 : 1);
                at = std::clamp(at, 0, static_cast<int>(dst->size()));
                dst->insert(dst->begin() + at, movedRules.begin(), movedRules.end());
                std::vector<int> parent(trackedTarget.begin(), trackedTarget.end() - 1);
                for (int k = 0; k < static_cast<int>(movedRules.size()); ++k) {
                    std::vector<int> np = parent;
                    np.push_back(at + k);
                    newPaths.push_back(std::move(np));
                }
            }
        }

        rebuildLayout();
        if (!newPaths.empty()) {
            setSelection({ newPaths.front(), UiFormat::Slot::Event, -1, {}, newPaths }, true);
        } else {
            setSelection({}, true);
        }
        emit sheetEdited();
        return true;
    }

    if (drag.slot != target.slot || drag.slot == 0) return false;
    ModLogic::Rule* srcRule = UiFormat::RuleAt(*m_sheet, drag.path);
    ModLogic::Rule* dstRule = UiFormat::RuleAt(*m_sheet, target.path);
    if (!srcRule || !dstRule || drag.indices.empty()) return false;

    std::vector<int> asc = drag.indices;
    std::sort(asc.begin(), asc.end());
    asc.erase(std::unique(asc.begin(), asc.end()), asc.end());

    if (drag.slot == 1) {
        std::vector<ModLogic::Condition> moved;
        for (int idx : asc) {
            if (idx >= 0 && idx < static_cast<int>(srcRule->conditions.size())) {
                moved.push_back(srcRule->conditions[idx]);
            }
        }
        if (moved.empty()) return false;

        emit aboutToEdit();
        if (target.nest) {
            for (auto it = asc.rbegin(); it != asc.rend(); ++it) {
                if (*it >= 0 && *it < static_cast<int>(srcRule->conditions.size())) {
                    srcRule->conditions.erase(srcRule->conditions.begin() + *it);
                }
            }
            if (ModLogic::Rule* owner = UiFormat::RuleAt(*m_sheet, target.path)) {
                ModLogic::Rule child;
                child.conditions = std::move(moved);
                owner->children.push_back(std::move(child));
                owner->collapsed = false;
                std::vector<int> childPath = target.path;
                childPath.push_back(static_cast<int>(owner->children.size()) - 1);
                rebuildLayout();
                setSelection({ childPath, UiFormat::Slot::Event, -1, {}, { childPath } }, true);
                emit sheetEdited();
                return true;
            }
            return false;
        }

        const bool same = (srcRule == dstRule);
        int at = target.index;
        int shift = 0;
        if (same) {
            for (int idx : asc) {
                if (idx < at) ++shift;
            }
        }
        for (auto it = asc.rbegin(); it != asc.rend(); ++it) {
            if (*it >= 0 && *it < static_cast<int>(srcRule->conditions.size())) {
                srcRule->conditions.erase(srcRule->conditions.begin() + *it);
            }
        }
        at = std::clamp(at - shift, 0, static_cast<int>(dstRule->conditions.size()));
        dstRule->conditions.insert(dstRule->conditions.begin() + at, moved.begin(), moved.end());

        std::vector<int> run;
        for (int k = 1; k < static_cast<int>(moved.size()); ++k) run.push_back(at + k);
        rebuildLayout();
        setSelection({ target.path, UiFormat::Slot::Condition, at, run, {} }, true);
        emit sheetEdited();
        return true;
    }

    std::vector<ModLogic::Action> moved;
    for (int idx : asc) {
        if (idx >= 0 && idx < static_cast<int>(srcRule->actions.size())) {
            moved.push_back(srcRule->actions[idx]);
        }
    }
    if (moved.empty()) return false;

    emit aboutToEdit();
    const bool same = (srcRule == dstRule);
    int at = target.index;
    int shift = 0;
    if (same) {
        for (int idx : asc) {
            if (idx < at) ++shift;
        }
    }
    for (auto it = asc.rbegin(); it != asc.rend(); ++it) {
        if (*it >= 0 && *it < static_cast<int>(srcRule->actions.size())) {
            srcRule->actions.erase(srcRule->actions.begin() + *it);
        }
    }
    at = std::clamp(at - shift, 0, static_cast<int>(dstRule->actions.size()));
    dstRule->actions.insert(dstRule->actions.begin() + at, moved.begin(), moved.end());

    std::vector<int> run;
    for (int k = 1; k < static_cast<int>(moved.size()); ++k) run.push_back(at + k);
    rebuildLayout();
    setSelection({ target.path, UiFormat::Slot::Action, at, run, {} }, true);
    emit sheetEdited();
    return true;
}

void EventSheetView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(QString::fromLatin1(MimeType))) {
        event->acceptProposedAction();
    }
}

void EventSheetView::dragMoveEvent(QDragMoveEvent* event) {
    if (!event->mimeData()->hasFormat(QString::fromLatin1(MimeType))) return;
    
    DragPayload drag;
    if (!DecodeDrag(event->mimeData()->data(QString::fromLatin1(MimeType)), drag.path, drag.slot, drag.indices, drag.paths)) return;

    const QPoint vpPos = viewport()->mapFromParent(event->position().toPoint());
    m_dropTarget = computeDropTarget(vpPos, drag, event->modifiers());

    if (m_dropTarget.valid) event->acceptProposedAction();
    else event->ignore();
    viewport()->update();
}

void EventSheetView::dragLeaveEvent(QDragLeaveEvent* /*event*/) {
    m_dropTarget = {};
    viewport()->update();
}

void EventSheetView::dropEvent(QDropEvent* event) {
    m_dropTarget = {};
    if (!event->mimeData()->hasFormat(QString::fromLatin1(MimeType))) return;
    DragPayload drag;

    if (!DecodeDrag(event->mimeData()->data(QString::fromLatin1(MimeType)), drag.path, drag.slot, drag.indices, drag.paths)) return;

    const QPoint vpPos = viewport()->mapFromParent(event->position().toPoint());
    const DropTarget dt = computeDropTarget(vpPos, drag, event->modifiers());

    if (applyDrop(drag, dt)) event->acceptProposedAction();
    viewport()->update();
}
