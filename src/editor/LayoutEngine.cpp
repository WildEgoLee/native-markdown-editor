#include "LayoutEngine.h"

#include <QPainterPath>
#include <QAbstractTextDocumentLayout>
#include <algorithm>

namespace MarkdownTool::Editor {

LayoutEngine::LayoutEngine() = default;
LayoutEngine::~LayoutEngine() = default;

void LayoutEngine::layout(const Mapping::DisplayProjection &projection,
                          qreal contentWidth,
                          int preeditOffset,
                          const QString &preeditString)
{
    m_blocks.clear();
    m_totalHeight = 0;
    m_lastWidth = contentWidth;

    const auto &vBlocks = projection.blocks();
    if (vBlocks.isEmpty()) {
        return;
    }

    qreal currentY = 20; // Top padding

    for (int i = 0; i < vBlocks.size(); ++i) {
        const auto &vBlock = vBlocks[i];
        BlockLayoutData bData;
        bData.visualBlockIndex = i;
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

        m_blocks.append(std::move(bData));
    }

    m_totalHeight = currentY + 40.0; // Bottom padding
}

int LayoutEngine::hitTest(const QPointF &point, const Mapping::DisplayProjection &projection) const {
    if (m_blocks.isEmpty()) {
        return 0;
    }

    const auto &vBlocks = projection.blocks();

    int targetIndex = -1;
    for (int i = 0; i < m_blocks.size(); ++i) {
        const auto &b = m_blocks[i];
        if (point.y() >= b.rect.top() && point.y() <= b.rect.bottom()) {
            targetIndex = i;
            break;
        }
    }

    if (targetIndex == -1) {
        if (point.y() < m_blocks.first().rect.top()) {
            targetIndex = 0;
        } else {
            targetIndex = m_blocks.size() - 1;
        }
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
    const auto &vBlocks = projection.blocks();

    painter->setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < m_blocks.size(); ++i) {
        const auto &bData = m_blocks[i];
        if (!bData.rect.intersects(visibleRect)) {
            continue; // Viewport clipping
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

        // Selection highlight
        if (selectionStart != selectionEnd && bData.layout) {
            int blockSourceStart = vBlock.sourceRange.start;
            int blockSourceEnd = vBlock.sourceRange.end;

            int selS = qMax(selectionStart, blockSourceStart);
            int selE = qMin(selectionEnd, blockSourceEnd);

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

        // Draw layout text
        if (bData.layout) {
            bData.layout->draw(painter, bData.rect.topLeft());
        }
    }
}

} // namespace MarkdownTool::Editor
