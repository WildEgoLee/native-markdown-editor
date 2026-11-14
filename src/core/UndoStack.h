#pragma once

#include "EditTransaction.h"
#include "TextBuffer.h"

#include <QObject>
#include <QVector>

namespace MarkdownTool::Core {

class UndoStack : public QObject {
    Q_OBJECT

public:
    explicit UndoStack(QObject *parent = nullptr);

    void push(const EditTransaction &transaction, TextBuffer &buffer);
    bool undo(TextBuffer &buffer, SelectionState &outSelection);
    bool redo(TextBuffer &buffer, SelectionState &outSelection);

    bool canUndo() const { return !m_undoList.isEmpty(); }
    bool canRedo() const { return !m_redoList.isEmpty(); }

    void clear();

    void setSaveCheckpoint(quint64 revision);
    bool isDirty(quint64 currentRevision) const;

signals:
    void canUndoChanged(bool canUndo);
    void canRedoChanged(bool canRedo);
    void cleanStateChanged(bool isClean);

private:
    void checkCleanState(quint64 currentRevision);

    QVector<EditTransaction> m_undoList;
    QVector<EditTransaction> m_redoList;

    quint64 m_savedCheckpointRevision = 0;
    bool m_hasSavedCheckpoint = false;
    bool m_wasClean = true;
};

} // namespace MarkdownTool::Core
