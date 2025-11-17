#pragma once
#include <QPlainTextEdit>
#include <QTemporaryFile>
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QPointF>
#include <QMap>
#include <QList>
#include <variant>
#include "general.h"

struct Port {
    QString id;           // e.g., "p128"
    QString name;         // e.g., "C", "D", "Q"
    PortType type;
    QList<QString> connections;
};
struct Node{
    QString id;
    std::variant<RType,GType,MType,bool> type;
    QPointF position;
    QList<Port> ports;
};

class HDLCompiler : public QObject{
    Q_OBJECT
public:
    explicit HDLCompiler(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    void receiveCode(const QString& code);

signals:
    void error(const QString& err);
    void success();
    void graphReady(const QList<Node>& graph);

private:
    bool compile(const QString& verilog);
    bool parseDotFile(const QString& filePath);
    QString yosysPath = "./yosys_bundle/bin/yosys";
    QList<Node> m_components; //maybe hashmap?
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
