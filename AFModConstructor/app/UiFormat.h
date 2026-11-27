#pragma once

#include "ModCatalog.h"
#include "ModLogic.h"

#include <QColor>
#include <QString>

#include <string>
#include <vector>

namespace UiFormat {

    enum class Slot { Event, Condition, Action };

    struct Selection {
        std::vector<int> path;
        Slot slot = Slot::Event;
        int index = -1;
        std::vector<int> run;
        std::vector<std::vector<int>> selectedPaths;

        bool valid() const { return !path.empty(); }
        int count() const {
            if (!valid()) return 0;
            if (slot == Slot::Event) {
                return selectedPaths.empty() ? 1 : static_cast<int>(selectedPaths.size());
            }
            return index >= 0 ? (1 + static_cast<int>(run.size())) : 0;
        }
    };

    struct LineSegment {
        QString text;
        bool isParam = false;
        std::string paramKey;
        std::string rawValue;
        QString paramLabel;
        bool isExpr = false;
        bool isEnum = false;
        bool hasOptionLabel = false;
        bool canRemove = false;
    };

    QString Label(const std::string& key);
    QString OptionLabel(const ModCatalog::ParamDef& def, const std::string& value);

    std::vector<LineSegment> BuildSegments(const std::string& labelKey, const std::vector<ModCatalog::ParamDef>& defs, const std::vector<ModLogic::Param>& args);
    QString LinePlain(const std::string& labelKey, const std::vector<ModCatalog::ParamDef>& defs, const std::vector<ModLogic::Param>& args);

    QString VarLine(const ModLogic::Var& var);
    QString VarTypeLabel(const std::string& type);
    QString RuleNumber(const std::vector<int>& path);
    QColor SubsystemColor(const std::string& groupId);

    ModLogic::Rule* RuleAt(ModLogic::Sheet& sheet, const std::vector<int>& path);
    const ModLogic::Rule* RuleAt(const ModLogic::Sheet& sheet, const std::vector<int>& path);

    const std::vector<ModCatalog::ParamDef>* ParamsOf(Slot slot, const std::string& id);
    std::string GroupOf(Slot slot, const std::string& id);
    std::string MissingParamsContext(const ModLogic::Sheet& sheet, const std::vector<int>& path, const std::vector<ModLogic::Param>& args);
    std::string MissingConditionContext(const ModLogic::Sheet& sheet, const std::vector<int>& path, const ModCatalog::ConditionDef& def, const std::vector<ModLogic::Param>& args);
    std::string MissingContext(const ModLogic::Sheet& sheet, const std::vector<int>& path, const ModCatalog::ActionDef& def, const std::vector<ModLogic::Param>& args = {});
    bool IsRuleBroken(const ModLogic::Rule& rule);
    bool IsValidIdentifier(const std::string& name);

} // namespace UiFormat
