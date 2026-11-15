#include "SidebarWidget.h"
#include "theme/Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDir>
#include <QMenu>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>

namespace MarkdownTool::UI {

namespace {

class FileFilterProxyModel : public QSortFilterProxyModel {
public:
    explicit FileFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setRecursiveFilteringEnabled(false);
    }

    void setFilterPattern(const QString &pattern) {
        m_filterText = pattern.trimmed();
        invalidateFilter();
    }

    const QString &filterPattern() const { return m_filterText; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override {
        auto *fsModel = qobject_cast<QFileSystemModel*>(sourceModel());
        if (!fsModel) {
            return true;
        }

        QModelIndex index = fsModel->index(sourceRow, 0, sourceParent);
        if (!index.isValid()) {
            return false;
        }

        // Directories are always shown to preserve folder navigation
        if (fsModel->isDir(index)) {
            return true;
        }

        // Files: only show supported document types (.md, .markdown, .txt)
        const QString fileName = fsModel->fileName(index);
        bool hasSupportedExt = fileName.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) ||
                               fileName.endsWith(QStringLiteral(".markdown"), Qt::CaseInsensitive) ||
                               fileName.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive);
        if (!hasSupportedExt) {
            return false;
        }

        // If no filter query, accept all supported files
        if (m_filterText.isEmpty()) {
            return true;
        }

        // Literal substring match (no wildcard interpretation, case-insensitive)
        return fileName.contains(m_filterText, Qt::CaseInsensitive);
    }

private:
    QString m_filterText;
};

} // namespace

SidebarWidget::SidebarWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sidebarContainer"));
    setMinimumWidth(100);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(6);

    // Header container with Segmented Control and Search Input
    auto headerContainer = new QWidget(this);
    headerContainer->setMinimumWidth(80);
    auto headerLayout = new QVBoxLayout(headerContainer);
    headerLayout->setContentsMargins(8, 8, 8, 4);
    headerLayout->setSpacing(8);

    // Segmented Control Pill Bar
    m_segmentedFrame = new QFrame(headerContainer);
    m_segmentedFrame->setObjectName(QStringLiteral("segmentedControl"));
    m_segmentedFrame->setMinimumWidth(80);
    auto segLayout = new QHBoxLayout(m_segmentedFrame);
    segLayout->setContentsMargins(2, 2, 2, 2);
    segLayout->setSpacing(2);

    m_outlineBtn = new QPushButton(Theme::ThemeManager::instance().icon(QStringLiteral("outline"), 14),
                                   QStringLiteral(" 大纲"), m_segmentedFrame);
    m_outlineBtn->setObjectName(QStringLiteral("segmentBtn"));
    m_outlineBtn->setProperty("active", true);

    m_filesBtn = new QPushButton(Theme::ThemeManager::instance().icon(QStringLiteral("folder"), 14),
                                 QStringLiteral(" 文件"), m_segmentedFrame);
    m_filesBtn->setObjectName(QStringLiteral("segmentBtn"));
    m_filesBtn->setProperty("active", false);

    m_recentBtn = new QPushButton(Theme::ThemeManager::instance().icon(QStringLiteral("history"), 14),
                                  QStringLiteral(" 历史"), m_segmentedFrame);
    m_recentBtn->setObjectName(QStringLiteral("segmentBtn"));
    m_recentBtn->setProperty("active", false);

    segLayout->addWidget(m_outlineBtn, 1);
    segLayout->addWidget(m_filesBtn, 1);
    segLayout->addWidget(m_recentBtn, 1);
    headerLayout->addWidget(m_segmentedFrame);

    // Quick filter / search bar
    m_searchEdit = new QLineEdit(headerContainer);
    m_searchEdit->setObjectName(QStringLiteral("sidebarSearch"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索大纲 / 目录 / 历史..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(80);
    headerLayout->addWidget(m_searchEdit);

    mainLayout->addWidget(headerContainer);

    // Stacked Pages
    m_stackWidget = new QStackedWidget(this);

    // Page 0: Outline
    m_outlineWidget = new OutlineWidget(m_stackWidget);
    m_stackWidget->addWidget(m_outlineWidget);

    // Page 1: Files
    m_fileTree = new QTreeView(m_stackWidget);
    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setRootPath(QDir::currentPath());

    auto *proxy = new FileFilterProxyModel(this);
    proxy->setSourceModel(m_fileModel);
    m_fileProxyModel = proxy;

    m_fileTree->setModel(m_fileProxyModel);
    m_fileTree->setRootIndex(m_fileProxyModel->mapFromSource(m_fileModel->index(QDir::currentPath())));
    m_fileTree->header()->hideSection(1);
    m_fileTree->header()->hideSection(2);
    m_fileTree->header()->hideSection(3);
    m_fileTree->setAnimated(true);
    m_fileTree->setIndentation(16);
    m_stackWidget->addWidget(m_fileTree);

    // Page 2: Recent History
    m_recentPage = new QWidget(m_stackWidget);
    auto recentLayout = new QVBoxLayout(m_recentPage);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(0);

    m_recentTree = new QTreeWidget(m_recentPage);
    m_recentTree->setHeaderHidden(true);
    m_recentTree->setColumnCount(1);
    m_recentTree->setAnimated(true);
    m_recentTree->setIndentation(16);
    m_recentTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_recentTree->setMouseTracking(true);
    m_recentTree->viewport()->setMouseTracking(true);

    m_emptyRecentLabel = new QLabel(QStringLiteral("暂无历史记录\n打开或编辑文档后将在此列出"), m_recentPage);
    m_emptyRecentLabel->setAlignment(Qt::AlignCenter);
    m_emptyRecentLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; padding: 32px 16px; line-height: 1.5;")
        .arg(Theme::ThemeManager::instance().palette().secondaryText.name()));

    recentLayout->addWidget(m_recentTree);
    recentLayout->addWidget(m_emptyRecentLabel);
    m_stackWidget->addWidget(m_recentPage);

    mainLayout->addWidget(m_stackWidget, 1);

    // Wire segmented buttons
    connect(m_outlineBtn, &QPushButton::clicked, this, &SidebarWidget::switchToOutline);
    connect(m_filesBtn, &QPushButton::clicked, this, &SidebarWidget::switchToFiles);
    connect(m_recentBtn, &QPushButton::clicked, this, &SidebarWidget::switchToRecent);

    // Wire search filter
    connect(m_searchEdit, &QLineEdit::textChanged, this, &SidebarWidget::onSearchTextChanged);

    // Wire file tree click
    connect(m_fileTree, &QTreeView::clicked, this, [this](const QModelIndex &proxyIndex) {
        QModelIndex srcIndex = m_fileProxyModel->mapToSource(proxyIndex);
        QString filePath = m_fileModel->filePath(srcIndex);
        if (QFileInfo(filePath).isFile()) {
            emit fileSelected(filePath);
        }
    });

    // Wire recent tree click
    connect(m_recentTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item) return;
        QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty()) {
            emit fileSelected(path);
        }
    });
    connect(m_recentTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item) return;
        QString path = item->data(0, Qt::UserRole).toString();
        if (!path.isEmpty()) {
            emit fileSelected(path);
        }
    });
    connect(m_recentTree, &QTreeWidget::customContextMenuRequested, this, &SidebarWidget::onRecentContextMenu);

    // Wire theme changes
    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this, &SidebarWidget::updateThemeStyles);
    updateThemeStyles();
}

void SidebarWidget::switchToOutline() {
    m_stackWidget->setCurrentIndex(0);
    m_outlineBtn->setProperty("active", true);
    m_filesBtn->setProperty("active", false);
    m_recentBtn->setProperty("active", false);
    m_outlineBtn->style()->unpolish(m_outlineBtn);
    m_outlineBtn->style()->polish(m_outlineBtn);
    m_filesBtn->style()->unpolish(m_filesBtn);
    m_filesBtn->style()->polish(m_filesBtn);
    m_recentBtn->style()->unpolish(m_recentBtn);
    m_recentBtn->style()->polish(m_recentBtn);

    m_outlineWidget->filter(m_searchEdit->text());
}

void SidebarWidget::switchToFiles() {
    m_stackWidget->setCurrentIndex(1);
    m_outlineBtn->setProperty("active", false);
    m_filesBtn->setProperty("active", true);
    m_recentBtn->setProperty("active", false);
    m_outlineBtn->style()->unpolish(m_outlineBtn);
    m_outlineBtn->style()->polish(m_outlineBtn);
    m_filesBtn->style()->unpolish(m_filesBtn);
    m_filesBtn->style()->polish(m_filesBtn);
    m_recentBtn->style()->unpolish(m_recentBtn);
    m_recentBtn->style()->polish(m_recentBtn);

    auto *proxy = static_cast<FileFilterProxyModel*>(m_fileProxyModel);
    if (proxy) {
        proxy->setFilterPattern(m_searchEdit->text());
    }
}

void SidebarWidget::switchToRecent() {
    m_stackWidget->setCurrentIndex(2);
    m_outlineBtn->setProperty("active", false);
    m_filesBtn->setProperty("active", false);
    m_recentBtn->setProperty("active", true);
    m_outlineBtn->style()->unpolish(m_outlineBtn);
    m_outlineBtn->style()->polish(m_outlineBtn);
    m_filesBtn->style()->unpolish(m_filesBtn);
    m_filesBtn->style()->polish(m_filesBtn);
    m_recentBtn->style()->unpolish(m_recentBtn);
    m_recentBtn->style()->polish(m_recentBtn);

    filterRecentItems(m_searchEdit->text());
}

void SidebarWidget::setRecentFiles(const QStringList &files) {
    m_recentFiles = files;
    refreshRecentItems();
}

void SidebarWidget::refreshRecentItems() {
    if (!m_recentTree) return;
    m_recentTree->clear();

    if (m_recentFiles.isEmpty()) {
        m_recentTree->hide();
        m_emptyRecentLabel->show();
        return;
    }

    m_emptyRecentLabel->hide();
    m_recentTree->show();

    for (const QString &path : m_recentFiles) {
        QFileInfo fi(path);
        QString nativePath = QDir::toNativeSeparators(fi.exists() ? fi.absoluteFilePath() : path);
        auto *item = new QTreeWidgetItem(m_recentTree);
        item->setIcon(0, Theme::ThemeManager::instance().icon(QStringLiteral("file"), 16));
        item->setText(0, fi.fileName());
        item->setToolTip(0, nativePath);
        item->setStatusTip(0, nativePath);
        item->setData(0, Qt::UserRole, nativePath);
    }

    filterRecentItems(m_searchEdit ? m_searchEdit->text() : QString());
}

void SidebarWidget::filterRecentItems(const QString &text) {
    if (!m_recentTree) return;
    const QString trimmed = text.trimmed();
    int visibleCount = 0;
    for (int i = 0; i < m_recentTree->topLevelItemCount(); ++i) {
        auto *item = m_recentTree->topLevelItem(i);
        bool match = trimmed.isEmpty() ||
                     item->text(0).contains(trimmed, Qt::CaseInsensitive) ||
                     item->data(0, Qt::UserRole).toString().contains(trimmed, Qt::CaseInsensitive);
        item->setHidden(!match);
        if (match) visibleCount++;
    }

    if (!m_recentFiles.isEmpty()) {
        if (visibleCount == 0 && !trimmed.isEmpty()) {
            m_emptyRecentLabel->setText(QStringLiteral("未找到匹配的历史记录"));
            m_emptyRecentLabel->show();
        } else {
            m_emptyRecentLabel->hide();
            m_emptyRecentLabel->setText(QStringLiteral("暂无历史记录\n打开或编辑文档后将在此列出"));
        }
    }
}

void SidebarWidget::onRecentContextMenu(const QPoint &pos) {
    auto *item = m_recentTree->itemAt(pos);
    QMenu menu(this);
    if (item) {
        QString path = item->data(0, Qt::UserRole).toString();
        menu.addAction(Theme::ThemeManager::instance().icon(QStringLiteral("open"), 16),
                       QStringLiteral("打开 (&O)"), [this, path]() {
            emit fileSelected(path);
        });
        menu.addAction(QStringLiteral("在文件资源管理器中显示 (&E)"), [path]() {
            QFileInfo fi(path);
            if (fi.exists()) {
#ifdef _WIN32
                QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
                QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
#endif
            }
        });
        menu.addAction(QStringLiteral("从历史记录中移除 (&R)"), [this, path]() {
            emit removeRecentRequested(path);
        });
        menu.addSeparator();
    }
    auto *clearAct = menu.addAction(QStringLiteral("清空历史记录 (&C)"), [this]() {
        emit clearRecentRequested();
    });
    clearAct->setEnabled(!m_recentFiles.isEmpty());
    menu.exec(m_recentTree->viewport()->mapToGlobal(pos));
}

void SidebarWidget::onSearchTextChanged(const QString &text) {
    if (m_outlineWidget) {
        m_outlineWidget->filter(text);
    }
    auto *proxy = static_cast<FileFilterProxyModel*>(m_fileProxyModel);
    if (proxy) {
        proxy->setFilterPattern(text);
    }
    filterRecentItems(text);
}

void SidebarWidget::setWorkspacePath(const QString &path) {
    m_fileModel->setRootPath(path);
    m_fileTree->setRootIndex(m_fileProxyModel->mapFromSource(m_fileModel->index(path)));
    switchToFiles();
}

QString SidebarWidget::fileFilterPattern() const {
    auto *proxy = static_cast<FileFilterProxyModel*>(m_fileProxyModel);
    return proxy ? proxy->filterPattern() : QString();
}

void SidebarWidget::updateThemeStyles() {
    setStyleSheet(Theme::ThemeManager::instance().sidebarStylesheet());
    if (m_outlineBtn) {
        m_outlineBtn->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("outline"), 14));
    }
    if (m_filesBtn) {
        m_filesBtn->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("folder"), 14));
    }
    if (m_recentBtn) {
        m_recentBtn->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("history"), 14));
    }
    if (m_emptyRecentLabel) {
        m_emptyRecentLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; padding: 32px 16px; line-height: 1.5;")
            .arg(Theme::ThemeManager::instance().palette().secondaryText.name()));
    }
    refreshRecentItems();
}

} // namespace MarkdownTool::UI
