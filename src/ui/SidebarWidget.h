#pragma once

#include "OutlineWidget.h"

#include <QWidget>
#include <QStackedWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QPushButton>
#include <QLineEdit>
#include <QFrame>

namespace MarkdownTool::UI {

class SidebarWidget : public QWidget {
    Q_OBJECT

public:
    explicit SidebarWidget(QWidget *parent = nullptr);

    OutlineWidget *outlineWidget() { return m_outlineWidget; }
    void setWorkspacePath(const QString &path);

signals:
    void fileSelected(const QString &filePath);

private slots:
    void switchToOutline();
    void switchToFiles();
    void onSearchTextChanged(const QString &text);
    void updateThemeStyles();

private:
    QFrame *m_segmentedFrame = nullptr;
    QPushButton *m_outlineBtn = nullptr;
    QPushButton *m_filesBtn = nullptr;
    QLineEdit *m_searchEdit = nullptr;

    QStackedWidget *m_stackWidget = nullptr;
    OutlineWidget *m_outlineWidget = nullptr;
    QTreeView *m_fileTree = nullptr;
    QFileSystemModel *m_fileModel = nullptr;
};

} // namespace MarkdownTool::UI
