#pragma once

#include "SemanticNode.h"
#include "core/TextBuffer.h"

#include <memory>

namespace MarkdownTool::Parser {

class MarkdownParserAdapter {
public:
    MarkdownParserAdapter();
    ~MarkdownParserAdapter();

    SemanticNodePtr parse(const Core::TextBuffer &buffer);

private:
    void initExtensions();
    bool m_extensionsInitialized = false;
};

} // namespace MarkdownTool::Parser
