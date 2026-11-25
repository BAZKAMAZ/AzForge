#include "ModLogic.h"

#include "ModCatalog.h"
#include "ModProject.h"
#include "PathUtil.h"
#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

bool Fail(std::string* error, const std::string& text) {
    if (error) *error = text;
    return false;
}

std::vector<ModLogic::Param> ReadParams(const json& j) {
    std::vector<ModLogic::Param> out;
    if (!j.is_object()) return out;
    for (auto it = j.begin(); it != j.end(); ++it) {
        out.push_back({ it.key(), it.value().is_string() ? it.value().get<std::string>() : it.value().dump() });
    }
    return out;
}

json WriteParams(const std::vector<ModLogic::Param>& params) {
    json j = json::object();
    for (const ModLogic::Param& p : params) j[p.key] = p.value;
    return j;
}

ModLogic::Rule ReadRule(const json& r) {
    ModLogic::Rule rule;
    rule.event = r.value("event", "");
    rule.veto = r.value("veto", false);
    rule.enabled = r.value("enabled", true);
    rule.group = r.value("group", false);
    rule.name = r.value("name", "");
    rule.collapsed = r.value("collapsed", false);
    if (r.contains("filters")) rule.filters = ReadParams(r["filters"]);
    if (r.contains("conditions") && r["conditions"].is_array()) {
        for (const json& c : r["conditions"]) {
            ModLogic::Condition condition;
            condition.id = c.value("id", "");
            condition.inverted = c.value("inverted", false);
            condition.enabled = c.value("enabled", true);
            if (c.contains("args")) condition.args = ReadParams(c["args"]);
            if (!condition.id.empty()) rule.conditions.push_back(std::move(condition));
        }
    }
    if (r.contains("actions") && r["actions"].is_array()) {
        for (const json& a : r["actions"]) {
            ModLogic::Action action;
            action.id = a.value("id", "");
            action.enabled = a.value("enabled", true);
            if (a.contains("args")) action.args = ReadParams(a["args"]);
            if (!action.id.empty()) rule.actions.push_back(std::move(action));
        }
    }
    if (r.contains("children") && r["children"].is_array()) {
        for (const json& c : r["children"]) rule.children.push_back(ReadRule(c));
    }
    return rule;
}

json WriteRule(const ModLogic::Rule& rule) {
    json jr;
    jr["event"] = rule.event;
    jr["filters"] = WriteParams(rule.filters);
    jr["veto"] = rule.veto;
    jr["enabled"] = rule.enabled;
    if (rule.group) {
        jr["group"] = true;
        jr["name"] = rule.name;
        jr["collapsed"] = rule.collapsed;
    }

    json conditions = json::array();
    for (const ModLogic::Condition& c : rule.conditions) {
        conditions.push_back({
            { "id", c.id },
            { "args", WriteParams(c.args) },
            { "inverted", c.inverted },
            { "enabled", c.enabled }
        });
    }
    jr["conditions"] = conditions;

    json actions = json::array();
    for (const ModLogic::Action& a : rule.actions) {
        actions.push_back({
            { "id", a.id },
            { "args", WriteParams(a.args) },
            { "enabled", a.enabled }
        });
    }
    jr["actions"] = actions;

    if (!rule.children.empty()) {
        json children = json::array();
        for (const ModLogic::Rule& child : rule.children) children.push_back(WriteRule(child));
        jr["children"] = children;
    }
    return jr;
}

} // namespace

std::vector<ModLogic::Sheet> ModLogic::Load(const std::string& projectDir) {
    std::vector<Sheet> sheets;
    std::error_code ec;
    const fs::path dir = PathUtil::MakePath(projectDir) / "logic";
    if (!fs::is_directory(dir, ec)) return sheets;

    std::vector<fs::path> files;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".json") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());

    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        if (!in) continue;
        json j;
        try {
            in >> j;
        } catch (const std::exception&) {
            continue;
        }

        Sheet sheet;
        sheet.file = PathUtil::U8(file);
        sheet.name = j.value("name", PathUtil::U8(file.stem()));

        if (j.contains("vars") && j["vars"].is_array()) {
            for (const json& v : j["vars"]) {
                Var var;
                var.name = v.value("name", "");
                var.type = v.value("type", "int");
                var.value = v.value("value", "0");
                if (!var.name.empty()) sheet.vars.push_back(std::move(var));
            }
        }
        if (j.contains("rules") && j["rules"].is_array()) {
            for (const json& r : j["rules"]) sheet.rules.push_back(ReadRule(r));
        }
        sheets.push_back(std::move(sheet));
    }
    return sheets;
}

bool ModLogic::Save(const Sheet& sheet, std::string* error) {
    if (sheet.file.empty()) return Fail(error, "sheet has no file");

    json j;
    j["name"] = sheet.name;

    json vars = json::array();
    for (const Var& v : sheet.vars) {
        vars.push_back({ { "name", v.name }, { "type", v.type }, { "value", v.value } });
    }
    j["vars"] = vars;

    json rules = json::array();
    for (const Rule& r : sheet.rules) rules.push_back(WriteRule(r));
    j["rules"] = rules;

    std::ofstream out(PathUtil::MakePath(sheet.file), std::ios::binary | std::ios::trunc);
    if (!out) return Fail(error, "cannot open " + sheet.file);
    out << j.dump(4);
    return true;
}

bool ModLogic::Create(const std::string& projectDir, const std::string& name, Sheet& out, std::string* error) {
    if (name.empty()) return Fail(error, "sheet has no name");

    std::error_code ec;
    const fs::path dir = PathUtil::MakePath(projectDir) / "logic";
    fs::create_directories(dir, ec);

    const std::string stem = ModProject::SlugOf(name);
    fs::path file = dir / (stem + ".json");
    for (int n = 2; fs::exists(file, ec); ++n) file = dir / (stem + "_" + std::to_string(n) + ".json");

    out = Sheet{};
    out.file = PathUtil::U8(file);
    out.name = name;
    return Save(out, error);
}

bool ModLogic::Remove(const Sheet& sheet, std::string* error) {
    std::error_code ec;
    fs::remove(PathUtil::MakePath(sheet.file), ec);
    if (ec) return Fail(error, "cannot delete " + sheet.file);
    return true;
}

ModLogic::Rule ModLogic::MakeRule(const std::string& eventId) {
    Rule rule;
    rule.event = eventId;
    if (const ModCatalog::EventDef* def = ModCatalog::FindEvent(eventId)) {
        for (const ModCatalog::ParamDef& p : def->filters) rule.filters.push_back({ p.key, p.preset });
    }
    return rule;
}

ModLogic::Condition ModLogic::MakeCondition(const std::string& conditionId) {
    Condition condition;
    condition.id = conditionId;
    if (const ModCatalog::ConditionDef* def = ModCatalog::FindCondition(conditionId)) {
        for (const ModCatalog::ParamDef& p : def->params) condition.args.push_back({ p.key, p.preset });
    }
    return condition;
}

ModLogic::Action ModLogic::MakeAction(const std::string& actionId) {
    Action action;
    action.id = actionId;
    if (const ModCatalog::ActionDef* def = ModCatalog::FindAction(actionId)) {
        for (const ModCatalog::ParamDef& p : def->params) action.args.push_back({ p.key, p.preset });
    }
    return action;
}

std::string& ModLogic::ParamValue(std::vector<Param>& params, const std::string& key) {
    for (Param& p : params) {
        if (p.key == key) return p.value;
    }
    params.push_back({ key, "" });
    return params.back().value;
}

std::string ModLogic::ParamValueOr(const std::vector<Param>& params, const std::string& key, const std::string& fallback) {
    for (const Param& p : params) {
        if (p.key == key) return p.value;
    }
    return fallback;
}
