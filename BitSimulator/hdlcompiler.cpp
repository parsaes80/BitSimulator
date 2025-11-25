#include "hdlcompiler.h"
#include <QFont>
#include <QFontMetrics>
#include <QProcess>
#include <QFile>
#include <QTextStream>

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
    arguments << "-p" << "read_verilog code.v; synth -top adder; show -format dot -prefix code_graph";
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
    emit graphReady(m_components);
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

    //second pass extract nodes
    for (int i = 0; i < lines.size() - 1; ++i) {
        QString& currLine = lines[i];

        if (currLine.contains("\t")){
            QStringList res = currLine.split("\t");
            QString& id = res[0];
            if(!id.contains("->")){ //skip edges
                qDebug() << '\n'<<id;
                Node node;
                node.id = id;
                int labelIndex;
                for(int j=i+1;!lines[j].contains("\t") && j< lines.size();j++){ // lines of a signle node
                    QString currNodeLine = lines[j];
                    if(currNodeLine.startsWith("shape=diamond")){ // signal or variable
                        node.type =  IOType::OUT;
                        node.id = lines[labelIndex].split("=")[1].removeLast();
                    }
                    if(currNodeLine.startsWith("shape=point")){ // junction
                        node.type = false;
                    }
                    if(currNodeLine.startsWith("style=rounded")){// slice

                        QRegularExpression re(R"((\d):(\d)\s*-\s*(\d):(\d))");
                        QRegularExpressionMatch match = re.match(lines[labelIndex]);
                        int res = -1;
                        if (match.hasMatch()) {
                            int a = match.captured(1).toInt();  // 0
                            int b = match.captured(2).toInt();  // 0
                            int c = match.captured(3).toInt();  // 1
                            int d = match.captured(4).toInt();  // 1

                            res= (a*1000) + (b*100)+ (c*10) + d;
                        }
                        node.type = res;
                        qDebug() << res;
                    }
                    if(currNodeLine.startsWith("shape=octagon")){ // IO
                        node.type = IOType::IN;
                        node.id = lines[labelIndex].split("=")[1].removeLast();
                    }
                    if(currNodeLine.startsWith("label=")){
                        labelIndex = j;
                        if (currNodeLine.contains("_NOT_")){
                            node.type = GType::NOT;
                            qDebug() << "Found NOT gate";
                        }
                        else if (currNodeLine.contains("_AND_")){
                            node.type = GType::AND;
                            qDebug() << "Found AND gate";
                        }
                        else if (currNodeLine.contains("_NAND_")){
                            node.type = GType::NAND;
                            qDebug() << "Found NAND gate";
                        }
                        else if (currNodeLine.contains("_OR_")){
                            node.type = GType::OR;
                            qDebug() << "Found OR gate";
                        }
                        else if (currNodeLine.contains("_NOR_")){
                            node.type = GType::NOR;
                            qDebug() << "Found NOR gate";
                        }
                        else if (currNodeLine.contains("_XOR_")){
                            node.type = GType::XOR;
                            qDebug() << "Found XOR gate";
                        }
                        else if (currNodeLine.contains("_XNOR_")){
                            node.type = GType::XNOR;
                            qDebug() << "Found XNOR gate";
                        }
                        else if (currNodeLine.contains("_MUX_")){
                            node.type = MType::MUX;
                            qDebug() << "Found MUX gate";
                        }
                        else if (currNodeLine.contains("_ORNOT_")){ // CHANGE LATERR
                            node.type = GType::NOR;
                            node.SecNotGate = true;
                            qDebug() << "Found ORNOT gate";
                        }
                        else if (currNodeLine.contains("_ANDNOT_")){ // CHANGE LATERR
                            node.type = GType::NAND;
                            node.SecNotGate = true;
                            qDebug() << "Found ANDNOT gate";
                        }
                        else if (currNodeLine.contains("_SDFF_")){ // CHANGE LATERR
                            node.type = RType::D;
                            node.HasReset = true;
                            qDebug() << "Found SDFF (Synchronous D FlipFlop)"; //has sync reset
                        }
                        else if (currNodeLine.contains("_DFF_P_")){ // CHANGE LATERR
                            node.type = RType::D;
                            qDebug() << "Found DFF_P (D FlipFlop Positive edge)"; //dosnt have sync reset
                        }

                        QRegularExpression re(R"(<([^>]+)>\s*([A-Za-z0-9_]+))");
                        QRegularExpressionMatchIterator it = re.globalMatch(currNodeLine);

                        while (it.hasNext() && !id.startsWith('x')) {
                            Port port;
                            auto m = it.next();
                            port.id = m.captured(1);
                            port.name = m.captured(2);
                            port.type = (port.name == "Q") || (port.name =="Y") ?  PortType::OUT : PortType::IN;
                            node.ports[port.id] = port;
                            QString Type = port.type == PortType::OUT ?  "out" : "in";
                            qDebug() << port.id  <<" "<< port.name << " "<< Type;
                        }
                    }
                    else if(currNodeLine.startsWith("pos=")){
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

                            // Convert from Graphviz coordinates (origin bottom-left) to Qt (origin top-left)
                            // You'll need the graph height - parse it from the bb attribute first
                            node.position.setX(x);
                            node.position.setY(y); // Will flip Y later when we have graphHeight

                            qDebug() << "Parsed position:" << x << "," << y;
                        }
                    }
                }
                m_components[id]=node;
            }
        }
    }

    //third pass exctract edges
    for (int i = 0; i < lines.size() - 1; ++i) {
        QString currLine = lines[i];
        if (currLine.contains("\t") && currLine.contains("->")){
            QString firstHalf = currLine.split('\t')[0];
            QRegularExpression re(R"((\w+):(\w+)(?::(\w+))?\s*->\s*(\w+):(\w+)(?::(\w+))?)");
            QRegularExpressionMatch m = re.match(firstHalf);

            if (m.hasMatch()) {
                QString startID = m.captured(1);
                QString startPart2 = m.captured(2);    // Could be port OR direction
                QString startPart3 = m.captured(3);    // Could be direction OR empty
                QString endID = m.captured(4);
                QString endPart2 = m.captured(5);      // Could be port OR direction
                QString endPart3 = m.captured(6);      // Could be direction OR empty

                QString startPortID = startPart3.isEmpty() ? startPart2 : startPart2;
                QString startDir = startPart3.isEmpty() ? "" : startPart3;

                QString endPortID = endPart3.isEmpty() ? endPart2 : endPart2;
                QString endDir = endPart3.isEmpty() ? "" : endPart3;

                qDebug() << startID << ":" << startPortID
                         << (startDir.isEmpty() ? "" : ":" + startDir)
                         << " -> " << endID << ":" << endPortID
                         << (endDir.isEmpty() ? "" : ":" + endDir);

                if (std::holds_alternative<IOType>(m_components[endID].type)) {
                    m_components[endID].type = IOType::OUT;
                }

                auto& endPort = m_components[endID].ports[endPortID];
                auto& startPort = m_components[startID].ports[startPortID];
                startPort.connections.append(&endPort);
                endPort.connections.append(&startPort);
            }
        }
    }
    for (auto& node : m_components) {
        for (auto& port : node.ports) {
            port.parent = &node;
        }
    }

    for(auto& comp: m_components){
        if (!std::holds_alternative<IOType>(comp.type)){
            continue;
        }
        for (auto& port: comp.ports){
            for (auto* otherPort:port.connections){
                auto* otherComp = otherPort->parent;
                if(std::holds_alternative<int>(otherComp->type)){
                    int bitSliceVal = std::get<int>(otherComp->type);
                    int first = (bitSliceVal / 1000) % 10;
                    int second = (bitSliceVal / 100) % 10;
                    int third = (bitSliceVal / 10) % 10;
                    int fourth = bitSliceVal % 10;

                }
            }
        }
    }

    return true;
}
bool HDLCompiler::processDotFile() {
    ;
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
