#include "MathSvgRenderer.h"

#include <QStringList>
#include <QRegularExpression>
#include <QtGlobal>
#include <algorithm>

namespace MarkdownTool::Render {

static QString escapeXml(const QString &str) {
    QString out;
    out.reserve(str.length());
    for (QChar c : str) {
        if (c == u'&') out.append(QStringLiteral("&amp;"));
        else if (c == u'<') out.append(QStringLiteral("&lt;"));
        else if (c == u'>') out.append(QStringLiteral("&gt;"));
        else if (c == u'"') out.append(QStringLiteral("&quot;"));
        else if (c == u'\'') out.append(QStringLiteral("&apos;"));
        else out.append(c);
    }
    return out;
}

static QString substituteGreekAndSymbols(QString s) {
    s.replace(QStringLiteral(R"(\alpha)"), QStringLiteral("α"));
    s.replace(QStringLiteral(R"(\beta)"), QStringLiteral("β"));
    s.replace(QStringLiteral(R"(\gamma)"), QStringLiteral("γ"));
    s.replace(QStringLiteral(R"(\theta)"), QStringLiteral("θ"));
    s.replace(QStringLiteral(R"(\lambda)"), QStringLiteral("λ"));
    s.replace(QStringLiteral(R"(\mu)"), QStringLiteral("μ"));
    s.replace(QStringLiteral(R"(\pi)"), QStringLiteral("π"));
    s.replace(QStringLiteral(R"(\sigma)"), QStringLiteral("σ"));
    s.replace(QStringLiteral(R"(\omega)"), QStringLiteral("ω"));
    s.replace(QStringLiteral(R"(\Delta)"), QStringLiteral("Δ"));
    s.replace(QStringLiteral(R"(\Omega)"), QStringLiteral("Ω"));
    s.replace(QStringLiteral(R"(\infty)"), QStringLiteral("∞"));
    s.replace(QStringLiteral(R"(\pm)"), QStringLiteral("±"));
    s.replace(QStringLiteral(R"(\times)"), QStringLiteral("×"));
    s.replace(QStringLiteral(R"(\div)"), QStringLiteral("÷"));
    s.replace(QStringLiteral(R"(\neq)"), QStringLiteral("≠"));
    s.replace(QStringLiteral(R"(\le)"), QStringLiteral("≤"));
    s.replace(QStringLiteral(R"(\ge)"), QStringLiteral("≥"));
    s.replace(QStringLiteral(R"(\approx)"), QStringLiteral("≈"));
    s.replace(QStringLiteral(R"(\cdot)"), QStringLiteral("·"));
    s.replace(QStringLiteral(R"(\rightarrow)"), QStringLiteral("→"));
    s.replace(QStringLiteral(R"(\leftarrow)"), QStringLiteral("←"));
    s.replace(QStringLiteral(R"(\in)"), QStringLiteral("∈"));
    s.replace(QStringLiteral(R"(\notin)"), QStringLiteral("∉"));
    s.replace(QStringLiteral(R"(\subset)"), QStringLiteral("⊂"));
    s.replace(QStringLiteral(R"(\cup)"), QStringLiteral("∪"));
    s.replace(QStringLiteral(R"(\cap)"), QStringLiteral("∩"));
    s.replace(QStringLiteral(R"(\forall)"), QStringLiteral("∀"));
    s.replace(QStringLiteral(R"(\exists)"), QStringLiteral("∃"));
    s.replace(QStringLiteral(R"(\partial)"), QStringLiteral("∂"));
    s.replace(QStringLiteral(R"(\nabla)"), QStringLiteral("∇"));
    s.replace(QStringLiteral(R"(\cdots)"), QStringLiteral("⋯"));
    s.replace(QStringLiteral(R"(\ldots)"), QStringLiteral("…"));
    s.replace(QStringLiteral(R"(\quad)"), QStringLiteral("  "));
    s.replace(QStringLiteral(R"(\qquad)"), QStringLiteral("    "));
    s.replace(QStringLiteral(R"(\,)"), QStringLiteral(" "));
    return s;
}

QSize MathSvgRenderer::estimateSize(const QString &latexFormula) {
    QString trimmed = latexFormula.trimmed();
    int len = static_cast<int>(trimmed.length());
    int width = std::max(120, len * 14 + 40);
    int height = 50;
    if (trimmed.contains(QStringLiteral(R"(\frac)")) || trimmed.contains(QStringLiteral(R"(\sum)")) || trimmed.contains(QStringLiteral(R"(\int)"))) {
        height = 70;
    }
    return QSize(width, height);
}

QByteArray MathSvgRenderer::renderToSvg(const QString &latexFormula) {
    QString latex = latexFormula.trimmed();
    if (latex.startsWith(QStringLiteral("$$")) && latex.endsWith(QStringLiteral("$$")) && latex.length() >= 4) {
        latex = latex.mid(2, latex.length() - 4).trimmed();
    }

    // Fraction layout: \frac{numerator}{denominator}
    static QRegularExpression fracRe(QStringLiteral(R"(\\frac\{([^{}]+)\}\{([^{}]+)\})"));
    auto fracMatch = fracRe.match(latex);

    int totalHeight = 56;
    int baselineY = 34;

    QString elementsXml;
    int currentX = 20;

    if (fracMatch.hasMatch()) {
        int prefixEnd = static_cast<int>(fracMatch.capturedStart(0));
        int suffixStart = static_cast<int>(fracMatch.capturedEnd(0));
        QString prefix = latex.left(prefixEnd).trimmed();
        QString num = substituteGreekAndSymbols(fracMatch.captured(1).trimmed());
        QString den = substituteGreekAndSymbols(fracMatch.captured(2).trimmed());
        QString suffix = latex.mid(suffixStart).trimmed();

        totalHeight = 68;
        baselineY = 40;

        if (!prefix.isEmpty()) {
            prefix = substituteGreekAndSymbols(prefix);
            int pWidth = static_cast<int>(prefix.length()) * 11;
            elementsXml.append(QString::fromUtf8(R"(<text x="%1" y="%2" font-family="'Cambria Math', 'Latin Modern Math', 'Times New Roman', serif" font-size="18" font-style="italic" fill="#0F172A">%3</text>)")
                .arg(currentX).arg(baselineY).arg(escapeXml(prefix)));
            currentX += pWidth + 8;
        }

        int numWidth = std::max(20, static_cast<int>(num.length()) * 10);
        int denWidth = std::max(20, static_cast<int>(den.length()) * 10);
        int fracWidth = std::max(numWidth, denWidth) + 12;

        int numX = currentX + (fracWidth - numWidth) / 2;
        int denX = currentX + (fracWidth - denWidth) / 2;
        int barY = baselineY - 6;

        // Numerator
        elementsXml.append(QString::fromUtf8(R"(<text x="%1" y="%2" font-family="'Cambria Math', 'Latin Modern Math', 'Times New Roman', serif" font-size="14" fill="#0F172A">%3</text>)")
            .arg(numX).arg(barY - 8).arg(escapeXml(num)));

        // Fraction bar
        elementsXml.append(QString::fromUtf8(R"(<line x1="%1" y1="%2" x2="%3" y2="%4" stroke="#0F172A" stroke-width="1.6" />)")
            .arg(currentX).arg(barY).arg(currentX + fracWidth).arg(barY));

        // Denominator
        elementsXml.append(QString::fromUtf8(R"(<text x="%1" y="%2" font-family="'Cambria Math', 'Latin Modern Math', 'Times New Roman', serif" font-size="14" fill="#0F172A">%3</text>)")
            .arg(denX).arg(barY + 18).arg(escapeXml(den)));

        currentX += fracWidth + 8;

        if (!suffix.isEmpty()) {
            suffix = substituteGreekAndSymbols(suffix);
            int sWidth = static_cast<int>(suffix.length()) * 11;
            elementsXml.append(QString::fromUtf8(R"(<text x="%1" y="%2" font-family="'Cambria Math', 'Latin Modern Math', 'Times New Roman', serif" font-size="18" font-style="italic" fill="#0F172A">%3</text>)")
                .arg(currentX).arg(baselineY).arg(escapeXml(suffix)));
            currentX += sWidth + 8;
        }
    } else {
        // Linear formula layout with support for sub/superscripts & symbols
        QString processed = substituteGreekAndSymbols(latex);

        // Check for \sum or \int
        if (processed.contains(QStringLiteral(R"(\sum)"))) {
            processed.replace(QStringLiteral(R"(\sum)"), QStringLiteral("∑"));
            totalHeight = 64;
            baselineY = 38;
        }
        if (processed.contains(QStringLiteral(R"(\int)"))) {
            processed.replace(QStringLiteral(R"(\int)"), QStringLiteral("∫"));
            totalHeight = 64;
            baselineY = 38;
        }
        if (processed.contains(QStringLiteral(R"(\prod)"))) {
            processed.replace(QStringLiteral(R"(\prod)"), QStringLiteral("∏"));
            totalHeight = 64;
            baselineY = 38;
        }

        // Parse superscripts (^) and subscripts (_)
        int i = 0;
        while (i < processed.length()) {
            QChar c = processed.at(i);
            if (c == u'^') {
                ++i;
                QString supText;
                if (i < processed.length() && processed.at(i) == u'{') {
                    int end = static_cast<int>(processed.indexOf(u'}', i + 1));
                    if (end != -1) {
                        supText = processed.mid(i + 1, end - (i + 1));
                        i = end + 1;
                    } else {
                        supText = processed.mid(i + 1);
                        i = static_cast<int>(processed.length());
                    }
                } else if (i < processed.length()) {
                    supText = QString(processed.at(i));
                    ++i;
                }
                int supWidth = static_cast<int>(supText.length()) * 8;
                elementsXml.append(QString::fromUtf8(R"(<text x="%1" y="%2" font-family="'Cambria Math', 'Latin Modern Math', 'Times New Roman', serif" font-size="12" fill="#0F172A">%3</text>)")
                    .arg(currentX).arg(baselineY - 10).arg(escapeXml(supText)));
                currentX += supWidth + 2;
            } else if (c == u'_') {
                ++i;
                QString subText;
                if (i < processed.length() && processed.at(i) == u'{') {
                    int end = static_cast<int>(processed.indexOf(u'}', i + 1));
                    if (end != -1) {
                        subText = processed.mid(i + 1, end - (i + 1));
                        i = end + 1;
                    } else {
                        subText = processed.mid(i + 1);
                        i = static_cast<int>(processed.length());
                    }
                } else if (i < processed.length()) {
                    subText = QString(processed.at(i));
                    ++i;
                }
                int subWidth = static_cast<int>(subText.length()) * 8;
                elementsXml.append(QString::fromUtf8(R"(<text x="%1" y="%2" font-family="'Cambria Math', 'Latin Modern Math', 'Times New Roman', serif" font-size="12" fill="#0F172A">%3</text>)")
                    .arg(currentX).arg(baselineY + 7).arg(escapeXml(subText)));
                currentX += subWidth + 2;
            } else {
                QString chunk;
                while (i < processed.length() && processed.at(i) != u'^' && processed.at(i) != u'_') {
                    chunk.append(processed.at(i));
                    ++i;
                }
                int chunkWidth = static_cast<int>(chunk.length()) * 11;
                bool isSymbol = (chunk == QStringLiteral("∑") || chunk == QStringLiteral("∫") || chunk == QStringLiteral("∏"));
                int fontSize = isSymbol ? 26 : 18;
                int textY = isSymbol ? baselineY + 4 : baselineY;
                QString fontStyle = isSymbol ? QStringLiteral("normal") : QStringLiteral("italic");

                elementsXml.append(QString::fromUtf8(R"(<text x="%1" y="%2" font-family="'Cambria Math', 'Latin Modern Math', 'Times New Roman', serif" font-size="%3" font-style="%4" fill="#0F172A">%5</text>)")
                    .arg(currentX).arg(textY).arg(fontSize).arg(fontStyle).arg(escapeXml(chunk)));
                currentX += chunkWidth + (isSymbol ? 8 : 2);
            }
        }
    }

    int totalWidth = std::max(120, currentX + 20);

    QString svg = QString::fromUtf8(
        R"(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="%1" height="%2" viewBox="0 0 %1 %2">
  <defs>
    <style>
      text { text-rendering: geometricPrecision; -webkit-font-smoothing: antialiased; }
    </style>
  </defs>
  %3
</svg>)").arg(totalWidth).arg(totalHeight).arg(elementsXml);

    return svg.toUtf8();
}

} // namespace MarkdownTool::Render
