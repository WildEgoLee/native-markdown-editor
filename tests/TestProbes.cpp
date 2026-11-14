#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>

#include "core/Position.h"
#include "core/TextBuffer.h"
#include "core/EditTransaction.h"
#include "core/UndoStack.h"
#include "parser/MarkdownParserAdapter.h"
#include "mapping/DisplayProjection.h"
#include "file/FileService.h"

using namespace MarkdownTool;

class TestProbes : public QObject {
    Q_OBJECT

private slots:
    void testByteForByteInvariance();
    void testPositionTranslationAndEmoji();
    void testUndoRedoAndCleanState();
    void testParserAndSyntaxRanges();
    void testLivePreviewActiveInactiveProjection();
    void testSafeAtomicSaveAndConflict();
    void testCrashRecoverySnapshot();
    void testHiddenMarkerCursorStepping();
    void testFindAndBatchReplaceUndo();
};

// Probe 1: UTF-8 BOM, CRLF, trailing spaces, no-newline exact byte-for-byte invariance
void TestProbes::testByteForByteInvariance() {
    // 1. CRLF with BOM
    QByteArray sampleBomCrlf = "\xEF\xBB\xBF# 标题\r\n第一行内容  \r\n- [ ] 任务项\r\n";
    Core::TextBuffer buffer1;
    buffer1.loadFromBytes(sampleBomCrlf);

    QCOMPARE(buffer1.encoding(), Core::DocumentEncoding::Utf8Bom);
    QCOMPARE(buffer1.lineEnding(), Core::LineEnding::Crlf);
    QVERIFY(buffer1.hasTrailingNewline());
    QVERIFY(buffer1.isRawBytesIntact());

    // Without modification, toBytes() must be 100% byte-for-byte identical
    QByteArray outBytes1 = buffer1.toBytes();
    QCOMPARE(outBytes1, sampleBomCrlf);

    // 2. LF without BOM and without trailing newline
    QByteArray sampleLfNoNewline = "## Subtitle\nSome text with no trailing newline";
    Core::TextBuffer buffer2;
    buffer2.loadFromBytes(sampleLfNoNewline);

    QCOMPARE(buffer2.encoding(), Core::DocumentEncoding::Utf8);
    QCOMPARE(buffer2.lineEnding(), Core::LineEnding::Lf);
    QVERIFY(!buffer2.hasTrailingNewline());
    QVERIFY(buffer2.isRawBytesIntact());

    QByteArray outBytes2 = buffer2.toBytes();
    QCOMPARE(outBytes2, sampleLfNoNewline);
}

// Probe 2: Chinese, Emoji, Surrogate pairs, UTF-16 to UTF-8 byte conversion
void TestProbes::testPositionTranslationAndEmoji() {
    // "Hello 世界 😊 极速 Markdown"
    // '世' is 3 bytes in UTF-8, 1 QChar in UTF-16
    // '😊' (U+1F60A) is 4 bytes in UTF-8, 2 QChars in UTF-16 (surrogate pair)
    QString sample = QStringLiteral("Hello 世界 😊 极速");

    int utf16Len = sample.length();
    int utf8ByteLen = sample.toUtf8().length();

    // Verify roundtrip translation across valid grapheme cluster boundaries
    int i = 0;
    while (i <= utf16Len) {
        int byteOff = Core::PositionTranslator::utf16ToUtf8Byte(i, sample);
        int backU16 = Core::PositionTranslator::utf8ByteToUtf16(byteOff, sample);
        QCOMPARE(backU16, i);
        if (i == utf16Len) {
            break;
        }
        i = Core::PositionTranslator::nextGraphemeOffset(sample, i);
    }

    // Verify grapheme cluster boundary for emoji
    int emojiStart = sample.indexOf(QStringLiteral("😊"));
    QVERIFY(emojiStart > 0);
    // Moving next from emojiStart must jump past the entire surrogate pair (length 2)
    int afterEmoji = Core::PositionTranslator::nextGraphemeOffset(sample, emojiStart);
    QCOMPARE(afterEmoji, emojiStart + 2);

    // Moving prev from afterEmoji must jump back to emojiStart
    int beforeEmoji = Core::PositionTranslator::prevGraphemeOffset(sample, afterEmoji);
    QCOMPARE(beforeEmoji, emojiStart);
}

// Probe 3: Undo, Redo, Revision checkpoints and clean state
void TestProbes::testUndoRedoAndCleanState() {
    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral("初始文本"));

    Core::UndoStack stack;
    stack.setSaveCheckpoint(buffer.revision());
    QVERIFY(!stack.isDirty(buffer.revision()));

    // Edit 1: append "，第二段"
    Core::EditTransaction tx1;
    tx1.setSelectionBefore(Core::SelectionState(4, 4));
    tx1.addEdit(4, QString(), QStringLiteral("，第二段"));
    tx1.setSelectionAfter(Core::SelectionState(8, 8));
    tx1.setTimestamp(1000);
    stack.push(tx1, buffer);

    QCOMPARE(buffer.text(), QStringLiteral("初始文本，第二段"));
    QVERIFY(stack.isDirty(buffer.revision()));
    QVERIFY(stack.canUndo());

    // Undo -> back to initial
    Core::SelectionState sel;
    bool okUndo = stack.undo(buffer, sel);
    QVERIFY(okUndo);
    QCOMPARE(buffer.text(), QStringLiteral("初始文本"));
    QCOMPARE(sel.cursor, 4);
    // Returned to checkpoint -> must be clean!
    QVERIFY(!stack.isDirty(buffer.revision()));

    // Redo -> forward to modified
    bool okRedo = stack.redo(buffer, sel);
    QVERIFY(okRedo);
    QCOMPARE(buffer.text(), QStringLiteral("初始文本，第二段"));
    QCOMPARE(sel.cursor, 8);
    QVERIFY(stack.isDirty(buffer.revision()));
}

// Probe 4: Parser AST and Syntax range mapping
void TestProbes::testParserAndSyntaxRanges() {
    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral("# 主标题\n\n这是**加粗文字**和`代码`。\n\n- [ ] 待办事项\n"));

    Parser::MarkdownParserAdapter parser;
    auto root = parser.parse(buffer);

    QVERIFY(root != nullptr);
    QCOMPARE(root->type, Parser::NodeType::Document);

    // Look for heading
    bool foundHeading = false;
    bool foundStrong = false;
    bool foundCode = false;
    bool foundTaskItem = false;

    std::function<void(const Parser::SemanticNodePtr&)> walk = [&](const Parser::SemanticNodePtr &node) {
        if (!node) return;
        if (node->type == Parser::NodeType::Heading) {
            foundHeading = true;
            QCOMPARE(node->headingLevel, 1);
            QVERIFY(node->prefixRange.isValid());
        } else if (node->type == Parser::NodeType::Strong) {
            foundStrong = true;
            QVERIFY(node->prefixRange.isValid());
            QVERIFY(node->suffixRange.isValid());
            // "**加粗文字**" prefix is "**", suffix is "**"
            QCOMPARE(buffer.substring(node->prefixRange), QStringLiteral("**"));
            QCOMPARE(buffer.substring(node->suffixRange), QStringLiteral("**"));
            QCOMPARE(buffer.substring(node->contentRange), QStringLiteral("加粗文字"));
        } else if (node->type == Parser::NodeType::CodeSpan) {
            foundCode = true;
            QCOMPARE(buffer.substring(node->contentRange), QStringLiteral("代码"));
        } else if (node->type == Parser::NodeType::ListItem && node->isTaskItem) {
            foundTaskItem = true;
            QVERIFY(node->taskCheckboxRange.isValid());
            QCOMPARE(buffer.substring(node->taskCheckboxRange), QStringLiteral("[ ]"));
        }
        for (const auto &child : node->children) {
            walk(child);
        }
    };

    walk(root);

    QVERIFY(foundHeading);
    QVERIFY(foundStrong);
    QVERIFY(foundCode);
    QVERIFY(foundTaskItem);
}

// Probe 5: In-place live preview active vs inactive projection
void TestProbes::testLivePreviewActiveInactiveProjection() {
    Core::TextBuffer buffer;
    QString raw = QStringLiteral("文字包含**重要**标记");
    buffer.setText(raw);

    Parser::MarkdownParserAdapter parser;
    auto root = parser.parse(buffer);

    Mapping::DisplayProjection projection;

    // Case 1: Cursor is at position 0 (outside "**重要**")
    // Markers must be hidden! Visual text should be "文字包含重要标记"
    projection.buildProjection(root, buffer, 0, false);
    QVERIFY(projection.blockCount() > 0);
    const auto &b1 = projection.blocks().first();
    QCOMPARE(b1.visualText, QStringLiteral("文字包含重要标记"));

    // Case 2: Cursor is inside "**重要**" (e.g. offset 7, right at '重')
    // Markers must be expanded! Visual text should show "**重要**"
    int strongPos = raw.indexOf(QStringLiteral("重要"));
    projection.buildProjection(root, buffer, strongPos, false);
    const auto &b2 = projection.blocks().first();
    QCOMPARE(b2.visualText, raw); // full "**重要**" expanded for direct in-place editing!
}

// Probe 6: Safe atomic save and conflict detection
void TestProbes::testSafeAtomicSaveAndConflict() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString filePath = tempDir.filePath(QStringLiteral("test_doc.md"));

    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral("# 原始文档\n第一行数据\n"));

    // First save
    auto res1 = File::FileService::safeSaveFile(filePath, buffer, QByteArray());
    QVERIFY(res1.success);
    QVERIFY(QFile::exists(filePath));

    QByteArray knownHash = File::FileService::calculateFileHash(filePath);
    QVERIFY(!knownHash.isEmpty());

    // Modify buffer and save again
    buffer.replace(0, 5, QStringLiteral("# 修改后"));
    auto res2 = File::FileService::safeSaveFile(filePath, buffer, knownHash);
    QVERIFY(res2.success);

    QByteArray updatedHash = File::FileService::calculateFileHash(filePath);
    QVERIFY(updatedHash != knownHash);

    // Simulate external modification by writing directly behind the app's back
    QFile extFile(filePath);
    QVERIFY(extFile.open(QIODevice::WriteOnly));
    extFile.write("第三方程序外部篡改的内容");
    extFile.close();

    // Now attempt to save with the old hash -> must detect conflict!
    buffer.replace(0, 0, QStringLiteral("又做了新改动"));
    auto res3 = File::FileService::safeSaveFile(filePath, buffer, updatedHash);
    QVERIFY(!res3.success);
    QVERIFY(res3.hadConflict);
}

// Probe 7: Crash recovery snapshot
void TestProbes::testCrashRecoverySnapshot() {
    QString dummyPath = QStringLiteral("D:/dummy_workspace/important_notes.md");
    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral("未保存的珍贵草稿"));

    File::FileService::writeRecoverySnapshot(dummyPath, buffer);

    QString snapshotFile = File::FileService::findRecoverySnapshot(dummyPath);
    QVERIFY(!snapshotFile.isEmpty());
    QVERIFY(QFile::exists(snapshotFile));

    Core::TextBuffer recBuffer;
    QString err;
    QVERIFY(File::FileService::loadFile(snapshotFile, recBuffer, err));
    QCOMPARE(recBuffer.text(), QStringLiteral("未保存的珍贵草稿"));

    // Cleanup snapshot after save
    File::FileService::removeRecoverySnapshot(dummyPath);
    QVERIFY(File::FileService::findRecoverySnapshot(dummyPath).isEmpty());
}

// Probe 8: Hidden marker cursor stepping and canonical drop points
void TestProbes::testHiddenMarkerCursorStepping() {
    Core::TextBuffer buffer;
    // "前缀**加粗**后缀"
    // offset 0..1: "前缀"
    // offset 2..3: "**" (hidden marker prefix)
    // offset 4..5: "加粗"
    // offset 6..7: "**" (hidden marker suffix)
    // offset 8..9: "后缀"
    buffer.setText(QStringLiteral("前缀**加粗**后缀"));

    Parser::MarkdownParserAdapter parser;
    auto root = parser.parse(buffer);

    Mapping::DisplayProjection projection;
    // Cursor at offset 0 (outside)
    projection.buildProjection(root, buffer, 0, false);

    // Step forward from offset 2 (start of hidden "**"): must skip straight to offset 4!
    int steppedForward = projection.stepCursor(2, true, buffer);
    QCOMPARE(steppedForward, 4);

    // Step backward from offset 4 (start of content): must jump back to offset 2 (before hidden "**")!
    int steppedBackward = projection.stepCursor(4, false, buffer);
    QCOMPARE(steppedBackward, 2);

    // Normalize any illegal offset inside [2, 4] e.g. 3
    int normForward = projection.normalizeSourceCursor(3, true);
    QCOMPARE(normForward, 4);
    int normBackward = projection.normalizeSourceCursor(3, false);
    QCOMPARE(normBackward, 2);
}

// Probe 9: Find and batch atomic replace with one-step undo
void TestProbes::testFindAndBatchReplaceUndo() {
    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral("苹果很好吃，我喜欢苹果，大家都爱苹果。"));

    Core::UndoStack stack;
    stack.setSaveCheckpoint(buffer.revision());

    // Batch replace "苹果" with "香蕉" in a compound transaction
    QString findStr = QStringLiteral("苹果");
    QString repStr = QStringLiteral("香蕉");

    QVector<int> matches;
    int idx = 0;
    while ((idx = buffer.text().indexOf(findStr, idx)) != -1) {
        matches.append(idx);
        idx += findStr.length();
    }
    QCOMPARE(matches.size(), 3);

    Core::EditTransaction tx;
    for (int i = matches.size() - 1; i >= 0; --i) {
        tx.addEdit(matches[i], findStr, repStr);
    }
    tx.setSelectionBefore(Core::SelectionState(0, 0));
    tx.setSelectionAfter(Core::SelectionState(matches.first(), matches.first() + repStr.length()));
    stack.push(tx, buffer);

    QCOMPARE(buffer.text(), QStringLiteral("香蕉很好吃，我喜欢香蕉，大家都爱香蕉。"));
    QVERIFY(stack.isDirty(buffer.revision()));

    // One single undo must revert all 3 replacements at once back to baseline!
    Core::SelectionState sel;
    bool okUndo = stack.undo(buffer, sel);
    QVERIFY(okUndo);
    QCOMPARE(buffer.text(), QStringLiteral("苹果很好吃，我喜欢苹果，大家都爱苹果。"));
    QVERIFY(!stack.isDirty(buffer.revision())); // back to clean state
}

QTEST_MAIN(TestProbes)
#include "TestProbes.moc"
