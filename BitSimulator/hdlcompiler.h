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

struct Node;

struct Port {
    Node* parent;
    QString name;         // e.g., "C", "D", "Q"
    PortType type;
    QList<int> connections;
};

struct Node{
    QString id;
    QPointF position;
    QHash<QString,Port> ports;
    bool HasReset = false;
    std::variant<RType,GType,MType,IOType> type;
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
    void finished(bool ok);   // always emitted once per receiveCode(), after any error() or sendGraph()
    void sendGraph(const QHash<QString,Node> graph);

private:
    bool compile(const QString& hdlCode);
    bool parseDotFile(const QString& filePath);
    bool processJsonFile(const QString& filePath);
#ifdef Q_OS_WIN
    // script.bat loads oss-cad-suite\environment.bat, then runs yosys (its command must match the Linux one in compile())
    QString yosysScript = "script.bat";
    QString dotPath = ".\\dot.exe";           // Graphviz, built from third_party/graphviz next to the app
#else
    QString yosysPath = "./yosys_bundle/bin/yosys";
    QString dotPath = "./dot";                  // Graphviz, built from third_party/graphviz next to the app
#endif
    QHash<QString,Node> m_componentsPos;
    QHash<QString,Node> m_components;
    QHash<int, QString> m_bitToNet;  // Maps bit numbers to net names
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
