#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>
#include <QPlainTextEdit>
#include <QString>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <string>

struct CppSyntaxTheme {
    QColor background = QColor(0x15, 0x17, 0x1e);
    QColor foreground = QColor(0xe4, 0xe7, 0xf0);

    QTextCharFormat keyword;
    QTextCharFormat type;
    QTextCharFormat preprocessor;
    QTextCharFormat azforge;
    QTextCharFormat macro;
    QTextCharFormat function;
    QTextCharFormat number;
    QTextCharFormat string;
    QTextCharFormat comment;
    QTextCharFormat op;

    static CppSyntaxTheme Default();
    static std::string FilePath();
    static CppSyntaxTheme LoadFromFile(const std::string& path = {});
};

// Watches syntax_cpp.json on disk and notifies all active highlighters on live edits.
class CppSyntaxWatcher : public QObject {
    Q_OBJECT
public:
    static CppSyntaxWatcher& Instance();

    const CppSyntaxTheme& theme() const { return m_theme; }
    void reload();

signals:
    void themeChanged(const CppSyntaxTheme& theme);

private:
    CppSyntaxWatcher();
    void ensureWatched();

    QFileSystemWatcher m_watcher;
    CppSyntaxTheme m_theme;
    QString m_watchedFile;
    QString m_watchedDir;
};

class CppHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit CppHighlighter(QPlainTextEdit* editor);

    void applyTheme(const CppSyntaxTheme& theme);

protected:
    void highlightBlock(const QString& text) override;

private:
    QPlainTextEdit* m_editor = nullptr;
    CppSyntaxTheme m_theme;
};
