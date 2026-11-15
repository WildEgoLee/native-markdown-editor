#include "Theme.h"
#include <QtSvg/QSvgRenderer>
#include <QPainter>
#include <QApplication>

namespace MarkdownTool::Theme {

ThemeManager &ThemeManager::instance() {
    static ThemeManager mgr;
    return mgr;
}

ThemeManager::ThemeManager() {
    updatePalette();
}

void ThemeManager::setMode(ThemeMode mode) {
    if (m_mode != mode) {
        m_mode = mode;
        updatePalette();
        emit themeChanged(m_mode);
    }
}

void ThemeManager::toggleMode() {
    setMode(m_mode == ThemeMode::Light ? ThemeMode::Dark : ThemeMode::Light);
}

void ThemeManager::updatePalette() {
    if (m_mode == ThemeMode::Light) {
        // Paper Light Design System
        m_palette.appBackground       = QColor(0xF5, 0xF6, 0xF8);
        m_palette.sidebarBackground   = QColor(0xF0, 0xF2, 0xF5);
        m_palette.editorBackground    = QColor(0xFF, 0xFF, 0xFF);
        m_palette.primaryText         = QColor(0x24, 0x29, 0x32);
        m_palette.secondaryText       = QColor(0x73, 0x7B, 0x88);
        m_palette.border              = QColor(0xE4, 0xE8, 0xEF);
        m_palette.accent              = QColor(0x56, 0x68, 0xDB);
        m_palette.hover               = QColor(0xE8, 0xEB, 0xF3);
        m_palette.selected            = QColor(0xDF, 0xE4, 0xF2);

        m_palette.quoteLine           = QColor(0x56, 0x68, 0xDB);
        m_palette.quoteBackground     = QColor(0xF8, 0xF9, 0xFC);
        m_palette.codeBlockBackground = QColor(0xF6, 0xF8, 0xFA);
        m_palette.codeBlockBorder     = QColor(0xE2, 0xE6, 0xEE);
        m_palette.inlineCodeBackground= QColor(0xEE, 0xF1, 0xF5);
        m_palette.inlineCodeText      = QColor(0x40, 0x48, 0x58);
        m_palette.tableCellBackground = QColor(0xF8, 0xFA, 0xFC);
        m_palette.tableBorder         = QColor(0xE4, 0xE8, 0xEF);
        m_palette.hrColor             = QColor(0xE2, 0xE6, 0xED);
        m_palette.selectionHighlight  = QColor(86, 104, 219, 45); // subtle accent translucent
        m_palette.caretColor          = QColor(0x56, 0x68, 0xDB);
        m_palette.cardBackground      = QColor(0xF8, 0xFA, 0xFC);
        m_palette.cardBorder          = QColor(0xE2, 0xE8, 0xF0);
        m_palette.syntaxMarkerColor   = QColor(0x9E, 0xA6, 0xB4);
        m_palette.taskCheckedColor    = QColor(0x56, 0x68, 0xDB);
        m_palette.taskUncheckedColor  = QColor(0x9E, 0xA6, 0xB4);
    } else {
        // Graphite Dark Design System
        m_palette.appBackground       = QColor(0x18, 0x1A, 0x20);
        m_palette.sidebarBackground   = QColor(0x20, 0x23, 0x2B);
        m_palette.editorBackground    = QColor(0x24, 0x27, 0x30);
        m_palette.primaryText         = QColor(0xEC, 0xEE, 0xF3);
        m_palette.secondaryText       = QColor(0xA0, 0xA7, 0xB4);
        m_palette.border              = QColor(0x34, 0x39, 0x43);
        m_palette.accent              = QColor(0x92, 0xA0, 0xFF);
        m_palette.hover               = QColor(0x2C, 0x31, 0x3C);
        m_palette.selected            = QColor(0x35, 0x3C, 0x4A);

        m_palette.quoteLine           = QColor(0x92, 0xA0, 0xFF);
        m_palette.quoteBackground     = QColor(0x22, 0x25, 0x2E);
        m_palette.codeBlockBackground = QColor(0x1C, 0x1E, 0x26);
        m_palette.codeBlockBorder     = QColor(0x32, 0x37, 0x42);
        m_palette.inlineCodeBackground= QColor(0x2D, 0x32, 0x3E);
        m_palette.inlineCodeText      = QColor(0xEC, 0xEE, 0xF3);
        m_palette.tableCellBackground = QColor(0x20, 0x23, 0x2C);
        m_palette.tableBorder         = QColor(0x34, 0x39, 0x43);
        m_palette.hrColor             = QColor(0x34, 0x39, 0x43);
        m_palette.selectionHighlight  = QColor(146, 160, 255, 60);
        m_palette.caretColor          = QColor(0x92, 0xA0, 0xFF);
        m_palette.cardBackground      = QColor(0x1F, 0x22, 0x2A);
        m_palette.cardBorder          = QColor(0x34, 0x39, 0x44);
        m_palette.syntaxMarkerColor   = QColor(0x65, 0x6E, 0x7E);
        m_palette.taskCheckedColor    = QColor(0x92, 0xA0, 0xFF);
        m_palette.taskUncheckedColor  = QColor(0x65, 0x6E, 0x7E);
    }
}

void ThemeManager::setFontPreset(FontPreset preset) {
    if (m_fontPreset != preset) {
        m_fontPreset = preset;
        emit typographyChanged();
    }
}

void ThemeManager::setCustomFontFamily(const QString &family) {
    m_fontPreset = FontPreset::Custom;
    m_customFontFamily = family;
    emit typographyChanged();
}

QStringList ThemeManager::proseFontFamilies() const {
    switch (m_fontPreset) {
    case FontPreset::Serif:
        return {
            QStringLiteral("Noto Serif SC"),
            QStringLiteral("Source Han Serif SC"),
            QStringLiteral("STSong"),
            QStringLiteral("SimSun"),
            QStringLiteral("Georgia"),
            QStringLiteral("Cambria"),
            QStringLiteral("serif")
        };
    case FontPreset::Monospace:
        return {
            QStringLiteral("Cascadia Code"),
            QStringLiteral("Consolas"),
            QStringLiteral("JetBrains Mono"),
            QStringLiteral("Fira Code"),
            QStringLiteral("Courier New"),
            QStringLiteral("monospace")
        };
    case FontPreset::Custom:
        if (!m_customFontFamily.isEmpty()) {
            return {
                m_customFontFamily,
                QStringLiteral("Microsoft YaHei UI"),
                QStringLiteral("sans-serif")
            };
        }
        [[fallthrough]];
    case FontPreset::SansSerif:
    default:
        return {
            QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Microsoft YaHei"),
            QStringLiteral("PingFang SC"),
            QStringLiteral("Segoe UI"),
            QStringLiteral("HarmonyOS Sans SC"),
            QStringLiteral("Noto Sans SC"),
            QStringLiteral("sans-serif")
        };
    }
}

QString ThemeManager::currentFontDisplayName() const {
    switch (m_fontPreset) {
    case FontPreset::SansSerif: return QStringLiteral("现代无衬线 (微软雅黑)");
    case FontPreset::Serif:     return QStringLiteral("典雅书卷体 (宋体)");
    case FontPreset::Monospace: return QStringLiteral("极客等宽体 (Cascadia Code)");
    case FontPreset::Custom:    return m_customFontFamily.isEmpty() ? QStringLiteral("自定义字体") : m_customFontFamily;
    }
    return QStringLiteral("现代无衬线");
}

void ThemeManager::setBasePointSize(qreal pt) {
    qreal clamped = std::clamp(pt, 8.0, 36.0);
    if (std::abs(m_basePointSize - clamped) > 0.01) {
        m_basePointSize = clamped;
        emit typographyChanged();
    }
}

void ThemeManager::zoomIn() {
    setBasePointSize(m_basePointSize + 1.0);
}

void ThemeManager::zoomOut() {
    setBasePointSize(m_basePointSize - 1.0);
}

void ThemeManager::resetZoom() {
    setBasePointSize(12.0);
}

QFont ThemeManager::fontSans(int pointSize, int weight) const {
    QFont f;
    f.setFamilies(proseFontFamilies());
    f.setPointSize(pointSize);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

QFont ThemeManager::fontMono(int pointSize, int weight) const {
    QFont f;
    f.setFamilies({QStringLiteral("Cascadia Code"), QStringLiteral("Consolas"), QStringLiteral("JetBrains Mono"), QStringLiteral("Fira Code"), QStringLiteral("monospace")});
    f.setPointSize(pointSize);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

QString ThemeManager::mainStylesheet() const {
    const auto &p = m_palette;
    return QStringLiteral(R"(
/* Global Application Style */
QWidget {
    font-family: 'Segoe UI', 'Microsoft YaHei UI', -apple-system, sans-serif;
    color: %1;
}

QMainWindow {
    background-color: %2;
}

/* Minimalist Clean Toolbar */
QToolBar {
    background-color: %2;
    border: none;
    border-bottom: 1px solid %3;
    padding: 4px 12px;
    spacing: 6px;
    min-height: 42px;
}

QToolBar::separator {
    width: 1px;
    background-color: %3;
    margin: 6px 4px;
}

QToolButton {
    background-color: transparent;
    border: none;
    border-radius: 6px;
    padding: 5px 8px;
    margin: 1px;
    color: %1;
    font-size: 13px;
    font-weight: 500;
}

QToolButton:hover {
    background-color: %4;
}

QToolButton:pressed, QToolButton:checked {
    background-color: %5;
    color: %6;
}

/* Elegant Menubar */
QMenuBar {
    background-color: %2;
    color: %7;
    border-bottom: 1px solid %3;
    padding: 2px 8px;
    font-size: 12.5px;
}

QMenuBar::item {
    background-color: transparent;
    padding: 4px 8px;
    border-radius: 4px;
}

QMenuBar::item:selected {
    background-color: %4;
    color: %1;
}

QMenu {
    background-color: %2;
    color: %1;
    border: 1px solid %3;
    border-radius: 8px;
    padding: 6px 0;
}

QMenu::item {
    padding: 6px 24px 6px 16px;
    font-size: 13px;
}

QMenu::item:selected {
    background-color: %4;
    color: %6;
}

QMenu::separator {
    height: 1px;
    background-color: %3;
    margin: 4px 8px;
}

/* Sleek Minimalist Statusbar */
QStatusBar {
    background-color: %2;
    border-top: 1px solid %3;
    color: %7;
    font-size: 12px;
    min-height: 26px;
    max-height: 28px;
    padding: 0 12px;
}

QStatusBar QLabel {
    color: %7;
    padding: 0 6px;
    font-size: 11.5px;
}

QStatusBar::item {
    border: none;
}

/* Splitter */
QSplitter::handle {
    background-color: %3;
    width: 1px;
}

/* Modern Minimalist Scrollbars */
QScrollBar:vertical {
    border: none;
    background-color: transparent;
    width: 8px;
    margin: 0;
}

QScrollBar::handle:vertical {
    background-color: %3;
    min-height: 30px;
    border-radius: 4px;
    margin: 2px;
}

QScrollBar::handle:vertical:hover {
    background-color: %7;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    border: none;
    background: none;
    height: 0;
}

QScrollBar:horizontal {
    border: none;
    background-color: transparent;
    height: 8px;
    margin: 0;
}

QScrollBar::handle:horizontal {
    background-color: %3;
    min-width: 30px;
    border-radius: 4px;
    margin: 2px;
}

QScrollBar::handle:horizontal:hover {
    background-color: %7;
}

QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    border: none;
    background: none;
    width: 0;
}
)")
        .arg(p.primaryText.name())
        .arg(p.appBackground.name())
        .arg(p.border.name())
        .arg(p.hover.name())
        .arg(p.selected.name())
        .arg(p.accent.name())
        .arg(p.secondaryText.name());
}

QString ThemeManager::sidebarStylesheet() const {
    const auto &p = m_palette;
    return QStringLiteral(R"(
QWidget#sidebarContainer {
    background-color: %1;
    border-right: 1px solid %2;
}

/* Segmented Control Pill Container */
QFrame#segmentedControl {
    background-color: %3;
    border-radius: 7px;
    padding: 2px;
}

QPushButton#segmentBtn {
    background-color: transparent;
    border: none;
    border-radius: 5px;
    color: %4;
    font-size: 12.5px;
    font-weight: 500;
    padding: 5px 12px;
}

QPushButton#segmentBtn:hover {
    color: %5;
}

QPushButton#segmentBtn[active="true"] {
    background-color: %6;
    color: %5;
    font-weight: 600;
}

/* Search input */
QLineEdit#sidebarSearch {
    background-color: %6;
    border: 1px solid %2;
    border-radius: 6px;
    padding: 5px 10px 5px 28px;
    font-size: 12.5px;
    color: %5;
}

QLineEdit#sidebarSearch:focus {
    border: 1px solid %7;
}

/* Trees */
QTreeView, QTreeWidget {
    background-color: transparent;
    border: none;
    outline: none;
    color: %5;
    font-size: 13px;
    padding: 4px;
}

QTreeView::item, QTreeWidget::item {
    height: 30px;
    border-radius: 5px;
    padding-left: 4px;
    margin: 1px 0;
}

QTreeView::item:hover, QTreeWidget::item:hover {
    background-color: %8;
}

QTreeView::item:selected, QTreeWidget::item:selected {
    background-color: %9;
    color: %7;
    font-weight: 600;
}

QHeaderView::section {
    background-color: transparent;
    border: none;
    color: %4;
    font-size: 11px;
    font-weight: 600;
    padding: 4px 6px;
}
)")
        .arg(p.sidebarBackground.name())
        .arg(p.border.name())
        .arg(p.hover.name())
        .arg(p.secondaryText.name())
        .arg(p.primaryText.name())
        .arg(p.editorBackground.name())
        .arg(p.accent.name())
        .arg(p.hover.name())
        .arg(p.selected.name());
}

QString ThemeManager::findBarStylesheet() const {
    const auto &p = m_palette;
    return QStringLiteral(R"(
QFrame#findReplaceBar {
    background-color: %1;
    border-bottom: 1px solid %2;
    padding: 6px 14px;
}

QLineEdit {
    background-color: %3;
    border: 1px solid %2;
    border-radius: 5px;
    padding: 4px 8px;
    color: %4;
    font-size: 13px;
}

QLineEdit:focus {
    border: 1px solid %5;
}

QPushButton {
    background-color: %6;
    border: 1px solid %2;
    border-radius: 5px;
    padding: 4px 10px;
    color: %4;
    font-size: 12.5px;
}

QPushButton:hover {
    background-color: %7;
    border-color: %5;
}

QCheckBox {
    color: %8;
    font-size: 12px;
    spacing: 5px;
}
)")
        .arg(p.appBackground.name())
        .arg(p.border.name())
        .arg(p.editorBackground.name())
        .arg(p.primaryText.name())
        .arg(p.accent.name())
        .arg(p.hover.name())
        .arg(p.selected.name())
        .arg(p.secondaryText.name());
}

QIcon ThemeManager::icon(const QString &name, int size, const QColor &customColor) const {
    QColor col = customColor.isValid() ? customColor : m_palette.primaryText;
    QString colStr = col.name();

    QString svg;
    if (name == QStringLiteral("sidebar")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <rect x="3" y="3" width="18" height="18" rx="2" ry="2"/>
            <line x1="9" y1="3" x2="9" y2="21"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("new")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
            <polyline points="14 2 14 8 20 8"/>
            <line x1="12" y1="18" x2="12" y2="12"/>
            <line x1="9" y1="15" x2="15" y2="15"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("open")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("save")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/>
            <polyline points="17 21 17 13 7 13 7 21"/>
            <polyline points="7 3 7 8 15 8"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("undo")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <polyline points="1 4 1 10 7 10"/>
            <path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("redo")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <polyline points="23 4 23 10 17 10"/>
            <path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("find")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <circle cx="11" cy="11" r="8"/>
            <line x1="21" y1="21" x2="16.65" y2="16.65"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("theme")) {
        // Sun / Moon icon
        if (m_mode == ThemeMode::Light) {
            // Moon icon (suggests switching to dark)
            svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>
            </svg>)").arg(size).arg(colStr);
        } else {
            // Sun icon (suggests switching to light)
            svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <circle cx="12" cy="12" r="5"/>
                <line x1="12" y1="1" x2="12" y2="3"/>
                <line x1="12" y1="21" x2="12" y2="23"/>
                <line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/>
                <line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/>
                <line x1="1" y1="12" x2="3" y2="12"/>
                <line x1="21" y1="12" x2="23" y2="12"/>
                <line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/>
                <line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/>
            </svg>)").arg(size).arg(colStr);
        }
    } else if (name == QStringLiteral("preview") || name == QStringLiteral("mode")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>
            <circle cx="12" cy="12" r="3"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("source")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <polyline points="16 18 22 12 16 6"/>
            <polyline points="8 6 2 12 8 18"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("outline")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <line x1="21" y1="6" x2="3" y2="6"/>
            <line x1="15" y1="12" x2="3" y2="12"/>
            <line x1="17" y1="18" x2="3" y2="18"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("folder")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("file")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"/>
            <polyline points="13 2 13 9 20 9"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("search")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <circle cx="11" cy="11" r="8"/>
            <line x1="21" y1="21" x2="16.65" y2="16.65"/>
        </svg>)").arg(size).arg(colStr);
    } else if (name == QStringLiteral("font")) {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <polyline points="4 20 12 4 20 20"/>
            <line x1="6.8" y1="14" x2="17.2" y2="14"/>
        </svg>)").arg(size).arg(colStr);
    } else {
        svg = QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%1" height="%1" fill="none" stroke="%2" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <circle cx="12" cy="12" r="10"/>
        </svg>)").arg(size).arg(colStr);
    }

    QByteArray bytes = svg.toUtf8();
    QSvgRenderer renderer(bytes);

    // Render sharp icon pixmap with device pixel ratio support
    QPixmap pix(size * 2, size * 2);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&p);
    p.end();

    pix.setDevicePixelRatio(2.0);
    return QIcon(pix);
}

} // namespace MarkdownTool::Theme
