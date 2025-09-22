#include "CircuitCanvas.h"


void CircuitCanvas::paintEvent(QPaintEvent *event)  {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::green); // background

    painter.setPen(Qt::black);
    // Example: draw a couple of edges
    for (auto &edge : m_edges) {
        painter.drawLine(edge.m_startPos.x(), edge.m_startPos.y(),
                         edge.m_endPos.x(), edge.m_endPos.y());
    }

    // Example: draw a node
    painter.setBrush(Qt::gray);
    for (auto &node : m_nodes) {
        painter.drawEllipse(node.m_pos, 20, 20);
    }
}

void CircuitCanvas::mousePressEvent(QMouseEvent *event)  {
    // Example: place a node where the user clicks
    if (event->button() == Qt::LeftButton) {
        m_nodes.push_back(CircuitNode(event->pos()));
        update(); // trigger repaint
    }
}
