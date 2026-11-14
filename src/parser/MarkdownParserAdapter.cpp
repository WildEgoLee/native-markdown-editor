#include "MarkdownParserAdapter.h"

extern "C" {
#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm-core-extensions.h>
}

#include <QDebug>

namespace MarkdownTool::Parser {

MarkdownParserAdapter::MarkdownParserAdapter() {
    initExtensions();
}

MarkdownParserAdapter::~MarkdownParserAdapter() = default;

void MarkdownParserAdapter::initExtensions() {
    if (!m_extensionsInitialized) {
        cmark_gfm_core_extensions_ensure_registered();
        m_extensionsInitialized = true;
    }
}

static Core::SourceRange calculateRange(cmark_node *node, const Core::TextBuffer &buffer) {
    if (!node) {
        return Core::SourceRange();
    }

    int startLine = cmark_node_get_start_line(node);   // 1-based
    int startCol = cmark_node_get_start_column(node);   // 1-based UTF-8 byte
    int endLine = cmark_node_get_end_line(node);       // 1-based
    int endCol = cmark_node_get_end_column(node);       // 1-based UTF-8 byte

    if (startLine <= 0 || endLine <= 0) {
        return Core::SourceRange();
    }

    const auto &translator = buffer.positionTranslator();
    const QString &text = buffer.text();

    int startLineIdx = startLine - 1;
    int startLineOffset = translator.lineStartOffset(startLineIdx);
    int startLineEnd = translator.lineEndOffset(startLineIdx, text, true);
    QString sLineText = text.mid(startLineOffset, startLineEnd - startLineOffset);
    int startUtf16 = startLineOffset + Core::PositionTranslator::utf8ByteToUtf16(startCol - 1, sLineText);

    int endLineIdx = endLine - 1;
    int endLineOffset = translator.lineStartOffset(endLineIdx);
    int endLineEnd = translator.lineEndOffset(endLineIdx, text, true);
    QString eLineText = text.mid(endLineOffset, endLineEnd - endLineOffset);
    int endUtf16 = endLineOffset + Core::PositionTranslator::utf8ByteToUtf16(endCol, eLineText);

    startUtf16 = qBound(0, startUtf16, text.length());
    endUtf16 = qBound(startUtf16, endUtf16, text.length());

    return Core::SourceRange(startUtf16, endUtf16);
}

static SemanticNodePtr convertNode(cmark_node *node, const Core::TextBuffer &buffer) {
    if (!node) {
        return nullptr;
    }

    auto semNode = std::make_shared<SemanticNode>();
    cmark_node_type type = cmark_node_get_type(node);
    const QString &fullText = buffer.text();

    semNode->fullRange = calculateRange(node, buffer);
    semNode->contentRange = semNode->fullRange;

    const char *nodeTypeStr = cmark_node_get_type_string(node);
    QString typeStr = QString::fromUtf8(nodeTypeStr);

    switch (type) {
    case CMARK_NODE_DOCUMENT:
        semNode->type = NodeType::Document;
        break;

    case CMARK_NODE_HEADING: {
        semNode->type = NodeType::Heading;
        semNode->headingLevel = cmark_node_get_heading_level(node);

        // Find prefix marker (e.g. "### ")
        int s = semNode->fullRange.start;
        int e = semNode->fullRange.end;
        int hashCount = 0;
        while (s + hashCount < e && fullText.at(s + hashCount) == u'#') {
            hashCount++;
        }
        int prefixEnd = s + hashCount;
        if (prefixEnd < e && fullText.at(prefixEnd) == u' ') {
            prefixEnd++;
        }
        semNode->prefixRange = Core::SourceRange(s, prefixEnd);
        semNode->contentRange = Core::SourceRange(prefixEnd, e);
        break;
    }

    case CMARK_NODE_PARAGRAPH:
        semNode->type = NodeType::Paragraph;
        break;

    case CMARK_NODE_BLOCK_QUOTE: {
        semNode->type = NodeType::BlockQuote;
        int s = semNode->fullRange.start;
        int e = semNode->fullRange.end;
        int prefixEnd = s;
        if (s < e && fullText.at(s) == u'>') {
            prefixEnd++;
            if (prefixEnd < e && fullText.at(prefixEnd) == u' ') {
                prefixEnd++;
            }
        }
        semNode->prefixRange = Core::SourceRange(s, prefixEnd);
        semNode->contentRange = Core::SourceRange(prefixEnd, e);
        break;
    }

    case CMARK_NODE_LIST:
        semNode->type = NodeType::List;
        break;

    case CMARK_NODE_ITEM: {
        semNode->type = NodeType::ListItem;

        // Check if there is a tasklist checkbox
        int s = semNode->fullRange.start;
        int e = semNode->fullRange.end;
        QString itemSnippet = fullText.mid(s, qMin(20, e - s));

        // Matches "- [ ] " or "- [x] " or "* [ ] "
        int boxPos = -1;
        if ((boxPos = itemSnippet.indexOf(QStringLiteral("[ ]"))) != -1) {
            semNode->isTaskItem = true;
            semNode->isTaskChecked = false;
            semNode->taskCheckboxRange = Core::SourceRange(s + boxPos, s + boxPos + 3);
        } else if ((boxPos = itemSnippet.indexOf(QStringLiteral("[x]"))) != -1 ||
                   (boxPos = itemSnippet.indexOf(QStringLiteral("[X]"))) != -1) {
            semNode->isTaskItem = true;
            semNode->isTaskChecked = true;
            semNode->taskCheckboxRange = Core::SourceRange(s + boxPos, s + boxPos + 3);
        }
        break;
    }

    case CMARK_NODE_CODE_BLOCK: {
        semNode->type = NodeType::CodeBlock;
        const char *info = cmark_node_get_fence_info(node);
        if (info) {
            semNode->codeLanguage = QString::fromUtf8(info);
        }
        const char *lit = cmark_node_get_literal(node);
        if (lit) {
            semNode->textContent = QString::fromUtf8(lit);
        }
        break;
    }

    case CMARK_NODE_THEMATIC_BREAK:
        semNode->type = NodeType::ThematicBreak;
        break;

    case CMARK_NODE_TEXT: {
        semNode->type = NodeType::Text;
        const char *lit = cmark_node_get_literal(node);
        if (lit) {
            semNode->textContent = QString::fromUtf8(lit);
        }
        break;
    }

    case CMARK_NODE_EMPH: {
        semNode->type = NodeType::Emphasis;
        int s = semNode->fullRange.start;
        int e = semNode->fullRange.end;
        if (e - s >= 2) {
            semNode->prefixRange = Core::SourceRange(s, s + 1);
            semNode->suffixRange = Core::SourceRange(e - 1, e);
            semNode->contentRange = Core::SourceRange(s + 1, e - 1);
        }
        break;
    }

    case CMARK_NODE_STRONG: {
        semNode->type = NodeType::Strong;
        int s = semNode->fullRange.start;
        int e = semNode->fullRange.end;
        if (e - s >= 4) {
            semNode->prefixRange = Core::SourceRange(s, s + 2);
            semNode->suffixRange = Core::SourceRange(e - 2, e);
            semNode->contentRange = Core::SourceRange(s + 2, e - 2);
        }
        break;
    }

    case CMARK_NODE_CODE: {
        semNode->type = NodeType::CodeSpan;
        const char *lit = cmark_node_get_literal(node);
        if (lit) {
            semNode->textContent = QString::fromUtf8(lit);
        }
        int s = semNode->fullRange.start;
        int e = semNode->fullRange.end;
        int tickCount = 0;
        while (s + tickCount < e && fullText.at(s + tickCount) == u'`') {
            tickCount++;
        }
        if (tickCount > 0 && e - s >= tickCount * 2) {
            semNode->prefixRange = Core::SourceRange(s, s + tickCount);
            semNode->suffixRange = Core::SourceRange(e - tickCount, e);
            semNode->contentRange = Core::SourceRange(s + tickCount, e - tickCount);
        }
        break;
    }

    case CMARK_NODE_LINK: {
        semNode->type = NodeType::Link;
        const char *url = cmark_node_get_url(node);
        if (url) {
            semNode->linkUrl = QString::fromUtf8(url);
        }
        const char *title = cmark_node_get_title(node);
        if (title) {
            semNode->linkTitle = QString::fromUtf8(title);
        }
        break;
    }

    case CMARK_NODE_IMAGE: {
        semNode->type = NodeType::Image;
        const char *url = cmark_node_get_url(node);
        if (url) {
            semNode->linkUrl = QString::fromUtf8(url);
        }
        break;
    }

    case CMARK_NODE_SOFTBREAK:
        semNode->type = NodeType::SoftBreak;
        break;

    case CMARK_NODE_LINEBREAK:
        semNode->type = NodeType::LineBreak;
        break;

    default:
        // Extensions: strikethrough, table, etc.
        if (typeStr == QStringLiteral("strikethrough")) {
            semNode->type = NodeType::Strikethrough;
            int s = semNode->fullRange.start;
            int e = semNode->fullRange.end;
            if (e - s >= 4) {
                semNode->prefixRange = Core::SourceRange(s, s + 2);
                semNode->suffixRange = Core::SourceRange(e - 2, e);
                semNode->contentRange = Core::SourceRange(s + 2, e - 2);
            }
        } else if (typeStr == QStringLiteral("table")) {
            semNode->type = NodeType::Table;
        } else if (typeStr == QStringLiteral("table_row")) {
            semNode->type = NodeType::TableRow;
        } else if (typeStr == QStringLiteral("table_cell")) {
            semNode->type = NodeType::TableCell;
        } else {
            semNode->type = NodeType::CustomBlock;
        }
        break;
    }

    // Traverse children
    for (cmark_node *child = cmark_node_first_child(node); child != nullptr; child = cmark_node_next(child)) {
        auto childSem = convertNode(child, buffer);
        if (childSem) {
            semNode->children.append(childSem);
        }
    }

    return semNode;
}

SemanticNodePtr MarkdownParserAdapter::parse(const Core::TextBuffer &buffer) {
    initExtensions();

    int options = CMARK_OPT_DEFAULT | CMARK_OPT_SOURCEPOS;
    cmark_parser *parser = cmark_parser_new(options);

    // Attach extensions
    cmark_syntax_extension *table_ext = cmark_find_syntax_extension("table");
    if (table_ext) cmark_parser_attach_syntax_extension(parser, table_ext);

    cmark_syntax_extension *strikethrough_ext = cmark_find_syntax_extension("strikethrough");
    if (strikethrough_ext) cmark_parser_attach_syntax_extension(parser, strikethrough_ext);

    cmark_syntax_extension *autolink_ext = cmark_find_syntax_extension("autolink");
    if (autolink_ext) cmark_parser_attach_syntax_extension(parser, autolink_ext);

    cmark_syntax_extension *tasklist_ext = cmark_find_syntax_extension("tasklist");
    if (tasklist_ext) cmark_parser_attach_syntax_extension(parser, tasklist_ext);

    QByteArray utf8Data = buffer.text().toUtf8();
    cmark_parser_feed(parser, utf8Data.constData(), utf8Data.size());
    cmark_node *doc = cmark_parser_finish(parser);

    SemanticNodePtr root = convertNode(doc, buffer);

    cmark_node_free(doc);
    cmark_parser_free(parser);

    return root;
}

} // namespace MarkdownTool::Parser
