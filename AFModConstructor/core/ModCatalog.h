#pragma once

#include <string>
#include <vector>

namespace ModCatalog {

struct Option {
    std::string value;
    std::string label;
};

struct ParamDef {
    std::string key;
    std::string label;
    std::string kind = "text";
    std::string preset;
    std::string test;
    std::vector<Option> options;
};

struct SubsystemDef {
    std::string id;
    std::string label;
    std::string icon;
    std::string prologue;
};

struct EventDef {
    std::string id;
    std::string label;
    std::string group;
    std::string section;
    std::string call;
    bool canVeto = false;
    std::vector<ParamDef> filters;
    std::vector<std::string> values;

    std::string handler;
    std::string subscribe;
    std::string retDefault = "return;";
    std::string retVeto = "return;";
    std::string frameTest;
    std::string body;
    std::string provides;
};

struct ConditionDef {
    std::string id;
    std::string label;
    std::string group;
    std::string section;
    std::vector<ParamDef> params;
    std::string emitCode;
    std::string loop;
    std::string state;
    std::string stateInit = "0";
};

struct ExpressionDef {
    std::string id;
    std::string label;
    std::string group;
    std::string section;
    std::string text;
    std::string hint;
    std::string kind = "float";
};

struct ActionDef {
    std::string id;
    std::string label;
    std::string call;
    std::string group;
    std::string section;
    std::vector<ParamDef> params;
    std::string emitCode;
    std::string needs;
};

std::string Dir();
bool Load(std::string* error = nullptr);
bool Loaded();
const std::vector<std::string>& Warnings();

const std::vector<SubsystemDef>& Subsystems();
const SubsystemDef* FindSubsystem(const std::string& id);
const std::vector<EventDef>& Events();
const std::vector<ConditionDef>& Conditions();
const std::vector<ActionDef>& Actions();
const std::vector<ExpressionDef>& Expressions();

const EventDef* FindEvent(const std::string& id);
const ConditionDef* FindCondition(const std::string& id);
const ActionDef* FindAction(const std::string& id);
const ExpressionDef* FindExpression(const std::string& id);

} // namespace ModCatalog
