#pragma once

#include "Position.h"

#include <QString>
#include <QVector>
#include <QDateTime>

namespace MarkdownTool::Core {

struct SelectionState {
    int anchor = 0;
    int cursor = 0;

    constexpr SelectionState() = default;
    constexpr SelectionState(int a, int c) : anchor(a), cursor(c) {}

    bool hasSelection() const { return anchor != cursor; }
    int selectionStart() const { return qMin(anchor, cursor); }
    int selectionEnd() const { return qMax(anchor, cursor); }
    int selectionLength() const { return qAbs(anchor - cursor); }

    auto operator<=>(const SelectionState&) const = default;
};

struct SingleEdit {
    int offset = 0;
    QString oldText;
    QString newText;

    SingleEdit() = default;
    SingleEdit(int off, const QString &oldT, const QString &newT)
        : offset(off), oldText(oldT), newText(newT) {}
};

class EditTransaction {
public:
    EditTransaction() = default;

    void addEdit(int offset, const QString &oldText, const QString &newText);

    const QVector<SingleEdit> &edits() const { return m_edits; }
    bool isEmpty() const { return m_edits.isEmpty(); }

    void setSelectionBefore(const SelectionState &sel) { m_selectionBefore = sel; }
    const SelectionState &selectionBefore() const { return m_selectionBefore; }

    void setSelectionAfter(const SelectionState &sel) { m_selectionAfter = sel; }
    const SelectionState &selectionAfter() const { return m_selectionAfter; }

    qint64 timestamp() const { return m_timestamp; }
    void setTimestamp(qint64 ts) { m_timestamp = ts; }

    quint64 revisionBefore() const { return m_revisionBefore; }
    void setRevisionBefore(quint64 rev) { m_revisionBefore = rev; }

    quint64 revisionAfter() const { return m_revisionAfter; }
    void setRevisionAfter(quint64 rev) { m_revisionAfter = rev; }

    bool canMergeWith(const EditTransaction &next) const;
    void merge(const EditTransaction &next);

private:
    QVector<SingleEdit> m_edits;
    SelectionState m_selectionBefore;
    SelectionState m_selectionAfter;
    qint64 m_timestamp = 0;
    quint64 m_revisionBefore = 0;
    quint64 m_revisionAfter = 0;
};

} // namespace MarkdownTool::Core
