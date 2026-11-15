#pragma once

#include "OutlineWidget.h"

#include <QWidget>
#include <QStackedWidget>
#include <QTreeView>
#include <QTreeWidget>
#include <QFileSystemModel>
#include <QSortFilterProxyModel>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QFrame>
#include <QStringList>

namespace MarkdownTool::UI {

class SidebarWidget : public QWidget {
    Q_OBJECT

public:
    explicit SidebarWidget(QWidget *parent = nullptr);

    OutlineWidget *outlineWidget() { return m_outlineWidget; }
    void setWorkspacePath(const QString &path);

    QTreeView *fileTree() const { return m_fileTree; }
    QFileSystemModel *fileModel() const { return m_fileModel; }
    QSortFilterProxyModel *fileProxyModel() const { return m_fileProxyModel; }
    QLineEdit *searchEdit() const { return m_searchEdit; }
    QString fileFilterPattern() const;

    void setRecentFiles(const QStringList &files);
    const QStringList &recentFiles() const { return m_recentFiles; }
    QTreeWidget *recentTree() const { return m_recentTree; }
    QPushButton *recentButton() const { return m_recentBtn; }

    QSize minimumSizeHint() const override { return QSize(160, 100); }
    QSize sizeHint() const override { return QSize(260, 600); }

signals:
    void fileSelected(const QString &filePath);
    void removeRecentRequested(const QString &filePath);
    void clearRecentRequested();

public slots:
    void switchToOutline();
    void switchToFiles();
    void switchToRecent();
    void onSearchTextChanged(const QString &text);
    void updateThemeStyles();
    void refreshRecentItems();

private slots:
    void onRecentContextMenu(const QPoint &pos);

private:
    void filterRecentItems(const QString &text);

    QFrame *m_segmentedFrame = nullptr;
    QPushButton *m_outlineBtn = nullptr;
    QPushButton *m_filesBtn = nullptr;
    QPushButton *m_recentBtn = nullptr;
    QLineEdit *m_searchEdit = nullptr;

    QStackedWidget *m_stackWidget = nullptr;
    OutlineWidget *m_outlineWidget = nullptr;
    QTreeView *m_fileTree = nullptr;
    QFileSystemModel *m_fileModel = nullptr;
    QSortFilterProxyModel *m_fileProxyModel = nullptr;

    QWidget *m_recentPage = nullptr;
    QTreeWidget *m_recentTree = nullptr;
    QLabel *m_emptyRecentLabel = nullptr;
    QStringList m_recentFiles;
};

} // namespace MarkdownTool::UI
