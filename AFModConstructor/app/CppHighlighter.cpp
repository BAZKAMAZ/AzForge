#include "CppHighlighter.h"

#include "PathUtil.h"
#include "SharedConfig.h"
#include "json.hpp"

#include <QFileInfo>
#include <QTimer>

#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

QTextCharFormat MakeFmt(const char* hex, bool bold = false, bool italic = false) {
    QTextCharFormat f;
    f.setForeground(QColor(QString::fromLatin1(hex)));
    if (bold) f.setFontWeight(QFont::Bold);
    if (italic) f.setFontItalic(true);
    return f;
}

void ApplyStyleJson(const json& styles, const char* key, QTextCharFormat& target) {
    if (!styles.is_object() || !styles.contains(key)) return;
    const json& v = styles[key];
    if (v.is_string()) {
        const QColor col(QString::fromStdString(v.get<std::string>()));
        if (col.isValid()) target.setForeground(col);
        return;
    }
    if (!v.is_object()) return;
    if (v.contains("color") && v["color"].is_string()) {
        const QColor col(QString::fromStdString(v["color"].get<std::string>()));
        if (col.isValid()) target.setForeground(col);
    }
    if (v.contains("bold") && v["bold"].is_boolean()) {
        target.setFontWeight(v["bold"].get<bool>() ? QFont::Bold : QFont::Normal);
    }
    if (v.contains("italic") && v["italic"].is_boolean()) {
        target.setFontItalic(v["italic"].get<bool>());
    }
    if (v.contains("underline") && v["underline"].is_boolean()) {
        target.setFontUnderline(v["underline"].get<bool>());
    }
}

bool IsIdentStart(QChar ch) {
    const ushort u = ch.unicode();
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_';
}

bool IsIdentChar(QChar ch) {
    const ushort u = ch.unicode();
    return IsIdentStart(ch) || (u >= '0' && u <= '9');
}

bool IsHexDigit(QChar ch) {
    const ushort u = ch.unicode();
    return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
}

bool IsAllCapsMacro(const QString& id) {
    if (id.size() < 2) return false;
    bool hasLetter = false;
    for (QChar ch : id) {
        const ushort u = ch.unicode();
        if (u >= 'A' && u <= 'Z') { hasLetter = true; continue; }
        if ((u >= '0' && u <= '9') || u == '_') continue;
        return false;
    }
    return hasLetter;
}

const std::unordered_set<QString>& Keywords() {
    static const std::unordered_set<QString> set = {
        QStringLiteral("alignas"), QStringLiteral("alignof"), QStringLiteral("auto"),
        QStringLiteral("break"), QStringLiteral("case"), QStringLiteral("catch"),
        QStringLiteral("class"), QStringLiteral("const"), QStringLiteral("consteval"),
        QStringLiteral("constexpr"), QStringLiteral("constinit"), QStringLiteral("const_cast"),
        QStringLiteral("continue"), QStringLiteral("default"), QStringLiteral("delete"),
        QStringLiteral("do"), QStringLiteral("dynamic_cast"), QStringLiteral("else"),
        QStringLiteral("enum"), QStringLiteral("explicit"), QStringLiteral("extern"),
        QStringLiteral("false"), QStringLiteral("final"), QStringLiteral("for"),
        QStringLiteral("friend"), QStringLiteral("goto"), QStringLiteral("if"),
        QStringLiteral("inline"), QStringLiteral("mutable"), QStringLiteral("namespace"),
        QStringLiteral("new"), QStringLiteral("noexcept"), QStringLiteral("nullptr"),
        QStringLiteral("operator"), QStringLiteral("override"), QStringLiteral("private"),
        QStringLiteral("protected"), QStringLiteral("public"), QStringLiteral("register"),
        QStringLiteral("reinterpret_cast"), QStringLiteral("restrict"), QStringLiteral("return"),
        QStringLiteral("sizeof"), QStringLiteral("static"), QStringLiteral("static_assert"),
        QStringLiteral("static_cast"), QStringLiteral("struct"), QStringLiteral("switch"),
        QStringLiteral("template"), QStringLiteral("this"), QStringLiteral("throw"),
        QStringLiteral("true"), QStringLiteral("try"), QStringLiteral("typedef"),
        QStringLiteral("typeid"), QStringLiteral("typename"), QStringLiteral("union"),
        QStringLiteral("using"), QStringLiteral("virtual"), QStringLiteral("volatile"),
        QStringLiteral("while"),
        QStringLiteral("__cdecl"), QStringLiteral("__stdcall"), QStringLiteral("__fastcall"),
        QStringLiteral("__attribute__"), QStringLiteral("__declspec")
    };
    return set;
}

const std::unordered_set<QString>& Types() {
    static const std::unordered_set<QString> set = {
        QStringLiteral("void"), QStringLiteral("bool"), QStringLiteral("char"),
        QStringLiteral("char8_t"), QStringLiteral("char16_t"), QStringLiteral("char32_t"),
        QStringLiteral("wchar_t"), QStringLiteral("short"), QStringLiteral("int"),
        QStringLiteral("long"), QStringLiteral("float"), QStringLiteral("double"),
        QStringLiteral("signed"), QStringLiteral("unsigned"),
        QStringLiteral("size_t"), QStringLiteral("ptrdiff_t"),
        QStringLiteral("int8_t"), QStringLiteral("int16_t"), QStringLiteral("int32_t"), QStringLiteral("int64_t"),
        QStringLiteral("uint8_t"), QStringLiteral("uint16_t"), QStringLiteral("uint32_t"), QStringLiteral("uint64_t"),
        QStringLiteral("intptr_t"), QStringLiteral("uintptr_t")
    };
    return set;
}

const std::unordered_set<QString>& AzForgeSymbols() {
    static const std::unordered_set<QString> set = {
        QStringLiteral("API"), QStringLiteral("SELF"), QStringLiteral("g_api"),
        QStringLiteral("mod_delta"), QStringLiteral("AmodInit"), QStringLiteral("AmodShutdown"),
        QStringLiteral("AmodUpdate"), QStringLiteral("AMOD_EXPORT")
    };
    return set;
}

} // namespace

CppSyntaxTheme CppSyntaxTheme::Default() {
    CppSyntaxTheme t;
    t.background   = QColor(0x15, 0x17, 0x1e);
    t.foreground   = QColor(0xe4, 0xe7, 0xf0);
    t.keyword      = MakeFmt("#c792ea", true, false);
    t.type         = MakeFmt("#7aa2f7", false, false);
    t.preprocessor = MakeFmt("#f07178", true, false);
    t.azforge      = MakeFmt("#9b87f5", true, false);
    t.macro        = MakeFmt("#e5c07b", false, false);
    t.function     = MakeFmt("#7dcfff", false, false);
    t.number       = MakeFmt("#f78c6c", false, false);
    t.string       = MakeFmt("#9ece6a", false, false);
    t.comment      = MakeFmt("#646c88", false, true);
    t.op           = MakeFmt("#89ddff", false, false);
    return t;
}

std::string CppSyntaxTheme::FilePath() {
    std::error_code ec;
    const std::string configured = SharedConfig::Get("syntaxTheme");
    if (!configured.empty() && fs::exists(PathUtil::MakePath(configured), ec)) {
        return configured;
    }
    return PathUtil::U8(PathUtil::ResourcePath("themes/syntax_cpp.json"));
}

CppSyntaxTheme CppSyntaxTheme::LoadFromFile(const std::string& path) {
    CppSyntaxTheme theme = Default();
    const std::string target = path.empty() ? FilePath() : path;
    std::ifstream in(PathUtil::MakePath(target), std::ios::binary);
    if (!in) return theme;

    json j = json::parse(in, nullptr, false);
    if (!j.is_object()) return theme;

    if (j.contains("background") && j["background"].is_string()) {
        const QColor c(QString::fromStdString(j["background"].get<std::string>()));
        if (c.isValid()) theme.background = c;
    }
    if (j.contains("foreground") && j["foreground"].is_string()) {
        const QColor c(QString::fromStdString(j["foreground"].get<std::string>()));
        if (c.isValid()) theme.foreground = c;
    }

    const json& styles = j.contains("styles") && j["styles"].is_object() ? j["styles"] : j;
    ApplyStyleJson(styles, "keyword",      theme.keyword);
    ApplyStyleJson(styles, "type",         theme.type);
    ApplyStyleJson(styles, "preprocessor", theme.preprocessor);
    ApplyStyleJson(styles, "azforge",      theme.azforge);
    ApplyStyleJson(styles, "macro",        theme.macro);
    ApplyStyleJson(styles, "function",     theme.function);
    ApplyStyleJson(styles, "number",       theme.number);
    ApplyStyleJson(styles, "string",       theme.string);
    ApplyStyleJson(styles, "comment",      theme.comment);
    ApplyStyleJson(styles, "operator",     theme.op);
    return theme;
}

CppSyntaxWatcher& CppSyntaxWatcher::Instance() {
    static CppSyntaxWatcher inst;
    return inst;
}

CppSyntaxWatcher::CppSyntaxWatcher() {
    m_theme = CppSyntaxTheme::LoadFromFile();
    ensureWatched();

    auto scheduleReload = [this]() {
        // Delay slightly so atomic save-and-replace editors finish writing before we read
        QTimer::singleShot(120, this, &CppSyntaxWatcher::reload);
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, scheduleReload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, scheduleReload);
}

void CppSyntaxWatcher::ensureWatched() {
    const QString file = QString::fromStdString(CppSyntaxTheme::FilePath());
    const QFileInfo fi(file);
    const QString dir = fi.absolutePath();

    if (m_watchedFile != file) {
        if (!m_watchedFile.isEmpty()) m_watcher.removePath(m_watchedFile);
        m_watchedFile = file;
    }
    if (m_watchedDir != dir) {
        if (!m_watchedDir.isEmpty()) m_watcher.removePath(m_watchedDir);
        m_watchedDir = dir;
    }
    if (!m_watchedFile.isEmpty() && fi.exists() && !m_watcher.files().contains(m_watchedFile)) {
        m_watcher.addPath(m_watchedFile);
    }
    if (!m_watchedDir.isEmpty() && QFileInfo::exists(m_watchedDir) && !m_watcher.directories().contains(m_watchedDir)) {
        m_watcher.addPath(m_watchedDir);
    }
}

void CppSyntaxWatcher::reload() {
    ensureWatched();
    m_theme = CppSyntaxTheme::LoadFromFile();
    emit themeChanged(m_theme);
}

CppHighlighter::CppHighlighter(QPlainTextEdit* editor)
    : QSyntaxHighlighter(editor->document()), m_editor(editor) {
    auto& watcher = CppSyntaxWatcher::Instance();
    applyTheme(watcher.theme());
    connect(&watcher, &CppSyntaxWatcher::themeChanged, this, &CppHighlighter::applyTheme);
}

void CppHighlighter::applyTheme(const CppSyntaxTheme& theme) {
    m_theme = theme;
    if (m_editor) {
        m_editor->setStyleSheet(QStringLiteral(
            "QPlainTextEdit { background: %1; color: %2; border: 1px solid #313647; border-radius: 4px; padding: 4px 6px; selection-background-color: #52468a; }"
        ).arg(m_theme.background.name(), m_theme.foreground.name()));
    }
    rehighlight();
}

void CppHighlighter::highlightBlock(const QString& text) {
    const int n = text.size();
    int i = 0;

    setCurrentBlockState(0);

    // Continue multi-line /* ... */ block comment from previous line
    if (previousBlockState() == 1) {
        const int end = text.indexOf(QStringLiteral("*/"), 0);
        if (end < 0) {
            setFormat(0, n, m_theme.comment);
            setCurrentBlockState(1);
            return;
        }
        setFormat(0, end + 2, m_theme.comment);
        i = end + 2;
    }

    // Check preprocessor directive at first non-whitespace character
    int firstNonSpace = 0;
    while (firstNonSpace < n && text[firstNonSpace].isSpace()) ++firstNonSpace;
    if (i == 0 && firstNonSpace < n && text[firstNonSpace] == QLatin1Char('#')) {
        int dirEnd = firstNonSpace + 1;
        while (dirEnd < n && text[dirEnd].isSpace()) ++dirEnd;
        while (dirEnd < n && IsIdentChar(text[dirEnd])) ++dirEnd;
        setFormat(firstNonSpace, dirEnd - firstNonSpace, m_theme.preprocessor);
        i = dirEnd;

        // Highlight <header.h> on #include lines
        int scan = i;
        while (scan < n && text[scan].isSpace()) ++scan;
        if (scan < n && text[scan] == QLatin1Char('<')) {
            const int close = text.indexOf(QLatin1Char('>'), scan + 1);
            if (close > scan) {
                setFormat(scan, close - scan + 1, m_theme.string);
                i = close + 1;
            }
        }
    }

    while (i < n) {
        const QChar ch = text[i];

        // Line comment //
        if (ch == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('/')) {
            setFormat(i, n - i, m_theme.comment);
            return;
        }

        // Block comment /* ... */
        if (ch == QLatin1Char('/') && i + 1 < n && text[i + 1] == QLatin1Char('*')) {
            const int end = text.indexOf(QStringLiteral("*/"), i + 2);
            if (end < 0) {
                setFormat(i, n - i, m_theme.comment);
                setCurrentBlockState(1);
                return;
            }
            setFormat(i, end + 2 - i, m_theme.comment);
            i = end + 2;
            continue;
        }

        // String or char literal
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            const QChar quote = ch;
            const int start = i++;
            while (i < n) {
                if (text[i] == QLatin1Char('\\') && i + 1 < n) { i += 2; continue; }
                if (text[i] == quote) { ++i; break; }
                ++i;
            }
            setFormat(start, i - start, m_theme.string);
            continue;
        }

        // Numeric literal (hex, binary, float, int with suffixes)
        if (ch.isDigit() || (ch == QLatin1Char('.') && i + 1 < n && text[i + 1].isDigit())) {
            const int start = i;
            if (ch == QLatin1Char('0') && i + 1 < n &&
                (text[i + 1] == QLatin1Char('x') || text[i + 1] == QLatin1Char('X'))) {
                i += 2;
                while (i < n && (IsHexDigit(text[i]) || text[i] == QLatin1Char('\''))) ++i;
            } else {
                while (i < n && (text[i].isDigit() || text[i] == QLatin1Char('.') || text[i] == QLatin1Char('\''))) ++i;
                if (i < n && (text[i] == QLatin1Char('e') || text[i] == QLatin1Char('E'))) {
                    ++i;
                    if (i < n && (text[i] == QLatin1Char('+') || text[i] == QLatin1Char('-'))) ++i;
                    while (i < n && text[i].isDigit()) ++i;
                }
            }
            while (i < n && (text[i] == QLatin1Char('u') || text[i] == QLatin1Char('U') ||
                             text[i] == QLatin1Char('l') || text[i] == QLatin1Char('L') ||
                             text[i] == QLatin1Char('f') || text[i] == QLatin1Char('F'))) {
                ++i;
            }
            setFormat(start, i - start, m_theme.number);
            continue;
        }

        // Identifier / keyword / type / AzForge symbol / macro / function call
        if (IsIdentStart(ch)) {
            const int start = i++;
            while (i < n && IsIdentChar(text[i])) ++i;
            const QString id = text.mid(start, i - start);

            if (Keywords().count(id)) {
                setFormat(start, i - start, m_theme.keyword);
            } else if (AzForgeSymbols().count(id) || id.startsWith(QStringLiteral("AMOD_"))) {
                setFormat(start, i - start, m_theme.azforge);
            } else if (Types().count(id) || id.startsWith(QStringLiteral("Amod"))) {
                setFormat(start, i - start, m_theme.type);
            } else if (IsAllCapsMacro(id)) {
                setFormat(start, i - start, m_theme.macro);
            } else {
                int look = i;
                while (look < n && text[look].isSpace()) ++look;
                if (look < n && text[look] == QLatin1Char('(')) {
                    setFormat(start, i - start, m_theme.function);
                }
            }
            continue;
        }

        static const QString ops = QStringLiteral("+-*/%=<>!&|^~?:");
        if (ops.contains(ch)) {
            setFormat(i, 1, m_theme.op);
        }
        ++i;
    }
}
