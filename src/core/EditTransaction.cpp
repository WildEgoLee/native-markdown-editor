#include "EditTransaction.h"

namespace MarkdownTool::Core {

void EditTransaction::addEdit(int offset, const QString &oldText, const QString &newText) {
    m_edits.append(SingleEdit(offset, oldText, newText));
}

bool EditTransaction::canMergeWith(const EditTransaction &next) const {
    if (m_edits.size() != 1 || next.m_edits.size() != 1) {
        return false;
    }

    const SingleEdit &e1 = m_edits.first();
    const SingleEdit &e2 = next.m_edits.first();

    // Only merge contiguous single-character typing (within 1000ms)
    if (next.timestamp() - m_timestamp > 1000) {
        return false;
    }

    // Forward single character insertions
    if (e1.oldText.isEmpty() && e2.oldText.isEmpty()) {
        if (!e1.newText.contains(u'\n') && !e2.newText.contains(u'\n')) {
            if (e1.offset + e1.newText.length() == e2.offset) {
                return true;
            }
        }
    }

    // Backward single character deletions (Backspace)
    if (e1.newText.isEmpty() && e2.newText.isEmpty()) {
        if (!e1.oldText.contains(u'\n') && !e2.oldText.contains(u'\n')) {
            if (e2.offset + e2.oldText.length() == e1.offset) {
                return true;
            }
        }
    }

    return false;
}

void EditTransaction::merge(const EditTransaction &next) {
    if (m_edits.size() != 1 || next.m_edits.size() != 1) {
        return;
    }

    SingleEdit &e1 = m_edits[0];
    const SingleEdit &e2 = next.m_edits[0];

    if (e1.oldText.isEmpty() && e2.oldText.isEmpty()) {
        // Appending text
        e1.newText.append(e2.newText);
    } else if (e1.newText.isEmpty() && e2.newText.isEmpty()) {
        // Prepending deleted text for backspace
        e1.offset = e2.offset;
        e1.oldText.prepend(e2.oldText);
    }

    m_selectionAfter = next.m_selectionAfter;
    m_timestamp = next.m_timestamp;
}

} // namespace MarkdownTool::Core
