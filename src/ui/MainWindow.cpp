#include "MainWindow.h"
#include "FindReplaceBar.h"
#include "file/FileService.h"
#include "theme/Theme.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QFontDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QToolButton>
#include <QActionGroup>
#include <QSettings>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>

namespace MarkdownTool::UI {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    resize(1100, 750);
    setupUi();
    setupActions();
    setupMenusAndToolbars();

    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setInterval(5000);
    connect(m_recoveryTimer, &QTimer::timeout, this, &MainWindow::onRecoveryTimer);
    m_recoveryTimer->start();

    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::onExternalFileChanged);

    m_externalChangeDebounceTimer = new QTimer(this);
    m_externalChangeDebounceTimer->setSingleShot(true);
    m_externalChangeDebounceTimer->setInterval(200);
    connect(m_externalChangeDebounceTimer, &QTimer::timeout, this, [this]() {
        handleExternalFileModification(true);
    });

    updateTitle();
    updateThemeStyles();
    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this, &MainWindow::updateThemeStyles);

    m_splitter->setSizes({260, 840});
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
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 840});

    setCentralWidget(m_splitter);

    // Find and Replace wiring
    connect(m_findBar, &FindReplaceBar::findNext, this, [this](const QString &t, bool mc) {
        m_editorView->findText(t, mc, true);
        updateFindMatches();
    });
    connect(m_findBar, &FindReplaceBar::findPrev, this, [this](const QString &t, bool mc) {
        m_editorView->findText(t, mc, false);
        updateFindMatches();
    });
    connect(m_findBar, &FindReplaceBar::replaceOne, this, [this](const QString &f, const QString &r, bool mc) {
        m_editorView->replaceOne(f, r, mc);
        updateFindMatches();
    });
    connect(m_findBar, &FindReplaceBar::replaceAll, this, [this](const QString &f, const QString &r, bool mc) {
        m_editorView->replaceAll(f, r, mc);
        updateFindMatches();
    });
    connect(m_findBar, &FindReplaceBar::findParamsChanged, this, [this](const QString &, bool) {
        updateFindMatches();
    });
    connect(m_findBar, &FindReplaceBar::closed, this, [this]() {
        m_editorView->setFocus();
    });

    // Outline synchronization (debounced & reusing editor AST - zero duplicate parsing!)
    m_outlineDebounceTimer = new QTimer(this);
    m_outlineDebounceTimer->setSingleShot(true);
    m_outlineDebounceTimer->setInterval(200);
    connect(m_outlineDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_editorView && m_sidebar && m_sidebar->outlineWidget()) {
            m_sidebar->outlineWidget()->updateOutline(m_editorView->rootNode(), m_editorView->buffer().text());
        }
    });

    connect(m_editorView, &Editor::NativeEditorView::textChanged, this, [this]() {
        m_outlineDebounceTimer->start();
        if (m_findBar && m_findBar->isVisible()) {
            updateFindMatches();
        }
    });

    connect(m_sidebar->outlineWidget(), &OutlineWidget::headingClicked, this, [this](int offset) {
        m_editorView->setCursorPos(offset);
        m_editorView->setFocus();
    });

    connect(m_sidebar, &SidebarWidget::fileSelected, this, [this](const QString &path) {
        openFile(path);
    });
    connect(m_sidebar, &SidebarWidget::removeRecentRequested, this, &MainWindow::removeRecentFile);
    connect(m_sidebar, &SidebarWidget::clearRecentRequested, this, &MainWindow::clearRecentFiles);

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

    setAcceptDrops(true);

    connect(m_editorView, &Editor::NativeEditorView::cursorMoved, this, &MainWindow::updateStatusBar);
    connect(m_editorView, &Editor::NativeEditorView::cursorMoved, this, [this](int, int, int) {
        if (m_findBar && m_findBar->isVisible()) {
            updateFindMatches();
        }
    });
    connect(&m_editorView->undoStack(), &Core::UndoStack::cleanStateChanged, this, &MainWindow::onCleanStateChanged);
    connect(m_editorView, &Editor::NativeEditorView::modeChanged, this, [this](bool isSource) {
        m_statusModeLabel->setText(isSource ? QStringLiteral("源码模式") : QStringLiteral("即时预览"));
    });
    connect(m_editorView, &Editor::NativeEditorView::fileOpenRequested, this, [this](const QString &path) {
        openFile(path);
    });
    connect(m_editorView, &Editor::NativeEditorView::folderOpenRequested, this, [this](const QString &dir) {
        m_sidebar->setWorkspacePath(dir);
        m_sidebar->setVisible(true);
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
    m_recentMenu = fileMenu->addMenu(Theme::ThemeManager::instance().icon(QStringLiteral("history"), 16),
                                     QStringLiteral("最近打开 (&R)"));
    m_recentMenu->setToolTipsVisible(true);
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::updateRecentFilesMenu);
    updateRecentFilesMenu();
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("保存 (&S)"), QKeySequence::Save, this, [this]() { saveFile(); });
    fileMenu->addAction(QStringLiteral("另存为 (&A)..."), QKeySequence::SaveAs, this, [this]() { saveFileAs(); });
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
    editMenu->addAction(QStringLiteral("查找 (&F)..."), QKeySequence::Find, [this]() {
        m_findBar->openFind();
        updateFindMatches();
    });
    editMenu->addAction(QStringLiteral("替换 (&H)..."), QKeySequence(QStringLiteral("Ctrl+H")), [this]() {
        m_findBar->openReplace();
        updateFindMatches();
    });
    editMenu->addAction(QStringLiteral("查找下一个"), QKeySequence(QKeySequence::FindNext), [this]() {
        // Find next shortcut F3
        const QString text = m_findBar->findText();
        if (!text.isEmpty()) {
            m_editorView->findText(text, m_findBar->matchCase(), true);
            updateFindMatches();
        }
    });
    editMenu->addAction(QStringLiteral("查找上一个"), QKeySequence(QKeySequence::FindPrevious), [this]() {
        const QString text = m_findBar->findText();
        if (!text.isEmpty()) {
            m_editorView->findText(text, m_findBar->matchCase(), false);
            updateFindMatches();
        }
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
        if (m_tbMode) {
            m_tbMode->setIcon(Theme::ThemeManager::instance().icon(m_editorView->isSourceMode() ? QStringLiteral("source") : QStringLiteral("mode"), 18));
        }
    });
    toggleModeAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+/")));

    QAction *toggleSidebarAct = viewMenu->addAction(QStringLiteral("显示/隐藏侧边栏 (Ctrl+\\)"), [this]() {
        m_sidebar->setVisible(!m_sidebar->isVisible());
    });
    toggleSidebarAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+\\")));

    viewMenu->addSeparator();
    QAction *toggleThemeAct = viewMenu->addAction(QStringLiteral("切换浅色/深色主题 (Ctrl+T)"), []() {
        Theme::ThemeManager::instance().toggleMode();
    });
    toggleThemeAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));

    viewMenu->addSeparator();

    // Reading Width Submenu
    QMenu *widthMenu = viewMenu->addMenu(QStringLiteral("阅读宽度 (&W)"));
    auto *widthGroup = new QActionGroup(this);

    m_actWidthAdaptive = widthMenu->addAction(QStringLiteral("自适应 (跟随字号，默认)"));
    m_actWidthAdaptive->setCheckable(true);
    widthGroup->addAction(m_actWidthAdaptive);
    connect(m_actWidthAdaptive, &QAction::triggered, []() {
        Theme::ThemeManager::instance().setReadingWidthMode(Theme::ThemeManager::ReadingWidthMode::Adaptive);
    });

    m_actWidthNarrow = widthMenu->addAction(QStringLiteral("窄 (640px)"));
    m_actWidthNarrow->setCheckable(true);
    widthGroup->addAction(m_actWidthNarrow);
    connect(m_actWidthNarrow, &QAction::triggered, []() {
        Theme::ThemeManager::instance().setReadingWidthMode(Theme::ThemeManager::ReadingWidthMode::Narrow);
    });

    m_actWidthStandard = widthMenu->addAction(QStringLiteral("标准 (780px)"));
    m_actWidthStandard->setCheckable(true);
    widthGroup->addAction(m_actWidthStandard);
    connect(m_actWidthStandard, &QAction::triggered, []() {
        Theme::ThemeManager::instance().setReadingWidthMode(Theme::ThemeManager::ReadingWidthMode::Standard);
    });

    m_actWidthWide = widthMenu->addAction(QStringLiteral("宽 (1040px)"));
    m_actWidthWide->setCheckable(true);
    widthGroup->addAction(m_actWidthWide);
    connect(m_actWidthWide, &QAction::triggered, []() {
        Theme::ThemeManager::instance().setReadingWidthMode(Theme::ThemeManager::ReadingWidthMode::Wide);
    });

    // Font / Typography Menu
    QMenu *fontMenu = mb->addMenu(QStringLiteral("字体 (&T)"));
    auto *fontGroup = new QActionGroup(this);

    m_actSans = fontMenu->addAction(QStringLiteral("现代无衬线 (微软雅黑 / YaHei UI)"));
    m_actSans->setCheckable(true);
    fontGroup->addAction(m_actSans);
    connect(m_actSans, &QAction::triggered, []() {
        Theme::ThemeManager::instance().setFontPreset(Theme::ThemeManager::FontPreset::SansSerif);
    });

    m_actSerif = fontMenu->addAction(QStringLiteral("典雅书卷体 (宋体 / Noto Serif)"));
    m_actSerif->setCheckable(true);
    fontGroup->addAction(m_actSerif);
    connect(m_actSerif, &QAction::triggered, []() {
        Theme::ThemeManager::instance().setFontPreset(Theme::ThemeManager::FontPreset::Serif);
    });

    m_actMono = fontMenu->addAction(QStringLiteral("极客等宽体 (Cascadia Code / Consolas)"));
    m_actMono->setCheckable(true);
    fontGroup->addAction(m_actMono);
    connect(m_actMono, &QAction::triggered, []() {
        Theme::ThemeManager::instance().setFontPreset(Theme::ThemeManager::FontPreset::Monospace);
    });

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::typographyChanged, this, &MainWindow::updateTypographyMenu);
    updateTypographyMenu();

    fontMenu->addSeparator();

    auto *actCustom = fontMenu->addAction(QStringLiteral("选择自定义系统字体..."));
    actCustom->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    connect(actCustom, &QAction::triggered, this, &MainWindow::showFontDialog);

    fontMenu->addSeparator();

    auto *actZoomIn = fontMenu->addAction(QStringLiteral("放大字号 (Ctrl+=)"));
    actZoomIn->setShortcuts({QKeySequence(QStringLiteral("Ctrl+=")), QKeySequence(QStringLiteral("Ctrl++"))});
    connect(actZoomIn, &QAction::triggered, []() {
        Theme::ThemeManager::instance().zoomIn();
    });

    auto *actZoomOut = fontMenu->addAction(QStringLiteral("缩小字号 (Ctrl+-)"));
    actZoomOut->setShortcut(QKeySequence(QStringLiteral("Ctrl+-")));
    connect(actZoomOut, &QAction::triggered, []() {
        Theme::ThemeManager::instance().zoomOut();
    });

    auto *actZoomReset = fontMenu->addAction(QStringLiteral("重置默认字号 (Ctrl+0)"));
    actZoomReset->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(actZoomReset, &QAction::triggered, []() {
        Theme::ThemeManager::instance().resetZoom();
    });

    // Sleek Modern Toolbar
    QToolBar *tb = addToolBar(QStringLiteral("主工具栏"));
    tb->setMovable(false);
    tb->setIconSize(QSize(18, 18));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_tbSidebar = tb->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("sidebar"), 18), QStringLiteral("侧边栏 (Ctrl+\\)"));
    m_tbSidebar->setToolTip(QStringLiteral("展开/折叠侧边栏 (Ctrl+\\)"));
    connect(m_tbSidebar, &QAction::triggered, [this]() {
        m_sidebar->setVisible(!m_sidebar->isVisible());
    });

    tb->addSeparator();

    m_tbNew = tb->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("new"), 18), QStringLiteral("新建 (Ctrl+N)"));
    m_tbNew->setToolTip(QStringLiteral("新建文档 (Ctrl+N)"));
    connect(m_tbNew, &QAction::triggered, this, &MainWindow::newFile);

    m_tbOpen = tb->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("open"), 18), QStringLiteral("打开 (Ctrl+O)"));
    m_tbOpen->setToolTip(QStringLiteral("打开文档 (Ctrl+O)"));
    connect(m_tbOpen, &QAction::triggered, this, &MainWindow::openFileDialog);

    m_tbSave = tb->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("save"), 18), QStringLiteral("保存 (Ctrl+S)"));
    m_tbSave->setToolTip(QStringLiteral("保存文档 (Ctrl+S)"));
    connect(m_tbSave, &QAction::triggered, this, [this]() { saveFile(); });

    tb->addSeparator();

    m_tbUndo = tb->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("undo"), 18), QStringLiteral("撤销 (Ctrl+Z)"));
    m_tbUndo->setToolTip(QStringLiteral("撤销上一操作 (Ctrl+Z)"));
    connect(m_tbUndo, &QAction::triggered, m_editorView, &Editor::NativeEditorView::undo);

    m_tbRedo = tb->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("redo"), 18), QStringLiteral("重做 (Ctrl+Y)"));
    m_tbRedo->setToolTip(QStringLiteral("重做操作 (Ctrl+Y)"));
    connect(m_tbRedo, &QAction::triggered, m_editorView, &Editor::NativeEditorView::redo);

    tb->addSeparator();

    m_tbFind = tb->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("find"), 18), QStringLiteral("查找与替换 (Ctrl+F)"));
    m_tbFind->setToolTip(QStringLiteral("查找与替换 (Ctrl+F)"));
    connect(m_tbFind, &QAction::triggered, [this]() {
        m_findBar->openFind();
        updateFindMatches();
    });

    // Spacer to right-align theme & mode actions
    auto spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    m_tbFont = tb->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("font"), 18), QStringLiteral("更换字体与排版"));
    m_tbFont->setToolTip(QStringLiteral("更换字体与排版 (Ctrl+Shift+F)"));
    auto fontToolBtn = qobject_cast<QToolButton*>(tb->widgetForAction(m_tbFont));
    if (fontToolBtn) {
        fontToolBtn->setPopupMode(QToolButton::InstantPopup);
        fontToolBtn->setMenu(fontMenu);
    } else {
        connect(m_tbFont, &QAction::triggered, this, &MainWindow::showFontDialog);
    }

    m_tbTheme = tb->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("theme"), 18), QStringLiteral("切换浅色/深色主题 (Ctrl+T)"));
    m_tbTheme->setToolTip(QStringLiteral("切换浅色 / 深色主题 (Ctrl+T)"));
    connect(m_tbTheme, &QAction::triggered, []() {
        Theme::ThemeManager::instance().toggleMode();
    });

    m_tbMode = tb->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("mode"), 18), QStringLiteral("切换即时预览 / 源码模式 (Ctrl+/)"));
    m_tbMode->setToolTip(QStringLiteral("切换即时预览 / 源码模式 (Ctrl+/)"));
    connect(m_tbMode, &QAction::triggered, [this]() {
        m_editorView->setSourceMode(!m_editorView->isSourceMode());
        m_tbMode->setIcon(Theme::ThemeManager::instance().icon(m_editorView->isSourceMode() ? QStringLiteral("source") : QStringLiteral("mode"), 18));
    });
}

void MainWindow::updateTitle() {
    QString title = m_session.displayName();
    if (m_editorView->undoStack().isDirty()) {
        title.prepend(QStringLiteral("● "));
    }
    setWindowTitle(title);
}

void MainWindow::updateStatusBar(int line, int col, int) {
    m_statusPosLabel->setText(QStringLiteral("第 %1 行, 第 %2 列").arg(line).arg(col));
    int charCount = m_editorView->buffer().length();
    m_statusStatsLabel->setText(QStringLiteral("%1 字符").arg(charCount));

    QString encStr = (m_editorView->buffer().encoding() == Core::DocumentEncoding::Utf8Bom) ? QStringLiteral("UTF-8 BOM") : QStringLiteral("UTF-8");
    QString leStr = (m_editorView->buffer().lineEnding() == Core::LineEnding::Crlf) ? QStringLiteral("CRLF") : QStringLiteral("LF");
    m_statusEncodingLabel->setText(encStr + QStringLiteral(" · ") + leStr);
}

void MainWindow::onCleanStateChanged(bool isClean) {
    m_statusDirtyLabel->setText(isClean ? QStringLiteral("● 已保存") : QStringLiteral("○ 未保存"));
    updateTitle();
}

void MainWindow::updateThemeStyles() {
    setStyleSheet(Theme::ThemeManager::instance().mainStylesheet());

    if (m_tbSidebar) m_tbSidebar->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("sidebar"), 18));
    if (m_tbNew) m_tbNew->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("new"), 18));
    if (m_tbOpen) m_tbOpen->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("open"), 18));
    if (m_tbSave) m_tbSave->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("save"), 18));
    if (m_tbUndo) m_tbUndo->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("undo"), 18));
    if (m_tbRedo) m_tbRedo->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("redo"), 18));
    if (m_tbFind) m_tbFind->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("find"), 18));
    if (m_tbFont) m_tbFont->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("font"), 18));
    if (m_tbTheme) m_tbTheme->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("theme"), 18));
    if (m_tbMode) {
        bool isSource = m_editorView ? m_editorView->isSourceMode() : false;
        m_tbMode->setIcon(Theme::ThemeManager::instance().icon(isSource ? QStringLiteral("source") : QStringLiteral("mode"), 18));
    }
    if (m_recentMenu) {
        m_recentMenu->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("history"), 16));
        updateRecentFilesMenu();
    }
    updateTitle();
}

void MainWindow::showFontDialog() {
    QFont currentFont(Theme::ThemeManager::instance().proseFontFamilies().first(),
                      static_cast<int>(Theme::ThemeManager::instance().basePointSize()));
    QFontDialog dlg(currentFont, this);
    dlg.setWindowTitle(QStringLiteral("选择正文字体与字号"));
    dlg.setOption(QFontDialog::DontUseNativeDialog, true);
    dlg.setStyleSheet(Theme::ThemeManager::instance().mainStylesheet());
    if (dlg.exec() == QDialog::Accepted) {
        QFont selectedFont = dlg.selectedFont();
        Theme::ThemeManager::instance().setCustomFontFamily(selectedFont.family());
        int pt = selectedFont.pointSize();
        if (pt <= 0 && selectedFont.pixelSize() > 0) {
            pt = static_cast<int>(selectedFont.pixelSize() * 72.0 / dlg.logicalDpiY());
        }
        if (pt > 0) {
            Theme::ThemeManager::instance().setBasePointSize(pt);
        }
    }
}

void MainWindow::addRecentFile(const QString &filePath) {
    if (filePath.isEmpty()) return;
    QFileInfo fi(filePath);
    QString canonical = fi.canonicalFilePath();
    if (canonical.isEmpty()) {
        canonical = fi.absoluteFilePath();
    }
    canonical = QDir::toNativeSeparators(canonical);

    m_recentFiles.removeAll(canonical);
    m_recentFiles.prepend(canonical);
    while (m_recentFiles.size() > 15) {
        m_recentFiles.removeLast();
    }

    updateRecentFilesMenu();
    if (m_sidebar) {
        m_sidebar->setRecentFiles(m_recentFiles);
    }

    QSettings s;
    s.setValue(QStringLiteral("history/recentFiles"), m_recentFiles);
}

void MainWindow::removeRecentFile(const QString &filePath) {
    QString native = QDir::toNativeSeparators(filePath);
    m_recentFiles.removeAll(filePath);
    m_recentFiles.removeAll(native);

    updateRecentFilesMenu();
    if (m_sidebar) {
        m_sidebar->setRecentFiles(m_recentFiles);
    }

    QSettings s;
    s.setValue(QStringLiteral("history/recentFiles"), m_recentFiles);
}

void MainWindow::clearRecentFiles() {
    m_recentFiles.clear();

    updateRecentFilesMenu();
    if (m_sidebar) {
        m_sidebar->setRecentFiles(m_recentFiles);
    }

    QSettings s;
    s.remove(QStringLiteral("history/recentFiles"));
}

void MainWindow::updateRecentFilesMenu() {
    if (!m_recentMenu) return;
    m_recentMenu->clear();

    if (m_recentFiles.isEmpty()) {
        auto *emptyAct = m_recentMenu->addAction(QStringLiteral("(无最近打开的文件)"));
        emptyAct->setEnabled(false);
        return;
    }

    int count = 0;
    for (const QString &path : m_recentFiles) {
        if (count >= 10) break;
        QFileInfo fi(path);
        QString text;
        if (count < 9) {
            text = QStringLiteral("&%1 %2").arg(count + 1).arg(fi.fileName());
        } else if (count == 9) {
            text = QStringLiteral("1&0 %1").arg(fi.fileName());
        } else {
            text = fi.fileName();
        }

        QString nativePath = QDir::toNativeSeparators(fi.exists() ? fi.absoluteFilePath() : path);
        auto *act = m_recentMenu->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("file"), 16), text);
        act->setToolTip(nativePath);
        act->setStatusTip(nativePath);
        connect(act, &QAction::triggered, this, [this, path]() {
            if (!QFile::exists(path)) {
                auto btn = QMessageBox::warning(this,
                    QStringLiteral("文件不存在"),
                    QStringLiteral("找不到文件:\n%1\n\n是否从最近文件列表中移除？").arg(path),
                    QMessageBox::Yes | QMessageBox::No);
                if (btn == QMessageBox::Yes) {
                    removeRecentFile(path);
                }
                return;
            }
            openFile(path);
        });
        count++;
    }

    m_recentMenu->addSeparator();
    auto *clearAct = m_recentMenu->addAction(QStringLiteral("清空历史记录 (&C)"), this, &MainWindow::clearRecentFiles);
    clearAct->setEnabled(!m_recentFiles.isEmpty());
}

void MainWindow::onRecoveryTimer() {
    if (!m_editorView || !m_editorView->undoStack().isDirty()) {
        return;
    }
    if (m_session.isUntitled()) {
        if (!m_editorView->buffer().isEmpty()) {
            File::FileService::writeUntitledRecoverySnapshot(m_session.untitledId(), m_editorView->buffer());
        }
    } else {
        File::FileService::writeRecoverySnapshot(m_session.filePath(), m_editorView->buffer());
    }
}

bool MainWindow::checkUntitledRecoverySnapshot(bool interactive) {
    if (!m_session.isUntitled() || !m_editorView->buffer().isEmpty()) {
        return false;
    }
    auto snapshots = File::FileService::findUntitledRecoverySnapshots();
    if (snapshots.isEmpty()) {
        return false;
    }
    QString recPath = snapshots.first();
    if (interactive) {
        auto btn = QMessageBox::question(
            this,
            QStringLiteral("发现未命名文档恢复草稿"),
            QStringLiteral("检测到上次运行遗留了未保存的未命名文档草稿，是否恢复？"),
            QMessageBox::Yes | QMessageBox::No);
        if (btn == QMessageBox::Yes) {
            QString err;
            if (File::FileService::loadFile(recPath, m_editorView->buffer(), err)) {
                m_editorView->undoStack().clear(true);
                updateTitle();
                QFile::remove(recPath);
                return true;
            } else {
                QMessageBox::warning(this, QStringLiteral("恢复失败"), QStringLiteral("无法加载恢复草稿: ") + err);
            }
        } else {
            for (const auto &p : snapshots) {
                QFile::remove(p);
            }
        }
    } else {
        QString err;
        if (File::FileService::loadFile(recPath, m_editorView->buffer(), err)) {
            m_editorView->undoStack().clear(true);
            updateTitle();
            QFile::remove(recPath);
            return true;
        }
    }
    return false;
}

void MainWindow::updateFileWatcher(const QString &newPath) {
    if (!m_fileWatcher) {
        return;
    }
    const QStringList watched = m_fileWatcher->files();
    if (!watched.isEmpty()) {
        m_fileWatcher->removePaths(watched);
    }
    if (!newPath.isEmpty() && QFile::exists(newPath)) {
        m_fileWatcher->addPath(newPath);
    }
}

void MainWindow::onExternalFileChanged(const QString &path) {
    if (m_isSelfSaving) {
        return;
    }
    if (path.isEmpty() || path != m_session.filePath()) {
        return;
    }
    // Windows file watchers sometimes drop the path if replaced atomically
    if (QFile::exists(path) && !m_fileWatcher->files().contains(path)) {
        m_fileWatcher->addPath(path);
    }
    m_externalChangeDebounceTimer->start(200);
}

void MainWindow::handleExternalFileModification(bool interactive) {
    const QString filePath = m_session.filePath();
    if (filePath.isEmpty() || m_session.isUntitled()) {
        return;
    }
    if (!QFile::exists(filePath)) {
        return;
    }

    if (!File::FileService::hasExternalModification(filePath, m_session.lastKnownHash())) {
        return;
    }

    QByteArray currentHash = File::FileService::calculateFileHash(filePath);
    if (!m_suppressedExternalChangeHash.isEmpty() && currentHash == m_suppressedExternalChangeHash) {
        return;
    }

    if (!m_editorView->undoStack().isDirty()) {
        // Clean state: Auto-reload smoothly
        reloadCurrentFile();
        if (statusBar()) {
            statusBar()->showMessage(QStringLiteral("检测到外部文件已更新，已自动重新加载"), 3000);
        }
    } else {
        // Dirty state: Local unsaved edits exist!
        if (interactive) {
            auto btn = QMessageBox::warning(
                this,
                QStringLiteral("外部修改冲突"),
                QStringLiteral("文件 \"%1\" 已在外部被其他程序修改。\n\n"
                               "是否重新从磁盘加载？\n"
                               "（注意：重新加载将丢失当前编辑器中未保存的修改）")
                    .arg(QFileInfo(filePath).fileName()),
                QMessageBox::Yes | QMessageBox::No);
            if (btn == QMessageBox::Yes) {
                reloadCurrentFile();
            } else {
                m_suppressedExternalChangeHash = currentHash;
            }
        }
    }
}

bool MainWindow::reloadCurrentFile() {
    if (m_session.isUntitled() || m_session.filePath().isEmpty()) {
        return false;
    }
    QString err;
    if (!m_session.load(m_session.filePath(), m_editorView->buffer(), err)) {
        return false;
    }
    m_editorView->undoStack().clear();
    m_editorView->undoStack().setSaveCheckpoint();
    m_suppressedExternalChangeHash.clear();
    updateTitle();
    updateFileWatcher(m_session.filePath());
    return true;
}

void MainWindow::newFile() {
    if (!maybeSave()) {
        return;
    }
    updateFileWatcher(QString());
    m_suppressedExternalChangeHash.clear();
    m_session.resetToUntitled(m_editorView->buffer());
    m_editorView->setDocumentPath(QString());
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
    return openFile(filePath, true);
}

bool MainWindow::openFile(const QString &filePath, bool interactive) {
    if (!maybeSave()) {
        return false;
    }

    if (!QFile::exists(filePath)) {
        if (interactive) {
            QMessageBox::critical(this, QStringLiteral("打开文件失败"), QStringLiteral("文件不存在: ") + filePath);
        }
        return false;
    }

    // Check for crash recovery snapshot
    QString recPath = File::FileService::findRecoverySnapshot(filePath);
    if (!recPath.isEmpty() && interactive) {
        auto btn = QMessageBox::question(this,
                                         QStringLiteral("发现未保存恢复副本"),
                                         QStringLiteral("检测到该文档有未保存的崩溃恢复快照，是否从恢复副本加载？"),
                                         QMessageBox::Yes | QMessageBox::No);
        if (btn == QMessageBox::Yes) {
            QString err;
            if (File::FileService::loadFile(recPath, m_editorView->buffer(), err)) {
                // Initialize session to target file and baseline hash, without overwriting recovered buffer
                m_session.attachFile(filePath);
                m_editorView->setDocumentPath(filePath);
                addRecentFile(filePath);
                updateFileWatcher(filePath);
                m_suppressedExternalChangeHash.clear();
                // Mark document as dirty so user is prompted to save, retaining snapshot until successful save
                m_editorView->undoStack().clear(true);
                updateTitle();
                return true;
            } else {
                QMessageBox::warning(this, QStringLiteral("恢复失败"), QStringLiteral("无法加载恢复快照，将尝试加载磁盘原始内容: ") + err);
            }
        } else {
            // User explicitly chose to discard recovery snapshot
            File::FileService::removeRecoverySnapshot(filePath);
        }
    }

    QString err;
    if (!m_session.load(filePath, m_editorView->buffer(), err)) {
        if (interactive) {
            QMessageBox::critical(this, QStringLiteral("打开文件失败"), QStringLiteral("无法加载文件: ") + err);
        }
        return false;
    }

    m_editorView->setDocumentPath(filePath);
    m_editorView->undoStack().clear();
    m_editorView->undoStack().setSaveCheckpoint();
    addRecentFile(filePath);
    updateFileWatcher(filePath);
    m_suppressedExternalChangeHash.clear();
    updateTitle();
    return true;
}

bool MainWindow::saveFile(bool interactive, bool forceOverwrite) {
    if (m_session.isUntitled()) {
        return saveFileAs(interactive);
    }

    struct SaveGuard {
        bool &flag;
        SaveGuard(bool &f) : flag(f) { flag = true; }
        ~SaveGuard() { flag = false; }
    } guard(m_isSelfSaving);

    auto res = m_session.save(m_editorView->buffer(), forceOverwrite);
    if (!res.success) {
        if (res.hadConflict) {
            if (interactive) {
                auto btn = QMessageBox::warning(this,
                                                QStringLiteral("外部修改冲突"),
                                                QStringLiteral("该文件已在外部被其他程序修改！\n是否强制覆盖外部修改？"),
                                                QMessageBox::Yes | QMessageBox::No);
                if (btn == QMessageBox::Yes) {
                    // Force overwrite
                    auto forceRes = m_session.save(m_editorView->buffer(), true);
                    if (forceRes.success) {
                        m_editorView->undoStack().setSaveCheckpoint();
                        m_suppressedExternalChangeHash.clear();
                        addRecentFile(m_session.filePath());
                        updateTitle();
                        updateFileWatcher(m_session.filePath());
                        return true;
                    } else {
                        QMessageBox::critical(this, QStringLiteral("保存失败"), forceRes.errorMessage);
                        return false;
                    }
                }
            }
            return false;
        } else {
            if (interactive) {
                QMessageBox::critical(this, QStringLiteral("保存失败"), res.errorMessage);
            }
            return false;
        }
    }

    m_editorView->undoStack().setSaveCheckpoint();
    m_editorView->setDocumentPath(m_session.filePath());
    m_suppressedExternalChangeHash.clear();
    addRecentFile(m_session.filePath());
    updateTitle();
    updateFileWatcher(m_session.filePath());
    return true;
}

bool MainWindow::saveFileAs(bool interactive) {
    QString path;
    if (interactive) {
        QString defaultPath = m_session.isUntitled() ? QStringLiteral("未命名.md") : m_session.filePath();
        path = QFileDialog::getSaveFileName(this, QStringLiteral("另存为"), defaultPath, QStringLiteral("Markdown Files (*.md);;All Files (*.*)"));
        if (path.isEmpty()) {
            return false;
        }
    } else {
        path = m_session.filePath();
        if (path.isEmpty()) {
            return false;
        }
    }

    struct SaveGuard {
        bool &flag;
        SaveGuard(bool &f) : flag(f) { flag = true; }
        ~SaveGuard() { flag = false; }
    } guard(m_isSelfSaving);

    // Transactional save as
    auto res = m_session.saveAs(path, m_editorView->buffer());
    if (!res.success) {
        if (interactive) {
            QMessageBox::critical(this, QStringLiteral("另存为失败"), res.errorMessage);
        }
        return false;
    }

    m_editorView->undoStack().setSaveCheckpoint();
    m_editorView->setDocumentPath(m_session.filePath());
    m_suppressedExternalChangeHash.clear();
    addRecentFile(m_session.filePath());
    updateTitle();
    updateFileWatcher(m_session.filePath());
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

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    if (!m_initialSplitterSet && width() > 300) {
        m_initialSplitterSet = true;
        m_splitter->setSizes({m_targetSidebarWidth, qMax(100, width() - m_targetSidebarWidth)});
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (!m_initialSplitterSet && width() > 300) {
        m_initialSplitterSet = true;
        m_splitter->setSizes({m_targetSidebarWidth, qMax(100, width() - m_targetSidebarWidth)});
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (maybeSave()) {
        savePreferences();
        if (m_session.isUntitled()) {
            File::FileService::removeUntitledRecoverySnapshot(m_session.untitledId());
        } else {
            File::FileService::removeRecoverySnapshot(m_session.filePath());
        }
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    if (event->mimeData()->hasUrls()) {
        for (const auto &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                QString path = url.toLocalFile();
                QFileInfo fi(path);
                if (fi.isDir()) {
                    m_sidebar->setWorkspacePath(path);
                    m_sidebar->setVisible(true);
                    event->acceptProposedAction();
                    return;
                } else {
                    event->acceptProposedAction();
                    openFile(path);
                    return;
                }
            }
        }
    }
    event->ignore();
}

void MainWindow::updateFindMatches() {
    if (!m_findBar || !m_editorView) {
        return;
    }
    const QString query = m_findBar->findText();
    if (query.isEmpty()) {
        m_findBar->setMatchStatus(0, 0);
        return;
    }
    auto status = m_editorView->searchStatus(query, m_findBar->matchCase());
    m_findBar->setMatchStatus(status.current, status.total);
}

void MainWindow::updateTypographyMenu() {
    auto preset = Theme::ThemeManager::instance().fontPreset();
    if (m_actSans) m_actSans->setChecked(preset == Theme::ThemeManager::FontPreset::SansSerif);
    if (m_actSerif) m_actSerif->setChecked(preset == Theme::ThemeManager::FontPreset::Serif);
    if (m_actMono) m_actMono->setChecked(preset == Theme::ThemeManager::FontPreset::Monospace);

    auto rwMode = Theme::ThemeManager::instance().readingWidthMode();
    if (m_actWidthAdaptive) m_actWidthAdaptive->setChecked(rwMode == Theme::ThemeManager::ReadingWidthMode::Adaptive);
    if (m_actWidthNarrow) m_actWidthNarrow->setChecked(rwMode == Theme::ThemeManager::ReadingWidthMode::Narrow);
    if (m_actWidthStandard) m_actWidthStandard->setChecked(rwMode == Theme::ThemeManager::ReadingWidthMode::Standard);
    if (m_actWidthWide) m_actWidthWide->setChecked(rwMode == Theme::ThemeManager::ReadingWidthMode::Wide);
}

void MainWindow::loadPreferences(QSettings *settings) {
    auto applyFromSettings = [this](QSettings &s) {
        Theme::ThemeManager::instance().loadSettings(&s);

        QByteArray geo = s.value(QStringLiteral("window/geometry")).toByteArray();
        if (!geo.isEmpty()) {
            restoreGeometry(geo);

            // Screen boundary protection: Ensure window is visible on at least one connected screen
            bool onScreen = false;
            const auto screens = QGuiApplication::screens();
            for (auto *screen : screens) {
                if (screen && screen->availableGeometry().intersects(frameGeometry())) {
                    onScreen = true;
                    break;
                }
            }
            if (!onScreen && !screens.isEmpty()) {
                auto *primary = QGuiApplication::primaryScreen();
                if (primary) {
                    QRect screenGeo = primary->availableGeometry();
                    resize(qMin(width(), screenGeo.width()), qMin(height(), screenGeo.height()));
                    move(screenGeo.center() - rect().center());
                }
            }
        }

        bool sidebarVisible = s.value(QStringLiteral("window/sidebarVisible"), true).toBool();
        m_sidebar->setVisible(sidebarVisible);

        m_targetSidebarWidth = s.value(QStringLiteral("window/sidebarWidth"), 260).toInt();
        m_targetSidebarWidth = std::clamp(m_targetSidebarWidth, 100, 800);
        if (width() > 300) {
            m_splitter->setSizes({m_targetSidebarWidth, qMax(200, width() - m_targetSidebarWidth)});
            m_initialSplitterSet = true;
        }

        m_recentFiles = s.value(QStringLiteral("history/recentFiles")).toStringList();
        updateRecentFilesMenu();
        if (m_sidebar) {
            m_sidebar->setRecentFiles(m_recentFiles);
        }

        updateTypographyMenu();
    };

    if (settings) {
        applyFromSettings(*settings);
    } else {
        QSettings s;
        applyFromSettings(s);
    }
}

void MainWindow::savePreferences(QSettings *settings) {
    auto writeToSettings = [this](QSettings &s) {
        Theme::ThemeManager::instance().saveSettings(&s);
        s.setValue(QStringLiteral("window/geometry"), saveGeometry());
        s.setValue(QStringLiteral("window/sidebarVisible"), m_sidebar->isVisible());
        s.setValue(QStringLiteral("window/sidebarWidth"), m_splitter->sizes().value(0, 260));
        s.setValue(QStringLiteral("history/recentFiles"), m_recentFiles);
    };

    if (settings) {
        writeToSettings(*settings);
    } else {
        QSettings s;
        writeToSettings(s);
    }
}

} // namespace MarkdownTool::UI
