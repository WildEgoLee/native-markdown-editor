#include "SidebarWidget.h"
#include "theme/Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>

namespace MarkdownTool::UI {

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
    m_fileModel->setNameFilters({QStringLiteral("*.md"), QStringLiteral("*.markdown"), QStringLiteral("*.txt")});
    m_fileModel->setNameFilterDisables(false);
    m_fileTree->setModel(m_fileModel);
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
    connect(m_fileTree, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        QString filePath = m_fileModel->filePath(index);
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
}

void SidebarWidget::onSearchTextChanged(const QString &text) {
    if (m_stackWidget->currentIndex() == 0) {
        m_outlineWidget->filter(text);
    } else {
        if (text.trimmed().isEmpty()) {
            m_fileModel->setNameFilters({QStringLiteral("*.md"), QStringLiteral("*.markdown"), QStringLiteral("*.txt")});
        } else {
            m_fileModel->setNameFilters({QStringLiteral("*%1*.md").arg(text.trimmed()),
                                         QStringLiteral("*%1*.markdown").arg(text.trimmed()),
                                         QStringLiteral("*%1*.txt").arg(text.trimmed())});
        }
    }
}

void SidebarWidget::setWorkspacePath(const QString &path) {
    m_fileModel->setRootPath(path);
    m_fileTree->setRootIndex(m_fileModel->index(path));
    switchToFiles();
}

void SidebarWidget::updateThemeStyles() {
    setStyleSheet(Theme::ThemeManager::instance().sidebarStylesheet());
    m_outlineBtn->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("outline"), 14));
    m_filesBtn->setIcon(Theme::ThemeManager::instance().icon(QStringLiteral("folder"), 14));
}

} // namespace MarkdownTool::UI
