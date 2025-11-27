#pragma once

#include <QtWidgets/QMainWindow>
#include <QThread>
#include <QRegularExpression>
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

    void on_slider_valueChanged(int value);
    void on_compileButton_clicked();

    void on_overlay_currentChanged(int arg1);
    void on_numGateMuxInputs_valueChanged(int value);
    void on_regType_currentIndexChanged(int index);
    void on_regHoldType_currentIndexChanged(int index);
    void on_regEnableType_currentIndexChanged(int index);
    void on_srcType_currentIndexChanged(int index);
    void on_srcValues_textChanged();

    void on_numDisplayInputs_valueChanged(int value);

    void on_numDisplayOutputs_valueChanged(int value);

    void on_clearButton_clicked();

signals:
    void sendTimerPeriod(int milliseconds);
private:
    void setup();

    Ui::BitSimulatorClass ui;
    QThread *simThread, *compilerThread;
    Simulator *simObj;
    HDLCompiler *compiler;
};

