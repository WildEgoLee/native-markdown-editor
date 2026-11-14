#include "DisplayProjection.h"

#include <QFont>
#include <QColor>
#include <algorithm>

namespace MarkdownTool::Mapping {

int VisualBlock::sourceToVisual(int sourceOffset) const {
    if (segments.isEmpty()) {
        return 0;
    }

    if (sourceOffset <= sourceRange.start) {
        return 0;
    }
    if (sourceOffset >= sourceRange.end) {
        return visualText.length();
    }

    for (const auto &seg : segments) {
        if (seg.type == SegmentType::HiddenMarker) {
            if (seg.sourceRange.contains(sourceOffset)) {
                return seg.visualStart;
            }
        } else {
            if (seg.sourceRange.contains(sourceOffset)) {
                int delta = sourceOffset - seg.sourceRange.start;
                return qBound(seg.visualStart, seg.visualStart + delta, seg.visualStart + seg.visualLength);
            }
        }
    }

    return visualText.length();
}

int VisualBlock::visualToSource(int visualOffset, bool preferTrailing) const {
    if (segments.isEmpty()) {
        return sourceRange.start;
    }

    if (visualOffset <= 0) {
        return sourceRange.start;
    }
    if (visualOffset >= visualText.length()) {
        return sourceRange.end;
    }

    for (const auto &seg : segments) {
        if (seg.type == SegmentType::HiddenMarker) {
            continue;
        }

        int segVisualEnd = seg.visualStart + seg.visualLength;
        if (visualOffset >= seg.visualStart && visualOffset <= segVisualEnd) {
            if (visualOffset == segVisualEnd && !preferTrailing) {
                // If on boundary, may check next segment
                continue;
            }
            int delta = visualOffset - seg.visualStart;
            return qBound(seg.sourceRange.start, seg.sourceRange.start + delta, seg.sourceRange.end);
        }
    }

    return sourceRange.end;
}

int DisplayProjection::normalizeSourceCursor(int sourceOffset, bool forward) const {
    int blockIdx = findBlockIndexBySourceOffset(sourceOffset);
    if (blockIdx < 0 || blockIdx >= m_blocks.size()) {
        return sourceOffset;
    }

    const auto &block = m_blocks[blockIdx];
    for (const auto &seg : block.segments) {
        if (seg.type == SegmentType::HiddenMarker && seg.sourceRange.isValid()) {
            if (seg.sourceRange.contains(sourceOffset)) {
                return forward ? seg.sourceRange.end : seg.sourceRange.start;
            }
        }
    }
    return sourceOffset;
}

int DisplayProjection::stepCursor(int currentSourceOffset, bool forward, const Core::TextBuffer &buffer) const {
    const QString &text = buffer.text();
    if (forward) {
        if (currentSourceOffset >= text.length()) {
            return text.length();
        }
        int next = Core::PositionTranslator::nextGraphemeOffset(text, currentSourceOffset);

        // Check if next hit a hidden marker
        int blockIdx = findBlockIndexBySourceOffset(next);
        if (blockIdx >= 0 && blockIdx < m_blocks.size()) {
            const auto &block = m_blocks[blockIdx];
            for (const auto &seg : block.segments) {
                if (seg.type == SegmentType::HiddenMarker && seg.sourceRange.isValid()) {
                    if (seg.sourceRange.contains(next) && next < seg.sourceRange.end) {
                        return seg.sourceRange.end;
                    }
                }
            }
        }
        return next;
    } else {
        if (currentSourceOffset <= 0) {
            return 0;
        }
        int prev = Core::PositionTranslator::prevGraphemeOffset(text, currentSourceOffset);

        // Check if prev hit a hidden marker
        int blockIdx = findBlockIndexBySourceOffset(prev);
        if (blockIdx >= 0 && blockIdx < m_blocks.size()) {
            const auto &block = m_blocks[blockIdx];
            for (const auto &seg : block.segments) {
                if (seg.type == SegmentType::HiddenMarker && seg.sourceRange.isValid()) {
                    if (seg.sourceRange.contains(prev) && prev > seg.sourceRange.start) {
                        return seg.sourceRange.start;
                    }
                }
            }
        }
        return prev;
    }
}

int DisplayProjection::findBlockIndexBySourceOffset(int sourceOffset) const {
    if (m_blocks.isEmpty()) {
        return -1;
    }

    if (sourceOffset <= m_blocks.first().sourceRange.start) {
        return 0;
    }
    if (sourceOffset >= m_blocks.last().sourceRange.end) {
        return m_blocks.size() - 1;
    }

    // Binary search for block containing sourceOffset
    auto it = std::lower_bound(m_blocks.begin(), m_blocks.end(), sourceOffset,
        [](const VisualBlock &b, int offset) {
            return b.sourceRange.end <= offset;
        });
    if (it != m_blocks.end()) {
        return static_cast<int>(std::distance(m_blocks.begin(), it));
    }
    return m_blocks.size() - 1;
}

static void applyInlineStyles(const Parser::SemanticNodePtr &inlineNode,
                             const Core::TextBuffer &buffer,
                             int cursorPos,
                             bool isSourceMode,
                             VisualBlock &block,
                             QTextCharFormat baseFormat)
{
    if (!inlineNode) {
        return;
    }

    using namespace Parser;
    const QString &fullText = buffer.text();

    switch (inlineNode->type) {
    case NodeType::Text: {
        VisualSegment seg;
        seg.sourceRange = inlineNode->fullRange;
        seg.text = fullText.mid(seg.sourceRange.start, seg.sourceRange.length());
        seg.visualStart = block.visualText.length();
        seg.visualLength = seg.text.length();
        seg.type = SegmentType::NormalText;
        seg.format = baseFormat;

        block.visualText.append(seg.text);
        block.segments.append(seg);
        break;
    }

    case NodeType::Strong:
    case NodeType::Emphasis:
    case NodeType::Strikethrough:
    case NodeType::CodeSpan: {
        bool isActive = isSourceMode || inlineNode->fullRange.contains(cursorPos);

        QTextCharFormat styledFormat = baseFormat;
        if (inlineNode->type == NodeType::Strong) {
            styledFormat.setFontWeight(QFont::Bold);
        } else if (inlineNode->type == NodeType::Emphasis) {
            styledFormat.setFontItalic(true);
        } else if (inlineNode->type == NodeType::Strikethrough) {
            styledFormat.setFontStrikeOut(true);
        } else if (inlineNode->type == NodeType::CodeSpan) {
            styledFormat.setFontFamilies({QStringLiteral("Consolas"), QStringLiteral("Courier New"), QStringLiteral("monospace")});
            styledFormat.setBackground(QColor(235, 237, 240));
            styledFormat.setForeground(QColor(199, 37, 78));
        }

        // Prefix marker
        if (inlineNode->prefixRange.isValid() && !inlineNode->prefixRange.isEmpty()) {
            VisualSegment prefixSeg;
            prefixSeg.sourceRange = inlineNode->prefixRange;
            if (isActive) {
                prefixSeg.text = fullText.mid(prefixSeg.sourceRange.start, prefixSeg.sourceRange.length());
                prefixSeg.visualStart = block.visualText.length();
                prefixSeg.visualLength = prefixSeg.text.length();
                prefixSeg.type = SegmentType::SyntaxMarker;
                prefixSeg.format = baseFormat;
                prefixSeg.format.setForeground(QColor(160, 160, 160));
                block.visualText.append(prefixSeg.text);
            } else {
                prefixSeg.visualStart = block.visualText.length();
                prefixSeg.visualLength = 0;
                prefixSeg.type = SegmentType::HiddenMarker;
            }
            block.segments.append(prefixSeg);
        }

        // Inner content
        if (!inlineNode->children.isEmpty()) {
            for (const auto &child : inlineNode->children) {
                applyInlineStyles(child, buffer, cursorPos, isSourceMode, block, styledFormat);
            }
        } else if (inlineNode->contentRange.isValid()) {
            VisualSegment contentSeg;
            contentSeg.sourceRange = inlineNode->contentRange;
            contentSeg.text = fullText.mid(contentSeg.sourceRange.start, contentSeg.sourceRange.length());
            contentSeg.visualStart = block.visualText.length();
            contentSeg.visualLength = contentSeg.text.length();
            contentSeg.type = SegmentType::NormalText;
            contentSeg.format = styledFormat;

            block.visualText.append(contentSeg.text);
            block.segments.append(contentSeg);
        }

        // Suffix marker
        if (inlineNode->suffixRange.isValid() && !inlineNode->suffixRange.isEmpty()) {
            VisualSegment suffixSeg;
            suffixSeg.sourceRange = inlineNode->suffixRange;
            if (isActive) {
                suffixSeg.text = fullText.mid(suffixSeg.sourceRange.start, suffixSeg.sourceRange.length());
                suffixSeg.visualStart = block.visualText.length();
                suffixSeg.visualLength = suffixSeg.text.length();
                suffixSeg.type = SegmentType::SyntaxMarker;
                suffixSeg.format = baseFormat;
                suffixSeg.format.setForeground(QColor(160, 160, 160));
                block.visualText.append(suffixSeg.text);
            } else {
                suffixSeg.visualStart = block.visualText.length();
                suffixSeg.visualLength = 0;
                suffixSeg.type = SegmentType::HiddenMarker;
            }
            block.segments.append(suffixSeg);
        }
        break;
    }

    case NodeType::SoftBreak:
    case NodeType::LineBreak: {
        VisualSegment seg;
        seg.sourceRange = inlineNode->fullRange;
        seg.text = (inlineNode->type == NodeType::LineBreak) ? QStringLiteral("\n") : QStringLiteral(" ");
        seg.visualStart = block.visualText.length();
        seg.visualLength = seg.text.length();
        seg.type = SegmentType::NormalText;
        seg.format = baseFormat;

        block.visualText.append(seg.text);
        block.segments.append(seg);
        break;
    }

    default:
        for (const auto &child : inlineNode->children) {
            applyInlineStyles(child, buffer, cursorPos, isSourceMode, block, baseFormat);
        }
        break;
    }
}

bool DisplayProjection::buildProjection(const Parser::SemanticNodePtr &rootNode,
                                        const Core::TextBuffer &buffer,
                                        int cursorPos,
                                        bool isSourceMode,
                                        int editOffset,
                                        int lengthRemoved,
                                        int lengthAdded)
{
    // Fast path: In-Place Dirty Block Projection
    // When a single localized text edit occurs within a block and does not alter document-wide structure,
    // update only the affected visual block and shift suffix source ranges in O(affected) time.
    if (editOffset >= 0 && !m_blocks.isEmpty() && !isSourceMode && rootNode && !buffer.isEmpty() && m_lastCursorPos >= 0 &&
        rootNode->children.size() == m_lastAstChildrenCount)
    {
        int dirtyIdx = findBlockIndexBySourceOffset(editOffset);
        if (dirtyIdx > 0 && (dirtyIdx >= m_blocks.size() || editOffset < m_blocks[dirtyIdx].sourceRange.start)) {
            if (editOffset <= m_blocks[dirtyIdx - 1].sourceRange.end) {
                dirtyIdx = dirtyIdx - 1;
            }
        }

        int prevCursorBlockIdx = findBlockIndexBySourceOffset(m_lastCursorPos);
        int newCursorBlockIdx = findBlockIndexBySourceOffset(cursorPos);

        // Crucial safety check: Fast path is only valid if both previous cursor and current cursor
        // belong to the dirty block! If cursor moved into or out of another block, syntax markers
        // in that other block may need to show/hide, requiring full projection.
        if (dirtyIdx >= 0 && dirtyIdx < m_blocks.size() &&
            prevCursorBlockIdx == dirtyIdx && newCursorBlockIdx == dirtyIdx)
        {
            const auto &curBlock = m_blocks[dirtyIdx];
            if (editOffset >= curBlock.sourceRange.start && (editOffset + lengthRemoved) <= curBlock.sourceRange.end) {
                // Find matching AST node in rootNode->children using binary search
                auto it = std::lower_bound(rootNode->children.begin(), rootNode->children.end(), editOffset,
                    [](const Parser::SemanticNodePtr &node, int offset) {
                        return node->fullRange.end <= offset;
                    });
                if (it > rootNode->children.begin() && (it == rootNode->children.end() || (*it)->fullRange.start > editOffset)) {
                    if ((*(it - 1))->fullRange.end >= editOffset) {
                        --it;
                    }
                }

                if (it != rootNode->children.end()) {
                    const auto &matchingChild = *it;
                    if (matchingChild->fullRange.start == curBlock.sourceRange.start &&
                        matchingChild->type == curBlock.blockType)
                    {
                        QVector<VisualBlock> tempBlocks;
                        processBlockNode(matchingChild, buffer, cursorPos, isSourceMode, tempBlocks);
                        if (tempBlocks.size() == 1) {
                            auto &oldBlock = m_blocks[dirtyIdx];
                            auto &newBlock = tempBlocks[0];

                            bool blockChanged = (oldBlock.visualText != newBlock.visualText ||
                                                 oldBlock.blockType != newBlock.blockType ||
                                                 oldBlock.headingLevel != newBlock.headingLevel ||
                                                 oldBlock.indentLevel != newBlock.indentLevel ||
                                                 oldBlock.isQuote != newBlock.isQuote ||
                                                 oldBlock.isCodeBlock != newBlock.isCodeBlock ||
                                                 oldBlock.isTaskItem != newBlock.isTaskItem ||
                                                 oldBlock.isTaskChecked != newBlock.isTaskChecked ||
                                                 oldBlock.marginTop != newBlock.marginTop ||
                                                 oldBlock.marginBottom != newBlock.marginBottom ||
                                                 oldBlock.segments.size() != newBlock.segments.size());
                            if (!blockChanged) {
                                for (int s = 0; s < oldBlock.segments.size(); ++s) {
                                    const auto &sOld = oldBlock.segments[s];
                                    const auto &sNew = newBlock.segments[s];
                                    if (sOld.type != sNew.type ||
                                        sOld.visualStart != sNew.visualStart ||
                                        sOld.visualLength != sNew.visualLength ||
                                        sOld.format != sNew.format)
                                    {
                                        blockChanged = true;
                                        break;
                                    }
                                }
                            }

                            m_blocks[dirtyIdx] = std::move(newBlock);

                            int delta = lengthAdded - lengthRemoved;
                            if (delta != 0) {
                                for (int i = dirtyIdx + 1; i < m_blocks.size(); ++i) {
                                    auto &b = m_blocks[i];
                                    b.sourceRange.start += delta;
                                    b.sourceRange.end += delta;
                                    for (auto &seg : b.segments) {
                                        if (seg.sourceRange.isValid()) {
                                            seg.sourceRange.start += delta;
                                            seg.sourceRange.end += delta;
                                        }
                                    }
                                    if (b.isTaskItem && b.checkboxSourceRange.isValid() && !b.checkboxSourceRange.isEmpty()) {
                                        b.checkboxSourceRange.start += delta;
                                        b.checkboxSourceRange.end += delta;
                                    }
                                }
                            }

                            m_lastCursorPos = cursorPos;
                            m_incrementalHits++;
                            return blockChanged || delta != 0;
                        }
                    }
                }
            }
        }
    }

    m_fullFallbacks++;
    QVector<VisualBlock> prevBlocks = std::move(m_blocks);
    m_blocks.clear();

    auto checkChanged = [&]() -> bool {
        m_lastCursorPos = cursorPos;
        m_lastAstChildrenCount = rootNode ? rootNode->children.size() : 0;
        if (m_blocks.size() != prevBlocks.size()) {
            return true;
        }
        for (int i = 0; i < m_blocks.size(); ++i) {
            const auto &b1 = m_blocks[i];
            const auto &b2 = prevBlocks[i];
            if (b1.visualText != b2.visualText ||
                b1.blockType != b2.blockType ||
                b1.headingLevel != b2.headingLevel ||
                b1.indentLevel != b2.indentLevel ||
                b1.isQuote != b2.isQuote ||
                b1.isCodeBlock != b2.isCodeBlock ||
                b1.isTaskItem != b2.isTaskItem ||
                b1.isTaskChecked != b2.isTaskChecked ||
                b1.marginTop != b2.marginTop ||
                b1.marginBottom != b2.marginBottom ||
                b1.segments.size() != b2.segments.size())
            {
                return true;
            }
            for (int s = 0; s < b1.segments.size(); ++s) {
                const auto &s1 = b1.segments[s];
                const auto &s2 = b2.segments[s];
                if (s1.type != s2.type ||
                    s1.visualStart != s2.visualStart ||
                    s1.visualLength != s2.visualLength ||
                    s1.format != s2.format)
                {
                    return true;
                }
            }
        }
        return false;
    };

    if (buffer.isEmpty()) {
        VisualBlock emptyBlock;
        emptyBlock.sourceRange = Core::SourceRange(0, 0);
        m_blocks.append(emptyBlock);
        return checkChanged();
    }

    if (isSourceMode) {
        // Direct 1:1 raw source projection from TextBuffer (P0-04)
        // No AST loss: every single character, whitespace, fence and newline is exactly preserved!
        QTextCharFormat monoFormat;
        monoFormat.setFontFamilies({QStringLiteral("Consolas"), QStringLiteral("Courier New"), QStringLiteral("monospace")});
        monoFormat.setFontPointSize(12);
        monoFormat.setForeground(QColor(36, 41, 47));

        const auto &translator = buffer.positionTranslator();
        int totalLines = translator.lineCount();
        const QString &fullText = buffer.text();

        for (int l = 0; l < totalLines; ++l) {
            int lineStart = translator.lineStartOffset(l);
            int lineEndWithNl = translator.lineEndOffset(l, fullText, true);
            int lineEndNoNl = translator.lineEndOffset(l, fullText, false);

            VisualBlock lineBlock;
            lineBlock.blockType = Parser::NodeType::Paragraph;
            lineBlock.sourceRange = Core::SourceRange(lineStart, lineEndWithNl);
            lineBlock.visualText = fullText.mid(lineStart, lineEndNoNl - lineStart);
            lineBlock.marginTop = 1;
            lineBlock.marginBottom = 1;

            VisualSegment seg;
            seg.sourceRange = Core::SourceRange(lineStart, lineEndNoNl);
            seg.visualStart = 0;
            seg.visualLength = lineBlock.visualText.length();
            seg.type = SegmentType::NormalText;
            seg.format = monoFormat;

            lineBlock.segments.append(seg);
            m_blocks.append(lineBlock);
        }

        if (m_blocks.isEmpty()) {
            VisualBlock emptyBlock;
            emptyBlock.sourceRange = Core::SourceRange(0, 0);
            m_blocks.append(emptyBlock);
        }
        return checkChanged();
    }

    if (!rootNode) {
        VisualBlock emptyBlock;
        emptyBlock.sourceRange = Core::SourceRange(0, buffer.length());
        emptyBlock.visualText = buffer.text();
        VisualSegment seg;
        seg.sourceRange = emptyBlock.sourceRange;
        seg.text = emptyBlock.visualText;
        seg.visualStart = 0;
        seg.visualLength = seg.text.length();
        seg.type = SegmentType::NormalText;
        emptyBlock.segments.append(seg);
        m_blocks.append(emptyBlock);
        return checkChanged();
    }

    // Traverse root children
    for (const auto &child : rootNode->children) {
        processBlockNode(child, buffer, cursorPos, isSourceMode, m_blocks);
    }

    if (m_blocks.isEmpty()) {
        VisualBlock emptyBlock;
        emptyBlock.sourceRange = Core::SourceRange(0, buffer.length());
        emptyBlock.visualText = buffer.text();
        VisualSegment seg;
        seg.sourceRange = emptyBlock.sourceRange;
        seg.text = emptyBlock.visualText;
        seg.visualStart = 0;
        seg.visualLength = seg.text.length();
        seg.type = SegmentType::NormalText;
        emptyBlock.segments.append(seg);
        m_blocks.append(emptyBlock);
    }
    return checkChanged();
}

void DisplayProjection::processBlockNode(const Parser::SemanticNodePtr &node,
                                        const Core::TextBuffer &buffer,
                                        int cursorPos,
                                        bool isSourceMode,
                                        QVector<VisualBlock> &outBlocks)
{
    if (!node) {
        return;
    }

    using namespace Parser;
    const QString &fullText = buffer.text();

    if (node->type == NodeType::List) {
        for (const auto &item : node->children) {
            processBlockNode(item, buffer, cursorPos, isSourceMode, outBlocks);
        }
        return;
    }

    VisualBlock block;
    block.blockType = node->type;
    block.sourceRange = node->fullRange;

    QTextCharFormat baseFormat;
    baseFormat.setFontFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Microsoft YaHei"), QStringLiteral("sans-serif")});
    baseFormat.setFontPointSize(14);
    baseFormat.setForeground(QColor(40, 44, 52));

    if (node->type == NodeType::Heading) {
        block.headingLevel = node->headingLevel;
        baseFormat.setFontWeight(QFont::Bold);

        qreal pointSize = 14;
        switch (node->headingLevel) {
        case 1: pointSize = 24; block.marginTop = 16; block.marginBottom = 8; break;
        case 2: pointSize = 20; block.marginTop = 14; block.marginBottom = 6; break;
        case 3: pointSize = 17; block.marginTop = 12; block.marginBottom = 4; break;
        case 4: pointSize = 15; block.marginTop = 10; block.marginBottom = 4; break;
        default: pointSize = 14; block.marginTop = 8; block.marginBottom = 4; break;
        }
        baseFormat.setFontPointSize(pointSize);

        bool isActive = isSourceMode || node->fullRange.contains(cursorPos);
        if (node->prefixRange.isValid() && !node->prefixRange.isEmpty()) {
            VisualSegment prefixSeg;
            prefixSeg.sourceRange = node->prefixRange;
            if (isActive) {
                prefixSeg.text = fullText.mid(prefixSeg.sourceRange.start, prefixSeg.sourceRange.length());
                prefixSeg.visualStart = block.visualText.length();
                prefixSeg.visualLength = prefixSeg.text.length();
                prefixSeg.type = SegmentType::SyntaxMarker;
                prefixSeg.format = baseFormat;
                prefixSeg.format.setForeground(QColor(160, 160, 160));
                block.visualText.append(prefixSeg.text);
            } else {
                prefixSeg.visualStart = block.visualText.length();
                prefixSeg.visualLength = 0;
                prefixSeg.type = SegmentType::HiddenMarker;
            }
            block.segments.append(prefixSeg);
        }

        for (const auto &child : node->children) {
            applyInlineStyles(child, buffer, cursorPos, isSourceMode, block, baseFormat);
        }
    } else if (node->type == NodeType::ListItem) {
        block.isTaskItem = node->isTaskItem;
        block.isTaskChecked = node->isTaskChecked;
        block.checkboxSourceRange = node->taskCheckboxRange;
        block.indentLevel = 1;
        block.marginTop = 3;
        block.marginBottom = 3;

        // Visual task checkbox or bullet
        if (node->isTaskItem && !isSourceMode) {
            VisualSegment checkSeg;
            checkSeg.sourceRange = node->taskCheckboxRange;
            checkSeg.text = node->isTaskChecked ? QStringLiteral("☑ ") : QStringLiteral("☐ ");
            checkSeg.visualStart = block.visualText.length();
            checkSeg.visualLength = checkSeg.text.length();
            checkSeg.type = SegmentType::TaskCheckBox;
            checkSeg.isInteractive = true;
            checkSeg.format = baseFormat;
            checkSeg.format.setForeground(node->isTaskChecked ? QColor(50, 130, 240) : QColor(100, 100, 100));

            block.visualText.append(checkSeg.text);
            block.segments.append(checkSeg);
        }

        for (const auto &child : node->children) {
            if (child->isBlock()) {
                for (const auto &inlineChild : child->children) {
                    applyInlineStyles(inlineChild, buffer, cursorPos, isSourceMode, block, baseFormat);
                }
            } else {
                applyInlineStyles(child, buffer, cursorPos, isSourceMode, block, baseFormat);
            }
        }
    } else if (node->type == NodeType::BlockQuote) {
        block.isQuote = true;
        block.marginTop = 6;
        block.marginBottom = 6;
        baseFormat.setForeground(QColor(100, 105, 115));
        baseFormat.setFontItalic(true);

        bool isActive = isSourceMode || node->fullRange.contains(cursorPos);
        if (node->prefixRange.isValid() && !node->prefixRange.isEmpty()) {
            VisualSegment prefixSeg;
            prefixSeg.sourceRange = node->prefixRange;
            if (isActive) {
                prefixSeg.text = fullText.mid(prefixSeg.sourceRange.start, prefixSeg.sourceRange.length());
                prefixSeg.visualStart = block.visualText.length();
                prefixSeg.visualLength = prefixSeg.text.length();
                prefixSeg.type = SegmentType::SyntaxMarker;
                prefixSeg.format = baseFormat;
                prefixSeg.format.setForeground(QColor(160, 160, 160));
                block.visualText.append(prefixSeg.text);
            } else {
                prefixSeg.visualStart = block.visualText.length();
                prefixSeg.visualLength = 0;
                prefixSeg.type = SegmentType::HiddenMarker;
            }
            block.segments.append(prefixSeg);
        }

        for (const auto &child : node->children) {
            if (child->isBlock()) {
                for (const auto &inlineChild : child->children) {
                    applyInlineStyles(inlineChild, buffer, cursorPos, isSourceMode, block, baseFormat);
                }
            } else {
                applyInlineStyles(child, buffer, cursorPos, isSourceMode, block, baseFormat);
            }
        }
    } else if (node->type == NodeType::CodeBlock) {
        block.isCodeBlock = true;
        block.codeLanguage = node->codeLanguage;
        block.marginTop = 8;
        block.marginBottom = 8;

        baseFormat.setFontFamilies({QStringLiteral("Consolas"), QStringLiteral("Fira Code"), QStringLiteral("Courier New"), QStringLiteral("monospace")});
        baseFormat.setFontPointSize(12);
        baseFormat.setForeground(QColor(220, 225, 230));

        VisualSegment codeSeg;
        codeSeg.sourceRange = node->fullRange;
        codeSeg.text = node->textContent.isEmpty() ? fullText.mid(node->fullRange.start, node->fullRange.length()) : node->textContent;
        codeSeg.visualStart = 0;
        codeSeg.visualLength = codeSeg.text.length();
        codeSeg.type = SegmentType::CodeBlockContent;
        codeSeg.format = baseFormat;

        block.visualText = codeSeg.text;
        block.segments.append(codeSeg);
    } else if (node->type == NodeType::Table) {
        block.marginTop = 10;
        block.marginBottom = 10;
        bool isActive = isSourceMode || node->fullRange.contains(cursorPos);

        baseFormat.setFontFamilies({QStringLiteral("Consolas"), QStringLiteral("Segoe UI"), QStringLiteral("sans-serif")});
        baseFormat.setFontPointSize(13);

        VisualSegment tableSeg;
        tableSeg.sourceRange = node->fullRange;
        tableSeg.text = fullText.mid(node->fullRange.start, node->fullRange.length());
        tableSeg.visualStart = 0;
        tableSeg.visualLength = tableSeg.text.length();
        tableSeg.type = SegmentType::NormalText;
        tableSeg.format = baseFormat;

        if (!isActive) {
            tableSeg.format.setBackground(QColor(248, 250, 252));
        }

        block.visualText = tableSeg.text;
        block.segments.append(tableSeg);
    } else if (node->type == NodeType::ThematicBreak) {
        block.marginTop = 12;
        block.marginBottom = 12;
        VisualSegment hrSeg;
        hrSeg.sourceRange = node->fullRange;
        hrSeg.text = QStringLiteral("──────");
        hrSeg.visualStart = 0;
        hrSeg.visualLength = hrSeg.text.length();
        hrSeg.format = baseFormat;
        hrSeg.format.setForeground(QColor(210, 215, 220));

        block.visualText = hrSeg.text;
        block.segments.append(hrSeg);
    } else {
        // Normal paragraph
        block.marginTop = 4;
        block.marginBottom = 4;
        for (const auto &child : node->children) {
            applyInlineStyles(child, buffer, cursorPos, isSourceMode, block, baseFormat);
        }
    }

    // If block text is empty (e.g. empty line or blank paragraph)
    if (block.visualText.isEmpty()) {
        VisualSegment emptySeg;
        emptySeg.sourceRange = block.sourceRange;
        emptySeg.visualStart = 0;
        emptySeg.visualLength = 0;
        emptySeg.type = SegmentType::NormalText;
        emptySeg.format = baseFormat;
        block.segments.append(emptySeg);
    }

    outBlocks.append(std::move(block));
}

} // namespace MarkdownTool::Mapping
