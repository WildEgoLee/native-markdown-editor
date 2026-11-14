#include "MermaidSvgRenderer.h"

#include <QStringList>
#include <QRegularExpression>
#include <QMap>
#include <QVector>
#include <QSet>
#include <QPair>
#include <cmath>
#include <algorithm>
#include <functional>

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

static QByteArray generateUnsupportedCard(const QString &diagramType, const QString &codeSnippet) {
    QString snippet = codeSnippet.left(60);
    if (codeSnippet.length() > 60) snippet.append(QStringLiteral("..."));

    QString svg = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"460\" height=\"120\" viewBox=\"0 0 460 120\">\n"
        "  <rect x=\"4\" y=\"4\" width=\"452\" height=\"112\" rx=\"8\" fill=\"#F8FAFC\" stroke=\"#CBD5E1\" stroke-width=\"1.5\" />\n"
        "  <rect x=\"20\" y=\"20\" width=\"86\" height=\"22\" rx=\"4\" fill=\"#FEF3C7\" stroke=\"#F59E0B\" stroke-width=\"1\" />\n"
        "  <text x=\"63\" y=\"35\" font-family=\"'Segoe UI', -apple-system, sans-serif\" font-size=\"11\" font-weight=\"600\" fill=\"#B45309\" text-anchor=\"middle\">Mermaid</text>\n"
        "  <text x=\"116\" y=\"35\" font-family=\"'Segoe UI', -apple-system, sans-serif\" font-size=\"12\" font-weight=\"600\" fill=\"#1E293B\">Notice: %1</text>\n"
        "  <text x=\"20\" y=\"64\" font-family=\"'Segoe UI', -apple-system, sans-serif\" font-size=\"11\" fill=\"#64748B\">Preview supports Flowchart (TD/LR) and Sequence Diagrams.</text>\n"
        "  <text x=\"20\" y=\"88\" font-family=\"Consolas, 'Courier New', monospace\" font-size=\"11\" fill=\"#334155\">%2</text>\n"
        "</svg>\n").arg(escapeXml(diagramType)).arg(escapeXml(snippet));

    return svg.toUtf8();
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
    QString firstLine;
    for (const auto &l : lines) {
        QString trimmed = l.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith(QStringLiteral("%%"))) {
            firstLine = trimmed;
            break;
        }
    }

    bool isGraph = firstLine.startsWith(QStringLiteral("graph")) || firstLine.startsWith(QStringLiteral("flowchart"));
    bool isSeq = firstLine.startsWith(QStringLiteral("sequenceDiagram"));
    if (!isGraph && !isSeq) {
        return QSize(460, 120);
    }

    int lineCount = static_cast<int>(lines.size());
    bool isLR = firstLine.contains(QStringLiteral("LR")) || firstLine.contains(QStringLiteral("lr"));
    if (isLR) {
        int w = std::clamp(lineCount * 110 + 220, 360, 1200);
        return QSize(w, 200);
    }
    int h = std::clamp(lineCount * 65 + 100, 160, 900);
    return QSize(std::clamp(lineCount * 30 + 360, 360, 1100), h);
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

    bool isFlowchart = firstLine.startsWith(QStringLiteral("graph")) || firstLine.startsWith(QStringLiteral("flowchart"));
    bool isSequence = firstLine.startsWith(QStringLiteral("sequenceDiagram"));

    // Graceful handling of unsupported diagram types
    if (!isFlowchart && !isSequence) {
        QString diagramType = firstLine.section(u' ', 0, 0);
        if (diagramType.isEmpty()) diagramType = QStringLiteral("Unknown");
        return generateUnsupportedCard(diagramType, mermaidCode.trimmed());
    }

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
        qreal contentWidth = pCount * colWidth + 80.0;
        qreal rowHeight = 48.0;
        qreal contentHeight = static_cast<qreal>(messages.size()) * rowHeight + 110.0;

        qreal displayWidth = std::clamp(contentWidth, 380.0, 1400.0);
        qreal displayHeight = displayWidth * (contentHeight / contentWidth);
        if (displayWidth == contentWidth) displayHeight = contentHeight;

        QString svg;
        svg.append(QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"0 0 %3 %4\">\n"
            "  <defs>\n"
            "    <marker id=\"seq-arrow\" markerWidth=\"8\" markerHeight=\"8\" refX=\"7\" refY=\"3.5\" orient=\"auto\">\n"
            "      <polygon points=\"0 0, 8 3.5, 0 7\" fill=\"#2563EB\" />\n"
            "    </marker>\n"
            "  </defs>\n")
            .arg(displayWidth).arg(displayHeight).arg(contentWidth).arg(contentHeight));

        QMap<QString, qreal> pX;
        for (int i = 0; i < pCount; ++i) {
            qreal x = 40.0 + i * colWidth + colWidth / 2.0;
            pX[participants[i]] = x;

            // Lifeline dashed line
            svg.append(QStringLiteral("<line x1=\"%1\" y1=\"50\" x2=\"%1\" y2=\"%2\" stroke=\"#94A3B8\" stroke-width=\"1.5\" stroke-dasharray=\"4,4\" />\n")
                .arg(x).arg(contentHeight - 20));

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
    bool isLR = firstLine.contains(QStringLiteral("LR")) || firstLine.contains(QStringLiteral("lr"));

    QMap<QString, NodeInfo> nodes;
    QVector<EdgeInfo> edges;

    static QRegularExpression reArrow(QStringLiteral(R"((-->|---|-.->|==>)(?:\|(.*?)\|)?)"));
    static QRegularExpression reNode(QStringLiteral(R"(^\s*([A-Za-z0-9_]+)(?:\[(.*?)\]|\((.*?)\)|\{(.*?)\}|\(\((.*?)\)\))?\s*$)"));

    auto extractNodeFromToken = [&](const QString &token) -> QString {
        auto m = reNode.match(token);
        if (!m.hasMatch()) return QString();
        QString id = m.captured(1);
        QString box = m.captured(2);
        QString round = m.captured(3);
        QString diam = m.captured(4);
        QString circ = m.captured(5);

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
        } else {
            auto &n = nodes[id];
            if (n.label == id) {
                if (!box.isEmpty()) { n.label = box; n.shape = QStringLiteral("rect"); }
                else if (!round.isEmpty()) { n.label = round; n.shape = QStringLiteral("round"); }
                else if (!diam.isEmpty()) { n.label = diam; n.shape = QStringLiteral("diamond"); }
                else if (!circ.isEmpty()) { n.label = circ; n.shape = QStringLiteral("circle"); }
                n.w = std::max(110.0, static_cast<qreal>(n.label.length()) * 9.5 + 24.0);
            }
        }
        return id;
    };

    for (const QString &raw : rawLines) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("graph")) || line.startsWith(QStringLiteral("flowchart")) || line.startsWith(QStringLiteral("%%"))) {
            continue;
        }

        // Split line by arrows to extract all node tokens and edge arrows
        auto arrowIt = reArrow.globalMatch(line);
        if (arrowIt.hasNext()) {
            struct ArrowMatch {
                int start;
                int end;
                QString arrow;
                QString label;
            };
            QVector<ArrowMatch> arrows;
            while (arrowIt.hasNext()) {
                auto am = arrowIt.next();
                arrows.append(ArrowMatch{
                    static_cast<int>(am.capturedStart(0)),
                    static_cast<int>(am.capturedEnd(0)),
                    am.captured(1),
                    am.captured(2)
                });
            }

            QStringList nodeTokens;
            int lastPos = 0;
            for (const auto &am : arrows) {
                nodeTokens.append(line.mid(lastPos, am.start - lastPos).trimmed());
                lastPos = am.end;
            }
            nodeTokens.append(line.mid(lastPos).trimmed());

            QStringList parsedIds;
            for (const auto &tok : nodeTokens) {
                parsedIds.append(extractNodeFromToken(tok));
            }

            for (int i = 0; i < arrows.size() && i + 1 < parsedIds.size(); ++i) {
                QString from = parsedIds[i];
                QString to = parsedIds[i + 1];
                if (!from.isEmpty() && !to.isEmpty()) {
                    edges.append(EdgeInfo{
                        from,
                        to,
                        arrows[i].label,
                        arrows[i].arrow.contains(QStringLiteral("-.")),
                        arrows[i].arrow.contains(QStringLiteral("=="))
                    });
                }
            }
            continue;
        }

        // Single node declaration
        extractNodeFromToken(line);
    }

    if (nodes.isEmpty()) {
        NodeInfo d;
        d.id = QStringLiteral("A");
        d.label = QStringLiteral("Diagram");
        d.shape = QStringLiteral("round");
        nodes[d.id] = d;
    }

    // Safety check on pathological diagram sizes
    if (nodes.size() > 150 || edges.size() > 300) {
        return generateUnsupportedCard(QStringLiteral("Resource Limit"), QStringLiteral("Diagram exceeds 150 nodes / 300 edges limit"));
    }

    // 1. Cycle detection using DFS back-edge detection
    QMap<QString, QStringList> adj;
    for (const auto &e : edges) {
        if (e.from != e.to && nodes.contains(e.from) && nodes.contains(e.to)) {
            adj[e.from].append(e.to);
        }
    }

    QMap<QString, int> visitState; // 0 = unvisited, 1 = visiting (in stack), 2 = visited
    QSet<QPair<QString, QString>> backEdges;

    std::function<void(const QString&)> dfs = [&](const QString &u) {
        visitState[u] = 1;
        for (const QString &v : adj.value(u)) {
            if (visitState.value(v, 0) == 1) {
                backEdges.insert(qMakePair(u, v));
            } else if (visitState.value(v, 0) == 0) {
                dfs(v);
            }
        }
        visitState[u] = 2;
    };

    for (const QString &k : nodes.keys()) {
        if (visitState.value(k, 0) == 0) {
            dfs(k);
        }
    }

    // 2. DAG Layering (ignoring back-edges and self-loops)
    QMap<QString, int> ranks;
    for (const auto &k : nodes.keys()) ranks[k] = 0;

    int maxPasses = std::min(50, static_cast<int>(nodes.size()));
    for (int pass = 0; pass < maxPasses; ++pass) {
        bool changed = false;
        for (const auto &e : edges) {
            if (e.from == e.to || backEdges.contains(qMakePair(e.from, e.to))) {
                continue;
            }
            if (ranks.contains(e.from) && ranks.contains(e.to)) {
                if (ranks[e.from] + 1 > ranks[e.to]) {
                    ranks[e.to] = ranks[e.from] + 1;
                    changed = true;
                }
            }
        }
        if (!changed) break;
    }

    // 3. Compact rank groups
    QMap<int, QStringList> rankGroups;
    for (auto it = ranks.begin(); it != ranks.end(); ++it) {
        rankGroups[it.value()].append(it.key());
    }

    QList<int> sortedRanks = rankGroups.keys();
    std::sort(sortedRanks.begin(), sortedRanks.end());

    if (isLR) {
        qreal layerX = 30.0;
        for (int r : sortedRanks) {
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
            layerX += maxW + 65.0;
        }
    } else {
        // Top-Down
        qreal layerY = 30.0;
        for (int r : sortedRanks) {
            const auto &group = rankGroups[r];
            qreal curX = 30.0;
            for (const auto &id : group) {
                auto &n = nodes[id];
                n.x = curX;
                n.y = layerY;
                curX += n.w + 24.0;
            }
            layerY += 42.0 + 50.0;
        }
    }

    // 4. Calculate actual bounding box across all nodes and edges to avoid any clipping
    qreal minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;

    for (const auto &n : nodes) {
        minX = std::min(minX, n.x);
        maxX = std::max(maxX, n.x + n.w);
        minY = std::min(minY, n.y);
        maxY = std::max(maxY, n.y + n.h);
    }

    // Calculate edge extent in bounding box
    for (const auto &e : edges) {
        if (!nodes.contains(e.from) || !nodes.contains(e.to)) continue;
        const auto &n1 = nodes[e.from];
        const auto &n2 = nodes[e.to];

        if (e.from == e.to) {
            // Self loop curves upwards by 30px
            minY = std::min(minY, n1.y - 34.0);
            continue;
        }

        bool isBack = backEdges.contains(qMakePair(e.from, e.to)) || (ranks.value(e.to, 0) <= ranks.value(e.from, 0));
        if (isBack) {
            if (isLR) {
                qreal arcY = std::max(n1.y + n1.h, n2.y + n2.h) + 32.0;
                maxY = std::max(maxY, arcY + 8.0);
            } else {
                qreal arcX = std::max(n1.x + n1.w, n2.x + n2.w) + 35.0;
                maxX = std::max(maxX, arcX + 8.0);
            }
        }
    }

    // Ensure all elements have safe positive margins by shifting coordinates if needed
    const qreal margin = 24.0;
    qreal shiftX = (minX < margin) ? (margin - minX) : 0.0;
    qreal shiftY = (minY < margin) ? (margin - minY) : 0.0;

    for (auto &n : nodes) {
        n.x += shiftX;
        n.y += shiftY;
    }

    qreal contentWidth = std::max(360.0, (maxX + shiftX) + margin);
    qreal contentHeight = std::max(140.0, (maxY + shiftY) + margin);

    qreal displayWidth = std::clamp(contentWidth, 360.0, 1400.0);
    qreal displayHeight = displayWidth * (contentHeight / contentWidth);
    if (displayWidth == contentWidth) displayHeight = contentHeight;

    QString elementsXml;

    // Draw Edges (with special handling for self-loops and back-edges)
    for (const auto &e : edges) {
        if (!nodes.contains(e.from) || !nodes.contains(e.to)) continue;
        const auto &n1 = nodes[e.from];
        const auto &n2 = nodes[e.to];

        QString dash = e.isDashed ? QStringLiteral(" stroke-dasharray=\"4,3\"") : QString();
        qreal strokeWidth = e.isThick ? 2.6 : 1.8;

        // 1. Self-loop
        if (e.from == e.to) {
            qreal sx = n1.x + n1.w * 0.75;
            qreal sy = n1.y;
            qreal tx = n1.x + n1.w * 0.25;
            qreal ty = n1.y;
            elementsXml.append(QStringLiteral("<path d=\"M %1 %2 C %1 %3, %4 %3, %4 %5\" fill=\"none\" stroke=\"#475569\" stroke-width=\"%6\"%7 marker-end=\"url(#mermaid-arrow)\" />\n")
                .arg(sx).arg(sy).arg(sy - 30).arg(tx).arg(ty - 2).arg(strokeWidth).arg(dash));
            continue;
        }

        bool isBack = backEdges.contains(qMakePair(e.from, e.to)) || (ranks.value(e.to, 0) <= ranks.value(e.from, 0));

        // 2. Back-edge or cycle feedback edge
        if (isBack) {
            if (isLR) {
                qreal sx = n1.x + n1.w / 2.0;
                qreal sy = n1.y + n1.h;
                qreal tx = n2.x + n2.w / 2.0;
                qreal ty = n2.y + n2.h;
                qreal arcY = std::max(sy, ty) + 32.0;
                elementsXml.append(QStringLiteral("<path d=\"M %1 %2 C %1 %3, %4 %3, %4 %5\" fill=\"none\" stroke=\"#475569\" stroke-width=\"%6\"%7 marker-end=\"url(#mermaid-arrow)\" />\n")
                    .arg(sx).arg(sy).arg(arcY).arg(tx).arg(ty + 2).arg(strokeWidth).arg(dash));
            } else {
                qreal sx = n1.x + n1.w;
                qreal sy = n1.y + n1.h / 2.0;
                qreal tx = n2.x + n2.w;
                qreal ty = n2.y + n2.h / 2.0;
                qreal arcX = std::max(sx, tx) + 35.0;
                elementsXml.append(QStringLiteral("<path d=\"M %1 %2 C %3 %2, %3 %4, %5 %4\" fill=\"none\" stroke=\"#475569\" stroke-width=\"%6\"%7 marker-end=\"url(#mermaid-arrow)\" />\n")
                    .arg(sx).arg(sy).arg(arcX).arg(ty).arg(tx + 2).arg(strokeWidth).arg(dash));
            }
            continue;
        }

        // 3. Normal Forward edge
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

        if (std::abs(y1 - y2) < 4.0 || std::abs(x1 - x2) < 4.0) {
            elementsXml.append(QStringLiteral("<line x1=\"%1\" y1=\"%2\" x2=\"%3\" y2=\"%4\" stroke=\"#475569\" stroke-width=\"%5\"%6 marker-end=\"url(#mermaid-arrow)\" />\n")
                .arg(x1).arg(y1).arg(x2).arg(y2).arg(strokeWidth).arg(dash));
        } else {
            qreal midX = (x1 + x2) / 2.0;
            qreal midY = (y1 + y2) / 2.0;
            if (isLR) {
                elementsXml.append(QStringLiteral("<path d=\"M %1 %2 C %3 %2, %3 %4, %5 %4\" fill=\"none\" stroke=\"#475569\" stroke-width=\"%6\"%7 marker-end=\"url(#mermaid-arrow)\" />\n")
                    .arg(x1).arg(y1).arg(midX).arg(y2).arg(x2).arg(strokeWidth).arg(dash));
            } else {
                elementsXml.append(QStringLiteral("<path d=\"M %1 %2 C %1 %3, %4 %3, %4 %5\" fill=\"none\" stroke=\"#475569\" stroke-width=\"%6\"%7 marker-end=\"url(#mermaid-arrow)\" />\n")
                    .arg(x1).arg(y1).arg(midY).arg(x2).arg(y2).arg(strokeWidth).arg(dash));
            }
        }

        if (!e.label.isEmpty()) {
            qreal lx = (x1 + x2) / 2.0;
            qreal ly = (y1 + y2) / 2.0 - 4.0;
            elementsXml.append(QStringLiteral("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"18\" rx=\"4\" fill=\"#FFFFFF\" fill-opacity=\"0.9\" />\n")
                .arg(lx - e.label.length() * 4.0).arg(ly - 13).arg(e.label.length() * 8.0 + 4.0));
            elementsXml.append(QStringLiteral("<text x=\"%1\" y=\"%2\" font-family=\"'Segoe UI', -apple-system, sans-serif\" font-size=\"11\" fill=\"#475569\" text-anchor=\"middle\">%3</text>\n")
                .arg(lx).arg(ly).arg(escapeXml(e.label)));
        }
    }

    // Draw Nodes
    for (const auto &n : nodes) {
        if (n.shape == QStringLiteral("diamond")) {
            qreal cx = n.x + n.w / 2.0;
            qreal cy = n.y + n.h / 2.0;
            elementsXml.append(QStringLiteral("<polygon points=\"%1 %2, %3 %4, %1 %5, %6 %4\" fill=\"#F8FAFC\" stroke=\"#0284C7\" stroke-width=\"1.8\" filter=\"url(#card-shadow)\" />\n")
                .arg(cx).arg(n.y).arg(n.x + n.w).arg(cy).arg(n.y + n.h).arg(n.x));
        } else if (n.shape == QStringLiteral("circle")) {
            qreal r = n.h / 2.0;
            qreal cx = n.x + n.w / 2.0;
            qreal cy = n.y + n.h / 2.0;
            elementsXml.append(QStringLiteral("<circle cx=\"%1\" cy=\"%2\" r=\"%3\" fill=\"#F8FAFC\" stroke=\"#0D9488\" stroke-width=\"1.8\" filter=\"url(#card-shadow)\" />\n")
                .arg(cx).arg(cy).arg(r));
        } else {
            // rect or round
            qreal rx = (n.shape == QStringLiteral("round")) ? 10.0 : 4.0;
            elementsXml.append(QStringLiteral("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" rx=\"%5\" fill=\"#FFFFFF\" stroke=\"#3B82F6\" stroke-width=\"1.8\" filter=\"url(#card-shadow)\" />\n")
                .arg(n.x).arg(n.y).arg(n.w).arg(n.h).arg(rx));
        }

        elementsXml.append(QStringLiteral("<text x=\"%1\" y=\"%2\" font-family=\"'Segoe UI', -apple-system, sans-serif\" font-size=\"12\" font-weight=\"500\" fill=\"#0F172A\" text-anchor=\"middle\">%3</text>\n")
            .arg(n.x + n.w / 2.0).arg(n.y + n.h / 2.0 + 4.0).arg(escapeXml(n.label)));
    }

    QString svg = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" viewBox=\"0 0 %3 %4\">\n"
        "  <defs>\n"
        "    <marker id=\"mermaid-arrow\" markerWidth=\"10\" markerHeight=\"10\" refX=\"9\" refY=\"3.5\" orient=\"auto\">\n"
        "      <polygon points=\"0 0, 10 3.5, 0 7\" fill=\"#475569\" />\n"
        "    </marker>\n"
        "    <filter id=\"card-shadow\" x=\"-6%\" y=\"-6%\" width=\"112%\" height=\"116%\" filterUnits=\"userSpaceOnUse\">\n"
        "      <feDropShadow dx=\"0\" dy=\"2\" stdDeviation=\"3\" flood-color=\"#0F172A\" flood-opacity=\"0.06\"/>\n"
        "    </filter>\n"
        "  </defs>\n"
        "%5\n"
        "</svg>\n")
        .arg(displayWidth).arg(displayHeight).arg(contentWidth).arg(contentHeight).arg(elementsXml);

    return svg.toUtf8();
}

} // namespace MarkdownTool::Render
