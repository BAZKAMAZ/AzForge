#pragma once

#include "ModLogic.h"
#include "UiFormat.h"

#include <QAbstractScrollArea>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QString>

#include <string>
#include <vector>

class QLineEdit;
class QComboBox;

class EventSheetView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit EventSheetView(QWidget* parent = nullptr);

    void setSheet(ModLogic::Sheet* sheet);
    ModLogic::Sheet* sheet() const { return m_sheet; }

    void setSelection(const UiFormat::Selection& sel, bool emitSignal = true);
    const UiFormat::Selection& selection() const { return m_sel; }

    void setFilter(const QString& query, const std::string& subsystemId, bool brokenOnly);
    void setAllGroupsCollapsed(bool collapsed);
    void scrollToRule(const std::vector<int>& path);
    void refreshLayout();

    void toggleEnabled();
    void toggleInverted();
    void addSubEvent();
    void moveConditionsToSubEvent();
    void deleteSelected();
    void copySelected();
    void pasteClipboard();

signals:
    void aboutToEdit();
    void sheetEdited();
    void selectionChanged(const UiFormat::Selection& sel);
    void requestAdd(const std::vector<int>& path, UiFormat::Slot slot);
    void requestReplace(const std::vector<int>& path, UiFormat::Slot slot, int index);
    void requestFocusInspector();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool viewportEvent(QEvent* event) override;

private:
    struct PillHit {
        QRect rect;
        std::string paramKey;
        std::string rawValue;
        QString displayText;
        QString paramLabel;
        bool isExpr = false;
    };

    struct VisualLine {
        QRect rect;
        UiFormat::Slot slot = UiFormat::Slot::Event;
        int index = -1;
        std::string entryId;
        std::string groupId;
        bool inverted = false;
        bool enabled = true;
        bool unknown = false;
        bool veto = false;
        std::string warnKey;
        std::vector<UiFormat::LineSegment> segments;
        std::vector<PillHit> pills;
    };

    struct VisualRow {
        std::vector<int> path;
        QString number;
        int depth = 0;
        bool isGroup = false;
        bool groupCollapsed = false;
        bool enabled = true;
        QString groupTitle;
        int groupCount = 0;
        QColor accentColor;

        QRect cardRect;
        QRect numRect;
        QRect foldRect;
        QRect condColRect;
        QRect actColRect;
        QRect addCondRect;
        QRect addActRect;

        std::vector<VisualLine> leftLines;
        std::vector<VisualLine> rightLines;
    };

    struct HitResult {
        enum class Kind {
            None, Splitter, Fold, Number, GroupHeader, Line, Pill, AddCondition, AddAction
        };
        Kind kind = Kind::None;
        int rowIndex = -1;
        int lineIndex = -1;
        int pillIndex = -1;
        UiFormat::Slot slot = UiFormat::Slot::Event;
    };

    struct DragPayload {
        std::vector<int> path;
        int slot = 0; // 0 = rule(s), 1 = condition(s), 2 = action(s)
        std::vector<int> indices;
        std::vector<std::vector<int>> paths;
    };

    struct DropTarget {
        bool valid = false;
        std::vector<int> path;
        int slot = 0;
        int index = -1;
        bool before = false;
        bool nest = false;
        QRect indicatorRect;
    };

    void rebuildLayout();
    void layoutRulesRecursive(const std::vector<ModLogic::Rule>& rules, std::vector<int>& path, int depth, int& yCursor, int contentWidth, int condWidth);
    bool ruleMatchesFilter(const ModLogic::Rule& rule) const;
    int splitX(int viewportWidth) const;
    QPoint toContent(const QPoint& vpPos) const;
    QPoint fromContent(const QPoint& cPos) const;
    HitResult hitTest(const QPoint& vpPos) const;
    DropTarget computeDropTarget(const QPoint& vpPos, const DragPayload& drag, Qt::KeyboardModifiers mods) const;
    bool applyDrop(const DragPayload& drag, const DropTarget& target);

    int visualRowIndex(const std::vector<int>& path) const;
    bool isRulePicked(const std::vector<int>& path) const;
    bool isLinePicked(const std::vector<int>& path, UiFormat::Slot slot, int index) const;
    void pickRule(const std::vector<int>& path, bool shift, bool ctrl);
    void pickLine(const std::vector<int>& path, UiFormat::Slot slot, int index, bool shift, bool ctrl = false);
    std::vector<int> pickedIndices() const;
    std::vector<int> pickedIndicesAsc() const;
    std::vector<std::vector<int>> pickedRulePaths() const;
    std::vector<std::vector<int>> topLevelPickedRulePaths() const;

    void startInlineEdit(const VisualRow& row, const VisualLine& line, const PillHit& pill);
    void closeInlineEdit(bool commit);
    void renameGroupDialog(const std::vector<int>& path);

    ModLogic::Sheet* m_sheet = nullptr;
    UiFormat::Selection m_sel;

    QString m_filterQuery;
    std::string m_filterSubsystem;
    bool m_filterBrokenOnly = false;

    std::vector<VisualRow> m_rows;
    int m_totalHeight = 0;
    double m_splitRatio = 0.46;

    HitResult m_hover;
    bool m_draggingSplitter = false;
    QPoint m_dragStartPos;
    bool m_dragPending = false;
    bool m_collapseSelectionOnRelease = false;
    std::vector<int> m_pendingClickPath;
    UiFormat::Slot m_pendingClickSlot = UiFormat::Slot::Event;
    int m_pendingClickIndex = -1;
    DragPayload m_dragSource;
    DropTarget m_dropTarget;

    QPointer<QWidget> m_inlineEditor;
    std::vector<int> m_inlinePath;
    UiFormat::Slot m_inlineSlot = UiFormat::Slot::Action;
    int m_inlineIndex = -1;
    std::string m_inlineParamKey;

    std::vector<ModLogic::Rule> m_clipRules;
    std::vector<ModLogic::Condition> m_clipConds;
    std::vector<ModLogic::Action> m_clipActs;
    UiFormat::Slot m_clipSlot = UiFormat::Slot::Event;
    bool m_hasClip = false;
};
