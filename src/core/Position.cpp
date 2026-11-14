#include "Position.h"

#include <algorithm>

namespace MarkdownTool::Core {

void PositionTranslator::update(const QString &text) {
    m_lineStarts.clear();
    m_lineStarts.append(0);

    const int len = text.length();
    for (int i = 0; i < len; ++i) {
        const QChar c = text.at(i);
        if (c == u'\n') {
            m_lineStarts.append(i + 1);
        } else if (c == u'\r') {
            if (i + 1 < len && text.at(i + 1) == u'\n') {
                m_lineStarts.append(i + 2);
                ++i; // skip \n
            } else {
                m_lineStarts.append(i + 1);
            }
        }
    }
}

LineCol PositionTranslator::offsetToLineCol(int utf16Offset) const {
    if (m_lineStarts.isEmpty()) {
        return LineCol(0, 0);
    }

    if (utf16Offset <= 0) {
        return LineCol(0, 0);
    }

    // Binary search for the line start <= utf16Offset
    auto it = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), utf16Offset);
    int line = static_cast<int>(std::distance(m_lineStarts.begin(), it)) - 1;
    if (line < 0) {
        line = 0;
    }

    int col = utf16Offset - m_lineStarts[line];
    return LineCol(line, col);
}

int PositionTranslator::lineColToOffset(int line, int col) const {
    if (m_lineStarts.isEmpty()) {
        return 0;
    }
    if (line < 0) {
        return 0;
    }
    if (line >= m_lineStarts.size()) {
        return m_lineStarts.last();
    }

    return m_lineStarts[line] + std::max(0, col);
}

int PositionTranslator::lineCount() const {
    return m_lineStarts.isEmpty() ? 1 : m_lineStarts.size();
}

int PositionTranslator::lineStartOffset(int line) const {
    if (m_lineStarts.isEmpty() || line <= 0) {
        return 0;
    }
    if (line >= m_lineStarts.size()) {
        return m_lineStarts.last();
    }
    return m_lineStarts[line];
}

int PositionTranslator::lineEndOffset(int line, const QString &text, bool includeNewline) const {
    if (line < 0) {
        return 0;
    }
    int start = lineStartOffset(line);
    int nextStart = (line + 1 < m_lineStarts.size()) ? m_lineStarts[line + 1] : text.length();

    if (includeNewline) {
        return nextStart;
    }

    // Strip trailing \r or \n
    int end = nextStart;
    while (end > start && (text.at(end - 1) == u'\n' || text.at(end - 1) == u'\r')) {
        --end;
    }
    return end;
}

int PositionTranslator::utf16ToUtf8Byte(int utf16Offset, const QString &text) {
    if (utf16Offset <= 0) {
        return 0;
    }
    const int maxLen = qMin(utf16Offset, static_cast<int>(text.length()));
    int byteCount = 0;

    for (int i = 0; i < maxLen; ++i) {
        const QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < maxLen && text.at(i + 1).isLowSurrogate()) {
            // 4-byte UTF-8 sequence for supplementary code point
            byteCount += 4;
            ++i;
        } else {
            const ushort u = c.unicode();
            if (u <= 0x7F) {
                byteCount += 1;
            } else if (u <= 0x7FF) {
                byteCount += 2;
            } else {
                byteCount += 3;
            }
        }
    }
    return byteCount;
}

int PositionTranslator::utf8ByteToUtf16(int utf8ByteOffset, const QString &text) {
    if (utf8ByteOffset <= 0) {
        return 0;
    }
    int currentByte = 0;
    const int len = text.length();

    for (int i = 0; i < len; ++i) {
        if (currentByte >= utf8ByteOffset) {
            return i;
        }
        const QChar c = text.at(i);
        int charBytes = 0;
        if (c.isHighSurrogate() && i + 1 < len && text.at(i + 1).isLowSurrogate()) {
            charBytes = 4;
            currentByte += charBytes;
            if (currentByte >= utf8ByteOffset) {
                return i + 2; // return after surrogate pair
            }
            ++i;
        } else {
            const ushort u = c.unicode();
            if (u <= 0x7F) {
                charBytes = 1;
            } else if (u <= 0x7FF) {
                charBytes = 2;
            } else {
                charBytes = 3;
            }
            currentByte += charBytes;
            if (currentByte >= utf8ByteOffset) {
                return i + 1;
            }
        }
    }
    return len;
}

int PositionTranslator::prevGraphemeOffset(const QString &text, int currentOffset) {
    if (currentOffset <= 0) {
        return 0;
    }
    int prev = currentOffset - 1;
    // Check CRLF: if current is after \n and before is \r, move before \r
    if (prev > 0 && text.at(prev) == u'\n' && text.at(prev - 1) == u'\r') {
        prev = prev - 1;
    } else if (prev > 0 && text.at(prev).isLowSurrogate() && text.at(prev - 1).isHighSurrogate()) {
        prev = prev - 1;
    }
    return prev;
}

int PositionTranslator::nextGraphemeOffset(const QString &text, int currentOffset) {
    const int len = text.length();
    if (currentOffset >= len) {
        return len;
    }
    int next = currentOffset + 1;
    // Check CRLF: if current is at \r and next is \n, move past \n
    if (next < len && text.at(currentOffset) == u'\r' && text.at(next) == u'\n') {
        next = next + 1;
    } else if (next < len && text.at(currentOffset).isHighSurrogate() && text.at(next).isLowSurrogate()) {
        next = next + 1;
    }
    return next;
}

} // namespace MarkdownTool::Core
