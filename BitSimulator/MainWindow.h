#pragma once

#include <QtWidgets/QMainWindow>
#include <QThread>
#include "ui_BitSimulator.h"
#include "simulator.h"
#include "Theme.h"

class FloatingPanel;
class PropertiesPanel;
class WaveformModel;
enum class SimState;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    ColorScheme colorScheme() const { return m_colorScheme; }
    // Restyles the whole window
    void setColorScheme(ColorScheme scheme);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    // Connected explicitly in setup(), no on_<object>_<signal> auto-connection
    void toggleSimulation();                 // Start/Pause
    void showSimState(SimState state);
    void resetSimulation();                  // end the run, clear its waveforms
    void clearCircuit();
    void compileHdl();
    void compileFinished(bool ok);
    void chooseCompileDir();
    void scanCompileDir();                   // the .v / .sv files under the folder in the path field
    void updateCompileDirStatus();           // "3 of 4 files selected"
    void compileDir();
    void updateTimerPeriod(int sliderValue);
    void toggleColorScheme();                // stops following the system setting

signals:
    void sendTimerPeriod(int milliseconds);
    void sendCodeToCompiler(const QString& code);
    void sendFilesToCompiler(const QStringList& files, const QStringList& includeDirs);
private:
    void setup();
    void setupHdlEditor();
    void setupPropertiesPanel();
    void setupColorScheme();
    void setupWaveforms();
    void updateWaveStatus();
    void beginCompile(const QString& message);   // stops the run, disables both compile buttons

    Ui::BitSimulatorClass ui;
    PropertiesPanel* m_properties = nullptr;
    FloatingPanel* m_propertiesFrame = nullptr;
    WaveformModel* m_waveModel = nullptr;
    ColorScheme m_colorScheme = ColorScheme::Dark;
    bool m_followSystemScheme = true;        // until the user picks a mode
    QString m_compileError;                  // shown if the compile fails
    bool m_compiling = false;
    QString m_scannedDir;
    QStringList m_compileIncludeDirs;        // for `include
    QThread *simThread, *compilerThread;
    Simulator *simObj;
    HDLCompiler *compiler;
};
