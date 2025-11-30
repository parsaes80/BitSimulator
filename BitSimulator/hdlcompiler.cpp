#include "hdlcompiler.h"
#include <QFont>
#include <QFontMetrics>
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QRegularExpression>

//===================== HDLCompiler ========================

bool HDLCompiler::compile(const QString& hdlCode) {

    QFile file("code.v");  
    QProcess *Process = new QProcess(this);

    // Write HDL code to file
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file for writing:" << file.errorString();
        emit error("Failed to create file: " + file.errorString());
        Process->deleteLater();
        return false;
    }

    qDebug() << "Created file:" << file.fileName();
    QTextStream out(&file);
    out << hdlCode;
    file.close();
    qDebug() << "HDL code written to file";

    // Run Yosys synthesis
    QStringList arguments;
    arguments << "-p" << "read_verilog code.v; synth -top top -noalumacc; abc -g AND,OR,XOR,NAND,NOR,XNOR; write_json code_netlist.json; show -format dot -format svg -prefix code_graph";
    Process->start(yosysPath, arguments);

    if (!Process->waitForFinished(30000)) {
        qDebug() << "Process timeout or failed to start";
        emit error("Yosys process timeout");
        return false;
    }

    if (Process->exitCode() != 0) {
        qDebug() << "Yosys compilation failed";
        qDebug() << "Error:" << Process->readAllStandardError();
        emit error("Compilation failed: " + Process->readAllStandardError());
        return false;
    }

    qDebug() << "Yosys compilation successful";

    QStringList dotArgs;

    // Output a positioned .dot file (plain format has coordinates)
    dotArgs << "-Tdot" << "code_graph.dot" << "-o" << "code_graph_positioned.dot";
    Process->start("./dot", dotArgs);

    if (!Process->waitForFinished(10000)) {
        qDebug() << "Dot process timeout";
        Process->deleteLater();
        emit success();
        return false;
    }

    if (Process->exitCode() != 0) {
        qDebug() << "Dot layout failed:" << Process->readAllStandardError();
        Process->deleteLater();
        emit success();
        return false;
    }

    Process->deleteLater();
    qDebug() << "Graph layout computed";

    if(!parseDotFile("code_graph_positioned.dot")) return false;
    if(!processJsonFile("code_netlist.json")) return false;

    emit sendGraph(m_components);
    emit success();
    return true;
}

bool HDLCompiler::parseDotFile(const QString& filePath) {
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    QStringList lines;

    while (!in.atEnd()) {
        lines.append(in.readLine());
    }
    file.close();

    // first pass remove tab and slash
    for (int i = 0; i < lines.size() - 1; ++i) {
        QString& currLine = lines[i];
        QString& nextLine = lines[i + 1];

        while(nextLine.startsWith("\t")){nextLine.removeFirst();};
        if (currLine.endsWith("\\")) {
            currLine.removeLast();
            currLine.append(nextLine);
            lines.removeAt(i+1);
            while(lines[i + 1].startsWith("\t")){nextLine.removeFirst();};
        }
    }
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();

    //second pass extract nodes and graph size
    for (int i = 0; i < lines.size() - 1; ++i) {
        QString& currLine = lines[i];

        if (currLine.contains("\t")){
            QStringList firstLine = currLine.split("\t");
            QString& firstLineStart = firstLine[0];
            if(!firstLineStart.contains("->") && !firstLineStart.startsWith('x')){ //skip edges and bitslice and junction
                Node node;
                node.id = firstLineStart;
                for(int j=i+1;!lines[j].contains("\t") && j< lines.size();j++){ // lines of a signle node
                    QString currNodeLine = lines[j];
                    if(currNodeLine.startsWith("label=")){
                        // Extract numeric ID from label like: label="{{<p128> C|<p129> D}|$317\n$_DFF_P_|{<p131> Q}}",
                        QRegularExpression labelRe(R"(\$(\d+))");
                        QRegularExpressionMatch labelMatch = labelRe.match(currNodeLine);
                        if (labelMatch.hasMatch()) {
                            node.id = labelMatch.captured(1);
                        } else {
                            node.id = currNodeLine.split('=')[1].removeLast();
                        }
                    }
                    if(currNodeLine.startsWith("pos=")){\
                        QString posLine = currNodeLine;

                        // Extract the value between quotes
                        int firstQuote = posLine.indexOf('"');
                        int lastQuote = posLine.lastIndexOf('"');
                        QString posValue = posLine.mid(firstQuote + 1, lastQuote - firstQuote - 1);

                        QStringList coords = posValue.split(',');
                        if (coords.size() == 2) {
                            double x = coords[0].toDouble();
                            double y = coords[1].toDouble();

                            minX = std::min(minX, x);
                            minY = std::min(minY, y);
                            maxX = std::max(maxX, x);
                            maxY = std::max(maxY, y);

                            node.position.setX(x);
                            node.position.setY(y); 

                            qDebug() << node.id<< " :" << x << "," << y;
                        }
                    }
                }
                m_componentsPos[node.id]=node;
            }
        }
    }

    double graphCenterX = (minX + maxX) / 2.0;
    double graphCenterY = (minY + maxY) / 2.0;

    // CircuitScene dimentions
    const double sceneWidth = 160000.0;
    const double sceneHeight = 100000.0;
    const double sceneCenterX = sceneWidth / 2.0;
    const double sceneCenterY = sceneHeight / 2.0;

    qDebug() << "Graph bounds: (" << minX << "," << minY << ") to (" << maxX << "," << maxY << ")";
    qDebug() << "Graph center:" << graphCenterX << "," << graphCenterY;

    // Transform all node positions
    for (auto it = m_componentsPos.begin(); it != m_componentsPos.end(); ++it) {
        Node& node = it.value();
        double x = node.position.x();
        double y = node.position.y();

        // Flip Y coordinate (Graphviz origin is bottom-left, Qt is top-left)
        y = maxY - y;

        // Translate to center the graph in the scene
        x = x - graphCenterX + sceneCenterX;
        y = y - graphCenterY + sceneCenterY;

        node.position.setX(x);
        node.position.setY(y);

        qDebug() << "Transformed position for" << node.id << ":" << x << "," << y;
    }
    return true;
}

bool HDLCompiler::processJsonFile(const QString& filePath) {
    QFile file(filePath);
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open JSON file:" << file.errorString();
        emit error("Failed to open JSON file: " + file.errorString());
        return false;
    }

    QByteArray jsonData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qDebug() << "JSON parse error:" << parseError.errorString();
        emit error("JSON parse error: " + parseError.errorString());
        return false;
    }

    if (!doc.isObject()) {
        qDebug() << "JSON root is not an object";
        emit error("Invalid JSON structure: root is not an object");
        return false;
    }

    QJsonObject root = doc.object();
    
    // Parse modules
    if (!root.contains("modules") || !root["modules"].isObject()) {
        qDebug() << "No modules found in JSON";
        emit error("No modules found in JSON");
        return false;
    }

    QJsonObject modules = root["modules"].toObject();
    
    // Process each module
    for (auto moduleIt = modules.begin(); moduleIt != modules.end(); ++moduleIt) {
        QString moduleName = moduleIt.key();
        QJsonObject module = moduleIt.value().toObject();
        
        qDebug() << "\n=== Processing Module:" << moduleName << "===";
        
        // First pass: Build bit-to-net mapping from netnames  NOT NEEDED FOR NOW
        if (module.contains("netnames") && module["netnames"].isObject()) {
            QJsonObject netnames = module["netnames"].toObject();
            
            for (auto netIt = netnames.begin(); netIt != netnames.end(); ++netIt) {
                QString netName = netIt.key();
                QJsonObject netData = netIt.value().toObject();
                
                // Only add to mapping if hide_name is 1
                bool hideNames = netData["hide_name"].toInt() == 1;
                if (!hideNames && netData.contains("bits") && netData["bits"].isArray()) {
                    QJsonArray bits = netData["bits"].toArray();
                    for (const QJsonValue& bitVal : std::as_const(bits)) {
                        int bitNum = bitVal.toInt();
                        m_bitToNet[bitNum] = netName;
                    }
                }
            }
            qDebug() << "Built bit-to-net mapping:" << m_bitToNet.size() << "bits";
        }
        
        // Second pass: Create nodes for module ports (I/O)
        if (module.contains("ports") && module["ports"].isObject()) {
            QJsonObject ports = module["ports"].toObject();
            
            for (auto portIt = ports.begin(); portIt != ports.end(); ++portIt) {
                QString portName = portIt.key();
                QJsonObject portData = portIt.value().toObject();
                
                QString direction = portData["direction"].toString();
                QJsonArray bits = portData["bits"].toArray();
                
                // Create an I/O node for each module port
                Node ioNode;
                ioNode.id = portName;
                
                
                if (direction == "input") {
                    ioNode.type = IOType::IN;
                } else if (direction == "output") {
                    ioNode.type = IOType::OUT;
                }

                ioNode.position = m_componentsPos[portName].position;

                // Create a port for this I/O
                Port ioPort;
                ioPort.parent = nullptr; 
                ioPort.name = portName;
                ioPort.type = (direction == "input") ? PortType::OUT : PortType::IN; 
                
                // Store bit connections
                for (const QJsonValue& bitVal : std::as_const(bits)) {
                    ioPort.connections.append(bitVal.toInt());
                }
                
                ioNode.ports[portName] = ioPort;
                
                m_components[portName] = ioNode;
                qDebug() << "Created I/O node:" << portName << "direction:" << direction << "bits:" << bits;
            }
        }
        
        // Third pass: Process cells 
        if (module.contains("cells") && module["cells"].isObject()) {
            QJsonObject cells = module["cells"].toObject();
            qDebug() << "\nProcessing" << cells.size() << "cells...";
            
            for (auto cellIt = cells.begin(); cellIt != cells.end(); ++cellIt) {
                QString cellName = cellIt.key();
                QJsonObject cellData = cellIt.value().toObject();
                
                QString cellType = cellData["type"].toString();
                
                // Extract numeric ID from cell name 
                QString cellId = cellName;
                QRegularExpression re(R"(\$(\d+)(?!.*\$\d))"); // Last number in the string
                QRegularExpressionMatch match = re.match(cellName);
                if (match.hasMatch()) {
                    cellId = match.captured(1);
                }
                
                Node node;
                node.id = cellId;
                // Determine node type based on cell type
                if (cellType.contains("DFF")) {
                    node.type = RType::D; 
                    if (cellType.contains("SDFF")) {
                        node.HasReset = true;
                    }
                } else if (cellType == "$_NOT_") {
                    node.type = GType::NOT;
                } else if (cellType == "$_AND_") {
                    node.type = GType::AND;
                } else if (cellType == "$_OR_") {
                    node.type = GType::OR;
                } else if (cellType == "$_XOR_") {
                    node.type = GType::XOR;
                } else if (cellType == "$_NAND_") {
                    node.type = GType::NAND;
                } else if (cellType == "$_NOR_") {
                    node.type = GType::NOR;
                } else if (cellType == "$_XNOR_") {
                    node.type = GType::XNOR;
                } else if (cellType == "$_MUX_") {
                    node.type = MType::MUX;
                } else {
                    // Unknown type - use a generic marker
                    qDebug() << "Warning: Unknown cell type:" << cellType;
                    continue; // Skip unknown types 
                }
                
                // Copy position from dot file
                if (m_componentsPos.contains(cellId)) {
                    node.position = m_componentsPos[cellId].position;
                }
                
                // Process connections to create ports
                if (cellData.contains("connections") && cellData["connections"].isObject()) {
                    QJsonObject connections = cellData["connections"].toObject();
                    QJsonObject portDirs = cellData["port_directions"].toObject();
                    
                    for (auto connIt = connections.begin(); connIt != connections.end(); ++connIt) {
                        QString portName = connIt.key();
                        QJsonArray bitArray = connIt.value().toArray();
                        
                        Port port;
                        port.parent = nullptr; 
                        port.name = portName;
                        
                        // Determine port direction
                        QString direction = portDirs[portName].toString();
                        if (direction == "input") {
                            port.type = PortType::IN;
                        } else if (direction == "output") {
                            port.type = PortType::OUT;
                        }
                        
                        // Store bit numbers as connections
                        for (const QJsonValue& bitVal : std::as_const(bitArray)) {
                            port.connections.append(bitVal.toInt());
                        }
                        
                        node.ports[portName] = port;
                    }
                }
                
                m_components[cellId] = node;
                qDebug() << "Created node:" << cellId << "type:" << cellType << "ports:" << node.ports.size();
            }
        }
    }
    
    // Update parent pointers in all ports
    for (auto it = m_components.begin(); it != m_components.end(); ++it) {
        for (auto portIt = it.value().ports.begin(); portIt != it.value().ports.end(); ++portIt) {
            portIt.value().parent = &it.value();
        }
    }
    
    qDebug() << "\n=== JSON processing complete ===";
    qDebug() << "Total components:" << m_components.size();
    qDebug() << "Total nets:" << m_bitToNet.size();
    return true;
}

void HDLCompiler::receiveCode(const QString& hdlCode) {
    compile(hdlCode);
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

    setLineWrapMode(QPlainTextEdit::NoWrap);

    setPlaceholderText("Enter your Verilog code here...");
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

    setLineWrapMode(QPlainTextEdit::NoWrap);
}
