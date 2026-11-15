#pragma once

#include "parser/SemanticNode.h"

#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QVBoxLayout>

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
    void filter(const QString &pattern);

    QTreeWidget *treeWidget() const { return m_treeWidget; }

signals:
    void headingClicked(int sourceOffset);

private:
    void extractHeadings(const Parser::SemanticNodePtr &node, const QString &fullText, QVector<OutlineItem> &items);
    bool filterItemRecursive(QTreeWidgetItem *item, const QString &pattern);

    QTreeWidget *m_treeWidget = nullptr;
    QLabel *m_emptyStateLabel = nullptr;
    QVBoxLayout *m_layout = nullptr;
};

} // namespace MarkdownTool::UI
