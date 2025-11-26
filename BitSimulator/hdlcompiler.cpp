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
    auto plaholderCode = R"(module adder (
    input  wire [3:0] digit,   // UNSIGNED(3 downto 0)
    input  wire       clk,
    output reg  [2:0] state,   // INTEGER RANGE 0 TO 6
    output reg  [6:0] seg7
);

    reg [6:0] reg_seg;
    reg [6:0] data;
    reg [2:0] stt = 0;

    // State output
    always @(*) begin
        state = stt;
    end

    // Combinational decoder
    always @(*) begin
        case (digit)
            4'b0000: data = ~7'b1000000; // '0'
            4'b0001: data = ~7'b1111001; // '1'
            4'b0010: data = ~7'b0100100; // '2'
            4'b0011: data = ~7'b0110000; // '3'
            4'b0100: data = ~7'b0011001; // '4'
            4'b0101: data = ~7'b0010010; // '5'
            4'b0110: data = ~7'b0000010; // '6'
            4'b0111: data = ~7'b1111000; // '7'
            4'b1000: data = ~7'b0000000; // '8'
            4'b1001: data = ~7'b0010000; // '9'
            4'b1010: data = ~7'b0001000; // 'A'
            4'b1011: data = ~7'b0000011; // 'B'
            4'b1100: data = ~7'b1000110; // 'C'
            4'b1101: data = ~7'b0100001; // 'D'
            4'b1110: data = ~7'b0000110; // 'E'
            4'b1111: data = ~7'b0001110; // 'F'
            default: data = 7'b0000000;
        endcase
    end

    // Clocked latch of decoded value
    always @(posedge clk) begin
        reg_seg <= data;
    end

    // Output mux + counter
    always @(posedge clk) begin
        case (stt)
            0: seg7 <= {reg_seg[6], 6'b000000};
            1: seg7 <= {1'b0, reg_seg[5], 5'b00000};
            2: seg7 <= {2'b00, reg_seg[4], 4'b0000};
            3: seg7 <= {3'b000, reg_seg[3], 3'b000};
            4: seg7 <= {4'b0000, reg_seg[2], 2'b00};
            5: seg7 <= {5'b00000, reg_seg[1], 1'b0};
            6: seg7 <= {6'b000000, reg_seg[0]};
            default: seg7 <= 7'b0000000;
        endcase

        // increment stt
        if (stt == 6)
            stt <= 0;
        else
            stt <= stt + 1;
    end

endmodule
)";

    QFile file("code.v");  // No parent needed - local scope
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
    out << plaholderCode;
    file.close();
    qDebug() << "HDL code written to file";

    // Run Yosys synthesis
    QStringList arguments;
    arguments << "-p" << "read_verilog code.v; synth -top adder -noalumacc; abc -g AND,OR,XOR,NAND,NOR,XNOR; write_json code_netlist.json; show -format dot -format svg -prefix code_graph";
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
    Process->start("dot", dotArgs);

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

    // Parse the positioned dot file
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
        // Check condition on first line
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

                        // Split by comma to get x,y
                        QStringList coords = posValue.split(',');
                        if (coords.size() == 2) {
                            double x = coords[0].toDouble();
                            double y = coords[1].toDouble();

                            minX = std::min(minX, x);
                            minY = std::min(minY, y);
                            maxX = std::max(maxX, x);
                            maxY = std::max(maxY, y);

                            node.position.setX(x);
                            node.position.setY(y); // Will flip Y later when we have graphHeight

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

    // Your scene dimensions (from CircuitScene constructor)
    const double sceneWidth = 16000.0;
    const double sceneHeight = 10000.0;
    const double sceneCenterX = sceneWidth / 2.0;
    const double sceneCenterY = sceneHeight / 2.0;

    qDebug() << "Graph bounds: (" << minX << "," << minY << ") to (" << maxX << "," << maxY << ")";
    qDebug() << "Graph center:" << graphCenterX << "," << graphCenterY;

    // Transform all node positions
    for (auto it = m_componentsPos.begin(); it != m_componentsPos.end(); ++it) {
        Node& node = it.value();
        double x = node.position.x();
        double y = node.position.y();

        // 1. Flip Y coordinate (Graphviz origin is bottom-left, Qt is top-left)
        y = maxY - y;

        // 2. Translate to center the graph in the scene
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
        
        // First pass: Build bit-to-net mapping from netnames  NOT NEEDED
        if (module.contains("netnames") && module["netnames"].isObject()) {
            QJsonObject netnames = module["netnames"].toObject();
            
            for (auto netIt = netnames.begin(); netIt != netnames.end(); ++netIt) {
                QString netName = netIt.key();
                QJsonObject netData = netIt.value().toObject();
                
                // Only add to mapping if hide_name is 0 (false)
                bool hideNames = netData["hide_name"].toInt() == 1;
                if (!hideNames && netData.contains("bits") && netData["bits"].isArray()) {
                    QJsonArray bits = netData["bits"].toArray();
                    for (const QJsonValue& bitVal : bits) {
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
                
                // Determine I/O type
                if (direction == "input") {
                    ioNode.type = IOType::IN;
                } else if (direction == "output") {
                    ioNode.type = IOType::OUT;
                }

                ioNode.position = m_componentsPos[portName].position;

                // Create a port for this I/O
                Port ioPort;
                ioPort.parent = nullptr; // Will be set after insertion
                ioPort.name = portName;
                ioPort.type = (direction == "input") ? PortType::OUT : PortType::IN; // Note: reversed for I/O nodes
                
                // Store bit connections
                for (const QJsonValue& bitVal : bits) {
                    ioPort.connections.append(bitVal.toInt());
                }
                
                ioNode.ports[portName] = ioPort;
                
                m_components[portName] = ioNode;
                qDebug() << "Created I/O node:" << portName << "direction:" << direction << "bits:" << bits;
            }
        }
        
        // Third pass: Process cells (logic gates, flip-flops, etc.)
        if (module.contains("cells") && module["cells"].isObject()) {
            QJsonObject cells = module["cells"].toObject();
            qDebug() << "\nProcessing" << cells.size() << "cells...";
            
            for (auto cellIt = cells.begin(); cellIt != cells.end(); ++cellIt) {
                QString cellName = cellIt.key();
                QJsonObject cellData = cellIt.value().toObject();
                
                QString cellType = cellData["type"].toString();
                
                // Extract numeric ID from cell name (e.g., "517" from "$abc$516$auto$blifparse.cc:397:parse_blif$517")
                QString cellId = cellName;
                QRegularExpression re(R"(\$(\d+)(?!.*\$\d))"); // Last number in the string
                QRegularExpressionMatch match = re.match(cellName);
                if (match.hasMatch()) {
                    cellId = match.captured(1);
                }
                
                Node node;
                node.id = cellId;
                
                // Determine node type based on cell type
                if (cellType == "$_DFF_P_" || cellType == "$_DFF_N_" || 
                    cellType == "$_SDFF_PN0_" || cellType == "$_SDFF_PN1_") {
                    node.type = RType::D; // D flip-flop
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
                    continue; // Skip unknown types for now
                }
                
                // Copy position if available from dot file
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
                        port.parent = nullptr; // Will be updated after insertion
                        port.name = portName;
                        
                        // Determine port direction
                        QString direction = portDirs[portName].toString();
                        if (direction == "input") {
                            port.type = PortType::IN;
                        } else if (direction == "output") {
                            port.type = PortType::OUT;
                        }
                        
                        // Store bit numbers as connections
                        for (const QJsonValue& bitVal : bitArray) {
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
