#pragma once

#include <QtWidgets/QMainWindow>
#include <QThread>
#include <QRegularExpression>
#include "ui_BitSimulator.h"
#include "simulator.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

class PropertiesPanel;
enum class SimState;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
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

signals:
    void sendTimerPeriod(int milliseconds);
private:
    void setup();
    void setupPropertiesPanel();

    Ui::BitSimulatorClass ui;
    PropertiesPanel* m_properties = nullptr;
    QString m_compileError;                  // last message from the HDL compiler, shown if the compile fails
    QThread *simThread, *compilerThread;
    Simulator *simObj;
    HDLCompiler *compiler;
};

// One look on every platform: Qt's Fusion style with a dark palette.
inline void applyDarkTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const QColor window(37, 38, 41), base(27, 28, 31), raised(52, 54, 58);
    const QColor text(226, 228, 232), muted(138, 142, 150), border(74, 77, 83);
    const QColor accent(38, 166, 154);      // teal, to go with the canvas

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Button, raised);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, raised);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, accent);
    palette.setColor(QPalette::Light, border.lighter(120));
    palette.setColor(QPalette::Midlight, border);
    palette.setColor(QPalette::Mid, border);
    palette.setColor(QPalette::Dark, base.darker(130));
    palette.setColor(QPalette::Shadow, Qt::black);

    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, muted.darker(130));
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, border);

    app.setPalette(palette);

    // Checkable buttons (the toolbar's component buttons) get a clear "this is the active tool" frame
    app.setStyleSheet(QStringLiteral(
                          "QPushButton:checked { border: 2px solid %1; border-radius: 5px; background-color: %2; }")
                          .arg(accent.name(), base.name()));
}
