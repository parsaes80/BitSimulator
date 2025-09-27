#pragma once

#include <QtWidgets/QMainWindow>
#include <QThread>
#include "ui_BitSimulator.h"
#include "simulator.h"


class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_startButton_clicked();

private:
    Ui::BitSimulatorClass ui;
    QThread *simThread;
    Simulator *simObj;
};

