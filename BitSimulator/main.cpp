#include "MainWindow.h"
#include <QtWidgets/QApplication>
#include <QIcon>
#ifdef Q_OS_WASM
#include <QAbstractEventDispatcher>
#include <QFontDatabase>
#include <emscripten.h>
#endif

using namespace Qt::StringLiterals;

#ifdef Q_OS_WASM
// Qt 6.12's browser event loop can stop re-arming its timer while idle (the simulation then waits for the mouse); waking it as input does keeps timers running
extern "C" EMSCRIPTEN_KEEPALIVE void bitsimulatorWakeEventLoop()
{
    if (QAbstractEventDispatcher* dispatcher = QCoreApplication::eventDispatcher()) dispatcher->wakeUp();
}
EM_JS(void, keepEventLoopAwake, (int intervalMs), { setInterval(() => _bitsimulatorWakeEventLoop(), intervalMs); });
#endif

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
    keepEventLoopAwake(15);
#endif
    applyBaseStyle(app);   // colors: see MainWindow::setColorScheme

    MainWindow window;
    window.show();
    return app.exec();
}
