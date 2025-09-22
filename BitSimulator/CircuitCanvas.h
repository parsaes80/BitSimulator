#pragma once
#include <unordered_map>
#include <vector>
#include <memory>
#include <set>
#include "components.h"
#include <QPoint>
#include <QRect>
#include <QColor>
#include <QPainter>
#include <QWidget>
#include <QMouseEvent>

// Base class for all circuit nodes
class CircuitNode {
public:
    CircuitNode() = default;
    CircuitNode(QPoint pos) : m_pos(pos) {}
    QPoint m_pos;
};

// Circuit connection (wire/net)
class CircuitEdge {
public:
    CircuitEdge(QPoint startpos, QPoint endpos): m_startPos(startpos), m_endPos(endpos) {}
    QPoint m_endPos;
    QPoint m_startPos;
private:
    CircuitNode* m_source;
    std::vector<CircuitNode*> m_destinations;
};

// Main circuit graph
class CircuitGraph {
public:
    void addNode();
};

class CircuitCanvas : public QWidget {
    Q_OBJECT
public:
    CircuitCanvas(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(800, 600);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    std::vector<CircuitNode> m_nodes;
    std::vector<CircuitEdge> m_edges;
};
