#include "NativeEditorView.h"
#include "ui/SlashCommandPopup.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QGuiApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <algorithm>

namespace MarkdownTool::Editor {

NativeEditorView::NativeEditorView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(false);

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(500);
    connect(m_blinkTimer, &QTimer::timeout, this, &NativeEditorView::onBlinkTimer);
    m_blinkTimer->start();

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
        reparseAndRelayout();
        emit textChanged();
    });

    reparseAndRelayout();
}

NativeEditorView::~NativeEditorView() = default;

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

    m_undoStack.push(tx, m_buffer);

    ensureCursorVisible();
}

void NativeEditorView::undo() {
    Core::SelectionState sel;
    m_buffer.blockSignals(true);
    bool ok = m_undoStack.undo(m_buffer, sel);
    m_buffer.blockSignals(false);
    if (ok) {
        m_cursorPos = sel.cursor;
        m_selectionAnchor = sel.anchor;
        reparseAndRelayout();
        emit textChanged();
        ensureCursorVisible();
    }
}

void NativeEditorView::redo() {
    Core::SelectionState sel;
    m_buffer.blockSignals(true);
    bool ok = m_undoStack.redo(m_buffer, sel);
    m_buffer.blockSignals(false);
    if (ok) {
        m_cursorPos = sel.cursor;
        m_selectionAnchor = sel.anchor;
        reparseAndRelayout();
        emit textChanged();
        ensureCursorVisible();
    }
}

void NativeEditorView::copy() {
    if (!hasSelection()) {
        return;
    }
    int s = qMin(m_cursorPos, m_selectionAnchor);
    int e = qMax(m_cursorPos, m_selectionAnchor);
    QString selected = m_buffer.substring(s, e - s);
    QGuiApplication::clipboard()->setText(selected);
}

void NativeEditorView::cut() {
    if (!hasSelection()) {
        return;
    }
    copy();
    int s = qMin(m_cursorPos, m_selectionAnchor);
    int e = qMax(m_cursorPos, m_selectionAnchor);
    executeEdit(s, e - s, QString());
}

void NativeEditorView::paste() {
    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
        return;
    }
    int s = qMin(m_cursorPos, m_selectionAnchor);
    int len = qAbs(m_cursorPos - m_selectionAnchor);
    executeEdit(s, len, text);
}

bool NativeEditorView::findText(const QString &text, bool matchCase, bool forward) {
    if (text.isEmpty() || m_buffer.isEmpty()) {
        return false;
    }

    Qt::CaseSensitivity cs = matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const QString &docText = m_buffer.text();

    int from = forward ? m_cursorPos : qMax(0, m_cursorPos - text.length() - 1);
    int idx = forward ? docText.indexOf(text, from, cs) : docText.lastIndexOf(text, from, cs);

    if (idx == -1) {
        // Wrap around search
        idx = forward ? docText.indexOf(text, 0, cs) : docText.lastIndexOf(text, -1, cs);
    }

    if (idx != -1) {
        m_selectionAnchor = idx;
        m_cursorPos = idx + text.length();
        ensureCursorVisible();
        viewport()->update();
        auto lc = m_buffer.offsetToLineCol(m_cursorPos);
        emit cursorMoved(lc.line1(), lc.col1(), m_cursorPos);
        return true;
    }
    return false;
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
        int s = qMin(m_cursorPos, m_selectionAnchor);
        int len = qAbs(m_cursorPos - m_selectionAnchor);
        executeEdit(s, len, event->commitString());
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

    // Clean background
    painter.fillRect(viewport()->rect(), QColor(255, 255, 255));

    painter.save();
    painter.translate(-horizontalScrollBar()->value(), -verticalScrollBar()->value());

    QRectF visibleDocRect(horizontalScrollBar()->value(),
                          verticalScrollBar()->value(),
                          viewport()->width(),
                          viewport()->height());

    int selS = qMin(m_cursorPos, m_selectionAnchor);
    int selE = qMax(m_cursorPos, m_selectionAnchor);

    m_layoutEngine.paint(&painter, visibleDocRect, m_projection, selS, selE, m_preeditString);

    // Draw cursor
    if (m_cursorVisible && hasFocus() && !hasSelection()) {
        QRectF cRect = m_layoutEngine.cursorRect(m_cursorPos, m_projection);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(40, 100, 230)); // Modern primary blue cursor
        painter.drawRect(cRect);
    }

    painter.restore();
}

} // namespace MarkdownTool::Editor
