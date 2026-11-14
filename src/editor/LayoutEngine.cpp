#include "LayoutEngine.h"

#include <QPainterPath>
#include <QAbstractTextDocumentLayout>
#include <algorithm>

namespace MarkdownTool::Editor {

LayoutEngine::LayoutEngine() = default;
LayoutEngine::~LayoutEngine() = default;

void LayoutEngine::layoutSingleBlock(const Mapping::VisualBlock &vBlock,
                                     int blockIndex,
                                     qreal contentWidth,
                                     qreal &currentY,
                                     int preeditOffset,
                                     const QString &preeditString)
{
    BlockLayoutData bData;
    bData.visualBlockIndex = blockIndex;
    bData.layout = std::make_shared<QTextLayout>();

    QString lineText = vBlock.visualText;
    QList<QTextLayout::FormatRange> formats;

    bool hasImeHere = !preeditString.isEmpty() && preeditOffset >= 0 && vBlock.sourceRange.contains(preeditOffset);
    int imeInsertPos = hasImeHere ? vBlock.sourceToVisual(preeditOffset) : -1;

    if (hasImeHere && imeInsertPos >= 0 && imeInsertPos <= lineText.length()) {
        lineText.insert(imeInsertPos, preeditString);

        // Shift subsequent format ranges
        for (const auto &seg : vBlock.segments) {
            if (seg.visualLength > 0) {
                QTextLayout::FormatRange fr;
                fr.start = (seg.visualStart >= imeInsertPos) ? (seg.visualStart + preeditString.length()) : seg.visualStart;
                fr.length = seg.visualLength;
                fr.format = seg.format;
                formats.append(fr);
            }
        }

        // Add IME preedit underline format
        QTextLayout::FormatRange imeFr;
        imeFr.start = imeInsertPos;
        imeFr.length = preeditString.length();
        imeFr.format.setFontUnderline(true);
        imeFr.format.setForeground(QColor(50, 130, 240));
        formats.append(imeFr);
    } else {
        // Apply normal format ranges
        for (const auto &seg : vBlock.segments) {
            if (seg.visualLength > 0) {
                QTextLayout::FormatRange fr;
                fr.start = seg.visualStart;
                fr.length = seg.visualLength;
                fr.format = seg.format;
                formats.append(fr);
            }
        }
    }

    bData.layout->setText(lineText);
    bData.layout->setFormats(formats);

    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    bData.layout->setTextOption(option);

    qreal indent = vBlock.indentLevel * 24.0;
    if (vBlock.isQuote) {
        indent += 16.0;
    }
    qreal blockContentWidth = qMax(100.0, contentWidth - indent - 40.0); // 40 = margins

    currentY += vBlock.marginTop;

    bData.layout->beginLayout();
    qreal y = 0;
    while (true) {
        QTextLine line = bData.layout->createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLineWidth(blockContentWidth);
        line.setPosition(QPointF(indent, y));
        y += line.height();
    }
    bData.layout->endLayout();

    if (y < 24.0) {
        y = 24.0; // Minimum block height
    }

    bData.rect = QRectF(20.0, currentY, contentWidth - 40.0, y);
    currentY += y + vBlock.marginBottom;

    bData.cachedText = vBlock.visualText;
    bData.cachedSegments = vBlock.segments;
    bData.cachedIndentLevel = vBlock.indentLevel;
    bData.cachedHeadingLevel = vBlock.headingLevel;
    bData.cachedMarginTop = vBlock.marginTop;
    bData.cachedMarginBottom = vBlock.marginBottom;
    bData.cachedIsQuote = vBlock.isQuote;
    bData.cachedIsCodeBlock = vBlock.isCodeBlock;
    bData.cachedBlockType = vBlock.blockType;
    bData.hasIme = hasImeHere;

    m_blocks.append(std::move(bData));
}

void LayoutEngine::reuseBlock(const BlockLayoutData &src,
                              const Mapping::VisualBlock &vBlock,
                              int blockIndex,
                              qreal contentWidth,
                              qreal &currentY)
{
    BlockLayoutData bData;
    bData.visualBlockIndex = blockIndex;
    bData.layout = src.layout; // Direct reuse without text shaping or line layout
    bData.cachedText = src.cachedText;
    bData.cachedSegments = src.cachedSegments;
    bData.cachedIndentLevel = src.cachedIndentLevel;
    bData.cachedHeadingLevel = src.cachedHeadingLevel;
    bData.cachedMarginTop = src.cachedMarginTop;
    bData.cachedMarginBottom = src.cachedMarginBottom;
    bData.cachedIsQuote = src.cachedIsQuote;
    bData.cachedIsCodeBlock = src.cachedIsCodeBlock;
    bData.cachedBlockType = src.cachedBlockType;
    bData.hasIme = false;

    currentY += vBlock.marginTop;
    qreal h = src.rect.height();
    bData.rect = QRectF(20.0, currentY, contentWidth - 40.0, h);
    currentY += h + vBlock.marginBottom;

    m_blocks.append(std::move(bData));
}

bool LayoutEngine::isBlockLayoutEqual(const Mapping::VisualBlock &vBlock,
                                      const BlockLayoutData &prevData,
                                      bool hasImeHere)
{
    if (prevData.hasIme || hasImeHere) {
        return false;
    }
    if (!prevData.layout) {
        return false;
    }
    if (prevData.cachedText != vBlock.visualText ||
        prevData.cachedBlockType != vBlock.blockType ||
        prevData.cachedHeadingLevel != vBlock.headingLevel ||
        prevData.cachedIndentLevel != vBlock.indentLevel ||
        prevData.cachedIsQuote != vBlock.isQuote ||
        prevData.cachedIsCodeBlock != vBlock.isCodeBlock ||
        prevData.cachedMarginTop != vBlock.marginTop ||
        prevData.cachedMarginBottom != vBlock.marginBottom ||
        prevData.cachedSegments.size() != vBlock.segments.size())
    {
        return false;
    }

    for (int s = 0; s < vBlock.segments.size(); ++s) {
        const auto &s1 = vBlock.segments[s];
        const auto &s2 = prevData.cachedSegments[s];
        if (s1.type != s2.type ||
            s1.visualStart != s2.visualStart ||
            s1.visualLength != s2.visualLength ||
            s1.format != s2.format)
        {
            return false;
        }
    }

    return true;
}

void LayoutEngine::layout(const Mapping::DisplayProjection &projection,
                          qreal contentWidth,
                          int preeditOffset,
                          const QString &preeditString)
{
    const auto &vBlocks = projection.blocks();
    if (vBlocks.isEmpty()) {
        m_blocks.clear();
        m_totalHeight = 0;
        m_lastWidth = contentWidth;
        return;
    }

    bool canReuse = (m_lastWidth > 0 && contentWidth == m_lastWidth && !m_blocks.isEmpty());
    m_lastWidth = contentWidth;

    const int N = vBlocks.size();
    const int M = m_blocks.size();

    QVector<BlockLayoutData> oldBlocks = std::move(m_blocks);
    m_blocks.clear();
    m_blocks.reserve(N);

    qreal currentY = 20.0; // Top padding

    if (!canReuse) {
        for (int i = 0; i < N; ++i) {
            layoutSingleBlock(vBlocks[i], i, contentWidth, currentY, preeditOffset, preeditString);
        }
        m_totalHeight = currentY + 40.0;
        return;
    }

    // 1. Prefix matching from beginning
    int prefixLen = 0;
    while (prefixLen < N && prefixLen < M) {
        bool hasIme = !preeditString.isEmpty() && preeditOffset >= 0 && vBlocks[prefixLen].sourceRange.contains(preeditOffset);
        if (!isBlockLayoutEqual(vBlocks[prefixLen], oldBlocks[prefixLen], hasIme)) {
            break;
        }
        ++prefixLen;
    }

    // 2. Suffix matching from end
    int suffixLen = 0;
    while (suffixLen < (N - prefixLen) && suffixLen < (M - prefixLen)) {
        int vIdx = N - 1 - suffixLen;
        int oldIdx = M - 1 - suffixLen;
        bool hasIme = !preeditString.isEmpty() && preeditOffset >= 0 && vBlocks[vIdx].sourceRange.contains(preeditOffset);
        if (!isBlockLayoutEqual(vBlocks[vIdx], oldBlocks[oldIdx], hasIme)) {
            break;
        }
        ++suffixLen;
    }

    // 3. Assemble: Prefix (reused layout)
    for (int i = 0; i < prefixLen; ++i) {
        reuseBlock(oldBlocks[i], vBlocks[i], i, contentWidth, currentY);
    }

    // 4. Assemble: Modified / New blocks
    for (int i = prefixLen; i < N - suffixLen; ++i) {
        layoutSingleBlock(vBlocks[i], i, contentWidth, currentY, preeditOffset, preeditString);
    }

    // 5. Assemble: Suffix (reused layout)
    for (int i = N - suffixLen; i < N; ++i) {
        int oldIdx = M - (N - i);
        reuseBlock(oldBlocks[oldIdx], vBlocks[i], i, contentWidth, currentY);
    }

    m_totalHeight = currentY + 40.0;
}

int LayoutEngine::hitTest(const QPointF &point, const Mapping::DisplayProjection &projection) const {
    if (m_blocks.isEmpty()) {
        return 0;
    }

    const auto &vBlocks = projection.blocks();

    // Binary search for block intersecting point.y()
    auto it = std::lower_bound(m_blocks.begin(), m_blocks.end(), point.y(),
        [](const BlockLayoutData &b, qreal y) {
            return b.rect.bottom() < y;
        });

    int targetIndex = 0;
    if (it == m_blocks.end()) {
        targetIndex = m_blocks.size() - 1;
    } else {
        targetIndex = static_cast<int>(std::distance(m_blocks.begin(), it));
    }

    const auto &bData = m_blocks[targetIndex];
    const auto &vBlock = vBlocks[bData.visualBlockIndex];

    if (!bData.layout || vBlock.visualText.isEmpty()) {
        return vBlock.sourceRange.start;
    }

    qreal relX = point.x() - bData.rect.left();
    qreal relY = point.y() - bData.rect.top();

    QTextLine line;
    for (int l = 0; l < bData.layout->lineCount(); ++l) {
        QTextLine curLine = bData.layout->lineAt(l);
        if (relY >= curLine.y() && relY <= curLine.y() + curLine.height()) {
            line = curLine;
            break;
        }
    }
    if (!line.isValid()) {
        if (relY <= 0) {
            line = bData.layout->lineAt(0);
        } else {
            line = bData.layout->lineAt(bData.layout->lineCount() - 1);
        }
    }

    int visualCursor = line.xToCursor(relX);
    return vBlock.visualToSource(visualCursor);
}

QRectF LayoutEngine::cursorRect(int sourceOffset, const Mapping::DisplayProjection &projection) const {
    if (m_blocks.isEmpty()) {
        return QRectF(20, 20, 2, 24);
    }

    const auto &vBlocks = projection.blocks();
    int blockIdx = projection.findBlockIndexBySourceOffset(sourceOffset);
    if (blockIdx < 0 || blockIdx >= m_blocks.size()) {
        blockIdx = 0;
    }

    const auto &bData = m_blocks[blockIdx];
    const auto &vBlock = vBlocks[bData.visualBlockIndex];

    int visualOffset = vBlock.sourceToVisual(sourceOffset);

    if (!bData.layout || bData.layout->lineCount() == 0) {
        return QRectF(bData.rect.left(), bData.rect.top(), 2, 24);
    }

    QTextLine line = bData.layout->lineForTextPosition(visualOffset);
    if (!line.isValid()) {
        line = bData.layout->lineAt(bData.layout->lineCount() - 1);
    }

    qreal cursorX = bData.rect.left() + line.cursorToX(visualOffset);
    qreal cursorY = bData.rect.top() + line.y();
    qreal cursorH = line.height();

    return QRectF(cursorX, cursorY, 2, cursorH);
}

void LayoutEngine::paint(QPainter *painter,
                         const QRectF &visibleRect,
                         const Mapping::DisplayProjection &projection,
                         int selectionStart,
                         int selectionEnd,
                         const QString &preeditString)
{
    QVector<Core::SelectionState> sels;
    if (selectionStart != selectionEnd) {
        sels.append(Core::SelectionState(selectionStart, selectionEnd));
    }
    paint(painter, visibleRect, projection, sels, preeditString);
}

void LayoutEngine::paint(QPainter *painter,
                         const QRectF &visibleRect,
                         const Mapping::DisplayProjection &projection,
                         const QVector<Core::SelectionState> &selections,
                         const QString &preeditString)
{
    if (m_blocks.isEmpty()) {
        return;
    }

    const auto &vBlocks = projection.blocks();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Binary search for first block that could intersect visibleRect
    auto it = std::lower_bound(m_blocks.begin(), m_blocks.end(), visibleRect.top(),
        [](const BlockLayoutData &b, qreal y) {
            return b.rect.bottom() < y;
        });
    int startIdx = (it != m_blocks.end()) ? static_cast<int>(std::distance(m_blocks.begin(), it)) : 0;

    for (int i = startIdx; i < m_blocks.size(); ++i) {
        const auto &bData = m_blocks[i];
        if (bData.rect.top() > visibleRect.bottom()) {
            break; // Past the visible viewport, stop rendering!
        }

        const auto &vBlock = vBlocks[bData.visualBlockIndex];

        // Draw block specific decorations
        if (vBlock.isCodeBlock) {
            // Dark rounded background
            painter->setBrush(QColor(40, 44, 52));
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(bData.rect.adjusted(-6, -4, 6, 4), 6, 6);
        } else if (vBlock.isQuote) {
            // Left quote bar
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(74, 144, 226));
            painter->drawRoundedRect(QRectF(bData.rect.left() + 4, bData.rect.top(), 3.5, bData.rect.height()), 1.5, 1.5);
        }

        // Selection highlights (supports multiple selections)
        if (!selections.isEmpty() && bData.layout) {
            int blockSourceStart = vBlock.sourceRange.start;
            int blockSourceEnd = vBlock.sourceRange.end;

            for (const auto &sel : selections) {
                if (!sel.hasSelection()) continue;

                int selS = qMax(sel.selectionStart(), blockSourceStart);
                int selE = qMin(sel.selectionEnd(), blockSourceEnd);

                if (selS < selE) {
                    int vSelS = vBlock.sourceToVisual(selS);
                    int vSelE = vBlock.sourceToVisual(selE);

                    painter->setPen(Qt::NoPen);
                    painter->setBrush(QColor(180, 215, 255, 120));

                    for (int l = 0; l < bData.layout->lineCount(); ++l) {
                        QTextLine line = bData.layout->lineAt(l);
                        int lineStart = line.textStart();
                        int lineEnd = lineStart + line.textLength();

                        int segS = qMax(vSelS, lineStart);
                        int segE = qMin(vSelE, lineEnd);
                        if (segS < segE) {
                            qreal x1 = bData.rect.left() + line.cursorToX(segS);
                            qreal x2 = bData.rect.left() + line.cursorToX(segE);
                            QRectF selRect(qMin(x1, x2), bData.rect.top() + line.y(), qAbs(x2 - x1), line.height());
                            painter->drawRect(selRect);
                        }
                    }
                }
            }
        }

        // Draw layout text
        if (bData.layout) {
            bData.layout->draw(painter, bData.rect.topLeft());
        }
    }
}

} // namespace MarkdownTool::Editor
