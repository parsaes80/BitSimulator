#pragma once
#include <QColor>
#include "general.h"

// The colors the canvas, its components and the toolbar icons are painted with.
//
// Edit them in BitSimulator.ui, not here: the main window's style sheet sets every one of them
// through the "CircuitCanvas { qproperty-... }" rule. The values below are only the fallback for
// code that runs without that style sheet (the tests, for example), and match it.
struct CanvasTheme {
    QColor background{0x0a, 0x0e, 0x11};
    QColor dot{0x1e, 0x28, 0x2e};            // the dot grid on the canvas

    // Components: a dark body with an outline in the component's own color
    QColor componentFill{0x14, 0x1b, 0x1f};
    QColor componentText{0xc3, 0xce, 0xd4};
    QColor andGate{0xe8, 0xb0, 0x5a};        // also NAND
    QColor orGate{0x5f, 0xd3, 0xa5};         // also NOR
    QColor xorGate{0xa7, 0x8b, 0xfa};        // also XNOR
    QColor notGate{0xf2, 0x7c, 0x7c};
    QColor mux{0xf5, 0x9e, 0x5b};
    QColor reg{0x7a, 0xa2, 0xf7};
    QColor source{0x9f, 0xb0, 0xb8};
    QColor display{0x67, 0xe8, 0xf9};

    // Wires: one color while editing; while the simulation runs, a 1 lights up and a 0 dims
    QColor wire{0xc3, 0xce, 0xd4};
    QColor wireHigh{0x2d, 0xd4, 0xbf};
    QColor wireLow{0x3b, 0x4a, 0x52};

    // Pins, selection, and the wire being drawn
    QColor pin{0x4a, 0x59, 0x60};            // nothing connected
    QColor pinWired{0x14, 0xb8, 0xa6};
    QColor selection{0x2d, 0xd4, 0xbf};
    QColor drag{0x7a, 0x88, 0x90};           // a wire being dragged, not over a pin it can connect to
    QColor snap{0xfd, 0xe6, 0x8a};           // ... over a pin it can connect to, and that pin

    QColor gateColor(GType type) const
    {
        switch (type) {
        case GType::AND: case GType::NAND: return andGate;
        case GType::OR:  case GType::NOR:  return orGate;
        case GType::XOR: case GType::XNOR: return xorGate;
        case GType::NOT: return notGate;
        }
        return componentText;
    }
};

// The one theme everything paints with. CircuitCanvas's properties write into it.
inline CanvasTheme& canvasTheme()
{
    static CanvasTheme theme;
    return theme;
}
