#pragma once
// Colors (makeTheme) and the style sheet (@name = a Theme field); layout and texts stay in the .ui

#include <QColor>
#include <QIcon>
#include <QString>
#include "general.h"

class QApplication;

enum class ColorScheme { Dark, Light };

struct Theme {
    //----- Widgets: the style sheet uses these as @window, @text, ... -----
    QColor window;          // also the tab bar
    QColor panel;           // also drop-downs and the breadcrumb
    QColor field;           // also lists and the HDL editor
    QColor button;
    QColor buttonHover;
    QColor buttonPressed;
    QColor buttonChecked;   // the selected tool or row
    QColor border;
    QColor borderHover;
    QColor text;
    QColor mutedText;
    QColor disabledText;
    QColor errorText;
    QColor accent;          // teal: focus, check boxes, Start
    QColor accentHover;
    QColor accentPressed;
    QColor onAccent;
    QColor pause;           // amber: Pause
    QColor pauseHover;
    QColor onPause;

    //----- The canvas (also @canvas, for the scroll bars) -----
    QColor canvas;
    QColor dot;

    QColor componentFill;   // the body before tinting
    double componentTint;   // 0 = plain body, 1 = the outline color
    QColor componentText;
    QColor andGate;         // also NAND
    QColor orGate;          // also NOR
    QColor xorGate;         // also XNOR
    QColor notGate;
    QColor mux;
    QColor reg;
    QColor source;          // gray: all pins 0
    QColor sourceLit;       // teal: all pins 1
    QColor display;
    QColor block;           // blocks and their port tags
    QColor inversion;       // NAND, NOR, XNOR bubble

    // Wires: one color while editing; running, 1 lights up and 0 dims
    QColor wire;
    QColor wireHigh;
    QColor wireLow;
    QColor wireSelected;    // yellow: unlike teal, never mistaken for a 1

    QColor pin;             // unconnected
    QColor pinWired;
    QColor selection;
    QColor drag;            // a dragged wire not over a pin it can take
    QColor snap;            // ... over one, and that pin

    //----- Waveforms (the waves use wireHigh) -----
    QColor waveCursor;
    QColor waveCursor2;     // Shift+click
    QColor waveGrid;        // row lines and ruler ticks

    // The body color: the plain body moved componentTint of the way to `outline`
    QColor fillFor(const QColor& outline) const;
    QColor gateColor(GType type) const;

    // `ones` = fraction of pins at 1: 0 is gray (source), 1 is teal (sourceLit)
    QColor sourceOutline(double ones) const;
    QColor sourceFill(double ones) const;
    // componentText or canvas, whichever reads better on `background`
    QColor textOn(const QColor& background) const;

    static QColor mix(const QColor& from, const QColor& to, double t);
};

const Theme& themeFor(ColorScheme scheme);

// Dark until setCurrentTheme()
const Theme& currentTheme();
void setCurrentTheme(ColorScheme scheme);

QString themeStyleSheet(const Theme& theme);

// Fusion, the same on every platform; call before the main window exists
void applyBaseStyle(QApplication& app);

// For whatever the style sheet does not reach
void applyPalette(QApplication& app, const Theme& theme);

// Shows the mode the button switches to
QIcon colorSchemeIcon(ColorScheme scheme);

// Dark if the system does not say
ColorScheme systemColorScheme();
