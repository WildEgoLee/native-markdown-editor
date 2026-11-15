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
    m_findEdit->setObjectName(QStringLiteral("findEdit"));
    m_findEdit->setPlaceholderText(QStringLiteral("查找内容 (按 Enter 查找下一个)..."));
    findRow->addWidget(m_findEdit, 1);

    m_matchCountLabel = new QLabel(this);
    m_matchCountLabel->setObjectName(QStringLiteral("matchCountLabel"));
    findRow->addWidget(m_matchCountLabel);

    auto btnPrev = new QPushButton(QStringLiteral("↑ 上一个"), this);
    btnPrev->setObjectName(QStringLiteral("btnPrev"));
    auto btnNext = new QPushButton(QStringLiteral("↓ 下一个"), this);
    btnNext->setObjectName(QStringLiteral("btnNext"));
    findRow->addWidget(btnPrev);
    findRow->addWidget(btnNext);

    m_caseCheckBox = new QCheckBox(QStringLiteral("区分大小写"), this);
    m_caseCheckBox->setObjectName(QStringLiteral("caseCheckBox"));
    findRow->addWidget(m_caseCheckBox);

    auto btnClose = new QPushButton(QStringLiteral("✕"), this);
    btnClose->setObjectName(QStringLiteral("btnClose"));
    btnClose->setFixedWidth(28);
    findRow->addWidget(btnClose);

    mainLayout->addLayout(findRow);

    // Row 2: Replace row
    m_replaceRow = new QWidget(this);
    m_replaceRow->setObjectName(QStringLiteral("replaceRow"));
    auto replaceLayout = new QHBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->setSpacing(6);

    m_replaceEdit = new QLineEdit(m_replaceRow);
    m_replaceEdit->setObjectName(QStringLiteral("replaceEdit"));
    m_replaceEdit->setPlaceholderText(QStringLiteral("替换为..."));
    replaceLayout->addWidget(m_replaceEdit, 1);

    auto btnReplace = new QPushButton(QStringLiteral("替换"), m_replaceRow);
    btnReplace->setObjectName(QStringLiteral("btnReplace"));
    auto btnReplaceAll = new QPushButton(QStringLiteral("全部替换"), m_replaceRow);
    btnReplaceAll->setObjectName(QStringLiteral("btnReplaceAll"));
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

    connect(m_findEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        emit findParamsChanged(text, m_caseCheckBox->isChecked());
    });
    connect(m_caseCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        emit findParamsChanged(m_findEdit->text(), checked);
    });

    m_findEdit->installEventFilter(this);
    m_replaceEdit->installEventFilter(this);
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

QString FindReplaceBar::findText() const {
    return m_findEdit ? m_findEdit->text() : QString();
}

bool FindReplaceBar::matchCase() const {
    return m_caseCheckBox ? m_caseCheckBox->isChecked() : false;
}

void FindReplaceBar::setMatchStatus(int current, int total) {
    if (!m_matchCountLabel) {
        return;
    }
    if (findText().isEmpty()) {
        m_matchCountLabel->setText(QString());
    } else if (total == 0) {
        m_matchCountLabel->setText(QStringLiteral("无结果"));
    } else {
        m_matchCountLabel->setText(QStringLiteral("%1/%2").arg(current).arg(total));
    }
}

bool FindReplaceBar::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            hide();
            emit closed();
            return true;
        }
        if (watched == m_findEdit && (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)) {
            if (ke->modifiers() & Qt::ShiftModifier) {
                emit findPrev(m_findEdit->text(), m_caseCheckBox->isChecked());
            } else {
                emit findNext(m_findEdit->text(), m_caseCheckBox->isChecked());
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace MarkdownTool::UI
