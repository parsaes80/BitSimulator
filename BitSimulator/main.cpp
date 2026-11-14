#include "MainWindow.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    applyBaseStyle(app);   // the colors come from BitSimulator.ui, see MainWindow::setColorScheme

    MainWindow window;
    window.show();
    return app.exec();
}
