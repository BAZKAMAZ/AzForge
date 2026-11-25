#include "UiFormat.h"

#include "Localization.h"

#include <algorithm>
#include <cctype>

namespace UiFormat {
    QString Label(const std::string& key) {
        if (key.empty()) return {};
        return QString::fromStdString(LANG(key.c_str()));
    }

    QString OptionLabel(const ModCatalog::ParamDef& def, const std::string& value) {
        for (const ModCatalog::Option& option : def.options) {
            if (option.value == value) return option.label.empty() ? QString::fromStdString(option.value) : Label(option.label);
        }
        return QString::fromStdString(value);
    }

    std::vector<LineSegment> BuildSegments(const std::string& labelKey, const std::vector<ModCatalog::ParamDef>& defs, const std::vector<ModLogic::Param>& args) {
        const std::string lineKey = labelKey + ".line";
        const std::string tmplUtf8 = LANG(lineKey.c_str());
        const bool haveLine = !tmplUtf8.empty() && tmplUtf8 != lineKey;

        struct Resolved {
            const ModCatalog::ParamDef* def = nullptr;
            std::string mark;
            std::string raw;
            QString display;
            bool isExpr = false;
            bool placed = false;
        };

        std::vector<Resolved> resolved;
        resolved.reserve(defs.size());
        for (const ModCatalog::ParamDef& def : defs) {
            std::string raw = def.preset;
            for (const ModLogic::Param& p : args) {
                if (p.key == def.key) raw = p.value;
            }
            const bool isExpr = raw.size() > 1 && raw[0] == '=' && raw[1] != '=';
            QString disp = !def.options.empty() ? OptionLabel(def, raw) : QString::fromStdString(raw);
            if (disp.isEmpty()) disp = QStringLiteral("?");
            resolved.push_back({ &def, "%" + def.key + "%", raw, disp, isExpr, false });
        }

        std::vector<LineSegment> segments;
        const std::string baseText = haveLine ? tmplUtf8 : Label(labelKey).toStdString();

        size_t pos = 0;
        while (pos < baseText.size()) {
            size_t bestAt = std::string::npos;
            size_t bestIdx = 0;
            for (size_t i = 0; i < resolved.size(); ++i) {
                const size_t at = baseText.find(resolved[i].mark, pos);
                if (at < bestAt) {
                    bestAt = at;
                    bestIdx = i;
                }
            }
            if (bestAt == std::string::npos) {
                segments.push_back({ QString::fromStdString(baseText.substr(pos)), false, {}, {}, {}, false });
                break;
            }
            if (bestAt > pos) {
                segments.push_back({ QString::fromStdString(baseText.substr(pos, bestAt - pos)), false, {}, {}, {}, false });
            }
            Resolved& r = resolved[bestIdx];
            r.placed = true;
            segments.push_back({
                r.display,
                true,
                r.def->key,
                r.raw,
                r.def->label.empty() ? QString::fromStdString(r.def->key) : Label(r.def->label),
                r.isExpr
            });
            pos = bestAt + r.mark.size();
        }

        if (!haveLine) {
            bool firstTail = true;
            for (const Resolved& r : resolved) {
                if (r.placed) continue;
                if (r.raw.empty() && r.def->preset.empty()) continue;
                segments.push_back({ firstTail ? QStringLiteral("  ") : QStringLiteral(", "), false, {}, {}, {}, false });
                firstTail = false;
                segments.push_back({
                    r.display,
                    true,
                    r.def->key,
                    r.raw,
                    r.def->label.empty() ? QString::fromStdString(r.def->key) : Label(r.def->label),
                    r.isExpr
                });
            }
        }

        return segments;
    }

    QString LinePlain(const std::string& labelKey, const std::vector<ModCatalog::ParamDef>& defs, const std::vector<ModLogic::Param>& args) {
        QString out;
        for (const LineSegment& seg : BuildSegments(labelKey, defs, args)) {
            out += seg.text;
        }
        return out;
    }

    QString VarTypeLabel(const std::string& type) {
        if (type == "float") return Label("mod.var.float");
        if (type == "text")  return Label("mod.var.text");
        if (type == "bool")  return Label("mod.var.bool");
        if (type == "list")  return Label("mod.var.list");
        return Label("mod.var.int");
    }

    QString VarLine(const ModLogic::Var& var) {
        const QString name = QString::fromStdString(var.name);
        const QString kind = VarTypeLabel(var.type);
        if (var.type == "bool") {
            const QString yn = (var.value == "1" || var.value == "true") ? Label("common.yes") : Label("common.no");
            return QStringLiteral("%1 (%2) = %3").arg(name, kind, yn);
        }
        if (var.type == "text") {
            return QStringLiteral("%1 (%2) = \"%3\"").arg(name, kind, QString::fromStdString(var.value));
        }
        if (var.type == "list") {
            return QStringLiteral("%1 (%2) [%3]").arg(name, kind, QString::fromStdString(var.value));
        }
        return QStringLiteral("%1 (%2) = %3")
            .arg(name, kind, QString::fromStdString(var.value.empty() ? "0" : var.value));
    }

    QString RuleNumber(const std::vector<int>& path) {
        QString out;
        for (size_t i = 0; i < path.size(); ++i) {
            if (i > 0) out += QLatin1Char('.');
            out += QString::number(path[i] + 1);
        }
        return out;
    }

    QColor SubsystemColor(const std::string& groupId) {
        static const QColor palette[] = {
            QColor(0x9b, 0x87, 0xf5), // violet
            QColor(0x5b, 0xb8, 0xf5), // sky
            QColor(0x6f, 0xd4, 0x9b), // mint
            QColor(0xeb, 0xb7, 0x5c), // amber
            QColor(0xe5, 0x7c, 0x9e), // rose
            QColor(0x6c, 0xd4, 0xd4), // cyan
            QColor(0xa6, 0xd4, 0x6c), // lime
            QColor(0xc4, 0x8c, 0xf0), // lavender
        };
        if (groupId.empty()) return QColor(0x6b, 0x73, 0x8f);
        uint32_t h = 2166136261u;
        for (char c : groupId) {
            h ^= static_cast<unsigned char>(c);
            h *= 16777619u;
        }
        return palette[h % std::size(palette)];
    }

    ModLogic::Rule* RuleAt(ModLogic::Sheet& sheet, const std::vector<int>& path) {
        if (path.empty()) return nullptr;
        if (path[0] < 0 || path[0] >= static_cast<int>(sheet.rules.size())) return nullptr;
        ModLogic::Rule* rule = &sheet.rules[path[0]];
        for (size_t at = 1; at < path.size(); ++at) {
            if (path[at] < 0 || path[at] >= static_cast<int>(rule->children.size())) return nullptr;
            rule = &rule->children[path[at]];
        }
        return rule;
    }

    const ModLogic::Rule* RuleAt(const ModLogic::Sheet& sheet, const std::vector<int>& path) {
        return RuleAt(const_cast<ModLogic::Sheet&>(sheet), path);
    }

    const std::vector<ModCatalog::ParamDef>* ParamsOf(Slot slot, const std::string& id) {
        if (slot != Slot::Action) {
            if (const ModCatalog::EventDef* def = ModCatalog::FindEvent(id)) return &def->filters;
            if (const ModCatalog::ConditionDef* def = ModCatalog::FindCondition(id)) return &def->params;
            return nullptr;
        }
        if (const ModCatalog::ActionDef* def = ModCatalog::FindAction(id)) return &def->params;
        return nullptr;
    }

    std::string GroupOf(Slot slot, const std::string& id) {
        if (slot != Slot::Action) {
            if (const ModCatalog::EventDef* def = ModCatalog::FindEvent(id)) return def->group;
            if (const ModCatalog::ConditionDef* def = ModCatalog::FindCondition(id)) return def->group;
        } else if (const ModCatalog::ActionDef* def = ModCatalog::FindAction(id)) {
            return def->group;
        }
        return {};
    }

    std::string MissingContext(const ModLogic::Sheet& sheet, const std::vector<int>& path, const ModCatalog::ActionDef& def) {
        if (def.needs.empty()) return {};

        std::string provides;
        bool loop = false;
        bool hasToken = false;

        for (size_t depth = 1; depth <= path.size(); ++depth) {
            const std::vector<int> head(path.begin(), path.begin() + static_cast<ptrdiff_t>(depth));
            const ModLogic::Rule* rule = RuleAt(sheet, head);
            if (!rule) break;

            if (const ModCatalog::EventDef* ev = ModCatalog::FindEvent(rule->event)) {
                if (!ev->provides.empty()) provides += (provides.empty() ? "" : ",") + ev->provides;
                if (ev->subscribe.find("%token%") != std::string::npos) hasToken = true;
            }
            for (const ModLogic::Condition& c : rule->conditions) {
                const ModCatalog::ConditionDef* cd = ModCatalog::FindCondition(c.id);
                if (cd && !cd->loop.empty()) loop = true;
            }
        }

        if (def.needs == "loop") return loop ? std::string() : "mod.needs.loop";
        if (def.needs == "token") return hasToken ? std::string() : "mod.needs.token";
        if (provides.find(def.needs) != std::string::npos) return {};
        if (def.needs == "item_style") return "mod.needs.itemStyle";
        if (def.needs == "slot_save") return "mod.needs.slotSave";
        if (def.needs == "slot_load") return "mod.needs.slotLoad";
        if (def.needs == "camera_view") return "mod.needs.cameraView";

        return "mod.needs.render";
    }

    bool IsRuleBroken(const ModLogic::Rule& rule) {
        if (!rule.group) {
            if (!rule.event.empty() && !ModCatalog::FindEvent(rule.event)) return true;
            for (const ModLogic::Condition& c : rule.conditions) {
                if (!ModCatalog::FindCondition(c.id)) return true;
            }
            for (const ModLogic::Action& a : rule.actions) {
                if (!ModCatalog::FindAction(a.id)) return true;
            }
        }
        for (const ModLogic::Rule& child : rule.children) {
            if (IsRuleBroken(child)) return true;
        }
        return false;
    }

    bool IsValidIdentifier(const std::string& name) {
        if (name.empty() || (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_')) return false;

        for (char c : name) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
        }
        return true;
    }

} // namespace UiFormat
