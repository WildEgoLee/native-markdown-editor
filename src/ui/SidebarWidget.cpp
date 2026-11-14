#include "SidebarWidget.h"

#include <QVBoxLayout>
#include <QHeaderView>

namespace MarkdownTool::UI {

SidebarWidget::SidebarWidget(QWidget *parent)
    : QWidget(parent)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new QTabWidget(this);

    // Outline Tab
    m_outlineWidget = new OutlineWidget(this);
    m_tabWidget->addTab(m_outlineWidget, QStringLiteral("大纲"));

    // Files Tab
    m_fileTree = new QTreeView(this);
    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setNameFilters({QStringLiteral("*.md"), QStringLiteral("*.markdown"), QStringLiteral("*.txt")});
    m_fileModel->setNameFilterDisables(false);
    m_fileTree->setModel(m_fileModel);
    m_fileTree->header()->hideSection(1);
    m_fileTree->header()->hideSection(2);
    m_fileTree->header()->hideSection(3);
    m_tabWidget->addTab(m_fileTree, QStringLiteral("文件"));

    layout->addWidget(m_tabWidget);

    connect(m_fileTree, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        QString filePath = m_fileModel->filePath(index);
        if (QFileInfo(filePath).isFile()) {
            emit fileSelected(filePath);
        }
    });
}

void SidebarWidget::setWorkspacePath(const QString &path) {
    m_fileModel->setRootPath(path);
    m_fileTree->setRootIndex(m_fileModel->index(path));
}

} // namespace MarkdownTool::UI
