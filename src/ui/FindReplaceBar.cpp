#include "FindReplaceBar.h"
#include "theme/Theme.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>

namespace MarkdownTool::UI {

FindReplaceBar::FindReplaceBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("findReplaceBar"));
    setStyleSheet(Theme::ThemeManager::instance().findBarStylesheet());
    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this, [this]() {
        setStyleSheet(Theme::ThemeManager::instance().findBarStylesheet());
    });

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 4, 8, 4);
    mainLayout->setSpacing(4);

    // Row 1: Find row
    auto findRow = new QHBoxLayout();
    findRow->setSpacing(6);

    m_findEdit = new QLineEdit(this);
    m_findEdit->setPlaceholderText(QStringLiteral("查找内容 (按 Enter 查找下一个)..."));
    findRow->addWidget(m_findEdit, 1);

    auto btnPrev = new QPushButton(QStringLiteral("↑ 上一个"), this);
    auto btnNext = new QPushButton(QStringLiteral("↓ 下一个"), this);
    findRow->addWidget(btnPrev);
    findRow->addWidget(btnNext);

    m_caseCheckBox = new QCheckBox(QStringLiteral("区分大小写"), this);
    findRow->addWidget(m_caseCheckBox);

    auto btnClose = new QPushButton(QStringLiteral("✕"), this);
    btnClose->setFixedWidth(28);
    findRow->addWidget(btnClose);

    mainLayout->addLayout(findRow);

    // Row 2: Replace row
    m_replaceRow = new QWidget(this);
    auto replaceLayout = new QHBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->setSpacing(6);

    m_replaceEdit = new QLineEdit(m_replaceRow);
    m_replaceEdit->setPlaceholderText(QStringLiteral("替换为..."));
    replaceLayout->addWidget(m_replaceEdit, 1);

    auto btnReplace = new QPushButton(QStringLiteral("替换"), m_replaceRow);
    auto btnReplaceAll = new QPushButton(QStringLiteral("全部替换"), m_replaceRow);
    replaceLayout->addWidget(btnReplace);
    replaceLayout->addWidget(btnReplaceAll);

    mainLayout->addWidget(m_replaceRow);
    m_replaceRow->hide();

    // Wiring
    connect(btnNext, &QPushButton::clicked, this, [this]() {
        emit findNext(m_findEdit->text(), m_caseCheckBox->isChecked());
    });
    connect(btnPrev, &QPushButton::clicked, this, [this]() {
        emit findPrev(m_findEdit->text(), m_caseCheckBox->isChecked());
    });
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this]() {
        emit findNext(m_findEdit->text(), m_caseCheckBox->isChecked());
    });

    connect(btnReplace, &QPushButton::clicked, this, [this]() {
        emit replaceOne(m_findEdit->text(), m_replaceEdit->text(), m_caseCheckBox->isChecked());
    });
    connect(btnReplaceAll, &QPushButton::clicked, this, [this]() {
        emit replaceAll(m_findEdit->text(), m_replaceEdit->text(), m_caseCheckBox->isChecked());
    });

    connect(btnClose, &QPushButton::clicked, this, [this]() {
        hide();
        emit closed();
    });

    installEventFilter(this);
}

void FindReplaceBar::openFind() {
    m_replaceRow->hide();
    show();
    m_findEdit->selectAll();
    m_findEdit->setFocus();
}

void FindReplaceBar::openReplace() {
    m_replaceRow->show();
    show();
    m_findEdit->selectAll();
    m_findEdit->setFocus();
}

bool FindReplaceBar::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            hide();
            emit closed();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace MarkdownTool::UI
