#pragma once

#include "core/EditTransaction.h"
#include "mapping/DisplayProjection.h"
#include "theme/Theme.h"

#include <QTextLayout>
#include <QVector>
#include <QRectF>
#include <QPainter>
#include <memory>

namespace MarkdownTool::Editor {

struct TableCellLayout {
    QRectF rect;
    std::shared_ptr<QTextLayout> layout;
    QString text;
    Core::SourceRange sourceRange;
    int visualStart = 0;
    int visualLength = 0;
    Qt::AlignmentFlag alignment = Qt::AlignLeft;
    bool isHeader = false;
    QVector<Mapping::VisualSegment> segments;

    int sourceToVisual(int sourceOffset) const;
    int visualToSource(int visualOffset, bool preferTrailing = false) const;
};

inline bool operator==(const TableCellLayout &a, const TableCellLayout &b) {
    return a.text == b.text &&
           a.sourceRange == b.sourceRange &&
           a.visualStart == b.visualStart &&
           a.visualLength == b.visualLength &&
           a.alignment == b.alignment &&
           a.isHeader == b.isHeader &&
           a.segments == b.segments;
}
inline bool operator!=(const TableCellLayout &a, const TableCellLayout &b) {
    return !(a == b);
}

inline bool operator==(const TableCellLayout &a, const Mapping::TableCellVisual &b) {
    return a.text == b.text &&
           a.sourceRange == b.sourceRange &&
           a.visualStart == b.visualStart &&
           a.visualLength == b.visualLength &&
           a.alignment == b.alignment &&
           a.isHeader == b.isHeader &&
           a.segments == b.segments;
}
inline bool operator!=(const TableCellLayout &a, const Mapping::TableCellVisual &b) {
    return !(a == b);
}

struct TableRowLayout {
    QRectF rect;
    QVector<TableCellLayout> cells;
    bool isHeader = false;
};

struct TableBlockLayout {
    QVector<TableRowLayout> rows;
    QVector<qreal> colWidths;
    QRectF tableRect;
    int colCount = 0;
};

struct BlockLayoutData {
    QRectF rect;
    std::shared_ptr<QTextLayout> layout;
    int visualBlockIndex = 0;

    // Cache signature for zero-cost layout reuse
    QString cachedText;
    QVector<Mapping::VisualSegment> cachedSegments;
    int cachedIndentLevel = 0;
    int cachedHeadingLevel = 0;
    int cachedMarginTop = 0;
    int cachedMarginBottom = 0;
    bool cachedIsQuote = false;
    bool cachedIsCodeBlock = false;
    Parser::NodeType cachedBlockType = Parser::NodeType::Paragraph;
    bool hasIme = false;

    // Math & Diagram Embedded Component data
    bool isMathBlock = false;
    bool isMermaidBlock = false;
    bool isInteractiveActive = false;
    QString embeddedSource;
    QRectF embeddedPreviewRect;

    // Image Component data
    bool isImageBlock = false;
    QString imagePath;
    QString imageAltText;
    QRectF imageRect;

    // Table Component data
    bool isTableBlock = false;
    TableBlockLayout tableLayout;
};

class LayoutEngine {
public:
    LayoutEngine();
    ~LayoutEngine();

    void layout(const Mapping::DisplayProjection &projection,
                qreal contentWidth,
                int preeditOffset = -1,
                const QString &preeditString = QString());

    qreal totalHeight() const { return m_totalHeight; }
    qreal totalWidth() const { return m_totalWidth; }
    qreal contentLeft() const { return m_contentLeft; }
    qreal contentWidth() const { return m_contentWidth; }

    int hitTest(const QPointF &point, const Mapping::DisplayProjection &projection) const;
    QRectF cursorRect(int sourceOffset, const Mapping::DisplayProjection &projection) const;

    void paint(QPainter *painter,
               const QRectF &visibleRect,
               const Mapping::DisplayProjection &projection,
               const QVector<Core::SelectionState> &selections,
               const QString &preeditString = QString());

    void paint(QPainter *painter,
               const QRectF &visibleRect,
               const Mapping::DisplayProjection &projection,
               int selectionStart,
               int selectionEnd,
               const QString &preeditString = QString());

    const QVector<BlockLayoutData> &blocks() const { return m_blocks; }
    void invalidateLayout();

    static bool isBlockLayoutEqual(const Mapping::VisualBlock &vBlock,
                                   const BlockLayoutData &prevData,
                                   bool hasImeHere = false);

private:
    void layoutSingleBlock(const Mapping::VisualBlock &vBlock,
                           int blockIndex,
                           qreal &currentY,
                           int preeditOffset,
                           const QString &preeditString);

    void reuseBlock(const BlockLayoutData &src,
                    const Mapping::VisualBlock &vBlock,
                    int blockIndex,
                    qreal &currentY);

    QVector<BlockLayoutData> m_blocks;
    qreal m_totalHeight = 0;
    qreal m_totalWidth = 0;
    qreal m_lastViewportWidth = 0;
    qreal m_lastContentWidth = 0;
    qreal m_lastBasePointSize = 0;
    int m_lastFontPreset = -1;
    QString m_lastCustomFont;
    qreal m_contentLeft = 20.0;
    qreal m_contentWidth = 780.0;
};

} // namespace MarkdownTool::Editor
