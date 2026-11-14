#pragma once

#include <QString>
#include <QByteArray>
#include <QSize>

namespace MarkdownTool::Render {

class MermaidSvgRenderer {
public:
    static QByteArray renderToSvg(const QString &mermaidCode);
    static QSize estimateSize(const QString &mermaidCode);
};

} // namespace MarkdownTool::Render
