#pragma once

#include <QtWidgets/QMainWindow>
#include <QThread>
#include <QRegularExpression>
#include "ui_BitSimulator.h"
#include "simulator.h"
#include "Theme.h"

class PropertiesPanel;
enum class SimState;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    ColorScheme colorScheme() const { return m_colorScheme; }
    // Restyles the whole window. Called with the system's setting at startup and by the theme button.
    void setColorScheme(ColorScheme scheme);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    // Connected explicitly in setup(); nothing here relies on Qt's on_<object>_<signal> name matching
    void toggleSimulation();                 // the Start/Pause button
    void showSimState(SimState state);       // makes that button say Start or Pause
    void clearCircuit();
    void compileHdl();
    void compileFinished(bool ok);
    void updateTimerPeriod(int sliderValue);
    void toggleColorScheme();                // the light/dark button; stops following the system setting

signals:
    void sendTimerPeriod(int milliseconds);
private:
    void setup();
    void setupPropertiesPanel();
    void setupColorScheme();

    Ui::BitSimulatorClass ui;
    PropertiesPanel* m_properties = nullptr;
    ThemeSheets m_themeSheets;               // the .ui style sheet in its dark and light versions
    ColorScheme m_colorScheme = ColorScheme::Dark;
    bool m_followSystemScheme = true;        // until the user picks a mode with the theme button
    QString m_compileError;                  // last message from the HDL compiler, shown if the compile fails
    QThread *simThread, *compilerThread;
    Simulator *simObj;
    HDLCompiler *compiler;
};
