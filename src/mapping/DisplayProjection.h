#pragma once

#include "core/Position.h"
#include "core/TextBuffer.h"
#include "parser/SemanticNode.h"

#include <QString>
#include <QVector>
#include <QTextCharFormat>

namespace MarkdownTool::Mapping {

enum class SegmentType {
    NormalText,
    SyntaxMarker,
    HiddenMarker,
    TaskCheckBox,
    CodeBlockContent,
};

struct VisualSegment {
    Core::SourceRange sourceRange;
    int visualStart = 0;
    int visualLength = 0;
    SegmentType type = SegmentType::NormalText;
    QTextCharFormat format;
    QString text;
    bool isInteractive = false;
};

struct VisualBlock {
    Parser::NodeType blockType = Parser::NodeType::Paragraph;
    int headingLevel = 0;
    Core::SourceRange sourceRange;
    QString visualText;
    QVector<VisualSegment> segments;

    // Block styling attributes
    int marginTop = 4;
    int marginBottom = 4;
    int indentLevel = 0;
    bool isQuote = false;
    bool isCodeBlock = false;
    QString codeLanguage;
    bool isTaskItem = false;
    bool isTaskChecked = false;
    Core::SourceRange checkboxSourceRange;

    int sourceToVisual(int sourceOffset) const;
    int visualToSource(int visualOffset, bool preferTrailing = false) const;
};

class DisplayProjection {
public:
    DisplayProjection() = default;

    bool buildProjection(const Parser::SemanticNodePtr &rootNode,
                         const Core::TextBuffer &buffer,
                         int cursorPos,
                         bool isSourceMode,
                         int editOffset = -1,
                         int lengthRemoved = 0,
                         int lengthAdded = 0);

    const QVector<VisualBlock> &blocks() const { return m_blocks; }
    int blockCount() const { return m_blocks.size(); }

    int findBlockIndexBySourceOffset(int sourceOffset) const;
    int findBlockIndexByVisualLine(int line) const;

    int normalizeSourceCursor(int sourceOffset, bool forward) const;
    int stepCursor(int currentSourceOffset, bool forward, const Core::TextBuffer &buffer) const;

private:
    void processBlockNode(const Parser::SemanticNodePtr &node,
                          const Core::TextBuffer &buffer,
                          int cursorPos,
                          bool isSourceMode,
                          QVector<VisualBlock> &outBlocks);

    QVector<VisualBlock> m_blocks;
    int m_lastCursorPos = -1;
};

} // namespace MarkdownTool::Mapping
