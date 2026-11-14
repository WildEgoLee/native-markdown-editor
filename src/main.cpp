#include "ui/MainWindow.h"

#include <QApplication>
#include <QFont>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("MarkdownNativeEditor"));
    app.setApplicationDisplayName(QStringLiteral("Windows 原生 Markdown 编辑器"));
    app.setOrganizationName(QStringLiteral("MarkdownTool"));
    app.setOrganizationDomain(QStringLiteral("markdowntool.local"));

    // Set high quality system font
    QFont defaultFont(QStringLiteral("Segoe UI"), 11);
    app.setFont(defaultFont);

    MarkdownTool::UI::MainWindow window;
    window.show();

    if (argc > 1) {
        QString openPath = QString::fromLocal8Bit(argv[1]);
        window.openFile(openPath);
    }

    return app.exec();
}
