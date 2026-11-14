#pragma once

#include "OutlineWidget.h"

#include <QWidget>
#include <QTabWidget>
#include <QTreeView>
#include <QFileSystemModel>

namespace MarkdownTool::UI {

class SidebarWidget : public QWidget {
    Q_OBJECT

public:
    explicit SidebarWidget(QWidget *parent = nullptr);

    OutlineWidget *outlineWidget() { return m_outlineWidget; }
    void setWorkspacePath(const QString &path);

signals:
    void fileSelected(const QString &filePath);

private:
    QTabWidget *m_tabWidget = nullptr;
    OutlineWidget *m_outlineWidget = nullptr;
    QTreeView *m_fileTree = nullptr;
    QFileSystemModel *m_fileModel = nullptr;
};

} // namespace MarkdownTool::UI
