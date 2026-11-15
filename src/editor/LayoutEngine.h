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

    static bool isBlockLayoutEqual(const Mapping::VisualBlock &vBlock,
                                   const BlockLayoutData &prevData,
                                   bool hasImeHere);

    QVector<BlockLayoutData> m_blocks;
    qreal m_totalHeight = 0;
    qreal m_lastViewportWidth = 0;
    qreal m_lastContentWidth = 0;
    qreal m_lastBasePointSize = 0;
    int m_lastFontPreset = -1;
    QString m_lastCustomFont;
    qreal m_contentLeft = 20.0;
    qreal m_contentWidth = 780.0;
};

} // namespace MarkdownTool::Editor
