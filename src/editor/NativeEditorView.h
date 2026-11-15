#pragma once

#include "core/TextBuffer.h"
#include "core/UndoStack.h"
#include "parser/MarkdownParserAdapter.h"
#include "mapping/DisplayProjection.h"
#include "LayoutEngine.h"

#include <QAbstractScrollArea>
#include <QTimer>
#include <QInputMethodEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>

namespace MarkdownTool::UI {
class SlashCommandPopup;
}

namespace MarkdownTool::Editor {

class NativeEditorView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit NativeEditorView(QWidget *parent = nullptr);
    ~NativeEditorView() override;

    Core::TextBuffer &buffer() { return m_buffer; }
    const Core::TextBuffer &buffer() const { return m_buffer; }

    Core::UndoStack &undoStack() { return m_undoStack; }
    const Core::UndoStack &undoStack() const { return m_undoStack; }

    Parser::SemanticNodePtr rootNode() const { return m_rootNode; }

    int cursorPos() const { return m_cursorPos; }
    int selectionAnchor() const { return m_selectionAnchor; }
    bool hasSelection() const;

    // Multi-cursor interface
    const QVector<Core::SelectionState> &cursors() const { return m_cursors; }
    int cursorCount() const { return m_cursors.size(); }
    void addCursor(int pos, int anchor = -1);
    void setCursors(const QVector<Core::SelectionState> &cursors);
    void clearSecondaryCursors();
    void addCursorAbove();
    void addCursorBelow();

    bool isSourceMode() const { return m_isSourceMode; }
    void setSourceMode(bool enabled);

    const Mapping::DisplayProjection &projection() const { return m_projection; }
    const LayoutEngine &layoutEngine() const { return m_layoutEngine; }

    void setCursorPos(int pos, bool keepAnchor = false);
    void selectAll();

    // Formatting commands
    void toggleBold();
    void toggleItalic();
    void toggleStrikethrough();
    void toggleCodeSpan();
    void insertLink();
    void toggleTaskList();
    void insertHeading(int level);

    void copy();
    void cut();
    void paste();
    void undo();
    void redo();

    bool findText(const QString &text, bool matchCase, bool forward);
    bool replaceOne(const QString &findStr, const QString &replaceStr, bool matchCase);
    int replaceAll(const QString &findStr, const QString &replaceStr, bool matchCase);

    void reparseAndRelayout(int editOffset = -1, int lengthRemoved = 0, int lengthAdded = 0);

    quint64 incrementalParseHits() const { return m_incrementalParseHits; }
    quint64 fullParseFallbacks() const { return m_fullParseFallbacks; }
    quint64 incrementalProjectionHits() const { return m_projection.incrementalHits(); }
    quint64 fullProjectionFallbacks() const { return m_projection.fullFallbacks(); }

signals:
    void cursorMoved(int line, int col, int offset);
    void textChanged();
    void modeChanged(bool isSourceMode);
    void fileOpenRequested(const QString &filePath);
    void folderOpenRequested(const QString &dirPath);

protected:
    bool viewportEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private slots:
    void onBlinkTimer();

private:
    void executeEdit(int offset, int removeLength, const QString &insertText);
    void executeMultiEdit(const QString &insertText);
    void executeMultiBackspace();
    void executeMultiDelete();
    void normalizeCursors();
    void syncPrimaryCursor();
    void handleEnterKey();
    void handleBackspaceKey();
    void handleTabKey(bool shiftPressed);
    void updateScrollBars();
    void ensureCursorVisible();

    Core::TextBuffer m_buffer;
    Core::UndoStack m_undoStack;
    Parser::MarkdownParserAdapter m_parser;
    Parser::SemanticNodePtr m_rootNode;
    Mapping::DisplayProjection m_projection;
    LayoutEngine m_layoutEngine;

    int m_cursorPos = 0;
    int m_selectionAnchor = 0;
    QVector<Core::SelectionState> m_cursors;

    bool m_isSourceMode = false;
    bool m_cursorVisible = true;
    QTimer *m_blinkTimer = nullptr;

    QString m_preeditString;
    int m_preeditCursorPos = 0;

    bool m_isMouseSelecting = false;
    UI::SlashCommandPopup *m_slashPopup = nullptr;

    quint64 m_incrementalParseHits = 0;
    quint64 m_fullParseFallbacks = 0;
};

} // namespace MarkdownTool::Editor
