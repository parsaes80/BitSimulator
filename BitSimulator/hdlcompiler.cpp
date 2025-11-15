#include "hdlcompiler.h"
#include <QFont>
#include <QFontMetrics>
#include <QProcess>
#include <QFile>
#include <QTextStream>

//===================== HDLCompiler ========================

void HDLCompiler::compile(const QString& hdlCode) {

    QFile file("code.v");  // No parent needed - local scope
    QProcess *Process = new QProcess(this);

    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Created file:" << file.fileName();
        
        QTextStream out(&file);
        out << hdlCode;
        file.close();
        
        QStringList arguments;
        arguments << "-p" << "read_verilog code.v; synth -top adder; show -format dot -prefix code_graph";
        Process->start(yosysPath, arguments);
        
        // Wait for the process to finish (blocking)
        if (Process->waitForFinished(30000)) { // 30 second timeout
            if (Process->exitCode() == 0) {
                qDebug() << "Yosys compilation successful";                
                emit success();
            } else {
                qDebug() << "Yosys compilation failed";
                qDebug() << "Error:" << Process->readAllStandardError();
                emit error("Compilation failed: " + Process->readAllStandardError());
            }
        } else {
            qDebug() << "Process timeout or failed to start";
            emit error("Process timeout");
        }

        qDebug() << "HDL code written to file";

    } else {
        qDebug() << "Failed to open file for writing:" << file.errorString();
    }
}

void HDLCompiler::receiveCode(const QString& code) {
    compile(code);
}

//===================== TextEditor ========================

TextEditor::TextEditor(QWidget *parent) : QPlainTextEdit(parent) {
    // Set monospace font for code
    QFont font("Courier");
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(12);
    setFont(font);

    // Set tab width (4 spaces)
    QFontMetrics metrics(font);
    setTabStopDistance(4 * metrics.horizontalAdvance(' '));

    // Enable line wrapping
    setLineWrapMode(QPlainTextEdit::NoWrap);

    // Set placeholder text
    setPlaceholderText("Enter your Verilog or VHDL code here...");
}

TextEditor::TextEditor(const QString &text, QWidget *parent) : QPlainTextEdit(text, parent) {
    // Set monospace font for code
    QFont font("Courier");
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(10);
    setFont(font);

    // Set tab width (4 spaces)
    QFontMetrics metrics(font);
    setTabStopDistance(4 * metrics.horizontalAdvance(' '));

    // Enable line wrapping
    setLineWrapMode(QPlainTextEdit::NoWrap);
}
