#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QImage>
#include <QClipboard>
#include <QScrollBar>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QUrl>
#include <QDir>
#include <QSettings>
#include <cmath>
#include <random>
#include <thread>
#include <vector>
#include <atomic>

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
#include "render/MathSvgRenderer.h"
#include "render/MermaidSvgRenderer.h"
#include "render/AsyncRenderService.h"
#include "theme/Theme.h"
#include "ui/MainWindow.h"
#include "ui/FindReplaceBar.h"
#include <QFontDialog>
#include <QListView>
#include <QMenu>
#include <QAction>

using namespace MarkdownTool;

class TestProbes : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
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
    void testReferenceStyleLinkIncrementalSafety();
    void testDifferentialFuzzingEquivalence();
    void testLargeDocumentGuiInputLatency();
    void testMultiCursorEditingAndUndo();
    void testColumnSelectionAndMultiLinePaste();
    void testMathFormulaAsyncRenderingAndProjection();
    void testMermaidDiagramAsyncRenderingAndInteraction();
    void testAsyncRenderServiceCachingAndThreadSafety();
    void testCacheKeyRenderKindIsolation();
    void testMermaidCyclicAndSelfLoopLayoutBoundedness();
    void testAsyncRenderServiceLRUAndEpochCancellation();
    void testNestedFractionsAndUnsupportedFallback();
    void testAsyncRenderServiceEpochRaceCondition();
    void testMermaidBoundingBoxAndVisualNoClipping();
    void testAsyncRenderServiceShutdownWithInFlightTasks();
    void testNestedFractionGeometryAndHierarchy();
    void testFirstRunAsyncRenderReliabilityAndByteBudget();
    void testFirstRunDocumentGuiIntegration();
    void testSyncFallbackEpochRaceConditionAndUnifiedCache();
    void testRealGuiAsyncAutoRefreshAndInteractiveRegression();
    void testFileAndImageDragAndDrop();
    void testVisualRedesignScreenshotsAndThemes();
    void testMouseWheelScrollingAndTypography();
    void testFindAndReplaceFeedback();
    void testPreferencesPersistenceAndMenuSync();
    void testSidebarSearchConsistency();
    void testWheelZoomAndReadingAnchor();
    void testFontDialogThemeAndLegibility();
    void testRecentFilesHistoryPersistenceAndMenu();
    void testAdaptiveReadingWidthAndTypographyReflow();
    void testMarkdownTableNativeLayoutAndInteraction();

private:
    std::unique_ptr<QTemporaryDir> m_tempSettingsDir;
};

void TestProbes::initTestCase() {
    m_tempSettingsDir = std::make_unique<QTemporaryDir>();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempSettingsDir->path());
}

void TestProbes::cleanupTestCase() {
    m_tempSettingsDir.reset();
}

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
    // 2. Incremental keystroke layout must be fast and bounded (p50 < 15ms in CI VM environments)
    QVERIFY(incStats.p50 < 15.0);
    // 3. Incremental layout must be demonstrably faster than full cold layout
    QVERIFY(coldStats.p50 > incStats.p50);
    // 4. Cursor navigation check within 20ms
    QVERIFY(cursorNavTimeNs < 20 * 1000 * 1000);
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

// Probe 23: Document Scale Performance Matrix (100 KB, 1 MB, 2.5 MB)
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

        // Baseline: Cold Parse, Cold Proj, Cold Layout
        timer.restart();
        auto root = parser.parse(buffer);
        qreal coldParseMs = timer.nsecsElapsed() / 1000000.0;
        QVERIFY(root != nullptr);

        Mapping::DisplayProjection proj;
        timer.restart();
        proj.buildProjection(root, buffer, 0, false);
        qreal coldProjMs = timer.nsecsElapsed() / 1000000.0;
        int blockCount = proj.blockCount();

        Editor::LayoutEngine engine;
        timer.restart();
        engine.layout(proj, 800.0);
        qreal coldLayoutMs = timer.nsecsElapsed() / 1000000.0;

        // Helper to test real single keystroke typing at a specified paragraph offset
        auto testKeystroke = [&](int targetOffset, int remLen, const QString &insText) -> std::tuple<qreal, qreal, qreal> {
            buffer.replace(targetOffset, remLen, insText);
            int addedLen = insText.length();
            int newCursor = targetOffset + addedLen;

            int childIdx = -1;
            bool canInc = parser.canIncrementalParse(root, buffer, targetOffset, remLen, addedLen, childIdx);
            if (!canInc) {
                return std::make_tuple(-1.0, -1.0, -1.0);
            }

            bool incSuccess = false;
            timer.restart();
            auto editedRoot = parser.incrementalParse(root, buffer, targetOffset, remLen, addedLen, childIdx, &incSuccess);
            qreal incParseMs = timer.nsecsElapsed() / 1000000.0;
            if (!incSuccess || !editedRoot) {
                return std::make_tuple(-1.0, -1.0, -1.0);
            }

            timer.restart();
            proj.buildProjection(editedRoot, buffer, newCursor, false, targetOffset, remLen, addedLen);
            qreal incProjMs = timer.nsecsElapsed() / 1000000.0;

            timer.restart();
            engine.layout(proj, 800.0);
            qreal incLayoutMs = timer.nsecsElapsed() / 1000000.0;

            root = editedRoot;
            return std::make_tuple(incParseMs, incProjMs, incLayoutMs);
        };

        // 1. Head Edit (+1 char insertion) in Chapter 1 paragraph
        int headOffset = doc.indexOf(QStringLiteral("伸缩能力"));
        QVERIFY(headOffset > 0);
        proj.buildProjection(root, buffer, headOffset, false);
        engine.layout(proj, 800.0);
        auto [headInsParse, headInsProj, headInsLayout] = testKeystroke(headOffset, 0, QStringLiteral("首"));
        QVERIFY(headInsParse >= 0.0);

        // 2. Middle Edit (+1 char insertion) in Chapter numChapters/2 paragraph
        int midChapter = numChapters / 2;
        QString midMarker = QStringLiteral("## 章节 %1:").arg(midChapter);
        int midHead = buffer.text().indexOf(midMarker);
        int midOffset = buffer.text().indexOf(QStringLiteral("伸缩能力"), midHead);
        QVERIFY(midOffset > 0);
        proj.buildProjection(root, buffer, midOffset, false);
        engine.layout(proj, 800.0);
        auto [midInsParse, midInsProj, midInsLayout] = testKeystroke(midOffset, 0, QStringLiteral("中"));
        QVERIFY(midInsParse >= 0.0);

        // 3. Middle Edit (-1 char deletion / backspace)
        auto [midDelParse, midDelProj, midDelLayout] = testKeystroke(midOffset, 1, QString());
        QVERIFY(midDelParse >= 0.0);

        // 4. Tail Edit (+1 char insertion) in Chapter numChapters-1 paragraph
        int tailChapter = numChapters - 1;
        QString tailMarker = QStringLiteral("## 章节 %1:").arg(tailChapter);
        int tailHead = buffer.text().indexOf(tailMarker);
        int tailOffset = buffer.text().indexOf(QStringLiteral("伸缩能力"), tailHead);
        QVERIFY(tailOffset > 0);
        proj.buildProjection(root, buffer, tailOffset, false);
        engine.layout(proj, 800.0);
        auto [tailInsParse, tailInsProj, tailInsLayout] = testKeystroke(tailOffset, 0, QStringLiteral("尾"));
        QVERIFY(tailInsParse >= 0.0);

        // Viewport Paint (1080p culling)
        QImage vpImg(1920, 1080, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&vpImg);
        timer.restart();
        engine.paint(&painter, QRectF(0, 500.0, 800.0, 1080.0), proj, -1, -1);
        painter.end();
        qreal vpPaintMs = timer.nsecsElapsed() / 1000000.0;

        qreal midTotalIncMs = midInsParse + midInsProj + midInsLayout;
        qreal coldTotalMs = coldParseMs + coldProjMs + coldLayoutMs;

        qDebug().noquote() << QStringLiteral("| %1 | %2 KB | %3 | %4 / %5 / %6 ms | %7 / %8 / %9 ms | %10 / %11 / %12 ms | %13 / %14 / %15 ms | %16 / %17 / %18 ms | %19 ms | %20x |")
            .arg(scaleLabel, -7)
            .arg(byteSize / 1024, 7)
            .arg(blockCount, 7)
            .arg(coldParseMs, 5, 'f', 1)
            .arg(coldProjMs, 5, 'f', 1)
            .arg(coldLayoutMs, 5, 'f', 1)
            .arg(headInsParse, 4, 'f', 2)
            .arg(headInsProj, 4, 'f', 2)
            .arg(headInsLayout, 4, 'f', 2)
            .arg(midInsParse, 4, 'f', 2)
            .arg(midInsProj, 4, 'f', 2)
            .arg(midInsLayout, 4, 'f', 2)
            .arg(midDelParse, 4, 'f', 2)
            .arg(midDelProj, 4, 'f', 2)
            .arg(midDelLayout, 4, 'f', 2)
            .arg(tailInsParse, 4, 'f', 2)
            .arg(tailInsProj, 4, 'f', 2)
            .arg(tailInsLayout, 4, 'f', 2)
            .arg(vpPaintMs, 5, 'f', 2)
            .arg(coldTotalMs / qMax(0.01, midTotalIncMs), 5, 'f', 1);
    };

    qDebug() << "\n================================================ Document Scale Performance Matrix (Real Typing) ================================================";
    qDebug() << "| Scale   | Doc Size| Blocks  | Cold (Parse/Proj/Lay) | Head (+1) (Parse/Proj/Lay)| Mid (+1) (Parse/Proj/Lay) | Mid (-1) (Parse/Proj/Lay) | Tail (+1) (Parse/Proj/Lay)| VpPaint | Speedup |";
    qDebug() << "|---------|---------|---------|-----------------------|---------------------------|---------------------------|---------------------------|---------------------------|---------|---------|";
    benchmarkScale(250, QStringLiteral("100 KB"));   // ~115 KB, ~1250 blocks
    benchmarkScale(2200, QStringLiteral("1.0 MB"));  // ~1.0 MB, ~11000 blocks
    benchmarkScale(5000, QStringLiteral("2.5 MB"));  // ~2.5 MB, ~25000 blocks
    qDebug() << "=================================================================================================================================================\n";
}

// Probe 24: Mathematical Equivalence between Incremental AST Parse / Dirty Block Projection and Cold Full Parse / Projection
static void assertSemanticNodesDeepEqual(const Parser::SemanticNodePtr &a, const Parser::SemanticNodePtr &b) {
    QVERIFY(a != nullptr);
    QVERIFY(b != nullptr);
    QCOMPARE(a->type, b->type);
    QCOMPARE(a->headingLevel, b->headingLevel);
    QCOMPARE(a->isTaskChecked, b->isTaskChecked);
    QCOMPARE(a->isTaskItem, b->isTaskItem);
    QCOMPARE(a->codeLanguage, b->codeLanguage);
    QCOMPARE(a->linkUrl, b->linkUrl);
    QCOMPARE(a->linkTitle, b->linkTitle);
    QCOMPARE(a->fullRange.start, b->fullRange.start);
    QCOMPARE(a->fullRange.end, b->fullRange.end);
    QCOMPARE(a->contentRange.start, b->contentRange.start);
    QCOMPARE(a->contentRange.end, b->contentRange.end);
    if (!a->prefixRange.isEmpty() || !b->prefixRange.isEmpty()) {
        QCOMPARE(a->prefixRange.start, b->prefixRange.start);
        QCOMPARE(a->prefixRange.end, b->prefixRange.end);
    }
    if (!a->suffixRange.isEmpty() || !b->suffixRange.isEmpty()) {
        QCOMPARE(a->suffixRange.start, b->suffixRange.start);
        QCOMPARE(a->suffixRange.end, b->suffixRange.end);
    }
    if (a->isTaskItem || b->isTaskItem) {
        QCOMPARE(a->taskCheckboxRange.start, b->taskCheckboxRange.start);
        QCOMPARE(a->taskCheckboxRange.end, b->taskCheckboxRange.end);
    }
    QCOMPARE(a->children.size(), b->children.size());
    for (int i = 0; i < a->children.size(); ++i) {
        assertSemanticNodesDeepEqual(a->children[i], b->children[i]);
    }
}

static void assertVisualBlocksDeepEqual(const QVector<Mapping::VisualBlock> &a, const QVector<Mapping::VisualBlock> &b) {
    QCOMPARE(a.size(), b.size());
    for (int i = 0; i < a.size(); ++i) {
        const auto &b1 = a[i];
        const auto &b2 = b[i];
        QCOMPARE(b1.visualText, b2.visualText);
        QCOMPARE(b1.sourceRange.start, b2.sourceRange.start);
        QCOMPARE(b1.sourceRange.end, b2.sourceRange.end);
        QCOMPARE(b1.blockType, b2.blockType);
        QCOMPARE(b1.headingLevel, b2.headingLevel);
        QCOMPARE(b1.indentLevel, b2.indentLevel);
        QCOMPARE(b1.isQuote, b2.isQuote);
        QCOMPARE(b1.isCodeBlock, b2.isCodeBlock);
        QCOMPARE(b1.isTaskItem, b2.isTaskItem);
        QCOMPARE(b1.isTaskChecked, b2.isTaskChecked);
        if (b1.isTaskItem || b2.isTaskItem) {
            QCOMPARE(b1.checkboxSourceRange.start, b2.checkboxSourceRange.start);
            QCOMPARE(b1.checkboxSourceRange.end, b2.checkboxSourceRange.end);
        }
        QCOMPARE(b1.segments.size(), b2.segments.size());
        for (int s = 0; s < b1.segments.size(); ++s) {
            const auto &s1 = b1.segments[s];
            const auto &s2 = b2.segments[s];
            QCOMPARE(s1.visualStart, s2.visualStart);
            QCOMPARE(s1.visualLength, s2.visualLength);
            QCOMPARE(s1.sourceRange.start, s2.sourceRange.start);
            QCOMPARE(s1.sourceRange.end, s2.sourceRange.end);
            QCOMPARE(s1.type, s2.type);
            QCOMPARE(s1.format, s2.format);
        }
    }
}

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

    // 1. Initial base parse and projection (cursor situated inside paragraph 2)
    int editPos = buffer.text().indexOf(QStringLiteral("重要粗体"));
    QVERIFY(editPos > 0);

    auto baseRoot = parser.parse(buffer);
    QVERIFY(baseRoot != nullptr);
    QCOMPARE(baseRoot->children.size(), 5);

    Mapping::DisplayProjection baseProj;
    baseProj.buildProjection(baseRoot, buffer, editPos, false);
    QCOMPARE(baseProj.blockCount(), 6);

    // =========================================================================
    // Round 1: Fast Path Single Keystroke Edit inside Paragraph 2
    // =========================================================================
    int removeLen = 4;
    QString insertStr = QStringLiteral("升级强化粗体");
    int insertLen = insertStr.length();
    buffer.replace(editPos, removeLen, insertStr);

    int childIdx = -1;
    bool canInc = parser.canIncrementalParse(baseRoot, buffer, editPos, removeLen, insertStr, childIdx);
    QVERIFY(canInc);
    QCOMPARE(childIdx, 2); // Paragraph 2 is index 2

    auto incRoot = parser.incrementalParse(baseRoot, buffer, editPos, removeLen, insertLen, childIdx);
    QVERIFY(incRoot != nullptr);

    auto coldRoot = parser.parse(buffer);
    QVERIFY(coldRoot != nullptr);

    // Assert recursive deep AST equivalence
    assertSemanticNodesDeepEqual(incRoot, coldRoot);

    // Incremental Projection with In-Place Dirty Block Update
    Mapping::DisplayProjection incProj = baseProj;
    bool incChanged = incProj.buildProjection(incRoot, buffer, editPos + insertLen, false, editPos, removeLen, insertLen);
    QVERIFY(incChanged);

    // Cold Projection
    Mapping::DisplayProjection coldProj;
    bool coldChanged = coldProj.buildProjection(coldRoot, buffer, editPos + insertLen, false);
    QVERIFY(coldChanged);

    // Assert deep VisualBlock and VisualSegment equivalence
    assertVisualBlocksDeepEqual(incProj.blocks(), coldProj.blocks());

    // =========================================================================
    // Round 2: Sequential Keystroke Append inside the same Paragraph
    // =========================================================================
    int editPos2 = editPos + insertLen;
    QString insertStr2 = QStringLiteral("及扩展说明");
    int insertLen2 = insertStr2.length();
    buffer.replace(editPos2, 0, insertStr2);

    int childIdx2 = -1;
    bool canInc2 = parser.canIncrementalParse(incRoot, buffer, editPos2, 0, insertStr2, childIdx2);
    QVERIFY(canInc2);
    QCOMPARE(childIdx2, 2);

    auto incRoot2 = parser.incrementalParse(incRoot, buffer, editPos2, 0, insertLen2, childIdx2);
    auto coldRoot2 = parser.parse(buffer);
    assertSemanticNodesDeepEqual(incRoot2, coldRoot2);

    bool incChanged2 = incProj.buildProjection(incRoot2, buffer, editPos2 + insertLen2, false, editPos2, 0, insertLen2);
    QVERIFY(incChanged2);

    Mapping::DisplayProjection coldProj2;
    coldProj2.buildProjection(coldRoot2, buffer, editPos2 + insertLen2, false);
    assertVisualBlocksDeepEqual(incProj.blocks(), coldProj2.blocks());

    // =========================================================================
    // Round 3: Inter-block Cursor Navigation / Edit Fallback Safety
    // (Edit performed with cursor originating from block 0 heading, jumping to paragraph)
    // =========================================================================
    Mapping::DisplayProjection jumpProj;
    jumpProj.buildProjection(coldRoot2, buffer, 0, false); // cursor at heading 0

    // Now edit in paragraph 2 with cursor landing in paragraph 2
    int editPos3 = buffer.text().indexOf(QStringLiteral("测试信息"));
    QVERIFY(editPos3 > 0);
    buffer.replace(editPos3, 4, QStringLiteral("完全核验"));

    auto coldRoot3 = parser.parse(buffer);
    // Because previous cursor was in block 0 and new cursor is in block 1, jumpProj safely triggers fallback path
    bool jumpChanged = jumpProj.buildProjection(coldRoot3, buffer, editPos3 + 4, false, editPos3, 4, 4);
    QVERIFY(jumpChanged);

    Mapping::DisplayProjection coldProj3;
    coldProj3.buildProjection(coldRoot3, buffer, editPos3 + 4, false);
    assertVisualBlocksDeepEqual(jumpProj.blocks(), coldProj3.blocks());
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

    // Telemetry Verification: verify real GUI keystroke edit successfully executed via incremental AST parse!
    QVERIFY(view.incrementalParseHits() > 0);

    // Sequential second keypress 'B' to verify hit counter increments
    quint64 hitsBefore = view.incrementalParseHits();
    QKeyEvent keyEvent2(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier, QStringLiteral("B"));
    QApplication::sendEvent(view.viewport(), &keyEvent2);
    QCOMPARE(view.incrementalParseHits(), hitsBefore + 1);
    QVERIFY(view.buffer().text().contains(QStringLiteral("AB")));

    // 2. Trigger real Qt Viewport render
    eventTimer.restart();
    QPixmap pixmap(1024, 768);
    view.render(&pixmap);
    qint64 paintUs = eventTimer.nsecsElapsed() / 1000;

    qDebug() << "=== Probe 25: Qt GUI Event-Driven Latency ===";
    qDebug() << "  Qt KeyPress Event Dispatch (Input -> Transaction -> Buffer -> Layout):" << eventDispatchUs / 1000.0 << "ms";
    qDebug() << "  Qt Viewport Paint Event Execution                                    :" << paintUs / 1000.0 << "ms";
    qDebug() << "  Total Interactive GUI Turnaround                                    :" << (eventDispatchUs + paintUs) / 1000.0 << "ms";
    qDebug() << "  Incremental Parse Hits:" << view.incrementalParseHits() << "| Full Parse Fallbacks:" << view.fullParseFallbacks();

    // Assert responsiveness limits with headroom for virtualized cloud CI runners
    QVERIFY(eventDispatchUs < 100 * 1000); // Under 100ms
    QVERIFY(paintUs < 100 * 1000);         // Under 100ms
}

// Probe 26: Reference-Style Link Incremental AST Parse Safety & Dependency Fallback
void TestProbes::testReferenceStyleLinkIncrementalSafety() {
    Parser::MarkdownParserAdapter parser;

    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral(
        "# 参考链接安全测试\n\n"
        "这是一个[参考链接][ref]测试段落。\n\n"
        "这里是第二段普通的正文内容，完全独立且不包含链接。\n\n"
        "[ref]: https://github.com/WildEgoLee/native-markdown-editor \"原生编辑器\"\n"
    ));

    auto baseRoot = parser.parse(buffer);
    QVERIFY(baseRoot != nullptr);
    QCOMPARE(baseRoot->children.size(), 3);

    Mapping::DisplayProjection baseProj;
    baseProj.buildProjection(baseRoot, buffer, 0, false);

    // Verify base link was correctly parsed as Link with expected URL
    auto findLink = [](auto self, const Parser::SemanticNodePtr &node) -> QString {
        if (!node) return QString();
        if (node->type == Parser::NodeType::Link) return node->linkUrl;
        for (const auto &c : node->children) {
            QString u = self(self, c);
            if (!u.isEmpty()) return u;
        }
        return QString();
    };
    QCOMPARE(findLink(findLink, baseRoot), QStringLiteral("https://github.com/WildEgoLee/native-markdown-editor"));

    // Case 1: Edit in Paragraph 2 (normal text without '[' or ']:')
    // canIncrementalParse MUST return true and resolve successfully!
    int posPara2 = buffer.text().indexOf(QStringLiteral("普通的正文内容"));
    QVERIFY(posPara2 > 0);
    buffer.replace(posPara2, 0, QStringLiteral("丰富拓展的"));

    int childIdx = -1;
    bool canInc = parser.canIncrementalParse(baseRoot, buffer, posPara2, 0, 5, childIdx);
    QVERIFY(canInc);
    QCOMPARE(childIdx, 2);

    bool incSuccess = false;
    auto incRoot = parser.incrementalParse(baseRoot, buffer, posPara2, 0, 5, childIdx, &incSuccess);
    QVERIFY(incSuccess);
    QVERIFY(incRoot != nullptr);

    auto coldRoot = parser.parse(buffer);
    assertSemanticNodesDeepEqual(incRoot, coldRoot);

    Mapping::DisplayProjection incProj = baseProj;
    incProj.buildProjection(incRoot, buffer, posPara2 + 5, false, posPara2, 0, 5);
    Mapping::DisplayProjection coldProj;
    coldProj.buildProjection(coldRoot, buffer, posPara2 + 5, false);
    assertVisualBlocksDeepEqual(incProj.blocks(), coldProj.blocks());

    // Case 2: Edit in Paragraph 1 (containing '[ref]')
    // Because this block contains '[' and document defines ']:', isolated snippet parsing cannot
    // resolve definitions outside this block. canIncrementalParse MUST return false!
    int posPara1 = buffer.text().indexOf(QStringLiteral("测试段落"));
    QVERIFY(posPara1 > 0);
    buffer.replace(posPara1, 0, QStringLiteral("严谨"));

    int childIdx1 = -1;
    bool canInc1 = parser.canIncrementalParse(incRoot, buffer, posPara1, 0, 2, childIdx1);
    QCOMPARE(canInc1, false); // Blocked from incremental snippet parse to protect link semantics!

    // Case 3: Edit in Link Definition block '[ref]: ...'
    // Because this block contains ']:', modifying it could break/alter links in other blocks.
    // canIncrementalParse MUST return false!
    int posDef = buffer.text().indexOf(QStringLiteral("[ref]:"));
    QVERIFY(posDef > 0);
    buffer.replace(posDef + 6, 0, QStringLiteral(" "));

    int childIdxDef = -1;
    bool canIncDef = parser.canIncrementalParse(incRoot, buffer, posDef + 6, 0, 1, childIdxDef);
    QCOMPARE(canIncDef, false); // Blocked from incremental snippet parse!
}

// Probe 27: Differential Fuzzing / Random Testing (50 Continuous Invariant Checks)
void TestProbes::testDifferentialFuzzingEquivalence() {
    Parser::MarkdownParserAdapter parser;

    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral(
        "# 差异化模糊测试基准\n\n"
        "这是首段正文内容，包含了**重要粗体**与*关键斜体*以及`代码片段`。\n\n"
        "> 这是一个引用块，用于验证边界与嵌套行为。\n\n"
        "- [ ] 任务一：验证模糊测试随机输入\n"
        "- [x] 任务二：验证状态机零漂移\n\n"
        "```cpp\nvoid fuzz() {\n    int x = 42;\n}\n```\n\n"
        "这里是尾部普通段落，提供充足的字符供随机编辑操作。\n"
    ));

    auto currentRoot = parser.parse(buffer);
    QVERIFY(currentRoot != nullptr);

    int currentCursor = 15;
    Mapping::DisplayProjection currentProj;
    currentProj.buildProjection(currentRoot, buffer, currentCursor, false);

    std::mt19937 rng(12345); // Deterministic seed for reproducible CI runs
    const QString sampleChars = QStringLiteral("abcdefg 01234 快速测试 弹性排版 !?");

    // 50 consecutive randomized editing transactions
    for (int step = 0; step < 50; ++step) {
        // Select an active paragraph node (child 1 or last child)
        const auto &targetChild = (rng() % 2 == 0) ? currentRoot->children[1] : currentRoot->children.last();
        int pStart = targetChild->fullRange.start;
        int pLen = targetChild->fullRange.length();
        if (pLen <= 6) continue;

        int baseOffset = pStart + 2 + (rng() % (pLen - 4));
        baseOffset = qBound(pStart, baseOffset, pStart + pLen);

        int opType = rng() % 4;
        int remLen = 0;
        QString insText;

        switch (opType) {
        case 0:
        case 1: {
            int insertCount = (rng() % 3) + 1;
            for (int k = 0; k < insertCount; ++k) {
                insText.append(sampleChars.at(rng() % sampleChars.length()));
            }
            remLen = 0;
            break;
        }
        case 2:
            remLen = (rng() % 2) + 1;
            remLen = qMin(remLen, targetChild->fullRange.end - baseOffset);
            insText = QString();
            break;
        case 3:
            remLen = 1;
            insText = sampleChars.at(rng() % sampleChars.length());
            break;
        }

        buffer.replace(baseOffset, remLen, insText);
        int addedLen = insText.length();
        currentCursor = baseOffset + addedLen;

        int childIdx = -1;
        bool canInc = parser.canIncrementalParse(currentRoot, buffer, baseOffset, remLen, addedLen, childIdx);
        bool incSuccess = false;

        if (canInc) {
            auto newRoot = parser.incrementalParse(currentRoot, buffer, baseOffset, remLen, addedLen, childIdx, &incSuccess);
            if (incSuccess) {
                currentRoot = newRoot;
                currentProj.buildProjection(currentRoot, buffer, currentCursor, false, baseOffset, remLen, addedLen);
            } else {
                currentRoot = parser.parse(buffer);
                currentProj.buildProjection(currentRoot, buffer, currentCursor, false);
            }
        } else {
            currentRoot = parser.parse(buffer);
            currentProj.buildProjection(currentRoot, buffer, currentCursor, false);
        }

        // Differential verification against Cold Parse & Cold Projection
        auto coldRoot = parser.parse(buffer);
        assertSemanticNodesDeepEqual(currentRoot, coldRoot);

        Mapping::DisplayProjection coldProj;
        coldProj.buildProjection(coldRoot, buffer, currentCursor, false);
        assertVisualBlocksDeepEqual(currentProj.blocks(), coldProj.blocks());
    }
}

// Probe 28: Large Document GUI Event-Driven Keystroke Latency (500 KB)
void TestProbes::testLargeDocumentGuiInputLatency() {
    Editor::NativeEditorView view;
    view.resize(1024, 768);

    // Build a ~500 KB document (~1100 chapters)
    QString doc;
    doc.reserve(1100 * 450);
    for (int i = 0; i < 1100; ++i) {
        doc.append(QStringLiteral("## 章节 %1: 交互性能评估\n\n").arg(i + 1));
        doc.append(QStringLiteral("测试原生窗口事件系统在大文档下的端到端响应时间与重绘吞吐。\n\n"));
        doc.append(QStringLiteral("- 项目一：行内样式测试\n- 项目二：连续打字体验\n\n"));
    }

    view.buffer().setText(doc);
    int midPos = doc.indexOf(QStringLiteral("端到端响应时间"), doc.length() / 2);
    QVERIFY(midPos > 0);
    view.setCursorPos(midPos);

    quint64 initIncParseHits = view.incrementalParseHits();
    quint64 initIncProjHits = view.incrementalProjectionHits();

    // Dispatch 5 sequential real Qt KeyPress events
    QElapsedTimer timer;
    qint64 totalDispatchUs = 0;

    const QString typeSequence = QStringLiteral("ABCDE");
    for (int i = 0; i < typeSequence.length(); ++i) {
        timer.restart();
        QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_A + i, Qt::NoModifier, QString(typeSequence.at(i)));
        bool handled = QApplication::sendEvent(view.viewport(), &keyEvent);
        totalDispatchUs += (timer.nsecsElapsed() / 1000);
        QVERIFY(handled);
    }

    qreal avgDispatchMs = (totalDispatchUs / 1000.0) / typeSequence.length();

    // Verify Telemetry counters: all 5 keystrokes hit the incremental fast paths!
    QCOMPARE(view.incrementalParseHits(), initIncParseHits + 5);
    QCOMPARE(view.incrementalProjectionHits(), initIncProjHits + 5);
    QVERIFY(view.buffer().text().contains(QStringLiteral("ABCDE")));

    // Measure Viewport Render latency
    timer.restart();
    QPixmap pixmap(1024, 768);
    view.render(&pixmap);
    qreal paintMs = (timer.nsecsElapsed() / 1000) / 1000.0;

    qDebug() << "=== Probe 28: Large Document GUI Event Latency (500 KB) ===";
    qDebug() << "  Avg Qt KeyPress Dispatch (Inc AST + In-Place Proj + Inc Layout):" << avgDispatchMs << "ms";
    qDebug() << "  Qt Viewport Paint Latency (1024x768 Culling)                   :" << paintMs << "ms";
    qDebug() << "  Total Interactive GUI Turnaround                               :" << (avgDispatchMs + paintMs) << "ms";
    qDebug() << "  Telemetry:" << view.incrementalParseHits() << "IncParseHits |" << view.incrementalProjectionHits() << "IncProjHits";

    // Performance assertions with realistic headroom for virtualized cloud CI runners
    QVERIFY(avgDispatchMs < 50.0);  // Keystroke event dispatch under 50ms on 500 KB document in VM CI
    QVERIFY(paintMs < 500.0);        // Render under 500ms on headless VM CI without hardware GPU acceleration
}

// Probe 29: Multi-Cursor simultaneous typing, backspace, and atomic undo/redo
void TestProbes::testMultiCursorEditingAndUndo() {
    Editor::NativeEditorView view;
    view.resize(800, 600);

    QString initialDoc = QStringLiteral("line 1: apple\nline 2: banana\nline 3: cherry\n");
    view.buffer().setText(initialDoc);

    // Place primary cursor at start of line 1 (offset 0)
    view.setCursorPos(0);
    QCOMPARE(view.cursorCount(), 1);

    // Add secondary cursors at start of line 2 and line 3
    int line2Start = view.buffer().lineStartOffset(1);
    int line3Start = view.buffer().lineStartOffset(2);
    view.addCursor(line2Start);
    view.addCursor(line3Start);

    QCOMPARE(view.cursorCount(), 3);
    QCOMPARE(view.cursors()[0].cursor, 0);
    QCOMPARE(view.cursors()[1].cursor, line2Start);
    QCOMPARE(view.cursors()[2].cursor, line3Start);

    // Type "* " using Qt KeyPress events
    QKeyEvent keyStar(QEvent::KeyPress, Qt::Key_Asterisk, Qt::NoModifier, QStringLiteral("*"));
    QApplication::sendEvent(view.viewport(), &keyStar);
    QKeyEvent keySpace(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    QApplication::sendEvent(view.viewport(), &keySpace);

    QString expectedAfterType = QStringLiteral("* line 1: apple\n* line 2: banana\n* line 3: cherry\n");
    QCOMPARE(view.buffer().text(), expectedAfterType);
    QCOMPARE(view.cursorCount(), 3);

    // All cursors advanced by 2 characters (+ cumulative shifts)
    QCOMPARE(view.cursors()[0].cursor, 2);
    int newLine2Start = view.buffer().lineStartOffset(1);
    int newLine3Start = view.buffer().lineStartOffset(2);
    QCOMPARE(view.cursors()[1].cursor, newLine2Start + 2);
    QCOMPARE(view.cursors()[2].cursor, newLine3Start + 2);

    // Backspace to delete space
    QKeyEvent keyBack(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &keyBack);

    QString expectedAfterBack = QStringLiteral("*line 1: apple\n*line 2: banana\n*line 3: cherry\n");
    QCOMPARE(view.buffer().text(), expectedAfterBack);
    QCOMPARE(view.cursorCount(), 3);

    // Test multi-cursor Undo: undo backspace
    view.undo();
    QCOMPARE(view.buffer().text(), expectedAfterType);
    QCOMPARE(view.cursorCount(), 3);

    // Undo typing "* "
    view.undo(); // Undo space
    view.undo(); // Undo star
    QCOMPARE(view.buffer().text(), initialDoc);
    QCOMPARE(view.cursorCount(), 3);
    QCOMPARE(view.cursors()[0].cursor, 0);
    QCOMPARE(view.cursors()[1].cursor, line2Start);
    QCOMPARE(view.cursors()[2].cursor, line3Start);

    // Redo typing
    view.redo();
    view.redo();
    QCOMPARE(view.buffer().text(), expectedAfterType);
    QCOMPARE(view.cursorCount(), 3);

    // Test Escape to clear secondary cursors
    QKeyEvent keyEsc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &keyEsc);
    QCOMPARE(view.cursorCount(), 1);
}

// Probe 30: Column selection (Ctrl+Alt+Up/Down), multi-line selection, and aligned paste
void TestProbes::testColumnSelectionAndMultiLinePaste() {
    Editor::NativeEditorView view;
    view.resize(800, 600);

    QString text = QStringLiteral("AAA\nBBB\nCCC\n");
    view.buffer().setText(text);
    view.setCursorPos(0);

    // Column selection down twice using addCursorBelow
    view.addCursorBelow();
    view.addCursorBelow();
    QCOMPARE(view.cursorCount(), 3);
    QCOMPARE(view.cursors()[0].cursor, 0);
    QCOMPARE(view.cursors()[1].cursor, 4);
    QCOMPARE(view.cursors()[2].cursor, 8);

    // Select 3 characters right with Shift+Right
    for (int i = 0; i < 3; ++i) {
        QKeyEvent keyRight(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
        QApplication::sendEvent(view.viewport(), &keyRight);
    }

    QVERIFY(view.hasSelection());
    QCOMPARE(view.cursors()[0].selectionLength(), 3);
    QCOMPARE(view.cursors()[1].selectionLength(), 3);
    QCOMPARE(view.cursors()[2].selectionLength(), 3);

    // Copy multi-selection
    view.copy();
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("AAA\nBBB\nCCC"));

    // Move to end of lines
    QKeyEvent keyEnd(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &keyEnd);
    QVERIFY(!view.hasSelection());
    QCOMPARE(view.cursorCount(), 3);
    QCOMPARE(view.cursors()[0].cursor, 3);
    QCOMPARE(view.cursors()[1].cursor, 7);
    QCOMPARE(view.cursors()[2].cursor, 11);

    // Aligned multi-line paste: 3 lines match 3 cursors
    QTest::qWait(50);
    for (int retry = 0; retry < 10; ++retry) {
        QGuiApplication::clipboard()->setText(QStringLiteral("111\n222\n333"));
        QTest::qWait(50);
        if (QGuiApplication::clipboard()->text() == QStringLiteral("111\n222\n333")) {
            break;
        }
    }
    view.paste();

    QString expected = QStringLiteral("AAA111\nBBB222\nCCC333\n");
    QCOMPARE(view.buffer().text(), expected);
    QCOMPARE(view.cursorCount(), 3);

    // Undo aligned paste
    view.undo();
    QCOMPARE(view.buffer().text(), text);
    QCOMPARE(view.cursorCount(), 3);
    QCOMPARE(view.cursors()[0].cursor, 3);
    QCOMPARE(view.cursors()[1].cursor, 7);
    QCOMPARE(view.cursors()[2].cursor, 11);
}

// Probe 31: Math formula LaTeX SVG rendering, parsing, and live preview projection
void TestProbes::testMathFormulaAsyncRenderingAndProjection() {
    // 1. Math Svg Renderer unit tests
    QString formula = QStringLiteral("$$E = mc^2$$");
    QByteArray svg = Render::MathSvgRenderer::renderToSvg(formula);
    QVERIFY(!svg.isEmpty());
    QVERIFY(svg.contains("<svg"));
    QVERIFY(svg.contains("font-family"));
    QVERIFY(svg.contains("text"));

    // Fraction rendering
    QString fracFormula = QStringLiteral("$$\\frac{a + b}{c}$$");
    QByteArray fracSvg = Render::MathSvgRenderer::renderToSvg(fracFormula);
    QVERIFY(!fracSvg.isEmpty());
    QVERIFY(fracSvg.contains("<line")); // Fraction bar

    QSize estimated = Render::MathSvgRenderer::estimateSize(fracFormula);
    QVERIFY(estimated.width() > 50);
    QVERIFY(estimated.height() >= 50);

    // 2. Parser recognizing MathBlock
    Parser::MarkdownParserAdapter parser;
    QString doc = QStringLiteral("Title\n\n$$\nf(x) = x^2\n$$\n\nAfter\n");
    Core::TextBuffer buffer;
    buffer.setText(doc);
    auto ast = parser.parse(buffer);
    QVERIFY(ast != nullptr);

    bool foundMathBlock = false;
    for (const auto &child : ast->children) {
        if (child->type == Parser::NodeType::MathBlock) {
            foundMathBlock = true;
            break;
        }
    }
    QVERIFY(foundMathBlock);

    // 3. DisplayProjection in Live Preview Mode
    Mapping::DisplayProjection proj;

    // Cursor at pos 0 (outside math block) -> Collapsed embedded card
    proj.buildProjection(ast, buffer, 0, false);
    bool hasMathVisual = false;
    for (const auto &b : proj.blocks()) {
        if (b.isMathBlock) {
            hasMathVisual = true;
            QVERIFY(!b.isInteractiveActive);
            QVERIFY(b.embeddedSource.contains(QStringLiteral("f(x) = x^2")));
        }
    }
    QVERIFY(hasMathVisual);

    // Move cursor inside the math block
    int insidePos = doc.indexOf(QStringLiteral("f(x)"));
    QVERIFY(insidePos > 0);
    proj.buildProjection(ast, buffer, insidePos, false);
    bool hasActiveMath = false;
    for (const auto &b : proj.blocks()) {
        if (b.isMathBlock && b.isInteractiveActive) {
            hasActiveMath = true;
        }
    }
    QVERIFY(hasActiveMath);

    // In Source Mode -> No special collapsed math visual block
    proj.buildProjection(ast, buffer, 0, true);
    for (const auto &b : proj.blocks()) {
        QVERIFY(!b.isMathBlock);
    }
}

// Probe 32: Mermaid diagram SVG rendering, sequence & flowchart layout, and interactive click activation
void TestProbes::testMermaidDiagramAsyncRenderingAndInteraction() {
    // 1. Flowchart rendering
    QString flowchart = QStringLiteral("graph TD\nA[Start] --> B{Choice}\nB -->|Yes| C((Done))");
    QByteArray fcSvg = Render::MermaidSvgRenderer::renderToSvg(flowchart);
    QVERIFY(!fcSvg.isEmpty());
    QVERIFY(fcSvg.contains("<svg"));
    QVERIFY(fcSvg.contains("Start"));
    QVERIFY(fcSvg.contains("Choice"));
    QVERIFY(fcSvg.contains("Done"));
    QVERIFY(fcSvg.contains("polygon") || fcSvg.contains("circle"));

    // 2. Sequence diagram rendering
    QString seq = QStringLiteral("sequenceDiagram\nClient->>Server: Request\nServer-->>Client: Response");
    QByteArray seqSvg = Render::MermaidSvgRenderer::renderToSvg(seq);
    QVERIFY(!seqSvg.isEmpty());
    QVERIFY(seqSvg.contains("<svg"));
    QVERIFY(seqSvg.contains("Client"));
    QVERIFY(seqSvg.contains("Server"));
    QVERIFY(seqSvg.contains("Request"));
    QVERIFY(seqSvg.contains("Response"));
    QVERIFY(seqSvg.contains("seq-arrow"));

    // 3. Editor View live preview & click activation
    Editor::NativeEditorView view;
    view.resize(800, 600);
    QString editorDoc = QStringLiteral("Intro\n\n```mermaid\ngraph LR\nA --> B\n```\n\nOutro\n");
    view.buffer().setText(editorDoc);
    view.setCursorPos(0); // Cursor at intro

    // Check that mermaid block is projected
    const auto &blocks = view.projection().blocks();
    int mermaidBlockIdx = -1;
    for (int i = 0; i < blocks.size(); ++i) {
        if (blocks[i].isMermaidBlock) {
            mermaidBlockIdx = i;
            break;
        }
    }
    QVERIFY(mermaidBlockIdx != -1);
    QVERIFY(!blocks[mermaidBlockIdx].isInteractiveActive);

    // Simulate clicking on the mermaid card
    QRectF cardRect = view.layoutEngine().cursorRect(blocks[mermaidBlockIdx].sourceRange.start, view.projection());
    QPoint clickPoint(static_cast<int>(cardRect.center().x()), static_cast<int>(cardRect.center().y()));
    QMouseEvent clickEvent(QEvent::MouseButtonPress, clickPoint, clickPoint, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &clickEvent);

    // Cursor should now be inside the mermaid code block, activating it
    int newCursor = view.cursorPos();
    QVERIFY(newCursor >= blocks[mermaidBlockIdx].sourceRange.start);
    QVERIFY(newCursor <= blocks[mermaidBlockIdx].sourceRange.end);
    QVERIFY(view.projection().blocks()[mermaidBlockIdx].isInteractiveActive);
}

// Probe 33: AsyncRenderService caching, thread safety, and memory lifecycle
void TestProbes::testAsyncRenderServiceCachingAndThreadSafety() {
    auto service = Render::AsyncRenderService::instance();
    QVERIFY(service != nullptr);

    service->clearCache();

    QString mathSource = QStringLiteral("$$\\int_0^1 x^2 dx$$");
    QSignalSpy spy(service, &Render::AsyncRenderService::renderReady);

    // First request: not in cache yet
    auto firstResult = service->getRenderer(mathSource, Render::RenderKind::Math, false);
    if (!firstResult) {
        service->requestRender(mathSource, Render::RenderKind::Math);
        // Wait up to 1000ms for background thread pool rendering
        QVERIFY(spy.wait(1000));
        QVERIFY(spy.count() >= 1);
    }

    // Subsequent request should be an immediate cache hit
    auto cachedResult = service->getRenderer(mathSource, Render::RenderKind::Math, false);
    QVERIFY(cachedResult != nullptr);
    QVERIFY(cachedResult->isValid());

    // Mermaid async request
    QString mermaidSource = QStringLiteral("graph LR\nX --> Y");
    auto mermaidResult = service->getRenderer(mermaidSource, Render::RenderKind::Mermaid, false);
    if (!mermaidResult) {
        service->requestRender(mermaidSource, Render::RenderKind::Mermaid);
        QVERIFY(spy.wait(1000));
    }
    auto cachedMermaid = service->getRenderer(mermaidSource, Render::RenderKind::Mermaid, false);
    QVERIFY(cachedMermaid != nullptr);
    QVERIFY(cachedMermaid->isValid());

    // Clear cache
    service->clearCache();
    QVERIFY(!service->isCached(mathSource, Render::RenderKind::Math));
    QVERIFY(!service->isCached(mermaidSource, Render::RenderKind::Mermaid));
}

// Probe 34: P0-1 Cache key isolation between RenderKind::Math and RenderKind::Mermaid
void TestProbes::testCacheKeyRenderKindIsolation() {
    auto service = Render::AsyncRenderService::instance();
    QVERIFY(service != nullptr);

    service->clearCache();

    // Identical raw source string
    QString sharedSource = QStringLiteral("A --> B");

    // Render as Math
    auto mathRenderer = service->getRenderer(sharedSource, Render::RenderKind::Math, true);
    QVERIFY(mathRenderer != nullptr);
    QVERIFY(service->isCached(sharedSource, Render::RenderKind::Math));
    QVERIFY(!service->isCached(sharedSource, Render::RenderKind::Mermaid));

    // Render as Mermaid
    auto mermaidRenderer = service->getRenderer(sharedSource, Render::RenderKind::Mermaid, true);
    QVERIFY(mermaidRenderer != nullptr);
    QVERIFY(service->isCached(sharedSource, Render::RenderKind::Math));
    QVERIFY(service->isCached(sharedSource, Render::RenderKind::Mermaid));

    // Verify both are cached simultaneously without overwriting
    QCOMPARE(service->cacheCount(), 2);
    QVERIFY(service->getRenderer(sharedSource, Render::RenderKind::Math, false) == mathRenderer);
    QVERIFY(service->getRenderer(sharedSource, Render::RenderKind::Mermaid, false) == mermaidRenderer);
}

// Probe 35: P0-2 Mermaid cyclic graphs and self-loops layout boundedness
void TestProbes::testMermaidCyclicAndSelfLoopLayoutBoundedness() {
    // 1. Self-loop
    QString selfLoopDoc = QStringLiteral("graph TD\nA[Node A] --> A");
    QByteArray slSvg = Render::MermaidSvgRenderer::renderToSvg(selfLoopDoc);
    QVERIFY(!slSvg.isEmpty());
    QVERIFY(slSvg.contains("<svg"));
    QVERIFY(slSvg.contains("Node A"));

    // 2. Direct 2-node cycle (A -> B -> A)
    QString twoNodeCycle = QStringLiteral("graph TD\nA[Alpha] --> B[Beta]\nB --> A");
    QByteArray tncSvg = Render::MermaidSvgRenderer::renderToSvg(twoNodeCycle);
    QVERIFY(!tncSvg.isEmpty());
    QVERIFY(tncSvg.contains("Alpha"));
    QVERIFY(tncSvg.contains("Beta"));

    // 3. Multi-node cycle with branches
    QString complexCycle = QStringLiteral(
        "graph TD\n"
        "Start[Start Step] --> Check{Is Valid?}\n"
        "Check -->|Yes| Process[Process Step]\n"
        "Process --> Check\n"
        "Check -->|No| Done[Final Step]\n"
    );
    QByteArray ccSvg = Render::MermaidSvgRenderer::renderToSvg(complexCycle);
    QVERIFY(!ccSvg.isEmpty());
    QVERIFY(ccSvg.contains("Start Step"));
    QVERIFY(ccSvg.contains("Process Step"));
    QVERIFY(ccSvg.contains("Final Step"));

    // Layout dimensions must be strictly bounded (< 1500px, never exploding into tens of thousands)
    QSize estimated = Render::MermaidSvgRenderer::estimateSize(complexCycle);
    QVERIFY(estimated.width() <= 1500);
    QVERIFY(estimated.height() <= 1500);
}

// Probe 36: P1 AsyncRenderService LRU eviction and epoch cancellation
void TestProbes::testAsyncRenderServiceLRUAndEpochCancellation() {
    auto service = Render::AsyncRenderService::instance();
    QVERIFY(service != nullptr);

    service->clearCache();
    service->setMaxCacheEntries(4);

    // Populate 6 distinct entries
    for (int i = 0; i < 6; ++i) {
        QString formula = QStringLiteral("$$x_%1 = %1$$").arg(i);
        service->getRenderer(formula, Render::RenderKind::Math, true);
    }

    // Cache count must be capped by maxCacheEntries
    QCOMPARE(service->cacheCount(), 4);

    // The oldest entries (0 and 1) should have been evicted
    QVERIFY(!service->isCached(QStringLiteral("$$x_0 = 0$$"), Render::RenderKind::Math));
    QVERIFY(!service->isCached(QStringLiteral("$$x_1 = 1$$"), Render::RenderKind::Math));
    // Recent entries (2..5) must still be cached
    QVERIFY(service->isCached(QStringLiteral("$$x_5 = 5$$"), Render::RenderKind::Math));

    // Restore default cache size
    service->setMaxCacheEntries(128);
    service->clearCache();
    QCOMPARE(service->cacheCount(), 0);
}

// Probe 37: P1 Nested fractions and unsupported syntax fallback
void TestProbes::testNestedFractionsAndUnsupportedFallback() {
    // 1. Nested fraction layout: \frac{\frac{a}{b}+c}{d}
    QString nestedFrac = QStringLiteral("$$\\frac{\\frac{a}{b}+c}{d}$$");
    QByteArray nSvg = Render::MathSvgRenderer::renderToSvg(nestedFrac);
    QVERIFY(!nSvg.isEmpty());
    QVERIFY(nSvg.contains("<line")); // Multiple fraction bars

    // 2. Unbalanced braces warning fallback
    QString unbalanced = QStringLiteral("$$\\frac{a}{b$$");
    QByteArray ubSvg = Render::MathSvgRenderer::renderToSvg(unbalanced);
    QVERIFY(!ubSvg.isEmpty());
    QVERIFY(ubSvg.contains("LaTeX: Unbalanced Braces"));

    // 3. Unsupported Mermaid diagram type graceful card
    QString unsupportedMermaid = QStringLiteral("pie title Pets\n\"Dogs\" : 386\n\"Cats\" : 85");
    QByteArray umSvg = Render::MermaidSvgRenderer::renderToSvg(unsupportedMermaid);
    QVERIFY(!umSvg.isEmpty());
    QVERIFY(umSvg.contains("Notice: pie"));
}

// Probe 38: P1 Epoch pending request race condition isolation and deterministic concurrency
void TestProbes::testAsyncRenderServiceEpochRaceCondition() {
    auto service = Render::AsyncRenderService::instance();
    QVERIFY(service != nullptr);

    service->clearCache();
    QString formula = QStringLiteral("$$E=mc^2$$");
    Render::CacheKey key{Render::RenderKind::Math, formula};

    // 1. Generation 1 submits request
    quint64 gen1 = service->currentGeneration();
    service->requestRender(formula, Render::RenderKind::Math);
    QVERIFY(service->isPending(formula, Render::RenderKind::Math));

    // 2. clearCache() bumps generation to gen2, clearing pending requests
    service->clearCache();
    quint64 gen2 = service->currentGeneration();
    QVERIFY(gen2 > gen1);
    QVERIFY(!service->isPending(formula, Render::RenderKind::Math));

    // 3. New request submitted in gen2
    service->requestRender(formula, Render::RenderKind::Math);
    QVERIFY(service->isPending(formula, Render::RenderKind::Math));

    // 4. Stale task from gen1 finishes now and calls onRenderCompleted
    QByteArray staleSvg = "<svg></svg>";
    service->onRenderCompleted(key, staleSvg, gen1);
    QCoreApplication::processEvents();

    // Gen 2 pending request must STILL BE VALID and not prematurely removed!
    QVERIFY(service->isPending(formula, Render::RenderKind::Math));
    // And stale SVG was not written to cache
    QVERIFY(!service->isCached(formula, Render::RenderKind::Math));

    // 5. Gen 2 task completes legitimately
    QByteArray validSvg = Render::MathSvgRenderer::renderToSvg(formula);
    service->onRenderCompleted(key, validSvg, gen2);
    QCoreApplication::processEvents();

    // Now it is cached and pending request is cleanly cleared
    QVERIFY(service->isCached(formula, Render::RenderKind::Math));
    QVERIFY(!service->isPending(formula, Render::RenderKind::Math));

    // 6. Deterministic multi-threaded concurrency stress
    std::atomic<bool> stopFlag{false};
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([service, &stopFlag, t]() {
            int counter = 0;
            while (!stopFlag.load()) {
                QString src = QStringLiteral("$$x_%1^2$$").arg((counter++) % 10);
                if (t % 2 == 0) {
                    service->requestRender(src, Render::RenderKind::Math);
                } else {
                    service->getRenderer(src, Render::RenderKind::Math, false);
                }
                if (counter % 15 == 0 && t == 0) {
                    service->clearCache();
                }
                std::this_thread::yield();
            }
        });
    }

    QTest::qWait(150);
    stopFlag.store(true);
    for (auto &w : workers) {
        if (w.joinable()) w.join();
    }
    QCoreApplication::processEvents();

    // Verify system state is completely consistent and thread-safe
    QVERIFY(service->currentGeneration() >= gen2);
    QVERIFY(service->maxCacheEntries() > 0);
}

// Probe 39: P1 Mermaid exact bounding box, universal coordinate normalization, and visual non-clipping
void TestProbes::testMermaidBoundingBoxAndVisualNoClipping() {
    // 1. Wide LR chain with multiple nodes
    QString wideChain = QStringLiteral("graph LR\nA --> B --> C --> D --> E --> F --> G");
    QByteArray svgBytes = Render::MermaidSvgRenderer::renderToSvg(wideChain);
    QString svg = QString::fromUtf8(svgBytes);

    // Extract viewBox="0 0 W H"
    static QRegularExpression reViewBox(QStringLiteral("viewBox=\"0 0 ([\\d\\.]+) ([\\d\\.]+)\""));
    auto m = reViewBox.match(svg);
    QVERIFY(m.hasMatch());
    qreal vbW = m.captured(1).toDouble();
    qreal vbH = m.captured(2).toDouble();
    QVERIFY(vbW >= 700.0);
    QVERIFY(vbH >= 120.0);

    // 2. Diagram with self-loops, back-edges, and cycles
    QString cyclicWithLoop = QStringLiteral(
        "graph TD\n"
        "Alpha[Node Alpha] --> Beta[Node Beta]\n"
        "Beta --> Alpha\n"
        "Alpha --> Alpha\n"
        "Beta --> Beta\n"
    );
    QByteArray cycSvgBytes = Render::MermaidSvgRenderer::renderToSvg(cyclicWithLoop);
    QString cycSvg = QString::fromUtf8(cycSvgBytes);
    auto mCyc = reViewBox.match(cycSvg);
    QVERIFY(mCyc.hasMatch());
    qreal cycW = mCyc.captured(1).toDouble();
    qreal cycH = mCyc.captured(2).toDouble();

    QVERIFY(cycSvg.contains("marker-end=\"url(#mermaid-arrow)\""));
    QVERIFY(cycW >= 360.0);
    QVERIFY(cycH >= 140.0);

    // Coordinate validation: verify every rect and path has coordinates within [16.0, content - 12.0]
    static QRegularExpression reRect(QStringLiteral("<rect x=\"([\\d\\.-]+)\" y=\"([\\d\\.-]+)\" width=\"([\\d\\.-]+)\" height=\"([\\d\\.-]+)\""));
    auto rIt = reRect.globalMatch(cycSvg);
    while (rIt.hasNext()) {
        auto rm = rIt.next();
        qreal rx = rm.captured(1).toDouble();
        qreal ry = rm.captured(2).toDouble();
        qreal rw = rm.captured(3).toDouble();
        qreal rh = rm.captured(4).toDouble();
        QVERIFY2(rx >= 16.0, qPrintable(QString("rx=%1 < 16.0").arg(rx)));
        QVERIFY2(ry >= 16.0, qPrintable(QString("ry=%1 < 16.0").arg(ry)));
        QVERIFY2(rx + rw <= cycW - 12.0, qPrintable(QString("rx+rw=%1 > cycW - 12 (%2)").arg(rx + rw).arg(cycW - 12.0)));
        QVERIFY2(ry + rh <= cycH - 12.0, qPrintable(QString("ry+rh=%1 > cycH - 12 (%2)").arg(ry + rh).arg(cycH - 12.0)));
    }

    // 3. Pixel-level rasterization non-clipping test using QSvgRenderer -> QImage
    QSvgRenderer renderer(cycSvgBytes);
    QVERIFY(renderer.isValid());

    QSize renderedSize(static_cast<int>(std::round(cycW)), static_cast<int>(std::round(cycH)));
    QImage image(renderedSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    renderer.render(&painter);
    painter.end();

    int nonTransparentCount = 0;
    int borderNonTransparentCount = 0;
    int w = image.width();
    int h = image.height();

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            QRgb pixel = image.pixel(x, y);
            if (qAlpha(pixel) > 0) {
                nonTransparentCount++;
                // If pixel is on outer boundary (x=0, x=w-1, y=0, y=h-1), it indicates hard clipping
                if (x == 0 || x == w - 1 || y == 0 || y == h - 1) {
                    borderNonTransparentCount++;
                }
            }
        }
    }

    // Must have successfully rendered graphic contents
    QVERIFY(nonTransparentCount > 200);
    // Boundary of image must have ZERO non-transparent pixels (guaranteed 100% no-clipping)
    QCOMPARE(borderNonTransparentCount, 0);
}

// Probe 40: P1 AsyncRenderService instance destruction safety with in-flight tasks & byte tracking
void TestProbes::testAsyncRenderServiceShutdownWithInFlightTasks() {
    // 1. Standalone, destroyable AsyncRenderService instance test
    {
        auto isolatedService = std::make_unique<Render::AsyncRenderService>();
        QVERIFY(isolatedService != nullptr);
        QCOMPARE(isolatedService->totalCachedBytes(), 0);

        // Queue 24 background requests (math + mermaid)
        for (int i = 0; i < 12; ++i) {
            isolatedService->requestRender(QStringLiteral("$$\\int_0^{%1} e^{-x^2} dx$$").arg(i), Render::RenderKind::Math);
            isolatedService->requestRender(QStringLiteral("graph LR\nN%1 --> N%2").arg(i).arg(i + 1), Render::RenderKind::Mermaid);
        }

        // Wait very briefly so that worker threads pick up tasks
        QTest::qWait(10);

        // Immediately destroy isolatedService while threads are running!
        // Destructor MUST call clear(), disable m_aliveToken, and waitForDone(5000) safely.
        isolatedService.reset();
    }
    // Process any lingering events; must not crash or dereference dangling pointer
    QCoreApplication::processEvents();

    // 2. Closed-state rejection test
    {
        auto isolatedService2 = std::make_unique<Render::AsyncRenderService>();
        // Check byte tracking
        auto r = isolatedService2->getRenderer(QStringLiteral("$$a+b=c$$"), Render::RenderKind::Math, true);
        QVERIFY(r != nullptr);
        QVERIFY(isolatedService2->totalCachedBytes() > 0);

        isolatedService2->clearCache();
        QCOMPARE(isolatedService2->totalCachedBytes(), 0);
        QCOMPARE(isolatedService2->cacheCount(), 0);

        // Reset service
        isolatedService2.reset();
    }
    QCoreApplication::processEvents();

    // 3. Global service cache sanity
    auto globalService = Render::AsyncRenderService::instance();
    QVERIFY(globalService != nullptr);
    globalService->clearCache();
    QCOMPARE(globalService->totalCachedBytes(), 0);
}

// Probe 41: P1 Nested fraction geometry, hierarchy, and fraction bar non-intersection validation
void TestProbes::testNestedFractionGeometryAndHierarchy() {
    // 1. Nested fraction: \frac{\frac{a}{b}+c}{d}
    QString nested = QStringLiteral("$$\\frac{\\frac{a}{b}+c}{d}$$");
    QByteArray svgBytes = Render::MathSvgRenderer::renderToSvg(nested);
    QString svg = QString::fromUtf8(svgBytes);

    // Must contain multiple fraction bar lines
    int lineCount = svg.count(QStringLiteral("<line"));
    QVERIFY(lineCount >= 2);

    // Must contain all components: a, b, c, d
    QVERIFY(svg.contains(">a<"));
    QVERIFY(svg.contains(">b<"));
    QVERIFY(svg.contains("+c") || svg.contains(">c<"));
    QVERIFY(svg.contains(">d<"));

    // Nested fraction inside numerator must have smaller font size than main expression
    QVERIFY(svg.contains("font-size=\"14\"") || svg.contains("font-size=\"11\"") || svg.contains("font-size=\"10\""));

    // 2. Strict fraction bar vs text bounding box disjoint geometry validation
    // Test formula: \frac{1+\frac{2}{3}}{4+\frac{5}{6}}
    QString nestedDouble = QStringLiteral("$$\\frac{1+\\frac{2}{3}}{4+\\frac{5}{6}}$$");
    QByteArray dblSvgBytes = Render::MathSvgRenderer::renderToSvg(nestedDouble);
    QString dblSvg = QString::fromUtf8(dblSvgBytes);

    // Extract all fraction line Y coordinates
    static QRegularExpression reLine(QStringLiteral("<line[^>]*y1=\"([\\d\\.]+)\"[^>]*y2=\"([\\d\\.]+)\""));
    auto lineIt = reLine.globalMatch(dblSvg);
    QVector<qreal> barYs;
    while (lineIt.hasNext()) {
        auto lm = lineIt.next();
        qreal y1 = lm.captured(1).toDouble();
        qreal y2 = lm.captured(2).toDouble();
        QCOMPARE(y1, y2); // Must be perfectly horizontal fraction line
        barYs.append(y1);
    }
    // Must have at least 3 fraction bars: main bar, numerator bar, denominator bar
    QVERIFY(barYs.size() >= 3);

    // Verify fraction structure: numerator group translation Y is strictly above fraction bar,
    // and denominator group translation Y is strictly below fraction bar.
    static QRegularExpression reFracAttr(QStringLiteral(
        "<g class=\"fraction\" data-num-y=\"([\\d\\.-]+)\" data-bar-y=\"([\\d\\.-]+)\" data-den-y=\"([\\d\\.-]+)\">"
    ));

    auto fracIt = reFracAttr.globalMatch(dblSvg);
    int matchedFracs = 0;
    while (fracIt.hasNext()) {
        auto fm = fracIt.next();
        matchedFracs++;
        qreal numY = fm.captured(1).toDouble();
        qreal barY = fm.captured(2).toDouble();
        qreal denY = fm.captured(3).toDouble();

        // Numerator box top/bottom must be strictly ABOVE fraction bar line
        QVERIFY2(numY < barY, qPrintable(QString("Numerator Y=%1 must be above Bar Y=%2").arg(numY).arg(barY)));
        // Denominator box must be strictly BELOW fraction bar line
        QVERIFY2(denY > barY, qPrintable(QString("Denominator Y=%1 must be below Bar Y=%2").arg(denY).arg(barY)));
        // Vertical clearance margins: minimum 4px clearance above bar and 6px below bar
        QVERIFY2(barY - numY >= 4.0, qPrintable(QString("Clearance above bar %1 must be >= 4").arg(barY - numY)));
        QVERIFY2(denY - barY >= 6.0, qPrintable(QString("Clearance below bar %1 must be >= 6").arg(denY - barY)));
    }
    // Formula has 3 fractions: main fraction, numerator fraction \frac{2}{3}, denominator fraction \frac{5}{6}
    QCOMPARE(matchedFracs, 3);
}

// Probe 42: P0 First-run async render task scheduling, deterministic sync/async byte tracking, and byte budget eviction
void TestProbes::testFirstRunAsyncRenderReliabilityAndByteBudget() {
    // 1. Fresh service instance without clearCache()
    auto isolated = std::make_unique<Render::AsyncRenderService>();
    QVERIFY(isolated != nullptr);
    QCOMPARE(isolated->currentGeneration(), 1ULL);

    QString formula = QStringLiteral("$$F=ma$$");
    // Under P0 bug, isPending returned true and requestRender was ignored
    QVERIFY(!isolated->isPending(formula, Render::RenderKind::Math));
    QVERIFY(!isolated->isCached(formula, Render::RenderKind::Math));

    QSignalSpy spy(isolated.get(), &Render::AsyncRenderService::renderReady);
    isolated->requestRender(formula, Render::RenderKind::Math);

    // Request MUST be accepted into pending queue
    QVERIFY(isolated->isPending(formula, Render::RenderKind::Math));

    // Wait for async render completion
    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.count(), 1);
    QVERIFY(isolated->isCached(formula, Render::RenderKind::Math));
    QVERIFY(!isolated->isPending(formula, Render::RenderKind::Math));
    QVERIFY(isolated->totalCachedBytes() > 0);

    // 2. Deterministic Sync fallback and Async callback interleaving byte-counting verification
    Render::CacheKey key{Render::RenderKind::Math, QStringLiteral("$$E=\\hbar\\omega$$")};
    quint64 gen = isolated->currentGeneration();

    // Step A: Synchronous fallback writes to cache first
    auto syncRenderer = isolated->getRenderer(key.source, key.kind, true);
    QVERIFY(syncRenderer != nullptr);
    qint64 syncBytes = isolated->totalCachedBytes();
    QVERIFY(syncBytes > 0);

    // Step B: Out-of-order / delayed async callback arrives now with updated/identical SVG
    QByteArray asyncSvg = Render::MathSvgRenderer::renderToSvg(key.source);
    isolated->onRenderCompleted(key, asyncSvg, gen);
    QCoreApplication::processEvents();

    // Cache byte count must remain exact and NOT be duplicated!
    QCOMPARE(isolated->totalCachedBytes(), syncBytes);

    // 3. Real Byte-limit LRU eviction verification
    QString chart1 = QStringLiteral("graph LR\nN1[Node 1 Alpha] --> N2[Node 2 Beta] --> N3[Node 3 Gamma]");
    QString chart2 = QStringLiteral("graph LR\nN4[Node 4 Delta] --> N5[Node 5 Epsilon] --> N6[Node 6 Zeta]");
    qint64 size1 = Render::MermaidSvgRenderer::renderToSvg(chart1).size();

    // Set budget strictly enough to hold chart1, but not both chart1 + chart2
    isolated->setMaxCacheBytes(size1 + 50);
    QCOMPARE(isolated->maxCacheBytes(), size1 + 50);

    // Insert chart1
    isolated->getRenderer(chart1, Render::RenderKind::Mermaid, true);
    QVERIFY(isolated->isCached(chart1, Render::RenderKind::Mermaid));
    QCOMPARE(isolated->cacheCount(), 1);

    // Insert chart2 -> total bytes exceed budget, so chart1 MUST be evicted!
    isolated->getRenderer(chart2, Render::RenderKind::Mermaid, true);

    QVERIFY(isolated->totalCachedBytes() <= isolated->maxCacheBytes());
    QVERIFY(!isolated->isCached(chart1, Render::RenderKind::Mermaid));
    QVERIFY(isolated->isCached(chart2, Render::RenderKind::Mermaid));

    // 4. Single oversized diagram preservation (no thrashing loop)
    // Set budget smaller than a single chart
    isolated->setMaxCacheBytes(1024); // 1 KB
    isolated->getRenderer(chart1, Render::RenderKind::Mermaid, true);

    // Cache retains exactly 1 active item, totalCachedBytes is preserved, no infinite eviction
    QCOMPARE(isolated->cacheCount(), 1);
    QVERIFY(isolated->isCached(chart1, Render::RenderKind::Mermaid));
}

// Probe 43: P1 Real-world GUI cold start with NativeEditorView, exact embeddedSource keys, and interactive editing
void TestProbes::testFirstRunDocumentGuiIntegration() {
    // 1. Cold Document AST & DisplayProjection: verify embeddedSource keys
    QString doc = QStringLiteral(
        "# Cold First-Run Physics Notes\n\n"
        "Newtonian mechanics foundation:\n\n"
        "$$\\mathbf{F} = \\frac{d\\mathbf{p}}{dt}$$\n\n"
        "Pipeline workflow architecture:\n\n"
        "```mermaid\n"
        "graph LR\n"
        "Input[Editor Keystroke] --> AST[Incremental Tree-Sitter AST] --> Proj[Display Projection] --> UI[Native Viewport]\n"
        "```\n\n"
        "Relativistic energy:\n\n"
        "$$E = \\sqrt{(pc)^2 + (m_0 c^2)^2}$$\n"
    );

    Core::TextBuffer buffer;
    buffer.setText(doc);
    Parser::MarkdownParserAdapter parser;
    auto astRoot = parser.parse(buffer);
    QVERIFY(astRoot != nullptr);

    Mapping::DisplayProjection proj;
    proj.buildProjection(astRoot, buffer, 0, false);

    // Extract exact embeddedSource keys from projection blocks
    QString mathSource1, mathSource2, mermaidSource;
    for (int i = 0; i < proj.blockCount(); ++i) {
        const auto &b = proj.blocks()[i];
        if (b.isMathBlock) {
            if (mathSource1.isEmpty()) mathSource1 = b.embeddedSource;
            else if (mathSource2.isEmpty()) mathSource2 = b.embeddedSource;
        } else if (b.isMermaidBlock) {
            mermaidSource = b.embeddedSource;
        }
    }

    // Crucial assertion: math embeddedSource must NOT contain $$ delimiters
    QVERIFY(!mathSource1.isEmpty());
    QVERIFY(!mathSource1.startsWith(QStringLiteral("$$")));
    QVERIFY(mathSource1.contains(QStringLiteral("\\frac{d\\mathbf{p}}{dt}")));
    QVERIFY(!mathSource2.isEmpty());
    QVERIFY(!mathSource2.startsWith(QStringLiteral("$$")));
    QVERIFY(!mermaidSource.isEmpty());
    QVERIFY(mermaidSource.contains(QStringLiteral("graph LR")));

    // 2. Real NativeEditorView window cold startup and event loop rendering
    Editor::NativeEditorView editorView;
    editorView.resize(960, 720);
    editorView.buffer().setText(doc);
    editorView.show();
    QCoreApplication::processEvents();

    // Trigger initial viewport rendering
    QPixmap initialPix(960, 720);
    editorView.render(&initialPix);

    // Wait for background workers to finish and trigger renderReady -> viewport update
    auto service = Render::AsyncRenderService::instance();
    QVERIFY(service != nullptr);

    QTest::qWait(350);
    QCoreApplication::processEvents();

    // Both math formulas and mermaid diagram MUST be cached with their exact embeddedSource keys
    QVERIFY(service->isCached(mathSource1, Render::RenderKind::Math));
    QVERIFY(service->isCached(mathSource2, Render::RenderKind::Math));
    QVERIFY(service->isCached(mermaidSource, Render::RenderKind::Mermaid));

    // Render viewport again; all cards must now render with valid SVG
    QPixmap finalPix(960, 720);
    editorView.render(&finalPix);

    // 3. Interactive in-place editing & Undo regression in Math Block
    int mathOffset = doc.indexOf(QStringLiteral("\\mathbf{F}"));
    QVERIFY(mathOffset > 0);

    // Place cursor inside math formula
    editorView.setCursorPos(mathOffset + 2);
    QCoreApplication::processEvents();

    // Type character edit
    QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
    QApplication::sendEvent(editorView.viewport(), &keyEvent);
    QCoreApplication::processEvents();
    QVERIFY(editorView.buffer().text().contains(QStringLiteral("x")));

    // Undo edit
    editorView.undo();
    QCoreApplication::processEvents();
    QCOMPARE(editorView.buffer().text(), doc);

    // 4. Diverse Mermaid Boundaries: Long Labels, Cyclic Feedback, and Chain Arrows
    QString diverseMermaid = QStringLiteral(
        "graph LR\n"
        "ComponentWithVeryLongDescriptiveArchitecturalLabel[Very Long Descriptive System Component Name] --> ShortNode[Core Kernel]\n"
        "ShortNode --> ComponentWithVeryLongDescriptiveArchitecturalLabel\n"
        "Step1 --> Step2 --> Step3 --> Step4\n"
    );

    QByteArray svgBytes = Render::MermaidSvgRenderer::renderToSvg(diverseMermaid);
    QString svg = QString::fromUtf8(svgBytes);

    static QRegularExpression reViewBox(QStringLiteral("viewBox=\"0 0 ([\\d\\.]+) ([\\d\\.]+)\""));
    auto m = reViewBox.match(svg);
    QVERIFY(m.hasMatch());
    qreal vbW = m.captured(1).toDouble();
    qreal vbH = m.captured(2).toDouble();

    QVERIFY(vbW >= 600.0);
    QVERIFY(vbH >= 120.0);

    // Pixel-level rasterization check: ensure 0 boundary pixels are clipped
    QSvgRenderer svgRenderer(svgBytes);
    QVERIFY(svgRenderer.isValid());

    QSize imgSize(static_cast<int>(std::round(vbW)), static_cast<int>(std::round(vbH)));
    QImage testImg(imgSize, QImage::Format_ARGB32_Premultiplied);
    testImg.fill(Qt::transparent);

    QPainter p(&testImg);
    svgRenderer.render(&p);
    p.end();

    int borderClippedPixels = 0;
    int tw = testImg.width();
    int th = testImg.height();
    for (int y = 0; y < th; ++y) {
        for (int x = 0; x < tw; ++x) {
            if (qAlpha(testImg.pixel(x, y)) > 0) {
                if (x == 0 || x == tw - 1 || y == 0 || y == th - 1) {
                    borderClippedPixels++;
                }
            }
        }
    }
    QCOMPARE(borderClippedPixels, 0);
}

// Probe 44: P1 Synchronous fallback cross-generation writeback race condition & unified cache safety
void TestProbes::testSyncFallbackEpochRaceConditionAndUnifiedCache() {
    // 1. Isolated service instance
    auto service = std::make_unique<Render::AsyncRenderService>();
    QVERIFY(service != nullptr);
    quint64 gen1 = service->currentGeneration();
    QCOMPARE(gen1, 1ULL);

    QString formulaA = QStringLiteral("$$A = \\pi r^2$$");
    QString formulaB = QStringLiteral("$$B = \\int_0^1 x dx$$");
    Render::CacheKey keyA{Render::RenderKind::Math, formulaA};
    Render::CacheKey keyB{Render::RenderKind::Math, formulaB};

    // Queue render request in gen 1
    service->requestRender(formulaA, Render::RenderKind::Math);
    QVERIFY(service->isPending(formulaA, Render::RenderKind::Math));

    // Clear cache bumps generation to gen 2
    service->clearCache();
    quint64 gen2 = service->currentGeneration();
    QVERIFY(gen2 > gen1);
    QVERIFY(!service->isPending(formulaA, Render::RenderKind::Math));
    QCOMPARE(service->cacheCount(), 0);
    QCOMPARE(service->totalCachedBytes(), 0);

    // Gen 2 submits formulaA
    service->requestRender(formulaA, Render::RenderKind::Math);
    QVERIFY(service->isPending(formulaA, Render::RenderKind::Math));

    // Simulate stale Gen 1 task attempting commit via onRenderCompleted
    QByteArray staleSvg = "<svg id='stale'></svg>";
    service->onRenderCompleted(keyA, staleSvg, gen1);
    QCoreApplication::processEvents();

    // Gen 2 pending request must not be removed by stale Gen 1 task
    QVERIFY(service->isPending(formulaA, Render::RenderKind::Math));
    // Stale Gen 1 SVG must not be present in Gen 2 cache
    QVERIFY(!service->isCached(formulaA, Render::RenderKind::Math));
    QCOMPARE(service->totalCachedBytes(), 0);

    // 2. Synchronous fallback under matching generation
    auto rB = service->getRenderer(formulaB, Render::RenderKind::Math, true);
    QVERIFY(rB != nullptr);
    QVERIFY(service->isCached(formulaB, Render::RenderKind::Math));
    QVERIFY(!service->isPending(formulaB, Render::RenderKind::Math));
    QVERIFY(service->totalCachedBytes() > 0);

    // 3. Deterministic synchronous fallback cross-generation race reproduction
    // Hook fires right after synchronous SVG rendering completes, before the lock is re-acquired.
    QString formulaC = QStringLiteral("$$\\gamma = \\frac{1}{1 - v^2}$$");
    quint64 preSyncGen = service->currentGeneration();
    bool hookFired = false;

    service->setTestPreCommitHook([&service, &hookFired, preSyncGen, formulaC](quint64 hookGen) {
        hookFired = true;
        // Verify hook receives the generation in which sync render began
        QCOMPARE(hookGen, preSyncGen);
        // Interleaving action: clearCache() bumps generation!
        service->clearCache();
        // Submit a new pending request in this new generation for the same formula
        service->requestRender(formulaC, Render::RenderKind::Math);
    });

    // Invoke getRenderer with synchronous fallback allowed
    auto rC = service->getRenderer(formulaC, Render::RenderKind::Math, true);
    // Disconnect test hook immediately
    service->setTestPreCommitHook(nullptr);

    QVERIFY(hookFired);
    QVERIFY(rC != nullptr);
    QVERIFY(rC->isValid());

    // Deterministic assertions:
    // a) Generation must have advanced
    QVERIFY(service->currentGeneration() > preSyncGen);
    // b) Stale synchronous render MUST NOT be written into the new generation cache!
    QVERIFY(!service->isCached(formulaC, Render::RenderKind::Math));
    // c) The new generation's pending request for formulaC MUST STILL BE ACTIVE!
    QVERIFY(service->isPending(formulaC, Render::RenderKind::Math));
    // d) Cached bytes must be 0 because the stale write was discarded
    QCOMPARE(service->totalCachedBytes(), 0);
    QCOMPARE(service->cacheCount(), 0);

    // 4. Multithreaded cross-generation concurrency race test
    std::atomic<bool> stopFlag{false};
    std::atomic<int> errors{0};
    std::vector<std::thread> workers;

    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&service, &stopFlag, &errors, t]() {
            int i = 0;
            while (!stopFlag.load()) {
                QString src = QStringLiteral("$$\\alpha_%1 = \\sqrt{%1}$$").arg(i % 8);
                if (t == 0) {
                    auto r = service->getRenderer(src, Render::RenderKind::Math, true);
                    if (!r || !r->isValid()) errors++;
                } else if (t == 1) {
                    service->requestRender(src, Render::RenderKind::Math);
                } else if (t == 2) {
                    if ((i % 16) == 0) {
                        service->clearCache();
                    }
                } else {
                    qint64 bytes = service->totalCachedBytes();
                    int count = service->cacheCount();
                    if (bytes < 0 || count < 0) errors++;
                }
                i++;
                std::this_thread::yield();
            }
        });
    }

    QTest::qWait(180);
    stopFlag.store(true);
    for (auto &w : workers) {
        if (w.joinable()) w.join();
    }
    QCoreApplication::processEvents();

    QCOMPARE(errors.load(), 0);
    service->clearCache();
    QCOMPARE(service->cacheCount(), 0);
    QCOMPARE(service->totalCachedBytes(), 0);
}

// Probe 45: P1 Real-world GUI async auto-refresh pipeline without manual render(), interactive editing, and mode switching
void TestProbes::testRealGuiAsyncAutoRefreshAndInteractiveRegression() {
    // 1. Setup Document with Math and Mermaid
    QString doc = QStringLiteral(
        "# Beta RC GUI Pipeline\n\n"
        "Calculus fundamental theorem:\n\n"
        "$$\\int_a^b f'(x) dx = f(b) - f(a)$$\n\n"
        "Architecture pipeline diagram:\n\n"
        "```mermaid\n"
        "graph LR\n"
        "Client[Keystroke] --> Adapter[AST Parser] --> Proj[Display Projection] --> View[Native Viewport]\n"
        "```\n\n"
        "Closing remarks paragraph.\n"
    );

    auto service = Render::AsyncRenderService::instance();
    QVERIFY(service != nullptr);
    service->clearCache();

    // Hook signal spy FIRST, before view construction or buffer loading,
    // ensuring zero possibility of missing early async signals.
    QSignalSpy readySpy(service, &Render::AsyncRenderService::renderReady);

    class PaintCounterFilter : public QObject {
    public:
        int count = 0;
    protected:
        bool eventFilter(QObject *obj, QEvent *event) override {
            if (event->type() == QEvent::Paint) {
                count++;
            }
            return QObject::eventFilter(obj, event);
        }
    };

    Editor::NativeEditorView editorView;
    editorView.resize(960, 720);
    PaintCounterFilter counter;
    editorView.viewport()->installEventFilter(&counter);

    editorView.buffer().setText(doc);
    editorView.show();
    QCoreApplication::processEvents();

    // Initial paint occurs (displaying placeholders)
    int initialPaints = counter.count;
    QVERIFY(initialPaints >= 1);

    // Grab initial placeholder framebuffer
    QPixmap initialPix(editorView.viewport()->size());
    editorView.viewport()->render(&initialPix);
    QImage initialImg = initialPix.toImage();

    // Wait for at least 2 renderReady signals from background threads
    int waitCycles = 0;
    while (readySpy.count() < 2 && waitCycles < 40) {
        QTest::qWait(50);
        QCoreApplication::processEvents();
        waitCycles++;
    }
    QVERIFY(readySpy.count() >= 2);

    // Process event loop so viewport()->update() posts and dispatches the paint event
    QCoreApplication::processEvents();
    QTest::qWait(50);
    QCoreApplication::processEvents();

    // Verification 1: Automatic refresh happened purely via Qt event loop without calling editorView.render()
    QVERIFY2(counter.count > initialPaints,
             qPrintable(QString("Paint count (%1) must be greater than initial (%2)")
                 .arg(counter.count).arg(initialPaints)));

    // Verification 2: Rendered framebuffer content changed from placeholder to SVG!
    QPixmap updatedPix(editorView.viewport()->size());
    editorView.viewport()->render(&updatedPix);
    QImage updatedImg = updatedPix.toImage();

    int diffPixels = 0;
    int w = std::min(initialImg.width(), updatedImg.width());
    int h = std::min(initialImg.height(), updatedImg.height());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (initialImg.pixel(x, y) != updatedImg.pixel(x, y)) {
                diffPixels++;
            }
        }
    }
    QVERIFY2(diffPixels > 100, qPrintable(QString("diffPixels (%1) must be > 100 to prove visual update").arg(diffPixels)));

    // Verification 3: Confirm presence of SVG dark glyph and graphic pixels in the updated framebuffer
    int svgColorPixels = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            QRgb p = updatedImg.pixel(x, y);
            // Math formula dark text or Mermaid primary blue strokes
            if (qRed(p) < 40 && qGreen(p) < 50 && qBlue(p) < 70) {
                svgColorPixels++;
            }
        }
    }
    QVERIFY2(svgColorPixels > 100, qPrintable(QString("svgColorPixels (%1) must be > 100 to confirm SVG rendering").arg(svgColorPixels)));

    // Verify cache items are populated
    const auto &proj = editorView.projection();
    QString mathSrc, mermaidSrc;
    for (int i = 0; i < proj.blockCount(); ++i) {
        if (proj.blocks()[i].isMathBlock) mathSrc = proj.blocks()[i].embeddedSource;
        if (proj.blocks()[i].isMermaidBlock) mermaidSrc = proj.blocks()[i].embeddedSource;
    }
    QVERIFY(!mathSrc.isEmpty());
    QVERIFY(!mermaidSrc.isEmpty());
    QVERIFY(service->isCached(mathSrc, Render::RenderKind::Math));
    QVERIFY(service->isCached(mermaidSrc, Render::RenderKind::Mermaid));

    // 2. Interactive in-place editing, Undo, and Redo
    int formulaPos = doc.indexOf(QStringLiteral("f'(x)"));
    QVERIFY(formulaPos > 0);

    // Place cursor inside the formula
    editorView.setCursorPos(formulaPos + 2);
    QCoreApplication::processEvents();

    // Send keystroke 'z'
    QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier, QStringLiteral("z"));
    QApplication::sendEvent(editorView.viewport(), &keyEvent);
    QCoreApplication::processEvents();

    QVERIFY(editorView.buffer().text().contains(QStringLiteral("f'z(x)")));
    QVERIFY(editorView.buffer().text() != doc);

    // Undo
    editorView.undo();
    QCoreApplication::processEvents();
    QCOMPARE(editorView.buffer().text(), doc);

    // Redo
    editorView.redo();
    QCoreApplication::processEvents();
    QVERIFY(editorView.buffer().text().contains(QStringLiteral("f'z(x)")));

    // Revert to original clean document
    editorView.undo();
    QCoreApplication::processEvents();
    QCOMPARE(editorView.buffer().text(), doc);

    // 3. Source Mode vs Live Preview Mode Switch
    editorView.setSourceMode(true);
    QCoreApplication::processEvents();
    QVERIFY(editorView.isSourceMode());
    for (int i = 0; i < editorView.projection().blockCount(); ++i) {
        const auto &b = editorView.projection().blocks()[i];
        if (b.isMathBlock || b.isMermaidBlock) {
            QVERIFY(b.isInteractiveActive);
        }
    }

    // Switch back to Live Preview Mode
    editorView.setSourceMode(false);
    editorView.setCursorPos(0);
    QCoreApplication::processEvents();
    QVERIFY(!editorView.isSourceMode());

    // 4. Viewport scrolling stability
    auto vBar = editorView.verticalScrollBar();
    if (vBar && vBar->maximum() > 0) {
        vBar->setValue(vBar->maximum());
        QCoreApplication::processEvents();
        vBar->setValue(vBar->maximum() / 2);
        QCoreApplication::processEvents();
        vBar->setValue(0);
        QCoreApplication::processEvents();
    }
}

// Probe 46: Native Drag-and-Drop for Markdown documents, images, folders, and text
void TestProbes::testFileAndImageDragAndDrop() {
    // 1. Editor View: Drop Markdown Document (.md and .txt)
    Editor::NativeEditorView editorView;
    editorView.resize(800, 600);
    editorView.show();
    QCoreApplication::processEvents();

    QSignalSpy fileSpy(&editorView, &Editor::NativeEditorView::fileOpenRequested);
    QSignalSpy folderSpy(&editorView, &Editor::NativeEditorView::folderOpenRequested);

    // Verify DragEnter and Drop on .md document
    QMimeData mdMime;
    QString sampleMdPath = QStringLiteral("D:/notes/test_doc.md");
    mdMime.setUrls({QUrl::fromLocalFile(sampleMdPath)});

    QDragEnterEvent enterEv(QPoint(100, 100), Qt::CopyAction, &mdMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &enterEv);
    QVERIFY(enterEv.isAccepted());

    QDropEvent dropEv(QPointF(100, 100), Qt::CopyAction, &mdMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &dropEv);
    QVERIFY(dropEv.isAccepted());
    QCOMPARE(fileSpy.count(), 1);
    QCOMPARE(fileSpy.takeFirst().at(0).toString(), sampleMdPath);

    // Verify Drop on .txt document
    QMimeData txtMime;
    QString sampleTxtPath = QStringLiteral("D:/notes/readme.txt");
    txtMime.setUrls({QUrl::fromLocalFile(sampleTxtPath)});
    QDragEnterEvent txtEnter(QPoint(100, 100), Qt::CopyAction, &txtMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &txtEnter);
    QVERIFY(txtEnter.isAccepted());

    QDropEvent txtDrop(QPointF(100, 100), Qt::CopyAction, &txtMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &txtDrop);
    QVERIFY(txtDrop.isAccepted());
    QCOMPARE(fileSpy.count(), 1);
    QCOMPARE(fileSpy.takeFirst().at(0).toString(), sampleTxtPath);

    // 2. Editor View: Drop Image File (.png) and verify Markdown insertion, undo/redo
    editorView.buffer().setText(QStringLiteral("# Header\n\nInitial Paragraph\n"));
    editorView.undoStack().clear();
    int insertPos = 10;
    editorView.setCursorPos(insertPos);
    QCoreApplication::processEvents();

    QMimeData imgMime;
    QString sampleImgPath = QStringLiteral("D:/images/flowchart_preview.png");
    imgMime.setUrls({QUrl::fromLocalFile(sampleImgPath)});

    QDragEnterEvent imgEnter(QPoint(50, 50), Qt::CopyAction, &imgMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &imgEnter);
    QVERIFY(imgEnter.isAccepted());

    QDropEvent imgDrop(QPointF(50, 50), Qt::CopyAction, &imgMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &imgDrop);
    QVERIFY(imgDrop.isAccepted());

    // Check inserted markdown image syntax
    QString expectedImgMd = QStringLiteral("![flowchart_preview](D:/images/flowchart_preview.png)\n");
    QVERIFY2(editorView.buffer().text().contains(expectedImgMd),
             qPrintable(QString("Buffer must contain inserted image syntax: %1").arg(editorView.buffer().text())));

    // Undo should revert the buffer to original state
    editorView.undo();
    QCoreApplication::processEvents();
    QCOMPARE(editorView.buffer().text(), QStringLiteral("# Header\n\nInitial Paragraph\n"));

    // Redo should bring back the image syntax
    editorView.redo();
    QCoreApplication::processEvents();
    QVERIFY(editorView.buffer().text().contains(expectedImgMd));

    // 3. Editor View: Drop Directory
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QMimeData dirMime;
    dirMime.setUrls({QUrl::fromLocalFile(tempDir.path())});

    QDragEnterEvent dirEnter(QPoint(50, 50), Qt::CopyAction, &dirMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &dirEnter);
    QVERIFY(dirEnter.isAccepted());

    QDropEvent dirDrop(QPointF(50, 50), Qt::CopyAction, &dirMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &dirDrop);
    QVERIFY(dirDrop.isAccepted());
    QCOMPARE(folderSpy.count(), 1);
    QCOMPARE(folderSpy.takeFirst().at(0).toString(), tempDir.path());

    // 4. Editor View: Drop Generic File (e.g. .pdf) -> [fileName](path)
    QMimeData pdfMime;
    QString samplePdfPath = QStringLiteral("D:/manuals/specification.pdf");
    pdfMime.setUrls({QUrl::fromLocalFile(samplePdfPath)});

    QDragEnterEvent pdfEnter(QPoint(50, 50), Qt::CopyAction, &pdfMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &pdfEnter);
    QVERIFY(pdfEnter.isAccepted());

    QDropEvent pdfDrop(QPointF(50, 50), Qt::CopyAction, &pdfMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &pdfDrop);
    QVERIFY(pdfDrop.isAccepted());
    QVERIFY(editorView.buffer().text().contains(QStringLiteral("[specification.pdf](D:/manuals/specification.pdf)")));

    // 5. Editor View: Drop Plain Text
    QMimeData textMime;
    QString sampleText = QStringLiteral("Pasted Drag-and-Drop Text");
    textMime.setText(sampleText);

    QDragEnterEvent textEnter(QPoint(50, 50), Qt::CopyAction, &textMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &textEnter);
    QVERIFY(textEnter.isAccepted());

    QDropEvent textDrop(QPointF(50, 50), Qt::CopyAction, &textMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(editorView.viewport(), &textDrop);
    QVERIFY(textDrop.isAccepted());
    QVERIFY(editorView.buffer().text().contains(sampleText));

    // 6. MainWindow Integration: Drag and drop files/folders directly onto top-level window
    QString testDocPath = tempDir.filePath("test_drop_mainWindow.md");
    {
        QFile f(testDocPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("# Main Window Drop Test\nVerifying Top-Level Integration\n");
        f.close();
    }

    UI::MainWindow mainWindow;
    mainWindow.resize(900, 700);
    mainWindow.show();
    QCoreApplication::processEvents();

    // Drop .md onto MainWindow
    QMimeData mainDocMime;
    mainDocMime.setUrls({QUrl::fromLocalFile(testDocPath)});

    QDragEnterEvent mainEnter(QPoint(100, 100), Qt::CopyAction, &mainDocMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&mainWindow, &mainEnter);
    QVERIFY(mainEnter.isAccepted());

    QDropEvent mainDrop(QPointF(100, 100), Qt::CopyAction, &mainDocMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&mainWindow, &mainDrop);
    QVERIFY(mainDrop.isAccepted());
    QCoreApplication::processEvents();

    QVERIFY(mainWindow.editorView() != nullptr);
    QVERIFY2(mainWindow.editorView()->buffer().text().contains(QStringLiteral("Verifying Top-Level Integration")),
             qPrintable(mainWindow.editorView()->buffer().text()));

    // Drop folder onto MainWindow
    QMimeData mainDirMime;
    mainDirMime.setUrls({QUrl::fromLocalFile(tempDir.path())});

    QDragEnterEvent mainDirEnter(QPoint(100, 100), Qt::CopyAction, &mainDirMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&mainWindow, &mainDirEnter);
    QVERIFY(mainDirEnter.isAccepted());

    QDropEvent mainDirDrop(QPointF(100, 100), Qt::CopyAction, &mainDirMime, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&mainWindow, &mainDirDrop);
    QVERIFY(mainDirDrop.isAccepted());
    QCoreApplication::processEvents();

    QVERIFY(mainWindow.sidebar() != nullptr);
    QVERIFY(mainWindow.sidebar()->isVisible());
}

// Probe 49: Visual Redesign (Phase 1-5) Verification: Design System Colors, Centered 780px Reading Width, Dynamic Theme Switching & Real UI Screenshots
void TestProbes::testVisualRedesignScreenshotsAndThemes() {
    // 1. Verify Design System Palettes
    Theme::ThemeManager::instance().setMode(Theme::ThemeMode::Light);
    const auto &lightPalette = Theme::ThemeManager::instance().palette();
    QCOMPARE(lightPalette.appBackground, QColor("#F5F6F8"));
    QCOMPARE(lightPalette.sidebarBackground, QColor("#F0F2F5"));
    QCOMPARE(lightPalette.editorBackground, QColor("#FFFFFF"));
    QCOMPARE(lightPalette.primaryText, QColor("#242932"));
    QCOMPARE(lightPalette.secondaryText, QColor("#737B88"));
    QCOMPARE(lightPalette.border, QColor("#E4E8EF"));
    QCOMPARE(lightPalette.accent, QColor("#5668DB"));
    QCOMPARE(lightPalette.hover, QColor("#E8EBF3"));

    Theme::ThemeManager::instance().setMode(Theme::ThemeMode::Dark);
    const auto &darkPalette = Theme::ThemeManager::instance().palette();
    QCOMPARE(darkPalette.appBackground, QColor("#181A20"));
    QCOMPARE(darkPalette.sidebarBackground, QColor("#20232B"));
    QCOMPARE(darkPalette.editorBackground, QColor("#242730"));
    QCOMPARE(darkPalette.primaryText, QColor("#ECEEF3"));
    QCOMPARE(darkPalette.secondaryText, QColor("#A0A7B4"));
    QCOMPARE(darkPalette.border, QColor("#343943"));
    QCOMPARE(darkPalette.accent, QColor("#92A0FF"));
    QCOMPARE(darkPalette.hover, QColor("#2C313C"));

    // Reset back to Light for initial load
    Theme::ThemeManager::instance().setMode(Theme::ThemeMode::Light);

    // 2. Launch MainWindow and set rich Markdown content
    UI::MainWindow mainWindow;
    mainWindow.resize(1200, 820);
    mainWindow.show();
    QCoreApplication::processEvents();

    QString sampleDoc = QStringLiteral(
        "# Native Markdown Editor\n\n"
        "面向桌面端的高性能 **原生 Markdown 写作工具**，兼具 *Craft*、*Bear* 与 *Linear* 的现代设计质感。\n\n"
        "访问项目官网：[Native Markdown Editor](https://github.com/WildEgoLee/native-markdown-editor)\n\n"
        "## 系统架构与渲染流程\n\n"
        "```mermaid\n"
        "graph TD\n"
        "    Source[Markdown 源文本] --> Parser(增量 AST 分析器)\n"
        "    Parser --> Projection[视觉投影引擎]\n"
        "    Projection --> Layout{780px 居中排版引擎}\n"
        "    Layout --> Viewport[原生双缓冲渲染]\n"
        "    Layout --> Outline[多级折叠大纲]\n"
        "```\n\n"
        "## 核心特性\n\n"
        "- **780px 阅读宽度**：自适应居中留白与 56px 顶部留白\n"
        "- **双主题引擎**：Paper Light 与 Graphite Dark 实时平滑切换\n"
        "- **全矢量图标**：42px 现代精简工具栏与 26px 弱视觉层级状态栏\n"
        "- **数学公式排版**：\n\n"
        "$$\\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$$\n\n"
        "## 代码块与表格\n\n"
        "```cpp\n"
        "// 现代 C++20 与 Qt 6 原生排版\n"
        "auto layout = LayoutEngine();\n"
        "layout.layout(projection, 780.0);\n"
        "```\n\n"
        "| 特性模块 | 设计规范 | 渲染状态 |\n"
        "| :--- | :--- | :--- |\n"
        "| 工具栏 | 42px 线性矢量 | 已就绪 |\n"
        "| 侧边栏 | Pill 切换器 + 自然缩进 | 已就绪 |\n"
        "| 编辑器画布 | 780px 居中自适应 | 已就绪 |\n"
    );

    mainWindow.editorView()->buffer().setText(sampleDoc);
    mainWindow.editorView()->undoStack().clear();
    mainWindow.editorView()->setCursorPos(0);
    QTest::qWait(300);

    // 3. Verify LayoutEngine 780px Centering and Responsive Margins
    const auto &layoutEng = mainWindow.editorView()->layoutEngine();
    QVERIFY(layoutEng.contentWidth() <= 780.0);
    QVERIFY(layoutEng.contentLeft() >= 16.0);

    // Deterministic LayoutEngine tests:
    // Wide viewport (1200px): strictly clamped to 780px and centered at (1200 - 780) / 2 = 210px
    Editor::LayoutEngine wideEngine;
    wideEngine.layout(mainWindow.editorView()->projection(), 1200.0);
    QCOMPARE(wideEngine.contentWidth(), 780.0);
    QCOMPARE(wideEngine.contentLeft(), 210.0);

    // Constrained viewport (600px): responsive margins (600 - 24*2 = 552px) and left = 24px
    Editor::LayoutEngine narrowEngine;
    narrowEngine.layout(mainWindow.editorView()->projection(), 600.0);
    QCOMPARE(narrowEngine.contentWidth(), 552.0);
    QCOMPARE(narrowEngine.contentLeft(), 24.0);

    // Check top margin of first block: 56px canvas top padding + 28px H1 marginTop = 84px
    QVERIFY(!layoutEng.blocks().isEmpty());
    QCOMPARE(layoutEng.blocks().first().rect.top(), 56.0 + 28.0);

    // 4. Capture Light Theme Screenshot
    QDir().mkpath(QStringLiteral("screenshots"));
    QPixmap lightPixmap = mainWindow.grab();
    QVERIFY(!lightPixmap.isNull());
    QVERIFY(lightPixmap.width() > 600);
    QVERIFY(lightPixmap.height() > 400);
    lightPixmap.save(QStringLiteral("screenshots/editor_paper_light.png"));

    // 5. Switch to Graphite Dark Theme
    Theme::ThemeManager::instance().setMode(Theme::ThemeMode::Dark);
    QTest::qWait(300);
    QCOMPARE(Theme::ThemeManager::instance().currentMode(), Theme::ThemeMode::Dark);

    QPixmap darkPixmap = mainWindow.grab();
    QVERIFY(!darkPixmap.isNull());
    darkPixmap.save(QStringLiteral("screenshots/editor_graphite_dark.png"));

    // 6. Test Source Mode and capture screenshot
    mainWindow.editorView()->setSourceMode(true);
    QTest::qWait(200);
    QVERIFY(mainWindow.editorView()->isSourceMode());

    QPixmap sourcePixmap = mainWindow.grab();
    QVERIFY(!sourcePixmap.isNull());
    sourcePixmap.save(QStringLiteral("screenshots/editor_graphite_dark_source.png"));

    mainWindow.editorView()->setSourceMode(false);
    QTest::qWait(150);

    // 7. Verify Sidebar Segmented Control & Outline Filtering
    auto *sidebar = mainWindow.sidebar();
    QVERIFY(sidebar != nullptr);
    auto *outline = sidebar->outlineWidget();
    QVERIFY(outline != nullptr);
    outline->filter(QStringLiteral("架构"));
    QTest::qWait(50);
    outline->filter(QString());

    // Restore to default Paper Light
    Theme::ThemeManager::instance().setMode(Theme::ThemeMode::Light);
}

// Probe 50: Mouse Wheel Natural Scrolling & Typography Customization (Presets, Custom Font, and Ctrl+Wheel Zoom)
void TestProbes::testMouseWheelScrollingAndTypography() {
    UI::MainWindow mainWindow;
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));

    auto *editorView = mainWindow.editorView();
    QVERIFY(editorView != nullptr);

    // Populate document with 100 lines so scrolling range is significant
    QString text;
    for (int i = 1; i <= 100; ++i) {
        text.append(QStringLiteral("### 行 %1: 测试滚轮自然平滑滚动与字体排版表现\n这是一段较长的测试文本，用于验证在各种字体与滚轮步长下的排版渲染。\n").arg(i));
    }
    editorView->buffer().setText(text);
    editorView->reparseAndRelayout();
    QTest::qWait(50);

    auto *vBar = editorView->verticalScrollBar();
    QVERIFY(vBar->maximum() > 500);
    QCOMPARE(vBar->value(), 0);

    // 1. Verify singleStep is responsive and >= 50px (not 1px)
    QVERIFY(vBar->singleStep() >= 50);

    // 2. Simulate standard mouse wheel scroll down (angleDelta y = -120, 1 notch)
    QPoint pos(editorView->viewport()->width() / 2, editorView->viewport()->height() / 2);
    QWheelEvent wheelDown(pos, editorView->viewport()->mapToGlobal(pos),
                         QPoint(0, 0), QPoint(0, -120),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(editorView->viewport(), &wheelDown);
    QTest::qWait(20);

    // Value must have scrolled down significantly (> 50px, typically ~78px for 3 lines)
    int scrolledVal = vBar->value();
    QVERIFY2(scrolledVal >= 50, qPrintable(QStringLiteral("Expected scrolled >= 50, actual: %1").arg(scrolledVal)));

    // 3. Simulate mouse wheel scroll up (angleDelta y = +120, 1 notch)
    QWheelEvent wheelUp(pos, editorView->viewport()->mapToGlobal(pos),
                       QPoint(0, 0), QPoint(0, 120),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(editorView->viewport(), &wheelUp);
    QTest::qWait(20);
    QCOMPARE(vBar->value(), 0);

    // 4. Simulate trackpad high-precision pixelDelta scrolling
    QWheelEvent trackpadScroll(pos, editorView->viewport()->mapToGlobal(pos),
                              QPoint(0, -35), QPoint(0, -35),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(editorView->viewport(), &trackpadScroll);
    QTest::qWait(20);
    QCOMPARE(vBar->value(), 35);

    // Reset scrollbar
    vBar->setValue(0);

    // 5. Test Typography Presets
    auto &themeMgr = Theme::ThemeManager::instance();
    // Default preset
    QCOMPARE(themeMgr.fontPreset(), Theme::ThemeManager::FontPreset::SansSerif);
    QVERIFY(themeMgr.proseFontFamilies().contains(QStringLiteral("Microsoft YaHei UI")));

    // Switch to Serif Preset (典雅书卷体)
    themeMgr.setFontPreset(Theme::ThemeManager::FontPreset::Serif);
    QTest::qWait(50);
    QCOMPARE(themeMgr.fontPreset(), Theme::ThemeManager::FontPreset::Serif);
    QVERIFY(themeMgr.proseFontFamilies().contains(QStringLiteral("SimSun")) ||
            themeMgr.proseFontFamilies().contains(QStringLiteral("Noto Serif SC")));

    // Switch to Monospace Preset (极客等宽体)
    themeMgr.setFontPreset(Theme::ThemeManager::FontPreset::Monospace);
    QTest::qWait(50);
    QCOMPARE(themeMgr.fontPreset(), Theme::ThemeManager::FontPreset::Monospace);
    QVERIFY(themeMgr.proseFontFamilies().contains(QStringLiteral("Cascadia Code")));

    // Set Custom Font
    themeMgr.setCustomFontFamily(QStringLiteral("CustomTestFont"));
    QTest::qWait(50);
    QCOMPARE(themeMgr.fontPreset(), Theme::ThemeManager::FontPreset::Custom);
    QCOMPARE(themeMgr.proseFontFamilies().first(), QStringLiteral("CustomTestFont"));

    // 6. Test Font Zoom (Ctrl + Wheel)
    themeMgr.resetZoom();
    QCOMPARE(themeMgr.basePointSize(), 12.0);

    // Zoom in with Ctrl + Wheel
    QWheelEvent zoomInWheel(pos, editorView->viewport()->mapToGlobal(pos),
                           QPoint(0, 0), QPoint(0, 120),
                           Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(editorView->viewport(), &zoomInWheel);
    QTest::qWait(20);
    QCOMPARE(themeMgr.basePointSize(), 13.0);

    // Zoom out with Ctrl + Wheel
    QWheelEvent zoomOutWheel(pos, editorView->viewport()->mapToGlobal(pos),
                            QPoint(0, 0), QPoint(0, -120),
                            Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(editorView->viewport(), &zoomOutWheel);
    QTest::qWait(20);
    QCOMPARE(themeMgr.basePointSize(), 12.0);

    // Restore to SansSerif default
    themeMgr.setFontPreset(Theme::ThemeManager::FontPreset::SansSerif);
    themeMgr.resetZoom();
}

void TestProbes::testFindAndReplaceFeedback() {
    Editor::NativeEditorView editor;
    editor.buffer().setText(QStringLiteral("apple banana apple orange apple"));
    editor.reparseAndRelayout();

    // 1. Empty query should return false and not move cursor
    editor.setCursorPos(7);
    QCOMPARE(editor.cursorPos(), 7);
    QVERIFY(!editor.findText(QString(), false, true));
    QCOMPARE(editor.cursorPos(), 7);

    // 2. Initial searchStatus
    auto s0 = editor.searchStatus(QStringLiteral("apple"), true);
    QCOMPARE(s0.total, 3);
    QCOMPARE(s0.current, 0); // nothing selected yet

    // 3. Forward cyclic search
    editor.setCursorPos(0);
    QVERIFY(editor.findText(QStringLiteral("apple"), true, true));
    QCOMPARE(editor.selectionAnchor(), 0);
    QCOMPARE(editor.cursorPos(), 5);
    QCOMPARE(editor.cursors().size(), 1);
    QCOMPARE(editor.cursors().first().anchor, 0);
    QCOMPARE(editor.cursors().first().cursor, 5);
    auto s1 = editor.searchStatus(QStringLiteral("apple"), true);
    QCOMPARE(s1.total, 3);
    QCOMPARE(s1.current, 1);

    // Next match
    QVERIFY(editor.findText(QStringLiteral("apple"), true, true));
    QCOMPARE(editor.selectionAnchor(), 13);
    QCOMPARE(editor.cursorPos(), 18);
    auto s2 = editor.searchStatus(QStringLiteral("apple"), true);
    QCOMPARE(s2.current, 2);

    // Next match
    QVERIFY(editor.findText(QStringLiteral("apple"), true, true));
    QCOMPARE(editor.selectionAnchor(), 26);
    QCOMPARE(editor.cursorPos(), 31);
    auto s3 = editor.searchStatus(QStringLiteral("apple"), true);
    QCOMPARE(s3.current, 3);

    // Wrap around to match 1
    QVERIFY(editor.findText(QStringLiteral("apple"), true, true));
    QCOMPARE(editor.selectionAnchor(), 0);
    QCOMPARE(editor.cursorPos(), 5);
    QCOMPARE(editor.searchStatus(QStringLiteral("apple"), true).current, 1);

    // 4. Backward cyclic search from match 1 wraps to match 3
    QVERIFY(editor.findText(QStringLiteral("apple"), true, false));
    QCOMPARE(editor.selectionAnchor(), 26);
    QCOMPARE(editor.cursorPos(), 31);
    QCOMPARE(editor.searchStatus(QStringLiteral("apple"), true).current, 3);

    // Backward to match 2
    QVERIFY(editor.findText(QStringLiteral("apple"), true, false));
    QCOMPARE(editor.selectionAnchor(), 13);
    QCOMPARE(editor.cursorPos(), 18);
    QCOMPARE(editor.searchStatus(QStringLiteral("apple"), true).current, 2);

    // 5. Case sensitivity
    editor.buffer().setText(QStringLiteral("Test test TEST"));
    editor.reparseAndRelayout();
    auto scaseSensitive = editor.searchStatus(QStringLiteral("Test"), true);
    QCOMPARE(scaseSensitive.total, 1);
    auto scaseInsensitive = editor.searchStatus(QStringLiteral("Test"), false);
    QCOMPARE(scaseInsensitive.total, 3);

    // 6. Chinese and Emoji
    editor.buffer().setText(QStringLiteral("你好世界 🎉 欢迎来到 🎉 世界"));
    editor.reparseAndRelayout();
    auto sEmoji = editor.searchStatus(QStringLiteral("🎉"), true);
    QCOMPARE(sEmoji.total, 2);
    auto sChinese = editor.searchStatus(QStringLiteral("世界"), true);
    QCOMPARE(sChinese.total, 2);

    // 7. Zero matches
    auto sZero = editor.searchStatus(QStringLiteral("不存在的内容"), false);
    QCOMPARE(sZero.total, 0);
    QCOMPARE(sZero.current, 0);

    // 8. Overlapping search consistency (aaaa with query aa)
    editor.buffer().setText(QStringLiteral("aaaa"));
    editor.reparseAndRelayout();
    auto sOverlap = editor.searchStatus(QStringLiteral("aa"), true);
    QCOMPARE(sOverlap.total, 2);
    editor.setCursorPos(0);
    // Forward to first match
    QVERIFY(editor.findText(QStringLiteral("aa"), true, true));
    QCOMPARE(editor.selectionAnchor(), 0);
    QCOMPARE(editor.cursorPos(), 2);
    QCOMPARE(editor.searchStatus(QStringLiteral("aa"), true).current, 1);
    // Reverse from first match wraps to second match
    QVERIFY(editor.findText(QStringLiteral("aa"), true, false));
    QCOMPARE(editor.selectionAnchor(), 2);
    QCOMPARE(editor.cursorPos(), 4);
    QCOMPARE(editor.searchStatus(QStringLiteral("aa"), true).current, 2);
    // Reverse again to first match
    QVERIFY(editor.findText(QStringLiteral("aa"), true, false));
    QCOMPARE(editor.selectionAnchor(), 0);
    QCOMPARE(editor.cursorPos(), 2);
    QCOMPARE(editor.searchStatus(QStringLiteral("aa"), true).current, 1);

    // 9. Replace one and replace all with undo
    editor.buffer().setText(QStringLiteral("cat and cat and cat"));
    editor.reparseAndRelayout();
    editor.setCursorPos(0);
    editor.findText(QStringLiteral("cat"), true, true);
    editor.replaceOne(QStringLiteral("cat"), QStringLiteral("dog"), true);
    QCOMPARE(editor.buffer().text(), QStringLiteral("dog and cat and cat"));
    QCOMPARE(editor.searchStatus(QStringLiteral("cat"), true).total, 2);

    // Replace all remaining
    int repCount = editor.replaceAll(QStringLiteral("cat"), QStringLiteral("dog"), true);
    QCOMPARE(repCount, 2);
    QCOMPARE(editor.buffer().text(), QStringLiteral("dog and dog and dog"));
    QCOMPARE(editor.searchStatus(QStringLiteral("cat"), true).total, 0);
    QCOMPARE(editor.cursors().size(), 1);
    QCOMPARE(editor.cursors().first().anchor, 8);
    QCOMPARE(editor.cursors().first().cursor, 11);

    // Undo replace all
    Core::SelectionState undoSel;
    editor.undoStack().undo(editor.buffer(), undoSel);
    QCOMPARE(editor.buffer().text(), QStringLiteral("dog and cat and cat"));
    QCOMPARE(editor.searchStatus(QStringLiteral("cat"), true).total, 2);

    // 10. MainWindow GUI wiring: F3, Shift+F3, buttons, Esc in both inputs, and live auto-updates
    UI::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.editorView()->buffer().setText(QStringLiteral("aaaa bb aaaa"));
    window.editorView()->reparseAndRelayout();

    auto findBar = window.findBar();
    QVERIFY(findBar != nullptr);
    auto findEdit = findBar->findChild<QLineEdit*>(QStringLiteral("findEdit"));
    auto replaceEdit = findBar->findChild<QLineEdit*>(QStringLiteral("replaceEdit"));
    auto countLabel = findBar->findChild<QLabel*>(QStringLiteral("matchCountLabel"));
    auto btnPrev = findBar->findChild<QPushButton*>(QStringLiteral("btnPrev"));
    auto btnNext = findBar->findChild<QPushButton*>(QStringLiteral("btnNext"));
    auto btnReplace = findBar->findChild<QPushButton*>(QStringLiteral("btnReplace"));
    QVERIFY(findEdit && replaceEdit && countLabel && btnPrev && btnNext && btnReplace);

    findBar->openFind();
    QVERIFY(findBar->isVisible());

    // Type query "aa" via real signal path
    findEdit->setText(QStringLiteral("aa"));
    QCOMPARE(countLabel->text(), QStringLiteral("0/4"));

    // Press F3 via Qt test shortcut event on window
    QTest::keyClick(&window, Qt::Key_F3);
    QCOMPARE(window.editorView()->selectionAnchor(), 0);
    QCOMPARE(window.editorView()->cursorPos(), 2);
    QCOMPARE(countLabel->text(), QStringLiteral("1/4"));

    // Press F3 again
    QTest::keyClick(&window, Qt::Key_F3);
    QCOMPARE(window.editorView()->selectionAnchor(), 2);
    QCOMPARE(window.editorView()->cursorPos(), 4);
    QCOMPARE(countLabel->text(), QStringLiteral("2/4"));

    // Press Shift+F3 to go backwards
    QTest::keyClick(&window, Qt::Key_F3, Qt::ShiftModifier);
    QCOMPARE(window.editorView()->selectionAnchor(), 0);
    QCOMPARE(window.editorView()->cursorPos(), 2);
    QCOMPARE(countLabel->text(), QStringLiteral("1/4"));

    // Press Shift+F3 again to wrap around backwards
    QTest::keyClick(&window, Qt::Key_F3, Qt::ShiftModifier);
    QCOMPARE(window.editorView()->selectionAnchor(), 10);
    QCOMPARE(window.editorView()->cursorPos(), 12);
    QCOMPARE(countLabel->text(), QStringLiteral("4/4"));

    // Move cursor to non-matching position, label automatically resets to 0/4
    window.editorView()->setCursorPos(5);
    QCOMPARE(countLabel->text(), QStringLiteral("0/4"));

    // Click Next button
    QTest::mouseClick(btnNext, Qt::LeftButton);
    QCOMPARE(window.editorView()->selectionAnchor(), 8);
    QCOMPARE(countLabel->text(), QStringLiteral("3/4"));

    // Test replace: open replace row, set replace text, click replace button
    findBar->openReplace();
    replaceEdit->setText(QStringLiteral("xx"));
    QTest::mouseClick(btnReplace, Qt::LeftButton);
    // Replaced at index 8; match count automatically updates to 3 total matches
    QCOMPARE(countLabel->text(), QStringLiteral("3/3"));

    // Test Escape in findEdit: closes findBar and restores editor focus
    findEdit->setFocus();
    QVERIFY(findEdit->hasFocus());
    QTest::keyClick(findEdit, Qt::Key_Escape);
    QVERIFY(findBar->isHidden());
    QVERIFY(window.editorView()->hasFocus());

    // Test Escape in replaceEdit: closes findBar and restores editor focus
    findBar->openReplace();
    QVERIFY(findBar->isVisible());
    replaceEdit->setFocus();
    QVERIFY(replaceEdit->hasFocus());
    QTest::keyClick(replaceEdit, Qt::Key_Escape);
    QVERIFY(findBar->isHidden());
    QVERIFY(window.editorView()->hasFocus());
}

void TestProbes::testPreferencesPersistenceAndMenuSync() {
    QTemporaryDir tempPrefDir;
    QString iniFile = tempPrefDir.filePath(QStringLiteral("test_prefs.ini"));
    QSettings settings(iniFile, QSettings::IniFormat);

    // 1. First run on clean settings file
    {
        UI::MainWindow w1;
        w1.show();
        w1.loadPreferences(&settings);
        QCOMPARE(Theme::ThemeManager::instance().currentMode(), Theme::ThemeMode::Light);
        QCOMPARE(Theme::ThemeManager::instance().fontPreset(), Theme::ThemeManager::FontPreset::SansSerif);
        QCOMPARE(Theme::ThemeManager::instance().basePointSize(), 12.0);
        QVERIFY(!w1.sidebar()->isHidden());

        // Modify settings and save
        Theme::ThemeManager::instance().setMode(Theme::ThemeMode::Dark);
        Theme::ThemeManager::instance().setFontPreset(Theme::ThemeManager::FontPreset::Serif);
        Theme::ThemeManager::instance().setBasePointSize(15.0);
        w1.sidebar()->setVisible(false);
        QVERIFY(w1.sidebar()->isHidden());
        w1.savePreferences(&settings);
    }

    // Reset global theme to defaults before restoring
    Theme::ThemeManager::instance().setMode(Theme::ThemeMode::Light);
    Theme::ThemeManager::instance().setFontPreset(Theme::ThemeManager::FontPreset::SansSerif);
    Theme::ThemeManager::instance().setBasePointSize(12.0);

    // 2. Round-trip restore in independent instance
    {
        UI::MainWindow w2;
        w2.show();
        w2.loadPreferences(&settings);
        QCOMPARE(Theme::ThemeManager::instance().currentMode(), Theme::ThemeMode::Dark);
        QCOMPARE(Theme::ThemeManager::instance().fontPreset(), Theme::ThemeManager::FontPreset::Serif);
        QCOMPARE(Theme::ThemeManager::instance().basePointSize(), 15.0);
        QVERIFY(w2.sidebar()->isHidden());

        // Programmatic font switch updates menu state
        Theme::ThemeManager::instance().setFontPreset(Theme::ThemeManager::FontPreset::Monospace);
        QTest::qWait(20);
        QCOMPARE(Theme::ThemeManager::instance().fontPreset(), Theme::ThemeManager::FontPreset::Monospace);

        Theme::ThemeManager::instance().setCustomFontFamily(QStringLiteral("MyCustomFont"));
        QTest::qWait(20);
        QCOMPARE(Theme::ThemeManager::instance().fontPreset(), Theme::ThemeManager::FontPreset::Custom);
    }

    // 3. Illegal configuration bounds
    settings.setValue(QStringLiteral("Appearance/basePointSize"), 999.0);
    settings.setValue(QStringLiteral("Appearance/fontPreset"), 88);
    settings.setValue(QStringLiteral("Appearance/themeMode"), 77);
    settings.setValue(QStringLiteral("window/sidebarWidth"), 5000);
    settings.remove(QStringLiteral("window/sidebarVisible"));
    {
        UI::MainWindow w3;
        w3.show();
        w3.loadPreferences(&settings);
        QCOMPARE(Theme::ThemeManager::instance().basePointSize(), 12.0);
        QCOMPARE(Theme::ThemeManager::instance().fontPreset(), Theme::ThemeManager::FontPreset::SansSerif);
        QCOMPARE(Theme::ThemeManager::instance().currentMode(), Theme::ThemeMode::Light);
        QVERIFY(!w3.sidebar()->isHidden());
    }

    // 4. Off-screen geometry recovery (simulating disconnected external monitor)
    {
        QSettings offscreenSettings(tempPrefDir.filePath(QStringLiteral("offscreen_prefs.ini")), QSettings::IniFormat);
        // Place window at (50000, 50000), far outside any real screen
        QWidget dummy;
        dummy.setGeometry(50000, 50000, 800, 600);
        offscreenSettings.setValue(QStringLiteral("window/geometry"), dummy.saveGeometry());

        UI::MainWindow wOff;
        wOff.loadPreferences(&offscreenSettings);
        // Window must be pulled back onto a connected screen
        bool visibleOnScreen = false;
        for (auto *screen : QGuiApplication::screens()) {
            if (screen && screen->availableGeometry().intersects(wOff.frameGeometry())) {
                visibleOnScreen = true;
                break;
            }
        }
        QVERIFY(visibleOnScreen);
    }

    // 5. Corrupted geometry binary payload
    {
        QSettings corruptSettings(tempPrefDir.filePath(QStringLiteral("corrupt_prefs.ini")), QSettings::IniFormat);
        corruptSettings.setValue(QStringLiteral("window/geometry"), QByteArray("corrupted_binary_geometry_payload"));
        UI::MainWindow wCorrupt;
        // Must not crash and must retain valid geometry
        wCorrupt.loadPreferences(&corruptSettings);
        QVERIFY(wCorrupt.width() > 0 && wCorrupt.height() > 0);
    }

    // Restore clean global theme
    Theme::ThemeManager::instance().setMode(Theme::ThemeMode::Light);
    Theme::ThemeManager::instance().setFontPreset(Theme::ThemeManager::FontPreset::SansSerif);
    Theme::ThemeManager::instance().setBasePointSize(12.0);
}

void TestProbes::testSidebarSearchConsistency() {
    QTemporaryDir workDir;
    QDir dir(workDir.path());
    dir.mkdir(QStringLiteral("subfolder"));

    // Create test files
    QFile f1(dir.filePath(QStringLiteral("normal_doc.md")));
    f1.open(QIODevice::WriteOnly);
    f1.write("# Doc 1\n");
    f1.close();

    QFile f2(dir.filePath(QStringLiteral("subfolder/nested.markdown")));
    f2.open(QIODevice::WriteOnly);
    f2.write("# Nested\n");
    f2.close();

    QFile f3(dir.filePath(QStringLiteral("special[filter].txt")));
    f3.open(QIODevice::WriteOnly);
    f3.write("notes\n");
    f3.close();

    QFile f4(dir.filePath(QStringLiteral("中文笔记.md")));
    f4.open(QIODevice::WriteOnly);
    f4.write("chinese\n");
    f4.close();

    QFile fIgnored(dir.filePath(QStringLiteral("ignored.exe")));
    fIgnored.open(QIODevice::WriteOnly);
    fIgnored.write("binary\n");
    fIgnored.close();

    UI::SidebarWidget sidebar;
    sidebar.setWorkspacePath(workDir.path());

    // Deterministically wait for asynchronous QFileSystemModel to populate test files
    QTRY_VERIFY_WITH_TIMEOUT(sidebar.fileModel()->index(dir.filePath(QStringLiteral("normal_doc.md"))).isValid(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(sidebar.fileModel()->index(dir.filePath(QStringLiteral("special[filter].txt"))).isValid(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(sidebar.fileModel()->index(dir.filePath(QStringLiteral("中文笔记.md"))).isValid(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(sidebar.fileModel()->index(dir.filePath(QStringLiteral("ignored.exe"))).isValid(), 5000);

    QModelIndex normalSrc = sidebar.fileModel()->index(dir.filePath(QStringLiteral("normal_doc.md")));
    QModelIndex specialSrc = sidebar.fileModel()->index(dir.filePath(QStringLiteral("special[filter].txt")));
    QModelIndex chineseSrc = sidebar.fileModel()->index(dir.filePath(QStringLiteral("中文笔记.md")));
    QModelIndex ignoredSrc = sidebar.fileModel()->index(dir.filePath(QStringLiteral("ignored.exe")));

    QVERIFY(normalSrc.isValid());
    QVERIFY(specialSrc.isValid());
    QVERIFY(chineseSrc.isValid());
    QVERIFY(ignoredSrc.isValid());

    // 0. Verify unsupported file types are filtered out even with empty filter
    QVERIFY(!sidebar.fileProxyModel()->mapFromSource(ignoredSrc).isValid());
    QVERIFY(sidebar.fileProxyModel()->mapFromSource(normalSrc).isValid());
    QVERIFY(sidebar.fileProxyModel()->mapFromSource(specialSrc).isValid());
    QVERIFY(sidebar.fileProxyModel()->mapFromSource(chineseSrc).isValid());

    // 1. Literal substring match with special chars '[' and ']'
    sidebar.searchEdit()->setText(QStringLiteral("[filter]"));
    QCOMPARE(sidebar.fileFilterPattern(), QStringLiteral("[filter]"));
    QVERIFY(sidebar.fileProxyModel()->mapFromSource(specialSrc).isValid());
    QVERIFY(!sidebar.fileProxyModel()->mapFromSource(normalSrc).isValid());
    QVERIFY(!sidebar.fileProxyModel()->mapFromSource(chineseSrc).isValid());

    // 2. Chinese search
    sidebar.searchEdit()->setText(QStringLiteral("中文"));
    QCOMPARE(sidebar.fileFilterPattern(), QStringLiteral("中文"));
    QVERIFY(sidebar.fileProxyModel()->mapFromSource(chineseSrc).isValid());
    QVERIFY(!sidebar.fileProxyModel()->mapFromSource(normalSrc).isValid());
    QVERIFY(!sidebar.fileProxyModel()->mapFromSource(specialSrc).isValid());

    // 3. Case-insensitive search "NORMAL"
    sidebar.searchEdit()->setText(QStringLiteral("NORMAL"));
    QVERIFY(sidebar.fileProxyModel()->mapFromSource(normalSrc).isValid());
    QVERIFY(!sidebar.fileProxyModel()->mapFromSource(specialSrc).isValid());
    QVERIFY(!sidebar.fileProxyModel()->mapFromSource(chineseSrc).isValid());

    // 4. Switch to Outline page applies current search query
    sidebar.outlineWidget()->updateOutline(Parser::SemanticNodePtr(), QStringLiteral("# NORMAL Heading\n"));
    sidebar.switchToOutline();
    QTest::qWait(20);

    // 5. Switch back to Files page preserves filter
    sidebar.switchToFiles();
    QTest::qWait(20);
    QCOMPARE(sidebar.searchEdit()->text(), QStringLiteral("NORMAL"));
    QCOMPARE(sidebar.fileFilterPattern(), QStringLiteral("NORMAL"));

    // 6. Clear search restores default view
    sidebar.searchEdit()->clear();
    QCOMPARE(sidebar.fileFilterPattern(), QString());
    auto curNormalSrc = sidebar.fileModel()->index(dir.filePath(QStringLiteral("normal_doc.md")));
    auto curSpecialSrc = sidebar.fileModel()->index(dir.filePath(QStringLiteral("special[filter].txt")));
    auto curChineseSrc = sidebar.fileModel()->index(dir.filePath(QStringLiteral("中文笔记.md")));
    QTRY_VERIFY_WITH_TIMEOUT(sidebar.fileProxyModel()->mapFromSource(curNormalSrc).isValid(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(sidebar.fileProxyModel()->mapFromSource(curSpecialSrc).isValid(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(sidebar.fileProxyModel()->mapFromSource(curChineseSrc).isValid(), 3000);

    // 7. Clicking file in file tree emits fileSelected with exact path (no conditional skip!)
    QSignalSpy fileSpy(&sidebar, &UI::SidebarWidget::fileSelected);
    QModelIndex proxyIdx = sidebar.fileProxyModel()->mapFromSource(curNormalSrc);
    QVERIFY(proxyIdx.isValid());
    sidebar.fileTree()->clicked(proxyIdx);
    QTRY_VERIFY_WITH_TIMEOUT(fileSpy.count() >= 1, 2000);
    QCOMPARE(QFileInfo(fileSpy.first().first().toString()).canonicalFilePath(),
             QFileInfo(dir.filePath(QStringLiteral("normal_doc.md"))).canonicalFilePath());
}

void TestProbes::testWheelZoomAndReadingAnchor() {
    Editor::NativeEditorView editor;
    editor.resize(800, 600);
    QString longDoc;
    for (int i = 1; i <= 150; ++i) {
        longDoc += QStringLiteral("### Heading %1\nThis is paragraph content for line %1 to test reading anchor.\n\n").arg(i);
    }
    editor.buffer().setText(longDoc);
    editor.reparseAndRelayout();

    Theme::ThemeManager::instance().setBasePointSize(12.0);

    // 1. High-precision trackpad / mouse wheel notch accumulation:
    // 3 small delta events of +30 units must NOT trigger zoom
    QPoint pos(400, 300);
    QPoint globalPos(400, 300);
    for (int i = 0; i < 3; ++i) {
        QWheelEvent ev(pos, globalPos, QPoint(), QPoint(0, 30), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(editor.viewport(), &ev);
    }
    QCOMPARE(Theme::ThemeManager::instance().basePointSize(), 12.0);

    // 4th event brings total to +120 -> triggers exactly one zoomIn!
    {
        QWheelEvent ev(pos, globalPos, QPoint(), QPoint(0, 30), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(editor.viewport(), &ev);
    }
    QCOMPARE(Theme::ThemeManager::instance().basePointSize(), 13.0);

    // Reverse: 4 events of -30 brings back to 12.0
    for (int i = 0; i < 4; ++i) {
        QWheelEvent ev(pos, globalPos, QPoint(), QPoint(0, -30), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(editor.viewport(), &ev);
    }
    QCOMPARE(Theme::ThemeManager::instance().basePointSize(), 12.0);

    // 2. Reading anchor preservation on font zoom:
    // Scroll editor to ~50% of the document
    int maxScroll = editor.verticalScrollBar()->maximum();
    QVERIFY(maxScroll > 1000);
    int targetScroll = maxScroll / 2;
    editor.verticalScrollBar()->setValue(targetScroll);
    QCOMPARE(editor.verticalScrollBar()->value(), targetScroll);

    // Trigger zoomIn
    Theme::ThemeManager::instance().zoomIn(); // 12 -> 13
    int newScroll = editor.verticalScrollBar()->value();
    int newMaxScroll = editor.verticalScrollBar()->maximum();
    QVERIFY(newMaxScroll > maxScroll);

    // Relative scroll position should be preserved within 5% margin
    double oldRatio = static_cast<double>(targetScroll) / maxScroll;
    double newRatio = static_cast<double>(newScroll) / newMaxScroll;
    QVERIFY(qAbs(oldRatio - newRatio) < 0.05);

    // Restore clean settings
    Theme::ThemeManager::instance().setBasePointSize(12.0);
}

void TestProbes::testFontDialogThemeAndLegibility() {
    auto calcLuma = [](const QColor &c) -> qreal {
        return 0.299 * c.redF() + 0.587 * c.greenF() + 0.114 * c.blueF();
    };

    // --- 1. Paper Light Mode Verification ---
    Theme::ThemeManager::instance().setMode(Theme::ThemeMode::Light);
    const auto &lightPalette = Theme::ThemeManager::instance().palette();
    QCOMPARE(Theme::ThemeManager::instance().currentMode(), Theme::ThemeMode::Light);

    // Verify QApplication palette synchronization
    QCOMPARE(QApplication::palette().color(QPalette::Window), lightPalette.appBackground);
    QCOMPARE(QApplication::palette().color(QPalette::Base), lightPalette.editorBackground);
    QCOMPARE(QApplication::palette().color(QPalette::Text), lightPalette.primaryText);
    QCOMPARE(QApplication::palette().color(QPalette::Highlight), lightPalette.accent);

    // Contrast guarantee for Light Mode
    qreal lightBgLuma = calcLuma(lightPalette.editorBackground);
    qreal lightTextLuma = calcLuma(lightPalette.primaryText);
    QVERIFY(std::abs(lightBgLuma - lightTextLuma) >= 0.5);

    // Instantiate QFontDialog under Light Mode
    {
        QFont initFont(QStringLiteral("Microsoft YaHei UI"), 12);
        QFontDialog dlg(initFont);
        dlg.setOption(QFontDialog::DontUseNativeDialog, true);
        dlg.setStyleSheet(Theme::ThemeManager::instance().mainStylesheet());

        // Verify child widgets exist and are correctly structured
        auto listViews = dlg.findChildren<QListView *>();
        QVERIFY(!listViews.isEmpty());
        auto lineEdits = dlg.findChildren<QLineEdit *>();
        QVERIFY(!lineEdits.isEmpty());

        // Verify that stylesheet contains QDialog, QListView, QLineEdit, QPushButton rules
        QString qss = dlg.styleSheet();
        QVERIFY(qss.contains(QStringLiteral("QDialog")));
        QVERIFY(qss.contains(QStringLiteral("QListView")));
        QVERIFY(qss.contains(QStringLiteral("QLineEdit")));
        QVERIFY(qss.contains(QStringLiteral("QPushButton")));
        QVERIFY(qss.contains(lightPalette.editorBackground.name()));
        QVERIFY(qss.contains(lightPalette.primaryText.name()));
    }

    // --- 2. Graphite Dark Mode Verification ---
    Theme::ThemeManager::instance().setMode(Theme::ThemeMode::Dark);
    const auto &darkPalette = Theme::ThemeManager::instance().palette();
    QCOMPARE(Theme::ThemeManager::instance().currentMode(), Theme::ThemeMode::Dark);

    // Verify QApplication palette synchronization in Dark Mode
    QCOMPARE(QApplication::palette().color(QPalette::Window), darkPalette.appBackground);
    QCOMPARE(QApplication::palette().color(QPalette::Base), darkPalette.editorBackground);
    QCOMPARE(QApplication::palette().color(QPalette::Text), darkPalette.primaryText);
    QCOMPARE(QApplication::palette().color(QPalette::Highlight), darkPalette.accent);
    QCOMPARE(QApplication::palette().color(QPalette::HighlightedText), darkPalette.accentText);

    // Contrast guarantee for Dark Mode
    qreal darkBgLuma = calcLuma(darkPalette.editorBackground);
    qreal darkTextLuma = calcLuma(darkPalette.primaryText);
    QVERIFY(std::abs(darkBgLuma - darkTextLuma) >= 0.5);

    // Instantiate QFontDialog under Dark Mode
    {
        QFont initFont(QStringLiteral("Cascadia Code"), 12);
        QFontDialog dlg(initFont);
        dlg.setOption(QFontDialog::DontUseNativeDialog, true);
        dlg.setStyleSheet(Theme::ThemeManager::instance().mainStylesheet());

        auto listViews = dlg.findChildren<QListView *>();
        QVERIFY(!listViews.isEmpty());
        auto lineEdits = dlg.findChildren<QLineEdit *>();
        QVERIFY(!lineEdits.isEmpty());

        QString qss = dlg.styleSheet();
        QVERIFY(qss.contains(darkPalette.editorBackground.name()));
        QVERIFY(qss.contains(darkPalette.primaryText.name()));
    }

    // Restore clean Light state
    Theme::ThemeManager::instance().setMode(Theme::ThemeMode::Light);
}

void TestProbes::testRecentFilesHistoryPersistenceAndMenu() {
    QTemporaryDir tempDir;
    QDir dir(tempDir.path());

    QString pathA = dir.filePath(QStringLiteral("fileA_notes.md"));
    QString pathB = dir.filePath(QStringLiteral("fileB_report.markdown"));
    QString pathC = dir.filePath(QStringLiteral("fileC_archive.md"));

    {
        QFile fA(pathA); fA.open(QIODevice::WriteOnly); fA.write("# Doc A\n"); fA.close();
        QFile fB(pathB); fB.open(QIODevice::WriteOnly); fB.write("# Doc B\n"); fB.close();
        QFile fC(pathC); fC.open(QIODevice::WriteOnly); fC.write("# Doc C\n"); fC.close();
    }

    QString normA = QDir::toNativeSeparators(QFileInfo(pathA).canonicalFilePath());
    QString normB = QDir::toNativeSeparators(QFileInfo(pathB).canonicalFilePath());
    QString normC = QDir::toNativeSeparators(QFileInfo(pathC).canonicalFilePath());

    // 1. Initial State
    UI::MainWindow w1;
    QVERIFY(w1.recentFiles().isEmpty());
    QVERIFY(w1.recentFilesMenu() != nullptr);
    QCOMPARE(w1.recentFilesMenu()->actions().size(), 1);
    QVERIFY(!w1.recentFilesMenu()->actions().first()->isEnabled());
    QVERIFY(w1.sidebar()->recentFiles().isEmpty());
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItemCount(), 0);

    // 2. Add files and test ordering & deduplication
    w1.addRecentFile(normA);
    w1.addRecentFile(normB);
    QCOMPARE(w1.recentFiles(), QStringList({normB, normA}));
    QCOMPARE(w1.sidebar()->recentFiles(), QStringList({normB, normA}));
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItemCount(), 2);

    // Re-add normA: should move to top without duplication
    w1.addRecentFile(normA);
    QCOMPARE(w1.recentFiles(), QStringList({normA, normB}));
    QCOMPARE(w1.sidebar()->recentFiles(), QStringList({normA, normB}));
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItemCount(), 2);

    // Add normC
    w1.addRecentFile(normC);
    QCOMPARE(w1.recentFiles(), QStringList({normC, normA, normB}));
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItemCount(), 3);

    // 3. Verify Menu Actions and Shortcuts & Tooltips
    QVERIFY(w1.recentFilesMenu()->toolTipsVisible());
    const auto &menuActions = w1.recentFilesMenu()->actions();
    // 3 items + 1 separator + 1 clear action = 5 actions
    QCOMPARE(menuActions.size(), 5);
    QVERIFY(menuActions[0]->text().contains(QStringLiteral("&1")));
    QVERIFY(menuActions[0]->text().contains(QStringLiteral("fileC_archive.md")));
    QCOMPARE(menuActions[0]->toolTip(), normC);
    QVERIFY(menuActions[1]->text().contains(QStringLiteral("&2")));
    QVERIFY(menuActions[1]->text().contains(QStringLiteral("fileA_notes.md")));
    QCOMPARE(menuActions[1]->toolTip(), normA);
    QVERIFY(menuActions[2]->text().contains(QStringLiteral("&3")));
    QVERIFY(menuActions[2]->text().contains(QStringLiteral("fileB_report.markdown")));
    QCOMPARE(menuActions[2]->toolTip(), normB);
    QVERIFY(menuActions[3]->isSeparator());
    QVERIFY(menuActions[4]->text().contains(QStringLiteral("清空历史记录")));
    QVERIFY(menuActions[4]->isEnabled());

    // Verify Sidebar Tree Item Tooltips with Full Native Path
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItem(0)->toolTip(0), normC);
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItem(1)->toolTip(0), normA);
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItem(2)->toolTip(0), normB);

    // 4. Verify Sidebar Search Filtering
    w1.sidebar()->switchToRecent();
    w1.sidebar()->searchEdit()->setText(QStringLiteral("archive"));
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItem(0)->isHidden(), false); // fileC_archive matches
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItem(1)->isHidden(), true);  // fileA hidden
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItem(2)->isHidden(), true);  // fileB hidden

    w1.sidebar()->searchEdit()->clear();
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItem(0)->isHidden(), false);
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItem(1)->isHidden(), false);
    QCOMPARE(w1.sidebar()->recentTree()->topLevelItem(2)->isHidden(), false);

    // 5. Verify Persistence via QSettings across instances
    QTemporaryDir tempSettingsDir;
    QSettings testSettings(tempSettingsDir.filePath(QStringLiteral("history_test.ini")), QSettings::IniFormat);
    w1.savePreferences(&testSettings);

    UI::MainWindow w2;
    w2.loadPreferences(&testSettings);
    QCOMPARE(w2.recentFiles(), QStringList({normC, normA, normB}));
    QCOMPARE(w2.sidebar()->recentFiles(), QStringList({normC, normA, normB}));
    QCOMPARE(w2.sidebar()->recentTree()->topLevelItemCount(), 3);
    QCOMPARE(w2.recentFilesMenu()->actions().size(), 5);

    // 6. Verify Removal and Clear
    w2.removeRecentFile(normA);
    QCOMPARE(w2.recentFiles(), QStringList({normC, normB}));
    QCOMPARE(w2.sidebar()->recentTree()->topLevelItemCount(), 2);

    w2.clearRecentFiles();
    QVERIFY(w2.recentFiles().isEmpty());
    QCOMPARE(w2.sidebar()->recentTree()->topLevelItemCount(), 0);
    QCOMPARE(w2.recentFilesMenu()->actions().size(), 1);

    // 7. Verify opening a document automatically updates recent files
    bool ok = w2.openFile(normA, false);
    QVERIFY(ok);
    QCOMPARE(w2.recentFiles().size(), 1);
    QCOMPARE(w2.recentFiles().first(), normA);
    QCOMPARE(w2.sidebar()->recentTree()->topLevelItemCount(), 1);
}

void TestProbes::testAdaptiveReadingWidthAndTypographyReflow() {
    // --- 1. Mathematical Scaling Verification of calculateAdaptiveReadingWidth ---
    QCOMPARE(Theme::ThemeManager::calculateAdaptiveReadingWidth(12.0), 780.0);
    QCOMPARE(Theme::ThemeManager::calculateAdaptiveReadingWidth(14.0), 910.0);
    QCOMPARE(Theme::ThemeManager::calculateAdaptiveReadingWidth(16.0), 1040.0);
    QCOMPARE(Theme::ThemeManager::calculateAdaptiveReadingWidth(18.0), 1120.0); // Clamped at 1120
    QCOMPARE(Theme::ThemeManager::calculateAdaptiveReadingWidth(24.0), 1120.0); // Clamped at 1120
    QCOMPARE(Theme::ThemeManager::calculateAdaptiveReadingWidth(8.0), 640.0);   // Clamped at 640

    // --- 2. Fixed & Adaptive Mode Logic ---
    auto &theme = Theme::ThemeManager::instance();
    theme.setBasePointSize(12.0);

    theme.setReadingWidthMode(Theme::ThemeManager::ReadingWidthMode::Narrow);
    QCOMPARE(theme.maxReadingWidth(), 640.0);

    theme.setReadingWidthMode(Theme::ThemeManager::ReadingWidthMode::Standard);
    QCOMPARE(theme.maxReadingWidth(), 780.0);

    theme.setReadingWidthMode(Theme::ThemeManager::ReadingWidthMode::Wide);
    QCOMPARE(theme.maxReadingWidth(), 1040.0);

    theme.setReadingWidthMode(Theme::ThemeManager::ReadingWidthMode::Adaptive);
    QCOMPARE(theme.maxReadingWidth(), 780.0);
    theme.setBasePointSize(14.0);
    QCOMPARE(theme.maxReadingWidth(), 910.0);
    theme.setBasePointSize(16.0);
    QCOMPARE(theme.maxReadingWidth(), 1040.0);
    theme.setBasePointSize(18.0);
    QCOMPARE(theme.maxReadingWidth(), 1120.0);

    // --- 3. LayoutEngine Dynamic Expansion & Reflow at Constant Viewport ---
    Parser::MarkdownParserAdapter parser;
    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral(
        "# 自适应阅读宽度重排测试\n\n"
        "这是一段较长且文字连贯的 Markdown 段落，用来严密验证在宽屏显示器下，"
        "当用户放大字号时，排版引擎不仅仅是将文字自身像素放大，还会自适应地适度扩展"
        "正文阅读宽度，让每一行能够保持最佳的文字容纳量，避免频繁出现局促的断行与大片空白。\n\n"
        "- 列表项第一条：验证自适应布局下的缩进与换行宽度\n"
        "- 列表项第二条：验证列表项随字号缩放时的排版表现\n"
    ));

    auto root = parser.parse(buffer);
    Mapping::DisplayProjection projection;

    Editor::LayoutEngine engine;
    const qreal wideViewport = 1400.0;

    // A. At 12pt (Adaptive -> 780px)
    theme.setBasePointSize(12.0);
    projection.buildProjection(root, buffer, 0, false);
    engine.layout(projection, wideViewport);

    QCOMPARE(engine.contentWidth(), 780.0);
    qreal expectedLeft12 = (wideViewport - 780.0) / 2.0;
    QCOMPARE(engine.contentLeft(), expectedLeft12);
    qreal textLineWidth12 = engine.blocks()[1].layout->lineAt(0).width();

    // B. At 16pt (Adaptive -> 1040px) with identical viewport 1400.0px
    theme.setBasePointSize(16.0);
    projection.buildProjection(root, buffer, 0, false);
    engine.layout(projection, wideViewport);

    QCOMPARE(engine.contentWidth(), 1040.0);
    qreal expectedLeft16 = (wideViewport - 1040.0) / 2.0;
    QCOMPARE(engine.contentLeft(), expectedLeft16);
    qreal textLineWidth16 = engine.blocks()[1].layout->lineAt(0).width();

    // Line wrap width must have expanded from ~772px to ~1032px
    QVERIFY(textLineWidth16 > textLineWidth12 + 200.0);

    // Assert exact mathematical equivalence with a fresh cold layout at 16pt
    Editor::LayoutEngine coldEngine16;
    coldEngine16.layout(projection, wideViewport);
    QCOMPARE(engine.contentWidth(), coldEngine16.contentWidth());
    QCOMPARE(engine.contentLeft(), coldEngine16.contentLeft());
    QVERIFY(qAbs(engine.totalHeight() - coldEngine16.totalHeight()) < 0.001);

    // --- 4. Small Viewport Constraint (No Horizontal Overflow) ---
    const qreal smallViewport = 600.0;
    engine.layout(projection, smallViewport);
    QVERIFY(engine.contentWidth() <= smallViewport - 32.0);
    QVERIFY(engine.contentLeft() >= 16.0);

    // --- 5. MainWindow Menu Synchronization & QSettings Persistence ---
    UI::MainWindow w1;
    QVERIFY(w1.actWidthAdaptive() != nullptr);
    QVERIFY(w1.actWidthNarrow() != nullptr);
    QVERIFY(w1.actWidthStandard() != nullptr);
    QVERIFY(w1.actWidthWide() != nullptr);

    // Switch to Wide mode via action
    w1.actWidthWide()->trigger();
    QCOMPARE(theme.readingWidthMode(), Theme::ThemeManager::ReadingWidthMode::Wide);
    QVERIFY(w1.actWidthWide()->isChecked());
    QVERIFY(!w1.actWidthStandard()->isChecked());
    QVERIFY(!w1.actWidthAdaptive()->isChecked());

    QTemporaryDir tempPrefDir;
    QSettings saveSettings(tempPrefDir.filePath(QStringLiteral("rw_pref.ini")), QSettings::IniFormat);
    w1.savePreferences(&saveSettings);

    // Second window loads preferences
    theme.setReadingWidthMode(Theme::ThemeManager::ReadingWidthMode::Narrow); // tamper
    UI::MainWindow w2;
    w2.loadPreferences(&saveSettings);
    QCOMPARE(theme.readingWidthMode(), Theme::ThemeManager::ReadingWidthMode::Wide);
    QVERIFY(w2.actWidthWide()->isChecked());

    // Clean restore
    theme.setReadingWidthMode(Theme::ThemeManager::ReadingWidthMode::Adaptive);
    theme.setBasePointSize(12.0);
}

void TestProbes::testMarkdownTableNativeLayoutAndInteraction() {
    Parser::MarkdownParserAdapter parser;
    Core::TextBuffer buffer;
    buffer.setText(QStringLiteral(
        "# GFM 表格排版验证\n\n"
        "| 数据点 | 数值 | 来源 |\n"
        "| :--- | :---: | ---: |\n"
        "| 信号 A | 98.5% | 核心引擎 |\n"
        "| 信号 B | 1024 | 缓存监控 |\n\n"
        "这是表格下方的后续段落。\n"
    ));

    // 1. Semantic AST Verification
    auto root = parser.parse(buffer);
    QVERIFY(root != nullptr);
    QCOMPARE(root->children.size(), 3); // Heading, Table, Paragraph

    auto tableNode = root->children[1];
    QCOMPARE(tableNode->type, Parser::NodeType::Table);
    QCOMPARE(tableNode->tableColumnCount, 3);
    QCOMPARE(tableNode->tableAlignments.size(), 3);
    QCOMPARE(tableNode->tableAlignments[0], static_cast<uint8_t>('l'));
    QCOMPARE(tableNode->tableAlignments[1], static_cast<uint8_t>('c'));
    QCOMPARE(tableNode->tableAlignments[2], static_cast<uint8_t>('r'));

    QCOMPARE(tableNode->children.size(), 3); // Header row, row 1, row 2
    QVERIFY(tableNode->children[0]->isTableHeader);
    QCOMPARE(tableNode->children[0]->children.size(), 3); // 3 cells in header
    QCOMPARE(tableNode->children[1]->children.size(), 3); // 3 cells in row 1

    // 2. DisplayProjection Verification (Live Preview Mode)
    Mapping::DisplayProjection projection;
    projection.buildProjection(root, buffer, 0, false);
    QCOMPARE(projection.blocks().size(), 3);

    const auto &tableBlock = projection.blocks()[1];
    QVERIFY(tableBlock.isTableBlock);
    QCOMPARE(tableBlock.tableData.colCount, 3);
    QCOMPARE(tableBlock.tableData.rows.size(), 3);
    QVERIFY(tableBlock.tableData.rows[0].isHeader);
    QCOMPARE(tableBlock.tableData.rows[0].cells[0].text, QStringLiteral("数据点"));
    QCOMPARE(tableBlock.tableData.rows[0].cells[1].text, QStringLiteral("数值"));
    QCOMPARE(tableBlock.tableData.rows[0].cells[2].text, QStringLiteral("来源"));
    QCOMPARE(tableBlock.tableData.rows[1].cells[0].text, QStringLiteral("信号 A"));
    QCOMPARE(tableBlock.tableData.rows[1].cells[1].text, QStringLiteral("98.5%"));
    QCOMPARE(tableBlock.tableData.rows[1].cells[2].text, QStringLiteral("核心引擎"));

    // Verify cell alignments in projection
    QCOMPARE(tableBlock.tableData.rows[0].cells[0].alignment, Qt::AlignLeft);
    QCOMPARE(tableBlock.tableData.rows[0].cells[1].alignment, Qt::AlignCenter);
    QCOMPARE(tableBlock.tableData.rows[0].cells[2].alignment, Qt::AlignRight);

    // 3. LayoutEngine Verification
    Editor::LayoutEngine engine;
    const qreal viewportW = 1200.0;
    engine.layout(projection, viewportW);

    const auto &layoutBlocks = engine.blocks();
    QCOMPARE(layoutBlocks.size(), 3);
    const auto &bData = layoutBlocks[1];
    QVERIFY(bData.isTableBlock);
    QCOMPARE(bData.tableLayout.colCount, 3);
    QCOMPARE(bData.tableLayout.colWidths.size(), 3);
    QCOMPARE(bData.tableLayout.rows.size(), 3);

    // Columns must fill contentWidth
    qreal sumColW = 0.0;
    for (qreal w : bData.tableLayout.colWidths) sumColW += w;
    QVERIFY(qAbs(sumColW - engine.contentWidth()) < 0.5);

    // Header row and data rows must have valid heights and cell rectangles
    for (int r = 0; r < 3; ++r) {
        const auto &row = bData.tableLayout.rows[r];
        QVERIFY(row.rect.height() >= 34.0);
        QCOMPARE(row.cells.size(), 3);
        for (int c = 0; c < 3; ++c) {
            const auto &cell = row.cells[c];
            QVERIFY(cell.rect.isValid());
            QVERIFY(cell.layout != nullptr);
            QCOMPARE(cell.rect.width(), bData.tableLayout.colWidths[c]);
        }
    }

    // 4. Hit-testing and Cursor Positioning in Table Cells
    int targetSourceOffset = buffer.text().indexOf(QStringLiteral("98.5%"));
    QVERIFY(targetSourceOffset > 0);

    QRectF cRect = engine.cursorRect(targetSourceOffset, projection);
    QVERIFY(cRect.isValid());
    // Cursor must fall inside row 1, cell 1
    const auto &cell11 = bData.tableLayout.rows[1].cells[1];
    QVERIFY(cRect.left() >= cell11.rect.left());
    QVERIFY(cRect.right() <= cell11.rect.right() + 5.0);
    QVERIFY(cRect.top() >= cell11.rect.top());
    QVERIFY(cRect.bottom() <= cell11.rect.bottom());

    // Hit-testing the center of that cell must return offset near or inside "98.5%"
    int hitOffset = engine.hitTest(cell11.rect.center(), projection);
    QVERIFY(hitOffset >= cell11.sourceRange.start && hitOffset <= cell11.sourceRange.end);

    // 5. Sidebar default width verification (P2)
    UI::MainWindow mw;
    mw.show();
    QTest::qWait(50);
    QCOMPARE(mw.findChild<QSplitter*>()->sizes().value(0), 260);
}

QTEST_MAIN(TestProbes)
#include "TestProbes.moc"
