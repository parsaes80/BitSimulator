#pragma once

#include <QtWidgets/QMainWindow>
#include <QThread>
#include "ui_BitSimulator.h"
#include "simulator.h"
#include "Theme.h"

class FloatingPanel;
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
    void compileHdl();                       // HDL Editor tab: the editor's text
    void compileFinished(bool ok);
    void chooseCompileDir();                 // Compile Dir tab: the system's folder chooser
    void scanCompileDir();                   // lists the .v / .sv files under the folder in the path field
    void updateCompileDirStatus();           // "3 of 4 files selected", and whether Compile directory is enabled
    void compileDir();                       // the checked files
    void updateTimerPeriod(int sliderValue);
    void toggleColorScheme();                // the light/dark button; stops following the system setting

signals:
    void sendTimerPeriod(int milliseconds);
    void sendCodeToCompiler(const QString& code);
    void sendFilesToCompiler(const QStringList& files, const QStringList& includeDirs);
private:
    void setup();
    void setupHdlEditor();
    void setupPropertiesPanel();
    void setupColorScheme();
    void beginCompile(const QString& message);   // stops the simulation and disables both compile buttons

    Ui::BitSimulatorClass ui;
    PropertiesPanel* m_properties = nullptr;
    FloatingPanel* m_propertiesFrame = nullptr;   // the floating box holding the panel
    ColorScheme m_colorScheme = ColorScheme::Dark;
    bool m_followSystemScheme = true;        // until the user picks a mode with the theme button
    QString m_compileError;                  // last message from the HDL compiler, shown if the compile fails
    bool m_compiling = false;
    QString m_scannedDir;                    // the folder the file list shows
    QStringList m_compileIncludeDirs;        // its folders with Verilog or header files, for `include
    QThread *simThread, *compilerThread;
    Simulator *simObj;
    HDLCompiler *compiler;
};
