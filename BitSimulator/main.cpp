#include "MainWindow.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    applyBaseStyle(app);   // colors: see MainWindow::setColorScheme

    MainWindow window;
    window.show();
    return app.exec();
}
