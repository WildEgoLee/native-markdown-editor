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

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < m_commands.size(); }

    void clear(bool startAsDirty = false);
    void markDirty();

    void setSaveCheckpoint();
    void breakMergeGroup();
    bool isDirty() const;

    int index() const { return m_index; }
    int cleanIndex() const { return m_cleanIndex; }

signals:
    void canUndoChanged(bool canUndo);
    void canRedoChanged(bool canRedo);
    void cleanStateChanged(bool isClean);

private:
    void checkCleanState();

    QVector<EditTransaction> m_commands;
    int m_index = 0;
    int m_cleanIndex = 0;
    bool m_mergeGroupActive = true;
    bool m_wasClean = true;
};

} // namespace MarkdownTool::Core
