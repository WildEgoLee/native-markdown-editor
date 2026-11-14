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
    void testFormattingChangeInvalidatesLayoutCache();
    void testIncrementalAndColdLayoutEquivalence();
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

// Probe 16: Latency Benchmark on 100 KB Document (with Warmup, Repetitions, and p50/p95 Metrics)
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

    // 1. Warmup iterations
    for (int w = 0; w < 2; ++w) {
        auto wRoot = parser.parse(buffer);
        Mapping::DisplayProjection wProj;
        wProj.buildProjection(wRoot, buffer, 0, false);
        Editor::LayoutEngine wEngine;
        wEngine.layout(wProj, 800.0);
    }

    // 2. Measure AST parsing time
    QElapsedTimer timer;
    timer.start();
    auto rootNode = parser.parse(buffer);
    qint64 parseTimeMs = timer.elapsed();
    QVERIFY(rootNode != nullptr);

    // 3. Measure DisplayProjection build time
    timer.restart();
    Mapping::DisplayProjection projection;
    bool initialChanged = projection.buildProjection(rootNode, buffer, 0, false);
    qint64 projTimeMs = timer.elapsed();
    QVERIFY(initialChanged);
    QVERIFY(projection.blockCount() > 500);

    // 4. Repeated measurement of Full Cold Layout (10 iterations)
    QVector<qreal> fullLayoutSamples;
    fullLayoutSamples.reserve(10);
    for (int iter = 0; iter < 10; ++iter) {
        Editor::LayoutEngine freshEngine;
        timer.restart();
        freshEngine.layout(projection, 800.0);
        fullLayoutSamples.append(timer.nsecsElapsed() / 1000000.0);
    }
    std::sort(fullLayoutSamples.begin(), fullLayoutSamples.end());
    qreal fullP50 = fullLayoutSamples[fullLayoutSamples.size() / 2];
    qreal fullP95 = fullLayoutSamples[static_cast<int>(fullLayoutSamples.size() * 0.95)];

    // 5. Initialise engine with base projection for incremental layout testing
    Editor::LayoutEngine engine;
    engine.layout(projection, 800.0);

    // 6. Repeated measurement of Incremental Keystroke Layout (10 iterations)
    // Modify one character in buffer (e.g. at offset 500)
    Core::TextBuffer editedBuffer;
    editedBuffer.setText(doc100k);
    editedBuffer.replace(500, 1, QStringLiteral("X"));
    auto editedRoot = parser.parse(editedBuffer);
    Mapping::DisplayProjection editedProjection;
    editedProjection.buildProjection(editedRoot, editedBuffer, 501, false);

    QVector<qreal> incLayoutSamples;
    incLayoutSamples.reserve(10);
    for (int iter = 0; iter < 10; ++iter) {
        timer.restart();
        engine.layout(editedProjection, 800.0);
        incLayoutSamples.append(timer.nsecsElapsed() / 1000000.0);
    }
    std::sort(incLayoutSamples.begin(), incLayoutSamples.end());
    qreal incP50 = incLayoutSamples[incLayoutSamples.size() / 2];
    qreal incP95 = incLayoutSamples[static_cast<int>(incLayoutSamples.size() * 0.95)];
    QVERIFY(engine.totalHeight() > 0);

    // 7. Measure Cursor Navigation with change detection:
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

    // 8. Measure hit test performance (binary search on the matching edited state!)
    timer.restart();
    int hitOff = engine.hitTest(QPointF(200, 1500), editedProjection);
    qint64 hitTestTimeUs = timer.nsecsElapsed() / 1000;
    QVERIFY(hitOff >= 0);

    qDebug() << "=== 100 KB Markdown Benchmark (p50 / p95) ===";
    qDebug() << "Document Size:" << byteSize / 1024 << "KB, Block Count:" << projection.blockCount();
    qDebug() << "AST Parse Time:" << parseTimeMs << "ms";
    qDebug() << "Display Projection Time:" << projTimeMs << "ms";
    qDebug() << "Full Cold Layout Time: p50 =" << fullP50 << "ms, p95 =" << fullP95 << "ms";
    qDebug() << "Incremental Keystroke Layout Time: p50 =" << incP50 << "ms, p95 =" << incP95 << "ms (Speedup: ~" << (fullP50 / qMax(0.01, incP50)) << "x)";
    qDebug() << "Cursor Navigation Check Time:" << cursorNavTimeNs / 1000.0 << "us (Changed:" << changedNormal << ")";
    qDebug() << "HitTest Time (Binary Search):" << hitTestTimeUs << "us";

    // Performance assertions:
    // 1. Initial parse < 150ms
    QVERIFY(parseTimeMs < 150);
    // 2. Incremental keystroke layout must be < 5ms (p50)
    QVERIFY(incP50 < 5.0);
    // 3. Cursor navigation check within 10ms (well under 60fps budget of 16.6ms)
    QVERIFY(cursorNavTimeNs < 10 * 1000 * 1000);
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

// Probe 19: LayoutEngine cache invalidation and DisplayProjection change detection on style changes with identical visible text
void TestProbes::testFormattingChangeInvalidatesLayoutCache() {
    Parser::MarkdownParserAdapter parser;
    Mapping::DisplayProjection projection1;
    Mapping::DisplayProjection projection2;
    Editor::LayoutEngine engine;

    // --- Scenario 1: Bold -> Italic with identical visible text ---
    Core::TextBuffer buf1;
    buf1.setText(QStringLiteral("前缀**重要**后缀\n"));

    auto root1 = parser.parse(buf1);
    bool proj1Changed = projection1.buildProjection(root1, buf1, 0, false);
    QVERIFY(proj1Changed);
    QCOMPARE(projection1.blockCount(), 1);

    // Layout the initial document
    engine.layout(projection1, 600.0);
    QCOMPARE(engine.blocks().size(), 1);

    // Verify initial format contains Bold
    const auto &formats1 = engine.blocks()[0].layout->formats();
    bool foundBold = false;
    for (const auto &fmtRange : formats1) {
        if (fmtRange.format.fontWeight() == QFont::Bold) {
            foundBold = true;
            break;
        }
    }
    QVERIFY(foundBold);

    // Now change to Italic: *重要*
    Core::TextBuffer buf2;
    buf2.setText(QStringLiteral("前缀*重要*后缀\n"));

    auto root2 = parser.parse(buf2);
    // Crucial check: DisplayProjection MUST detect that format changed even though visible text is still "前缀重要后缀"
    bool proj2Changed = projection2.buildProjection(root2, buf2, 0, false);
    QVERIFY(proj2Changed);
    QCOMPARE(projection2.blocks()[0].visualText, projection1.blocks()[0].visualText); // Visible text is identical!

    // Layout again with engine (incremental layout)
    engine.layout(projection2, 600.0);
    QCOMPARE(engine.blocks().size(), 1);

    // Crucial check: engine must NOT have reused the old bold layout! It must now be Italic and NOT Bold.
    const auto &formats2 = engine.blocks()[0].layout->formats();
    bool foundItalic = false;
    bool foundBoldAfter = false;
    for (const auto &fmtRange : formats2) {
        if (fmtRange.format.fontItalic()) {
            foundItalic = true;
        }
        if (fmtRange.format.fontWeight() == QFont::Bold) {
            foundBoldAfter = true;
        }
    }
    QVERIFY(foundItalic);
    QVERIFY(!foundBoldAfter);

    // --- Scenario 2: Heading Level Change (# 大标题 -> ## 大标题) with identical visible text ---
    Core::TextBuffer bufH1;
    bufH1.setText(QStringLiteral("# 大标题\n"));
    auto rootH1 = parser.parse(bufH1);
    Mapping::DisplayProjection projH1;
    projH1.buildProjection(rootH1, bufH1, 0, false);
    engine.layout(projH1, 600.0);

    qreal h1FontSize = 0;
    if (!engine.blocks().isEmpty() && !engine.blocks()[0].layout->formats().isEmpty()) {
        h1FontSize = engine.blocks()[0].layout->formats()[0].format.fontPointSize();
    }
    QCOMPARE(h1FontSize, 24.0);

    Core::TextBuffer bufH2;
    bufH2.setText(QStringLiteral("## 大标题\n"));
    auto rootH2 = parser.parse(bufH2);
    Mapping::DisplayProjection projH2;
    bool projH2Changed = projH2.buildProjection(rootH2, bufH2, 0, false);
    QVERIFY(projH2Changed);

    engine.layout(projH2, 600.0);
    qreal h2FontSize = 0;
    if (!engine.blocks().isEmpty() && !engine.blocks()[0].layout->formats().isEmpty()) {
        h2FontSize = engine.blocks()[0].layout->formats()[0].format.fontPointSize();
    }
    QCOMPARE(h2FontSize, 20.0);
    QVERIFY(h1FontSize > h2FontSize);
}

// Probe 20: Mathematical Equivalence between Incremental Cached Layout and Fresh Cold Layout
void TestProbes::testIncrementalAndColdLayoutEquivalence() {
    Parser::MarkdownParserAdapter parser;
    qreal contentWidth = 650.0;

    auto assertEnginesEquivalent = [contentWidth](const Editor::LayoutEngine &inc,
                                                  const Editor::LayoutEngine &cold,
                                                  const Mapping::DisplayProjection &proj,
                                                  int textLength) {
        QVERIFY(qAbs(inc.totalHeight() - cold.totalHeight()) < 0.001);
        QCOMPARE(inc.blocks().size(), cold.blocks().size());

        for (int i = 0; i < inc.blocks().size(); ++i) {
            const auto &bInc = inc.blocks()[i];
            const auto &bCold = cold.blocks()[i];

            QVERIFY(qAbs(bInc.rect.x() - bCold.rect.x()) < 0.001);
            QVERIFY(qAbs(bInc.rect.y() - bCold.rect.y()) < 0.001);
            QVERIFY(qAbs(bInc.rect.width() - bCold.rect.width()) < 0.001);
            QVERIFY(qAbs(bInc.rect.height() - bCold.rect.height()) < 0.001);

            QCOMPARE(bInc.layout->lineCount(), bCold.layout->lineCount());
            for (int l = 0; l < bInc.layout->lineCount(); ++l) {
                auto lInc = bInc.layout->lineAt(l);
                auto lCold = bCold.layout->lineAt(l);
                QVERIFY(qAbs(lInc.rect().x() - lCold.rect().x()) < 0.001);
                QVERIFY(qAbs(lInc.rect().y() - lCold.rect().y()) < 0.001);
                QVERIFY(qAbs(lInc.rect().width() - lCold.rect().width()) < 0.001);
                QVERIFY(qAbs(lInc.rect().height() - lCold.rect().height()) < 0.001);
            }
        }

        // Check cursor rects across sample offsets
        for (int off = 0; off <= textLength; off += qMax(1, textLength / 15)) {
            QRectF crInc = inc.cursorRect(off, proj);
            QRectF crCold = cold.cursorRect(off, proj);
            QVERIFY(qAbs(crInc.x() - crCold.x()) < 0.001);
            QVERIFY(qAbs(crInc.y() - crCold.y()) < 0.001);
            QVERIFY(qAbs(crInc.width() - crCold.width()) < 0.001);
            QVERIFY(qAbs(crInc.height() - crCold.height()) < 0.001);
        }

        // Check hit testing across sample points
        for (qreal y = 10.0; y < inc.totalHeight(); y += qMax(10.0, inc.totalHeight() / 15)) {
            for (qreal x = 25.0; x <= contentWidth - 25.0; x += 150.0) {
                QPointF pt(x, y);
                int hitInc = inc.hitTest(pt, proj);
                int hitCold = cold.hitTest(pt, proj);
                QCOMPARE(hitInc, hitCold);
            }
        }
    };

    // Stage 0: Initial base multi-element document
    QString doc0 = QStringLiteral(
        "# 顶层标题\n\n"
        "这是第一段正文，包含了**粗体**与*斜体*以及`行内代码`的内容测试。\n\n"
        "- [x] 任务一已完成\n"
        "- [ ] 任务二进行中\n\n"
        "> 这里是一段长引用内容，用于测试引用块的边距与排版稳定性。\n\n"
        "```cpp\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n"
        "```\n\n"
        "这是末尾的一段普通正文段落。\n"
    );

    Core::TextBuffer buf;
    buf.setText(doc0);

    Editor::LayoutEngine incEngine; // This engine persists across all edits, utilizing incremental caching

    auto root0 = parser.parse(buf);
    Mapping::DisplayProjection proj0;
    proj0.buildProjection(root0, buf, 0, false);

    incEngine.layout(proj0, contentWidth);

    Editor::LayoutEngine coldEngine0;
    coldEngine0.layout(proj0, contentWidth);

    assertEnginesEquivalent(incEngine, coldEngine0, proj0, buf.length());

    // Stage 1: Keystroke edit inside first paragraph
    int editPos = doc0.indexOf(QStringLiteral("包含"));
    QVERIFY(editPos > 0);
    buf.replace(editPos, 2, QStringLiteral("详细包含了经过扩充的更多文字信息"));

    auto root1 = parser.parse(buf);
    Mapping::DisplayProjection proj1;
    proj1.buildProjection(root1, buf, editPos + 5, false);

    incEngine.layout(proj1, contentWidth); // Incremental layout with cached prefix/suffix blocks!

    Editor::LayoutEngine coldEngine1;
    coldEngine1.layout(proj1, contentWidth); // Cold layout with no cache

    assertEnginesEquivalent(incEngine, coldEngine1, proj1, buf.length());

    // Stage 2: Format toggle on another block (Heading 1 -> Heading 2)
    buf.replace(0, 1, QStringLiteral("##")); // Change "# " to "## "

    auto root2 = parser.parse(buf);
    Mapping::DisplayProjection proj2;
    proj2.buildProjection(root2, buf, 0, false);

    incEngine.layout(proj2, contentWidth);

    Editor::LayoutEngine coldEngine2;
    coldEngine2.layout(proj2, contentWidth);

    assertEnginesEquivalent(incEngine, coldEngine2, proj2, buf.length());

    // Stage 3: Insert a whole new section block into the document
    int insertPos = buf.text().indexOf(QStringLiteral("```cpp"));
    QVERIFY(insertPos > 0);
    buf.replace(insertPos, 0, QStringLiteral("### 插入的新章节\n新插入的段落文本，测试块插入缓存处理。\n\n"));

    auto root3 = parser.parse(buf);
    Mapping::DisplayProjection proj3;
    proj3.buildProjection(root3, buf, insertPos, false);

    incEngine.layout(proj3, contentWidth);

    Editor::LayoutEngine coldEngine3;
    coldEngine3.layout(proj3, contentWidth);

    assertEnginesEquivalent(incEngine, coldEngine3, proj3, buf.length());

    // Stage 4: Delete a block (delete the blockquote)
    int quoteStart = buf.text().indexOf(QStringLiteral("> 这里"));
    int quoteEnd = buf.text().indexOf(QStringLiteral("### 插入"));
    QVERIFY(quoteStart > 0 && quoteEnd > quoteStart);
    buf.replace(quoteStart, quoteEnd - quoteStart, QString());

    auto root4 = parser.parse(buf);
    Mapping::DisplayProjection proj4;
    proj4.buildProjection(root4, buf, quoteStart, false);

    incEngine.layout(proj4, contentWidth);

    Editor::LayoutEngine coldEngine4;
    coldEngine4.layout(proj4, contentWidth);

    assertEnginesEquivalent(incEngine, coldEngine4, proj4, buf.length());
}

QTEST_MAIN(TestProbes)
#include "TestProbes.moc"
