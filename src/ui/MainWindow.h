#pragma once

#include "editor/NativeEditorView.h"
#include "file/DocumentSession.h"
#include "SidebarWidget.h"

#include <QMainWindow>
#include <QLabel>
#include <QSplitter>
#include <QTimer>

namespace MarkdownTool::UI {

class FindReplaceBar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString &filePath);
    bool saveFile();
    bool saveFileAs();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newFile();
    void openFileDialog();
    void openFolderDialog();
    void updateTitle();
    void updateStatusBar(int line, int col, int offset);
    void onCleanStateChanged(bool isClean);
    void onRecoveryTimer();

private:
    void setupUi();
    void setupActions();
    void setupMenusAndToolbars();
    bool maybeSave();

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
};

} // namespace MarkdownTool::UI
