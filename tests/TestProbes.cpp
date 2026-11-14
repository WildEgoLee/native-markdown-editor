#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>

#include "core/Position.h"
#include "core/TextBuffer.h"
#include "core/EditTransaction.h"
#include "core/UndoStack.h"
#include "parser/MarkdownParserAdapter.h"
#include "mapping/DisplayProjection.h"
#include "editor/LayoutEngine.h"
#include "file/FileService.h"
#include "file/DocumentSession.h"

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
    void testUndoBranchSafetyAndSavepoints();
    void testSaveBreaksMergeGroup();
    void testExactSourceModeProjection();
    void testComplexGraphemeClustersAndUnicode();
    void testIMEPreeditLayoutAndCoordinates();
    void testDocumentSessionTransactionalSaveAs();
    void testPerformanceBaselineAndLatency();
    void testRecoveryLoadRetainsSnapshotAndDirtyState();
    void testRecoveryDiscardRemovesSnapshot();
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
    stack.setSaveCheckpoint();
    QVERIFY(!stack.isDirty());

    // Edit 1: append "，第二段"
    Core::EditTransaction tx1;
    tx1.setSelectionBefore(Core::SelectionState(4, 4));
    tx1.addEdit(4, QString(), QStringLiteral("，第二段"));
    tx1.setSelectionAfter(Core::SelectionState(8, 8));
    tx1.setTimestamp(1000);
    stack.push(tx1, buffer);

    QCOMPARE(buffer.text(), QStringLiteral("初始文本，第二段"));
    QVERIFY(stack.isDirty());
    QVERIFY(stack.canUndo());

    // Undo -> back to initial
    Core::SelectionState sel;
    bool okUndo = stack.undo(buffer, sel);
    QVERIFY(okUndo);
    QCOMPARE(buffer.text(), QStringLiteral("初始文本"));
    QCOMPARE(sel.cursor, 4);
    // Returned to checkpoint -> must be clean!
    QVERIFY(!stack.isDirty());

    // Redo -> forward to modified
    bool okRedo = stack.redo(buffer, sel);
    QVERIFY(okRedo);
    QCOMPARE(buffer.text(), QStringLiteral("初始文本，第二段"));
    QCOMPARE(sel.cursor, 8);
    QVERIFY(stack.isDirty());
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
    stack.setSaveCheckpoint();

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
    QVERIFY(stack.isDirty());

    // One single undo must revert all 3 replacements at once back to baseline!
    Core::SelectionState sel;
    bool okUndo = stack.undo(buffer, sel);
    QVERIFY(okUndo);
    QCOMPARE(buffer.text(), QStringLiteral("苹果很好吃，我喜欢苹果，大家都爱苹果。"));
    QVERIFY(!stack.isDirty()); // back to clean state
}

// Probe 10: Undo branch safety (Save A -> Modify B -> Undo to A -> Branch Modify C)
void TestProbes::testUndoBranchSafetyAndSavepoints() {
    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral("版本A"));

    Core::UndoStack stack;
    stack.setSaveCheckpoint();
    QVERIFY(!stack.isDirty());

    // Step B: Append "修改B"
    Core::EditTransaction txB;
    txB.addEdit(3, QString(), QStringLiteral("-修改B"));
    stack.push(txB, buffer);
    QCOMPARE(buffer.text(), QStringLiteral("版本A-修改B"));
    QVERIFY(stack.isDirty());

    // Undo back to A
    Core::SelectionState sel;
    QVERIFY(stack.undo(buffer, sel));
    QCOMPARE(buffer.text(), QStringLiteral("版本A"));
    // Exactly at savepoint -> Clean!
    QVERIFY(!stack.isDirty());

    // Branch Step C: Append "修改C" (discards redo branch)
    Core::EditTransaction txC;
    txC.addEdit(3, QString(), QStringLiteral("-修改C"));
    stack.push(txC, buffer);
    QCOMPARE(buffer.text(), QStringLiteral("版本A-修改C"));
    // Must be Dirty! (Must NEVER mistakenly match savepoint)
    QVERIFY(stack.isDirty());

    // Undo back to A again
    QVERIFY(stack.undo(buffer, sel));
    QCOMPARE(buffer.text(), QStringLiteral("版本A"));
    QVERIFY(!stack.isDirty());
}

// Probe 11: Save checkpoint breaks continuous merge group
void TestProbes::testSaveBreaksMergeGroup() {
    Core::TextBuffer buffer;
    buffer.setText(QString());

    Core::UndoStack stack;

    // Type 'a'
    Core::EditTransaction tx1;
    tx1.addEdit(0, QString(), QStringLiteral("a"));
    tx1.setTimestamp(100);
    stack.push(tx1, buffer);

    // Save checkpoint at 'a'
    stack.setSaveCheckpoint();
    QVERIFY(!stack.isDirty());

    // Type 'b' immediately (even if within 100ms)
    Core::EditTransaction tx2;
    tx2.addEdit(1, QString(), QStringLiteral("b"));
    tx2.setTimestamp(150);
    stack.push(tx2, buffer);

    QCOMPARE(buffer.text(), QStringLiteral("ab"));
    QVERIFY(stack.isDirty());

    // Undo must ONLY revert 'b' back to saved checkpoint 'a', NEVER revert the whole merged group!
    Core::SelectionState sel;
    QVERIFY(stack.undo(buffer, sel));
    QCOMPARE(buffer.text(), QStringLiteral("a"));
    QVERIFY(!stack.isDirty()); // perfectly clean at savepoint!
}

// Probe 12: Source mode 100% exact raw projection
void TestProbes::testExactSourceModeProjection() {
    Core::TextBuffer buffer;
    // An unfinished code block and weird indentation
    QString raw = QStringLiteral("```python\ndef test():\n    print('hello')\n");
    buffer.setText(raw);

    Parser::MarkdownParserAdapter parser;
    auto root = parser.parse(buffer);

    Mapping::DisplayProjection projection;
    projection.buildProjection(root, buffer, 0, true); // true = isSourceMode

    // Source mode must exactly match each line of raw buffer without stripping backticks or fences
    QString reconstructed;
    for (int i = 0; i < projection.blockCount(); ++i) {
        if (i > 0) reconstructed.append(QStringLiteral("\n"));
        reconstructed.append(projection.blocks()[i].visualText);
    }

    QCOMPARE(reconstructed.trimmed(), raw.trimmed());

    // First block visualText must be "```python"
    QCOMPARE(projection.blocks()[0].visualText, QStringLiteral("```python"));
    // Second block visualText must be "def test():"
    QCOMPARE(projection.blocks()[1].visualText, QStringLiteral("def test():"));
    // Third block visualText must preserve the 4 spaces: "    print('hello')"
    QCOMPARE(projection.blocks()[2].visualText, QStringLiteral("    print('hello')"));
}

// Probe 13: Complex Grapheme Clusters, ZWJ sequences, skin-tone modifiers, combining diacritics
void TestProbes::testComplexGraphemeClustersAndUnicode() {
    // 1. ZWJ emoji: 👨‍👩‍👧‍👦 (Man + ZWJ + Woman + ZWJ + Girl + ZWJ + Boy)
    QString zwjFamily = QStringLiteral("👨‍👩‍👧‍👦");
    QCOMPARE(zwjFamily.length(), 11);
    int nextFrom0 = Core::PositionTranslator::nextGraphemeOffset(zwjFamily, 0);
    QCOMPARE(nextFrom0, 11);
    int prevFrom11 = Core::PositionTranslator::prevGraphemeOffset(zwjFamily, 11);
    QCOMPARE(prevFrom11, 0);

    // 2. Emoji with skin tone modifier: 👍🏽 (U+1F44D + U+1F3FD)
    QString skinTone = QStringLiteral("👍🏽");
    QCOMPARE(skinTone.length(), 4);
    QCOMPARE(Core::PositionTranslator::nextGraphemeOffset(skinTone, 0), 4);
    QCOMPARE(Core::PositionTranslator::prevGraphemeOffset(skinTone, 4), 0);

    // 3. Combining diacritical mark: e + \u0301 (é)
    QString combining = QStringLiteral("e\u0301");
    QCOMPARE(combining.length(), 2);
    QCOMPARE(Core::PositionTranslator::nextGraphemeOffset(combining, 0), 2);
    QCOMPARE(Core::PositionTranslator::prevGraphemeOffset(combining, 2), 0);

    // 4. Windows CRLF in mixed text: "A\r\nB"
    QString crlfSample = QStringLiteral("A\r\nB");
    QCOMPARE(Core::PositionTranslator::nextGraphemeOffset(crlfSample, 1), 3); // from \r past \n to B
    QCOMPARE(Core::PositionTranslator::prevGraphemeOffset(crlfSample, 3), 1); // from B past \r\n to A

    // 5. Mixed sentence with Unicode clusters
    QString mixed = QStringLiteral("你好 👨‍👩‍👧‍👦! 👍🏽 done");
    int cur = 0;
    while (cur < mixed.length()) {
        int next = Core::PositionTranslator::nextGraphemeOffset(mixed, cur);
        QVERIFY(next > cur);
        int back = Core::PositionTranslator::prevGraphemeOffset(mixed, next);
        QCOMPARE(back, cur);
        cur = next;
    }
    QCOMPARE(cur, mixed.length());
}

// Probe 14: IME Preedit String Embedded into Visual Layout Engine
void TestProbes::testIMEPreeditLayoutAndCoordinates() {
    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral("# 标题\n这是一段测试正文内容。"));

    Parser::MarkdownParserAdapter parser;
    auto root = parser.parse(buffer);

    Mapping::DisplayProjection projection;
    projection.buildProjection(root, buffer, 0, false);

    Editor::LayoutEngine engine;
    qreal contentWidth = 600.0;

    // Normal layout
    engine.layout(projection, contentWidth);
    QVERIFY(engine.totalHeight() > 0);
    int initialBlockCount = engine.blocks().size();
    QVERIFY(initialBlockCount >= 2);

    // Now insert IME preedit: user is typing pinyin "ceshi" at offset 10 ("这是一段" after '段')
    int preeditOffset = 10;
    QString preeditStr = QStringLiteral("ceshi");
    engine.layout(projection, contentWidth, preeditOffset, preeditStr);

    // Verify block containing preedit has shifted text and contains "ceshi"
    int targetBlockIdx = projection.findBlockIndexBySourceOffset(preeditOffset);
    QVERIFY(targetBlockIdx >= 0 && targetBlockIdx < engine.blocks().size());
    const auto &bData = engine.blocks()[targetBlockIdx];
    QVERIFY(bData.layout->text().contains(QStringLiteral("ceshi")));

    // Verify cursor rect during preedit
    QRectF cRect = engine.cursorRect(preeditOffset, projection);
    QVERIFY(cRect.isValid());
    QVERIFY(cRect.width() > 0);
    QVERIFY(cRect.height() > 0);
}

// Probe 15: DocumentSession Transactional Save As
void TestProbes::testDocumentSessionTransactionalSaveAs() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString origFile = tempDir.filePath(QStringLiteral("original.md"));
    QString targetFile = tempDir.filePath(QStringLiteral("save_as.md"));

    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral("原文档内容"));

    File::DocumentSession session;
    session.resetToUntitled(buffer);
    QVERIFY(session.isUntitled());

    // Save as targetFile
    auto res = session.saveAs(targetFile, buffer);
    QVERIFY(res.success);
    QCOMPARE(session.filePath(), targetFile);
    QVERIFY(!session.isUntitled());
    QVERIFY(QFile::exists(targetFile));

    // Now attempt saveAs to an invalid path with illegal characters
    QString invalidPath = tempDir.filePath(QStringLiteral("invalid_*<>?.md"));
    auto failRes = session.saveAs(invalidPath, buffer);
    QVERIFY(!failRes.success);
    // Crucial check: m_currentFilePath must NOT have changed on failure!
    QCOMPARE(session.filePath(), targetFile);
}

// Probe 16: Latency Benchmark on 100 KB Document
void TestProbes::testPerformanceBaselineAndLatency() {
    // Generate ~100 KB realistic Markdown content (~2000 lines)
    QString doc100k;
    doc100k.reserve(120 * 1024);
    doc100k.append(QStringLiteral("# 性能基准测试文档 100KB\n\n"));
    for (int i = 0; i < 300; ++i) {
        doc100k.append(QStringLiteral("## 章节 %1: 原生架构与性能指标\n\n").arg(i + 1));
        doc100k.append(QStringLiteral("这是一个由 **C++20** 与 **Qt 6** 原生驱动的高性能 Markdown 编辑器。\n"));
        doc100k.append(QStringLiteral("- [x] 100% 原始字节一致性\n- [ ] 即时预览无闪烁\n- [x] 原生 IME 预编辑推开\n\n"));
        doc100k.append(QStringLiteral("> 引用内容：测试无 WebView 下的纯原生文本排版效率与性能。\n\n"));
        doc100k.append(QStringLiteral("```cpp\nvoid process(int x) {\n    int y = x * 2;\n}\n```\n\n"));
        doc100k.append(QStringLiteral("| 参数 | 数值 | 状态 |\n|---|---|---|\n| p50 延迟 | 0.8ms | 优秀 |\n| 内存开销 | 45MB | 极低 |\n\n"));
    }

    int byteSize = doc100k.toUtf8().size();
    QVERIFY(byteSize >= 90 * 1024); // at least ~90-100 KB

    Core::TextBuffer buffer;
    buffer.setText(doc100k);

    Parser::MarkdownParserAdapter parser;

    // 1. Measure AST parsing time
    QElapsedTimer timer;
    timer.start();
    auto rootNode = parser.parse(buffer);
    qint64 parseTimeMs = timer.elapsed();
    QVERIFY(rootNode != nullptr);

    // 2. Measure DisplayProjection build time
    timer.restart();
    Mapping::DisplayProjection projection;
    bool initialChanged = projection.buildProjection(rootNode, buffer, 0, false);
    qint64 projTimeMs = timer.elapsed();
    QVERIFY(initialChanged);
    QVERIFY(projection.blockCount() > 500);

    // 3. Measure LayoutEngine layout time
    timer.restart();
    Editor::LayoutEngine engine;
    engine.layout(projection, 800.0);
    qint64 layoutTimeMs = timer.elapsed();
    QVERIFY(engine.totalHeight() > 0);

    // 4. Measure Cursor Navigation with change detection:
    // First, cursor at body offset 100
    projection.buildProjection(rootNode, buffer, 100, false);
    // Moving cursor from 100 to 105 within normal text -> projection unchanged!
    timer.restart();
    bool changedNormal = projection.buildProjection(rootNode, buffer, 105, false);
    qint64 cursorNavTimeNs = timer.nsecsElapsed();
    // Must NOT have changed visual projection!
    QVERIFY(!changedNormal);

    // Moving cursor to offset 0 (into the Heading '# ') -> projection toggles marker!
    bool changedHeading = projection.buildProjection(rootNode, buffer, 0, false);
    QVERIFY(changedHeading);

    // 5. Measure hit test performance
    timer.restart();
    int hitOff = engine.hitTest(QPointF(200, 1500), projection);
    qint64 hitTestTimeUs = timer.nsecsElapsed() / 1000;
    QVERIFY(hitOff >= 0);

    qDebug() << "=== 100 KB Markdown Benchmark Results ===";
    qDebug() << "Document Size:" << byteSize / 1024 << "KB, Block Count:" << projection.blockCount();
    qDebug() << "AST Parse Time:" << parseTimeMs << "ms";
    qDebug() << "Display Projection Time:" << projTimeMs << "ms";
    qDebug() << "LayoutEngine Layout Time:" << layoutTimeMs << "ms";
    qDebug() << "Cursor Navigation Check Time:" << cursorNavTimeNs / 1000.0 << "us (Changed:" << changedNormal << ")";
    qDebug() << "HitTest Time:" << hitTestTimeUs << "us";

    // Performance assertions: 100KB document (2400 blocks) must parse within 150ms and cursor navigation check within 10ms (well under 60fps budget of 16.6ms)
    QVERIFY(parseTimeMs < 150);
    QVERIFY(cursorNavTimeNs < 10 * 1000 * 1000); // < 10ms
}

// Probe 17: Crash recovery load preserves recovered draft, sets dirty state, retains snapshot until saved
void TestProbes::testRecoveryLoadRetainsSnapshotAndDirtyState() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString filePath = tempDir.filePath(QStringLiteral("important_report.md"));

    // 1. Initial disk content
    Core::TextBuffer diskBuffer;
    diskBuffer.setText(QStringLiteral("# 磁盘原始基线内容\n这是保存过的旧版本。"));
    auto saveRes = File::FileService::safeSaveFile(filePath, diskBuffer, QByteArray());
    QVERIFY(saveRes.success);

    // 2. Simulate unsaved edits before crash and write recovery snapshot
    Core::TextBuffer draftBuffer;
    draftBuffer.setText(QStringLiteral("# 崩溃前未保存的珍贵草稿\n这是用户写了半小时的新内容！"));
    File::FileService::writeRecoverySnapshot(filePath, draftBuffer);

    QString snapshotFile = File::FileService::findRecoverySnapshot(filePath);
    QVERIFY(!snapshotFile.isEmpty());
    QVERIFY(QFile::exists(snapshotFile));

    // 3. User re-opens document and confirms "Load from recovery"
    Core::TextBuffer editorBuffer;
    Core::UndoStack undoStack;
    File::DocumentSession session;

    QString loadErr;
    QVERIFY(File::FileService::loadFile(snapshotFile, editorBuffer, loadErr));
    // Verify buffer actually contains recovered draft, NOT overwritten by disk
    QCOMPARE(editorBuffer.text(), draftBuffer.text());

    // Attach file identity and disk baseline hash
    session.attachFile(filePath);
    undoStack.clear(true); // start as dirty

    // 4. Assertions on recovery state:
    // - Editor buffer retains recovered draft
    QCOMPARE(editorBuffer.text(), draftBuffer.text());
    // - Document is Dirty! (User must be aware this is uncommitted recovery)
    QVERIFY(undoStack.isDirty());
    // - Snapshot file STILL exists on disk to protect against consecutive crash before save
    QVERIFY(QFile::exists(snapshotFile));

    // 5. User executes Save
    auto saveRecoveredRes = session.save(editorBuffer);
    QVERIFY(saveRecoveredRes.success);
    undoStack.setSaveCheckpoint();
    QVERIFY(!undoStack.isDirty());

    // 6. Verify:
    // - Original disk file now has the recovered content
    Core::TextBuffer verifyBuffer;
    QVERIFY(File::FileService::loadFile(filePath, verifyBuffer, loadErr));
    QCOMPARE(verifyBuffer.text(), draftBuffer.text());
    // - Recovery snapshot was safely cleaned up upon successful save
    QVERIFY(File::FileService::findRecoverySnapshot(filePath).isEmpty());
}

// Probe 18: Crash recovery discard cleanly removes snapshot and opens disk original
void TestProbes::testRecoveryDiscardRemovesSnapshot() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QString filePath = tempDir.filePath(QStringLiteral("discard_test.md"));

    // 1. Initial disk content
    Core::TextBuffer diskBuffer;
    diskBuffer.setText(QStringLiteral("# 磁盘原始内容"));
    QVERIFY(File::FileService::safeSaveFile(filePath, diskBuffer, QByteArray()).success);

    // 2. Recovery snapshot
    Core::TextBuffer draftBuffer;
    draftBuffer.setText(QStringLiteral("# 错误的草稿（用户想丢弃）"));
    File::FileService::writeRecoverySnapshot(filePath, draftBuffer);
    QVERIFY(!File::FileService::findRecoverySnapshot(filePath).isEmpty());

    // 3. User clicks NO (Discard recovery)
    File::FileService::removeRecoverySnapshot(filePath);
    QVERIFY(File::FileService::findRecoverySnapshot(filePath).isEmpty());

    // 4. Load from original disk file
    Core::TextBuffer editorBuffer;
    File::DocumentSession session;
    Core::UndoStack undoStack;
    QString loadErr;
    QVERIFY(session.load(filePath, editorBuffer, loadErr));
    undoStack.clear(false);
    undoStack.setSaveCheckpoint();

    // Verify buffer has disk content and state is clean
    QCOMPARE(editorBuffer.text(), diskBuffer.text());
    QVERIFY(!undoStack.isDirty());
}

QTEST_MAIN(TestProbes)
#include "TestProbes.moc"
