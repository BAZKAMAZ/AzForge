#include "ModCatalog.h"

#include "Localization.h"
#include "ModLogic.h"
#include "ModPackages.h"
#include "PathUtil.h"
#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::vector<ModCatalog::SubsystemDef> g_subsystems;
std::vector<ModCatalog::EventDef> g_events;
std::vector<ModCatalog::ConditionDef> g_conditions;
std::vector<ModCatalog::ActionDef> g_actions;
std::vector<ModCatalog::ExpressionDef> g_expressions;
std::vector<std::string> g_warnings;
bool g_loaded = false;

std::vector<ModCatalog::ParamDef> ReadParams(const json& j, const char* key) {
    std::vector<ModCatalog::ParamDef> params;
    if (!j.contains(key) || !j[key].is_array()) return params;
    for (const json& p : j[key]) {
        ModCatalog::ParamDef def;
        def.key = p.value("key", "");
        def.label = p.value("label", "");
        def.kind = p.value("kind", "text");
        def.preset = p.value("preset", "");
        def.test = p.value("test", "");
        def.optional = p.value("optional", false);
        if (p.contains("when") && p["when"].is_object()) {
            const json& w = p["when"];
            def.whenParam = w.value("param", "");
            def.whenCustom = w.value("custom", false);
            if (w.contains("equals") && w["equals"].is_string()) {
                def.whenValues.push_back(w["equals"].get<std::string>());
            }
            if (w.contains("in") && w["in"].is_array()) {
                for (const json& v : w["in"]) {
                    if (v.is_string()) def.whenValues.push_back(v.get<std::string>());
                }
            }
            if (w.contains("notIn") && w["notIn"].is_array()) {
                def.whenNegate = true;
                for (const json& v : w["notIn"]) {
                    if (v.is_string()) def.whenValues.push_back(v.get<std::string>());
                }
            }
        }
        if (p.contains("autoWhen") && p["autoWhen"].is_object()) {
            const json& aw = p["autoWhen"];
            def.autoWhenParam = aw.value("param", "");
            if (aw.contains("in") && aw["in"].is_array()) {
                for (const json& v : aw["in"]) {
                    if (v.is_string()) def.autoWhenValues.push_back(v.get<std::string>());
                }
            }
        }
        if (p.contains("labelWhen") && p["labelWhen"].is_array()) {
            for (const json& lw : p["labelWhen"]) {
                if (!lw.is_object()) continue;
                ModCatalog::LabelWhen item;
                item.label = lw.value("label", "");
                if (lw.contains("in") && lw["in"].is_array()) {
                    for (const json& v : lw["in"]) {
                        if (v.is_string()) item.values.push_back(v.get<std::string>());
                    }
                }
                if (!item.label.empty() && !item.values.empty()) def.labelWhen.push_back(std::move(item));
            }
        }
        if (p.contains("options") && p["options"].is_array()) {
            for (const json& o : p["options"]) {
                ModCatalog::Option option;
                option.value = o.value("value", "");
                option.label = o.value("label", "");
                if (!option.value.empty()) def.options.push_back(std::move(option));
            }
        }
        if (!def.key.empty()) params.push_back(std::move(def));
    }
    return params;
}

bool ReadFile(const fs::path& path, json& out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "missing file " + PathUtil::U8(path);
        return false;
    }
    try {
        in >> out;
    } catch (const std::exception& e) {
        if (error) *error = PathUtil::U8(path.filename()) + ": " + e.what();
        return false;
    }
    if (!out.is_array()) {
        if (error) *error = PathUtil::U8(path.filename()) + ": expected JSON array";
        return false;
    }
    return true;
}

template <typename T>
bool Claim(const ModPackages::Package& package, const T& def, const std::vector<T>& existing, const char* what) {
    if (!ModPackages::Owns(package, def.id)) {
        g_warnings.push_back(std::string(what) + " '" + def.id + "': outside the namespaces of '" + package.id + "'");
        return false;
    }
    for (const T& other : existing) {
        if (other.id != def.id) continue;
        g_warnings.push_back(std::string(what) + " '" + def.id + "': already in the catalog");
        return false;
    }
    return true;
}

bool PackageJson(const ModPackages::Package& package, const char* name, json& out) {
    std::string text;
    if (!ModPackages::ReadFile(package, std::string("catalog/") + name, text, nullptr)) return false;
    try {
        out = json::parse(text);
    } catch (const std::exception& e) {
        g_warnings.push_back(package.id + "/" + name + ": " + e.what());
        return false;
    }
    return out.is_array();
}

void LoadPackages() {
    Lang::ClearPackages();
    ModPackages::Scan(nullptr);

    for (const ModPackages::Package& package : ModPackages::All()) {
        if (!package.error.empty()) {
            g_warnings.push_back("package " + package.id + ": " + package.error);
            continue;
        }
        if (!package.enabled) continue;

        std::vector<std::string> keys;
        const auto want = [&keys](const std::string& key) {
            if (!key.empty()) keys.push_back(key);
        };
        const auto wantLine = [&want](const std::string& label) {
            if (!label.empty()) want(label + ".line");
        };
        const auto wantParams = [&want](const std::vector<ModCatalog::ParamDef>& params) {
            for (const ModCatalog::ParamDef& param : params) {
                want(param.label);
                for (const ModCatalog::LabelWhen& lw : param.labelWhen) want(lw.label);
                for (const ModCatalog::Option& option : param.options) want(option.label);
            }
        };

        json j;
        if (PackageJson(package, "subsystems.json", j)) {
            for (const json& item : j) {
                ModCatalog::SubsystemDef def;
                def.id = item.value("id", "");
                def.label = item.value("label", "");
                def.prologue = item.value("prologue", "");
                const std::string icon = item.value("icon", "");
                if (def.id.empty()) continue;
                if (def.id != package.id && !Claim(package, def, g_subsystems, "subsystem")) continue;
                want(def.label);
                if (!icon.empty()) def.icon = "pkg:" + package.id + ":catalog/" + icon;
                g_subsystems.push_back(std::move(def));
            }
        }

        if (PackageJson(package, "conditions.json", j)) {
            for (const json& item : j) {
                ModCatalog::ConditionDef def;
                def.id = item.value("id", "");
                def.label = item.value("label", "");
                def.group = item.value("group", "");
                def.section = item.value("section", "");
                def.params = ReadParams(item, "params");
                def.emitCode = item.value("emit", "");
                def.loop = item.value("loop", "");
                def.state = item.value("state", "");
                def.stateInit = item.value("stateInit", "0");
                def.needs = item.value("needs", "");
                if (def.id.empty() || !Claim(package, def, g_conditions, "condition")) continue;
                want(def.label);
                wantLine(def.label);
                want(def.section);
                wantParams(def.params);
                g_conditions.push_back(std::move(def));
            }
        }

        if (PackageJson(package, "actions.json", j)) {
            for (const json& item : j) {
                ModCatalog::ActionDef def;
                def.id = item.value("id", "");
                def.label = item.value("label", "");
                def.call = item.value("call", "");
                def.group = item.value("group", "");
                def.section = item.value("section", "");
                def.params = ReadParams(item, "params");
                def.emitCode = item.value("emit", "");
                def.needs = item.value("needs", "");
                if (def.id.empty() || !Claim(package, def, g_actions, "action")) continue;
                want(def.label);
                wantLine(def.label);
                want(def.section);
                wantParams(def.params);
                g_actions.push_back(std::move(def));
            }
        }

        if (PackageJson(package, "events.json", j)) {
            for (const json& item : j) {
                ModCatalog::EventDef def;
                def.id = item.value("id", "");
                def.label = item.value("label", "");
                def.group = item.value("group", "");
                def.section = item.value("section", "");
                def.call = item.value("call", "");
                def.canVeto = item.value("canVeto", false);
                def.filters = ReadParams(item, "filters");
                def.handler = item.value("handler", "");
                def.subscribe = item.value("subscribe", "");
                def.retDefault = item.value("retDefault", "return;");
                def.retVeto = item.value("retVeto", "return;");
                def.frameTest = item.value("frameTest", "");
                def.body = item.value("body", "");
                def.provides = item.value("provides", "");
                if (item.contains("values") && item["values"].is_array()) {
                    for (const json& v : item["values"]) def.values.push_back(v.get<std::string>());
                }
                if (def.id.empty() || !Claim(package, def, g_events, "event")) continue;
                want(def.label);
                wantLine(def.label);
                want(def.section);
                wantParams(def.filters);
                g_events.push_back(std::move(def));
            }
        }

        if (PackageJson(package, "expressions.json", j)) {
            for (const json& item : j) {
                ModCatalog::ExpressionDef def;
                def.id = item.value("id", "");
                def.label = item.value("label", "");
                def.group = item.value("group", "");
                def.section = item.value("section", "");
                def.text = item.value("text", "");
                def.hint = item.value("hint", "");
                def.kind = item.value("kind", "float");
                def.needs = item.value("needs", "");
                if (def.id.empty() || def.text.empty()) continue;
                if (!Claim(package, def, g_expressions, "expression")) continue;
                want(def.label);
                want(def.section);
                want(def.hint);
                g_expressions.push_back(std::move(def));
            }
        }

        std::string translations;
        const std::string language = Lang::CurrentLanguage().empty() ? "en" : Lang::CurrentLanguage();
        if (ModPackages::ReadFile(package, "lang/" + language + ".txt", translations, nullptr) ||
            ModPackages::ReadFile(package, "lang/en.txt", translations, nullptr)) {
            std::vector<std::string> rejected;
            Lang::AddPackage(package.id, translations, keys, &rejected);
            for (const std::string& key : rejected) {
                g_warnings.push_back(package.id + ": key '" + key + "' is not this package's to define");
            }
        }
    }
}

} // namespace

std::string ModCatalog::Dir() {
    return PathUtil::U8(PathUtil::ResourcePath("config"));
}

bool ModCatalog::Load(std::string* error) {
    g_subsystems.clear();
    g_events.clear();
    g_conditions.clear();
    g_actions.clear();
    g_expressions.clear();
    g_warnings.clear();
    g_loaded = false;

    const fs::path dir = PathUtil::MakePath(Dir());
    json js = json::array(), je = json::array(), jc = json::array(), ja = json::array(), jx = json::array();
    ReadFile(dir / "subsystems.json", js, nullptr);
    if (ReadFile(dir / "expressions.json", jx, nullptr)) {
        for (const json& j : jx) {
            ExpressionDef def;
            def.id = j.value("id", "");
            def.label = j.value("label", "");
            def.group = j.value("group", "");
            def.section = j.value("section", "");
            def.text = j.value("text", "");
            def.hint = j.value("hint", "");
            def.kind = j.value("kind", "float");
            if (!def.id.empty() && !def.text.empty()) g_expressions.push_back(std::move(def));
        }
    }
    ReadFile(dir / "events.json", je, nullptr);
    ReadFile(dir / "conditions.json", jc, nullptr);
    ReadFile(dir / "actions.json", ja, nullptr);

    for (const json& j : js) {
        SubsystemDef def;
        def.id = j.value("id", "");
        def.label = j.value("label", "");
        def.icon = j.value("icon", "");
        def.prologue = j.value("prologue", "");
        if (!def.id.empty()) g_subsystems.push_back(std::move(def));
    }

    for (const json& j : je) {
        EventDef def;
        def.id = j.value("id", "");
        def.label = j.value("label", "");
        def.group = j.value("group", "");
        def.section = j.value("section", "");
        def.call = j.value("call", "");
        def.canVeto = j.value("canVeto", false);
        def.filters = ReadParams(j, "filters");
        def.handler = j.value("handler", "");
        def.subscribe = j.value("subscribe", "");
        def.retDefault = j.value("retDefault", "return;");
        def.retVeto = j.value("retVeto", "return;");
        def.frameTest = j.value("frameTest", "");
        def.body = j.value("body", "");
        def.provides = j.value("provides", "");
        if (j.contains("values") && j["values"].is_array()) {
            for (const json& v : j["values"]) def.values.push_back(v.get<std::string>());
        }
        if (!def.id.empty()) g_events.push_back(std::move(def));
    }

    for (const json& j : jc) {
        ConditionDef def;
        def.id = j.value("id", "");
        def.label = j.value("label", "");
        def.group = j.value("group", "");
        def.section = j.value("section", "");
        def.params = ReadParams(j, "params");
        def.emitCode = j.value("emit", "");
        def.loop = j.value("loop", "");
        def.state = j.value("state", "");
        def.stateInit = j.value("stateInit", "0");
        if (!def.id.empty()) g_conditions.push_back(std::move(def));
    }

    for (const json& j : ja) {
        ActionDef def;
        def.id = j.value("id", "");
        def.label = j.value("label", "");
        def.call = j.value("call", "");
        def.group = j.value("group", "");
        def.section = j.value("section", "");
        def.params = ReadParams(j, "params");
        def.emitCode = j.value("emit", "");
        def.needs = j.value("needs", "");
        if (!def.id.empty()) g_actions.push_back(std::move(def));
    }

    LoadPackages();

    g_loaded = !g_events.empty() && !g_actions.empty();
    if (!g_loaded && error) *error = "catalog is empty (check " + ModPackages::Dir() + ")";
    return g_loaded;
}

bool ModCatalog::Loaded() { return g_loaded; }
const std::vector<std::string>& ModCatalog::Warnings() { return g_warnings; }

const std::vector<ModCatalog::SubsystemDef>& ModCatalog::Subsystems() { return g_subsystems; }
const std::vector<ModCatalog::EventDef>& ModCatalog::Events() { return g_events; }
const std::vector<ModCatalog::ConditionDef>& ModCatalog::Conditions() { return g_conditions; }
const std::vector<ModCatalog::ActionDef>& ModCatalog::Actions() { return g_actions; }
const std::vector<ModCatalog::ExpressionDef>& ModCatalog::Expressions() { return g_expressions; }

const ModCatalog::SubsystemDef* ModCatalog::FindSubsystem(const std::string& id) {
    for (const SubsystemDef& s : g_subsystems) if (s.id == id) return &s;
    return nullptr;
}
const ModCatalog::EventDef* ModCatalog::FindEvent(const std::string& id) {
    for (const EventDef& e : g_events) if (e.id == id) return &e;
    return nullptr;
}
const ModCatalog::ConditionDef* ModCatalog::FindCondition(const std::string& id) {
    for (const ConditionDef& c : g_conditions) if (c.id == id) return &c;
    return nullptr;
}
const ModCatalog::ActionDef* ModCatalog::FindAction(const std::string& id) {
    for (const ActionDef& a : g_actions) if (a.id == id) return &a;
    return nullptr;
}
const ModCatalog::ExpressionDef* ModCatalog::FindExpression(const std::string& id) {
    for (const ExpressionDef& e : g_expressions) if (e.id == id) return &e;
    return nullptr;
}

namespace {

const ModCatalog::ParamDef* FindParamInList(const std::vector<ModCatalog::ParamDef>& allDefs, const std::string& key) {
    for (const ModCatalog::ParamDef& p : allDefs) {
        if (p.key == key) return &p;
    }
    return nullptr;
}

} // namespace

bool ModCatalog::IsWhenMatched(const ParamDef& def, const std::vector<ParamDef>& allDefs, const std::vector<ModLogic::Param>& args) {
    if (def.whenParam.empty()) return true;
    const ParamDef* ctrl = FindParamInList(allDefs, def.whenParam);
    const std::string val = ModLogic::ParamValueOr(args, def.whenParam, ctrl ? ctrl->preset : std::string());
    const bool inList = std::find(def.whenValues.begin(), def.whenValues.end(), val) != def.whenValues.end();
    bool isCustom = false;
    if (!inList && def.whenCustom && ctrl && !ctrl->options.empty()) {
        isCustom = std::none_of(ctrl->options.begin(), ctrl->options.end(), [&](const Option& o) { return o.value == val; });
    }
    return def.whenNegate ? (!inList) : (inList || isCustom);
}

bool ModCatalog::IsAutoWhenMatched(const ParamDef& def, const std::vector<ParamDef>& allDefs, const std::vector<ModLogic::Param>& args) {
    if (def.autoWhenParam.empty()) return false;
    const ParamDef* ctrl = FindParamInList(allDefs, def.autoWhenParam);
    const std::string val = ModLogic::ParamValueOr(args, def.autoWhenParam, ctrl ? ctrl->preset : std::string());
    return std::find(def.autoWhenValues.begin(), def.autoWhenValues.end(), val) != def.autoWhenValues.end();
}

bool ModCatalog::IsParamActive(const ParamDef& def, const std::vector<ParamDef>& allDefs, const std::vector<ModLogic::Param>& args) {
    if (!IsWhenMatched(def, allDefs, args)) return false;
    if (!def.optional) return true;
    return ModLogic::HasParam(args, def.key) || IsAutoWhenMatched(def, allDefs, args);
}

bool ModCatalog::CanRemoveOptionalParam(const ParamDef& def, const std::vector<ParamDef>& allDefs, const std::vector<ModLogic::Param>& args) {
    return def.optional && !IsAutoWhenMatched(def, allDefs, args) && ModLogic::HasParam(args, def.key);
}

bool ModCatalog::CanAddOptionalParam(const ParamDef& def, const std::vector<ParamDef>& allDefs, const std::vector<ModLogic::Param>& args) {
    return def.optional && IsWhenMatched(def, allDefs, args) && !IsParamActive(def, allDefs, args);
}

bool ModCatalog::HasAddableOptionalParams(const std::vector<ParamDef>& allDefs, const std::vector<ModLogic::Param>& args) {
    for (const ParamDef& def : allDefs) {
        if (CanAddOptionalParam(def, allDefs, args)) return true;
    }
    return false;
}

bool ModCatalog::HasDependentParams(const std::vector<ParamDef>& allDefs, const std::string& changedKey) {
    for (const ParamDef& def : allDefs) {
        if (def.whenParam == changedKey || def.autoWhenParam == changedKey) return true;
    }
    return false;
}

std::string ModCatalog::EffectiveParamLabelKey(const ParamDef& def, const std::vector<ParamDef>& allDefs, const std::vector<ModLogic::Param>& args) {
    if (!def.labelWhen.empty()) {
        const std::string& ctrlKey = !def.whenParam.empty() ? def.whenParam : def.autoWhenParam;
        if (!ctrlKey.empty()) {
            const ParamDef* ctrl = FindParamInList(allDefs, ctrlKey);
            const std::string val = ModLogic::ParamValueOr(args, ctrlKey, ctrl ? ctrl->preset : std::string());
            for (const LabelWhen& lw : def.labelWhen) {
                if (std::find(lw.values.begin(), lw.values.end(), val) != lw.values.end()) return lw.label;
            }
        }
    }
    return def.label;
}
