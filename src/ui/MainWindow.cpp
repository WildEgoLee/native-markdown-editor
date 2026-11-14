#include "MainWindow.h"
#include "FindReplaceBar.h"
#include "file/FileService.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QVBoxLayout>

namespace MarkdownTool::UI {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupActions();
    setupMenusAndToolbars();

    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setInterval(5000);
    connect(m_recoveryTimer, &QTimer::timeout, this, &MainWindow::onRecoveryTimer);
    m_recoveryTimer->start();

    updateTitle();
    resize(1100, 750);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_sidebar = new SidebarWidget(this);

    auto editorContainer = new QWidget(this);
    auto editorLayout = new QVBoxLayout(editorContainer);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);

    m_findBar = new FindReplaceBar(editorContainer);
    m_findBar->hide();

    m_editorView = new Editor::NativeEditorView(editorContainer);

    editorLayout->addWidget(m_findBar);
    editorLayout->addWidget(m_editorView, 1);

    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(editorContainer);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 4);
    m_splitter->setSizes({240, 860});

    setCentralWidget(m_splitter);

    // Find and Replace wiring
    connect(m_findBar, &FindReplaceBar::findNext, this, [this](const QString &t, bool mc) {
        m_editorView->findText(t, mc, true);
    });
    connect(m_findBar, &FindReplaceBar::findPrev, this, [this](const QString &t, bool mc) {
        m_editorView->findText(t, mc, false);
    });
    connect(m_findBar, &FindReplaceBar::replaceOne, this, [this](const QString &f, const QString &r, bool mc) {
        m_editorView->replaceOne(f, r, mc);
    });
    connect(m_findBar, &FindReplaceBar::replaceAll, this, [this](const QString &f, const QString &r, bool mc) {
        m_editorView->replaceAll(f, r, mc);
    });
    connect(m_findBar, &FindReplaceBar::closed, this, [this]() {
        m_editorView->setFocus();
    });

    // Outline synchronization
    connect(m_editorView, &Editor::NativeEditorView::textChanged, this, [this]() {
        Parser::MarkdownParserAdapter parser;
        auto root = parser.parse(m_editorView->buffer());
        m_sidebar->outlineWidget()->updateOutline(root, m_editorView->buffer().text());
    });

    connect(m_sidebar->outlineWidget(), &OutlineWidget::headingClicked, this, [this](int offset) {
        m_editorView->setCursorPos(offset);
        m_editorView->setFocus();
    });

    connect(m_sidebar, &SidebarWidget::fileSelected, this, &MainWindow::openFile);

    // Status bar setup
    QStatusBar *sb = statusBar();
    m_statusDirtyLabel = new QLabel(QStringLiteral("已保存"), this);
    m_statusPosLabel = new QLabel(QStringLiteral("第 1 行, 第 1 列"), this);
    m_statusStatsLabel = new QLabel(QStringLiteral("0 字符"), this);
    m_statusEncodingLabel = new QLabel(QStringLiteral("UTF-8 | LF"), this);
    m_statusModeLabel = new QLabel(QStringLiteral("即时预览"), this);

    sb->addPermanentWidget(m_statusDirtyLabel);
    sb->addPermanentWidget(m_statusPosLabel);
    sb->addPermanentWidget(m_statusStatsLabel);
    sb->addPermanentWidget(m_statusEncodingLabel);
    sb->addPermanentWidget(m_statusModeLabel);

    connect(m_editorView, &Editor::NativeEditorView::cursorMoved, this, &MainWindow::updateStatusBar);
    connect(&m_editorView->undoStack(), &Core::UndoStack::cleanStateChanged, this, &MainWindow::onCleanStateChanged);
    connect(m_editorView, &Editor::NativeEditorView::modeChanged, this, [this](bool isSource) {
        m_statusModeLabel->setText(isSource ? QStringLiteral("源码模式") : QStringLiteral("即时预览"));
    });

    // Populate initial outline
    Parser::MarkdownParserAdapter parser;
    auto root = parser.parse(m_editorView->buffer());
    m_sidebar->outlineWidget()->updateOutline(root, m_editorView->buffer().text());
}

void MainWindow::setupActions() {
    // Actions are directly wired in menus & toolbar
}

void MainWindow::setupMenusAndToolbars() {
    QMenuBar *mb = menuBar();

    // File Menu
    QMenu *fileMenu = mb->addMenu(QStringLiteral("文件 (&F)"));
    fileMenu->addAction(QStringLiteral("新建 (&N)"), QKeySequence::New, this, &MainWindow::newFile);
    fileMenu->addAction(QStringLiteral("打开文件 (&O)..."), QKeySequence::Open, this, &MainWindow::openFileDialog);
    fileMenu->addAction(QStringLiteral("打开工作区文件夹..."), this, &MainWindow::openFolderDialog);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("保存 (&S)"), QKeySequence::Save, this, &MainWindow::saveFile);
    fileMenu->addAction(QStringLiteral("另存为 (&A)..."), QKeySequence::SaveAs, this, &MainWindow::saveFileAs);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("退出 (&X)"), QKeySequence::Quit, this, &MainWindow::close);

    // Edit Menu
    QMenu *editMenu = mb->addMenu(QStringLiteral("编辑 (&E)"));
    editMenu->addAction(QStringLiteral("撤销 (&U)"), QKeySequence::Undo, m_editorView, &Editor::NativeEditorView::undo);
    editMenu->addAction(QStringLiteral("重做 (&R)"), QKeySequence::Redo, m_editorView, &Editor::NativeEditorView::redo);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("剪切 (&T)"), QKeySequence::Cut, m_editorView, &Editor::NativeEditorView::cut);
    editMenu->addAction(QStringLiteral("复制 (&C)"), QKeySequence::Copy, m_editorView, &Editor::NativeEditorView::copy);
    editMenu->addAction(QStringLiteral("粘贴 (&P)"), QKeySequence::Paste, m_editorView, &Editor::NativeEditorView::paste);
    editMenu->addAction(QStringLiteral("全选 (&A)"), QKeySequence::SelectAll, m_editorView, &Editor::NativeEditorView::selectAll);
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("查找 (&F)..."), QKeySequence::Find, m_findBar, &FindReplaceBar::openFind);
    editMenu->addAction(QStringLiteral("替换 (&H)..."), QKeySequence(QStringLiteral("Ctrl+H")), m_findBar, &FindReplaceBar::openReplace);
    editMenu->addAction(QStringLiteral("查找下一个"), QKeySequence(QKeySequence::FindNext), [this]() {
        // Find next shortcut F3
        emit m_findBar->findNext(QString(), false);
    });
    editMenu->addAction(QStringLiteral("查找上一个"), QKeySequence(QKeySequence::FindPrevious), [this]() {
        emit m_findBar->findPrev(QString(), false);
    });

    // Format Menu
    QMenu *formatMenu = mb->addMenu(QStringLiteral("段落与格式 (&M)"));
    formatMenu->addAction(QStringLiteral("加粗 (**粗体**)"), QKeySequence::Bold, m_editorView, &Editor::NativeEditorView::toggleBold);
    formatMenu->addAction(QStringLiteral("斜体 (*斜体*)"), QKeySequence::Italic, m_editorView, &Editor::NativeEditorView::toggleItalic);
    formatMenu->addAction(QStringLiteral("删除线 (~~文字~~)"), m_editorView, &Editor::NativeEditorView::toggleStrikethrough);
    formatMenu->addAction(QStringLiteral("行内代码 (`code`)"), m_editorView, &Editor::NativeEditorView::toggleCodeSpan);
    formatMenu->addAction(QStringLiteral("超链接 [文字](url)"), m_editorView, &Editor::NativeEditorView::insertLink);
    formatMenu->addAction(QStringLiteral("切换任务列表 (- [ ])"), m_editorView, &Editor::NativeEditorView::toggleTaskList);
    formatMenu->addSeparator();
    for (int lvl = 1; lvl <= 6; ++lvl) {
        formatMenu->addAction(QStringLiteral("标题 %1 (H%2)").arg(lvl).arg(lvl), [this, lvl]() {
            m_editorView->insertHeading(lvl);
        });
    }

    // View Menu
    QMenu *viewMenu = mb->addMenu(QStringLiteral("视图 (&V)"));
    QAction *toggleModeAct = viewMenu->addAction(QStringLiteral("切换源码模式 (Ctrl+/)"), [this]() {
        m_editorView->setSourceMode(!m_editorView->isSourceMode());
    });
    toggleModeAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+/")));

    QAction *toggleSidebarAct = viewMenu->addAction(QStringLiteral("显示/隐藏侧边栏 (Ctrl+\\)"), [this]() {
        m_sidebar->setVisible(!m_sidebar->isVisible());
    });
    toggleSidebarAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+\\")));

    // Toolbar
    QToolBar *tb = addToolBar(QStringLiteral("主工具栏"));
    tb->setMovable(false);

    QAction *tbSidebar = tb->addAction(QStringLiteral("📁 侧边栏"));
    connect(tbSidebar, &QAction::triggered, [this]() {
        m_sidebar->setVisible(!m_sidebar->isVisible());
    });
    tb->addSeparator();

    QAction *tbBold = tb->addAction(QStringLiteral("加粗"));
    connect(tbBold, &QAction::triggered, m_editorView, &Editor::NativeEditorView::toggleBold);

    QAction *tbItalic = tb->addAction(QStringLiteral("斜体"));
    connect(tbItalic, &QAction::triggered, m_editorView, &Editor::NativeEditorView::toggleItalic);

    QAction *tbStrike = tb->addAction(QStringLiteral("删除线"));
    connect(tbStrike, &QAction::triggered, m_editorView, &Editor::NativeEditorView::toggleStrikethrough);

    QAction *tbCode = tb->addAction(QStringLiteral("代码"));
    connect(tbCode, &QAction::triggered, m_editorView, &Editor::NativeEditorView::toggleCodeSpan);

    QAction *tbLink = tb->addAction(QStringLiteral("链接"));
    connect(tbLink, &QAction::triggered, m_editorView, &Editor::NativeEditorView::insertLink);

    QAction *tbTask = tb->addAction(QStringLiteral("任务框"));
    connect(tbTask, &QAction::triggered, m_editorView, &Editor::NativeEditorView::toggleTaskList);

    tb->addSeparator();

    auto comboHeading = new QComboBox(this);
    comboHeading->addItems({QStringLiteral("段落"),
                            QStringLiteral("标题 1"),
                            QStringLiteral("标题 2"),
                            QStringLiteral("标题 3"),
                            QStringLiteral("标题 4")});
    connect(comboHeading, &QComboBox::activated, this, [this](int index) {
        m_editorView->insertHeading(index);
    });
    tb->addWidget(comboHeading);

    tb->addSeparator();

    QAction *tbMode = tb->addAction(QStringLiteral("源码/预览切换"));
    connect(tbMode, &QAction::triggered, [this]() {
        m_editorView->setSourceMode(!m_editorView->isSourceMode());
    });
}

void MainWindow::updateTitle() {
    QString title = m_session.displayName();
    if (m_editorView->undoStack().isDirty()) {
        title.prepend(QStringLiteral("* "));
    }
    title.append(QStringLiteral(" - Windows 原生 Markdown 编辑器"));
    setWindowTitle(title);
}

void MainWindow::updateStatusBar(int line, int col, int) {
    m_statusPosLabel->setText(QStringLiteral("第 %1 行, 第 %2 列").arg(line).arg(col));
    int charCount = m_editorView->buffer().length();
    m_statusStatsLabel->setText(QStringLiteral("%1 字符").arg(charCount));

    QString encStr = (m_editorView->buffer().encoding() == Core::DocumentEncoding::Utf8Bom) ? QStringLiteral("UTF-8 BOM") : QStringLiteral("UTF-8");
    QString leStr = (m_editorView->buffer().lineEnding() == Core::LineEnding::Crlf) ? QStringLiteral("CRLF") : QStringLiteral("LF");
    m_statusEncodingLabel->setText(encStr + QStringLiteral(" | ") + leStr);
}

void MainWindow::onCleanStateChanged(bool isClean) {
    m_statusDirtyLabel->setText(isClean ? QStringLiteral("已保存") : QStringLiteral("未保存 *"));
    updateTitle();
}

void MainWindow::onRecoveryTimer() {
    if (!m_session.isUntitled() && m_editorView->undoStack().isDirty()) {
        File::FileService::writeRecoverySnapshot(m_session.filePath(), m_editorView->buffer());
    }
}

void MainWindow::newFile() {
    if (!maybeSave()) {
        return;
    }
    m_session.resetToUntitled(m_editorView->buffer());
    m_editorView->undoStack().clear();
    m_editorView->undoStack().setSaveCheckpoint();
    updateTitle();
}

void MainWindow::openFileDialog() {
    QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 Markdown 文档"), QString(), QStringLiteral("Markdown Files (*.md *.markdown *.txt);;All Files (*.*)"));
    if (!path.isEmpty()) {
        openFile(path);
    }
}

void MainWindow::openFolderDialog() {
    QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("打开工作区文件夹"));
    if (!dir.isEmpty()) {
        m_sidebar->setWorkspacePath(dir);
        m_sidebar->setVisible(true);
    }
}

bool MainWindow::openFile(const QString &filePath) {
    if (!maybeSave()) {
        return false;
    }

    // Check for crash recovery snapshot
    QString recPath = File::FileService::findRecoverySnapshot(filePath);
    if (!recPath.isEmpty()) {
        auto btn = QMessageBox::question(this,
                                         QStringLiteral("发现未保存恢复副本"),
                                         QStringLiteral("检测到该文档有未保存的崩溃恢复快照，是否从恢复副本加载？"),
                                         QMessageBox::Yes | QMessageBox::No);
        if (btn == QMessageBox::Yes) {
            QString err;
            if (File::FileService::loadFile(recPath, m_editorView->buffer(), err)) {
                QString loadErr;
                m_session.load(filePath, m_editorView->buffer(), loadErr);
                m_editorView->undoStack().clear();
                // Treat recovered as dirty so user can re-save
                updateTitle();
                return true;
            }
        }
    }

    QString err;
    if (!m_session.load(filePath, m_editorView->buffer(), err)) {
        QMessageBox::critical(this, QStringLiteral("打开文件失败"), QStringLiteral("无法加载文件: ") + err);
        return false;
    }

    m_editorView->undoStack().clear();
    m_editorView->undoStack().setSaveCheckpoint();
    updateTitle();
    return true;
}

bool MainWindow::saveFile() {
    if (m_session.isUntitled()) {
        return saveFileAs();
    }

    auto res = m_session.save(m_editorView->buffer());
    if (!res.success) {
        if (res.hadConflict) {
            auto btn = QMessageBox::warning(this,
                                            QStringLiteral("外部修改冲突"),
                                            QStringLiteral("该文件已在外部被其他程序修改！\n是否强制覆盖外部修改？"),
                                            QMessageBox::Yes | QMessageBox::No);
            if (btn == QMessageBox::Yes) {
                // Force overwrite
                auto forceRes = m_session.save(m_editorView->buffer(), true);
                if (forceRes.success) {
                    m_editorView->undoStack().setSaveCheckpoint();
                    updateTitle();
                    return true;
                } else {
                    QMessageBox::critical(this, QStringLiteral("保存失败"), forceRes.errorMessage);
                    return false;
                }
            }
            return false;
        } else {
            QMessageBox::critical(this, QStringLiteral("保存失败"), res.errorMessage);
            return false;
        }
    }

    m_editorView->undoStack().setSaveCheckpoint();
    updateTitle();
    return true;
}

bool MainWindow::saveFileAs() {
    QString defaultPath = m_session.isUntitled() ? QStringLiteral("未命名.md") : m_session.filePath();
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("另存为"), defaultPath, QStringLiteral("Markdown Files (*.md);;All Files (*.*)"));
    if (path.isEmpty()) {
        return false;
    }

    // Transactional save as
    auto res = m_session.saveAs(path, m_editorView->buffer());
    if (!res.success) {
        QMessageBox::critical(this, QStringLiteral("另存为失败"), res.errorMessage);
        return false;
    }

    m_editorView->undoStack().setSaveCheckpoint();
    updateTitle();
    return true;
}

bool MainWindow::maybeSave() {
    if (!m_editorView->undoStack().isDirty()) {
        return true;
    }

    auto ret = QMessageBox::warning(this,
                                    QStringLiteral("未保存更改"),
                                    QStringLiteral("文档已被修改，是否立即保存？"),
                                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (ret == QMessageBox::Save) {
        return saveFile();
    } else if (ret == QMessageBox::Cancel) {
        return false;
    }
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (maybeSave()) {
        if (!m_session.isUntitled()) {
            File::FileService::removeRecoverySnapshot(m_session.filePath());
        }
        event->accept();
    } else {
        event->ignore();
    }
}

} // namespace MarkdownTool::UI
