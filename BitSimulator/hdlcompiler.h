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
    QPointF position;
    QHash<QString,Port> ports;
    u16 BitSlice;
    bool HasReset = false;
    bool SecNotGate = false;
    u8 bitWidth;
    std::variant<RType,GType,MType,IOType,bool> type; //bool for bit slice
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
    void graphReady(const QHash<QString,Node>& graph);

private:
    bool compile(const QString& hdlCode);
    bool parseDotFile(const QString& filePath);
    QString yosysPath = "./yosys_bundle/bin/yosys";
    QHash<QString,Node> m_components;
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
