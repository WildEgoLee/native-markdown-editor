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

bool MarkdownParserAdapter::canIncrementalParse(const SemanticNodePtr &previousRoot,
                                                const Core::TextBuffer &buffer,
                                                int editOffset,
                                                int lengthRemoved,
                                                int lengthAdded,
                                                int &outChildIndex) const
{
    outChildIndex = -1;
    if (!previousRoot || previousRoot->children.isEmpty()) {
        return false;
    }

    if (editOffset < 0 || editOffset + lengthAdded > buffer.length()) {
        return false;
    }

    // 1. Inspect inserted text in buffer:
    // If inserted text contains ANY newline, carriage return, code fence, blockquote, list marker, or heading prefix,
    // it can change line numbers, block structure, or hierarchy. Must fall back to full cold parse!
    if (lengthAdded > 0) {
        QString insertText = buffer.substring(editOffset, lengthAdded);
        if (insertText.contains(u'\n') ||
            insertText.contains(u'\r') ||
            insertText.contains(QStringLiteral("```")) ||
            insertText.contains(QStringLiteral(">")) ||
            insertText.contains(QStringLiteral("- ")) ||
            insertText.contains(QStringLiteral("* ")) ||
            insertText.contains(QStringLiteral("#")) ||
            insertText.contains(QStringLiteral("|")))
        {
            return false;
        }
    }

    // 2. Find the single top-level block child that encloses this edit
    int editEnd = editOffset + lengthRemoved;
    for (int i = 0; i < previousRoot->children.size(); ++i) {
        const auto &child = previousRoot->children[i];
        if (editOffset >= child->fullRange.start && editEnd <= child->fullRange.end) {
            // Must be a leaf Paragraph block
            if (child->type == NodeType::Paragraph) {
                // If text was removed, ensure lengthRemoved did not delete past paragraph boundaries
                int newLen = child->fullRange.length() + (lengthAdded - lengthRemoved);
                if (newLen > 0) {
                    QString blockText = buffer.substring(child->fullRange.start, newLen);

                    // P0: Cross-block link reference dependency safety:
                    // 1. If this block defines a link reference (contains ']:'), editing it alters references for other blocks!
                    if (blockText.contains(QStringLiteral("]:"))) {
                        return false;
                    }
                    // 2. If the document defines reference links and this block contains brackets '[',
                    // isolated snippet parsing cannot resolve definitions outside this block. Fall back to full parse!
                    if (blockText.contains(u'[') && buffer.text().contains(QStringLiteral("]:"))) {
                        return false;
                    }

                    outChildIndex = i;
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

bool MarkdownParserAdapter::canIncrementalParse(const SemanticNodePtr &previousRoot,
                                                const Core::TextBuffer &buffer,
                                                int editOffset,
                                                int lengthRemoved,
                                                const QString &insertText,
                                                int &outChildIndex) const
{
    return canIncrementalParse(previousRoot, buffer, editOffset, lengthRemoved, insertText.length(), outChildIndex);
}

SemanticNodePtr MarkdownParserAdapter::incrementalParse(const SemanticNodePtr &previousRoot,
                                                         const Core::TextBuffer &buffer,
                                                         int editOffset,
                                                         int lengthRemoved,
                                                         int lengthAdded,
                                                         int childIndex,
                                                         bool *outSucceeded)
{
    if (outSucceeded) *outSucceeded = false;

    if (!previousRoot || childIndex < 0 || childIndex >= previousRoot->children.size()) {
        return parse(buffer);
    }

    int delta = lengthAdded - lengthRemoved;
    const auto &oldChild = previousRoot->children[childIndex];
    int blockStart = oldChild->fullRange.start;
    int blockLen = oldChild->fullRange.length() + delta;

    if (blockStart < 0 || blockStart + blockLen > buffer.length()) {
        return parse(buffer);
    }

    QString blockText = buffer.substring(blockStart, blockLen);
    Core::TextBuffer snippetBuffer;
    snippetBuffer.setText(blockText);

    auto snippetRoot = parse(snippetBuffer);
    // Double-lock safety 1: snippet must produce EXACTLY ONE block of the EXACT SAME type!
    if (!snippetRoot || snippetRoot->children.size() != 1 || snippetRoot->children[0]->type != oldChild->type) {
        return parse(buffer);
    }

    SemanticNodePtr newChild = snippetRoot->children[0];

    // Double-lock safety 2: Verify inline link count semantic consistency
    // If snippet parsing lost links (e.g. unresolved reference link), fall back to full parse!
    auto countLinks = [](auto self, const SemanticNodePtr &node) -> int {
        if (!node) return 0;
        int count = (node->type == NodeType::Link || node->type == NodeType::Image) ? 1 : 0;
        for (const auto &c : node->children) {
            count += self(self, c);
        }
        return count;
    };
    if (countLinks(countLinks, oldChild) != countLinks(countLinks, newChild)) {
        return parse(buffer);
    }

    // Offset all ranges in newChild by +blockStart
    auto offsetNode = [blockStart](auto self, const SemanticNodePtr &node) -> void {
        if (!node) return;
        node->fullRange.start += blockStart;
        node->fullRange.end += blockStart;
        node->contentRange.start += blockStart;
        node->contentRange.end += blockStart;
        if (node->prefixRange.isValid() && !node->prefixRange.isEmpty()) {
            node->prefixRange.start += blockStart;
            node->prefixRange.end += blockStart;
        }
        if (node->suffixRange.isValid() && !node->suffixRange.isEmpty()) {
            node->suffixRange.start += blockStart;
            node->suffixRange.end += blockStart;
        }
        if (node->isTaskItem && node->taskCheckboxRange.isValid() && !node->taskCheckboxRange.isEmpty()) {
            node->taskCheckboxRange.start += blockStart;
            node->taskCheckboxRange.end += blockStart;
        }
        for (const auto &c : node->children) {
            self(self, c);
        }
    };
    offsetNode(offsetNode, newChild);

    // Create a new document root with updated bounds
    auto newRoot = std::make_shared<SemanticNode>(*previousRoot);
    newRoot->fullRange.end += delta;
    newRoot->contentRange.end += delta;
    newRoot->children.resize(previousRoot->children.size());

    // Recursive helper to clone and shift subsequent nodes by delta without redundant children copy
    auto shiftNode = [delta](auto self, const SemanticNodePtr &src) -> SemanticNodePtr {
        if (!src) return nullptr;
        auto cloned = std::make_shared<SemanticNode>();
        cloned->type = src->type;
        cloned->fullRange = Core::SourceRange(src->fullRange.start + delta, src->fullRange.end + delta);
        cloned->contentRange = Core::SourceRange(src->contentRange.start + delta, src->contentRange.end + delta);
        cloned->prefixRange = (src->prefixRange.isValid() && !src->prefixRange.isEmpty())
            ? Core::SourceRange(src->prefixRange.start + delta, src->prefixRange.end + delta)
            : src->prefixRange;
        cloned->suffixRange = (src->suffixRange.isValid() && !src->suffixRange.isEmpty())
            ? Core::SourceRange(src->suffixRange.start + delta, src->suffixRange.end + delta)
            : src->suffixRange;
        cloned->taskCheckboxRange = (src->isTaskItem && src->taskCheckboxRange.isValid() && !src->taskCheckboxRange.isEmpty())
            ? Core::SourceRange(src->taskCheckboxRange.start + delta, src->taskCheckboxRange.end + delta)
            : src->taskCheckboxRange;
        cloned->headingLevel = src->headingLevel;
        cloned->isTaskItem = src->isTaskItem;
        cloned->isTaskChecked = src->isTaskChecked;
        cloned->taskCheckboxRange = (src->isTaskItem && src->taskCheckboxRange.isValid() && !src->taskCheckboxRange.isEmpty())
            ? Core::SourceRange(src->taskCheckboxRange.start + delta, src->taskCheckboxRange.end + delta)
            : src->taskCheckboxRange;
        cloned->codeLanguage = src->codeLanguage;
        cloned->linkUrl = src->linkUrl;
        cloned->linkTitle = src->linkTitle;
        cloned->textContent = src->textContent;

        cloned->children.reserve(src->children.size());
        for (const auto &child : src->children) {
            cloned->children.push_back(self(self, child));
        }
        return cloned;
    };

    // Prefix children (0 .. childIndex - 1): unmodified shared pointers
    for (int i = 0; i < childIndex; ++i) {
        newRoot->children[i] = previousRoot->children[i];
    }
    // Edited child
    newRoot->children[childIndex] = newChild;
    // Suffix children (childIndex + 1 .. N - 1): clone with ranges shifted by delta
    for (int i = childIndex + 1; i < previousRoot->children.size(); ++i) {
        newRoot->children[i] = shiftNode(shiftNode, previousRoot->children[i]);
    }

    if (outSucceeded) *outSucceeded = true;

    return newRoot;
}

} // namespace MarkdownTool::Parser
