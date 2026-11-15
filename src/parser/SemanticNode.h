#pragma once

#include "core/Position.h"

#include <QString>
#include <QVector>
#include <memory>

namespace MarkdownTool::Parser {

enum class NodeType {
    Document,
    Heading,
    Paragraph,
    BlockQuote,
    List,
    ListItem,
    TaskListItem,
    CodeBlock,
    ThematicBreak,
    Table,
    TableRow,
    TableCell,
    Text,
    Emphasis,
    Strong,
    Strikethrough,
    CodeSpan,
    Link,
    Image,
    SoftBreak,
    LineBreak,
    MathBlock,
    CustomBlock,
};

struct SemanticNode {
    NodeType type = NodeType::Text;
    Core::SourceRange fullRange;
    Core::SourceRange contentRange;
    Core::SourceRange prefixRange;
    Core::SourceRange suffixRange;

    // Attributes
    int headingLevel = 0;       // 1 - 6
    bool isTaskItem = false;
    bool isTaskChecked = false;
    Core::SourceRange taskCheckboxRange;

    QString codeLanguage;
    QString linkUrl;
    QString linkTitle;

    // Table attributes
    int tableColumnCount = 0;
    QVector<uint8_t> tableAlignments;
    bool isTableHeader = false;

    QString textContent;

    QVector<std::shared_ptr<SemanticNode>> children;

    bool isBlock() const {
        return type == NodeType::Heading ||
               type == NodeType::Paragraph ||
               type == NodeType::BlockQuote ||
               type == NodeType::List ||
               type == NodeType::ListItem ||
               type == NodeType::TaskListItem ||
               type == NodeType::CodeBlock ||
               type == NodeType::ThematicBreak ||
               type == NodeType::Table ||
               type == NodeType::TableRow ||
               type == NodeType::MathBlock;
    }
};

using SemanticNodePtr = std::shared_ptr<SemanticNode>;

} // namespace MarkdownTool::Parser
