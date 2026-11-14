#pragma once

#include "Position.h"

#include <QByteArray>
#include <QString>
#include <QObject>

namespace MarkdownTool::Core {

enum class DocumentEncoding {
    Utf8,
    Utf8Bom,
};

enum class LineEnding {
    Lf,
    Crlf,
    Cr,
};

class TextBuffer : public QObject {
    Q_OBJECT

public:
    explicit TextBuffer(QObject *parent = nullptr);

    void setText(const QString &text);
    void loadFromBytes(const QByteArray &bytes);
    QByteArray toBytes(bool forceUtf8NoBom = false) const;

    const QString &text() const { return m_text; }
    int length() const { return m_text.length(); }
    bool isEmpty() const { return m_text.isEmpty(); }

    QChar charAt(int index) const;
    QString substring(int start, int length) const;
    QString substring(const SourceRange &range) const;

    void replace(int offset, int length, const QString &newText);
    void insert(int offset, const QString &text);
    void remove(int offset, int length);

    quint64 revision() const { return m_revision; }
    void setRevision(quint64 rev) { m_revision = rev; }

    const PositionTranslator &positionTranslator() const { return m_translator; }
    int lineCount() const { return m_translator.lineCount(); }
    int lineStartOffset(int line) const { return m_translator.lineStartOffset(line); }
    int lineEndOffset(int line, bool includeNewline = false) const {
        return m_translator.lineEndOffset(line, m_text, includeNewline);
    }
    LineCol offsetToLineCol(int offset) const { return m_translator.offsetToLineCol(offset); }
    int lineColToOffset(int line, int col) const { return m_translator.lineColToOffset(line, col); }
    QString lineText(int line) const;

    DocumentEncoding encoding() const { return m_encoding; }
    void setEncoding(DocumentEncoding enc) { m_encoding = enc; }

    LineEnding lineEnding() const { return m_lineEnding; }
    void setLineEnding(LineEnding le) { m_lineEnding = le; }

    bool hasTrailingNewline() const { return m_hasTrailingNewline; }
    void setHasTrailingNewline(bool v) { m_hasTrailingNewline = v; }

    const QByteArray &originalBytes() const { return m_originalBytes; }
    bool isRawBytesIntact() const { return m_rawBytesIntact; }

    QString lineEndingString() const;

signals:
    void textChanged(int offset, int lengthRemoved, int lengthAdded);
    void textReset();

private:
    void detectFormat(const QByteArray &bytes);

    QString m_text;
    quint64 m_revision = 0;
    PositionTranslator m_translator;

    DocumentEncoding m_encoding = DocumentEncoding::Utf8;
    LineEnding m_lineEnding = LineEnding::Lf;
    bool m_hasTrailingNewline = false;

    QByteArray m_originalBytes;
    bool m_rawBytesIntact = false;
};

} // namespace MarkdownTool::Core
