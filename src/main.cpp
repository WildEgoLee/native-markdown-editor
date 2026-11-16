#include "ui/MainWindow.h"
#include "theme/Theme.h"

#include <QApplication>
#include <QFont>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QTimer>
#include <QFileInfo>
#include <QFile>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS *pException) {
    FILE *f = fopen("crash.log", "w");
    if (f) {
        fprintf(f, "Crash occurred! Exception code: 0x%08lX at address: %p\n",
                pException->ExceptionRecord->ExceptionCode,
                pException->ExceptionRecord->ExceptionAddress);
        void *stack[64];
        unsigned short frames = CaptureStackBackTrace(0, 64, stack, NULL);
        for (unsigned short i = 0; i < frames; ++i) {
            fprintf(f, "  [%02d] %p\n", i, stack[i]);
        }
        fclose(f);
    }
    HANDLE hFile = CreateFileW(L"crash.dmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = pException;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mei, NULL, NULL);
        CloseHandle(hFile);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(CrashHandler);
#endif
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("MarkdownNativeEditor"));
    app.setApplicationVersion(QStringLiteral("0.4.1-beta"));
    app.setApplicationDisplayName(QStringLiteral("Windows 原生 Markdown 编辑器"));
    app.setOrganizationName(QStringLiteral("MarkdownTool"));
    app.setOrganizationDomain(QStringLiteral("markdowntool.local"));

    QString fileToOpen;
    bool isGuiSmokeTest = false;
    {
        QCommandLineParser parser;
        parser.setApplicationDescription(QStringLiteral("Windows 原生 Markdown 编辑器"));
        parser.addHelpOption();
        parser.addVersionOption();
        QCommandLineOption smokeOption(QStringLiteral("smoke-test"), QStringLiteral("验证应用初始化与所有运行时依赖加载后安全退出"));
        parser.addOption(smokeOption);
        QCommandLineOption guiSmokeOption(QStringLiteral("gui-smoke-test"), QStringLiteral("构造主窗口，加载指定文档，运行事件循环并验证真实绘制后安全退出"));
        parser.addOption(guiSmokeOption);
        parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("要打开的 Markdown 文件路径"));

        parser.process(app);

        if (parser.isSet(smokeOption)) {
            return 0;
        }

        isGuiSmokeTest = parser.isSet(guiSmokeOption);

        const QStringList posArgs = parser.positionalArguments();
        if (!posArgs.isEmpty()) {
            fileToOpen = posArgs.first();
        }
    }

    // Set high quality system font with optimal Chinese / English glyph harmonization
    QFont defaultFont;
    defaultFont.setFamilies({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("PingFang SC"), QStringLiteral("Segoe UI"), QStringLiteral("sans-serif")});
    defaultFont.setPointSize(10);
    app.setFont(defaultFont);
    MarkdownTool::Theme::ThemeManager::instance().applyApplicationPalette();

    auto window = std::make_unique<MarkdownTool::UI::MainWindow>();
    window->loadPreferences();
    window->show();

    bool openFileResult = true;
    if (!fileToOpen.isEmpty()) {
        openFileResult = window->openFile(fileToOpen, !isGuiSmokeTest);
    } else if (!isGuiSmokeTest) {
        window->checkUntitledRecoverySnapshot(true);
    }

    if (isGuiSmokeTest) {
        // Run full Qt event loop to ensure layout, styling, and rendering execute properly
        QTimer::singleShot(600, &app, [&app, w = window.get(), fileToOpen, openFileResult]() {
            if (!w->isVisible()) {
                std::exit(1);
            }
            // Trigger off-screen render to verify entire widget tree paint pipeline
            QPixmap pix(w->size());
            w->render(&pix);
            if (pix.isNull()) {
                std::exit(2);
            }
            // Strict verification: openFile must have succeeded and editor buffer must contain the file's content
            if (!fileToOpen.isEmpty()) {
                if (!openFileResult) {
                    std::exit(3);
                }
                QFile f(fileToOpen);
                if (f.open(QIODevice::ReadOnly)) {
                    QByteArray rawBytes = f.readAll();
                    MarkdownTool::Core::TextBuffer expectedBuf;
                    expectedBuf.loadFromBytes(rawBytes);
                    if (w->editorView()->buffer().text() != expectedBuf.text()) {
                        std::exit(4);
                    }
                } else {
                    std::exit(5);
                }
            }
            app.exit(0);
        });
    }

    return app.exec();
}
