#include "MainWindow.h"
#include <QDebug>
#include <iostream>
#include <print>

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent)
{
    ui.setupUi(this);

    // Create thread and Simulator
    simThread = new QThread(this);
    simObj = new Simulator;
    
    // Move Simulator to worker thread
    simObj->moveToThread(simThread);
    
    // Connect signals
    connect(simThread, &QThread::finished, simObj, &QObject::deleteLater);
    connect(simObj, &Simulator::finished, this, &MainWindow::onSimulationFinished);

  
    // Optional: Start simulation automatically when thread starts
    connect(simThread, &QThread::started, simObj, &Simulator::SimController);

    // Start the thread (this will automatically trigger SimController() due to the connection above)
    simThread->start();
    
    // Alternative: To start simulation manually later, you would call:
    // QMetaObject::invokeMethod(simObj, "SimController", Qt::QueuedConnection);

}

MainWindow::~MainWindow()
{
    simThread->quit();
    simThread->wait();
}

void MainWindow::onSimulationFinished() {
    // Handle simulation finished
    qDebug() << "Simulation finished";
}

void MainWindow::onSimulationProgress(int percentage) {

    
    // Also output to console
    std::println ("Simulation Progress: {}%" , percentage);
}

void MainWindow::onSimulationResult(const QString &data) {
    // Handle simulation result
    qDebug() << "Result:" << data;
}

void MainWindow::onSimulationError(const QString &message) {
    // Handle simulation error
    qDebug() << "Error:" << message;
}

