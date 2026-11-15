#include "NativeEditorView.h"
#include "ui/SlashCommandPopup.h"
#include "render/AsyncRenderService.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include "theme/Theme.h"
#include <algorithm>

namespace MarkdownTool::Editor {

NativeEditorView::NativeEditorView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(false);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(500);
    connect(m_blinkTimer, &QTimer::timeout, this, &NativeEditorView::onBlinkTimer);
    m_blinkTimer->start();

    auto onVisualRefresh = [this]() {
        m_projection.buildProjection(m_rootNode, m_buffer, m_cursorPos, m_isSourceMode);
        m_layoutEngine.layout(m_projection, viewport()->width());
        updateScrollBars();
        viewport()->update();
    };
    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this, onVisualRefresh);
    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::typographyChanged, this, onVisualRefresh);

    m_slashPopup = new UI::SlashCommandPopup(this);
    connect(m_slashPopup, &UI::SlashCommandPopup::actionTriggered, this, [this](const UI::SlashAction &action) {
        // Replace slash character with snippet
        if (m_cursorPos > 0 && m_buffer.charAt(m_cursorPos - 1) == u'/') {
            executeEdit(m_cursorPos - 1, 1, action.snippet);
            if (action.cursorOffsetFromEnd > 0) {
                setCursorPos(m_cursorPos - action.cursorOffsetFromEnd);
            }
        }
    });

    connect(&m_buffer, &Core::TextBuffer::textChanged, this, [this](int offset, int removed, int added) {
        reparseAndRelayout(offset, removed, added);
        emit textChanged();
    });

    connect(&m_buffer, &Core::TextBuffer::textReset, this, [this]() {
        m_cursorPos = 0;
        m_selectionAnchor = 0;
        m_cursors = {Core::SelectionState(0, 0)};
        reparseAndRelayout();
        emit textChanged();
    });

    connect(Render::AsyncRenderService::instance(), &Render::AsyncRenderService::renderReady, this, [this]() {
        viewport()->update();
    });

    m_cursors = {Core::SelectionState(0, 0)};
    reparseAndRelayout();
}

NativeEditorView::~NativeEditorView() = default;

bool NativeEditorView::hasSelection() const {
    if (m_cursors.isEmpty()) {
        return m_cursorPos != m_selectionAnchor;
    }
    for (const auto &c : m_cursors) {
        if (c.hasSelection()) return true;
    }
    return false;
}

void NativeEditorView::syncPrimaryCursor() {
    if (m_cursors.isEmpty()) {
        m_cursors.append(Core::SelectionState(m_selectionAnchor, m_cursorPos));
    } else {
        m_cursorPos = m_cursors.first().cursor;
        m_selectionAnchor = m_cursors.first().anchor;
    }
}

void NativeEditorView::normalizeCursors() {
    if (m_cursors.isEmpty()) {
        m_cursors.append(Core::SelectionState(m_selectionAnchor, m_cursorPos));
        return;
    }

    int bufLen = m_buffer.length();
    for (auto &c : m_cursors) {
        c.cursor = qBound(0, c.cursor, bufLen);
        c.anchor = qBound(0, c.anchor, bufLen);
    }

    std::sort(m_cursors.begin(), m_cursors.end(), [](const Core::SelectionState &a, const Core::SelectionState &b) {
        if (a.selectionStart() != b.selectionStart()) {
            return a.selectionStart() < b.selectionStart();
        }
        return a.selectionEnd() < b.selectionEnd();
    });

    QVector<Core::SelectionState> merged;
    for (const auto &c : m_cursors) {
        if (merged.isEmpty()) {
            merged.append(c);
            continue;
        }
        auto &last = merged.last();
        if (!last.hasSelection() && !c.hasSelection()) {
            if (last.cursor == c.cursor) {
                continue;
            }
        } else {
            if (c.selectionStart() <= last.selectionEnd()) {
                int newStart = qMin(last.selectionStart(), c.selectionStart());
                int newEnd = qMax(last.selectionEnd(), c.selectionEnd());
                bool forward = c.cursor >= c.anchor;
                last.anchor = forward ? newStart : newEnd;
                last.cursor = forward ? newEnd : newStart;
                continue;
            }
        }
        merged.append(c);
    }

    m_cursors = merged;
    syncPrimaryCursor();
}

void NativeEditorView::clearSecondaryCursors() {
    if (m_cursors.size() > 1) {
        m_cursors.resize(1);
    }
    syncPrimaryCursor();
    viewport()->update();
}

void NativeEditorView::addCursor(int pos, int anchor) {
    if (anchor < 0) {
        anchor = pos;
    }
    int bufLen = m_buffer.length();
    pos = qBound(0, pos, bufLen);
    anchor = qBound(0, anchor, bufLen);
    if (!m_isSourceMode) {
        pos = m_projection.normalizeSourceCursor(pos, true);
        anchor = m_projection.normalizeSourceCursor(anchor, true);
    }
    m_cursors.append(Core::SelectionState(anchor, pos));
    normalizeCursors();
    viewport()->update();
}

void NativeEditorView::setCursors(const QVector<Core::SelectionState> &cursors) {
    m_cursors = cursors;
    if (m_cursors.isEmpty()) {
        m_cursors.append(Core::SelectionState(m_selectionAnchor, m_cursorPos));
    }
    normalizeCursors();
    viewport()->update();
}

void NativeEditorView::addCursorAbove() {
    if (m_cursors.isEmpty()) {
        syncPrimaryCursor();
    }
    int topOffset = m_buffer.length() + 1;
    Core::SelectionState topSel;
    for (const auto &c : m_cursors) {
        if (c.selectionStart() < topOffset) {
            topOffset = c.selectionStart();
            topSel = c;
        }
    }

    auto lc = m_buffer.offsetToLineCol(topSel.cursor);
    if (lc.line > 0) {
        int targetLine = lc.line - 1;
        int targetCol = lc.col;
        int targetCursor = m_buffer.lineColToOffset(targetLine, targetCol);
        int targetAnchor = targetCursor;
        if (topSel.hasSelection()) {
            auto alc = m_buffer.offsetToLineCol(topSel.anchor);
            targetAnchor = m_buffer.lineColToOffset(targetLine, alc.col);
        }
        m_cursors.append(Core::SelectionState(targetAnchor, targetCursor));
        normalizeCursors();
        viewport()->update();
    }
}

void NativeEditorView::addCursorBelow() {
    if (m_cursors.isEmpty()) {
        syncPrimaryCursor();
    }
    int botOffset = -1;
    Core::SelectionState botSel;
    for (const auto &c : m_cursors) {
        if (c.selectionEnd() > botOffset) {
            botOffset = c.selectionEnd();
            botSel = c;
        }
    }

    auto lc = m_buffer.offsetToLineCol(botSel.cursor);
    if (lc.line + 1 < m_buffer.lineCount()) {
        int targetLine = lc.line + 1;
        int targetCol = lc.col;
        int targetCursor = m_buffer.lineColToOffset(targetLine, targetCol);
        int targetAnchor = targetCursor;
        if (botSel.hasSelection()) {
            auto alc = m_buffer.offsetToLineCol(botSel.anchor);
            targetAnchor = m_buffer.lineColToOffset(targetLine, alc.col);
        }
        m_cursors.append(Core::SelectionState(targetAnchor, targetCursor));
        normalizeCursors();
        viewport()->update();
    }
}

void NativeEditorView::onBlinkTimer() {
    m_cursorVisible = !m_cursorVisible;
    if (hasFocus()) {
        viewport()->update();
    }
}

void NativeEditorView::setSourceMode(bool enabled) {
    if (m_isSourceMode != enabled) {
        m_isSourceMode = enabled;
        reparseAndRelayout();
        emit modeChanged(m_isSourceMode);
    }
}

void NativeEditorView::setCursorPos(int pos, bool keepAnchor) {
    int clamped = qBound(0, pos, m_buffer.length());
    if (!m_isSourceMode) {
        clamped = m_projection.normalizeSourceCursor(clamped, clamped >= m_cursorPos);
    }
    if (m_cursorPos != clamped || (!keepAnchor && m_selectionAnchor != clamped)) {
        m_cursorPos = clamped;
        if (!keepAnchor) {
            m_selectionAnchor = clamped;
            m_cursors = {Core::SelectionState(m_selectionAnchor, m_cursorPos)};
        } else {
            if (m_cursors.isEmpty()) {
                m_cursors.append(Core::SelectionState(m_selectionAnchor, m_cursorPos));
            } else {
                m_cursors[0] = Core::SelectionState(m_selectionAnchor, m_cursorPos);
            }
        }

        m_cursorVisible = true;
        ensureCursorVisible();

        // In live preview mode, cursor moving into/out of a syntax node may change active markers
        if (!m_isSourceMode) {
            bool changed = m_projection.buildProjection(m_rootNode, m_buffer, m_cursorPos, m_isSourceMode);
            if (changed) {
                m_layoutEngine.layout(m_projection, viewport()->width());
                updateScrollBars();
            }
        }

        viewport()->update();

        auto lc = m_buffer.offsetToLineCol(m_cursorPos);
        emit cursorMoved(lc.line1(), lc.col1(), m_cursorPos);
    }
}

void NativeEditorView::selectAll() {
    m_selectionAnchor = 0;
    m_cursorPos = m_buffer.length();
    m_cursors = {Core::SelectionState(m_selectionAnchor, m_cursorPos)};
    viewport()->update();
}

void NativeEditorView::reparseAndRelayout(int editOffset, int lengthRemoved, int lengthAdded) {
    int childIdx = -1;
    bool isIncrementalAst = false;
    if (editOffset >= 0 && m_rootNode &&
        m_parser.canIncrementalParse(m_rootNode, m_buffer, editOffset, lengthRemoved, lengthAdded, childIdx))
    {
        bool incSuccess = false;
        m_rootNode = m_parser.incrementalParse(m_rootNode, m_buffer, editOffset, lengthRemoved, lengthAdded, childIdx, &incSuccess);
        if (incSuccess) {
            m_incrementalParseHits++;
            isIncrementalAst = true;
        } else {
            m_fullParseFallbacks++;
        }
    } else {
        m_rootNode = m_parser.parse(m_buffer);
        m_fullParseFallbacks++;
    }

    int projEditOffset = isIncrementalAst ? editOffset : -1;
    m_projection.buildProjection(m_rootNode, m_buffer, m_cursorPos, m_isSourceMode, projEditOffset, lengthRemoved, lengthAdded);
    m_layoutEngine.layout(m_projection, qMax(200, viewport()->width()));
    updateScrollBars();
    viewport()->update();

    auto lc = m_buffer.offsetToLineCol(m_cursorPos);
    emit cursorMoved(lc.line1(), lc.col1(), m_cursorPos);
}

void NativeEditorView::updateScrollBars() {
    int maxScrollY = qMax(0, static_cast<int>(m_layoutEngine.totalHeight()) - viewport()->height());
    verticalScrollBar()->setRange(0, maxScrollY);
    verticalScrollBar()->setPageStep(viewport()->height());
    int scrollLines = qMax(1, QApplication::wheelScrollLines());
    verticalScrollBar()->setSingleStep(static_cast<int>(scrollLines * 26.0));
}

void NativeEditorView::ensureCursorVisible() {
    QRectF cRect = m_layoutEngine.cursorRect(m_cursorPos, m_projection);
    int curY = static_cast<int>(cRect.top());
    int curH = static_cast<int>(cRect.height());

    int scrollVal = verticalScrollBar()->value();
    int viewH = viewport()->height();

    if (curY < scrollVal) {
        verticalScrollBar()->setValue(qMax(0, curY - 20));
    } else if (curY + curH > scrollVal + viewH) {
        verticalScrollBar()->setValue(curY + curH - viewH + 20);
    }
}

void NativeEditorView::executeEdit(int offset, int removeLength, const QString &insertText) {
    Core::EditTransaction tx;
    tx.setSelectionBefore(Core::SelectionState(m_selectionAnchor, m_cursorPos));

    QString oldText = m_buffer.substring(offset, removeLength);
    tx.addEdit(offset, oldText, insertText);
    tx.setTimestamp(QDateTime::currentMSecsSinceEpoch());

    int newCursor = offset + insertText.length();
    tx.setSelectionAfter(Core::SelectionState(newCursor, newCursor));

    // Crucial: update cursor position BEFORE pushing to undo stack so that when
    // textChanged triggers reparseAndRelayout(), m_cursorPos is ALREADY the updated cursor!
    m_cursorPos = newCursor;
    m_selectionAnchor = newCursor;
    m_cursors = {Core::SelectionState(newCursor, newCursor)};

    m_undoStack.push(tx, m_buffer);

    ensureCursorVisible();
}

void NativeEditorView::executeMultiEdit(const QString &insertText) {
    if (m_cursors.size() <= 1) {
        int s = qMin(m_cursorPos, m_selectionAnchor);
        int len = qAbs(m_cursorPos - m_selectionAnchor);
        executeEdit(s, len, insertText);
        return;
    }

    normalizeCursors();
    QVector<Core::SelectionState> cursorsAsc = m_cursors;
    QVector<Core::SelectionState> newCursors;
    int cumulativeShift = 0;

    for (int i = 0; i < cursorsAsc.size(); ++i) {
        int s = cursorsAsc[i].selectionStart();
        int len = cursorsAsc[i].selectionLength();
        int newPos = s + cumulativeShift + insertText.length();
        newCursors.append(Core::SelectionState(newPos, newPos));
        cumulativeShift += insertText.length() - len;
    }

    Core::EditTransaction tx;
    tx.setSelectionsBefore(m_cursors);
    tx.setSelectionsAfter(newCursors);
    tx.setTimestamp(QDateTime::currentMSecsSinceEpoch());

    for (int i = cursorsAsc.size() - 1; i >= 0; --i) {
        int s = cursorsAsc[i].selectionStart();
        int len = cursorsAsc[i].selectionLength();
        QString oldText = m_buffer.substring(s, len);
        tx.addEdit(s, oldText, insertText);
    }

    m_cursors = newCursors;
    syncPrimaryCursor();
    m_undoStack.push(tx, m_buffer);
    ensureCursorVisible();
}

void NativeEditorView::executeMultiBackspace() {
    if (m_cursors.size() <= 1) {
        handleBackspaceKey();
        return;
    }

    normalizeCursors();
    if (hasSelection()) {
        executeMultiEdit(QString());
        return;
    }

    QVector<Core::SelectionState> cursorsAsc = m_cursors;
    QVector<Core::SelectionState> newCursors;
    int cumulativeShift = 0;

    struct BackspaceEdit {
        int offset;
        int len;
        QString oldText;
    };
    QVector<BackspaceEdit> editsAsc;

    for (int i = 0; i < cursorsAsc.size(); ++i) {
        int pos = cursorsAsc[i].cursor;
        if (pos <= 0) {
            int newPos = pos + cumulativeShift;
            newCursors.append(Core::SelectionState(newPos, newPos));
            continue;
        }

        int prevPos = Core::PositionTranslator::prevGraphemeOffset(m_buffer.text(), pos);
        int delLen = pos - prevPos;
        QString oldText = m_buffer.substring(prevPos, delLen);

        int newPos = prevPos + cumulativeShift;
        newCursors.append(Core::SelectionState(newPos, newPos));
        cumulativeShift -= delLen;

        editsAsc.append(BackspaceEdit{prevPos, delLen, oldText});
    }

    if (editsAsc.isEmpty()) {
        return;
    }

    Core::EditTransaction tx;
    tx.setSelectionsBefore(m_cursors);
    tx.setSelectionsAfter(newCursors);
    tx.setTimestamp(QDateTime::currentMSecsSinceEpoch());

    for (int i = editsAsc.size() - 1; i >= 0; --i) {
        tx.addEdit(editsAsc[i].offset, editsAsc[i].oldText, QString());
    }

    m_cursors = newCursors;
    syncPrimaryCursor();
    m_undoStack.push(tx, m_buffer);
    ensureCursorVisible();
}

void NativeEditorView::executeMultiDelete() {
    if (m_cursors.size() <= 1) {
        if (hasSelection()) {
            int s = qMin(m_cursorPos, m_selectionAnchor);
            int len = qAbs(m_cursorPos - m_selectionAnchor);
            executeEdit(s, len, QString());
        } else if (m_cursorPos < m_buffer.length()) {
            int next = Core::PositionTranslator::nextGraphemeOffset(m_buffer.text(), m_cursorPos);
            executeEdit(m_cursorPos, next - m_cursorPos, QString());
        }
        return;
    }

    normalizeCursors();
    if (hasSelection()) {
        executeMultiEdit(QString());
        return;
    }

    QVector<Core::SelectionState> cursorsAsc = m_cursors;
    QVector<Core::SelectionState> newCursors;
    int cumulativeShift = 0;

    struct DeleteEdit {
        int offset;
        int len;
        QString oldText;
    };
    QVector<DeleteEdit> editsAsc;

    int bufLen = m_buffer.length();
    for (int i = 0; i < cursorsAsc.size(); ++i) {
        int pos = cursorsAsc[i].cursor;
        if (pos >= bufLen) {
            int newPos = pos + cumulativeShift;
            newCursors.append(Core::SelectionState(newPos, newPos));
            continue;
        }

        int nextPos = Core::PositionTranslator::nextGraphemeOffset(m_buffer.text(), pos);
        int delLen = nextPos - pos;
        QString oldText = m_buffer.substring(pos, delLen);

        int newPos = pos + cumulativeShift;
        newCursors.append(Core::SelectionState(newPos, newPos));
        cumulativeShift -= delLen;

        editsAsc.append(DeleteEdit{pos, delLen, oldText});
    }

    if (editsAsc.isEmpty()) {
        return;
    }

    Core::EditTransaction tx;
    tx.setSelectionsBefore(m_cursors);
    tx.setSelectionsAfter(newCursors);
    tx.setTimestamp(QDateTime::currentMSecsSinceEpoch());

    for (int i = editsAsc.size() - 1; i >= 0; --i) {
        tx.addEdit(editsAsc[i].offset, editsAsc[i].oldText, QString());
    }

    m_cursors = newCursors;
    syncPrimaryCursor();
    m_undoStack.push(tx, m_buffer);
    ensureCursorVisible();
}

void NativeEditorView::undo() {
    QVector<Core::SelectionState> sels;
    m_buffer.blockSignals(true);
    bool ok = m_undoStack.undo(m_buffer, sels);
    m_buffer.blockSignals(false);
    if (ok) {
        setCursors(sels);
        reparseAndRelayout();
        emit textChanged();
        ensureCursorVisible();
    }
}

void NativeEditorView::redo() {
    QVector<Core::SelectionState> sels;
    m_buffer.blockSignals(true);
    bool ok = m_undoStack.redo(m_buffer, sels);
    m_buffer.blockSignals(false);
    if (ok) {
        setCursors(sels);
        reparseAndRelayout();
        emit textChanged();
        ensureCursorVisible();
    }
}

void NativeEditorView::copy() {
    if (!hasSelection()) {
        return;
    }
    QStringList parts;
    for (const auto &c : m_cursors) {
        if (c.hasSelection()) {
            parts.append(m_buffer.substring(c.selectionStart(), c.selectionLength()));
        }
    }
    if (parts.isEmpty() && m_cursorPos != m_selectionAnchor) {
        int s = qMin(m_cursorPos, m_selectionAnchor);
        int e = qMax(m_cursorPos, m_selectionAnchor);
        parts.append(m_buffer.substring(s, e - s));
    }
    QGuiApplication::clipboard()->setText(parts.join(u'\n'));
}

void NativeEditorView::cut() {
    if (!hasSelection()) {
        return;
    }
    copy();
    if (m_cursors.size() > 1) {
        executeMultiEdit(QString());
    } else {
        int s = qMin(m_cursorPos, m_selectionAnchor);
        int e = qMax(m_cursorPos, m_selectionAnchor);
        executeEdit(s, e - s, QString());
    }
}

void NativeEditorView::paste() {
    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
        return;
    }
    if (m_cursors.size() > 1) {
        QStringList lines = text.split(u'\n');
        if (lines.size() == m_cursors.size()) {
            normalizeCursors();
            QVector<Core::SelectionState> cursorsAsc = m_cursors;
            QVector<Core::SelectionState> newCursors;
            int cumulativeShift = 0;
            for (int i = 0; i < cursorsAsc.size(); ++i) {
                int s = cursorsAsc[i].selectionStart();
                int len = cursorsAsc[i].selectionLength();
                int newPos = s + cumulativeShift + lines[i].length();
                newCursors.append(Core::SelectionState(newPos, newPos));
                cumulativeShift += lines[i].length() - len;
            }

            Core::EditTransaction tx;
            tx.setSelectionsBefore(m_cursors);
            tx.setSelectionsAfter(newCursors);
            tx.setTimestamp(QDateTime::currentMSecsSinceEpoch());

            for (int i = cursorsAsc.size() - 1; i >= 0; --i) {
                int s = cursorsAsc[i].selectionStart();
                int len = cursorsAsc[i].selectionLength();
                QString oldText = m_buffer.substring(s, len);
                tx.addEdit(s, oldText, lines[i]);
            }

            m_cursors = newCursors;
            syncPrimaryCursor();
            m_undoStack.push(tx, m_buffer);
            ensureCursorVisible();
            return;
        }

        executeMultiEdit(text);
        return;
    }

    int s = qMin(m_cursorPos, m_selectionAnchor);
    int len = qAbs(m_cursorPos - m_selectionAnchor);
    executeEdit(s, len, text);
}

const QVector<int> &NativeEditorView::ensureFindMatches(const QString &text, bool matchCase) const {
    if (m_findCache.bufferRevision == m_buffer.revision() &&
        m_findCache.query == text &&
        m_findCache.matchCase == matchCase) {
        return m_findCache.matches;
    }

    m_findCache.bufferRevision = m_buffer.revision();
    m_findCache.query = text;
    m_findCache.matchCase = matchCase;
    m_findCache.matches.clear();

    if (text.isEmpty() || m_buffer.isEmpty()) {
        return m_findCache.matches;
    }

    Qt::CaseSensitivity cs = matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const QString &docText = m_buffer.text();
    int idx = 0;
    while ((idx = static_cast<int>(docText.indexOf(text, idx, cs))) != -1) {
        m_findCache.matches.append(idx);
        idx += text.length();
    }
    return m_findCache.matches;
}

bool NativeEditorView::findText(const QString &text, bool matchCase, bool forward) {
    if (text.isEmpty() || m_buffer.isEmpty()) {
        return false;
    }

    const QVector<int> &matches = ensureFindMatches(text, matchCase);
    if (matches.isEmpty()) {
        return false;
    }

    int selStart = qMin(m_cursorPos, m_selectionAnchor);
    int selLen = qAbs(m_cursorPos - m_selectionAnchor);

    int currentMatchIdx = -1;
    if (selLen == text.length()) {
        for (int i = 0; i < matches.size(); ++i) {
            if (matches[i] == selStart) {
                currentMatchIdx = i;
                break;
            }
        }
    }

    int targetIdx = -1;
    int total = static_cast<int>(matches.size());
    if (forward) {
        if (currentMatchIdx != -1) {
            targetIdx = matches[(currentMatchIdx + 1) % total];
        } else {
            targetIdx = matches[0];
            for (int pos : matches) {
                if (pos >= m_cursorPos) {
                    targetIdx = pos;
                    break;
                }
            }
        }
    } else {
        if (currentMatchIdx != -1) {
            targetIdx = matches[(currentMatchIdx - 1 + total) % total];
        } else {
            targetIdx = matches.last();
            for (int i = total - 1; i >= 0; --i) {
                if (matches[i] < selStart) {
                    targetIdx = matches[i];
                    break;
                }
            }
        }
    }

    m_selectionAnchor = targetIdx;
    m_cursorPos = targetIdx + text.length();
    ensureCursorVisible();
    viewport()->update();
    auto lc = m_buffer.offsetToLineCol(m_cursorPos);
    emit cursorMoved(lc.line1(), lc.col1(), m_cursorPos);
    return true;
}

NativeEditorView::SearchMatchStatus NativeEditorView::searchStatus(const QString &text, bool matchCase) const {
    if (text.isEmpty() || m_buffer.isEmpty()) {
        return {0, 0};
    }

    const QVector<int> &matches = ensureFindMatches(text, matchCase);
    if (matches.isEmpty()) {
        return {0, 0};
    }

    int selStart = qMin(m_cursorPos, m_selectionAnchor);
    int selLen = qAbs(m_cursorPos - m_selectionAnchor);

    int current = 0;
    if (selLen == text.length()) {
        for (int i = 0; i < matches.size(); ++i) {
            if (matches[i] == selStart) {
                current = i + 1;
                break;
            }
        }
    }

    return {current, static_cast<int>(matches.size())};
}

bool NativeEditorView::replaceOne(const QString &findStr, const QString &replaceStr, bool matchCase) {
    if (findStr.isEmpty()) {
        return false;
    }

    int selStart = qMin(m_cursorPos, m_selectionAnchor);
    int selLen = qAbs(m_cursorPos - m_selectionAnchor);
    QString curSelected = m_buffer.substring(selStart, selLen);

    bool matches = matchCase ? (curSelected == findStr) : (curSelected.compare(findStr, Qt::CaseInsensitive) == 0);
    if (matches) {
        executeEdit(selStart, selLen, replaceStr);
        findText(findStr, matchCase, true);
        return true;
    } else {
        return findText(findStr, matchCase, true);
    }
}

int NativeEditorView::replaceAll(const QString &findStr, const QString &replaceStr, bool matchCase) {
    if (findStr.isEmpty() || m_buffer.isEmpty()) {
        return 0;
    }

    Qt::CaseSensitivity cs = matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const QString &docText = m_buffer.text();

    QVector<int> matches;
    int idx = 0;
    while ((idx = docText.indexOf(findStr, idx, cs)) != -1) {
        matches.append(idx);
        idx += findStr.length();
    }

    if (matches.isEmpty()) {
        return 0;
    }

    // Compound transaction in reverse order so offsets remain stable
    Core::EditTransaction tx;
    tx.setSelectionBefore(Core::SelectionState(m_selectionAnchor, m_cursorPos));

    for (int i = matches.size() - 1; i >= 0; --i) {
        int pos = matches[i];
        tx.addEdit(pos, findStr, replaceStr);
    }
    tx.setTimestamp(QDateTime::currentMSecsSinceEpoch());
    tx.setSelectionAfter(Core::SelectionState(matches.first(), matches.first() + replaceStr.length()));

    m_undoStack.push(tx, m_buffer);
    m_cursorPos = matches.first() + replaceStr.length();
    m_selectionAnchor = matches.first();
    ensureCursorVisible();
    viewport()->update();

    return matches.size();
}

void NativeEditorView::toggleBold() {
    int s = qMin(m_cursorPos, m_selectionAnchor);
    int e = qMax(m_cursorPos, m_selectionAnchor);
    if (s == e) {
        executeEdit(s, 0, QStringLiteral("****"));
        setCursorPos(s + 2, false);
    } else {
        QString sel = m_buffer.substring(s, e - s);
        executeEdit(s, e - s, QStringLiteral("**") + sel + QStringLiteral("**"));
        setCursorPos(e + 4, false);
    }
}

void NativeEditorView::toggleItalic() {
    int s = qMin(m_cursorPos, m_selectionAnchor);
    int e = qMax(m_cursorPos, m_selectionAnchor);
    if (s == e) {
        executeEdit(s, 0, QStringLiteral("**"));
        setCursorPos(s + 1, false);
    } else {
        QString sel = m_buffer.substring(s, e - s);
        executeEdit(s, e - s, QStringLiteral("*") + sel + QStringLiteral("*"));
        setCursorPos(e + 2, false);
    }
}

void NativeEditorView::toggleStrikethrough() {
    int s = qMin(m_cursorPos, m_selectionAnchor);
    int e = qMax(m_cursorPos, m_selectionAnchor);
    if (s == e) {
        executeEdit(s, 0, QStringLiteral("~~~~"));
        setCursorPos(s + 2, false);
    } else {
        QString sel = m_buffer.substring(s, e - s);
        executeEdit(s, e - s, QStringLiteral("~~") + sel + QStringLiteral("~~"));
        setCursorPos(e + 4, false);
    }
}

void NativeEditorView::toggleCodeSpan() {
    int s = qMin(m_cursorPos, m_selectionAnchor);
    int e = qMax(m_cursorPos, m_selectionAnchor);
    if (s == e) {
        executeEdit(s, 0, QStringLiteral("``"));
        setCursorPos(s + 1, false);
    } else {
        QString sel = m_buffer.substring(s, e - s);
        executeEdit(s, e - s, QStringLiteral("`") + sel + QStringLiteral("`"));
        setCursorPos(e + 2, false);
    }
}

void NativeEditorView::insertLink() {
    int s = qMin(m_cursorPos, m_selectionAnchor);
    int e = qMax(m_cursorPos, m_selectionAnchor);
    QString sel = (s == e) ? QStringLiteral("链接文字") : m_buffer.substring(s, e - s);
    QString linkTmpl = QStringLiteral("[%1](https://)").arg(sel);
    executeEdit(s, e - s, linkTmpl);
    setCursorPos(s + linkTmpl.length() - 1, false);
}

void NativeEditorView::toggleTaskList() {
    int line = m_buffer.offsetToLineCol(m_cursorPos).line;
    int lineStart = m_buffer.lineStartOffset(line);
    QString lText = m_buffer.lineText(line);

    if (lText.startsWith(QStringLiteral("- [ ] "))) {
        executeEdit(lineStart, 6, QStringLiteral("- [x] "));
    } else if (lText.startsWith(QStringLiteral("- [x] "))) {
        executeEdit(lineStart, 6, QStringLiteral("- "));
    } else if (lText.startsWith(QStringLiteral("- "))) {
        executeEdit(lineStart, 2, QStringLiteral("- [ ] "));
    } else {
        executeEdit(lineStart, 0, QStringLiteral("- [ ] "));
    }
}

void NativeEditorView::insertHeading(int level) {
    int line = m_buffer.offsetToLineCol(m_cursorPos).line;
    int lineStart = m_buffer.lineStartOffset(line);
    QString lText = m_buffer.lineText(line);

    // Strip existing '#' prefix
    int prefixLen = 0;
    while (prefixLen < lText.length() && lText.at(prefixLen) == u'#') {
        prefixLen++;
    }
    if (prefixLen < lText.length() && lText.at(prefixLen) == u' ') {
        prefixLen++;
    }

    QString newPrefix = (level > 0) ? (QString(level, u'#') + QStringLiteral(" ")) : QString();
    executeEdit(lineStart, prefixLen, newPrefix);
}

void NativeEditorView::handleEnterKey() {
    int line = m_buffer.offsetToLineCol(m_cursorPos).line;
    int lineStart = m_buffer.lineStartOffset(line);
    QString lText = m_buffer.lineText(line);

    QString nl = m_buffer.lineEndingString();

    // Check task item
    if (lText == QStringLiteral("- [ ] ") || lText == QStringLiteral("- [x] ")) {
        // Exit task list
        executeEdit(lineStart, lText.length(), QString());
        return;
    }
    if (lText.startsWith(QStringLiteral("- [ ] ")) || lText.startsWith(QStringLiteral("- [x] "))) {
        executeEdit(m_cursorPos, 0, nl + QStringLiteral("- [ ] "));
        return;
    }

    // Check unordered list
    if (lText == QStringLiteral("- ") || lText == QStringLiteral("* ") || lText == QStringLiteral("+ ")) {
        // Exit list
        executeEdit(lineStart, lText.length(), QString());
        return;
    }
    if (lText.startsWith(QStringLiteral("- ")) || lText.startsWith(QStringLiteral("* ")) || lText.startsWith(QStringLiteral("+ "))) {
        QString bullet = lText.left(2);
        executeEdit(m_cursorPos, 0, nl + bullet);
        return;
    }

    // Check ordered list (e.g. "1. ")
    static QRegularExpression reOrdered(QStringLiteral(R"(^(\d+)\.\s)"));
    auto match = reOrdered.match(lText);
    if (match.hasMatch()) {
        if (lText == match.captured(0)) {
            // Exit ordered list
            executeEdit(lineStart, lText.length(), QString());
            return;
        }
        int nextNum = match.captured(1).toInt() + 1;
        executeEdit(m_cursorPos, 0, nl + QString::number(nextNum) + QStringLiteral(". "));
        return;
    }

    // Default: simple newline
    if (hasSelection()) {
        int s = qMin(m_cursorPos, m_selectionAnchor);
        int len = qAbs(m_cursorPos - m_selectionAnchor);
        executeEdit(s, len, nl);
    } else {
        executeEdit(m_cursorPos, 0, nl);
    }
}

void NativeEditorView::handleBackspaceKey() {
    if (hasSelection()) {
        int s = qMin(m_cursorPos, m_selectionAnchor);
        int len = qAbs(m_cursorPos - m_selectionAnchor);
        executeEdit(s, len, QString());
        return;
    }

    if (m_cursorPos <= 0) {
        return;
    }

    int line = m_buffer.offsetToLineCol(m_cursorPos).line;
    int lineStart = m_buffer.lineStartOffset(line);
    QString lText = m_buffer.lineText(line);

    // If at beginning of list item content, backspace deletes bullet
    if (lText.startsWith(QStringLiteral("- [ ] ")) && m_cursorPos == lineStart + 6) {
        executeEdit(lineStart, 6, QString());
        return;
    }
    if (lText.startsWith(QStringLiteral("- ")) && m_cursorPos == lineStart + 2) {
        executeEdit(lineStart, 2, QString());
        return;
    }

    // Grapheme safe backspace
    int prevPos = Core::PositionTranslator::prevGraphemeOffset(m_buffer.text(), m_cursorPos);
    int delLen = m_cursorPos - prevPos;
    executeEdit(prevPos, delLen, QString());
}

void NativeEditorView::handleTabKey(bool shiftPressed) {
    int line = m_buffer.offsetToLineCol(m_cursorPos).line;
    int lineStart = m_buffer.lineStartOffset(line);
    QString lText = m_buffer.lineText(line);

    if (shiftPressed) {
        // Unindent
        if (lText.startsWith(QStringLiteral("    "))) {
            executeEdit(lineStart, 4, QString());
        } else if (lText.startsWith(QStringLiteral("\t"))) {
            executeEdit(lineStart, 1, QString());
        }
    } else {
        // Indent
        executeEdit(lineStart, 0, QStringLiteral("    "));
    }
}

void NativeEditorView::keyPressEvent(QKeyEvent *event) {
    if (m_slashPopup && m_slashPopup->isVisible()) {
        if (event->key() == Qt::Key_Up) {
            m_slashPopup->selectPrev();
            return;
        }
        if (event->key() == Qt::Key_Down) {
            m_slashPopup->selectNext();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (m_slashPopup->executeSelected()) {
                return;
            }
        }
        if (event->key() == Qt::Key_Escape) {
            m_slashPopup->hide();
            return;
        }
    }

    if (event->matches(QKeySequence::Undo)) {
        undo();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copy();
        return;
    }
    if (event->matches(QKeySequence::Cut)) {
        cut();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        paste();
        return;
    }

    // Escape clears secondary cursors or deselects
    if (event->key() == Qt::Key_Escape) {
        if (m_cursors.size() > 1) {
            clearSecondaryCursors();
            return;
        }
        if (hasSelection()) {
            setCursorPos(m_cursorPos, false);
            return;
        }
    }

    // Column selection hotkeys: Ctrl+Alt+Up / Down
    if ((event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::AltModifier)) {
        if (event->key() == Qt::Key_Up) {
            addCursorAbove();
            return;
        }
        if (event->key() == Qt::Key_Down) {
            addCursorBelow();
            return;
        }
    }

    // Hotkey: Ctrl+/ to toggle mode
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Slash) {
        setSourceMode(!m_isSourceMode);
        return;
    }

    // Hotkey: Ctrl+B
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_B) {
        toggleBold();
        return;
    }

    // Hotkey: Ctrl+I
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_I) {
        toggleItalic();
        return;
    }

    // Hotkey: Ctrl+K
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_K) {
        insertLink();
        return;
    }

    bool shift = event->modifiers() & Qt::ShiftModifier;

    if (m_cursors.size() > 1) {
        switch (event->key()) {
        case Qt::Key_Left: {
            for (auto &c : m_cursors) {
                int next = m_isSourceMode ? Core::PositionTranslator::prevGraphemeOffset(m_buffer.text(), c.cursor)
                                          : m_projection.stepCursor(c.cursor, false, m_buffer);
                c.cursor = next;
                if (!shift) c.anchor = next;
            }
            normalizeCursors();
            viewport()->update();
            return;
        }
        case Qt::Key_Right: {
            for (auto &c : m_cursors) {
                int next = m_isSourceMode ? Core::PositionTranslator::nextGraphemeOffset(m_buffer.text(), c.cursor)
                                          : m_projection.stepCursor(c.cursor, true, m_buffer);
                c.cursor = next;
                if (!shift) c.anchor = next;
            }
            normalizeCursors();
            viewport()->update();
            return;
        }
        case Qt::Key_Up: {
            for (auto &c : m_cursors) {
                auto lc = m_buffer.offsetToLineCol(c.cursor);
                if (lc.line > 0) {
                    c.cursor = m_buffer.lineColToOffset(lc.line - 1, lc.col);
                }
                if (!shift) c.anchor = c.cursor;
            }
            normalizeCursors();
            viewport()->update();
            return;
        }
        case Qt::Key_Down: {
            for (auto &c : m_cursors) {
                auto lc = m_buffer.offsetToLineCol(c.cursor);
                if (lc.line + 1 < m_buffer.lineCount()) {
                    c.cursor = m_buffer.lineColToOffset(lc.line + 1, lc.col);
                }
                if (!shift) c.anchor = c.cursor;
            }
            normalizeCursors();
            viewport()->update();
            return;
        }
        case Qt::Key_Home: {
            for (auto &c : m_cursors) {
                int line = m_buffer.offsetToLineCol(c.cursor).line;
                c.cursor = m_buffer.lineStartOffset(line);
                if (!shift) c.anchor = c.cursor;
            }
            normalizeCursors();
            viewport()->update();
            return;
        }
        case Qt::Key_End: {
            for (auto &c : m_cursors) {
                int line = m_buffer.offsetToLineCol(c.cursor).line;
                c.cursor = m_buffer.lineEndOffset(line, false);
                if (!shift) c.anchor = c.cursor;
            }
            normalizeCursors();
            viewport()->update();
            return;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
            executeMultiEdit(QStringLiteral("\n"));
            return;
        case Qt::Key_Backspace:
            executeMultiBackspace();
            return;
        case Qt::Key_Delete:
            executeMultiDelete();
            return;
        default:
            break;
        }
    }

    switch (event->key()) {
    case Qt::Key_Left: {
        int next = m_isSourceMode ? Core::PositionTranslator::prevGraphemeOffset(m_buffer.text(), m_cursorPos)
                                  : m_projection.stepCursor(m_cursorPos, false, m_buffer);
        setCursorPos(next, shift);
        return;
    }
    case Qt::Key_Right: {
        int next = m_isSourceMode ? Core::PositionTranslator::nextGraphemeOffset(m_buffer.text(), m_cursorPos)
                                  : m_projection.stepCursor(m_cursorPos, true, m_buffer);
        setCursorPos(next, shift);
        return;
    }
    case Qt::Key_Up: {
        auto lc = m_buffer.offsetToLineCol(m_cursorPos);
        if (lc.line > 0) {
            int targetOffset = m_buffer.lineColToOffset(lc.line - 1, lc.col);
            setCursorPos(targetOffset, shift);
        }
        return;
    }
    case Qt::Key_Down: {
        auto lc = m_buffer.offsetToLineCol(m_cursorPos);
        if (lc.line + 1 < m_buffer.lineCount()) {
            int targetOffset = m_buffer.lineColToOffset(lc.line + 1, lc.col);
            setCursorPos(targetOffset, shift);
        }
        return;
    }
    case Qt::Key_Home: {
        int line = m_buffer.offsetToLineCol(m_cursorPos).line;
        setCursorPos(m_buffer.lineStartOffset(line), shift);
        return;
    }
    case Qt::Key_End: {
        int line = m_buffer.offsetToLineCol(m_cursorPos).line;
        setCursorPos(m_buffer.lineEndOffset(line, false), shift);
        return;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
        handleEnterKey();
        return;
    case Qt::Key_Backspace:
        handleBackspaceKey();
        return;
    case Qt::Key_Delete:
        if (hasSelection()) {
            int s = qMin(m_cursorPos, m_selectionAnchor);
            int len = qAbs(m_cursorPos - m_selectionAnchor);
            executeEdit(s, len, QString());
        } else if (m_cursorPos < m_buffer.length()) {
            int next = Core::PositionTranslator::nextGraphemeOffset(m_buffer.text(), m_cursorPos);
            executeEdit(m_cursorPos, next - m_cursorPos, QString());
        }
        return;
    case Qt::Key_Tab:
        handleTabKey(shift);
        return;
    default:
        break;
    }

    // Normal text character input
    QString text = event->text();
    if (!text.isEmpty() && (text.at(0).isPrint() || text.at(0) == u'\t')) {
        if (m_cursors.size() > 1) {
            executeMultiEdit(text);
            return;
        }

        int s = qMin(m_cursorPos, m_selectionAnchor);
        int len = qAbs(m_cursorPos - m_selectionAnchor);

        // Check if typing '/' at beginning of block
        bool isSlashAtBlockStart = false;
        if (text == QStringLiteral("/") && !m_isSourceMode) {
            int line = m_buffer.offsetToLineCol(s).line;
            int lStart = m_buffer.lineStartOffset(line);
            QString linePrefix = m_buffer.substring(lStart, s - lStart);
            if (linePrefix.trimmed().isEmpty()) {
                isSlashAtBlockStart = true;
            }
        }

        executeEdit(s, len, text);

        if (isSlashAtBlockStart && m_slashPopup) {
            QRectF cRect = m_layoutEngine.cursorRect(m_cursorPos, m_projection);
            cRect.translate(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
            QPoint globalPos = viewport()->mapToGlobal(cRect.toRect().bottomLeft() + QPoint(0, 4));
            m_slashPopup->showAt(globalPos);
        }
        return;
    }

    QAbstractScrollArea::keyPressEvent(event);
}

void NativeEditorView::inputMethodEvent(QInputMethodEvent *event) {
    if (!event->commitString().isEmpty()) {
        if (m_cursors.size() > 1) {
            executeMultiEdit(event->commitString());
        } else {
            int s = qMin(m_cursorPos, m_selectionAnchor);
            int len = qAbs(m_cursorPos - m_selectionAnchor);
            executeEdit(s, len, event->commitString());
        }
    }

    m_preeditString = event->preeditString();
    m_layoutEngine.layout(m_projection, viewport()->width(), m_cursorPos, m_preeditString);
    updateScrollBars();
    viewport()->update();
}

QVariant NativeEditorView::inputMethodQuery(Qt::InputMethodQuery query) const {
    switch (query) {
    case Qt::ImCursorRectangle: {
        QRectF cRect = m_layoutEngine.cursorRect(m_cursorPos, m_projection);
        cRect.translate(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
        QRect localRect = cRect.toRect();
        localRect.moveTopLeft(viewport()->mapTo(this, localRect.topLeft()));
        return localRect;
    }
    case Qt::ImAnchorRectangle: {
        QRectF aRect = m_layoutEngine.cursorRect(m_selectionAnchor, m_projection);
        aRect.translate(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
        QRect localRect = aRect.toRect();
        localRect.moveTopLeft(viewport()->mapTo(this, localRect.topLeft()));
        return localRect;
    }
    case Qt::ImSurroundingText: {
        int line = m_buffer.offsetToLineCol(m_cursorPos).line;
        return m_buffer.lineText(line);
    }
    case Qt::ImCursorPosition: {
        return m_buffer.offsetToLineCol(m_cursorPos).col;
    }
    case Qt::ImAnchorPosition: {
        return m_buffer.offsetToLineCol(m_selectionAnchor).col;
    }
    default:
        return QAbstractScrollArea::inputMethodQuery(query);
    }
}

void NativeEditorView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPointF docPoint = event->position() + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());

        // Ctrl + Click opens link in external browser (Scheme 7.2)
        if (event->modifiers() & Qt::ControlModifier) {
            int clickedOffset = m_layoutEngine.hitTest(docPoint, m_projection);
            std::function<QString(const Parser::SemanticNodePtr&)> findLink = [&](const Parser::SemanticNodePtr &node) -> QString {
                if (!node) return QString();
                if (node->type == Parser::NodeType::Link && node->fullRange.contains(clickedOffset)) {
                    return node->linkUrl;
                }
                for (const auto &child : node->children) {
                    QString u = findLink(child);
                    if (!u.isEmpty()) return u;
                }
                return QString();
            };
            QString targetUrl = findLink(m_rootNode);
            if (!targetUrl.isEmpty()) {
                QDesktopServices::openUrl(QUrl(targetUrl));
                return;
            }
        }

        // Check if clicked on task checkbox
        const auto &vBlocks = m_projection.blocks();
        for (const auto &b : vBlocks) {
            if (b.isTaskItem && b.checkboxSourceRange.isValid()) {
                QRectF cRect = m_layoutEngine.cursorRect(b.checkboxSourceRange.start, m_projection);
                QRectF hitBox(cRect.left() - 4, cRect.top() - 4, 30, cRect.height() + 8);
                if (hitBox.contains(docPoint)) {
                    QString curBox = m_buffer.substring(b.checkboxSourceRange);
                    if (curBox == QStringLiteral("[ ]")) {
                        executeEdit(b.checkboxSourceRange.start, 3, QStringLiteral("[x]"));
                    } else if (curBox == QStringLiteral("[x]") || curBox == QStringLiteral("[X]")) {
                        executeEdit(b.checkboxSourceRange.start, 3, QStringLiteral("[ ]"));
                    }
                    return;
                }
            }
        }

        int targetOffset = m_layoutEngine.hitTest(docPoint, m_projection);

        // Alt + Click: Add multi-cursor
        if (event->modifiers() & Qt::AltModifier) {
            addCursor(targetOffset, targetOffset);
            return;
        }

        clearSecondaryCursors();
        bool shift = event->modifiers() & Qt::ShiftModifier;
        setCursorPos(targetOffset, shift);
        m_isMouseSelecting = true;
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void NativeEditorView::mouseMoveEvent(QMouseEvent *event) {
    if (m_isMouseSelecting) {
        QPointF docPoint = event->position() + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
        int targetOffset = m_layoutEngine.hitTest(docPoint, m_projection);
        setCursorPos(targetOffset, true);
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void NativeEditorView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isMouseSelecting = false;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void NativeEditorView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        // Select word under cursor
        const QString &text = m_buffer.text();
        int s = m_cursorPos;
        int e = m_cursorPos;

        while (s > 0 && !text.at(s - 1).isSpace() && !text.at(s - 1).isPunct()) {
            --s;
        }
        while (e < text.length() && !text.at(e).isSpace() && !text.at(e).isPunct()) {
            ++e;
        }

        if (s < e) {
            m_selectionAnchor = s;
            m_cursorPos = e;
            viewport()->update();
        }
    }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void NativeEditorView::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    m_layoutEngine.layout(m_projection, viewport()->width());
    updateScrollBars();
}

void NativeEditorView::focusInEvent(QFocusEvent *event) {
    m_cursorVisible = true;
    viewport()->update();
    QAbstractScrollArea::focusInEvent(event);
}

void NativeEditorView::focusOutEvent(QFocusEvent *event) {
    m_cursorVisible = false;
    viewport()->update();
    QAbstractScrollArea::focusOutEvent(event);
}

void NativeEditorView::paintEvent(QPaintEvent *event) {
    QPainter painter(viewport());
    const auto &palette = Theme::currentPalette();

    // Theme-aware canvas background
    painter.fillRect(viewport()->rect(), palette.editorBackground);

    painter.save();
    painter.translate(-horizontalScrollBar()->value(), -verticalScrollBar()->value());

    QRectF visibleDocRect(horizontalScrollBar()->value(),
                          verticalScrollBar()->value(),
                          viewport()->width(),
                          viewport()->height());

    m_layoutEngine.paint(&painter, visibleDocRect, m_projection, m_cursors, m_preeditString);

    // Draw cursor carets
    if (m_cursorVisible && hasFocus()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.caretColor); // Theme-aware primary accent caret
        for (const auto &c : m_cursors) {
            if (!c.hasSelection()) {
                QRectF cRect = m_layoutEngine.cursorRect(c.cursor, m_projection);
                painter.drawRect(cRect);
            }
        }
    }

    painter.restore();
}

bool NativeEditorView::viewportEvent(QEvent *event) {
    if (event->type() == QEvent::DragEnter) {
        dragEnterEvent(static_cast<QDragEnterEvent*>(event));
        return event->isAccepted();
    }
    if (event->type() == QEvent::DragMove) {
        dragMoveEvent(static_cast<QDragMoveEvent*>(event));
        return event->isAccepted();
    }
    if (event->type() == QEvent::Drop) {
        dropEvent(static_cast<QDropEvent*>(event));
        return event->isAccepted();
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void NativeEditorView::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void NativeEditorView::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        QPointF docPoint = event->position() + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
        int targetOffset = m_layoutEngine.hitTest(docPoint, m_projection);
        setCursorPos(targetOffset);
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void NativeEditorView::dropEvent(QDropEvent *event) {
    if (event->mimeData()->hasUrls()) {
        const auto urls = event->mimeData()->urls();
        if (urls.isEmpty()) {
            event->ignore();
            return;
        }

        QPointF docPoint = event->position() + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
        int dropOffset = m_layoutEngine.hitTest(docPoint, m_projection);
        dropOffset = qBound(0, dropOffset, m_buffer.length());
        setCursorPos(dropOffset);

        for (const auto &url : urls) {
            if (url.isLocalFile()) {
                QString localPath = url.toLocalFile();
                QFileInfo fi(localPath);

                if (fi.isDir()) {
                    emit folderOpenRequested(localPath);
                    event->acceptProposedAction();
                    return;
                }

                QString ext = fi.suffix().toLower();
                bool isImage = (ext == QStringLiteral("png") || ext == QStringLiteral("jpg") ||
                                ext == QStringLiteral("jpeg") || ext == QStringLiteral("gif") ||
                                ext == QStringLiteral("svg") || ext == QStringLiteral("webp") ||
                                ext == QStringLiteral("bmp") || ext == QStringLiteral("ico"));

                if (isImage) {
                    QString normPath = localPath;
                    normPath.replace(u'\\', u'/');
                    QString altName = fi.completeBaseName();
                    QString snippet = QStringLiteral("![%1](%2)\n").arg(altName, normPath);

                    executeEdit(m_cursorPos, 0, snippet);
                    setCursorPos(m_cursorPos + snippet.length());
                    event->acceptProposedAction();
                } else if (ext == QStringLiteral("md") || ext == QStringLiteral("markdown") ||
                           ext == QStringLiteral("mdown") || ext == QStringLiteral("mkd") ||
                           ext == QStringLiteral("txt")) {
                    emit fileOpenRequested(localPath);
                    event->acceptProposedAction();
                    return;
                } else {
                    QString normPath = localPath;
                    normPath.replace(u'\\', u'/');
                    QString snippet = QStringLiteral("[%1](%2)").arg(fi.fileName(), normPath);

                    executeEdit(m_cursorPos, 0, snippet);
                    setCursorPos(m_cursorPos + snippet.length());
                    event->acceptProposedAction();
                }
            }
        }
        return;
    }

    if (event->mimeData()->hasText()) {
        QPointF docPoint = event->position() + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
        int dropOffset = m_layoutEngine.hitTest(docPoint, m_projection);
        dropOffset = qBound(0, dropOffset, m_buffer.length());
        setCursorPos(dropOffset);

        QString text = event->mimeData()->text();
        executeEdit(dropOffset, 0, text);
        setCursorPos(dropOffset + text.length());
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

void NativeEditorView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->angleDelta().y() > 0) {
            Theme::ThemeManager::instance().zoomIn();
        } else if (event->angleDelta().y() < 0) {
            Theme::ThemeManager::instance().zoomOut();
        }
        event->accept();
        return;
    }

    int dy = 0;
    if (!event->pixelDelta().isNull()) {
        dy = event->pixelDelta().y();
    } else if (!event->angleDelta().isNull()) {
        // Standard mouse wheel notch: 120 units
        int scrollLines = qMax(1, QApplication::wheelScrollLines());
        double lineH = 26.0;
        dy = static_cast<int>((event->angleDelta().y() / 120.0) * (scrollLines * lineH));
    }

    int dx = 0;
    if (event->pixelDelta().x() != 0) {
        dx = event->pixelDelta().x();
    } else if (event->angleDelta().x() != 0) {
        dx = static_cast<int>((event->angleDelta().x() / 120.0) * 40.0);
    }

    if (dy != 0) {
        verticalScrollBar()->setValue(verticalScrollBar()->value() - dy);
    }
    if (dx != 0) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dx);
    }

    event->accept();
}

} // namespace MarkdownTool::Editor
