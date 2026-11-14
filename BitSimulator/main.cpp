#include "MainWindow.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    applyDarkTheme(app);

    MainWindow window;
    window.show();
    return app.exec();
}
