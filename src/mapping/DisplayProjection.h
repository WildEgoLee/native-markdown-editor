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
    EmbeddedWidget,
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

inline bool operator==(const VisualSegment &a, const VisualSegment &b) {
    return a.sourceRange == b.sourceRange &&
           a.visualStart == b.visualStart &&
           a.visualLength == b.visualLength &&
           a.type == b.type &&
           a.format == b.format &&
           a.text == b.text &&
           a.isInteractive == b.isInteractive;
}
inline bool operator!=(const VisualSegment &a, const VisualSegment &b) {
    return !(a == b);
}

struct TableCellVisual {
    QString text;
    Core::SourceRange sourceRange;
    int visualStart = 0;
    int visualLength = 0;
    Qt::AlignmentFlag alignment = Qt::AlignLeft;
    bool isHeader = false;
    QVector<VisualSegment> segments;

    int sourceToVisual(int sourceOffset) const;
    int visualToSource(int visualOffset, bool preferTrailing = false) const;
};

inline bool operator==(const TableCellVisual &a, const TableCellVisual &b) {
    return a.text == b.text &&
           a.sourceRange == b.sourceRange &&
           a.visualStart == b.visualStart &&
           a.visualLength == b.visualLength &&
           a.alignment == b.alignment &&
           a.isHeader == b.isHeader &&
           a.segments == b.segments;
}
inline bool operator!=(const TableCellVisual &a, const TableCellVisual &b) {
    return !(a == b);
}

struct TableRowVisual {
    QVector<TableCellVisual> cells;
    bool isHeader = false;
    Core::SourceRange sourceRange;
};

inline bool operator==(const TableRowVisual &a, const TableRowVisual &b) {
    return a.isHeader == b.isHeader &&
           a.sourceRange == b.sourceRange &&
           a.cells == b.cells;
}
inline bool operator!=(const TableRowVisual &a, const TableRowVisual &b) {
    return !(a == b);
}

struct TableVisualData {
    QVector<TableRowVisual> rows;
    int colCount = 0;
    QVector<Qt::AlignmentFlag> alignments;
    Core::SourceRange sourceRange;
};

inline bool operator==(const TableVisualData &a, const TableVisualData &b) {
    return a.colCount == b.colCount &&
           a.sourceRange == b.sourceRange &&
           a.alignments == b.alignments &&
           a.rows == b.rows;
}
inline bool operator!=(const TableVisualData &a, const TableVisualData &b) {
    return !(a == b);
}

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

    // Math & Diagram Embedded Component attributes
    bool isMathBlock = false;
    bool isMermaidBlock = false;
    QString embeddedSource;
    bool isInteractiveActive = false;
    qreal embeddedHeight = 0.0;

    // Table component attributes
    bool isTableBlock = false;
    TableVisualData tableData;

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

    quint64 incrementalHits() const { return m_incrementalHits; }
    quint64 fullFallbacks() const { return m_fullFallbacks; }

private:
    void processBlockNode(const Parser::SemanticNodePtr &node,
                          const Core::TextBuffer &buffer,
                          int cursorPos,
                          bool isSourceMode,
                          QVector<VisualBlock> &outBlocks);

    QVector<VisualBlock> m_blocks;
    int m_lastCursorPos = -1;
    int m_lastAstChildrenCount = -1;
    quint64 m_incrementalHits = 0;
    quint64 m_fullFallbacks = 0;
};

} // namespace MarkdownTool::Mapping
