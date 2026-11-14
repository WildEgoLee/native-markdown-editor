#include "SlashCommandPopup.h"

#include <QVBoxLayout>
#include <QKeyEvent>

namespace MarkdownTool::UI {

SlashCommandPopup::SlashCommandPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating, false);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_listWidget = new QListWidget(this);
    m_listWidget->setFocusPolicy(Qt::NoFocus);
    m_listWidget->setStyleSheet(QStringLiteral(
        "QListWidget { background: #ffffff; border: 1px solid #d0d7de; border-radius: 6px; padding: 4px; font-size: 13px; }"
        "QListWidget::item { padding: 6px 10px; border-radius: 4px; }"
        "QListWidget::item:selected { background: #e8f0fe; color: #1a73e8; font-weight: bold; }"
    ));

    layout->addWidget(m_listWidget);
    setFixedSize(220, 260);

    populateActions();

    connect(m_listWidget, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        int idx = m_listWidget->row(item);
        if (idx >= 0 && idx < m_actions.size()) {
            emit actionTriggered(m_actions[idx]);
            hide();
        }
    });

    installEventFilter(this);
}

void SlashCommandPopup::populateActions() {
    m_actions = {
        {QStringLiteral("标题 1"), QStringLiteral("一级主标题"), QStringLiteral("H1"), QStringLiteral("# "), 0},
        {QStringLiteral("标题 2"), QStringLiteral("二级标题"), QStringLiteral("H2"), QStringLiteral("## "), 0},
        {QStringLiteral("标题 3"), QStringLiteral("三级标题"), QStringLiteral("H3"), QStringLiteral("### "), 0},
        {QStringLiteral("待办事项"), QStringLiteral("任务复选框"), QStringLiteral("☑"), QStringLiteral("- [ ] "), 0},
        {QStringLiteral("无序列表"), QStringLiteral("圆点列表"), QStringLiteral("•"), QStringLiteral("- "), 0},
        {QStringLiteral("有序列表"), QStringLiteral("数字列表"), QStringLiteral("1."), QStringLiteral("1. "), 0},
        {QStringLiteral("代码块"), QStringLiteral("围栏代码块"), QStringLiteral("</>"), QStringLiteral("```cpp\n\n```"), 4},
        {QStringLiteral("引用"), QStringLiteral("引用文本段"), QStringLiteral("❝"), QStringLiteral("> "), 0},
        {QStringLiteral("表格"), QStringLiteral("3列表格"), QStringLiteral("▦"), QStringLiteral("| 列 1 | 列 2 | 列 3 |\n| --- | --- | --- |\n| 内容 | 内容 | 内容 |"), 0},
        {QStringLiteral("分割线"), QStringLiteral("水平线"), QStringLiteral("—"), QStringLiteral("---\n"), 0},
    };

    m_listWidget->clear();
    for (const auto &act : m_actions) {
        auto item = new QListWidgetItem(act.iconText + QStringLiteral("  ") + act.title, m_listWidget);
        item->setToolTip(act.description);
    }
    m_listWidget->setCurrentRow(0);
}

void SlashCommandPopup::showAt(const QPoint &globalPos) {
    m_listWidget->setCurrentRow(0);
    move(globalPos);
    show();
}

void SlashCommandPopup::selectNext() {
    int cur = m_listWidget->currentRow();
    int next = (cur + 1) % m_actions.size();
    m_listWidget->setCurrentRow(next);
}

void SlashCommandPopup::selectPrev() {
    int cur = m_listWidget->currentRow();
    int prev = (cur - 1 + m_actions.size()) % m_actions.size();
    m_listWidget->setCurrentRow(prev);
}

bool SlashCommandPopup::executeSelected() {
    int idx = m_listWidget->currentRow();
    if (idx >= 0 && idx < m_actions.size()) {
        emit actionTriggered(m_actions[idx]);
        hide();
        return true;
    }
    return false;
}

bool SlashCommandPopup::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto kEvent = static_cast<QKeyEvent*>(event);
        if (kEvent->key() == Qt::Key_Escape) {
            hide();
            emit dismissed();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace MarkdownTool::UI
