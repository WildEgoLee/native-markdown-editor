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

    bool canIncrementalParse(const SemanticNodePtr &previousRoot,
                             const Core::TextBuffer &buffer,
                             int editOffset,
                             int lengthRemoved,
                             int lengthAdded,
                             int &outChildIndex) const;

    bool canIncrementalParse(const SemanticNodePtr &previousRoot,
                             const Core::TextBuffer &buffer,
                             int editOffset,
                             int lengthRemoved,
                             const QString &insertText,
                             int &outChildIndex) const;

    SemanticNodePtr incrementalParse(const SemanticNodePtr &previousRoot,
                                     const Core::TextBuffer &buffer,
                                     int editOffset,
                                     int lengthRemoved,
                                     int lengthAdded,
                                     int childIndex);

private:
    void initExtensions();
    bool m_extensionsInitialized = false;
};

} // namespace MarkdownTool::Parser
