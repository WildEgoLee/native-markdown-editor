#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QImage>
#include <cmath>

#include "core/Position.h"
#include "core/TextBuffer.h"
#include "core/EditTransaction.h"
#include "core/UndoStack.h"
#include "parser/MarkdownParserAdapter.h"
#include "mapping/DisplayProjection.h"
#include "editor/LayoutEngine.h"
#include "editor/NativeEditorView.h"
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
    void testWindowWidthChangeReflow();
    void testCacheHitPreservesBlockLayoutIdentity();
    void testDocumentScalePerformanceMatrix();
    void testIncrementalAstParseAndProjectionEquivalence();
    void testGuiEventDrivenInputLatency();
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

// Probe 16: Latency Benchmark on 100 KB Document (3-Tier Metrics, Multi-Iteration Real Edits, p50/p95, and Dispersion)
void TestProbes::testPerformanceBaselineAndLatency() {
    // Generate ~137 KB realistic Markdown content (~2400 blocks)
    QString doc100k;
    doc100k.reserve(150 * 1024);
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
    QVERIFY(byteSize >= 90 * 1024);

    Core::TextBuffer buffer;
    buffer.setText(doc100k);

    Parser::MarkdownParserAdapter parser;

    // Helper for computing comprehensive descriptive statistics
    struct Stats {
        qreal min = 0;
        qreal max = 0;
        qreal p50 = 0;
        qreal p95 = 0;
        qreal mean = 0;
        qreal stddev = 0;
    };

    auto calcStats = [](QVector<qreal> samples) -> Stats {
        if (samples.isEmpty()) return {};
        std::sort(samples.begin(), samples.end());
        Stats s;
        s.min = samples.first();
        s.max = samples.last();
        s.p50 = samples[samples.size() / 2];
        s.p95 = samples[static_cast<int>(samples.size() * 0.95)];
        qreal sum = 0;
        for (qreal v : samples) sum += v;
        s.mean = sum / samples.size();
        qreal varSum = 0;
        for (qreal v : samples) varSum += (v - s.mean) * (v - s.mean);
        s.stddev = std::sqrt(varSum / samples.size());
        return s;
    };

    // 1. Warmup passes (2 runs)
    for (int w = 0; w < 2; ++w) {
        auto wRoot = parser.parse(buffer);
        Mapping::DisplayProjection wProj;
        wProj.buildProjection(wRoot, buffer, 0, false);
        Editor::LayoutEngine wEngine;
        wEngine.layout(wProj, 800.0);
    }

    // 2. Initial baseline parse and projection
    QElapsedTimer timer;
    auto rootNode = parser.parse(buffer);
    QVERIFY(rootNode != nullptr);

    Mapping::DisplayProjection baseProjection;
    bool initialChanged = baseProjection.buildProjection(rootNode, buffer, 0, false);
    QVERIFY(initialChanged);
    QVERIFY(baseProjection.blockCount() > 500);

    // 3. Repeated measurement of Full Cold Layout (10 iterations)
    QVector<qreal> fullColdSamples;
    fullColdSamples.reserve(10);
    for (int iter = 0; iter < 10; ++iter) {
        Editor::LayoutEngine freshEngine;
        timer.restart();
        freshEngine.layout(baseProjection, 800.0);
        fullColdSamples.append(timer.nsecsElapsed() / 1000000.0);
    }
    Stats coldStats = calcStats(fullColdSamples);

    // 4. Repeated measurement of Stable Identity Layout (10 iterations without edits)
    Editor::LayoutEngine engine;
    engine.layout(baseProjection, 800.0);

    QVector<qreal> stableLayoutSamples;
    stableLayoutSamples.reserve(10);
    for (int iter = 0; iter < 10; ++iter) {
        timer.restart();
        engine.layout(baseProjection, 800.0);
        stableLayoutSamples.append(timer.nsecsElapsed() / 1000000.0);
    }
    Stats stableStats = calcStats(stableLayoutSamples);

    // 5. Multi-Iteration Real Keystroke Edits (30 independent real single-character edits across varying blocks)
    const int numEditIterations = 30;
    QVector<qreal> parseSamples;
    QVector<qreal> projSamples;
    QVector<qreal> incLayoutSamples;
    QVector<qreal> paintSamples;
    QVector<qreal> e2eSamples;

    parseSamples.reserve(numEditIterations);
    projSamples.reserve(numEditIterations);
    incLayoutSamples.reserve(numEditIterations);
    paintSamples.reserve(numEditIterations);
    e2eSamples.reserve(numEditIterations);

    Core::TextBuffer liveBuffer;
    liveBuffer.setText(doc100k);
    Mapping::DisplayProjection liveProjection;
    liveProjection.buildProjection(rootNode, liveBuffer, 0, false);

    // Raster buffer for simulating 1080p viewport rendering
    QImage viewportImage(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    QRectF visibleViewport(0, 500.0, 800.0, 1080.0);

    for (int i = 0; i < numEditIterations; ++i) {
        // Pick an edit offset distributed across chapters to ensure distinct blocks are edited
        int targetChapter = (i * 10 + 1) % 300;
        QString searchKey = QStringLiteral("## 章节 %1:").arg(targetChapter + 1);
        int editOffset = liveBuffer.text().indexOf(searchKey);
        if (editOffset < 0) {
            editOffset = 100 + i * 200;
        } else {
            editOffset += searchKey.length() + 1;
        }

        // Apply a real single-character edit (alternating characters)
        QString newChar = QString(QChar('A' + (i % 26)));
        liveBuffer.replace(editOffset, 1, newChar);

        // Tier 2: Measure Parse
        timer.restart();
        auto curRoot = parser.parse(liveBuffer);
        qreal tParse = timer.nsecsElapsed() / 1000000.0;
        parseSamples.append(tParse);

        // Tier 2: Measure Projection build (with dirty block reconstruction)
        timer.restart();
        bool projChanged = liveProjection.buildProjection(curRoot, liveBuffer, editOffset, false, editOffset, 1, 1);
        qreal tProj = timer.nsecsElapsed() / 1000000.0;
        projSamples.append(tProj);
        QVERIFY(projChanged);

        // Tier 1: Measure Real Incremental Layout (invalidates target block, reuses ~2400 blocks)
        timer.restart();
        engine.layout(liveProjection, 800.0);
        qreal tLayout = timer.nsecsElapsed() / 1000000.0;
        incLayoutSamples.append(tLayout);

        // Tier 2: Measure Viewport Paint
        timer.restart();
        QPainter painter(&viewportImage);
        engine.paint(&painter, visibleViewport, liveProjection, -1, -1);
        painter.end();
        qreal tPaint = timer.nsecsElapsed() / 1000000.0;
        paintSamples.append(tPaint);

        // Tier 3: End-to-End Keypress Latency
        e2eSamples.append(tParse + tProj + tLayout + tPaint);
    }

    Stats parseStats = calcStats(parseSamples);
    Stats projStats = calcStats(projSamples);
    Stats incStats = calcStats(incLayoutSamples);
    Stats paintStats = calcStats(paintSamples);
    Stats e2eStats = calcStats(e2eSamples);

    // 6. Measure Cursor Navigation with change detection:
    baseProjection.buildProjection(rootNode, buffer, 100, false);
    timer.restart();
    bool changedNormal = baseProjection.buildProjection(rootNode, buffer, 105, false);
    qint64 cursorNavTimeNs = timer.nsecsElapsed();
    QVERIFY(!changedNormal);

    // 7. Measure hit test performance (binary search)
    timer.restart();
    int hitOff = engine.hitTest(QPointF(200, 1500), liveProjection);
    qint64 hitTestTimeUs = timer.nsecsElapsed() / 1000;
    QVERIFY(hitOff >= 0);

    qreal realSpeedup = coldStats.p50 / qMax(0.001, incStats.p50);

    qDebug() << "================== 137 KB Benchmark (3-Tier Performance Breakdown) ==================";
    qDebug() << "Document Size:" << byteSize / 1024 << "KB | Block Count:" << liveProjection.blockCount() << "| Edit Iterations:" << numEditIterations;
    qDebug() << "-------------------------------------------------------------------------------------";
    qDebug() << "[Tier 1: Pure Layout Engine Times]";
    qDebug() << "  Full Cold Layout (N=10)       : p50 =" << coldStats.p50 << "ms | p95 =" << coldStats.p95 << "ms | mean =" << coldStats.mean << "ms (stddev:" << coldStats.stddev << "ms)";
    qDebug() << "  Real Edit Inc Layout (N=30)   : p50 =" << incStats.p50 << "ms | p95 =" << incStats.p95 << "ms | mean =" << incStats.mean << "ms (stddev:" << incStats.stddev << "ms)";
    qDebug() << "  Stable Identity Layout (N=10) : p50 =" << stableStats.p50 << "ms | p95 =" << stableStats.p95 << "ms | mean =" << stableStats.mean << "ms (stddev:" << stableStats.stddev << "ms)";
    qDebug() << "  Layout Speedup (Cold/Inc p50) : ~" << realSpeedup << "x";
    qDebug() << "-------------------------------------------------------------------------------------";
    qDebug() << "[Tier 2: Pipeline Component Times (N=30 Real Edits)]";
    qDebug() << "  AST Parse                     : p50 =" << parseStats.p50 << "ms | p95 =" << parseStats.p95 << "ms | mean =" << parseStats.mean << "ms";
    qDebug() << "  Display Projection            : p50 =" << projStats.p50 << "ms | p95 =" << projStats.p95 << "ms | mean =" << projStats.mean << "ms";
    qDebug() << "  Incremental Layout            : p50 =" << incStats.p50 << "ms | p95 =" << incStats.p95 << "ms | mean =" << incStats.mean << "ms";
    qDebug() << "  Viewport Paint (1080p Culling): p50 =" << paintStats.p50 << "ms | p95 =" << paintStats.p95 << "ms | mean =" << paintStats.mean << "ms";
    qDebug() << "-------------------------------------------------------------------------------------";
    qDebug() << "[Tier 3: Simulated Document Edit Pipeline Latency (Parse + Proj + IncLayout + Paint)]";
    qDebug() << "  Simulated Pipeline Latency (N=30) : p50 =" << e2eStats.p50 << "ms | p95 =" << e2eStats.p95 << "ms | mean =" << e2eStats.mean << "ms (min:" << e2eStats.min << "ms, max:" << e2eStats.max << "ms)";
    qDebug() << "  (Note: Excludes OS/Qt window manager event loop & hardware display VSync; real GUI latency measured in Probe 25)";
    qDebug() << "  Cursor Nav Projection Check   :" << cursorNavTimeNs / 1000.0 << "us (Unchanged:" << !changedNormal << ")";
    qDebug() << "  HitTest (Binary Search)       :" << hitTestTimeUs << "us";
    qDebug() << "=====================================================================================";

    // Performance assertions:
    // 1. Initial parse p50 < 150ms
    QVERIFY(parseStats.p50 < 150.0);
    // 2. Incremental keystroke layout must be < 5ms (p50)
    QVERIFY(incStats.p50 < 5.0);
    // 3. Incremental layout must be demonstrably faster than full cold layout
    QVERIFY(coldStats.p50 > incStats.p50);
    // 4. Cursor navigation check within 10ms
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
    Mapping::DisplayProjection projection;
    Editor::LayoutEngine engine;

    // --- Scenario 1: Bold -> Italic with identical visible text on the SAME DisplayProjection instance ---
    Core::TextBuffer buf1;
    buf1.setText(QStringLiteral("前缀**重要**后缀\n"));

    auto root1 = parser.parse(buf1);
    bool proj1Changed = projection.buildProjection(root1, buf1, 0, false);
    QVERIFY(proj1Changed);
    QCOMPARE(projection.blockCount(), 1);

    // Assert that re-running projection on the identical state returns false (no change)
    bool proj1NoChange = projection.buildProjection(root1, buf1, 0, false);
    QVERIFY(!proj1NoChange);

    // Layout the initial document
    engine.layout(projection, 600.0);
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
    // Crucial check: on the SAME projection instance, checkChanged() MUST detect that format changed
    // even though visible text is still "前缀重要后缀"!
    bool projFormatChanged = projection.buildProjection(root2, buf2, 0, false);
    QVERIFY(projFormatChanged);
    QCOMPARE(projection.blocks()[0].visualText, QStringLiteral("前缀重要后缀")); // Visible text is identical!

    // Layout again with engine (incremental layout)
    engine.layout(projection, 600.0);
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

    // --- Scenario 2: Heading Level Change (# 大标题 -> ## 大标题) with identical visible text on single instance ---
    Core::TextBuffer bufH1;
    bufH1.setText(QStringLiteral("# 大标题\n"));
    auto rootH1 = parser.parse(bufH1);
    Mapping::DisplayProjection projHeading;
    bool h1Init = projHeading.buildProjection(rootH1, bufH1, 0, false);
    QVERIFY(h1Init);
    QVERIFY(!projHeading.buildProjection(rootH1, bufH1, 0, false)); // No change on identical

    engine.layout(projHeading, 600.0);

    qreal h1FontSize = 0;
    if (!engine.blocks().isEmpty() && !engine.blocks()[0].layout->formats().isEmpty()) {
        h1FontSize = engine.blocks()[0].layout->formats()[0].format.fontPointSize();
    }
    QCOMPARE(h1FontSize, 24.0);

    Core::TextBuffer bufH2;
    bufH2.setText(QStringLiteral("## 大标题\n"));
    auto rootH2 = parser.parse(bufH2);
    // On the SAME projHeading instance: MUST return true for heading level & format change!
    bool h2Changed = projHeading.buildProjection(rootH2, bufH2, 0, false);
    QVERIFY(h2Changed);

    engine.layout(projHeading, 600.0);
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

// Probe 21: Viewport Width Change Reflow and Boundary Tolerance Equivalence
void TestProbes::testWindowWidthChangeReflow() {
    Parser::MarkdownParserAdapter parser;
    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral(
        "# 视口宽度变化与重新断行测试\n\n"
        "这是一段较长且包含复杂词句的连续文本，用来严密验证当窗口宽度发生改变时，"
        "排版引擎能够精确判断可用行宽的变化并放弃旧的断行缓存，从而产生与冷排版完全一致的换行结果。\n\n"
        "- 列表项一：包含部分行内测试文本\n"
        "- 列表项二：另一段较长的文本，用来测试列表项在不同窗口宽度下的换行与边距表现\n\n"
        "> 引用段落：测试在宽度调整时，引用块与普通段落的排版高度与断行位置是否同步更新。"
    ));

    auto root = parser.parse(buffer);
    Mapping::DisplayProjection projection;
    projection.buildProjection(root, buffer, 0, false);

    Editor::LayoutEngine engine;

    // 1. Initial layout at 800.0px width
    engine.layout(projection, 800.0);
    qreal heightAt800 = engine.totalHeight();
    QVERIFY(heightAt800 > 0);

    // 2. Window resized to 700.0px (major width decrease -> text must wrap onto more lines)
    engine.layout(projection, 700.0);
    qreal heightAt700 = engine.totalHeight();
    QVERIFY(heightAt700 >= heightAt800);

    // Assert exact mathematical equivalence with a fresh cold layout at 700.0px
    Editor::LayoutEngine coldEngine700;
    coldEngine700.layout(projection, 700.0);
    QVERIFY(qAbs(engine.totalHeight() - coldEngine700.totalHeight()) < 0.001);
    QCOMPARE(engine.blocks().size(), coldEngine700.blocks().size());
    for (int i = 0; i < engine.blocks().size(); ++i) {
        QCOMPARE(engine.blocks()[i].layout->lineCount(), coldEngine700.blocks()[i].layout->lineCount());
        QVERIFY(qAbs(engine.blocks()[i].rect.height() - coldEngine700.blocks()[i].rect.height()) < 0.001);
    }

    // 3. Subtle sub-pixel / 0.5px boundary test (from 800.0 to 800.5)
    // Layout initial at 800.0
    engine.layout(projection, 800.0);
    // Resize slightly by 0.5px: must reflow and match cold layout at 800.5px
    engine.layout(projection, 800.5);
    Editor::LayoutEngine coldEngine800_5;
    coldEngine800_5.layout(projection, 800.5);

    QVERIFY(qAbs(engine.totalHeight() - coldEngine800_5.totalHeight()) < 0.001);
    QCOMPARE(engine.blocks().size(), coldEngine800_5.blocks().size());
    for (int i = 0; i < engine.blocks().size(); ++i) {
        QCOMPARE(engine.blocks()[i].layout->lineCount(), coldEngine800_5.blocks()[i].layout->lineCount());
        QVERIFY(qAbs(engine.blocks()[i].rect.height() - coldEngine800_5.blocks()[i].rect.height()) < 0.001);
    }
}

// Probe 22: Cache Hit Verification via Block Layout Pointer Identity
void TestProbes::testCacheHitPreservesBlockLayoutIdentity() {
    Parser::MarkdownParserAdapter parser;
    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral(
        "段落区块 0：这是第零段正文。\n\n"
        "段落区块 1：这是第一段正文。\n\n"
        "段落区块 2：这是第二段正文，将被单字符局部编辑。\n\n"
        "段落区块 3：这是第三段正文。\n\n"
        "段落区块 4：这是第四段正文。\n"
    ));

    int editPos = buffer.text().indexOf(QStringLiteral("第二段"));
    QVERIFY(editPos > 0);

    auto root = parser.parse(buffer);
    Mapping::DisplayProjection projection;
    projection.buildProjection(root, buffer, editPos, false);

    Editor::LayoutEngine engine;
    engine.layout(projection, 750.0);
    QCOMPARE(engine.blocks().size(), 5);

    // Record raw QTextLayout pointer addresses
    QVector<QTextLayout*> originalPointers;
    originalPointers.reserve(5);
    for (int i = 0; i < 5; ++i) {
        originalPointers.append(engine.blocks()[i].layout.get());
        QVERIFY(originalPointers.last() != nullptr);
    }

    // Now edit single character in block 2 only (replacing "第二段" with "修改段")
    buffer.replace(editPos, 3, QStringLiteral("修改段"));

    auto rootEdited = parser.parse(buffer);
    Mapping::DisplayProjection editedProjection;
    editedProjection.buildProjection(rootEdited, buffer, editPos + 3, false);

    // Perform incremental layout
    engine.layout(editedProjection, 750.0);
    QCOMPARE(engine.blocks().size(), 5);

    // Assert:
    // Prefix blocks 0 and 1 MUST preserve their original QTextLayout pointers (100% cache hit, zero text shaping)
    QCOMPARE(engine.blocks()[0].layout.get(), originalPointers[0]);
    QCOMPARE(engine.blocks()[1].layout.get(), originalPointers[1]);

    // Edited block 2 MUST have a new QTextLayout pointer (cache miss, newly shaped)
    QVERIFY(engine.blocks()[2].layout.get() != originalPointers[2]);

    // Suffix blocks 3 and 4 MUST preserve their original QTextLayout pointers (100% cache hit, zero text shaping)
    QCOMPARE(engine.blocks()[3].layout.get(), originalPointers[3]);
    QCOMPARE(engine.blocks()[4].layout.get(), originalPointers[4]);
}

// Probe 23: Document Scale Performance Matrix (100 KB, 1 MB, 3 MB)
void TestProbes::testDocumentScalePerformanceMatrix() {
    Parser::MarkdownParserAdapter parser;

    auto benchmarkScale = [&](int numChapters, const QString &scaleLabel) {
        QString doc;
        doc.reserve(numChapters * 450);
        for (int i = 0; i < numChapters; ++i) {
            doc.append(QStringLiteral("## 章节 %1: 性能规模扩展评估\n\n").arg(i + 1));
            doc.append(QStringLiteral("测试原生排版引擎与增量解析流水线在大规模文档下的伸缩能力与内存表现。\n\n"));
            doc.append(QStringLiteral("- 列表项一：包含部分行内样式测试\n- 列表项二：另一段较长的文本，用来测试列表项\n\n"));
            doc.append(QStringLiteral("```cpp\nint fn_%1() { return %1; }\n```\n\n").arg(i + 1));
        }

        int byteSize = doc.toUtf8().size();
        Core::TextBuffer buffer;
        buffer.setText(doc);

        QElapsedTimer timer;

        // 1. Cold Parse
        timer.restart();
        auto root = parser.parse(buffer);
        qreal coldParseMs = timer.nsecsElapsed() / 1000000.0;
        QVERIFY(root != nullptr);

        // 2. Cold Projection
        Mapping::DisplayProjection proj;
        timer.restart();
        proj.buildProjection(root, buffer, 0, false);
        qreal coldProjMs = timer.nsecsElapsed() / 1000000.0;
        int blockCount = proj.blockCount();

        // 3. Cold Layout
        Editor::LayoutEngine engine;
        timer.restart();
        engine.layout(proj, 800.0);
        qreal coldLayoutMs = timer.nsecsElapsed() / 1000000.0;

        // 4. Incremental Edit: Modify one character in a middle paragraph
        int editOffset = doc.indexOf(QStringLiteral("伸缩能力"));
        if (editOffset < 0) editOffset = 100;
        buffer.replace(editOffset, 2, QStringLiteral("弹性"));

        // Incremental AST Parse (if supported) vs Cold
        int childIdx = -1;
        qreal incParseMs = 0;
        Parser::SemanticNodePtr editedRoot;
        if (parser.canIncrementalParse(root, buffer, editOffset, 2, QStringLiteral("弹性"), childIdx)) {
            timer.restart();
            editedRoot = parser.incrementalParse(root, buffer, editOffset, 2, 2, childIdx);
            incParseMs = timer.nsecsElapsed() / 1000000.0;
        } else {
            timer.restart();
            editedRoot = parser.parse(buffer);
            incParseMs = timer.nsecsElapsed() / 1000000.0;
        }

        // Incremental Projection with Dirty Block Reconstruction
        timer.restart();
        proj.buildProjection(editedRoot, buffer, editOffset, false, editOffset, 2, 2);
        qreal incProjMs = timer.nsecsElapsed() / 1000000.0;

        // Incremental Layout
        timer.restart();
        engine.layout(proj, 800.0);
        qreal incLayoutMs = timer.nsecsElapsed() / 1000000.0;

        // Viewport Paint (1080p culling)
        QImage vpImg(1920, 1080, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&vpImg);
        timer.restart();
        engine.paint(&painter, QRectF(0, 500.0, 800.0, 1080.0), proj, -1, -1);
        painter.end();
        qreal vpPaintMs = timer.nsecsElapsed() / 1000000.0;

        qDebug().noquote() << QStringLiteral("| %1 | %2 KB | %3 | %4 ms | %5 ms | %6 ms | %7 ms | %8 ms | %9 ms | %10 ms |")
            .arg(scaleLabel, -7)
            .arg(byteSize / 1024, 7)
            .arg(blockCount, 7)
            .arg(coldParseMs, 7, 'f', 1)
            .arg(incParseMs, 7, 'f', 2)
            .arg(coldProjMs, 7, 'f', 1)
            .arg(incProjMs, 7, 'f', 2)
            .arg(coldLayoutMs, 7, 'f', 1)
            .arg(incLayoutMs, 7, 'f', 2)
            .arg(vpPaintMs, 7, 'f', 2);
    };

    qDebug() << "\n=========================================== Document Scale Performance Matrix ===========================================";
    qDebug() << "| Scale   | Doc Size| Blocks  | ColdParse | IncParse  | ColdProj  | IncProj   | ColdLayout| IncLayout | ViewportPaint|";
    qDebug() << "|---------|---------|---------|-----------|-----------|-----------|-----------|-----------|-----------|--------------|";
    benchmarkScale(250, QStringLiteral("100 KB"));   // ~115 KB, ~1500 blocks
    benchmarkScale(2200, QStringLiteral("1.0 MB"));  // ~1.0 MB, ~13,200 blocks
    benchmarkScale(5000, QStringLiteral("2.5 MB"));  // ~2.5 MB, ~30,000 blocks
    qDebug() << "=========================================================================================================================\n";
}

// Probe 24: Mathematical Equivalence between Incremental AST Parse / Dirty Block Projection and Cold Full Parse / Projection
void TestProbes::testIncrementalAstParseAndProjectionEquivalence() {
    Parser::MarkdownParserAdapter parser;

    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral(
        "# 顶层标题\n\n"
        "这是第一段正文，包含普通文字与测试信息。\n\n"
        "这是第二段正文，包含了**重要粗体**与*关键斜体*的内容。\n\n"
        "这是第三段正文，保持不变以验证前缀与后缀的精确复用。\n\n"
        "- [x] 任务项一已完成\n"
        "- [ ] 任务项二进行中\n"
    ));

    // 1. Initial base parse and projection
    auto baseRoot = parser.parse(buffer);
    QVERIFY(baseRoot != nullptr);
    QCOMPARE(baseRoot->children.size(), 5);

    Mapping::DisplayProjection baseProj;
    baseProj.buildProjection(baseRoot, buffer, 0, false);
    QCOMPARE(baseProj.blockCount(), 6);

    // 2. Perform a local in-paragraph keystroke edit in paragraph 2:
    // Replace "重要粗体" with "升级强化粗体"
    int editPos = buffer.text().indexOf(QStringLiteral("重要粗体"));
    QVERIFY(editPos > 0);
    int removeLen = 4;
    QString insertStr = QStringLiteral("升级强化粗体");
    int insertLen = insertStr.length();
    buffer.replace(editPos, removeLen, insertStr);

    // 3. Verify canIncrementalParse recognizes the minimal affected semantic region
    int childIdx = -1;
    bool canInc = parser.canIncrementalParse(baseRoot, buffer, editPos, removeLen, insertStr, childIdx);
    QVERIFY(canInc);
    QCOMPARE(childIdx, 2); // Paragraph 2 is index 2

    // 4. Run incremental AST parse
    auto incRoot = parser.incrementalParse(baseRoot, buffer, editPos, removeLen, insertLen, childIdx);
    QVERIFY(incRoot != nullptr);

    // 5. Run cold full AST parse on the exact same buffer
    auto coldRoot = parser.parse(buffer);
    QVERIFY(coldRoot != nullptr);

    // 6. Assert mathematical equivalence between incRoot and coldRoot
    QCOMPARE(incRoot->children.size(), coldRoot->children.size());
    for (int i = 0; i < incRoot->children.size(); ++i) {
        const auto &incChild = incRoot->children[i];
        const auto &coldChild = coldRoot->children[i];
        QCOMPARE(incChild->type, coldChild->type);
        QCOMPARE(incChild->fullRange.start, coldChild->fullRange.start);
        QCOMPARE(incChild->fullRange.end, coldChild->fullRange.end);
        QCOMPARE(incChild->contentRange.start, coldChild->contentRange.start);
        QCOMPARE(incChild->contentRange.end, coldChild->contentRange.end);
        QCOMPARE(incChild->headingLevel, coldChild->headingLevel);
        QCOMPARE(incChild->children.size(), coldChild->children.size());
    }

    // 7. Test Incremental Projection with Dirty Block Reconstruction
    Mapping::DisplayProjection incProj = baseProj;
    bool incChanged = incProj.buildProjection(incRoot, buffer, editPos + insertLen, false, editPos, removeLen, insertLen);
    QVERIFY(incChanged);

    // 8. Test Cold Projection on fresh instance
    Mapping::DisplayProjection coldProj;
    bool coldChanged = coldProj.buildProjection(coldRoot, buffer, editPos + insertLen, false);
    QVERIFY(coldChanged);

    // 9. Assert mathematical equivalence between incProj and coldProj blocks
    QCOMPARE(incProj.blockCount(), coldProj.blockCount());
    for (int i = 0; i < incProj.blockCount(); ++i) {
        const auto &bInc = incProj.blocks()[i];
        const auto &bCold = coldProj.blocks()[i];
        QCOMPARE(bInc.visualText, bCold.visualText);
        QCOMPARE(bInc.sourceRange.start, bCold.sourceRange.start);
        QCOMPARE(bInc.sourceRange.end, bCold.sourceRange.end);
        QCOMPARE(bInc.blockType, bCold.blockType);
        QCOMPARE(bInc.headingLevel, bCold.headingLevel);
        QCOMPARE(bInc.segments.size(), bCold.segments.size());
        for (int s = 0; s < bInc.segments.size(); ++s) {
            const auto &sInc = bInc.segments[s];
            const auto &sCold = bCold.segments[s];
            QCOMPARE(sInc.visualStart, sCold.visualStart);
            QCOMPARE(sInc.visualLength, sCold.visualLength);
            QCOMPARE(sInc.sourceRange.start, sCold.sourceRange.start);
            QCOMPARE(sInc.sourceRange.end, sCold.sourceRange.end);
            QCOMPARE(sInc.format, sCold.format);
        }
    }
}

// Probe 25: Qt GUI Event-Driven Input and Repaint Latency
void TestProbes::testGuiEventDrivenInputLatency() {
    Editor::NativeEditorView view;
    view.resize(1024, 768);
    view.buffer().setText(QStringLiteral("# 原生 Markdown 窗口输入测试\n\n测试由 Qt 事件系统驱动的实际按键输入与绘制延迟。\n"));
    view.setCursorPos(35); // in the body

    // 1. Dispatch real Qt KeyPress event through Qt event system
    QElapsedTimer eventTimer;
    eventTimer.start();

    QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("A"));
    bool handled = QApplication::sendEvent(view.viewport(), &keyEvent);
    qint64 eventDispatchUs = eventTimer.nsecsElapsed() / 1000;

    QVERIFY(handled);
    QVERIFY(view.buffer().text().contains(QStringLiteral("A")));

    // 2. Trigger real Qt Viewport render
    eventTimer.restart();
    QPixmap pixmap(1024, 768);
    view.render(&pixmap);
    qint64 paintUs = eventTimer.nsecsElapsed() / 1000;

    qDebug() << "=== Probe 25: Qt GUI Event-Driven Latency ===";
    qDebug() << "  Qt KeyPress Event Dispatch (Input -> Transaction -> Buffer -> Layout):" << eventDispatchUs / 1000.0 << "ms";
    qDebug() << "  Qt Viewport Paint Event Execution                                    :" << paintUs / 1000.0 << "ms";
    qDebug() << "  Total Interactive GUI Turnaround                                    :" << (eventDispatchUs + paintUs) / 1000.0 << "ms";

    // Assert responsiveness limits
    QVERIFY(eventDispatchUs < 30 * 1000); // Under 30ms
    QVERIFY(paintUs < 20 * 1000);         // Under 20ms
}

QTEST_MAIN(TestProbes)
#include "TestProbes.moc"
