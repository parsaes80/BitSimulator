#include "MainWindow.h"
#include <QtWidgets/QApplication>
#include <QIcon>
#ifdef Q_OS_WASM
#include <QFontDatabase>
#endif

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral(BITSIM_VERSION));
    // icons/icons.qrc: the rising edge up to 48 px, the square logo from 64 px
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256, 512}) icon.addFile(u":/icons/icon-%1.png"_s.arg(size), QSize(size, size));
    app.setWindowIcon(icon);
#ifdef Q_OS_WASM
    // A web page can't use the system's fonts; this is the desktop app's on Linux (KDE's default), from fonts/fonts.qrc
    QFontDatabase::addApplicationFont(u":/fonts/NotoSans-Regular.ttf"_s);
    QFontDatabase::addApplicationFont(u":/fonts/NotoSans-Bold.ttf"_s);
    app.setFont(QFont(u"Noto Sans"_s, 10));
#endif
    applyBaseStyle(app);   // colors: see MainWindow::setColorScheme

    MainWindow window;
    window.show();
    return app.exec();
}
