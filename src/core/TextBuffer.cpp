#include "TextBuffer.h"

namespace MarkdownTool::Core {

TextBuffer::TextBuffer(QObject *parent)
    : QObject(parent)
{
    m_translator.update(m_text);
}

void TextBuffer::setText(const QString &text) {
    m_text = text;
    m_rawBytesIntact = false;
    m_originalBytes.clear();
    ++m_revision;
    m_translator.update(m_text);
    emit textReset();
}

void TextBuffer::loadFromBytes(const QByteArray &bytes) {
    m_originalBytes = bytes;
    m_rawBytesIntact = true;
    detectFormat(bytes);

    QByteArray contentBytes = bytes;
    if (m_encoding == DocumentEncoding::Utf8Bom && contentBytes.startsWith("\xEF\xBB\xBF")) {
        contentBytes = contentBytes.mid(3);
    }

    m_text = QString::fromUtf8(contentBytes);
    ++m_revision;
    m_translator.update(m_text);
    emit textReset();
}

void TextBuffer::detectFormat(const QByteArray &bytes) {
    // 1. Detect BOM
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        m_encoding = DocumentEncoding::Utf8Bom;
    } else {
        m_encoding = DocumentEncoding::Utf8;
    }

    // 2. Detect line endings
    int crlfCount = 0;
    int lfCount = 0;
    int crCount = 0;
    const int len = bytes.size();

    for (int i = 0; i < len; ++i) {
        if (bytes[i] == '\r') {
            if (i + 1 < len && bytes[i + 1] == '\n') {
                crlfCount++;
                i++;
            } else {
                crCount++;
            }
        } else if (bytes[i] == '\n') {
            lfCount++;
        }
    }

    if (crlfCount >= lfCount && crlfCount > 0) {
        m_lineEnding = LineEnding::Crlf;
    } else if (crCount > lfCount && crCount > crlfCount) {
        m_lineEnding = LineEnding::Cr;
    } else {
        m_lineEnding = LineEnding::Lf;
    }

    // 3. Detect trailing newline
    if (!bytes.isEmpty()) {
        char last = bytes.back();
        m_hasTrailingNewline = (last == '\n' || last == '\r');
    } else {
        m_hasTrailingNewline = false;
    }
}

QByteArray TextBuffer::toBytes(bool forceUtf8NoBom) const {
    // If text was never modified, return original bytes directly (100% byte-for-byte identical)
    if (m_rawBytesIntact && !forceUtf8NoBom) {
        return m_originalBytes;
    }

    QByteArray out;
    if (!forceUtf8NoBom && m_encoding == DocumentEncoding::Utf8Bom) {
        out.append("\xEF\xBB\xBF", 3);
    }

    out.append(m_text.toUtf8());
    return out;
}

QChar TextBuffer::charAt(int index) const {
    if (index >= 0 && index < m_text.length()) {
        return m_text.at(index);
    }
    return QChar();
}

QString TextBuffer::substring(int start, int length) const {
    if (start < 0 || start >= m_text.length() || length <= 0) {
        return QString();
    }
    return m_text.mid(start, length);
}

QString TextBuffer::substring(const SourceRange &range) const {
    if (!range.isValid()) {
        return QString();
    }
    return substring(range.start, range.length());
}

void TextBuffer::replace(int offset, int length, const QString &newText) {
    if (offset < 0) {
        offset = 0;
    }
    if (offset > m_text.length()) {
        offset = m_text.length();
    }
    if (length < 0) {
        length = 0;
    }
    if (offset + length > m_text.length()) {
        length = m_text.length() - offset;
    }

    m_text.replace(offset, length, newText);
    m_rawBytesIntact = false;
    ++m_revision;
    m_translator.update(m_text);

    emit textChanged(offset, length, newText.length());
}

void TextBuffer::insert(int offset, const QString &text) {
    replace(offset, 0, text);
}

void TextBuffer::remove(int offset, int length) {
    replace(offset, length, QString());
}

QString TextBuffer::lineText(int line) const {
    int start = lineStartOffset(line);
    int end = lineEndOffset(line, false);
    return substring(start, end - start);
}

QString TextBuffer::lineEndingString() const {
    switch (m_lineEnding) {
    case LineEnding::Crlf: return QStringLiteral("\r\n");
    case LineEnding::Cr: return QStringLiteral("\r");
    case LineEnding::Lf:
    default: return QStringLiteral("\n");
    }
}

} // namespace MarkdownTool::Core
