#pragma once

#include "parser/SemanticNode.h"

#include <QWidget>
#include <QTreeWidget>

namespace MarkdownTool::UI {

struct OutlineItem {
    int level = 1;
    QString title;
    int sourceOffset = 0;
};

class OutlineWidget : public QWidget {
    Q_OBJECT

public:
    explicit OutlineWidget(QWidget *parent = nullptr);

    void updateOutline(const Parser::SemanticNodePtr &rootNode, const QString &fullText);

signals:
    void headingClicked(int sourceOffset);

private:
    void extractHeadings(const Parser::SemanticNodePtr &node, const QString &fullText, QVector<OutlineItem> &items);

    QTreeWidget *m_treeWidget = nullptr;
};

} // namespace MarkdownTool::UI
