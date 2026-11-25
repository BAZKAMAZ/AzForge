#pragma once

#include <string>
#include <vector>

namespace ModLogic {

struct Param {
    std::string key;
    std::string value;
};

struct Condition {
    std::string id;
    std::vector<Param> args;
    bool inverted = false;
    bool enabled = true;
};

struct Action {
    std::string id;
    std::vector<Param> args;
    bool enabled = true;
};

struct Rule {
    std::string event;
    std::vector<Param> filters;
    std::vector<Condition> conditions;
    std::vector<Action> actions;
    std::vector<Rule> children;
    bool veto = false;
    bool enabled = true;

    bool group = false;
    std::string name;
    bool collapsed = false;
};

struct Var {
    std::string name;
    std::string type = "int";
    std::string value = "0";
};

struct Sheet {
    std::string file;
    std::string name;
    std::vector<Var> vars;
    std::vector<Rule> rules;
};

std::vector<Sheet> Load(const std::string& projectDir);
bool Save(const Sheet& sheet, std::string* error = nullptr);
bool Create(const std::string& projectDir, const std::string& name, Sheet& out, std::string* error = nullptr);
bool Remove(const Sheet& sheet, std::string* error = nullptr);

Rule MakeRule(const std::string& eventId);
Action MakeAction(const std::string& actionId);
Condition MakeCondition(const std::string& conditionId);

std::string& ParamValue(std::vector<Param>& params, const std::string& key);
std::string ParamValueOr(const std::vector<Param>& params, const std::string& key, const std::string& fallback = {});

} // namespace ModLogic
