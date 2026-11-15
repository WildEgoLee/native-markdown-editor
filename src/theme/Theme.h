#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QObject>

class QSettings;

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
    QColor accentText;
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
    void applyApplicationPalette();

    void loadSettings(QSettings *settings = nullptr);
    void saveSettings(QSettings *settings = nullptr);

    const ColorPalette &palette() const { return m_palette; }

    // Typography
    enum class FontPreset {
        SansSerif,  // 现代无衬线 (微软雅黑 / PingFang SC / Segoe UI)
        Serif,      // 典雅书卷体 (宋体 / 思源宋体 / Georgia / Cambria)
        Monospace,  // 极客编程等宽 (Cascadia Code / Consolas / JetBrains Mono)
        Custom      // 用户自定义字体
    };

    FontPreset fontPreset() const { return m_fontPreset; }
    void setFontPreset(FontPreset preset);

    QString customFontFamily() const { return m_customFontFamily; }
    void setCustomFontFamily(const QString &family);

    QStringList proseFontFamilies() const;
    QString currentFontDisplayName() const;

    qreal basePointSize() const { return m_basePointSize; }
    void setBasePointSize(qreal pt);
    void zoomIn();
    void zoomOut();
    void resetZoom();

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
    void typographyChanged();

private:
    ThemeManager();
    void updatePalette();

    ThemeMode m_mode = ThemeMode::Light;
    ColorPalette m_palette;

    FontPreset m_fontPreset = FontPreset::SansSerif;
    QString m_customFontFamily;
    qreal m_basePointSize = 12.0;
};

inline const ColorPalette &currentPalette() {
    return ThemeManager::instance().palette();
}

} // namespace MarkdownTool::Theme
