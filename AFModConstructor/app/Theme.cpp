#include "Theme.h"

#include "PathUtil.h"

#include <QFontDatabase>
#include <QFontInfo>
#include <QPalette>
#include <QString>
#include <QStyleFactory>

namespace Theme {
    const Palette& Colors() {
        static const Palette palette;
        return palette;
    }

    QFont MonoFont(int pointSize) {
        QFont f(QStringLiteral("Cascadia Mono"));
        if (!QFontInfo(f).exactMatch()) f = QFont(QStringLiteral("Consolas"));
        f.setStyleHint(QFont::Monospace);
        f.setPointSize(pointSize);
        return f;
    }

    void Apply(QApplication& app) {
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

        const auto fontPath = PathUtil::ResourcePath("fonts/Inter-Regular.ttf");
        const int fontId = QFontDatabase::addApplicationFont(QString::fromStdWString(fontPath.wstring()));
        if (fontId >= 0) {
            const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
            if (!families.isEmpty()) {
                QFont uiFont(families.first(), 10);
                uiFont.setHintingPreference(QFont::PreferNoHinting);
                app.setFont(uiFont);
            }
        } else {
            QFont uiFont(QStringLiteral("Segoe UI"), 10);
            app.setFont(uiFont);
        }

        const Palette& c = Colors();
        QPalette pal;
        pal.setColor(QPalette::Window, c.windowBg);
        pal.setColor(QPalette::WindowText, c.text);
        pal.setColor(QPalette::Base, c.panelBg);
        pal.setColor(QPalette::AlternateBase, c.cardBg);
        pal.setColor(QPalette::ToolTipBase, c.cardBg);
        pal.setColor(QPalette::ToolTipText, c.text);
        pal.setColor(QPalette::Text, c.text);
        pal.setColor(QPalette::Button, c.cardBg);
        pal.setColor(QPalette::ButtonText, c.text);
        pal.setColor(QPalette::BrightText, Qt::white);
        pal.setColor(QPalette::Highlight, QColor(0x52, 0x46, 0x8a));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::PlaceholderText, c.textMuted);
        pal.setColor(QPalette::Disabled, QPalette::Text, c.textMuted);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, c.textMuted);
        app.setPalette(pal);

        app.setStyleSheet(QStringLiteral(R"(
            QMainWindow {
                background: #16181f;
            }
            QMainWindow::separator {
                background: #272c3a;
                width: 3px;
                height: 3px;
            }
            QMainWindow::separator:hover {
                background: #9b87f5;
            }
            QMenuBar {
                background: #181b23;
                border-bottom: 1px solid #272c3a;
                padding: 2px 4px;
            }
            QMenuBar::item {
                padding: 4px 10px;
                border-radius: 4px;
                color: #cfd4e2;
            }
            QMenuBar::item:selected {
                background: #282d3b;
                color: #ffffff;
            }
            QMenu {
                background: #1f232e;
                border: 1px solid #353b4d;
                padding: 4px;
            }
            QMenu::item {
                padding: 6px 24px 6px 12px;
                border-radius: 4px;
            }
            QMenu::item:selected {
                background: #363257;
                color: #ffffff;
            }
            QMenu::separator {
                height: 1px;
                background: #2d3242;
                margin: 4px 6px;
            }
            QToolBar {
                background: #1c1f28;
                border-bottom: 1px solid #2a2f3e;
                spacing: 6px;
                padding: 5px 8px;
            }
            QToolBar::separator {
                width: 1px;
                background: #313647;
                margin: 3px 4px;
            }
            QDockWidget {
                titlebar-close-icon: none;
                color: #d6daea;
                font-weight: 600;
            }
            QDockWidget::title {
                background: #1c1f28;
                border-bottom: 1px solid #2a2f3e;
                padding: 6px 10px;
                text-align: left;
            }
            QTabWidget::pane {
                border: none;
                background: #1c1f28;
            }
            QTabBar::tab {
                background: #191c24;
                color: #8c93a8;
                padding: 6px 12px;
                border-bottom: 2px solid transparent;
                margin-right: 2px;
            }
            QTabBar::tab:hover {
                color: #e6e8f0;
                background: #212530;
            }
            QTabBar::tab:selected {
                color: #ffffff;
                background: #212530;
                border-bottom: 2px solid #9b87f5;
            }
            QPushButton {
                background: #252a38;
                color: #e6e8f0;
                border: 1px solid #353c50;
                border-radius: 5px;
                padding: 5px 12px;
            }
            QPushButton:hover {
                background: #2e3445;
                border-color: #525b78;
            }
            QPushButton:pressed {
                background: #1f232e;
            }
            QPushButton:disabled {
                background: #1d2029;
                color: #60677c;
                border-color: #272c3a;
            }
            QPushButton[primary="true"] {
                background: #6c5ce7;
                color: #ffffff;
                border: 1px solid #7f71f0;
                font-weight: 600;
            }
            QPushButton[primary="true"]:hover {
                background: #7c6df2;
            }
            QPushButton[primary="true"]:disabled {
                background: #39345c;
                color: #938eb8;
                border-color: #423c69;
            }
            QLineEdit, QComboBox, QPlainTextEdit, QSpinBox {
                background: #16181f;
                color: #e6e8f0;
                border: 1px solid #313647;
                border-radius: 4px;
                padding: 4px 8px;
                selection-background-color: #52468a;
            }
            QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus, QSpinBox:focus {
                border-color: #9b87f5;
            }
            QComboBox::drop-down {
                border: none;
                width: 22px;
            }
            QTreeWidget, QListWidget, QTableWidget {
                background: #1a1d26;
                alternate-background-color: #1e222c;
                border: none;
                outline: none;
            }
            QTreeWidget::item, QListWidget::item {
                padding: 4px 6px;
                border-radius: 4px;
            }
            QTreeWidget::item:hover, QListWidget::item:hover {
                background: #252a38;
            }
            QTreeWidget::item:selected, QListWidget::item:selected {
                background: #352f54;
                color: #ffffff;
            }
            QHeaderView::section {
                background: #1f232e;
                color: #8c93a8;
                border: none;
                border-bottom: 1px solid #2f3547;
                border-right: 1px solid #272c3a;
                padding: 5px 8px;
                font-weight: 600;
            }
            QScrollBar:vertical {
                background: transparent;
                width: 10px;
                margin: 0;
            }
            QScrollBar::handle:vertical {
                background: #343a4d;
                border-radius: 4px;
                min-height: 28px;
                margin: 2px;
            }
            QScrollBar::handle:vertical:hover {
                background: #4b536e;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
            QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
                height: 0;
                width: 0;
            }
            QScrollBar:horizontal {
                background: transparent;
                height: 10px;
                margin: 0;
            }
            QScrollBar::handle:horizontal {
                background: #343a4d;
                border-radius: 4px;
                min-width: 28px;
                margin: 2px;
            }
            QStatusBar {
                background: #181b23;
                border-top: 1px solid #272c3a;
                color: #8c93a8;
            }
            QStatusBar::item {
                border: none;
            }
            QToolTip {
                background: #232836;
                color: #e6e8f0;
                border: 1px solid #454d66;
                padding: 4px 8px;
            }
        )"));
    }

} // namespace Theme
