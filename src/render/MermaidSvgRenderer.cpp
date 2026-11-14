#include "MermaidSvgRenderer.h"

#include <QStringList>
#include <QRegularExpression>
#include <QMap>
#include <QVector>
#include <cmath>
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

struct NodeInfo {
    QString id;
    QString label;
    QString shape; // "rect", "round", "diamond", "circle"
    qreal x = 0;
    qreal y = 0;
    qreal w = 120;
    qreal h = 42;
    int rank = 0;
};

struct EdgeInfo {
    QString from;
    QString to;
    QString label;
    bool isDashed = false;
    bool isThick = false;
};

QSize MermaidSvgRenderer::estimateSize(const QString &mermaidCode) {
    QStringList lines = mermaidCode.split(u'\n');
    int lineCount = static_cast<int>(lines.size());
    bool isLR = mermaidCode.contains(QStringLiteral("LR")) || mermaidCode.contains(QStringLiteral("lr"));
    if (isLR) {
        return QSize(std::max(350, lineCount * 120), 180);
    }
    return QSize(std::max(300, 400), std::max(160, lineCount * 65));
}

QByteArray MermaidSvgRenderer::renderToSvg(const QString &mermaidCode) {
    QStringList rawLines = mermaidCode.split(u'\n');
    QString firstLine;
    for (const auto &l : rawLines) {
        QString trimmed = l.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(QStringLiteral("%%"))) {
            firstLine = trimmed;
            break;
        }
    }

    bool isLR = firstLine.contains(QStringLiteral("LR")) || firstLine.contains(QStringLiteral("lr"));
    bool isSequence = firstLine.startsWith(QStringLiteral("sequenceDiagram"));

    // Sequence Diagram renderer
    if (isSequence) {
        QStringList participants;
        struct SeqMsg {
            QString from;
            QString to;
            QString text;
            bool isDashed = false;
        };
        QVector<SeqMsg> messages;

        static QRegularExpression reMsg(QStringLiteral("^\\s*(\\w+)\\s*(-->>|->>|-->|->)\\s*(\\w+)\\s*:\\s*(.*)$"));
        static QRegularExpression rePart(QStringLiteral("^\\s*participant\\s+(\\w+)(?:\\s+as\\s+(.*))?$"));

        for (const QString &raw : rawLines) {
            QString line = raw.trimmed();
            if (line.isEmpty() || line.startsWith(QStringLiteral("sequenceDiagram")) || line.startsWith(QStringLiteral("%%"))) {
                continue;
            }

            auto pMatch = rePart.match(line);
            if (pMatch.hasMatch()) {
                QString pId = pMatch.captured(1);
                if (!participants.contains(pId)) {
                    participants.append(pId);
                }
                continue;
            }

            auto mMatch = reMsg.match(line);
            if (mMatch.hasMatch()) {
                QString from = mMatch.captured(1);
                QString arrow = mMatch.captured(2);
                QString to = mMatch.captured(3);
                QString txt = mMatch.captured(4);

                if (!participants.contains(from)) participants.append(from);
                if (!participants.contains(to)) participants.append(to);

                messages.append(SeqMsg{from, to, txt, arrow.contains(QStringLiteral("--"))});
            }
        }

        if (participants.isEmpty()) {
            participants = {QStringLiteral("Client"), QStringLiteral("Server")};
        }

        int pCount = static_cast<int>(participants.size());
        qreal colWidth = 160.0;
        qreal totalWidth = std::max(380.0, pCount * colWidth + 60.0);
        qreal rowHeight = 48.0;
        qreal totalHeight = std::max(180.0, static_cast<qreal>(messages.size()) * rowHeight + 110.0);

        QString svg;
        svg.append(QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"0 0 %1 %2\">\n"
            "  <defs>\n"
            "    <marker id=\"seq-arrow\" markerWidth=\"8\" markerHeight=\"8\" refX=\"7\" refY=\"3.5\" orient=\"auto\">\n"
            "      <polygon points=\"0 0, 8 3.5, 0 7\" fill=\"#2563EB\" />\n"
            "    </marker>\n"
            "  </defs>\n").arg(totalWidth).arg(totalHeight));

        QMap<QString, qreal> pX;
        for (int i = 0; i < pCount; ++i) {
            qreal x = 40.0 + i * colWidth + colWidth / 2.0;
            pX[participants[i]] = x;

            // Lifeline dashed line
            svg.append(QStringLiteral("<line x1=\"%1\" y1=\"50\" x2=\"%1\" y2=\"%2\" stroke=\"#94A3B8\" stroke-width=\"1.5\" stroke-dasharray=\"4,4\" />\n")
                .arg(x).arg(totalHeight - 20));

            // Participant box top
            svg.append(QStringLiteral("<rect x=\"%1\" y=\"16\" width=\"100\" height=\"34\" rx=\"6\" fill=\"#F1F5F9\" stroke=\"#64748B\" stroke-width=\"1.5\" />\n")
                .arg(x - 50));
            svg.append(QStringLiteral("<text x=\"%1\" y=\"38\" font-family=\"'Segoe UI', -apple-system, sans-serif\" font-size=\"13\" font-weight=\"600\" fill=\"#0F172A\" text-anchor=\"middle\">%2</text>\n")
                .arg(x).arg(escapeXml(participants[i])));
        }

        qreal curY = 80.0;
        for (const auto &m : messages) {
            qreal x1 = pX.value(m.from, 60.0);
            qreal x2 = pX.value(m.to, 220.0);
            qreal textX = (x1 + x2) / 2.0;

            QString dash = m.isDashed ? QStringLiteral(" stroke-dasharray=\"4,3\"") : QString();
            qreal arrowTargetX = (x1 < x2) ? (x2 - 4) : (x2 + 4);

            svg.append(QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%2\" stroke=\"#2563EB\" stroke-width=\"1.8\"%4 marker-end=\"url(#seq-arrow)\" />\n")
                .arg(x1).arg(curY).arg(arrowTargetX).arg(dash));

            svg.append(QStringLiteral("<text x=\"%1\" y=\"%2\" font-family=\"'Segoe UI', -apple-system, sans-serif\" font-size=\"12\" fill=\"#1E293B\" text-anchor=\"middle\">%3</text>\n")
                .arg(textX).arg(curY - 6).arg(escapeXml(m.text)));

            curY += rowHeight;
        }

        svg.append(QStringLiteral("</svg>"));
        return svg.toUtf8();
    }

    // Graph / Flowchart renderer (TD / LR)
    QMap<QString, NodeInfo> nodes;
    QVector<EdgeInfo> edges;

    static QRegularExpression reEdge(QStringLiteral("^([A-Za-z0-9_]+)(?:\\[(.*?)\\]|\\((.*?)\\)|\\{(.*?)\\}|\\(\\((.*?)\\)\\))?\\s*(-->|---|-.->|==>)(?:\\|(.*?)\\|)?\\s*([A-Za-z0-9_]+)(?:\\[(.*?)\\]|\\((.*?)\\)|\\{(.*?)\\}|\\(\\((.*?)\\)\\))?$"));
    static QRegularExpression reNodeOnly(QStringLiteral("^([A-Za-z0-9_]+)(?:\\[(.*?)\\]|\\((.*?)\\)|\\{(.*?)\\}|\\(\\((.*?)\\)\\))$"));

    auto extractNode = [&](const QString &id, const QString &box, const QString &round, const QString &diam, const QString &circ) {
        if (!nodes.contains(id)) {
            NodeInfo n;
            n.id = id;
            if (!box.isEmpty()) { n.label = box; n.shape = QStringLiteral("rect"); }
            else if (!round.isEmpty()) { n.label = round; n.shape = QStringLiteral("round"); }
            else if (!diam.isEmpty()) { n.label = diam; n.shape = QStringLiteral("diamond"); }
            else if (!circ.isEmpty()) { n.label = circ; n.shape = QStringLiteral("circle"); }
            else { n.label = id; n.shape = QStringLiteral("round"); }
            n.w = std::max(110.0, static_cast<qreal>(n.label.length()) * 9.5 + 24.0);
            nodes[id] = n;
        }
    };

    for (const QString &raw : rawLines) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("graph")) || line.startsWith(QStringLiteral("flowchart")) || line.startsWith(QStringLiteral("%%"))) {
            continue;
        }

        auto em = reEdge.match(line);
        if (em.hasMatch()) {
            QString id1 = em.captured(1);
            extractNode(id1, em.captured(2), em.captured(3), em.captured(4), em.captured(5));

            QString arr = em.captured(6);
            QString label = em.captured(7);

            QString id2 = em.captured(8);
            extractNode(id2, em.captured(9), em.captured(10), em.captured(11), em.captured(12));

            edges.append(EdgeInfo{id1, id2, label, arr.contains(QStringLiteral("-.")), arr.contains(QStringLiteral("=="))});
            continue;
        }

        auto nm = reNodeOnly.match(line);
        if (nm.hasMatch()) {
            extractNode(nm.captured(1), nm.captured(2), nm.captured(3), nm.captured(4), nm.captured(5));
        }
    }

    if (nodes.isEmpty()) {
        NodeInfo d;
        d.id = QStringLiteral("A");
        d.label = QStringLiteral("Diagram");
        d.shape = QStringLiteral("round");
        nodes[d.id] = d;
    }

    // Calculate layout ranks (BFS / Layering)
    QMap<QString, int> ranks;
    for (const auto &k : nodes.keys()) ranks[k] = 0;

    for (int pass = 0; pass < nodes.size(); ++pass) {
        for (const auto &e : edges) {
            if (ranks.contains(e.from) && ranks.contains(e.to)) {
                ranks[e.to] = std::max(ranks[e.to], ranks[e.from] + 1);
            }
        }
    }

    QMap<int, QStringList> rankGroups;
    int maxRank = 0;
    for (auto it = ranks.begin(); it != ranks.end(); ++it) {
        rankGroups[it.value()].append(it.key());
        maxRank = std::max(maxRank, it.value());
    }

    qreal totalWidth = 0;
    qreal totalHeight = 0;

    if (isLR) {
        qreal layerX = 30.0;
        qreal maxColH = 0.0;
        for (int r = 0; r <= maxRank; ++r) {
            const auto &group = rankGroups[r];
            qreal curY = 30.0;
            qreal maxW = 120.0;
            for (const auto &id : group) {
                auto &n = nodes[id];
                n.x = layerX;
                n.y = curY;
                maxW = std::max(maxW, n.w);
                curY += n.h + 28.0;
            }
            maxColH = std::max(maxColH, curY);
            layerX += maxW + 65.0;
        }
        totalWidth = std::max(360.0, layerX);
        totalHeight = std::max(140.0, maxColH + 10.0);
    } else {
        // Top-Down
        qreal layerY = 30.0;
        qreal maxRowW = 0.0;
        for (int r = 0; r <= maxRank; ++r) {
            const auto &group = rankGroups[r];
            qreal totalW = 0.0;
            for (const auto &id : group) totalW += nodes[id].w + 24.0;
            maxRowW = std::max(maxRowW, totalW);

            qreal curX = 30.0;
            for (const auto &id : group) {
                auto &n = nodes[id];
                n.x = curX;
                n.y = layerY;
                curX += n.w + 24.0;
            }
            layerY += 42.0 + 50.0;
        }
        totalWidth = std::max(360.0, maxRowW + 40.0);
        totalHeight = std::max(140.0, layerY);
    }

    QString svg;
    svg.append(QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"0 0 %1 %2\">\n"
        "  <defs>\n"
        "    <marker id=\"mermaid-arrow\" markerWidth=\"10\" markerHeight=\"10\" refX=\"9\" refY=\"3.5\" orient=\"auto\">\n"
        "      <polygon points=\"0 0, 10 3.5, 0 7\" fill=\"#475569\" />\n"
        "    </marker>\n"
        "    <filter id=\"card-shadow\" x=\"-6%\" y=\"-6%\" width=\"112%\" height=\"116%\" filterUnits=\"userSpaceOnUse\">\n"
        "      <feDropShadow dx=\"0\" dy=\"2\" stdDeviation=\"3\" flood-color=\"#0F172A\" flood-opacity=\"0.06\"/>\n"
        "    </filter>\n"
        "  </defs>\n").arg(totalWidth).arg(totalHeight));

    // Draw Edges
    for (const auto &e : edges) {
        if (!nodes.contains(e.from) || !nodes.contains(e.to)) continue;
        const auto &n1 = nodes[e.from];
        const auto &n2 = nodes[e.to];

        qreal x1, y1, x2, y2;
        if (isLR) {
            x1 = n1.x + n1.w;
            y1 = n1.y + n1.h / 2.0;
            x2 = n2.x - 2.0;
            y2 = n2.y + n2.h / 2.0;
        } else {
            x1 = n1.x + n1.w / 2.0;
            y1 = n1.y + n1.h;
            x2 = n2.x + n2.w / 2.0;
            y2 = n2.y - 2.0;
        }

        QString dash = e.isDashed ? QStringLiteral(" stroke-dasharray=\"4,3\"") : QString();
        qreal strokeWidth = e.isThick ? 2.6 : 1.8;

        // Path
        if (std::abs(y1 - y2) < 4.0 || std::abs(x1 - x2) < 4.0) {
            svg.append(QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%4\" stroke=\"#475569\" stroke-width=\"%5\"%6 marker-end=\"url(#mermaid-arrow)\" />\n")
                .arg(x1).arg(y1).arg(x2).arg(y2).arg(strokeWidth).arg(dash));
        } else {
            qreal midX = (x1 + x2) / 2.0;
            qreal midY = (y1 + y2) / 2.0;
            if (isLR) {
                svg.append(QStringLiteral("<path d=\"M %1 %2 C %3 %2, %3 %4, %5 %4\" fill=\"none\" stroke=\"#475569\" stroke-width=\"%6\"%7 marker-end=\"url(#mermaid-arrow)\" />\n")
                    .arg(x1).arg(y1).arg(midX).arg(y2).arg(x2).arg(strokeWidth).arg(dash));
            } else {
                svg.append(QStringLiteral("<path d=\"M %1 %2 C %1 %3, %4 %3, %4 %5\" fill=\"none\" stroke=\"#475569\" stroke-width=\"%6\"%7 marker-end=\"url(#mermaid-arrow)\" />\n")
                    .arg(x1).arg(y1).arg(midY).arg(x2).arg(y2).arg(strokeWidth).arg(dash));
            }
        }

        if (!e.label.isEmpty()) {
            qreal lx = (x1 + x2) / 2.0;
            qreal ly = (y1 + y2) / 2.0 - 4.0;
            svg.append(QStringLiteral("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"18\" rx=\"4\" fill=\"#FFFFFF\" fill-opacity=\"0.9\" />\n")
                .arg(lx - e.label.length() * 4.0).arg(ly - 13).arg(e.label.length() * 8.0 + 4.0));
            svg.append(QStringLiteral("<text x=\"%1\" y=\"%2\" font-family=\"'Segoe UI', -apple-system, sans-serif\" font-size=\"11\" fill=\"#475569\" text-anchor=\"middle\">%3</text>\n")
                .arg(lx).arg(ly).arg(escapeXml(e.label)));
        }
    }

    // Draw Nodes
    for (const auto &n : nodes) {
        if (n.shape == QStringLiteral("diamond")) {
            qreal cx = n.x + n.w / 2.0;
            qreal cy = n.y + n.h / 2.0;
            svg.append(QStringLiteral("<polygon points=\"%1 %2, %3 %4, %1 %5, %6 %4\" fill=\"#F8FAFC\" stroke=\"#0284C7\" stroke-width=\"1.8\" filter=\"url(#card-shadow)\" />\n")
                .arg(cx).arg(n.y).arg(n.x + n.w).arg(cy).arg(n.y + n.h).arg(n.x));
        } else if (n.shape == QStringLiteral("circle")) {
            qreal r = n.h / 2.0;
            qreal cx = n.x + n.w / 2.0;
            qreal cy = n.y + n.h / 2.0;
            svg.append(QStringLiteral("<circle cx=\"%1\" cy=\"%2\" r=\"%3\" fill=\"#F8FAFC\" stroke=\"#0D9488\" stroke-width=\"1.8\" filter=\"url(#card-shadow)\" />\n")
                .arg(cx).arg(cy).arg(r));
        } else {
            // rect or round
            qreal rx = (n.shape == QStringLiteral("round")) ? 10.0 : 4.0;
            svg.append(QStringLiteral("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" rx=\"%5\" fill=\"#FFFFFF\" stroke=\"#3B82F6\" stroke-width=\"1.8\" filter=\"url(#card-shadow)\" />\n")
                .arg(n.x).arg(n.y).arg(n.w).arg(n.h).arg(rx));
        }

        svg.append(QStringLiteral("<text x=\"%1\" y=\"%2\" font-family=\"'Segoe UI', -apple-system, sans-serif\" font-size=\"12\" font-weight=\"500\" fill=\"#0F172A\" text-anchor=\"middle\">%3</text>\n")
            .arg(n.x + n.w / 2.0).arg(n.y + n.h / 2.0 + 4.0).arg(escapeXml(n.label)));
    }

    svg.append(QStringLiteral("</svg>"));
    return svg.toUtf8();
}

} // namespace MarkdownTool::Render
