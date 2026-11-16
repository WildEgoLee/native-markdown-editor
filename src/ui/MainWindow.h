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

class QSettings;

namespace MarkdownTool::UI {

class FindReplaceBar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString &filePath);
    bool openFile(const QString &filePath, bool interactive);
    bool saveFile(bool interactive = true, bool forceOverwrite = false);
    bool saveFileAs(bool interactive = true);
    bool reloadCurrentFile();
    void handleExternalFileModification(bool interactive = true);
    bool checkUntitledRecoverySnapshot(bool interactive = true);

    void loadPreferences(QSettings *settings = nullptr);
    void savePreferences(QSettings *settings = nullptr);

    Editor::NativeEditorView *editorView() const { return m_editorView; }
    SidebarWidget *sidebar() const { return m_sidebar; }
    FindReplaceBar *findBar() const { return m_findBar; }
    File::DocumentSession &session() { return m_session; }
    const File::DocumentSession &session() const { return m_session; }
    QFileSystemWatcher *fileWatcher() const { return m_fileWatcher; }
    const QStringList &recentFiles() const { return m_recentFiles; }
    QMenu *recentFilesMenu() const { return m_recentMenu; }
    QAction *actWidthAdaptive() const { return m_actWidthAdaptive; }
    QAction *actWidthNarrow() const { return m_actWidthNarrow; }
    QAction *actWidthStandard() const { return m_actWidthStandard; }
    QAction *actWidthWide() const { return m_actWidthWide; }

public slots:
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
    void newFile();
    void openFileDialog();
    void openFolderDialog();
    void updateTitle();
    void updateStatusBar(int line, int col, int offset);
    void onCleanStateChanged(bool isClean);
    void onRecoveryTimer();
    void onExternalFileChanged(const QString &path);

private:
    void setupUi();
    void setupActions();
    void setupMenusAndToolbars();
    bool maybeSave();
    void updateFileWatcher(const QString &newPath);

    SidebarWidget *m_sidebar = nullptr;
    Editor::NativeEditorView *m_editorView = nullptr;
    FindReplaceBar *m_findBar = nullptr;
    QSplitter *m_splitter = nullptr;

    QLabel *m_statusPosLabel = nullptr;
    QLabel *m_statusStatsLabel = nullptr;
    QLabel *m_statusEncodingLabel = nullptr;
    QLabel *m_statusModeLabel = nullptr;
    QLabel *m_statusDirtyLabel = nullptr;

    File::DocumentSession m_session;
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
