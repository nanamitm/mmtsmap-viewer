#include "MainWindow.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mmtsmap-viewer"));

    // Dark Fusion palette so the timeline view (dark) blends with the chrome.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(0x2b, 0x2e, 0x36));
    pal.setColor(QPalette::WindowText, QColor(0xe6, 0xe9, 0xef));
    pal.setColor(QPalette::Base, QColor(0x23, 0x26, 0x2d));
    pal.setColor(QPalette::AlternateBase, QColor(0x2b, 0x2e, 0x36));
    pal.setColor(QPalette::Text, QColor(0xe6, 0xe9, 0xef));
    pal.setColor(QPalette::Button, QColor(0x33, 0x37, 0x40));
    pal.setColor(QPalette::ButtonText, QColor(0xe6, 0xe9, 0xef));
    pal.setColor(QPalette::Highlight, QColor(0x3d, 0x6f, 0xb5));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(pal);

    MainWindow w;
    w.show();

    // Allow "mmtsmap-viewer <file>" to open directly.
    const QStringList args = app.arguments();
    if (args.size() > 1)
        w.loadFile(args.at(1));

    return app.exec();
}
