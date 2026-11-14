#include "UndoStack.h"

namespace MarkdownTool::Core {

UndoStack::UndoStack(QObject *parent)
    : QObject(parent)
{
}

void UndoStack::clear() {
    m_commands.clear();
    m_index = 0;
    m_cleanIndex = 0;
    m_mergeGroupActive = true;
    m_wasClean = true;

    emit canUndoChanged(false);
    emit canRedoChanged(false);
    emit cleanStateChanged(true);
}

void UndoStack::push(const EditTransaction &transaction, TextBuffer &buffer) {
    if (transaction.isEmpty()) {
        return;
    }

    // Try to merge if merge group is active and cursor is at end of stack
    if (m_mergeGroupActive && m_index > 0 && m_index == m_commands.size() && m_commands[m_index - 1].canMergeWith(transaction)) {
        for (const auto &edit : transaction.edits()) {
            buffer.replace(edit.offset, edit.oldText.length(), edit.newText);
        }
        m_commands[m_index - 1].merge(transaction);
    } else {
        // Discard future redo branch if branching from past undo state
        if (m_index < m_commands.size()) {
            if (m_cleanIndex > m_index) {
                // Checkpoint was on the discarded branch, can never return to it
                m_cleanIndex = -1;
            }
            m_commands.resize(m_index);
        }

        // Apply edits to buffer
        for (const auto &edit : transaction.edits()) {
            buffer.replace(edit.offset, edit.oldText.length(), edit.newText);
        }

        m_commands.append(transaction);
        ++m_index;
        m_mergeGroupActive = true;
    }

    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
    checkCleanState();
}

bool UndoStack::undo(TextBuffer &buffer, SelectionState &outSelection) {
    if (m_index <= 0) {
        return false;
    }

    breakMergeGroup();

    const EditTransaction &tx = m_commands[m_index - 1];

    // Revert edits in reverse order
    const auto &edits = tx.edits();
    for (int i = edits.size() - 1; i >= 0; --i) {
        const auto &edit = edits[i];
        buffer.replace(edit.offset, edit.newText.length(), edit.oldText);
    }

    outSelection = tx.selectionBefore();
    --m_index;

    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
    checkCleanState();
    return true;
}

bool UndoStack::redo(TextBuffer &buffer, SelectionState &outSelection) {
    if (m_index >= m_commands.size()) {
        return false;
    }

    breakMergeGroup();

    const EditTransaction &tx = m_commands[m_index];

    // Re-apply edits in forward order
    const auto &edits = tx.edits();
    for (int i = 0; i < edits.size(); ++i) {
        const auto &edit = edits[i];
        buffer.replace(edit.offset, edit.oldText.length(), edit.newText);
    }

    outSelection = tx.selectionAfter();
    ++m_index;

    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
    checkCleanState();
    return true;
}

void UndoStack::setSaveCheckpoint() {
    m_cleanIndex = m_index;
    breakMergeGroup();
    checkCleanState();
}

void UndoStack::breakMergeGroup() {
    m_mergeGroupActive = false;
}

bool UndoStack::isDirty() const {
    return m_index != m_cleanIndex;
}

void UndoStack::checkCleanState() {
    bool clean = !isDirty();
    if (clean != m_wasClean) {
        m_wasClean = clean;
        emit cleanStateChanged(clean);
    }
}

} // namespace MarkdownTool::Core
