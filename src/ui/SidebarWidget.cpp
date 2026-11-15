#include "SidebarWidget.h"
#include "theme/Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDir>

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

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(6);

    // Header container with Segmented Control and Search Input
    auto headerContainer = new QWidget(this);
    auto headerLayout = new QVBoxLayout(headerContainer);
    headerLayout->setContentsMargins(10, 10, 10, 4);
    headerLayout->setSpacing(8);

    // Segmented Control Pill Bar
    m_segmentedFrame = new QFrame(headerContainer);
    m_segmentedFrame->setObjectName(QStringLiteral("segmentedControl"));
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

    segLayout->addWidget(m_outlineBtn, 1);
    segLayout->addWidget(m_filesBtn, 1);
    headerLayout->addWidget(m_segmentedFrame);

    // Quick filter / search bar
    m_searchEdit = new QLineEdit(headerContainer);
    m_searchEdit->setObjectName(QStringLiteral("sidebarSearch"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索大纲 / 文件..."));
    m_searchEdit->setClearButtonEnabled(true);
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

    mainLayout->addWidget(m_stackWidget, 1);

    // Wire segmented buttons
    connect(m_outlineBtn, &QPushButton::clicked, this, &SidebarWidget::switchToOutline);
    connect(m_filesBtn, &QPushButton::clicked, this, &SidebarWidget::switchToFiles);

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

    // Wire theme changes
    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this, &SidebarWidget::updateThemeStyles);
    updateThemeStyles();
}

void SidebarWidget::switchToOutline() {
    m_stackWidget->setCurrentIndex(0);
    m_outlineBtn->setProperty("active", true);
    m_filesBtn->setProperty("active", false);
    m_outlineBtn->style()->unpolish(m_outlineBtn);
    m_outlineBtn->style()->polish(m_outlineBtn);
    m_filesBtn->style()->unpolish(m_filesBtn);
    m_filesBtn->style()->polish(m_filesBtn);

    m_outlineWidget->filter(m_searchEdit->text());
}

void SidebarWidget::switchToFiles() {
    m_stackWidget->setCurrentIndex(1);
    m_outlineBtn->setProperty("active", false);
    m_filesBtn->setProperty("active", true);
    m_outlineBtn->style()->unpolish(m_outlineBtn);
    m_outlineBtn->style()->polish(m_outlineBtn);
    m_filesBtn->style()->unpolish(m_filesBtn);
    m_filesBtn->style()->polish(m_filesBtn);

    auto *proxy = static_cast<FileFilterProxyModel*>(m_fileProxyModel);
    if (proxy) {
        proxy->setFilterPattern(m_searchEdit->text());
    }
}

void SidebarWidget::onSearchTextChanged(const QString &text) {
    if (m_stackWidget->currentIndex() == 0) {
        m_outlineWidget->filter(text);
    } else {
        auto *proxy = static_cast<FileFilterProxyModel*>(m_fileProxyModel);
        if (proxy) {
            proxy->setFilterPattern(text);
        }
    }
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
    m_outlineBtn->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("outline"), 14));
    m_filesBtn->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("folder"), 14));
}

} // namespace MarkdownTool::UI
