#pragma once

#include <QString>
#include <QByteArray>
#include <QSize>

namespace MarkdownTool::Render {

class MathSvgRenderer {
public:
    static QByteArray renderToSvg(const QString &latexFormula);
    static QSize estimateSize(const QString &latexFormula);
};

} // namespace MarkdownTool::Render
