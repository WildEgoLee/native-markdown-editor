#include "LayoutEngine.h"
#include "render/AsyncRenderService.h"
#include "render/MathSvgRenderer.h"
#include "render/MermaidSvgRenderer.h"

#include <QPainterPath>
#include <QAbstractTextDocumentLayout>
#include <algorithm>

namespace MarkdownTool::Editor {

LayoutEngine::LayoutEngine() = default;
LayoutEngine::~LayoutEngine() = default;

void LayoutEngine::layoutSingleBlock(const Mapping::VisualBlock &vBlock,
                                     int blockIndex,
                                     qreal &currentY,
                                     int preeditOffset,
                                     const QString &preeditString)
{
    BlockLayoutData bData;
    bData.visualBlockIndex = blockIndex;
    bData.isMathBlock = vBlock.isMathBlock;
    bData.isMermaidBlock = vBlock.isMermaidBlock;
    bData.isInteractiveActive = vBlock.isInteractiveActive;
    bData.embeddedSource = vBlock.embeddedSource;

    // Collapsed Embedded Component (Live Preview Mode, cursor outside)
    if ((vBlock.isMathBlock || vBlock.isMermaidBlock) && !vBlock.isInteractiveActive) {
        currentY += vBlock.marginTop;
        qreal cardH = 80.0;
        if (vBlock.isMermaidBlock) {
            cardH = std::max(160.0, static_cast<qreal>(Render::MermaidSvgRenderer::estimateSize(vBlock.embeddedSource).height() + 30.0));
        } else {
            cardH = std::max(70.0, static_cast<qreal>(Render::MathSvgRenderer::estimateSize(vBlock.embeddedSource).height() + 24.0));
        }
        bData.rect = QRectF(m_contentLeft, currentY, m_contentWidth, cardH);
        bData.embeddedPreviewRect = bData.rect;
        currentY += cardH + vBlock.marginBottom;

        bData.cachedText = vBlock.visualText;
        bData.cachedSegments = vBlock.segments;
        bData.cachedIndentLevel = vBlock.indentLevel;
        bData.cachedHeadingLevel = vBlock.headingLevel;
        bData.cachedMarginTop = vBlock.marginTop;
        bData.cachedMarginBottom = vBlock.marginBottom;
        bData.cachedIsQuote = vBlock.isQuote;
        bData.cachedIsCodeBlock = vBlock.isCodeBlock;
        bData.cachedBlockType = vBlock.blockType;
        bData.hasIme = false;

        m_blocks.append(std::move(bData));
        return;
    }

    // Real Table Layout (Live Preview Mode)
    if (vBlock.isTableBlock && !vBlock.tableData.rows.isEmpty()) {
        bData.isTableBlock = true;
        currentY += vBlock.marginTop;

        const auto &tableData = vBlock.tableData;
        int numRows = tableData.rows.size();
        int numCols = tableData.colCount;
        if (numCols <= 0) {
            for (const auto &r : tableData.rows) {
                numCols = qMax(numCols, r.cells.size());
            }
        }
        numCols = qMax(1, numCols);
        bData.tableLayout.colCount = numCols;

        // 1. Measure natural column widths
        QVector<qreal> naturalColWidths(numCols, 60.0);
        QFont headerFont = Theme::ThemeManager::instance().fontSans(static_cast<int>(Theme::ThemeManager::instance().basePointSize()), QFont::Bold);
        QFont normalFont = Theme::ThemeManager::instance().fontSans(static_cast<int>(Theme::ThemeManager::instance().basePointSize()), QFont::Normal);
        QFontMetricsF fmHeader(headerFont);
        QFontMetricsF fmNormal(normalFont);

        for (int r = 0; r < numRows; ++r) {
            const auto &row = tableData.rows[r];
            const auto &fm = row.isHeader ? fmHeader : fmNormal;
            for (int c = 0; c < row.cells.size() && c < numCols; ++c) {
                qreal w = fm.horizontalAdvance(row.cells[c].text) + 24.0;
                naturalColWidths[c] = qMax(naturalColWidths[c], w);
            }
        }

        // 2. Distribute available content width (m_contentWidth)
        qreal totalNatural = 0.0;
        for (qreal w : naturalColWidths) totalNatural += w;

        QVector<qreal> colWidths(numCols, 60.0);
        if (totalNatural <= m_contentWidth && totalNatural > 0.0) {
            for (int c = 0; c < numCols; ++c) {
                colWidths[c] = (naturalColWidths[c] / totalNatural) * m_contentWidth;
            }
        } else {
            for (int c = 0; c < numCols; ++c) {
                qreal ratio = (totalNatural > 0.0) ? (naturalColWidths[c] / totalNatural) : (1.0 / numCols);
                colWidths[c] = qMax(60.0, ratio * m_contentWidth);
            }
        }
        bData.tableLayout.colWidths = colWidths;

        // 3. Lay out rows and cells
        qreal rowY = currentY;
        bData.tableLayout.rows.reserve(numRows);

        for (int r = 0; r < numRows; ++r) {
            const auto &vRow = tableData.rows[r];
            TableRowLayout rowLayout;
            rowLayout.isHeader = vRow.isHeader;
            rowLayout.cells.reserve(numCols);

            qreal maxRowH = 34.0;

            for (int c = 0; c < numCols; ++c) {
                TableCellLayout cellLayout;
                cellLayout.isHeader = vRow.isHeader;
                if (c < vRow.cells.size()) {
                    cellLayout.sourceRange = vRow.cells[c].sourceRange;
                    cellLayout.alignment = vRow.cells[c].alignment;
                    cellLayout.visualStart = vRow.cells[c].visualStart;
                    cellLayout.visualLength = vRow.cells[c].visualLength;
                }

                qreal cellX = m_contentLeft;
                for (int i = 0; i < c; ++i) {
                    cellX += colWidths[i];
                }
                qreal cellW = colWidths[c];

                cellLayout.layout = std::make_shared<QTextLayout>();
                QString text = (c < vRow.cells.size()) ? vRow.cells[c].text : QString();
                cellLayout.layout->setText(text);
                cellLayout.layout->setFont(vRow.isHeader ? headerFont : normalFont);

                QTextOption opt;
                opt.setAlignment(cellLayout.alignment);
                opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
                cellLayout.layout->setTextOption(opt);

                qreal innerW = qMax(20.0, cellW - 20.0);
                cellLayout.layout->beginLayout();
                qreal lineY = 0;
                while (true) {
                    QTextLine line = cellLayout.layout->createLine();
                    if (!line.isValid()) break;
                    line.setLineWidth(innerW);
                    line.setPosition(QPointF(0, lineY));
                    lineY += line.height();
                }
                cellLayout.layout->endLayout();

                qreal cellH = lineY + 16.0;
                maxRowH = qMax(maxRowH, cellH);

                rowLayout.cells.append(std::move(cellLayout));
            }

            rowLayout.rect = QRectF(m_contentLeft, rowY, m_contentWidth, maxRowH);
            for (int c = 0; c < rowLayout.cells.size(); ++c) {
                qreal cellX = m_contentLeft;
                for (int i = 0; i < c; ++i) cellX += colWidths[i];
                rowLayout.cells[c].rect = QRectF(cellX, rowY, colWidths[c], maxRowH);
            }

            rowY += maxRowH;
            bData.tableLayout.rows.append(std::move(rowLayout));
        }

        qreal totalTableH = rowY - currentY;
        bData.rect = QRectF(m_contentLeft, currentY, m_contentWidth, totalTableH);
        bData.tableLayout.tableRect = bData.rect;
        currentY += totalTableH + vBlock.marginBottom;

        bData.cachedText = vBlock.visualText;
        bData.cachedSegments = vBlock.segments;
        bData.cachedIndentLevel = vBlock.indentLevel;
        bData.cachedHeadingLevel = vBlock.headingLevel;
        bData.cachedMarginTop = vBlock.marginTop;
        bData.cachedMarginBottom = vBlock.marginBottom;
        bData.cachedIsQuote = vBlock.isQuote;
        bData.cachedIsCodeBlock = vBlock.isCodeBlock;
        bData.cachedBlockType = vBlock.blockType;
        bData.hasIme = false;

        m_blocks.append(std::move(bData));
        return;
    }

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
        imeFr.format.setForeground(Theme::currentPalette().accent);
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
        indent += 18.0;
    }
    qreal blockContentWidth = qMax(80.0, m_contentWidth - indent - 8.0);

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

    qreal previewH = 0.0;
    if (vBlock.isMathBlock || vBlock.isMermaidBlock) {
        if (vBlock.isMermaidBlock) {
            previewH = std::max(150.0, static_cast<qreal>(Render::MermaidSvgRenderer::estimateSize(vBlock.embeddedSource).height() + 20.0));
        } else {
            previewH = std::max(64.0, static_cast<qreal>(Render::MathSvgRenderer::estimateSize(vBlock.embeddedSource).height() + 20.0));
        }
    }

    qreal totalBlockH = y + (previewH > 0 ? (previewH + 12.0) : 0.0);
    bData.rect = QRectF(m_contentLeft, currentY, m_contentWidth, totalBlockH);
    if (previewH > 0) {
        bData.embeddedPreviewRect = QRectF(bData.rect.left(), bData.rect.top() + y + 8.0, bData.rect.width(), previewH);
    }
    currentY += totalBlockH + vBlock.marginBottom;

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
                              qreal &currentY)
{
    BlockLayoutData bData;
    bData.visualBlockIndex = blockIndex;
    bData.layout = src.layout; // Direct reuse without text shaping or line layout
    bData.isMathBlock = src.isMathBlock;
    bData.isMermaidBlock = src.isMermaidBlock;
    bData.isTableBlock = src.isTableBlock;
    bData.tableLayout = src.tableLayout;
    bData.isInteractiveActive = src.isInteractiveActive;
    bData.embeddedSource = src.embeddedSource;
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
    qreal dy = currentY - src.rect.top();
    bData.rect = QRectF(m_contentLeft, currentY, m_contentWidth, h);
    if (!src.embeddedPreviewRect.isEmpty()) {
        bData.embeddedPreviewRect = QRectF(m_contentLeft, currentY + (src.embeddedPreviewRect.top() - src.rect.top()), m_contentWidth, src.embeddedPreviewRect.height());
    }
    if (bData.isTableBlock) {
        bData.tableLayout.tableRect = bData.rect;
        for (auto &row : bData.tableLayout.rows) {
            row.rect.translate(0, dy);
            for (auto &cell : row.cells) {
                cell.rect.translate(0, dy);
            }
        }
    }
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
    if (prevData.isMathBlock != vBlock.isMathBlock ||
        prevData.isMermaidBlock != vBlock.isMermaidBlock ||
        prevData.isTableBlock != vBlock.isTableBlock ||
        prevData.isInteractiveActive != vBlock.isInteractiveActive ||
        prevData.embeddedSource != vBlock.embeddedSource)
    {
        return false;
    }
    if (!prevData.layout && (!prevData.isMathBlock && !prevData.isMermaidBlock && !prevData.isTableBlock)) {
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

void LayoutEngine::invalidateLayout() {
    m_lastViewportWidth = -1;
    m_lastContentWidth = -1;
    m_lastBasePointSize = -1;
    m_lastFontPreset = -1;
    m_lastCustomFont.clear();
    m_blocks.clear();
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
        m_lastViewportWidth = contentWidth;
        m_lastContentWidth = 0;
        return;
    }

    qreal maxW = Theme::ThemeManager::instance().maxReadingWidth();
    qreal sidePad = Theme::ThemeManager::sideMarginMin();

    if (contentWidth > maxW + sidePad * 2.0) {
        m_contentWidth = maxW;
        m_contentLeft = (contentWidth - maxW) / 2.0;
    } else {
        m_contentLeft = qMax(16.0, qMin(24.0, (contentWidth - 100.0) / 2.0));
        m_contentWidth = qMax(100.0, contentWidth - m_contentLeft * 2.0);
    }

    qreal currentPt = Theme::ThemeManager::instance().basePointSize();
    int currentPreset = static_cast<int>(Theme::ThemeManager::instance().fontPreset());
    const QString &currentCustomFont = Theme::ThemeManager::instance().customFontFamily();

    bool widthMatch = (m_lastViewportWidth > 0 && contentWidth == m_lastViewportWidth && m_contentWidth == m_lastContentWidth);
    bool typographyMatch = (std::abs(currentPt - m_lastBasePointSize) < 0.01 &&
                            currentPreset == m_lastFontPreset &&
                            currentCustomFont == m_lastCustomFont);

    bool canReuse = (widthMatch && typographyMatch && !m_blocks.isEmpty());

    m_lastViewportWidth = contentWidth;
    m_lastContentWidth = m_contentWidth;
    m_lastBasePointSize = currentPt;
    m_lastFontPreset = currentPreset;
    m_lastCustomFont = currentCustomFont;

    const int N = vBlocks.size();
    const int M = m_blocks.size();

    QVector<BlockLayoutData> oldBlocks = std::move(m_blocks);
    m_blocks.clear();
    m_blocks.reserve(N);

    qreal currentY = Theme::ThemeManager::topBreathingPadding();

    if (!canReuse) {
        for (int i = 0; i < N; ++i) {
            layoutSingleBlock(vBlocks[i], i, currentY, preeditOffset, preeditString);
        }
        m_totalHeight = currentY + Theme::ThemeManager::bottomBreathingPadding();
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
        reuseBlock(oldBlocks[i], vBlocks[i], i, currentY);
    }

    // 4. Assemble: Modified / New blocks
    for (int i = prefixLen; i < N - suffixLen; ++i) {
        layoutSingleBlock(vBlocks[i], i, currentY, preeditOffset, preeditString);
    }

    // 5. Assemble: Suffix (reused layout)
    for (int i = N - suffixLen; i < N; ++i) {
        int oldIdx = M - (N - i);
        reuseBlock(oldBlocks[oldIdx], vBlocks[i], i, currentY);
    }

    m_totalHeight = currentY + Theme::ThemeManager::bottomBreathingPadding();
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
    if (bData.visualBlockIndex < 0 || bData.visualBlockIndex >= vBlocks.size()) {
        return 0;
    }
    const auto &vBlock = vBlocks[bData.visualBlockIndex];

    if (bData.isTableBlock) {
        for (const auto &row : bData.tableLayout.rows) {
            if (point.y() >= row.rect.top() && point.y() <= row.rect.bottom()) {
                for (const auto &cell : row.cells) {
                    if (point.x() >= cell.rect.left() && point.x() <= cell.rect.right()) {
                        if (cell.layout && cell.layout->lineCount() > 0) {
                            qreal cellContentH = cell.layout->boundingRect().height();
                            qreal textY = cell.rect.top() + (cell.rect.height() - cellContentH) / 2.0;
                            qreal relY = point.y() - textY;
                            qreal relX = point.x() - (cell.rect.left() + 10.0);
                            QTextLine line;
                            for (int l = 0; l < cell.layout->lineCount(); ++l) {
                                QTextLine curLine = cell.layout->lineAt(l);
                                if (relY >= curLine.y() && relY <= curLine.y() + curLine.height()) {
                                    line = curLine;
                                    break;
                                }
                            }
                            if (!line.isValid()) {
                                line = cell.layout->lineAt(0);
                            }
                            int cursorOffset = line.xToCursor(relX);
                            return qBound(cell.sourceRange.start, cell.sourceRange.start + cursorOffset, cell.sourceRange.end);
                        }
                        return cell.sourceRange.start;
                    }
                }
            }
        }
        return vBlock.sourceRange.start;
    }

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
    if (bData.visualBlockIndex < 0 || bData.visualBlockIndex >= vBlocks.size()) {
        return QRectF(bData.rect.left(), bData.rect.top(), 2, 24);
    }
    const auto &vBlock = vBlocks[bData.visualBlockIndex];

    if (bData.isTableBlock) {
        for (const auto &row : bData.tableLayout.rows) {
            for (const auto &cell : row.cells) {
                if (cell.sourceRange.contains(sourceOffset) || sourceOffset == cell.sourceRange.end) {
                    if (cell.layout && cell.layout->lineCount() > 0) {
                        qreal cellContentH = cell.layout->boundingRect().height();
                        qreal textY = cell.rect.top() + (cell.rect.height() - cellContentH) / 2.0;
                        int relOffset = sourceOffset - cell.sourceRange.start;
                        QTextLine line = cell.layout->lineForTextPosition(relOffset);
                        if (!line.isValid()) {
                            line = cell.layout->lineAt(0);
                        }
                        qreal cursorX = cell.rect.left() + 10.0 + line.cursorToX(relOffset);
                        qreal cursorY = textY + line.y();
                        return QRectF(cursorX, cursorY, 2, line.height());
                    }
                    return QRectF(cell.rect.left() + 10.0, cell.rect.top() + 6.0, 2, 20);
                }
            }
        }
    }

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

    const auto &palette = Theme::currentPalette();

    for (int i = startIdx; i < m_blocks.size(); ++i) {
        const auto &bData = m_blocks[i];
        if (bData.rect.top() > visibleRect.bottom()) {
            break; // Past the visible viewport, stop rendering!
        }

        const auto &vBlock = vBlocks[bData.visualBlockIndex];

        // Draw Math or Mermaid Embedded Component
        if (bData.isMathBlock || bData.isMermaidBlock) {
            Render::RenderKind kind = bData.isMathBlock ? Render::RenderKind::Math : Render::RenderKind::Mermaid;
            auto renderer = Render::AsyncRenderService::instance()->getRenderer(bData.embeddedSource, kind, false);

            if (!bData.isInteractiveActive) {
                // Collapsed Live Preview Mode (Cursor is outside): Render full embedded preview card!
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing, true);

                QRectF cardRect = bData.rect.adjusted(2, 2, -2, -2);
                painter->setPen(QPen(palette.cardBorder, 1.0));
                painter->setBrush(palette.cardBackground);
                painter->drawRoundedRect(cardRect, 8.0, 8.0);

                // Badge top-left
                QRectF badgeRect(cardRect.left() + 10, cardRect.top() + 6, bData.isMathBlock ? 54 : 68, 18);
                painter->setPen(Qt::NoPen);
                if (bData.isMathBlock) {
                    painter->setBrush(QColor(139, 92, 246, 35));
                    painter->drawRoundedRect(badgeRect, 4.0, 4.0);
                    painter->setPen(QColor(124, 58, 237));
                } else {
                    painter->setBrush(QColor(37, 99, 235, 35));
                    painter->drawRoundedRect(badgeRect, 4.0, 4.0);
                    painter->setPen(QColor(37, 99, 235));
                }
                QFont badgeFont = painter->font();
                badgeFont.setPointSize(8);
                badgeFont.setBold(true);
                painter->setFont(badgeFont);
                painter->drawText(badgeRect, Qt::AlignCenter, bData.isMathBlock ? QStringLiteral("LaTeX") : QStringLiteral("Mermaid"));

                // Render SVG or Placeholder
                QRectF contentRect = cardRect.adjusted(12, 24, -12, -8);
                if (renderer && renderer->isValid()) {
                    renderer->render(painter, contentRect);
                } else {
                    painter->setPen(palette.secondaryText);
                    QFont hintFont = painter->font();
                    hintFont.setPointSize(9);
                    hintFont.setItalic(true);
                    painter->setFont(hintFont);
                    painter->drawText(contentRect, Qt::AlignCenter, QStringLiteral("Rendering preview..."));
                }

                painter->restore();
                continue; // Do not draw code text when collapsed
            }
        }

        // Draw Table Component (Live Preview Mode)
        if (bData.isTableBlock) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);

            QRectF tableRect = bData.tableLayout.tableRect;

            // 1. Outer rounded container
            painter->setPen(QPen(palette.tableBorder, 1.0));
            painter->setBrush(palette.editorBackground);
            painter->drawRoundedRect(tableRect, 6.0, 6.0);

            // 2. Draw Rows (Header background & Dividers)
            for (int r = 0; r < bData.tableLayout.rows.size(); ++r) {
                const auto &row = bData.tableLayout.rows[r];

                if (row.isHeader) {
                    // Header background
                    QPainterPath headerPath;
                    headerPath.addRoundedRect(tableRect, 6.0, 6.0);
                    QRectF headerClipRect(tableRect.left(), tableRect.top(), tableRect.width(), row.rect.height());
                    painter->save();
                    painter->setClipRect(headerClipRect);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(palette.tableCellBackground);
                    painter->drawPath(headerPath);
                    painter->restore();

                    // Header bottom border
                    painter->setPen(QPen(palette.tableBorder, 1.5));
                    painter->drawLine(QPointF(tableRect.left(), row.rect.bottom()),
                                     QPointF(tableRect.right(), row.rect.bottom()));
                } else {
                    // Row bottom divider
                    painter->setPen(QPen(palette.tableBorder, 0.8));
                    painter->drawLine(QPointF(tableRect.left(), row.rect.bottom()),
                                     QPointF(tableRect.right(), row.rect.bottom()));
                }

                // 3. Draw Cells Text & Selection
                for (int c = 0; c < row.cells.size(); ++c) {
                    const auto &cell = row.cells[c];
                    if (!cell.layout) continue;

                    qreal cellContentH = cell.layout->boundingRect().height();
                    qreal textY = cell.rect.top() + (cell.rect.height() - cellContentH) / 2.0;

                    // Draw selection in cell if any
                    if (!selections.isEmpty() && cell.sourceRange.isValid()) {
                        for (const auto &sel : selections) {
                            if (!sel.hasSelection()) continue;
                            int selS = qMax(sel.selectionStart(), cell.sourceRange.start);
                            int selE = qMin(sel.selectionEnd(), cell.sourceRange.end);
                            if (selS < selE) {
                                int relS = selS - cell.sourceRange.start;
                                int relE = selE - cell.sourceRange.start;
                                painter->setPen(Qt::NoPen);
                                painter->setBrush(palette.selectionHighlight);
                                for (int l = 0; l < cell.layout->lineCount(); ++l) {
                                    QTextLine line = cell.layout->lineAt(l);
                                    int lStart = line.textStart();
                                    int lEnd = lStart + line.textLength();
                                    int s = qMax(relS, lStart);
                                    int e = qMin(relE, lEnd);
                                    if (s < e) {
                                        qreal x1 = cell.rect.left() + 10.0 + line.cursorToX(s);
                                        qreal x2 = cell.rect.left() + 10.0 + line.cursorToX(e);
                                        QRectF selR(qMin(x1, x2), textY + line.y(), qAbs(x2 - x1), line.height());
                                        painter->drawRect(selR);
                                    }
                                }
                            }
                        }
                    }

                    // Draw cell text
                    painter->setPen(palette.primaryText);
                    cell.layout->draw(painter, QPointF(cell.rect.left() + 10.0, textY));
                }
            }

            // 4. Vertical column divider lines
            qreal curColX = tableRect.left();
            painter->setPen(QPen(palette.tableBorder, 0.8));
            for (int c = 0; c < bData.tableLayout.colWidths.size() - 1; ++c) {
                curColX += bData.tableLayout.colWidths[c];
                painter->drawLine(QPointF(curColX, tableRect.top()),
                                 QPointF(curColX, tableRect.bottom()));
            }

            painter->restore();
            continue;
        }

        // Draw block specific decorations
        if (vBlock.isCodeBlock || vBlock.isMathBlock) {
            // Sleek rounded background with border
            painter->setBrush(palette.codeBlockBackground);
            painter->setPen(QPen(palette.codeBlockBorder, 1.0));
            painter->drawRoundedRect(bData.rect.adjusted(-4, -2, 4, 2), 6, 6);
        } else if (vBlock.isQuote) {
            // Soft rounded quote background
            painter->setPen(Qt::NoPen);
            painter->setBrush(palette.quoteBackground);
            painter->drawRoundedRect(bData.rect.adjusted(-2, -2, 2, 2), 6.0, 6.0);

            // Left quote accent bar
            painter->setBrush(palette.quoteLine);
            painter->drawRoundedRect(QRectF(bData.rect.left() + 4, bData.rect.top() + 2, 3.5, bData.rect.height() - 4), 1.75, 1.75);
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
                    painter->setBrush(palette.selectionHighlight);

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

        // Live preview container below code when active
        if ((bData.isMathBlock || bData.isMermaidBlock) && bData.isInteractiveActive && !bData.embeddedPreviewRect.isEmpty()) {
            Render::RenderKind kind = bData.isMathBlock ? Render::RenderKind::Math : Render::RenderKind::Mermaid;
            auto renderer = Render::AsyncRenderService::instance()->getRenderer(bData.embeddedSource, kind, false);

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);

            QRectF previewCard = bData.embeddedPreviewRect.adjusted(2, 2, -2, -2);
            painter->setPen(QPen(palette.cardBorder, 1.0));
            painter->setBrush(palette.cardBackground);
            painter->drawRoundedRect(previewCard, 8.0, 8.0);

            // Live preview badge
            QRectF badgeRect(previewCard.left() + 10, previewCard.top() + 6, 80, 18);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(16, 185, 129, 30));
            painter->drawRoundedRect(badgeRect, 4.0, 4.0);

            painter->setPen(QColor(5, 150, 105));
            QFont badgeFont = painter->font();
            badgeFont.setPointSize(8);
            badgeFont.setBold(true);
            painter->setFont(badgeFont);
            painter->drawText(badgeRect, Qt::AlignCenter, QStringLiteral("Live Preview"));

            QRectF contentRect = previewCard.adjusted(12, 26, -12, -8);
            if (renderer && renderer->isValid()) {
                renderer->render(painter, contentRect);
            }
            painter->restore();
        }
    }
}

} // namespace MarkdownTool::Editor
