#pragma once

#include <QString>
#include <QVector>
#include <compare>

namespace MarkdownTool::Core {

struct Utf16Pos {
    int value = 0;

    constexpr Utf16Pos() = default;
    constexpr explicit Utf16Pos(int v) : value(v) {}

    auto operator<=>(const Utf16Pos&) const = default;
    Utf16Pos operator+(int offset) const { return Utf16Pos(value + offset); }
    Utf16Pos operator-(int offset) const { return Utf16Pos(value - offset); }
    int operator-(Utf16Pos other) const { return value - other.value; }
};

struct Utf8BytePos {
    int value = 0;

    constexpr Utf8BytePos() = default;
    constexpr explicit Utf8BytePos(int v) : value(v) {}

    auto operator<=>(const Utf8BytePos&) const = default;
};

struct LineCol {
    int line = 0;   // 0-based
    int col = 0;    // 0-based

    constexpr LineCol() = default;
    constexpr LineCol(int l, int c) : line(l), col(c) {}

    auto operator<=>(const LineCol&) const = default;

    int line1() const { return line + 1; }
    int col1() const { return col + 1; }
};

struct SourceRange {
    int start = 0;
    int end = 0;

    constexpr SourceRange() = default;
    constexpr SourceRange(int s, int e) : start(s), end(e) {}

    bool isValid() const { return start >= 0 && end >= start; }
    int length() const { return end - start; }
    bool isEmpty() const { return start == end; }
    bool contains(int pos) const { return pos >= start && pos <= end; }
    bool strictlyContains(int pos) const { return pos > start && pos < end; }

    auto operator<=>(const SourceRange&) const = default;
};

class PositionTranslator {
public:
    PositionTranslator() = default;

    void update(const QString &text);

    LineCol offsetToLineCol(int utf16Offset) const;
    int lineColToOffset(int line, int col) const;

    int lineCount() const;
    int lineStartOffset(int line) const;
    int lineEndOffset(int line, const QString &text, bool includeNewline = false) const;

    static int utf16ToUtf8Byte(int utf16Offset, const QString &text);
    static int utf8ByteToUtf16(int utf8ByteOffset, const QString &text);

    // Grapheme cluster boundary navigation (avoids breaking surrogate pairs / emoji)
    static int prevGraphemeOffset(const QString &text, int currentOffset);
    static int nextGraphemeOffset(const QString &text, int currentOffset);

private:
    QVector<int> m_lineStarts;
};

} // namespace MarkdownTool::Core
