#pragma once

#include "editor/NativeEditorView.h"
#include "file/DocumentSession.h"
#include "SidebarWidget.h"

#include <QMainWindow>
#include <QLabel>
#include <QSplitter>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTabWidget>
#include <memory>
#include <vector>

class QSettings;
class QTabBar;

namespace MarkdownTool::UI {

class FindReplaceBar;

class DocumentTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit DocumentTabWidget(QWidget *parent = nullptr) : QTabWidget(parent) {}
    QTabBar *tabBar() const { return QTabWidget::tabBar(); }
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    struct TabInfo {
        Editor::NativeEditorView *view = nullptr;
        File::DocumentSession session;
        int untitledNumber = 0;
        QByteArray suppressedExternalChangeHash;
        bool hasPendingExternalConflict = false;
    };

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString &filePath);
    bool openFile(const QString &filePath, bool interactive);
    bool saveFile(bool interactive = true, bool forceOverwrite = false);
    bool saveFileAs(const QString &targetPath = QString(), bool interactive = true);
    bool reloadCurrentFile();
    void handleExternalFileModification(bool interactive = true);
    void handleAllExternalFileModifications(bool interactive = true);
    bool checkUntitledRecoverySnapshot(bool interactive = true);

    // Multi-Document Tab Management
    DocumentTabWidget *tabWidget() const { return m_tabWidget; }
    int tabCount() const { return static_cast<int>(m_tabs.size()); }
    int currentTabIndex() const;
    TabInfo *currentTabInfo() const;
    TabInfo *tabInfoAt(int index) const;
    int createTab(const QString &filePath = QString(), bool makeCurrent = true, bool interactive = true);
    bool closeTab(int index, bool interactive = true);
    bool closeCurrentTab(bool interactive = true);
    bool closeAllTabs(bool interactive = true);
    void switchToTab(int index);
    void nextTab();
    void prevTab();
    bool saveTab(int index, bool interactive = true, bool forceOverwrite = false);
    bool saveTabAs(int index, const QString &targetPath = QString(), bool interactive = true);
    bool reloadTab(int index);

    void loadPreferences(QSettings *settings = nullptr);
    void savePreferences(QSettings *settings = nullptr);

    Editor::NativeEditorView *editorView() const { return m_editorView; }
    SidebarWidget *sidebar() const { return m_sidebar; }
    FindReplaceBar *findBar() const { return m_findBar; }
    File::DocumentSession &session();
    const File::DocumentSession &session() const;
    QFileSystemWatcher *fileWatcher() const { return m_fileWatcher; }
    const QStringList &recentFiles() const { return m_recentFiles; }
    QMenu *recentFilesMenu() const { return m_recentMenu; }
    QAction *actWidthAdaptive() const { return m_actWidthAdaptive; }
    QAction *actWidthNarrow() const { return m_actWidthNarrow; }
    QAction *actWidthStandard() const { return m_actWidthStandard; }
    QAction *actWidthWide() const { return m_actWidthWide; }

public slots:
    void newFile();
    void openFileDialog();
    void openFolderDialog();
    void addRecentFile(const QString &filePath);
    void removeRecentFile(const QString &filePath);
    void clearRecentFiles();
    void updateRecentFilesMenu();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void updateTitle();
    void updateStatusBar(int line, int col, int offset);
    void onCleanStateChanged(bool isClean);
    void onRecoveryTimer();
    void onExternalFileChanged(const QString &path);
    void onCurrentTabChanged(int index);
    void onTabMoved(int from, int to);

private:
    void setupUi();
    void setupActions();
    void setupMenusAndToolbars();
    bool maybeSave();
    void updateFileWatcher();
    void updateFileWatcher(const QString &newPath);

    int findTabIndexByPath(const QString &path) const;
    void updateTabTitle(int index);
    void setupEditorViewConnections(Editor::NativeEditorView *view);
    bool loadFileIntoTab(int index, const QString &filePath, bool interactive);

    SidebarWidget *m_sidebar = nullptr;
    Editor::NativeEditorView *m_editorView = nullptr;
    FindReplaceBar *m_findBar = nullptr;
    QSplitter *m_splitter = nullptr;
    DocumentTabWidget *m_tabWidget = nullptr;
    std::vector<std::unique_ptr<TabInfo>> m_tabs;
    int m_nextUntitledNumber = 1;

    QLabel *m_statusPosLabel = nullptr;
    QLabel *m_statusStatsLabel = nullptr;
    QLabel *m_statusEncodingLabel = nullptr;
    QLabel *m_statusModeLabel = nullptr;
    QLabel *m_statusDirtyLabel = nullptr;

    mutable File::DocumentSession m_fallbackSession;
    QTimer *m_recoveryTimer = nullptr;
    QTimer *m_outlineDebounceTimer = nullptr;
    QFileSystemWatcher *m_fileWatcher = nullptr;
    QTimer *m_externalChangeDebounceTimer = nullptr;
    bool m_isSelfSaving = false;
    QByteArray m_suppressedExternalChangeHash;

    QAction *m_tbSidebar = nullptr;
    QAction *m_tbNew = nullptr;
    QAction *m_tbOpen = nullptr;
    QAction *m_tbSave = nullptr;
    QAction *m_tbUndo = nullptr;
    QAction *m_tbRedo = nullptr;
    QAction *m_tbFind = nullptr;
    QAction *m_tbFont = nullptr;
    QAction *m_tbTheme = nullptr;
    QAction *m_tbMode = nullptr;

    void updateThemeStyles();
    void showFontDialog();
    void updateFindMatches();
    void updateTypographyMenu();

    QAction *m_actSans = nullptr;
    QAction *m_actSerif = nullptr;
    QAction *m_actMono = nullptr;

    QAction *m_actWidthAdaptive = nullptr;
    QAction *m_actWidthNarrow = nullptr;
    QAction *m_actWidthStandard = nullptr;
    QAction *m_actWidthWide = nullptr;

    QMenu *m_recentMenu = nullptr;
    QStringList m_recentFiles;
    int m_targetSidebarWidth = 260;
    bool m_initialSplitterSet = false;
};

} // namespace MarkdownTool::UI
