#include "OutlineWidget.h"

#include <QVBoxLayout>
#include <QHeaderView>

namespace MarkdownTool::UI {

OutlineWidget::OutlineWidget(QWidget *parent)
    : QWidget(parent)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderHidden(true);
    m_treeWidget->setIndentation(16);
    layout->addWidget(m_treeWidget);

    connect(m_treeWidget, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        int offset = item->data(0, Qt::UserRole).toInt();
        emit headingClicked(offset);
    });
}

void OutlineWidget::extractHeadings(const Parser::SemanticNodePtr &node, const QString &fullText, QVector<OutlineItem> &items) {
    if (!node) {
        return;
    }

    if (node->type == Parser::NodeType::Heading) {
        OutlineItem item;
        item.level = node->headingLevel;
        item.sourceOffset = node->fullRange.start;

        if (node->contentRange.isValid()) {
            item.title = fullText.mid(node->contentRange.start, node->contentRange.length()).trimmed();
        } else {
            item.title = fullText.mid(node->fullRange.start, node->fullRange.length()).trimmed();
        }
        if (item.title.isEmpty()) {
            item.title = QStringLiteral("(未命名标题)");
        }
        items.append(item);
    }

    for (const auto &child : node->children) {
        extractHeadings(child, fullText, items);
    }
}

void OutlineWidget::updateOutline(const Parser::SemanticNodePtr &rootNode, const QString &fullText) {
    m_treeWidget->clear();
    QVector<OutlineItem> items;
    extractHeadings(rootNode, fullText, items);

    for (const auto &item : items) {
        auto treeItem = new QTreeWidgetItem(m_treeWidget);
        QString indent(qMax(0, item.level - 1) * 2, u' ');
        treeItem->setText(0, indent + QStringLiteral("H%1  %2").arg(item.level).arg(item.title));
        treeItem->setData(0, Qt::UserRole, item.sourceOffset);
    }

    m_treeWidget->expandAll();
}

} // namespace MarkdownTool::UI
