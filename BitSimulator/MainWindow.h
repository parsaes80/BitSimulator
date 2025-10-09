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
protected:
    void closeEvent(QCloseEvent* event) override;
private slots:
    void on_startButton_clicked();

    void on_srcvalues_textChanged();

private:
    void setup();
    Ui::BitSimulatorClass ui;
    QThread *simThread;
    Simulator *simObj;
};

