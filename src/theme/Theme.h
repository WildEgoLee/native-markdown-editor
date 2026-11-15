#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QObject>

namespace MarkdownTool::Theme {

enum class ThemeMode {
    Light, // Paper Light
    Dark   // Graphite Dark
};

struct ColorPalette {
    QColor appBackground;
    QColor sidebarBackground;
    QColor editorBackground;
    QColor primaryText;
    QColor secondaryText;
    QColor border;
    QColor accent;
    QColor hover;
    QColor selected;

    // Component specific colors
    QColor quoteLine;
    QColor quoteBackground;
    QColor codeBlockBackground;
    QColor codeBlockBorder;
    QColor inlineCodeBackground;
    QColor inlineCodeText;
    QColor tableCellBackground;
    QColor tableBorder;
    QColor hrColor;
    QColor selectionHighlight;
    QColor caretColor;
    QColor cardBackground;
    QColor cardBorder;
    QColor syntaxMarkerColor;
    QColor taskCheckedColor;
    QColor taskUncheckedColor;
};

class ThemeManager : public QObject {
    Q_OBJECT

public:
    static ThemeManager &instance();

    ThemeMode currentMode() const { return m_mode; }
    void setMode(ThemeMode mode);
    void toggleMode();

    const ColorPalette &palette() const { return m_palette; }

    // Typography
    QFont fontSans(int pointSize = 11, int weight = QFont::Normal) const;
    QFont fontMono(int pointSize = 10, int weight = QFont::Normal) const;

    // Reading column layout constants
    static constexpr qreal maxReadingWidth() { return 780.0; }
    static constexpr qreal topBreathingPadding() { return 56.0; }
    static constexpr qreal bottomBreathingPadding() { return 80.0; }
    static constexpr qreal sideMarginMin() { return 24.0; }

    // QSS stylesheet generators
    QString mainStylesheet() const;
    QString sidebarStylesheet() const;
    QString findBarStylesheet() const;

    // SVG Vector Icon Generator
    QIcon icon(const QString &name, int size = 18, const QColor &customColor = QColor()) const;

signals:
    void themeChanged(ThemeMode newMode);

private:
    ThemeManager();
    void updatePalette();

    ThemeMode m_mode = ThemeMode::Light;
    ColorPalette m_palette;
};

inline const ColorPalette &currentPalette() {
    return ThemeManager::instance().palette();
}

} // namespace MarkdownTool::Theme
