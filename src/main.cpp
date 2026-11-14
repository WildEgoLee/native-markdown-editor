#include "ui/MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QCommandLineParser>
#include <QCommandLineOption>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("MarkdownNativeEditor"));
    app.setApplicationVersion(QStringLiteral("0.1.0-beta"));
    app.setApplicationDisplayName(QStringLiteral("Windows 原生 Markdown 编辑器"));
    app.setOrganizationName(QStringLiteral("MarkdownTool"));
    app.setOrganizationDomain(QStringLiteral("markdowntool.local"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Windows 原生 Markdown 编辑器"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption smokeOption(QStringLiteral("smoke-test"), QStringLiteral("验证应用初始化与所有运行时依赖加载后安全退出"));
    parser.addOption(smokeOption);
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("要打开的 Markdown 文件路径"));

    parser.process(app);

    if (parser.isSet(smokeOption)) {
        return 0;
    }

    // Set high quality system font
    QFont defaultFont(QStringLiteral("Segoe UI"), 11);
    app.setFont(defaultFont);

    MarkdownTool::UI::MainWindow window;
    window.show();

    const QStringList posArgs = parser.positionalArguments();
    if (!posArgs.isEmpty()) {
        window.openFile(posArgs.first());
    }

    return app.exec();
}
