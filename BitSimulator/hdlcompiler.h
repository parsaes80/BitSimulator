#pragma once
#include <QPlainTextEdit>
#include <QTemporaryFile>
#include <QProcess>
#include <QRegularExpression>
#include <QString>

class HDLCompiler : public QObject{
    Q_OBJECT
public:
    explicit HDLCompiler(QObject* parent = nullptr) : QObject(parent) {}
   
public slots:
    void receiveCode(const QString& code);

signals:
    void error(const QString& err);
    void success();

private:
    void compile(const QString& verilog);
    QString yosysPath = "./yosys_bundle/bin/yosys";
};

class TextEditor : public QPlainTextEdit{
    Q_OBJECT
public:
    explicit TextEditor(QWidget *parent = nullptr);
    explicit TextEditor(const QString &text, QWidget *parent = nullptr);
    void onSendCode() {emit sendCode(toPlainText());}
signals:
    void sendCode(const QString& code);
};
