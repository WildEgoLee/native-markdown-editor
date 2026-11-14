#include "UndoStack.h"

namespace MarkdownTool::Core {

UndoStack::UndoStack(QObject *parent)
    : QObject(parent)
{
}

void UndoStack::clear() {
    m_undoList.clear();
    m_redoList.clear();
    m_savedCheckpointRevision = 0;
    m_hasSavedCheckpoint = false;
    m_wasClean = true;

    emit canUndoChanged(false);
    emit canRedoChanged(false);
    emit cleanStateChanged(true);
}

void UndoStack::push(const EditTransaction &transaction, TextBuffer &buffer) {
    if (transaction.isEmpty()) {
        return;
    }

    EditTransaction tx = transaction;
    tx.setRevisionBefore(buffer.revision());

    // Attempt to merge with top of undo list
    if (!m_undoList.isEmpty() && m_undoList.last().canMergeWith(tx)) {
        // Apply edits to buffer
        for (const auto &edit : tx.edits()) {
            buffer.replace(edit.offset, edit.oldText.length(), edit.newText);
        }
        m_undoList.last().merge(tx);
        m_undoList.last().setRevisionAfter(buffer.revision());
    } else {
        // Apply edits to buffer
        for (const auto &edit : tx.edits()) {
            buffer.replace(edit.offset, edit.oldText.length(), edit.newText);
        }
        tx.setRevisionAfter(buffer.revision());
        m_undoList.append(tx);
        emit canUndoChanged(true);
    }

    if (!m_redoList.isEmpty()) {
        m_redoList.clear();
        emit canRedoChanged(false);
    }

    checkCleanState(buffer.revision());
}

bool UndoStack::undo(TextBuffer &buffer, SelectionState &outSelection) {
    if (m_undoList.isEmpty()) {
        return false;
    }

    EditTransaction tx = m_undoList.takeLast();

    // Revert edits in reverse order
    const auto &edits = tx.edits();
    for (int i = edits.size() - 1; i >= 0; --i) {
        const auto &edit = edits[i];
        buffer.replace(edit.offset, edit.newText.length(), edit.oldText);
    }

    buffer.setRevision(tx.revisionBefore());
    outSelection = tx.selectionBefore();

    m_redoList.append(tx);

    if (m_undoList.isEmpty()) {
        emit canUndoChanged(false);
    }
    emit canRedoChanged(true);

    checkCleanState(buffer.revision());
    return true;
}

bool UndoStack::redo(TextBuffer &buffer, SelectionState &outSelection) {
    if (m_redoList.isEmpty()) {
        return false;
    }

    EditTransaction tx = m_redoList.takeLast();

    // Re-apply edits in forward order
    const auto &edits = tx.edits();
    for (int i = 0; i < edits.size(); ++i) {
        const auto &edit = edits[i];
        buffer.replace(edit.offset, edit.oldText.length(), edit.newText);
    }

    buffer.setRevision(tx.revisionAfter());
    outSelection = tx.selectionAfter();

    m_undoList.append(tx);

    if (m_redoList.isEmpty()) {
        emit canRedoChanged(false);
    }
    emit canUndoChanged(true);

    checkCleanState(buffer.revision());
    return true;
}

void UndoStack::setSaveCheckpoint(quint64 revision) {
    m_savedCheckpointRevision = revision;
    m_hasSavedCheckpoint = true;
    checkCleanState(revision);
}

bool UndoStack::isDirty(quint64 currentRevision) const {
    if (!m_hasSavedCheckpoint) {
        return currentRevision > 0;
    }
    return currentRevision != m_savedCheckpointRevision;
}

void UndoStack::checkCleanState(quint64 currentRevision) {
    bool clean = !isDirty(currentRevision);
    if (clean != m_wasClean) {
        m_wasClean = clean;
        emit cleanStateChanged(clean);
    }
}

} // namespace MarkdownTool::Core
