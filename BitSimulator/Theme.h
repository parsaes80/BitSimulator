#pragma once
// How the app looks: the colors of dark and light mode, the style sheet the widgets are drawn with,
// and the colors the canvas, its components and the toolbar icons are painted with.
//
// Everything that can be set in Qt Designer stays in BitSimulator.ui (layout, sizes, texts).
// Everything else about the look is here and in Theme.cpp:
//   - a color:            makeTheme() in Theme.cpp, one line per color with its dark and light value
//   - a widget's style:   the style sheet in Theme.cpp, which names colors as @name (a field of Theme)

#include <QColor>
#include <QIcon>
#include <QString>
#include "general.h"

class QApplication;

enum class ColorScheme { Dark, Light };

struct Theme {
    //----- Widgets: the style sheet uses these as @window, @text, ... -----
    QColor window;          // the main window and the tab bar
    QColor panel;           // the properties panel, drop-down lists, the block breadcrumb
    QColor field;           // text fields, number boxes, lists, the HDL editor
    QColor button;
    QColor buttonHover;
    QColor buttonPressed;
    QColor buttonChecked;   // the selected tool, a selected list row
    QColor border;
    QColor borderHover;
    QColor text;
    QColor mutedText;
    QColor disabledText;
    QColor errorText;
    QColor accent;          // teal: focus, check boxes, the Start button
    QColor accentHover;
    QColor accentPressed;
    QColor onAccent;        // text on teal
    QColor pause;           // amber: the Start button while it says Pause
    QColor pauseHover;
    QColor onPause;         // text on amber

    //----- The canvas (also the style sheet's @canvas, for the scroll bars) -----
    QColor canvas;
    QColor dot;             // the dot grid

    // Components: an outline in the component's own color, and a body tinted with it
    QColor componentFill;   // the body before tinting
    double componentTint;   // 0 = plain body, 0.5 = halfway to the outline color, 1 = the outline color
    QColor componentText;
    QColor andGate;         // also NAND
    QColor orGate;          // also NOR
    QColor xorGate;         // also XNOR
    QColor notGate;
    QColor mux;
    QColor reg;
    QColor source;          // gray: a source with all pins 0 (see sourceOutline)
    QColor sourceLit;       // red: a source with all pins 1, like a lit LED
    QColor display;
    QColor block;           // submodule blocks and the module ports inside them
    QColor inversion;       // the bubble at the output of a NAND, NOR and XNOR gate

    // Wires: one color while editing; while the simulation runs, a 1 lights up and a 0 dims
    QColor wire;
    QColor wireHigh;
    QColor wireLow;

    // Pins, selection, and the wire being drawn
    QColor pin;             // nothing connected
    QColor pinWired;
    QColor selection;
    QColor drag;            // a wire being dragged, not over a pin it can connect to
    QColor snap;            // ... over a pin it can connect to, and that pin

    //----- The waveforms (the waves themselves use wireHigh, like a wire carrying 1) -----
    QColor waveCursor;      // the cursor, set with a click
    QColor waveCursor2;     // the second cursor, set with Shift+click, to measure from the first
    QColor waveGrid;        // lines between rows, and at the ruler's ticks

    // The body of a component outlined in `outline`: the plain body moved `componentTint` of the way to it
    QColor fillFor(const QColor& outline) const;
    QColor gateColor(GType type) const;

    // A source shows how many of its pins are 1: `ones` from 0 (none: gray, `source`) to 1 (all: red,
    // `sourceLit`), in a straight line. A 1-pin source goes all the way at once; one of 4 pins moves it
    // a quarter of the way.
    QColor sourceOutline(double ones) const;
    QColor sourceFill(double ones) const;
    // Of the two text colors (componentText, canvas), the one that reads better on `background`
    QColor textOn(const QColor& background) const;

    // `from` moved `t` (0..1) of the way to `to`
    static QColor mix(const QColor& from, const QColor& to, double t);
};

// The colors of a color scheme
const Theme& themeFor(ColorScheme scheme);

// The colors everything paints with now. Dark until setCurrentTheme() is called.
const Theme& currentTheme();
void setCurrentTheme(ColorScheme scheme);

// The style sheet for the main window, in the colors of `theme`
QString themeStyleSheet(const Theme& theme);

// Qt's Fusion style, the same on every platform. Called once, before the main window exists.
void applyBaseStyle(QApplication& app);

// The application palette underneath the style sheet, for anything its rules do not reach
void applyPalette(QApplication& app, const Theme& theme);

// The icon of the light/dark button in `scheme`: it shows the mode the button switches to
QIcon colorSchemeIcon(ColorScheme scheme);

// The operating system's light/dark setting; dark if it does not say
ColorScheme systemColorScheme();
