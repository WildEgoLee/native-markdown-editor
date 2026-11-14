#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>

namespace MarkdownTool::UI {

class FindReplaceBar : public QWidget {
    Q_OBJECT

public:
    explicit FindReplaceBar(QWidget *parent = nullptr);

    void openFind();
    void openReplace();

signals:
    void findNext(const QString &text, bool matchCase);
    void findPrev(const QString &text, bool matchCase);
    void replaceOne(const QString &findText, const QString &replaceText, bool matchCase);
    void replaceAll(const QString &findText, const QString &replaceText, bool matchCase);
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLineEdit *m_findEdit = nullptr;
    QLineEdit *m_replaceEdit = nullptr;
    QWidget *m_replaceRow = nullptr;
    QCheckBox *m_caseCheckBox = nullptr;
    QLabel *m_matchCountLabel = nullptr;
};

} // namespace MarkdownTool::UI
