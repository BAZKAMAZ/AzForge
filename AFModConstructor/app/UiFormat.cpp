#include "UiFormat.h"

#include "Localization.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace UiFormat {
    namespace {
        bool IsSymbolOperator(const std::string& v) {
            if (v.empty() || v.size() > 3) return false;
            for (unsigned char c : v) {
                if (std::isalnum(c)) return false;
            }
            return true;
        }
    }

    QString Label(const std::string& key) {
        if (key.empty()) return {};
        return QString::fromStdString(LANG(key.c_str()));
    }

    QString OptionLabel(const ModCatalog::ParamDef& def, const std::string& value) {
        for (const ModCatalog::Option& option : def.options) {
            if (option.value == value) {
                if (option.label.empty()) return QString::fromStdString(option.value);
                const QString lbl = Label(option.label);
                const QString raw = QString::fromStdString(option.value);
                if (IsSymbolOperator(option.value) && lbl != raw) {
                    return QStringLiteral("%1 (%2)").arg(lbl, raw);
                }
                return lbl;
            }
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
            QString paramLabel;
            bool isExpr = false;
            bool isEnum = false;
            bool hasOptionLabel = false;
            bool canRemove = false;
            bool placed = false;
        };

        std::vector<Resolved> resolved;
        resolved.reserve(defs.size());
        for (const ModCatalog::ParamDef& def : defs) {
            if (!ModCatalog::IsParamActive(def, defs, args)) continue;
            std::string raw = def.preset;
            for (const ModLogic::Param& p : args) {
                if (p.key == def.key) raw = p.value;
            }
            const bool isExpr = raw.size() > 1 && raw[0] == '=' && raw[1] != '=';
            const bool isEnum = !def.options.empty() && def.kind == "enum" && !isExpr;
            bool hasOptLbl = false;
            if (!isExpr && !def.options.empty()) {
                for (const ModCatalog::Option& o : def.options) {
                    if (o.value == raw) { hasOptLbl = true; break; }
                }
            }
            QString disp = (!def.options.empty() && !isExpr) ? OptionLabel(def, raw) : QString::fromStdString(raw);
            if (disp.isEmpty()) disp = QStringLiteral("?");
            const std::string effLblKey = ModCatalog::EffectiveParamLabelKey(def, defs, args);
            const QString pLabel = effLblKey.empty() ? QString::fromStdString(def.key) : Label(effLblKey);
            const bool canRem = ModCatalog::CanRemoveOptionalParam(def, defs, args);
            resolved.push_back({ &def, "%" + def.key + "%", raw, disp, pLabel, isExpr, isEnum, hasOptLbl, canRem, false });
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
                segments.push_back({ QString::fromStdString(baseText.substr(pos)), false, {}, {}, {}, false, false, false, false });
                break;
            }
            if (bestAt > pos) {
                segments.push_back({ QString::fromStdString(baseText.substr(pos, bestAt - pos)), false, {}, {}, {}, false, false, false, false });
            }
            Resolved& r = resolved[bestIdx];
            r.placed = true;
            segments.push_back({
                r.display,
                true,
                r.def->key,
                r.raw,
                r.paramLabel,
                r.isExpr,
                r.isEnum,
                r.hasOptionLabel,
                r.canRemove
            });
            pos = bestAt + r.mark.size();
        }

        bool firstTail = true;
        for (const Resolved& r : resolved) {
            if (r.placed) continue;
            const bool labeled = haveLine || r.def->optional || !r.def->whenParam.empty();
            if (!labeled && r.raw.empty() && r.def->preset.empty()) continue;
            if (labeled) {
                const QString prefix = segments.empty()
                    ? (r.paramLabel + QStringLiteral(" "))
                    : (QStringLiteral(" · ") + r.paramLabel + QStringLiteral(" "));
                segments.push_back({ prefix, false, {}, {}, {}, false, false, false, false });
            } else {
                segments.push_back({ firstTail ? QStringLiteral("  ") : QStringLiteral(", "), false, {}, {}, {}, false, false, false, false });
            }
            firstTail = false;
            segments.push_back({
                r.display,
                true,
                r.def->key,
                r.raw,
                r.paramLabel,
                r.isExpr,
                r.isEnum,
                r.hasOptionLabel,
                r.canRemove
            });
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

    namespace {

    std::vector<std::string> ParseHandlerVarNames(const std::string& declaration) {
        std::vector<std::string> names;
        const size_t open = declaration.find('(');
        const size_t close = declaration.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close <= open) return names;

        std::string part;
        for (size_t at = open + 1; at <= close; ++at) {
            if (at != close && declaration[at] != ',') {
                part += declaration[at];
                continue;
            }
            const size_t bracket = part.find('[');
            if (bracket != std::string::npos) part.erase(bracket);
            const size_t end = part.find_last_not_of(" \t*&");
            if (end != std::string::npos) {
                size_t start = end;
                while (start > 0 && (std::isalnum(static_cast<unsigned char>(part[start - 1])) || part[start - 1] == '_')) {
                    --start;
                }
                const std::string name = part.substr(start, end - start + 1);
                if (!name.empty() && name != "void") names.push_back(name);
            }
            part.clear();
        }
        return names;
    }

    bool HasWordArrow(const std::string& code, const std::string& ident) {
        const std::string needle = ident + "->";
        for (size_t pos = code.find(needle); pos != std::string::npos; pos = code.find(needle, pos + 1)) {
            if (pos == 0) return true;
            const unsigned char prev = static_cast<unsigned char>(code[pos - 1]);
            if (!std::isalnum(prev) && prev != '_' && prev != '.') return true;
        }
        return false;
    }

    struct RuleScopeInfo {
        std::set<std::string> provides;
        std::set<std::string> handlerVars;
        bool loop = false;
        bool hasToken = false;
    };

    RuleScopeInfo CollectScope(const ModLogic::Sheet& sheet, const std::vector<int>& path) {
        RuleScopeInfo info;
        const ModCatalog::EventDef* leadEv = nullptr;
        bool sawNonGroup = false;

        for (size_t depth = 1; depth <= path.size(); ++depth) {
            const std::vector<int> head(path.begin(), path.begin() + static_cast<ptrdiff_t>(depth));
            const ModLogic::Rule* rule = UiFormat::RuleAt(sheet, head);
            if (!rule) break;
            if (!rule->group && !sawNonGroup) {
                sawNonGroup = true;
                leadEv = ModCatalog::FindEvent(rule->event);
            }
            if (const ModCatalog::EventDef* ev = ModCatalog::FindEvent(rule->event)) {
                std::string part;
                for (size_t at = 0; at <= ev->provides.size(); ++at) {
                    const char c = at < ev->provides.size() ? ev->provides[at] : ',';
                    if (c != ',') { if (c != ' ') part += c; continue; }
                    if (!part.empty()) {
                        info.provides.insert(part);
                        if (part == "slot_save" || part == "slot_load") info.provides.insert("slot_event");
                    }
                    part.clear();
                }
                if (ev->subscribe.find("%token%") != std::string::npos) info.hasToken = true;
            }
            for (const ModLogic::Condition& c : rule->conditions) {
                const ModCatalog::ConditionDef* cd = ModCatalog::FindCondition(c.id);
                if (cd && !cd->loop.empty()) info.loop = true;
            }
        }

        if (!leadEv || !leadEv->frameTest.empty()) {
            info.handlerVars.insert("frame");
        } else if (!leadEv->handler.empty()) {
            for (const std::string& v : ParseHandlerVarNames(leadEv->handler)) {
                info.handlerVars.insert(v);
            }
        }
        return info;
    }

    std::string NeedsKeyOf(const std::string& needs, const RuleScopeInfo& scope) {
        if (needs.empty()) return {};
        if (needs == "loop") return scope.loop ? std::string() : "mod.needs.loop";
        if (needs == "token") return scope.hasToken ? std::string() : "mod.needs.token";
        if (scope.provides.count(needs) != 0) return {};
        if (needs == "item_style") return "mod.needs.itemStyle";
        if (needs == "slot_save") return "mod.needs.slotSave";
        if (needs == "slot_load") return "mod.needs.slotLoad";
        if (needs == "slot_event") return "mod.needs.slotEvent";
        if (needs == "camera_view") return "mod.needs.cameraView";
        if (needs == "camera_update") return "mod.needs.cameraUpdate";
        if (needs == "camera_place") return "mod.needs.cameraPlace";
        if (needs == "particle_birth") return "mod.needs.particleBirth";
        if (needs == "particle_think") return "mod.needs.particleThink";
        if (needs == "particle_trail") return "mod.needs.particleTrail";
        if (needs == "demo_state") return "mod.needs.demoState";
        if (needs == "demo_move") return "mod.needs.demoMove";
        if (needs == "demo_input") return "mod.needs.demoInput";
        if (needs == "world_event") return "mod.needs.worldEvent";
        if (needs == "player_event") return "mod.needs.playerEvent";
        if (needs == "player_death") return "mod.needs.playerDeath";
        if (needs == "player_frame") return "mod.needs.playerFrame";
        if (needs == "player_move") return "mod.needs.playerMove";
        if (needs == "bonus_take") return "mod.needs.bonusTake";
        if (needs == "bonus_spawn") return "mod.needs.bonusSpawn";
        if (needs == "bonus_body") return "mod.needs.bonusBody";
        if (needs == "bonus_kind_update") return "mod.needs.bonusKindUpdate";
        if (needs == "inv_slot_event") return "mod.needs.invSlotEvent";
        if (needs == "inv_chain_event") return "mod.needs.invChainEvent";
        if (needs == "monster_kill") return "mod.needs.monsterKill";
        if (needs == "monster_touch") return "mod.needs.monsterTouch";
        if (needs == "monster_spawn") return "mod.needs.monsterSpawn";
        if (needs == "monster_place") return "mod.needs.monsterPlace";
        if (needs == "monster_think") return "mod.needs.monsterThink";
        if (needs == "monster_body") return "mod.needs.monsterBody";
        return "mod.needs.render";
    }

    std::string CheckSnippetScope(const std::string& snippet, const RuleScopeInfo& scope) {
        static const char* const KnownVars[] = {
            "frame", "info", "pframe", "dinfo", "move", "view", "camFrame", "trailDesc",
            "tinfo", "sinfo", "kinfo", "iinfo", "cinfo", "pinfo", "minfo", "mkinfo"
        };
        for (const char* ident : KnownVars) {
            if (HasWordArrow(snippet, ident) && scope.handlerVars.count(ident) == 0) {
                return "mod.needs.handlerVar";
            }
        }
        for (const ModCatalog::ExpressionDef& ex : ModCatalog::Expressions()) {
            if (ex.needs.empty() || ex.text.empty()) continue;
            if (snippet.find(ex.text) != std::string::npos) {
                const std::string miss = NeedsKeyOf(ex.needs, scope);
                if (!miss.empty()) return miss;
            }
        }
        return {};
    }

    } // namespace

    std::string MissingParamsContext(const ModLogic::Sheet& sheet, const std::vector<int>& path, const std::vector<ModLogic::Param>& args) {
        if (args.empty()) return {};
        bool anyExpr = false;
        for (const ModLogic::Param& p : args) {
            if ((p.value.size() > 1 && p.value[0] == '=' && p.value[1] != '=') || p.value.find('{') != std::string::npos) {
                anyExpr = true;
                break;
            }
        }
        if (!anyExpr) return {};

        const RuleScopeInfo scope = CollectScope(sheet, path);
        for (const ModLogic::Param& p : args) {
            const std::string& v = p.value;
            if (v.size() > 1 && v[0] == '=' && v[1] != '=') {
                const std::string miss = CheckSnippetScope(v.substr(1), scope);
                if (!miss.empty()) return miss;
                continue;
            }
            for (size_t at = 0; at < v.size();) {
                const size_t open = v.find('{', at);
                const size_t close = open == std::string::npos ? std::string::npos : v.find('}', open + 1);
                if (close == std::string::npos) break;
                if (open + 2 < close && (v[open + 1] == '#' || v[open + 1] == '~' || v[open + 1] == '$')) {
                    const std::string miss = CheckSnippetScope(v.substr(open + 2, close - open - 2), scope);
                    if (!miss.empty()) return miss;
                }
                at = close + 1;
            }
        }
        return {};
    }

    std::string MissingConditionContext(const ModLogic::Sheet& sheet, const std::vector<int>& path, const ModCatalog::ConditionDef& def, const std::vector<ModLogic::Param>& args) {
        if (!def.needs.empty()) {
            const RuleScopeInfo scope = CollectScope(sheet, path);
            const std::string miss = NeedsKeyOf(def.needs, scope);
            if (!miss.empty()) return miss;
        }
        return MissingParamsContext(sheet, path, args);
    }

    std::string MissingContext(const ModLogic::Sheet& sheet, const std::vector<int>& path, const ModCatalog::ActionDef& def, const std::vector<ModLogic::Param>& args) {
        if (!def.needs.empty()) {
            const RuleScopeInfo scope = CollectScope(sheet, path);
            const std::string miss = NeedsKeyOf(def.needs, scope);
            if (!miss.empty()) return miss;
        }
        return MissingParamsContext(sheet, path, args);
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
