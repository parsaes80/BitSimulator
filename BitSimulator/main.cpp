#include "MainWindow.h"
#include <QtWidgets/QApplication>

bool sim_running = false;
GlobalMap map;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
