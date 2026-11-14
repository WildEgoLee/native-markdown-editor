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
    s.replace(QStringLiteral(R"(\sum)"), QStringLiteral("∑"));
    s.replace(QStringLiteral(R"(\int)"), QStringLiteral("∫"));
    s.replace(QStringLiteral(R"(\prod)"), QStringLiteral("∏"));
    return s;
}

// Helper to find matching closing brace '}' for the opening '{' at startIdx
static int findMatchingBrace(const QString &str, int startIdx) {
    if (startIdx < 0 || startIdx >= str.length() || str.at(startIdx) != u'{') {
        return -1;
    }
    int depth = 0;
    for (int i = startIdx; i < str.length(); ++i) {
        if (str.at(i) == u'{') {
            depth++;
        } else if (str.at(i) == u'}') {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

struct MathBox {
    QString xml;
    int width = 0;
    int height = 40;
    int baseline = 26;
    bool hasWarning = false;
};

// Recursive box layout engine supporting nested \frac, scripts, and symbols
static MathBox renderBox(const QString &latex, int fontSize, int depth = 0) {
    MathBox box;
    box.height = std::max(36, fontSize * 2);
    box.baseline = std::max(24, static_cast<int>(fontSize * 1.4));

    if (latex.trimmed().isEmpty()) {
        box.width = 10;
        return box;
    }

    if (depth > 6) {
        // Prevent infinite recursion on pathological inputs
        box.xml = QStringLiteral("<text x=\"0\" y=\"%1\" font-size=\"%2\" fill=\"#EF4444\">...</text>")
                      .arg(box.baseline).arg(fontSize);
        box.width = 20;
        return box;
    }

    // Check for \frac
    int fracPos = latex.indexOf(QStringLiteral(R"(\frac)"));
    if (fracPos != -1) {
        int numStart = latex.indexOf(u'{', fracPos + 5);
        if (numStart != -1) {
            int numEnd = findMatchingBrace(latex, numStart);
            if (numEnd != -1) {
                int denStart = latex.indexOf(u'{', numEnd + 1);
                if (denStart != -1) {
                    int denEnd = findMatchingBrace(latex, denStart);
                    if (denEnd != -1) {
                        QString prefix = latex.left(fracPos);
                        QString numStr = latex.mid(numStart + 1, numEnd - (numStart + 1));
                        QString denStr = latex.mid(denStart + 1, denEnd - (denStart + 1));
                        QString suffix = latex.mid(denEnd + 1);

                        int subFont = std::max(11, static_cast<int>(fontSize * 0.78));
                        MathBox boxPrefix = renderBox(prefix, fontSize, depth);
                        MathBox boxNum = renderBox(numStr, subFont, depth + 1);
                        MathBox boxDen = renderBox(denStr, subFont, depth + 1);
                        MathBox boxSuffix = renderBox(suffix, fontSize, depth);

                        int fracContentWidth = std::max(boxNum.width, boxDen.width) + 12;
                        int fracHeight = boxNum.height + boxDen.height + 10;

                        box.height = std::max({boxPrefix.height, fracHeight, boxSuffix.height});
                        box.baseline = box.height / 2 + 4;
                        int barY = box.baseline - 4;

                        QString elements;
                        int curX = 0;

                        // 1. Prefix
                        if (!prefix.isEmpty()) {
                            int shiftY = box.baseline - boxPrefix.baseline;
                            elements.append(QStringLiteral("<g transform=\"translate(%1, %2)\">%3</g>\n")
                                .arg(curX).arg(shiftY).arg(boxPrefix.xml));
                            curX += boxPrefix.width + 6;
                        }

                        // 2. Fraction (Numerator, Line, Denominator)
                        int numX = curX + (fracContentWidth - boxNum.width) / 2;
                        int numY = barY - boxNum.height + (boxNum.height - boxNum.baseline) - 4;
                        elements.append(QStringLiteral("<g transform=\"translate(%1, %2)\">%3</g>\n")
                            .arg(numX).arg(numY).arg(boxNum.xml));

                        // Bar
                        elements.append(QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%2\" stroke=\"#0F172A\" stroke-width=\"1.6\" />\n")
                            .arg(curX).arg(barY).arg(curX + fracContentWidth));

                        int denX = curX + (fracContentWidth - boxDen.width) / 2;
                        int denY = barY + boxDen.baseline + 4;
                        elements.append(QStringLiteral("<g transform=\"translate(%1, %2)\">%3</g>\n")
                            .arg(denX).arg(denY).arg(boxDen.xml));

                        curX += fracContentWidth + 6;

                        // 3. Suffix
                        if (!suffix.isEmpty()) {
                            int shiftY = box.baseline - boxSuffix.baseline;
                            elements.append(QStringLiteral("<g transform=\"translate(%1, %2)\">%3</g>\n")
                                .arg(curX).arg(shiftY).arg(boxSuffix.xml));
                            curX += boxSuffix.width;
                        }

                        box.width = curX;
                        box.xml = elements;
                        return box;
                    }
                }
            }
        }

        // If \frac was found but braces were unbalanced:
        box.hasWarning = true;
    }

    // Check for unbalanced braces overall
    int braceBalance = 0;
    for (QChar c : latex) {
        if (c == u'{') braceBalance++;
        else if (c == u'}') braceBalance--;
    }
    if (braceBalance != 0) {
        box.hasWarning = true;
    }

    // Linear formula parsing with sub/superscripts
    QString processed = substituteGreekAndSymbols(latex);
    QString elements;
    int curX = 0;
    int i = 0;

    while (i < processed.length()) {
        QChar c = processed.at(i);
        if (c == u'^') {
            ++i;
            QString supText;
            if (i < processed.length() && processed.at(i) == u'{') {
                int end = findMatchingBrace(processed, i);
                if (end != -1) {
                    supText = processed.mid(i + 1, end - (i + 1));
                    i = end + 1;
                } else {
                    supText = processed.mid(i + 1);
                    i = static_cast<int>(processed.length());
                    box.hasWarning = true;
                }
            } else if (i < processed.length()) {
                supText = QString(processed.at(i));
                ++i;
            }
            supText = substituteGreekAndSymbols(supText);
            int supSize = std::max(10, static_cast<int>(fontSize * 0.7));
            int supWidth = std::max(12, static_cast<int>(supText.length()) * (supSize - 3));
            elements.append(QStringLiteral("<text x=\"%1\" y=\"%2\" font-family=\"'Cambria Math', 'Latin Modern Math', 'Times New Roman', serif\" font-size=\"%3\" fill=\"#0F172A\">%4</text>\n")
                .arg(curX).arg(box.baseline - fontSize / 2).arg(supSize).arg(escapeXml(supText)));
            curX += supWidth + 2;
        } else if (c == u'_') {
            ++i;
            QString subText;
            if (i < processed.length() && processed.at(i) == u'{') {
                int end = findMatchingBrace(processed, i);
                if (end != -1) {
                    subText = processed.mid(i + 1, end - (i + 1));
                    i = end + 1;
                } else {
                    subText = processed.mid(i + 1);
                    i = static_cast<int>(processed.length());
                    box.hasWarning = true;
                }
            } else if (i < processed.length()) {
                subText = QString(processed.at(i));
                ++i;
            }
            subText = substituteGreekAndSymbols(subText);
            int subSize = std::max(10, static_cast<int>(fontSize * 0.7));
            int subWidth = std::max(12, static_cast<int>(subText.length()) * (subSize - 3));
            elements.append(QStringLiteral("<text x=\"%1\" y=\"%2\" font-family=\"'Cambria Math', 'Latin Modern Math', 'Times New Roman', serif\" font-size=\"%3\" fill=\"#0F172A\">%4</text>\n")
                .arg(curX).arg(box.baseline + fontSize / 3).arg(subSize).arg(escapeXml(subText)));
            curX += subWidth + 2;
        } else {
            QString chunk;
            while (i < processed.length() && processed.at(i) != u'^' && processed.at(i) != u'_') {
                chunk.append(processed.at(i));
                ++i;
            }
            chunk = substituteGreekAndSymbols(chunk);
            int chunkWidth = static_cast<int>(chunk.length()) * (fontSize - 7) + 6;
            bool isBigSymbol = (chunk == QStringLiteral("∑") || chunk == QStringLiteral("∫") || chunk == QStringLiteral("∏"));
            int actualFont = isBigSymbol ? static_cast<int>(fontSize * 1.45) : fontSize;
            int textY = isBigSymbol ? box.baseline + 4 : box.baseline;
            QString style = isBigSymbol ? QStringLiteral("normal") : QStringLiteral("italic");

            elements.append(QStringLiteral("<text x=\"%1\" y=\"%2\" font-family=\"'Cambria Math', 'Latin Modern Math', 'Times New Roman', serif\" font-size=\"%3\" font-style=\"%4\" fill=\"#0F172A\">%5</text>\n")
                .arg(curX).arg(textY).arg(actualFont).arg(style).arg(escapeXml(chunk)));
            curX += chunkWidth + (isBigSymbol ? 8 : 2);
        }
    }

    box.width = curX;
    box.xml = elements;
    return box;
}

QSize MathSvgRenderer::estimateSize(const QString &latexFormula) {
    QString latex = latexFormula.trimmed();
    if (latex.startsWith(QStringLiteral("$$")) && latex.endsWith(QStringLiteral("$$")) && latex.length() >= 4) {
        latex = latex.mid(2, latex.length() - 4).trimmed();
    }
    MathBox box = renderBox(latex, 18);
    int totalWidth = std::max(120, box.width + 40);
    int totalHeight = std::max(50, box.height + (box.hasWarning ? 40 : 20));
    return QSize(totalWidth, totalHeight);
}

QByteArray MathSvgRenderer::renderToSvg(const QString &latexFormula) {
    QString latex = latexFormula.trimmed();
    if (latex.startsWith(QStringLiteral("$$")) && latex.endsWith(QStringLiteral("$$")) && latex.length() >= 4) {
        latex = latex.mid(2, latex.length() - 4).trimmed();
    }

    MathBox mainBox = renderBox(latex, 18);

    int contentY = 24;
    int warningExtra = 0;
    QString warningXml;

    if (mainBox.hasWarning) {
        warningExtra = 26;
        contentY += warningExtra;
        warningXml = QStringLiteral(
            "<g transform=\"translate(16, 12)\">\n"
            "  <rect x=\"0\" y=\"0\" width=\"170\" height=\"18\" rx=\"3\" fill=\"#FEF2F2\" stroke=\"#F87171\" stroke-width=\"1\" />\n"
            "  <text x=\"85\" y=\"13\" font-family=\"'Segoe UI', -apple-system, sans-serif\" font-size=\"10\" font-weight=\"600\" fill=\"#DC2626\" text-anchor=\"middle\">LaTeX: Unbalanced Braces</text>\n"
            "</g>\n");
    }

    int totalWidth = std::clamp(std::max(140, mainBox.width + 40), 140, 2000);
    int totalHeight = std::clamp(std::max(50, mainBox.height + contentY + 16), 50, 1000);

    QString svg = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"0 0 %1 %2\">\n"
        "  <defs>\n"
        "    <style>\n"
        "      text { text-rendering: geometricPrecision; -webkit-font-smoothing: antialiased; }\n"
        "    </style>\n"
        "  </defs>\n"
        "  %3\n"
        "  <g transform=\"translate(20, %4)\">\n"
        "    %5\n"
        "  </g>\n"
        "</svg>\n")
        .arg(totalWidth)
        .arg(totalHeight)
        .arg(warningXml)
        .arg(contentY)
        .arg(mainBox.xml);

    return svg.toUtf8();
}

} // namespace MarkdownTool::Render
