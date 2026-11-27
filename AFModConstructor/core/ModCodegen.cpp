#include "ModCodegen.h"

#include "ModCatalog.h"
#include "PathUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace {

constexpr int MaxTextVar = 256;
constexpr int MaxListVar = 4096;

std::string Escape(const std::string& value) {
    std::string out;
    char previous = 0;
    for (char c : value) {
        if (c == '\\' || c == '"') out += '\\';
        if (c == '?' && previous == '?') out += '\\';
        if (c == '\n' || c == '\r') continue;
        out += c;
        previous = c;
    }
    return out;
}

const std::string* Find(const std::vector<ModLogic::Param>& params, const std::string& key) {
    for (const ModLogic::Param& p : params) {
        if (p.key == key) return &p.value;
    }
    return nullptr;
}

std::string DoublePercent(const std::string& value) {
    std::string out;
    for (char c : value) {
        out += c;
        if (c == '%') out += '%';
    }
    return out;
}

std::string Replace(std::string text, const std::string& what, const std::string& with) {
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + with.size())) {
        text.replace(at, what.size(), with);
    }
    return text;
}

bool IsIdentifier(const std::string& name) {
    if (name.empty() || (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_')) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return true;
}

struct Numbered {
    const ModLogic::Rule* rule;
    std::string number;
};

void Flatten(const std::vector<ModLogic::Rule>& rules, std::vector<Numbered>& out, const std::string& prefix = std::string()) {
    int at = 0;
    for (const ModLogic::Rule& rule : rules) {
        const std::string number = prefix + std::to_string(++at);
        if (!rule.enabled) continue;
        if (rule.group) Flatten(rule.children, out, number + ".");
        else out.push_back({ &rule, number });
    }
}

bool HasExpression(const std::vector<ModLogic::Param>& params) {
    for (const ModLogic::Param& param : params) {
        const std::string& value = param.value;
        if (value.size() > 1 && value[0] == '=' && value[1] != '=') return true;
        if (value.find("{#") != std::string::npos) return true;
        if (value.find("{~") != std::string::npos) return true;
        if (value.find("{$") != std::string::npos) return true;
    }
    return false;
}

void CollectGroups(const ModLogic::Rule& rule, std::set<std::string>& groups) {
    if (const ModCatalog::EventDef* def = ModCatalog::FindEvent(rule.event)) groups.insert(def->group);
    if (HasExpression(rule.filters)) groups.insert("*");

    for (const ModLogic::Condition& condition : rule.conditions) {
        if (const ModCatalog::ConditionDef* def = ModCatalog::FindCondition(condition.id)) groups.insert(def->group);
        if (HasExpression(condition.args)) groups.insert("*");
    }
    for (const ModLogic::Action& action : rule.actions) {
        if (const ModCatalog::ActionDef* def = ModCatalog::FindAction(action.id)) groups.insert(def->group);
        if (HasExpression(action.args)) groups.insert("*");
    }
    for (const ModLogic::Rule& child : rule.children) CollectGroups(child, groups);
}

std::string ContextHint(const std::string& needs) {
    if (needs == "render") return "draws only inside a render event";
    if (needs == "item_style") return "works only inside the item style event";
    if (needs == "loop") return "works only inside a loop";
    if (needs == "token") return "cancels a subscription, so it belongs in a rule whose event subscribes";
    if (needs == "slot_save") return "writes into a save, so it belongs in the event that writes one";
    if (needs == "slot_load") return "reads a save, so it belongs in the event that reads one";
    if (needs == "camera_view") return "works only inside the camera view event";
    if (needs == "particle_trail") return "works only inside the bonus trail spawn event";
    if (needs == "demo_input") return "works only inside the demo input event";
    if (needs == "player_move") return "works only inside the player move-intent event";
    if (needs == "world_event") return "reads world event payload outside a world event";
    if (needs == "player_event") return "reads player event payload outside a player event";
    if (needs == "player_death") return "reads death hook payload outside Before player dies";
    if (needs == "player_frame") return "reads player hook frame outside Drive or State update";
    if (needs == "camera_update") return "reads camera update frame outside Camera update";
    if (needs == "camera_place") return "reads camera placement point outside Camera placing";
    if (needs == "particle_birth") return "reads particle birth kind outside Particle birth";
    if (needs == "particle_think") return "reads particle think delta outside Particle think";
    if (needs == "demo_state") return "reads demo event outside Demo state changed";
    if (needs == "demo_move") return "reads demo move index outside Demo move started";
    if (needs == "slot_event") return "reads save slot outside a slot save/load event";
    if (needs == "bonus_take") return "works only inside a bonus take event";
    if (needs == "bonus_spawn") return "works only inside Before bonus spawns";
    if (needs == "bonus_body") return "works only inside Build or Update of a custom bonus kind";
    if (needs == "bonus_kind_update") return "reads kind update delta outside Update bonus kind";
    if (needs == "inv_slot_event") return "works only inside Inventory slot emptied";
    if (needs == "inv_chain_event") return "works only inside Combo chain grew or broke";
    if (needs == "monster_kill") return "works only inside Before monster dies";
    if (needs == "monster_touch") return "works only inside a monster touch event";
    if (needs == "monster_spawn") return "works only inside Monster spawned in room";
    if (needs == "monster_place") return "works only inside Before placement builds monster";
    if (needs == "monster_think") return "works only inside a monster AI / behaviour tick event";
    if (needs == "monster_body") return "works only inside Build custom monster kind";
    return "out of place";
}

bool HasParamKey(const std::vector<ModCatalog::ParamDef>& defs, const std::string& key) {
    for (const ModCatalog::ParamDef& d : defs) {
        if (d.key == key) return true;
    }
    return false;
}

bool HasArrowAccess(const std::string& code, const std::string& ident) {
    const std::string needle = ident + "->";
    for (size_t pos = code.find(needle); pos != std::string::npos; pos = code.find(needle, pos + 1)) {
        if (pos == 0) return true;
        const unsigned char prev = static_cast<unsigned char>(code[pos - 1]);
        if (!std::isalnum(prev) && prev != '_' && prev != '.') return true;
    }
    return false;
}

std::vector<std::string> HandlerParams(const std::string& declaration) {
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
            if (IsIdentifier(name) && name != "void") names.push_back(name);
        }
        part.clear();
    }
    return names;
}

std::string Wrap(const std::vector<std::string>& loops, const std::string& body, const std::string& indent) {
    if (loops.empty()) return body;
    std::string out;
    std::string pad = indent;
    for (const std::string& loop : loops) {
        out += pad + loop + " {\n";
        pad += "    ";
    }
    out += body;
    for (size_t at = loops.size(); at > 0; --at) {
        pad.erase(pad.size() - 4);
        out += pad + "}\n";
    }
    return out;
}

std::vector<std::string> LoopVars(const ModLogic::Rule& rule) {
    std::vector<std::string> names;
    for (const ModLogic::Condition& condition : rule.conditions) {
        const ModCatalog::ConditionDef* cd = ModCatalog::FindCondition(condition.id);
        if (!cd || cd->loop.empty()) continue;
        const std::string* value = Find(condition.args, "var");
        std::string name = value && !value->empty() ? *value : "i";
        for (const ModCatalog::ParamDef& param : cd->params) {
            if (param.key == "var" && (!value || value->empty())) name = param.preset;
        }
        if (IsIdentifier(name)) names.push_back(name);
    }
    return names;
}

struct Piece {
    std::string text;
    std::string spec;
};

using Values = std::map<std::string, std::vector<Piece>>;

std::string Join(const std::vector<Piece>& chunks, bool quoted) {
    bool code = false;
    for (const Piece& chunk : chunks) code = code || !chunk.spec.empty();

    if (!code) {
        std::string out;
        for (const Piece& chunk : chunks) out += chunk.text;
        return quoted ? "\"" + out + "\"" : out;
    }
    if (chunks.size() == 1 && (!quoted || chunks[0].spec == "%s")) return chunks[0].text;

    std::string format = "\"", arguments;
    for (const Piece& chunk : chunks) {
        if (chunk.spec.empty()) format += DoublePercent(chunk.text);
        else {
            format += chunk.spec;
            arguments += ", " + chunk.text;
        }
    }
    return "mod_text(" + format + "\"" + arguments + ")";
}

std::string Literal(const std::string& body, const Values& values) {
    std::vector<Piece> chunks;
    for (size_t at = 0; at < body.size();) {
        size_t best = std::string::npos;
        const std::vector<Piece>* pieces = nullptr;
        size_t markSize = 0;

        for (const auto& value : values) {
            const size_t pos = body.find(value.first, at);
            if (pos < best) {
                best = pos;
                pieces = &value.second;
                markSize = value.first.size();
            }
        }
        if (!pieces) {
            chunks.push_back({ body.substr(at), {} });
            break;
        }
        if (best > at) chunks.push_back({ body.substr(at, best - at), {} });
        for (const Piece& piece : *pieces) {
            chunks.push_back(piece.spec.empty() ? Piece{ Escape(piece.text), {} } : piece);
        }
        at = best + markSize;
    }
    return Join(chunks, true);
}

// Per-invocation codegen state (thread-safe).
struct GenState {
    std::map<std::string, std::string> vars;
    std::string where;
    std::set<std::string> context;
    std::set<std::string> handlerVars;
    std::string ruleName;
    std::vector<std::string> ruleStates;
    std::string ruleToken;
    std::vector<std::string> ruleTokens;
    std::vector<std::pair<std::string, std::string>> states;
    std::vector<std::string>* warnings = nullptr;

    void Warn(const std::string& text) {
        if (warnings) warnings->push_back(where.empty() ? text : where + ": " + text);
    }

    void CheckSnippetVars(const std::string& code) {
        static const char* const KnownVars[] = {
            "frame", "info", "pframe", "dinfo", "move", "view", "camFrame", "trailDesc",
            "tinfo", "sinfo", "kinfo", "iinfo", "cinfo", "pinfo", "minfo", "mkinfo"
        };
        for (const char* ident : KnownVars) {
            if (HasArrowAccess(code, ident) && handlerVars.count(ident) == 0) {
                Warn(std::string("expression uses '") + ident + "->' which does not exist in this event (use mod_delta / ready expressions instead)");
            }
        }
        for (const ModCatalog::ExpressionDef& ex : ModCatalog::Expressions()) {
            if (ex.needs.empty() || ex.text.empty()) continue;
            if (code.find(ex.text) != std::string::npos && context.count(ex.needs) == 0) {
                Warn(ContextHint(ex.needs) + ": " + ex.id);
            }
        }
    }

    void CheckRawHandlerVars(const std::vector<ModCatalog::ParamDef>& defs, const std::vector<ModLogic::Param>& args) {
        for (const ModLogic::Param& p : args) {
            if (!defs.empty()) {
                const ModCatalog::ParamDef* foundDef = nullptr;
                for (const ModCatalog::ParamDef& d : defs) {
                    if (d.key == p.key) { foundDef = &d; break; }
                }
                if (foundDef && !ModCatalog::IsParamActive(*foundDef, defs, args)) continue;
            }
            const std::string& v = p.value;
            if (v.size() > 1 && v[0] == '=' && v[1] != '=') {
                CheckSnippetVars(v.substr(1));
                continue;
            }
            for (size_t at = 0; at < v.size();) {
                const size_t open = v.find('{', at);
                const size_t close = open == std::string::npos ? std::string::npos : v.find('}', open + 1);
                if (close == std::string::npos) break;
                if (open + 2 < close && (v[open + 1] == '#' || v[open + 1] == '~' || v[open + 1] == '$')) {
                    CheckSnippetVars(v.substr(open + 2, close - open - 2));
                }
                at = close + 1;
            }
        }
    }

    std::string SpecOf(const std::string& code) const {
        const auto found = vars.find(code);
        if (found == vars.end()) return "%s";
        if (found->second == "float") return "%g";
        if (found->second == "text") return "%s";
        if (found->second == "list") return "";
        return "%d";
    }

    std::vector<Piece> PiecesOf(const ModCatalog::ParamDef& def, const std::string& text) {
        std::vector<Piece> pieces;
        if (text.size() > 1 && text[0] == '=' && text[1] != '=') {
            const std::string code = text.substr(1);
            pieces.push_back({ code, SpecOf(code) });
            return pieces;
        }
        if (def.kind == "number" || text.find('{') == std::string::npos) {
            pieces.push_back({ text, {} });
            return pieces;
        }

        std::string plain;
        for (size_t at = 0; at < text.size();) {
            const size_t open = text.find('{', at);
            const size_t close = open == std::string::npos ? std::string::npos : text.find('}', open + 1);
            if (open == std::string::npos || close == std::string::npos) {
                plain += text.substr(at);
                break;
            }
            const size_t inner = text.find('{', open + 1);
            if (inner < close) {
                plain += text.substr(at, inner - at);
                at = inner;
                continue;
            }

            const std::string name = text.substr(open + 1, close - open - 1);
            std::string code, spec;
            if (name.size() > 1 && (name[0] == '#' || name[0] == '~' || name[0] == '$')) {
                const std::string rest = name.substr(1);
                if (name[0] == '#') { code = "(int)(" + rest + ")"; spec = "%d"; }
                else if (name[0] == '~') { code = "(double)(" + rest + ")"; spec = "%g"; }
                else { code = rest; spec = "%s"; }
            } else {
                const auto found = vars.find(name);
                if (found != vars.end() && found->second == "list") {
                    Warn("a list has no text of its own, name an element of it: " + name);
                } else if (found != vars.end()) {
                    code = name;
                    spec = found->second == "float" ? "%g" : (found->second == "text" ? "%s" : "%d");
                }
            }

            if (spec.empty()) {
                if (IsIdentifier(name)) Warn("no such variable: " + name);
                plain += text.substr(at, close - at + 1);
                at = close + 1;
                continue;
            }

            plain += text.substr(at, open - at);
            if (!plain.empty()) {
                pieces.push_back({ plain, {} });
                plain.clear();
            }
            pieces.push_back({ code, spec });
            at = close + 1;
        }
        if (!plain.empty() || pieces.empty()) pieces.push_back({ plain, {} });
        return pieces;
    }

    std::string Substitute(const std::string& tmpl, const std::vector<ModCatalog::ParamDef>& defs, const std::vector<ModLogic::Param>& args) {
        Values values;
        for (const ModCatalog::ParamDef& def : defs) {
            const bool active = ModCatalog::IsParamActive(def, defs, args);
            const std::string* value = active ? Find(args, def.key) : nullptr;
            std::string text = value ? *value : def.preset;
            if (text.empty()) text = (def.kind == "number") ? "0" : "";

            const size_t first = text.find_first_not_of(" \t");
            const bool written = text.size() > 1 && text[0] == '=' && text[1] != '=';
            if (def.kind == "number" && !written && first != std::string::npos && text[first] == '"') {
                Warn("text where a number belongs, " + def.key + ": " + text);
                text = "0";
            }
            values["%" + def.key + "%"] = PiecesOf(def, text);
        }

        std::string out;
        for (size_t at = 0; at < tmpl.size();) {
            if (tmpl[at] == '"') {
                size_t end = at + 1;
                while (end < tmpl.size() && tmpl[end] != '"') end += tmpl[end] == '\\' ? 2 : 1;
                end = std::min(end, tmpl.size());
                out += Literal(tmpl.substr(at + 1, end - at - 1), values);
                at = end + 1;
                continue;
            }
            const auto found = std::find_if(values.begin(), values.end(), [&](const auto& v) {
                return tmpl.compare(at, v.first.size(), v.first) == 0;
            });
            if (found == values.end()) {
                out += tmpl[at++];
                continue;
            }
            out += Join(found->second, false);
            at += found->first.size();
        }
        return out;
    }

    std::string StateOf(const ModCatalog::ConditionDef& def) {
        const std::string name = ruleName + "_s" + std::to_string(states.size());
        states.push_back({ name, def.state + " " + name + " = " + def.stateInit });
        return name;
    }

    std::vector<std::string> LoopsOf(const ModLogic::Rule& rule) {
        std::vector<std::string> loops;
        for (const ModLogic::Condition& condition : rule.conditions) {
            if (!condition.enabled) continue;
            const ModCatalog::ConditionDef* cd = ModCatalog::FindCondition(condition.id);
            if (cd && !cd->loop.empty()) loops.push_back(Substitute(cd->loop, cd->params, condition.args));
        }
        return loops;
    }

    std::string ConditionsExpr(const ModLogic::Rule& rule) {
        std::string expr;
        bool once = false;
        for (const ModLogic::Condition& condition : rule.conditions) {
            if (!condition.enabled) continue;
            const ModCatalog::ConditionDef* cd = ModCatalog::FindCondition(condition.id);
            if (!cd) {
                Warn("no such condition: " + condition.id);
                continue;
            }
            if (!cd->loop.empty()) continue;
            if (cd->state.empty() && !HasParamKey(cd->params, "state") && cd->emitCode.find("%state%") != std::string::npos) {
                once = true;
                continue;
            }
            if (!cd->needs.empty() && context.count(cd->needs) == 0) {
                Warn(ContextHint(cd->needs) + ": " + cd->id);
            }
            CheckRawHandlerVars(cd->params, condition.args);
            std::string emit = cd->emitCode;
            if (!cd->state.empty()) emit = Replace(emit, "%state%", StateOf(*cd));

            const std::string one = "(" + Substitute(emit, cd->params, condition.args) + ")";
            if (!expr.empty()) expr += " && ";
            expr += condition.inverted ? "!" + one : one;
        }
        if (!once) return expr;

        const std::string flag = ruleName + "_once" + std::to_string(ruleStates.size());
        ruleStates.push_back(flag);
        return "trigger_once(&" + flag + ", " + (expr.empty() ? "1" : expr) + ")";
    }

    struct LoopScope {
        GenState& gen;
        std::vector<std::string> added;
        bool loop = false;

        LoopScope(GenState& g, const ModLogic::Rule& rule) : gen(g) {
            for (const std::string& name : LoopVars(rule)) {
                if (gen.vars.count(name) != 0) continue;
                gen.vars[name] = "int";
                added.push_back(name);
            }
            for (const ModLogic::Condition& condition : rule.conditions) {
                const ModCatalog::ConditionDef* def = ModCatalog::FindCondition(condition.id);
                if (def && !def->loop.empty()) loop = true;
            }
            if (loop) loop = gen.context.insert("loop").second;
        }
        ~LoopScope() {
            for (const std::string& name : added) gen.vars.erase(name);
            if (loop) gen.context.erase("loop");
        }
    };

    std::string ActionsCode(const ModLogic::Rule& rule, const std::string& indent) {
        std::string body;
        for (const ModLogic::Action& action : rule.actions) {
            if (!action.enabled) continue;
            const ModCatalog::ActionDef* ad = ModCatalog::FindAction(action.id);
            if (!ad) { Warn("no such action: " + action.id); continue; }
            if (ad->emitCode.empty()) { Warn("action has no code: " + action.id); continue; }
            if (!ad->needs.empty() && context.count(ad->needs) == 0) {
                Warn(ContextHint(ad->needs) + ": " + action.id);
            }
            CheckRawHandlerVars(ad->params, action.args);
            std::string emit = ad->emitCode;
            if (emit.find("%token%") != std::string::npos) {
                emit = Replace(emit, "%token%", ruleToken.empty() ? "0" : ruleToken);
            }
            body += indent + Substitute(emit, ad->params, action.args) + "\n";
        }
        return body;
    }

    std::string ChildrenCode(const ModLogic::Rule& rule, const std::string& indent) {
        std::string body;
        for (const ModLogic::Rule& child : rule.children) {
            if (!child.enabled) continue;
            if (!child.group && !child.event.empty()) {
                Warn("sub-event cannot subscribe to event '" + child.event + "' (move it to top level)");
            }
            const LoopScope scope(*this, child);
            const std::vector<std::string> loops = LoopsOf(child);
            const std::string test = ConditionsExpr(child);
            const std::string pad = indent + "    " + std::string(loops.size() * 4, ' ');
            body += indent + "if (" + (test.empty() ? "1" : test) + ") {\n";
            body += Wrap(loops, ActionsCode(child, pad) + ChildrenCode(child, pad), indent + "    ");
            body += indent + "}\n";
        }
        return body;
    }

    std::string RuleBody(const ModLogic::Rule& rule, const std::string& ret) {
        const LoopScope scope(*this, rule);
        const std::vector<std::string> loops = LoopsOf(rule);
        const std::string pad = std::string(4 + loops.size() * 4, ' ');
        std::string body;
        const std::string conditions = ConditionsExpr(rule);
        if (!conditions.empty()) body += "    if (!(" + conditions + ")) " + ret + "\n";
        return body + Wrap(loops, ActionsCode(rule, pad) + ChildrenCode(rule, pad), "    ");
    }

    std::string FrameBlock(const ModLogic::Rule& rule, const std::string& test) {
        const LoopScope scope(*this, rule);
        const std::vector<std::string> loops = LoopsOf(rule);
        const std::string pad = std::string(8 + loops.size() * 4, ' ');
        const std::string body = ActionsCode(rule, pad) + ChildrenCode(rule, pad);
        return "    if (" + test + ") {\n" + Wrap(loops, body, "        ") + "    }\n";
    }

    void RegisterVars(const std::vector<ModLogic::Sheet>& sheets, std::string& outDecls) {
        std::set<std::string> declared;
        for (const ModLogic::Sheet& sheet : sheets) {
            for (const ModLogic::Var& var : sheet.vars) {
                if (!IsIdentifier(var.name)) {
                    Warn("variable name is not usable in code: " + var.name);
                    continue;
                }
                if (!declared.insert(var.name).second) continue;

                std::string kind = var.type;
                if (kind != "float" && kind != "text" && kind != "bool" && kind != "list") {
                    kind = var.value.find('.') != std::string::npos ? "float" : "int";
                }
                if (kind == "list") {
                    int length = std::atoi(var.value.c_str());
                    if (length < 1) length = 16;
                    if (length > MaxListVar) length = MaxListVar;
                    vars[var.name] = "list";
                    outDecls += "static float " + var.name + "[" + std::to_string(length) + "] = {0};\n";
                    continue;
                }
                if (kind == "text") {
                    vars[var.name] = "text";
                    outDecls += "static char " + var.name + "[" + std::to_string(MaxTextVar) + "] = \"" +
                                Escape(var.value) + "\";\n";
                    continue;
                }
                if (kind == "bool") {
                    vars[var.name] = "int";
                    const bool on = var.value == "1" || var.value == "true";
                    outDecls += "static int " + var.name + " = " + (on ? "1" : "0") + ";\n";
                    continue;
                }
                const bool real = kind == "float";
                vars[var.name] = real ? "float" : "int";
                std::string value = var.value.empty() ? "0" : var.value;
                if (real && value.find('.') == std::string::npos) value += ".0";
                outDecls += std::string("static ") + (real ? "float " : "int ") + var.name + " = " +
                            value + (real ? "f" : "") + ";\n";
            }
        }
    }
};

} // namespace

std::set<std::string> ModCodegen::UsedGroups(const std::vector<ModLogic::Sheet>& sheets) {
    std::set<std::string> groups;
    for (const ModLogic::Sheet& sheet : sheets) {
        std::vector<Numbered> flat;
        Flatten(sheet.rules, flat);
        for (const Numbered& item : flat) CollectGroups(*item.rule, groups);
    }
    return groups;
}

std::string ModCodegen::Generate(const std::vector<ModLogic::Sheet>& sheets, std::vector<std::string>* warnings) {
    GenState gen;
    gen.warnings = warnings;

    std::string code =
        "/* Generated by AFModConstructor */\n"
        "#include \"_as-cm10.h\"\n"
        "\n";

    gen.RegisterVars(sheets, code);
    code += "\n";

    std::string handlers, subscribe, rebind, frame, started, stopping;
    int index = 0;

    for (const ModLogic::Sheet& sheet : sheets) {
        std::vector<Numbered> flat;
        Flatten(sheet.rules, flat);

        for (const Numbered& item : flat) {
            const ModLogic::Rule& rule = *item.rule;
            gen.ruleName = "rule_" + std::to_string(index);
            gen.where = sheet.name + " / " + item.number;
            const ModCatalog::EventDef* def = ModCatalog::FindEvent(rule.event);
            if (!def && !rule.event.empty()) {
                gen.Warn("no such event: " + rule.event);
                continue;
            }

            const std::string name = "rule_" + std::to_string(index++);

            gen.ruleToken.clear();
            if (def && def->subscribe.find("%token%") != std::string::npos) {
                gen.ruleToken = name + "_token";
                gen.ruleTokens.push_back(gen.ruleToken);
            }

            gen.context.clear();
            gen.handlerVars.clear();
            if (!gen.ruleToken.empty()) gen.context.insert("token");
            if (!def || !def->frameTest.empty()) {
                gen.handlerVars.insert("frame");
            } else if (!def->handler.empty()) {
                for (const std::string& hp : HandlerParams(def->handler)) gen.handlerVars.insert(hp);
            }
            if (def) {
                std::string part;
                for (size_t at = 0; at <= def->provides.size(); ++at) {
                    const char c = at < def->provides.size() ? def->provides[at] : ',';
                    if (c != ',') { if (c != ' ') part += c; continue; }
                    if (!part.empty()) {
                        gen.context.insert(part);
                        if (part == "slot_save" || part == "slot_load") gen.context.insert("slot_event");
                    }
                    part.clear();
                }
                gen.CheckRawHandlerVars(def->filters, rule.filters);
            }

            if (!def) {
                const std::string conditions = gen.ConditionsExpr(rule);
                frame += "    /* " + sheet.name + " */\n";
                frame += gen.FrameBlock(rule, conditions.empty() ? std::string("1") : conditions);
                continue;
            }
            if (def->body == "init" || def->body == "shutdown") {
                const std::string conditions = gen.ConditionsExpr(rule);
                std::string& into = def->body == "init" ? started : stopping;
                into += "    /* " + sheet.name + ": " + def->id + " */\n";
                into += gen.FrameBlock(rule, conditions.empty() ? std::string("1") : conditions);
                continue;
            }
            if (!def->frameTest.empty()) {
                std::string test = gen.Substitute(def->frameTest, def->filters, rule.filters);
                const std::string conditions = gen.ConditionsExpr(rule);
                if (!conditions.empty()) test += " && " + conditions;
                frame += "    /* " + sheet.name + ": " + def->id + " */\n";
                frame += gen.FrameBlock(rule, test);
                continue;
            }
            if (def->handler.empty() || def->subscribe.empty()) {
                gen.Warn("event cannot generate code: " + def->id);
                continue;
            }

            const std::string ret = rule.veto ? def->retVeto : def->retDefault;
            handlers += "/* " + sheet.name + ": " + def->id + " */\n";
            handlers += Replace(def->handler, "%H%", name) + " {\n";
            const std::vector<std::string> hParams = HandlerParams(def->handler);
            for (const std::string& param : hParams) {
                handlers += "    (void)" + param + ";\n";
            }
            if (def->handler.find("const AmodFrame* frame") != std::string::npos) {
                handlers += "    if (frame != 0) { mod_delta = frame->delta; mod_elapsed = frame->elapsed; }\n";
            }
            for (const ModCatalog::ParamDef& filter : def->filters) {
                if (filter.test.empty()) continue;
                const std::string* value = Find(rule.filters, filter.key);
                if (!value || value->empty()) continue;
                handlers += "    " + Replace(filter.test, "%v%", Escape(*value)) + "\n";
            }
            handlers += gen.RuleBody(rule, def->retDefault);
            handlers += "    " + ret + "\n}\n\n";

            std::string subscription = Replace(def->subscribe, "%H%", name);
            if (!gen.ruleToken.empty()) subscription = Replace(subscription, "%token%", gen.ruleToken);

            const std::string line = "    " + gen.Substitute(subscription, def->filters, rule.filters) + "\n";
            if (line.find("ctrl_index") != std::string::npos) rebind += line;
            else subscribe += line;
        }
    }

    for (const std::string& flag : gen.ruleStates) code += "static int " + flag + " = 0;\n";
    if (!gen.ruleStates.empty()) code += "\n";

    for (const std::string& token : gen.ruleTokens) code += "static AmodToken " + token + ";\n";
    if (!gen.ruleTokens.empty()) code += "\n";

    for (const auto& state : gen.states) code += "static " + state.second + ";\n";
    if (!gen.states.empty()) code += "\n";

    code += handlers;

    if (!rebind.empty()) {
        code += "static void __cdecl bind_room(const AmodWorldEventInfo* info, void* user) {\n"
                "    (void)info; (void)user;\n" + rebind + "}\n\n";
    }

    code += "AMOD_EXPORT int __cdecl AmodInit(const AmodApi* api) {\n"
            "    if (api == 0 || api->apiVersion < AMOD_API_VERSION || api->structSize < sizeof(AmodApi))\n"
            "        return AMOD_ERROR_API_VERSION;\n"
            "    g_api = api;\n";
    code += subscribe;
    if (!rebind.empty()) {
        code += "    if (API->world) API->world->AddEventHandler(SELF, AMOD_WORLD_EVENT_ROOM_CHANGED, bind_room, 0);\n";
    }
    code += started;
    code += "    return AMOD_OK;\n}\n\n";

    code += "AMOD_EXPORT void __cdecl AmodShutdown(void) {\n" + stopping + "}\n\n";
    code += "AMOD_EXPORT void __cdecl AmodUpdate(const AmodFrame* frame) {\n"
            "    mod_delta = frame != 0 ? frame->delta : 0.0f;\n"
            "    mod_elapsed = frame != 0 ? frame->elapsed : mod_elapsed;\n"
            "    (void)frame;\n";
    code += frame;
    code += "}\n";
    return code;
}

std::string ModCodegen::GenerateRulePreview(const ModLogic::Sheet& sheet, const ModLogic::Rule& rule, std::vector<std::string>* warnings) {
    if (rule.group) {
        return "/* Group \"" + rule.name + "\" (" + std::to_string(rule.children.size()) +
               " child item(s)) - flattened during build */\n";
    }
    if (!rule.enabled) {
        return "/* Rule disabled (D) - excluded from build */\n";
    }

    GenState gen;
    gen.warnings = warnings;
    std::string dummyDecls;
    gen.RegisterVars({ sheet }, dummyDecls);
    gen.ruleName = "preview";
    gen.where = sheet.name;
    gen.context.insert({
        "render", "item_style", "loop", "token", "slot_save", "slot_load", "slot_event",
        "camera_view", "camera_update", "camera_place", "particle_birth", "particle_think",
        "particle_trail", "demo_state", "demo_move", "demo_input", "world_event",
        "player_event", "player_death", "player_frame", "player_move",
        "bonus_take", "bonus_spawn", "bonus_body", "bonus_kind_update",
        "inv_slot_event", "inv_chain_event",
        "monster_kill", "monster_touch", "monster_spawn", "monster_place",
        "monster_think", "monster_body"
    });

    const ModCatalog::EventDef* def = ModCatalog::FindEvent(rule.event);
    if (!def || !def->frameTest.empty()) {
        gen.handlerVars.insert("frame");
    } else if (!def->handler.empty()) {
        for (const std::string& hp : HandlerParams(def->handler)) gen.handlerVars.insert(hp);
    }
    if (!def) {
        const std::string conditions = gen.ConditionsExpr(rule);
        std::string out;
        for (const auto& s : gen.states) out += "static " + s.second + ";\n";
        out += gen.FrameBlock(rule, conditions.empty() ? std::string("1") : conditions);
        return out;
    }

    if (def->body == "init" || def->body == "shutdown" || !def->frameTest.empty()) {
        std::string test = def->frameTest.empty()
            ? std::string("1")
            : gen.Substitute(def->frameTest, def->filters, rule.filters);
        const std::string conditions = gen.ConditionsExpr(rule);
        if (!conditions.empty()) test = (test == "1") ? conditions : (test + " && " + conditions);
        return "/* " + def->id + " */\n" + gen.FrameBlock(rule, test);
    }

    std::string out;
    out += "/* Subscribe: " + gen.Substitute(Replace(def->subscribe, "%H%", "preview"), def->filters, rule.filters) + " */\n";
    out += Replace(def->handler, "%H%", "preview") + " {\n";
    out += gen.RuleBody(rule, def->retDefault);
    out += "    " + (rule.veto ? def->retVeto : def->retDefault) + "\n}\n";
    return out;
}

bool ModCodegen::Write(const std::string& projectDir, const std::vector<ModLogic::Sheet>& sheets, std::string* outPath, std::string* error, std::vector<std::string>* warnings) {
    std::vector<std::string> collected;
    const std::string code = Generate(sheets, &collected);
    if (warnings) *warnings = collected;

    std::error_code ec;
    const fs::path dir = PathUtil::MakePath(projectDir) / "src";
    fs::create_directories(dir, ec);
    const fs::path file = dir / "mod_generated.c";

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot open " + PathUtil::U8(file);
        return false;
    }
    out << code;
    if (outPath) *outPath = PathUtil::U8(file);
    return true;
}
