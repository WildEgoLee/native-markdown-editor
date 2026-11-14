#pragma once

#include "mapping/DisplayProjection.h"

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
    int cachedSegmentCount = 0;
    int cachedIndentLevel = 0;
    bool cachedIsQuote = false;
    Parser::NodeType cachedBlockType = Parser::NodeType::Paragraph;
    bool hasIme = false;
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

    int hitTest(const QPointF &point, const Mapping::DisplayProjection &projection) const;
    QRectF cursorRect(int sourceOffset, const Mapping::DisplayProjection &projection) const;

    void paint(QPainter *painter,
               const QRectF &visibleRect,
               const Mapping::DisplayProjection &projection,
               int selectionStart,
               int selectionEnd,
               const QString &preeditString = QString());

    const QVector<BlockLayoutData> &blocks() const { return m_blocks; }

private:
    void layoutSingleBlock(const Mapping::VisualBlock &vBlock,
                           int blockIndex,
                           qreal contentWidth,
                           qreal &currentY,
                           int preeditOffset,
                           const QString &preeditString);

    void reuseBlock(const BlockLayoutData &src,
                    const Mapping::VisualBlock &vBlock,
                    int blockIndex,
                    qreal contentWidth,
                    qreal &currentY);

    static bool isBlockLayoutEqual(const Mapping::VisualBlock &vBlock,
                                   const BlockLayoutData &prevData,
                                   bool hasImeHere);

    QVector<BlockLayoutData> m_blocks;
    qreal m_totalHeight = 0;
    qreal m_lastWidth = 0;
};

} // namespace MarkdownTool::Editor
