#include "src/Api/ShaderAsm.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace Forge
{
    namespace
    {
        constexpr uint32_t kParam = 0x80000000u;
        constexpr uint32_t kNoSwizzle = 0x00E40000u; // .xyzw

        // Register files.
        constexpr uint32_t kTemp = 0u << 28;
        constexpr uint32_t kInput = 1u << 28;
        constexpr uint32_t kConst = 2u << 28;
        constexpr uint32_t kAddrTex = 3u << 28; // a0 in VS, tN in PS
        constexpr uint32_t kRastOut = 4u << 28;
        constexpr uint32_t kAttrOut = 5u << 28;
        constexpr uint32_t kTexcrdOut = 6u << 28;

        struct Op
        {
            const char* name;
            uint32_t code;
            int sources;
        };

        const Op kOps[] =
        {
            { "mov", 1, 1 }, { "add", 2, 2 }, { "sub", 3, 2 }, { "mad", 4, 3 }, { "mul", 5, 2 },
            { "rcp", 6, 1 }, { "rsq", 7, 1 }, { "dp3", 8, 2 }, { "dp4", 9, 2 }, { "min", 10, 2 },
            { "max", 11, 2 }, { "slt", 12, 2 }, { "sge", 13, 2 }, { "frc", 19, 1 },
            { "lrp", 18, 3 }, { "texcoord", 64, 0 }, { "tex", 66, 0 },
        };

        std::string Lower(const std::string& text)
        {
            std::string out = text;
            for (char& at : out) { at = static_cast<char>(tolower(static_cast<unsigned char>(at))); }
            return out;
        }

        bool Fault(std::string& error, int line, const char* what, const std::string& detail)
        {
            char head[64] = {};
            sprintf_s(head, "line %d: ", line);
            error = head;
            error += what;

            if (!detail.empty())
            {
                error += " - ";
                error += detail;
            }
            return false;
        }

        // Parses a register name and returns its file and index.
        bool Register(const std::string& name, uint32_t& file, int& number, bool pixel)
        {
            const std::string low = Lower(name);

            auto digits = [](const std::string& text, size_t from) -> int
            {
                int value = 0;
                bool any = false;
                for (size_t at = from; at < text.size() && isdigit(static_cast<unsigned char>(text[at])); ++at)
                {
                    value = value * 10 + (text[at] - '0');
                    any = true;
                }
                return any ? value : 0;
            };

            if (low == "opos") { file = kRastOut; number = 0; return true; }
            if (low == "ofog") { file = kRastOut; number = 1; return true; }
            if (low.rfind("od", 0) == 0) { file = kAttrOut; number = digits(low, 2); return true; }
            if (low.rfind("ot", 0) == 0) { file = kTexcrdOut; number = digits(low, 2); return true; }

            if (low[0] == 'v') { file = kInput; number = digits(low, 1); return true; }
            if (low[0] == 'c') { file = kConst; number = digits(low, 1); return true; }
            if (low[0] == 'r') { file = kTemp; number = digits(low, 1); return true; }
            if (low[0] == 't') { file = kAddrTex; number = digits(low, 1); return true; }
            if (low == "a0") { file = kAddrTex; number = 0; return true; }

            (void)pixel;
            return false;
        }

        // Parses a component letter.
        bool Component(char at, uint32_t& value)
        {
            switch (tolower(static_cast<unsigned char>(at)))
            {
                case 'x': case 'r': value = 0; return true;
                case 'y': case 'g': value = 1; return true;
                case 'z': case 'b': value = 2; return true;
                case 'w': case 'a': value = 3; return true;
            }
            return false;
        }

        // Builds a destination write mask.
        bool WriteMask(const std::string& swizzle, uint32_t& mask)
        {
            if (swizzle.empty())
            {
                mask = 0x000F0000u;
                return true;
            }

            mask = 0;
            for (char at : swizzle)
            {
                uint32_t which = 0;
                if (!Component(at, which))
                {
                    return false;
                }
                mask |= (0x00010000u << which);
            }
            return true;
        }

        // Builds a source swizzle.
        bool Swizzle(const std::string& swizzle, uint32_t& bits)
        {
            if (swizzle.empty())
            {
                bits = kNoSwizzle;
                return true;
            }

            uint32_t part[4] = {};

            if (swizzle.size() == 1)
            {
                uint32_t one = 0;
                if (!Component(swizzle[0], one))
                {
                    return false;
                }
                part[0] = part[1] = part[2] = part[3] = one;
            }
            else if (swizzle.size() == 4)
            {
                for (int at = 0; at < 4; ++at)
                {
                    if (!Component(swizzle[at], part[at]))
                    {
                        return false;
                    }
                }
            }
            else
            {
                return false;
            }

            bits = (part[0] << 16) | (part[1] << 18) | (part[2] << 20) | (part[3] << 22);
            return true;
        }

        // Splits a register token into name, swizzle and negate.
        void Split(const std::string& token, std::string& name, std::string& swizzle, bool& negate)
        {
            std::string body = token;
            negate = false;

            if (!body.empty() && body[0] == '-')
            {
                negate = true;
                body = body.substr(1);
            }

            const size_t dot = body.find('.');
            if (dot == std::string::npos)
            {
                name = body;
                swizzle.clear();
            }
            else
            {
                name = body.substr(0, dot);
                swizzle = body.substr(dot + 1);
            }
        }

        std::vector<std::string> Words(const std::string& line)
        {
            std::vector<std::string> out;
            std::string word;

            for (char at : line)
            {
                if (at == ',' || isspace(static_cast<unsigned char>(at)))
                {
                    if (!word.empty()) { out.push_back(word); word.clear(); }
                }
                else
                {
                    word += at;
                }
            }
            if (!word.empty()) { out.push_back(word); }
            return out;
        }
    }

    bool AssembleShader(const std::string& source, std::vector<uint32_t>& tokens, std::string& error)
    {
        tokens.clear();

        std::string line;
        int number = 0;
        bool haveVersion = false;
        bool pixel = false;

        std::string text = source;
        text += "\n";

        for (size_t at = 0; at < text.size(); ++at)
        {
            const char letter = text[at];

            if (letter != '\n')
            {
                line += letter;
                continue;
            }

            ++number;

            const size_t slash = line.find("//");
            if (slash != std::string::npos) { line = line.substr(0, slash); }
            const size_t semi = line.find(';');
            if (semi != std::string::npos) { line = line.substr(0, semi); }

            const std::vector<std::string> words = Words(line);
            line.clear();

            if (words.empty())
            {
                continue;
            }

            const std::string head = Lower(words[0]);

            if (head.rfind("vs.", 0) == 0 || head.rfind("ps.", 0) == 0)
            {
                pixel = head[0] == 'p';
                haveVersion = true;

                int major = 1;
                int minor = 1;
                sscanf_s(head.c_str() + 3, "%d.%d", &major, &minor);

                tokens.push_back((pixel ? 0xFFFF0000u : 0xFFFE0000u) | (static_cast<uint32_t>(major) << 8) | static_cast<uint32_t>(minor));
                continue;
            }

            if (!haveVersion)
            {
                return Fault(error, number, "a shader opens with vs.1.1 or ps.1.1", head);
            }

            const Op* op = nullptr;
            for (const Op& one : kOps)
            {
                if (head == one.name)
                {
                    op = &one;
                    break;
                }
            }

            if (op == nullptr)
            {
                return Fault(error, number, "unknown instruction", head);
            }

            const int operands = op->sources + 1;

            if (static_cast<int>(words.size()) != operands + 1)
            {
                return Fault(error, number, "wrong number of operands for", head);
            }

            tokens.push_back(op->code);

            // Parses the destination register and write mask.
            {
                std::string name;
                std::string swizzle;
                bool negate = false;
                Split(words[1], name, swizzle, negate);

                uint32_t file = 0;
                int reg = 0;
                uint32_t mask = 0;

                if (!Register(name, file, reg, pixel) || !WriteMask(swizzle, mask))
                {
                    return Fault(error, number, "bad destination", words[1]);
                }

                tokens.push_back(kParam | file | mask | static_cast<uint32_t>(reg));
            }

            for (int which = 0; which < op->sources; ++which)
            {
                std::string name;
                std::string swizzle;
                bool negate = false;
                Split(words[2 + which], name, swizzle, negate);

                uint32_t file = 0;
                int reg = 0;
                uint32_t bits = 0;

                if (!Register(name, file, reg, pixel) || !Swizzle(swizzle, bits))
                {
                    return Fault(error, number, "bad source", words[2 + which]);
                }

                const uint32_t modifier = negate ? (1u << 24) : 0u;
                tokens.push_back(kParam | file | modifier | bits | static_cast<uint32_t>(reg));
            }
        }

        if (!haveVersion)
        {
            return Fault(error, 0, "the shader is empty", "");
        }

        tokens.push_back(0x0000FFFFu); // end
        return true;
    }
}