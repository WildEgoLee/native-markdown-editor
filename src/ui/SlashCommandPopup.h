#pragma once

#include <QWidget>
#include <QListWidget>

namespace MarkdownTool::UI {

struct SlashAction {
    QString title;
    QString description;
    QString iconText;
    QString snippet;
    int cursorOffsetFromEnd = 0;
};

class SlashCommandPopup : public QWidget {
    Q_OBJECT

public:
    explicit SlashCommandPopup(QWidget *parent = nullptr);

    void showAt(const QPoint &globalPos);
    void selectNext();
    void selectPrev();
    bool executeSelected();

signals:
    void actionTriggered(const SlashAction &action);
    void dismissed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void populateActions();

    QListWidget *m_listWidget = nullptr;
    QVector<SlashAction> m_actions;
};

} // namespace MarkdownTool::UI
