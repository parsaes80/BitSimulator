#include "MainWindow.h"
#include <QDebug>

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent)
{
    ui.setupUi(this);

    // Create thread and Simulator
    simThread = new QThread(this);
    simObj = new Simulator;
    
    simObj->moveToThread(simThread);
    
    connect(simThread, &QThread::finished, simObj, &QObject::deleteLater);
    connect(simThread, &QThread::started, simObj, &Simulator::SimController);

    simThread->start();
    
    // Alternative: To start simulation manually later, you would call:
    // QMetaObject::invokeMethod(simObj, "SimController", Qt::QueuedConnection);

}

MainWindow::~MainWindow()
{
    simThread->quit();
    simThread->wait();
}



