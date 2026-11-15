#include "OutlineWidget.h"
#include "theme/Theme.h"

#include <QVBoxLayout>
#include <QHeaderView>

namespace MarkdownTool::UI {

OutlineWidget::OutlineWidget(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(0);

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderHidden(true);
    m_treeWidget->setIndentation(16);
    m_treeWidget->setRootIsDecorated(true);
    m_treeWidget->setAnimated(true);
    m_treeWidget->setUniformRowHeights(true);
    m_layout->addWidget(m_treeWidget);

    m_emptyStateLabel = new QLabel(QStringLiteral("暂无大纲内容\n在文档中使用 # 标题 建立大纲"), this);
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    m_emptyStateLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; padding: 32px 16px; line-height: 1.5;")
        .arg(Theme::currentPalette().secondaryText.name()));
    m_emptyStateLabel->hide();
    m_layout->addWidget(m_emptyStateLabel);

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

    if (items.isEmpty()) {
        m_treeWidget->hide();
        m_emptyStateLabel->show();
        return;
    }

    m_emptyStateLabel->hide();
    m_treeWidget->show();

    // Stack tracking ancestors at each level 1..6
    QTreeWidgetItem *ancestors[7] = {nullptr};

    for (const auto &item : items) {
        int lvl = qBound(1, item.level, 6);

        // Find nearest ancestor with level < current level
        QTreeWidgetItem *parent = nullptr;
        for (int l = lvl - 1; l >= 1; --l) {
            if (ancestors[l]) {
                parent = ancestors[l];
                break;
            }
        }

        QTreeWidgetItem *treeItem = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_treeWidget);
        treeItem->setText(0, item.title);
        treeItem->setToolTip(0, item.title);
        treeItem->setData(0, Qt::UserRole, item.sourceOffset);

        // Hierarchy styling via natural typography
        QFont itemFont = m_treeWidget->font();
        if (lvl == 1) {
            itemFont.setWeight(QFont::Bold);
            itemFont.setPointSize(10);
        } else if (lvl == 2) {
            itemFont.setWeight(QFont::DemiBold);
            itemFont.setPointSize(9.5);
        } else {
            itemFont.setWeight(QFont::Normal);
            itemFont.setPointSize(9);
        }
        treeItem->setFont(0, itemFont);

        ancestors[lvl] = treeItem;
        for (int l = lvl + 1; l <= 6; ++l) {
            ancestors[l] = nullptr;
        }
    }

    m_treeWidget->expandAll();
}

void OutlineWidget::filter(const QString &pattern) {
    QString pat = pattern.trimmed();
    if (pat.isEmpty()) {
        for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
            QTreeWidgetItem *top = m_treeWidget->topLevelItem(i);
            top->setHidden(false);
            filterItemRecursive(top, QString());
        }
        return;
    }

    for (int i = 0; i < m_treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_treeWidget->topLevelItem(i);
        filterItemRecursive(top, pat);
    }
}

bool OutlineWidget::filterItemRecursive(QTreeWidgetItem *item, const QString &pattern) {
    if (pattern.isEmpty()) {
        item->setHidden(false);
        for (int i = 0; i < item->childCount(); ++i) {
            filterItemRecursive(item->child(i), pattern);
        }
        return true;
    }

    bool selfMatch = item->text(0).contains(pattern, Qt::CaseInsensitive);
    bool childMatch = false;

    for (int i = 0; i < item->childCount(); ++i) {
        if (filterItemRecursive(item->child(i), pattern)) {
            childMatch = true;
        }
    }

    bool visible = selfMatch || childMatch;
    item->setHidden(!visible);
    if (visible && childMatch) {
        item->setExpanded(true);
    }
    return visible;
}

} // namespace MarkdownTool::UI
