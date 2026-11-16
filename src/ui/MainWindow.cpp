#include "MainWindow.h"
#include "FindReplaceBar.h"
#include "file/FileService.h"
#include "theme/Theme.h"
#include "render/ImageService.h"

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
        handleAllExternalFileModifications(true);
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

    m_tabWidget = new DocumentTabWidget(editorContainer);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setElideMode(Qt::ElideRight);

    editorLayout->addWidget(m_findBar);
    editorLayout->addWidget(m_tabWidget, 1);

    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(editorContainer);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 840});

    setCentralWidget(m_splitter);

    // Tab Widget wiring
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onCurrentTabChanged);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, [this](int idx) { closeTab(idx); });
    connect(m_tabWidget->tabBar(), &QTabBar::tabMoved, this, &MainWindow::onTabMoved);

    // Outline debounce timer
    m_outlineDebounceTimer = new QTimer(this);
    m_outlineDebounceTimer->setSingleShot(true);
    m_outlineDebounceTimer->setInterval(200);
    connect(m_outlineDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_editorView && m_sidebar && m_sidebar->outlineWidget()) {
            m_sidebar->outlineWidget()->updateOutline(m_editorView->rootNode(), m_editorView->buffer().text());
        }
    });

    // Find and Replace wiring
    connect(m_findBar, &FindReplaceBar::findNext, this, [this](const QString &t, bool mc) {
        if (editorView()) {
            editorView()->findText(t, mc, true);
            updateFindMatches();
        }
    });
    connect(m_findBar, &FindReplaceBar::findPrev, this, [this](const QString &t, bool mc) {
        if (editorView()) {
            editorView()->findText(t, mc, false);
            updateFindMatches();
        }
    });
    connect(m_findBar, &FindReplaceBar::replaceOne, this, [this](const QString &f, const QString &r, bool mc) {
        if (editorView()) {
            editorView()->replaceOne(f, r, mc);
            updateFindMatches();
        }
    });
    connect(m_findBar, &FindReplaceBar::replaceAll, this, [this](const QString &f, const QString &r, bool mc) {
        if (editorView()) {
            editorView()->replaceAll(f, r, mc);
            updateFindMatches();
        }
    });
    connect(m_findBar, &FindReplaceBar::findParamsChanged, this, [this](const QString &, bool) {
        updateFindMatches();
    });
    connect(m_findBar, &FindReplaceBar::closed, this, [this]() {
        if (editorView()) {
            editorView()->setFocus();
        }
    });

    connect(m_sidebar->outlineWidget(), &OutlineWidget::headingClicked, this, [this](int offset) {
        if (editorView()) {
            editorView()->setCursorPos(offset);
            editorView()->setFocus();
        }
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

    // Create initial document tab after all UI widgets and status bar are initialized
    createTab(QString(), true);
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
    fileMenu->addAction(QStringLiteral("关闭标签页 (&W)"), QKeySequence(QStringLiteral("Ctrl+W")), this, [this]() { closeCurrentTab(); });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("退出 (&X)"), QKeySequence::Quit, this, &MainWindow::close);

    // Edit Menu
    QMenu *editMenu = mb->addMenu(QStringLiteral("编辑 (&E)"));
    editMenu->addAction(QStringLiteral("撤销 (&U)"), QKeySequence::Undo, [this]() { if (editorView()) editorView()->undo(); });
    editMenu->addAction(QStringLiteral("重做 (&R)"), QKeySequence::Redo, [this]() { if (editorView()) editorView()->redo(); });
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("剪切 (&T)"), QKeySequence::Cut, [this]() { if (editorView()) editorView()->cut(); });
    editMenu->addAction(QStringLiteral("复制 (&C)"), QKeySequence::Copy, [this]() { if (editorView()) editorView()->copy(); });
    editMenu->addAction(QStringLiteral("粘贴 (&P)"), QKeySequence::Paste, [this]() { if (editorView()) editorView()->paste(); });
    editMenu->addAction(QStringLiteral("全选 (&A)"), QKeySequence::SelectAll, [this]() { if (editorView()) editorView()->selectAll(); });
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
        if (!text.isEmpty() && editorView()) {
            editorView()->findText(text, m_findBar->matchCase(), true);
            updateFindMatches();
        }
    });
    editMenu->addAction(QStringLiteral("查找上一个"), QKeySequence(QKeySequence::FindPrevious), [this]() {
        const QString text = m_findBar->findText();
        if (!text.isEmpty() && editorView()) {
            editorView()->findText(text, m_findBar->matchCase(), false);
            updateFindMatches();
        }
    });

    // Format Menu
    QMenu *formatMenu = mb->addMenu(QStringLiteral("段落与格式 (&M)"));
    formatMenu->addAction(QStringLiteral("加粗 (**粗体**)"), QKeySequence::Bold, [this]() { if (editorView()) editorView()->toggleBold(); });
    formatMenu->addAction(QStringLiteral("斜体 (*斜体*)"), QKeySequence::Italic, [this]() { if (editorView()) editorView()->toggleItalic(); });
    formatMenu->addAction(QStringLiteral("删除线 (~~文字~~)"), [this]() { if (editorView()) editorView()->toggleStrikethrough(); });
    formatMenu->addAction(QStringLiteral("行内代码 (`code`)"), [this]() { if (editorView()) editorView()->toggleCodeSpan(); });
    formatMenu->addAction(QStringLiteral("超链接 [文字](url)"), [this]() { if (editorView()) editorView()->insertLink(); });
    formatMenu->addAction(QStringLiteral("切换任务列表 (- [ ])"), [this]() { if (editorView()) editorView()->toggleTaskList(); });
    formatMenu->addSeparator();
    for (int lvl = 1; lvl <= 6; ++lvl) {
        formatMenu->addAction(QStringLiteral("标题 %1 (H%2)").arg(lvl).arg(lvl), [this, lvl]() {
            if (editorView()) editorView()->insertHeading(lvl);
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
    connect(m_tbUndo, &QAction::triggered, [this]() { if (editorView()) editorView()->undo(); });

    m_tbRedo = tb->addAction(Theme::ThemeManager::instance().icon(QStringLiteral("redo"), 18), QStringLiteral("重做 (Ctrl+Y)"));
    m_tbRedo->setToolTip(QStringLiteral("重做操作 (Ctrl+Y)"));
    connect(m_tbRedo, &QAction::triggered, [this]() { if (editorView()) editorView()->redo(); });

    // Tab Navigation Global Shortcuts
    auto tabNextAct = new QAction(this);
    tabNextAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")));
    connect(tabNextAct, &QAction::triggered, this, &MainWindow::nextTab);
    addAction(tabNextAct);

    auto tabPrevAct = new QAction(this);
    tabPrevAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")));
    connect(tabPrevAct, &QAction::triggered, this, &MainWindow::prevTab);
    addAction(tabPrevAct);

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
    int idx = currentTabIndex();
    if (idx >= 0 && idx < m_tabs.size()) {
        const auto &tab = m_tabs[idx];
        QString title = tab->session.isUntitled()
            ? (tab->untitledNumber > 0 ? QStringLiteral("未命名-%1").arg(tab->untitledNumber) : QStringLiteral("未命名文档"))
            : tab->session.displayName();
        if (tab->view->undoStack().isDirty()) {
            title.prepend(QStringLiteral("● "));
        }
        setWindowTitle(title);
    } else {
        setWindowTitle(QStringLiteral("Native Markdown Editor"));
    }
}

void MainWindow::updateStatusBar(int line, int col, int) {
    if (!m_editorView) {
        return;
    }
    if (m_statusPosLabel) m_statusPosLabel->setText(QStringLiteral("第 %1 行, 第 %2 列").arg(line).arg(col));
    int charCount = m_editorView->buffer().length();
    if (m_statusStatsLabel) m_statusStatsLabel->setText(QStringLiteral("%1 字符").arg(charCount));

    QString encStr = (m_editorView->buffer().encoding() == Core::DocumentEncoding::Utf8Bom) ? QStringLiteral("UTF-8 BOM") : QStringLiteral("UTF-8");
    QString leStr = (m_editorView->buffer().lineEnding() == Core::LineEnding::Crlf) ? QStringLiteral("CRLF") : QStringLiteral("LF");
    if (m_statusEncodingLabel) m_statusEncodingLabel->setText(encStr + QStringLiteral(" · ") + leStr);
}

void MainWindow::onCleanStateChanged(bool isClean) {
    if (m_statusDirtyLabel) m_statusDirtyLabel->setText(isClean ? QStringLiteral("● 已保存") : QStringLiteral("○ 未保存"));
    updateTitle();
}

File::DocumentSession &MainWindow::session() {
    if (auto *tab = currentTabInfo()) {
        return tab->session;
    }
    return m_fallbackSession;
}

const File::DocumentSession &MainWindow::session() const {
    if (auto *tab = currentTabInfo()) {
        return tab->session;
    }
    return m_fallbackSession;
}

int MainWindow::currentTabIndex() const {
    return m_tabWidget ? m_tabWidget->currentIndex() : -1;
}

MainWindow::TabInfo *MainWindow::currentTabInfo() const {
    return tabInfoAt(currentTabIndex());
}

MainWindow::TabInfo *MainWindow::tabInfoAt(int index) const {
    if (!m_tabWidget || index < 0 || index >= m_tabWidget->count()) {
        return nullptr;
    }
    if (index < static_cast<int>(m_tabs.size()) && m_tabs[index]->view == m_tabWidget->widget(index)) {
        return m_tabs[index].get();
    }
    QWidget *target = m_tabWidget->widget(index);
    for (const auto &tab : m_tabs) {
        if (tab->view == target) {
            return tab.get();
        }
    }
    return nullptr;
}

int MainWindow::findTabIndexByPath(const QString &path) const {
    if (path.isEmpty() || !m_tabWidget) return -1;
    QString target = QFileInfo(path).canonicalFilePath();
    if (target.isEmpty()) target = QFileInfo(path).absoluteFilePath();

    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (auto *tab = tabInfoAt(i)) {
            if (!tab->session.filePath().isEmpty()) {
                QString cur = QFileInfo(tab->session.filePath()).canonicalFilePath();
                if (cur.isEmpty()) cur = QFileInfo(tab->session.filePath()).absoluteFilePath();
                if (cur == target) {
                    return i;
                }
            }
        }
    }
    return -1;
}

void MainWindow::onTabMoved(int from, int to) {
    if (from < 0 || from >= static_cast<int>(m_tabs.size()) ||
        to < 0 || to >= static_cast<int>(m_tabs.size()) || from == to) {
        return;
    }
    auto item = std::move(m_tabs[from]);
    m_tabs.erase(m_tabs.begin() + from);
    m_tabs.insert(m_tabs.begin() + to, std::move(item));

    onCurrentTabChanged(m_tabWidget->currentIndex());
}

void MainWindow::setupEditorViewConnections(Editor::NativeEditorView *view) {
    connect(view, &Editor::NativeEditorView::cursorMoved, this, [this, view](int line, int col, int offset) {
        if (m_editorView == view) {
            updateStatusBar(line, col, offset);
            if (m_findBar && m_findBar->isVisible()) {
                updateFindMatches();
            }
        }
    });

    connect(view, &Editor::NativeEditorView::textChanged, this, [this, view]() {
        if (m_editorView == view) {
            m_outlineDebounceTimer->start();
            if (m_findBar && m_findBar->isVisible()) {
                updateFindMatches();
            }
        }
    });

    connect(&view->undoStack(), &Core::UndoStack::cleanStateChanged, this, [this, view](bool) {
        for (int i = 0; i < m_tabWidget->count(); ++i) {
            if (auto *tab = tabInfoAt(i)) {
                if (tab->view == view) {
                    updateTabTitle(i);
                    if (m_editorView == view) {
                        onCleanStateChanged(!view->undoStack().isDirty());
                    }
                    break;
                }
            }
        }
    });

    connect(view, &Editor::NativeEditorView::modeChanged, this, [this, view](bool isSource) {
        if (m_editorView == view) {
            if (m_statusModeLabel) {
                m_statusModeLabel->setText(isSource ? QStringLiteral("源码模式") : QStringLiteral("即时预览"));
            }
            if (m_tbMode) {
                m_tbMode->setIcon(Theme::ThemeManager::instance().icon(isSource ? QStringLiteral("source") : QStringLiteral("mode"), 18));
            }
        }
    });

    connect(view, &Editor::NativeEditorView::fileOpenRequested, this, [this](const QString &path) {
        openFile(path);
    });

    connect(view, &Editor::NativeEditorView::folderOpenRequested, this, [this](const QString &dir) {
        m_sidebar->setWorkspacePath(dir);
        m_sidebar->setVisible(true);
    });
}

int MainWindow::createTab(const QString &filePath, bool makeCurrent, bool interactive) {
    auto view = new Editor::NativeEditorView(m_tabWidget);
    setupEditorViewConnections(view);

    auto tab = std::make_unique<TabInfo>();
    tab->view = view;

    if (filePath.isEmpty()) {
        tab->untitledNumber = m_nextUntitledNumber++;
        tab->session.resetToUntitled(view->buffer());
        view->setDocumentPath(QString());
        view->setStagingAssetsDir(File::FileService::untitledDraftAssetsDirectory(tab->session.untitledId()));
    }

    QString baseTitle = tab->session.isUntitled()
        ? QStringLiteral("未命名-%1").arg(tab->untitledNumber)
        : tab->session.displayName();

    int tabIdx = m_tabWidget->addTab(view, baseTitle);
    m_tabWidget->setTabToolTip(tabIdx, filePath.isEmpty() ? QStringLiteral("未保存的草稿") : filePath);
    m_tabs.push_back(std::move(tab));

    if (!filePath.isEmpty()) {
        if (!loadFileIntoTab(tabIdx, filePath, interactive)) {
            QWidget *w = m_tabWidget->widget(tabIdx);
            m_tabWidget->removeTab(tabIdx);
            for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
                if ((*it)->view == view) {
                    m_tabs.erase(it);
                    break;
                }
            }
            delete w;
            return -1;
        }
    }

    if (makeCurrent) {
        m_tabWidget->setCurrentIndex(tabIdx);
        onCurrentTabChanged(tabIdx);
    }

    return tabIdx;
}

void MainWindow::updateTabTitle(int index) {
    TabInfo *tab = tabInfoAt(index);
    if (!tab) return;
    QString baseName = tab->session.isUntitled()
        ? (tab->untitledNumber > 0 ? QStringLiteral("未命名-%1").arg(tab->untitledNumber) : QStringLiteral("未命名文档"))
        : tab->session.displayName();
    if (tab->hasPendingExternalConflict) {
        baseName.append(QStringLiteral(" [外部冲突]"));
    } else if (tab->view->undoStack().isDirty()) {
        baseName.append(QStringLiteral(" ●"));
    }
    m_tabWidget->setTabText(index, baseName);
    m_tabWidget->setTabToolTip(index, tab->session.filePath().isEmpty() ? QStringLiteral("未保存的草稿") : tab->session.filePath());
}

void MainWindow::onCurrentTabChanged(int index) {
    TabInfo *tab = tabInfoAt(index);
    if (tab) {
        m_editorView = tab->view;
        Render::ImageService::instance()->setDocumentPath(tab->session.filePath());
        if (tab->session.isUntitled()) {
            Render::ImageService::instance()->setStagingAssetsDir(File::FileService::untitledDraftAssetsDirectory(tab->session.untitledId()));
        } else {
            Render::ImageService::instance()->setStagingAssetsDir(QString());
        }
    } else {
        m_editorView = nullptr;
    }

    updateTitle();

    if (m_editorView && tab) {
        if (tab->hasPendingExternalConflict) {
            tab->hasPendingExternalConflict = false;
            updateTabTitle(index);
            handleExternalFileModification(true);
        }

        auto lc = m_editorView->buffer().offsetToLineCol(m_editorView->cursorPos());
        updateStatusBar(lc.line1(), lc.col1(), m_editorView->cursorPos());
        if (m_statusModeLabel) m_statusModeLabel->setText(m_editorView->isSourceMode() ? QStringLiteral("源码模式") : QStringLiteral("即时预览"));
        if (m_statusDirtyLabel) m_statusDirtyLabel->setText(m_editorView->undoStack().isDirty() ? QStringLiteral("○ 未保存") : QStringLiteral("● 已保存"));
        if (m_tbMode) {
            m_tbMode->setIcon(Theme::ThemeManager::instance().icon(m_editorView->isSourceMode() ? QStringLiteral("source") : QStringLiteral("mode"), 18));
        }

        if (m_sidebar && m_sidebar->outlineWidget()) {
            m_sidebar->outlineWidget()->updateOutline(m_editorView->rootNode(), m_editorView->buffer().text());
        }

        if (m_findBar && m_findBar->isVisible()) {
            updateFindMatches();
        }

        m_editorView->setFocus();
    }
}

bool MainWindow::closeTab(int index, bool interactive) {
    if (index < 0 || index >= m_tabWidget->count()) {
        return false;
    }

    TabInfo *tab = tabInfoAt(index);
    if (!tab) {
        return false;
    }

    if (tab->view->undoStack().isDirty()) {
        if (interactive) {
            m_tabWidget->setCurrentIndex(index);
            auto ret = QMessageBox::warning(this,
                                            QStringLiteral("未保存更改"),
                                            QStringLiteral("文档 \"%1\" 已被修改，是否保存？").arg(tab->session.displayName()),
                                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (ret == QMessageBox::Save) {
                if (!saveTab(index, interactive)) {
                    return false;
                }
            } else if (ret == QMessageBox::Cancel) {
                return false;
            }
        }
    }

    // Clean up recovery snapshot and draft assets
    if (tab->session.isUntitled()) {
        File::FileService::removeUntitledRecoverySnapshot(tab->session.untitledId());
        File::FileService::removeUntitledDraftAssetsDirectory(tab->session.untitledId());
    } else {
        File::FileService::removeRecoverySnapshot(tab->session.filePath());
    }

    QWidget *viewWidget = m_tabWidget->widget(index);
    m_tabWidget->removeTab(index);

    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if ((*it)->view == viewWidget) {
            m_tabs.erase(it);
            break;
        }
    }
    delete viewWidget;

    updateFileWatcher();

    if (m_tabs.empty()) {
        createTab(QString(), true);
    } else {
        onCurrentTabChanged(m_tabWidget->currentIndex());
    }

    return true;
}

bool MainWindow::closeCurrentTab(bool interactive) {
    return closeTab(currentTabIndex(), interactive);
}

bool MainWindow::closeAllTabs(bool interactive) {
    // Phase 1: Confirmation pass. Validate all dirty tabs without deleting any snapshots
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        TabInfo *tab = tabInfoAt(i);
        if (!tab) continue;
        if (tab->view->undoStack().isDirty()) {
            if (interactive) {
                m_tabWidget->setCurrentIndex(i);
                auto ret = QMessageBox::warning(this,
                                                QStringLiteral("未保存更改"),
                                                QStringLiteral("文档 \"%1\" 已被修改，是否保存？").arg(tab->session.displayName()),
                                                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
                if (ret == QMessageBox::Save) {
                    if (!saveTab(i, interactive)) return false;
                } else if (ret == QMessageBox::Cancel) {
                    return false;
                }
            }
        }
    }

    // Phase 2: All tabs confirmed! Clean up snapshots, draft assets, and close widgets
    for (int i = m_tabWidget->count() - 1; i >= 0; --i) {
        TabInfo *tab = tabInfoAt(i);
        if (tab) {
            if (tab->session.isUntitled()) {
                File::FileService::removeUntitledRecoverySnapshot(tab->session.untitledId());
                File::FileService::removeUntitledDraftAssetsDirectory(tab->session.untitledId());
            } else {
                File::FileService::removeRecoverySnapshot(tab->session.filePath());
            }
        }
        QWidget *w = m_tabWidget->widget(i);
        m_tabWidget->removeTab(i);
        delete w;
    }
    m_tabs.clear();

    updateFileWatcher();
    return true;
}

void MainWindow::switchToTab(int index) {
    if (index >= 0 && index < m_tabs.size() && m_tabWidget) {
        if (m_tabWidget->currentIndex() == index) {
            onCurrentTabChanged(index);
        } else {
            m_tabWidget->setCurrentIndex(index);
        }
    }
}

void MainWindow::nextTab() {
    if (m_tabs.size() > 1 && m_tabWidget) {
        int next = (m_tabWidget->currentIndex() + 1) % m_tabs.size();
        m_tabWidget->setCurrentIndex(next);
    }
}

void MainWindow::prevTab() {
    if (m_tabs.size() > 1 && m_tabWidget) {
        int prev = (m_tabWidget->currentIndex() - 1 + m_tabs.size()) % m_tabs.size();
        m_tabWidget->setCurrentIndex(prev);
    }
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
    for (const auto &tab : m_tabs) {
        if (!tab->view || !tab->view->undoStack().isDirty()) {
            continue;
        }
        if (tab->session.isUntitled()) {
            if (!tab->view->buffer().isEmpty()) {
                File::FileService::writeUntitledRecoverySnapshot(tab->session.untitledId(), tab->view->buffer());
            }
        } else {
            File::FileService::writeRecoverySnapshot(tab->session.filePath(), tab->view->buffer());
        }
    }
}

bool MainWindow::checkUntitledRecoverySnapshot(bool interactive) {
    auto snapshots = File::FileService::findUntitledRecoverySnapshots();
    if (snapshots.isEmpty()) {
        return false;
    }

    if (interactive) {
        QString msg = (snapshots.size() == 1)
            ? QStringLiteral("检测到上次运行遗留了 1 份未保存的未命名文档草稿，是否恢复？")
            : QStringLiteral("检测到上次运行遗留了 %1 份未保存的未命名文档草稿，是否全部恢复？").arg(snapshots.size());
        auto btn = QMessageBox::question(
            this,
            QStringLiteral("发现未命名文档恢复草稿"),
            msg,
            QMessageBox::Yes | QMessageBox::No);
        if (btn != QMessageBox::Yes) {
            for (const auto &p : snapshots) {
                QString uid = File::FileService::extractUntitledIdFromSnapshotPath(p);
                QFile::remove(p);
                if (!uid.isEmpty()) {
                    File::FileService::removeUntitledDraftAssetsDirectory(uid);
                }
            }
            return false;
        }
    }

    int restoredCount = 0;
    for (int i = 0; i < snapshots.size(); ++i) {
        const QString &recPath = snapshots[i];
        QString originalId = File::FileService::extractUntitledIdFromSnapshotPath(recPath);

        int targetTabIdx = -1;
        if (i == 0 && m_tabWidget->count() == 1) {
            TabInfo *firstTab = tabInfoAt(0);
            if (firstTab && firstTab->session.isUntitled() &&
                !firstTab->view->undoStack().isDirty() && firstTab->view->buffer().isEmpty()) {
                targetTabIdx = 0;
            }
        }
        if (targetTabIdx < 0) {
            targetTabIdx = createTab(QString(), false);
        }

        if (targetTabIdx >= 0) {
            TabInfo *tab = tabInfoAt(targetTabIdx);
            if (tab) {
                QString err;
                if (File::FileService::loadFile(recPath, tab->view->buffer(), err)) {
                    if (!originalId.isEmpty()) {
                        tab->session.setUntitledId(originalId);
                        tab->view->setStagingAssetsDir(File::FileService::untitledDraftAssetsDirectory(originalId));
                    }
                    tab->view->undoStack().clear(true);
                    updateTabTitle(targetTabIdx);
                    restoredCount++;
                }
            }
        }
    }

    if (restoredCount > 0) {
        m_tabWidget->setCurrentIndex(0);
        onCurrentTabChanged(0);
        updateTitle();
        return true;
    }

    return false;
}

void MainWindow::updateFileWatcher() {
    if (!m_fileWatcher) {
        return;
    }
    const QStringList watched = m_fileWatcher->files();
    if (!watched.isEmpty()) {
        m_fileWatcher->removePaths(watched);
    }
    QStringList pathsToWatch;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (auto *tab = tabInfoAt(i)) {
            if (!tab->session.isUntitled() && !tab->session.filePath().isEmpty()) {
                if (QFile::exists(tab->session.filePath())) {
                    pathsToWatch.append(tab->session.filePath());
                }
            }
        }
    }
    pathsToWatch.removeDuplicates();
    if (!pathsToWatch.isEmpty()) {
        m_fileWatcher->addPaths(pathsToWatch);
    }
}

void MainWindow::updateFileWatcher(const QString &) {
    updateFileWatcher();
}

void MainWindow::onExternalFileChanged(const QString &path) {
    if (m_isSelfSaving) {
        return;
    }
    if (path.isEmpty()) {
        return;
    }
    int tabIdx = findTabIndexByPath(path);
    if (tabIdx < 0) {
        return;
    }
    if (QFile::exists(path) && m_fileWatcher && !m_fileWatcher->files().contains(path)) {
        m_fileWatcher->addPath(path);
    }
    m_externalChangeDebounceTimer->start(200);
}

void MainWindow::handleExternalFileModification(bool interactive) {
    int curIdx = currentTabIndex();
    TabInfo *tab = tabInfoAt(curIdx);
    if (!tab) {
        return;
    }
    const QString filePath = tab->session.filePath();
    if (filePath.isEmpty() || tab->session.isUntitled() || !QFile::exists(filePath)) {
        return;
    }

    if (!File::FileService::hasExternalModification(filePath, tab->session.lastKnownHash())) {
        return;
    }

    QByteArray currentHash = File::FileService::calculateFileHash(filePath);
    if (!tab->suppressedExternalChangeHash.isEmpty() && currentHash == tab->suppressedExternalChangeHash) {
        return;
    }

    if (!tab->view->undoStack().isDirty()) {
        // Clean state: Auto-reload smoothly
        reloadTab(curIdx);
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
                reloadTab(curIdx);
            } else {
                tab->suppressedExternalChangeHash = currentHash;
            }
        }
    }
}

void MainWindow::handleAllExternalFileModifications(bool interactive) {
    int cur = currentTabIndex();
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        TabInfo *tab = tabInfoAt(i);
        if (!tab || tab->session.isUntitled() || tab->session.filePath().isEmpty()) continue;
        if (!QFile::exists(tab->session.filePath())) continue;
        if (File::FileService::hasExternalModification(tab->session.filePath(), tab->session.lastKnownHash())) {
            QByteArray currentHash = File::FileService::calculateFileHash(tab->session.filePath());
            if (!tab->suppressedExternalChangeHash.isEmpty() && currentHash == tab->suppressedExternalChangeHash) {
                continue;
            }

            if (i == cur) {
                handleExternalFileModification(interactive);
            } else if (!tab->view->undoStack().isDirty()) {
                reloadTab(i);
            } else {
                tab->hasPendingExternalConflict = true;
                updateTabTitle(i);
            }
        }
    }
}

bool MainWindow::reloadCurrentFile() {
    return reloadTab(currentTabIndex());
}

bool MainWindow::reloadTab(int index) {
    TabInfo *tab = tabInfoAt(index);
    if (!tab) {
        return false;
    }
    if (tab->session.isUntitled() || tab->session.filePath().isEmpty()) {
        return false;
    }
    QString err;
    if (!tab->session.load(tab->session.filePath(), tab->view->buffer(), err)) {
        return false;
    }
    tab->view->undoStack().clear();
    tab->view->undoStack().setSaveCheckpoint();
    tab->suppressedExternalChangeHash.clear();
    tab->hasPendingExternalConflict = false;
    updateTabTitle(index);
    updateTitle();
    updateFileWatcher();
    return true;
}

void MainWindow::newFile() {
    createTab(QString(), true);
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
    if (filePath.isEmpty()) {
        return false;
    }

    if (!QFile::exists(filePath)) {
        if (interactive) {
            QMessageBox::critical(this, QStringLiteral("打开文件失败"), QStringLiteral("文件不存在: ") + filePath);
        }
        return false;
    }

    // Check if already open in an existing tab
    int existingIdx = findTabIndexByPath(filePath);
    if (existingIdx >= 0) {
        m_tabWidget->setCurrentIndex(existingIdx);
        onCurrentTabChanged(existingIdx);
        return true;
    }

    // If there is only 1 tab and it is untitled, clean, and empty, reuse it!
    if (m_tabWidget->count() == 1) {
        TabInfo *firstTab = tabInfoAt(0);
        if (firstTab &&
            firstTab->session.isUntitled() &&
            !firstTab->view->undoStack().isDirty() &&
            firstTab->view->buffer().isEmpty()) {

            QString oldUntitledId = firstTab->session.untitledId();
            bool ok = loadFileIntoTab(0, filePath, interactive);
            if (ok) {
                File::FileService::removeUntitledDraftAssetsDirectory(oldUntitledId);
                firstTab->view->setStagingAssetsDir(QString());
                m_tabWidget->setCurrentIndex(0);
                onCurrentTabChanged(0);
            }
            return ok;
        }
    }

    // Otherwise, create a new tab and load file into it
    int newTabIdx = createTab(filePath, true, interactive);
    return newTabIdx >= 0;
}

bool MainWindow::loadFileIntoTab(int index, const QString &filePath, bool interactive) {
    TabInfo *tab = tabInfoAt(index);
    if (!tab) {
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
            if (File::FileService::loadFile(recPath, tab->view->buffer(), err)) {
                tab->session.attachFile(filePath);
                tab->view->setDocumentPath(filePath);
                tab->view->setStagingAssetsDir(QString());
                addRecentFile(filePath);
                tab->suppressedExternalChangeHash.clear();
                tab->hasPendingExternalConflict = false;
                tab->view->undoStack().clear(true);
                updateTabTitle(index);
                updateTitle();
                updateFileWatcher();
                return true;
            } else {
                QMessageBox::warning(this, QStringLiteral("恢复失败"), QStringLiteral("无法加载恢复快照，将尝试加载磁盘原始内容: ") + err);
            }
        } else {
            File::FileService::removeRecoverySnapshot(filePath);
        }
    }

    QString err;
    if (!tab->session.load(filePath, tab->view->buffer(), err)) {
        if (interactive) {
            QMessageBox::critical(this, QStringLiteral("打开文件失败"), QStringLiteral("无法加载文件: ") + err);
        }
        return false;
    }

    tab->view->setDocumentPath(filePath);
    tab->view->setStagingAssetsDir(QString());
    tab->view->undoStack().clear();
    tab->view->undoStack().setSaveCheckpoint();
    addRecentFile(filePath);
    tab->suppressedExternalChangeHash.clear();
    tab->hasPendingExternalConflict = false;
    updateTabTitle(index);
    updateTitle();
    updateFileWatcher();
    return true;
}

bool MainWindow::saveFile(bool interactive, bool forceOverwrite) {
    return saveTab(currentTabIndex(), interactive, forceOverwrite);
}

bool MainWindow::saveFileAs(const QString &targetPath, bool interactive) {
    return saveTabAs(currentTabIndex(), targetPath, interactive);
}

bool MainWindow::saveTab(int index, bool interactive, bool forceOverwrite) {
    TabInfo *tab = tabInfoAt(index);
    if (!tab) {
        return false;
    }
    if (tab->session.isUntitled()) {
        return saveTabAs(index, QString(), interactive);
    }

    struct SaveGuard {
        bool &flag;
        SaveGuard(bool &f) : flag(f) { flag = true; }
        ~SaveGuard() { flag = false; }
    } guard(m_isSelfSaving);

    auto res = tab->session.save(tab->view->buffer(), forceOverwrite);
    if (!res.success) {
        if (res.hadConflict) {
            if (interactive) {
                auto btn = QMessageBox::warning(this,
                                                QStringLiteral("外部修改冲突"),
                                                QStringLiteral("该文件已在外部被其他程序修改！\n是否强制覆盖外部修改？"),
                                                QMessageBox::Yes | QMessageBox::No);
                if (btn == QMessageBox::Yes) {
                    auto forceRes = tab->session.save(tab->view->buffer(), true);
                    if (forceRes.success) {
                        tab->view->undoStack().setSaveCheckpoint();
                        tab->view->setDocumentPath(tab->session.filePath());
                        tab->suppressedExternalChangeHash.clear();
                        tab->hasPendingExternalConflict = false;
                        addRecentFile(tab->session.filePath());
                        updateTabTitle(index);
                        updateTitle();
                        updateFileWatcher();
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

    tab->view->undoStack().setSaveCheckpoint();
    tab->view->setDocumentPath(tab->session.filePath());
    tab->suppressedExternalChangeHash.clear();
    tab->hasPendingExternalConflict = false;
    addRecentFile(tab->session.filePath());
    updateTabTitle(index);
    updateTitle();
    updateFileWatcher();
    return true;
}

bool MainWindow::saveTabAs(int index, const QString &targetPath, bool interactive) {
    TabInfo *tab = tabInfoAt(index);
    if (!tab) {
        return false;
    }
    QString path = targetPath;
    if (path.isEmpty()) {
        if (interactive) {
            QString defaultPath = tab->session.isUntitled() ? QStringLiteral("未命名.md") : tab->session.filePath();
            path = QFileDialog::getSaveFileName(this, QStringLiteral("另存为"), defaultPath, QStringLiteral("Markdown Files (*.md);;All Files (*.*)"));
            if (path.isEmpty()) {
                return false;
            }
        } else {
            path = tab->session.filePath();
            if (path.isEmpty()) {
                return false;
            }
        }
    }

    struct SaveGuard {
        bool &flag;
        SaveGuard(bool &f) : flag(f) { flag = true; }
        ~SaveGuard() { flag = false; }
    } guard(m_isSelfSaving);

    bool wasUntitled = tab->session.isUntitled();
    QString untitledId = tab->session.untitledId();
    File::AssetRelocationTransaction assetTx;

    if (wasUntitled) {
        if (!assetTx.prepare(untitledId, path)) {
            if (interactive) {
                QMessageBox::critical(this, QStringLiteral("另存为失败"), QStringLiteral("迁移临时图片资源失败，操作已回滚。"));
            }
            return false;
        }
    }

    auto res = tab->session.saveAs(path, tab->view->buffer());
    if (!res.success) {
        if (wasUntitled) {
            assetTx.rollback();
        }
        if (interactive) {
            QMessageBox::critical(this, QStringLiteral("另存为失败"), res.errorMessage);
        }
        return false;
    }

    if (wasUntitled) {
        assetTx.commit();
        File::FileService::removeUntitledRecoverySnapshot(untitledId);
        tab->view->setStagingAssetsDir(QString());
        if (currentTabIndex() == index) {
            Render::ImageService::instance()->setStagingAssetsDir(QString());
        }
    }

    tab->view->undoStack().setSaveCheckpoint();
    tab->view->setDocumentPath(tab->session.filePath());
    tab->suppressedExternalChangeHash.clear();
    tab->hasPendingExternalConflict = false;
    addRecentFile(tab->session.filePath());
    updateTabTitle(index);
    updateTitle();
    updateFileWatcher();
    return true;
}

bool MainWindow::maybeSave() {
    int curIdx = currentTabIndex();
    if (curIdx < 0 || curIdx >= m_tabs.size()) {
        return true;
    }
    auto &tab = m_tabs[curIdx];
    if (!tab->view->undoStack().isDirty()) {
        return true;
    }

    auto ret = QMessageBox::warning(this,
                                    QStringLiteral("未保存更改"),
                                    QStringLiteral("文档 \"%1\" 已被修改，是否立即保存？").arg(tab->session.displayName()),
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
    if (closeAllTabs(true)) {
        savePreferences();
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
