#include "Theme.h"
#include <QApplication>
#include <QDebug>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyleHints>
#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

//===================== The colors ========================

Theme makeTheme(ColorScheme scheme)
{
    const auto pick = [scheme](QRgb dark, QRgb light) { return QColor::fromRgb(scheme == ColorScheme::Dark ? dark : light); };

    Theme t;
    //                           dark      light
    // Widgets
    t.window        = pick(0x0f1316, 0xe9edf0);
    t.panel         = pick(0x141a1e, 0xf2f5f7);
    t.field         = pick(0x0c1013, 0xffffff);
    t.button        = pick(0x1b2227, 0xffffff);
    t.buttonHover   = pick(0x222b31, 0xeef2f4);
    t.buttonPressed = pick(0x12171b, 0xe2e8eb);
    t.buttonChecked = pick(0x10302d, 0xd2f2ec);
    t.border        = pick(0x263036, 0xd5dce0);
    t.borderHover   = pick(0x33404a, 0xb4bfc5);
    t.text          = pick(0xe4ebee, 0x1a2328);
    t.mutedText     = pick(0x7a8890, 0x5f6e76);
    t.disabledText  = pick(0x4f5b62, 0xa4afb5);
    t.errorText     = pick(0xff9e80, 0xc2410c);
    t.accent        = pick(0x14b8a6, 0x14b8a6);
    t.accentHover   = pick(0x2dd4bf, 0x0d9488);
    t.accentPressed = pick(0x0f9488, 0x0b7f75);
    t.onAccent      = pick(0x04211e, 0x04211e);
    t.pause         = pick(0xe0a526, 0xe0a526);
    t.pauseHover    = pick(0xedb740, 0xedb740);
    t.onPause       = pick(0x1b1405, 0x1b1405);

    // Canvas
    t.canvas        = pick(0x0a0e11, 0xf7f9fa);
    t.dot           = pick(0x1e282e, 0xd3dade);
    t.componentFill = pick(0x141b1f, 0xffffff);
    t.componentTint = 0.5;
    t.componentText = pick(0xc3ced4, 0x3d4b53);
    t.andGate       = pick(0xf1cf51, 0xcfa312);
    t.orGate        = pick(0x55dd87, 0x0aa049);
    t.xorGate       = pick(0xa586ff, 0x7842e6);
    t.notGate       = pick(0xf97575, 0xe43c41);
    t.mux           = pick(0xf59e5b, 0xdd6f16);
    t.reg           = pick(0x7aa2f7, 0x3567d6);
    t.source        = pick(0x9aa6ad, 0x7d878d);
    t.sourceLit     = pick(0x2dd4bf, 0x0d9488);   // teal, like a wire carrying 1
    t.display       = pick(0xef4444, 0xc62828);
    t.block         = pick(0xf0abfc, 0xa21caf);
    t.inversion     = pick(0xef4444, 0xc62828);
    t.wire          = pick(0xc3ced4, 0x3d4b53);
    t.wireHigh      = pick(0x2dd4bf, 0x0d9488);
    t.wireLow       = pick(0x3b4a52, 0xc3cdd2);
    t.wireSelected  = pick(0xfacc15, 0xca8a04);
    t.pin           = pick(0x4a5960, 0xa3b0b6);
    t.pinWired      = pick(0x14b8a6, 0x14b8a6);
    t.selection     = pick(0x2dd4bf, 0x0d9488);
    t.drag          = pick(0x7a8890, 0x5f6e76);
    t.snap          = pick(0xfde68a, 0xd98a06);

    // Waveforms
    t.waveCursor    = pick(0xfde68a, 0xd98a06);
    t.waveCursor2   = pick(0x7aa2f7, 0x3567d6);
    t.waveGrid      = pick(0x1c2428, 0xe1e7ea);
    return t;
}

const Theme* current = &themeFor(ColorScheme::Dark);

//===================== The style sheet ========================

// @name = that Theme color, e.g. @accent; images are in theme/ (theme.qrc)
constexpr const char* SheetTemplate = R"qss(
QWidget {
    color: @text;
    selection-background-color: @accent;
    selection-color: @onAccent;
}

QMainWindow, QDialog, QMessageBox {
    background-color: @window;
}

QToolTip {
    background-color: @button;
    color: @text;
    border: 1px solid @border;
    border-radius: 4px;
    padding: 4px 6px;
}

QLabel[muted="true"] {
    color: @mutedText;
}

/* ---------- Tabs ---------- */

QTabWidget::pane {
    border: none;
    border-top: 1px solid @border;
    background-color: @window;
}

QTabBar {
    background-color: @window;
}

QTabBar::tab {
    background-color: transparent;
    color: @mutedText;
    padding: 7px 16px;
    border: none;
    border-bottom: 2px solid transparent;
}

QTabBar::tab:hover {
    color: @text;
}

QTabBar::tab:selected {
    color: @text;
    border-bottom: 2px solid @accent;
}

/* ---------- Buttons (toolbar components too) ---------- */

QPushButton {
    background-color: @button;
    color: @text;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 5px 10px;
}

QPushButton:hover {
    background-color: @buttonHover;
    border-color: @borderHover;
}

QPushButton:pressed {
    background-color: @buttonPressed;
}

QPushButton:checked {
    background-color: @buttonChecked;
    border: 1px solid @accent;
}

QPushButton:disabled {
    background-color: @panel;
    color: @disabledText;
}

/* Start / Pause: teal for Start, amber for Pause */
#startButton, #waveStartButton {
    background-color: @accent;
    border-color: @accent;
    color: @onAccent;
    font-weight: bold;
}

#startButton:hover, #waveStartButton:hover {
    background-color: @accentHover;
    border-color: @accentHover;
}

#startButton:pressed, #waveStartButton:pressed {
    background-color: @accentPressed;
}

#startButton[running="true"], #waveStartButton[running="true"] {
    background-color: @pause;
    border-color: @pause;
    color: @onPause;
}

#startButton[running="true"]:hover, #waveStartButton[running="true"]:hover {
    background-color: @pauseHover;
    border-color: @pauseHover;
}

/* Waveforms toolbar: line before Start / Reset */
#waveToolbarSeparator {
    border: none;
    background-color: @border;
    min-width: 1px;
    max-width: 1px;
    margin: 6px 4px;
}

/* Light / dark mode button (MainWindow sets the icon) and the About button next to it */
#themeButton, #aboutButton {
    background-color: transparent;
    border: 1px solid transparent;
    border-radius: 18px;
    padding: 0px;
    icon-size: 18px;
}

#aboutButton {
    color: @mutedText;
    font-weight: bold;
    font-size: 16px;
}

#themeButton:hover, #aboutButton:hover {
    background-color: @buttonHover;
    border-color: @borderHover;
}

#themeButton:pressed, #aboutButton:pressed {
    background-color: @buttonPressed;
}

/* ---------- Speed slider ---------- */

QSlider::groove:horizontal {
    height: 4px;
    background-color: @border;
    border-radius: 2px;
}

QSlider::sub-page:horizontal {
    background-color: @accent;
    border-radius: 2px;
}

QSlider::handle:horizontal {
    background-color: @text;
    width: 14px;
    margin: -5px 0;
    border-radius: 7px;
}

QSlider::handle:horizontal:hover {
    background-color: @accentHover;
}

/* ---------- Text fields, number boxes, drop-downs ---------- */

QLineEdit, QSpinBox, QComboBox {
    background-color: @field;
    color: @text;
    border: 1px solid @border;
    border-radius: 5px;
    padding: 3px 6px;
    min-height: 18px;
}

QLineEdit:hover, QSpinBox:hover, QComboBox:hover {
    border-color: @borderHover;
}

QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
    border-color: @accent;
}

QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled {
    color: @disabledText;
}

/* The number box draws the border, not its text field */
QSpinBox QLineEdit {
    background-color: transparent;
    border: none;
    padding: 0px;
    min-height: 0px;
}

/* Arrow images: theme/ (theme.qrc) */
QSpinBox::up-button, QSpinBox::down-button {
    subcontrol-origin: border;
    width: 20px;
    border: none;
    background-color: transparent;
}

QSpinBox::up-button {
    subcontrol-position: top right;
}

QSpinBox::down-button {
    subcontrol-position: bottom right;
}

QSpinBox::up-arrow {
    image: url(:/theme/arrow-up.png);
}

QSpinBox::down-arrow, QComboBox::down-arrow {
    image: url(:/theme/arrow-down.png);
}

QSpinBox::up-arrow:disabled, QSpinBox::up-arrow:off {
    image: url(:/theme/arrow-up-disabled.png);
}

QSpinBox::down-arrow:disabled, QSpinBox::down-arrow:off, QComboBox::down-arrow:disabled {
    image: url(:/theme/arrow-down-disabled.png);
}

QComboBox::drop-down {
    subcontrol-origin: border;
    subcontrol-position: center right;
    width: 22px;
    border: none;
}

QComboBox QAbstractItemView {
    background-color: @panel;
    border: 1px solid @border;
    outline: none;
    padding: 2px;
}

/* The search bar's results */
#searchResults {
    background-color: @panel;
    color: @text;
    border: 1px solid @border;
    outline: none;
    padding: 2px;
}

#searchResults::item {
    padding: 3px 6px;
}

#searchResults::item:hover, #searchResults::item:selected {
    background-color: @buttonChecked;
    color: @text;
}

QCheckBox {
    spacing: 8px;
}

QCheckBox::indicator {
    width: 14px;
    height: 14px;
    background-color: @field;
    border: 1px solid @borderHover;
    border-radius: 4px;
}

QCheckBox::indicator:hover {
    border-color: @accent;
}

QCheckBox::indicator:checked {
    background-color: @accent;
    border-color: @accent;
    image: url(:/theme/check.png);
}

QCheckBox::indicator:checked:hover {
    background-color: @accentHover;
    border-color: @accentHover;
}

QCheckBox::indicator:disabled {
    background-color: @panel;
    border-color: @border;
}

QCheckBox:disabled {
    color: @disabledText;
}

/* ---------- HDL editor ---------- */

QPlainTextEdit {
    background-color: @field;
    color: @text;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 6px 8px;
}

QPlainTextEdit:focus {
    border-color: @accent;
}

/* ---------- Scroll bars ---------- */

QScrollBar:vertical {
    background-color: @canvas;
    width: 10px;
    margin: 0;
}

QScrollBar:horizontal {
    background-color: @canvas;
    height: 10px;
    margin: 0;
}

QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background-color: @border;
    border-radius: 3px;
    margin: 2px;
}

QScrollBar::handle:vertical {
    min-height: 30px;
}

QScrollBar::handle:horizontal {
    min-width: 30px;
}

QScrollBar::handle:hover {
    background-color: @borderHover;
}

QScrollBar::add-line, QScrollBar::sub-line {
    width: 0;
    height: 0;
    border: none;
}

QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
}

QAbstractScrollArea::corner {
    background-color: @canvas;
}

/* ---------- Properties panel ---------- */

/* FloatingPanel: the box over the canvas */
#propertiesFrame {
    background-color: @panel;
    border: 1px solid @border;
    border-radius: 10px;
}

/* Its title bar and minimize button */
#panelTitle {
    color: @mutedText;
}

#panelMinimize {
    padding: 1px 6px;
    background-color: transparent;
    color: @mutedText;
    border: none;
    border-radius: 4px;
}

#panelMinimize:hover {
    background-color: @buttonHover;
    color: @text;
}

#panelSeparator {
    color: @border;
}

#propertiesScroll, #propertiesScroll QWidget#qt_scrollarea_viewport, #propertiesPanel {
    background-color: transparent;
}

#propertiesSeparator {
    color: @border;
}

#propertiesMessage {
    color: @errorText;
}

#propertiesLock {
    background-color: @field;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 8px;
}

/* The joined buttons, e.g. MUX | DEMUX */
#segmented QToolButton {
    padding: 5px 6px;
    background-color: @button;
    color: @text;
    border: 1px solid @border;
    border-left-width: 0px;
}

#segmented QToolButton:hover {
    background-color: @buttonHover;
}

#segmented QToolButton[segment="first"] {
    border-left-width: 1px;
    border-top-left-radius: 5px;
    border-bottom-left-radius: 5px;
}

#segmented QToolButton[segment="last"] {
    border-top-right-radius: 5px;
    border-bottom-right-radius: 5px;
}

#segmented QToolButton:checked {
    background-color: @accent;
    border-color: @accent;
    color: @onAccent;
    font-weight: bold;
}

#segmented QToolButton:disabled {
    color: @disabledText;
}

/* Block breadcrumb: top > core (RISCV) > alu (ALU) */
#blockBreadcrumb {
    background-color: @panel;
    border: 1px solid @border;
    border-radius: 8px;
}

#blockBreadcrumb QToolButton {
    padding: 3px 6px;
    background-color: transparent;
    color: @text;
    border: none;
    border-radius: 4px;
}

#blockBreadcrumb QToolButton:hover {
    background-color: @buttonHover;
}

#blockBreadcrumb QToolButton:disabled {
    color: @block;
    font-weight: bold;
}

#blockBreadcrumb QLabel {
    color: @disabledText;
}

/* Lists, e.g. the Compile Dir files */
QListWidget {
    background-color: @field;
    color: @text;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 4px;
    outline: none;
}

QListWidget::item {
    padding: 4px 6px;
    border-radius: 4px;
}

QListWidget::item:hover {
    background-color: @button;
}

QListWidget::item:selected {
    background-color: @buttonChecked;
    color: @text;
}

/* Trees, e.g. the waveform signals */
QTreeWidget {
    background-color: @field;
    color: @text;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 4px;
    outline: none;
}

QTreeWidget::item {
    padding: 2px 2px;
}

QTreeWidget::item:hover {
    background-color: @button;
}

QTreeWidget::item:selected {
    background-color: @buttonChecked;
    color: @text;
}

QHeaderView::section {
    background-color: @panel;
    color: @mutedText;
    border: none;
    border-bottom: 1px solid @border;
    padding: 4px 6px;
}

QListWidget::indicator, QTreeWidget::indicator {
    width: 14px;
    height: 14px;
    background-color: @field;
    border: 1px solid @borderHover;
    border-radius: 4px;
}

QListWidget::indicator:checked, QTreeWidget::indicator:checked {
    background-color: @accent;
    border-color: @accent;
    image: url(:/theme/check.png);
}

/* Partly shown block in the signal tree */
QTreeWidget::indicator:indeterminate {
    background-color: @buttonChecked;
    border-color: @accent;
}

/* Waveform source pin buttons; checked = the pin being edited */
QToolButton[wavePin="true"] {
    padding: 3px 0px;
    background-color: @button;
    color: @text;
    border: 1px solid @border;
    border-radius: 5px;
}

QToolButton[wavePin="true"]:hover {
    background-color: @buttonHover;
}

QToolButton[wavePin="true"]:checked {
    background-color: @accent;
    border-color: @accent;
    color: @onAccent;
    font-weight: bold;
}

/* The numbers of a list source */
#sourceValues {
    background-color: @field;
    border: 1px solid @border;
    border-radius: 5px;
    padding: 2px 4px;
}

#sourceValues:focus {
    border-color: @accent;
}

/* ---------- Canvas (painted in code) ---------- */

CircuitCanvas {
    border: none;
    background-color: @canvas;
}
)qss";

const std::pair<QLatin1StringView, QColor Theme::*> SheetColors[] = {
    {"window"_L1, &Theme::window},
    {"panel"_L1, &Theme::panel},
    {"field"_L1, &Theme::field},
    {"button"_L1, &Theme::button},
    {"buttonHover"_L1, &Theme::buttonHover},
    {"buttonPressed"_L1, &Theme::buttonPressed},
    {"buttonChecked"_L1, &Theme::buttonChecked},
    {"border"_L1, &Theme::border},
    {"borderHover"_L1, &Theme::borderHover},
    {"text"_L1, &Theme::text},
    {"mutedText"_L1, &Theme::mutedText},
    {"disabledText"_L1, &Theme::disabledText},
    {"errorText"_L1, &Theme::errorText},
    {"accent"_L1, &Theme::accent},
    {"accentHover"_L1, &Theme::accentHover},
    {"accentPressed"_L1, &Theme::accentPressed},
    {"onAccent"_L1, &Theme::onAccent},
    {"pause"_L1, &Theme::pause},
    {"pauseHover"_L1, &Theme::pauseHover},
    {"onPause"_L1, &Theme::onPause},
    {"canvas"_L1, &Theme::canvas},
    {"block"_L1, &Theme::block},
};

}

//===================== Theme ========================

QColor Theme::mix(const QColor& from, const QColor& to, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const auto channel = [t](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * t)); };
    return QColor(channel(from.red(), to.red()), channel(from.green(), to.green()), channel(from.blue(), to.blue()));
}

QColor Theme::fillFor(const QColor& outline) const
{
    return mix(componentFill, outline, componentTint);
}

QColor Theme::sourceOutline(double ones) const
{
    return mix(source, sourceLit, ones);
}

QColor Theme::sourceFill(double ones) const
{
    // From a gray source's tinted body to solid teal
    return mix(fillFor(source), sourceLit, ones);
}

QColor Theme::textOn(const QColor& background) const
{
    // WCAG contrast ratio
    const auto luminance = [](const QColor& color) {
        const auto linear = [](double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
        return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
    };
    const auto contrast = [&](const QColor& a, const QColor& b) {
        const double la = luminance(a), lb = luminance(b);
        return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    };
    return contrast(componentText, background) >= contrast(canvas, background) ? componentText : canvas;
}

QColor Theme::gateColor(GType type) const
{
    switch (type) {
    case GType::AND: case GType::NAND: return andGate;
    case GType::OR:  case GType::NOR:  return orGate;
    case GType::XOR: case GType::XNOR: return xorGate;
    case GType::NOT: return notGate;
    }
    return componentText;
}

const Theme& themeFor(ColorScheme scheme)
{
    static const Theme dark = makeTheme(ColorScheme::Dark);
    static const Theme light = makeTheme(ColorScheme::Light);
    return scheme == ColorScheme::Dark ? dark : light;
}

const Theme& currentTheme()
{
    return *current;
}

void setCurrentTheme(ColorScheme scheme)
{
    current = &themeFor(scheme);
}

//===================== Style sheet, palette, icons ========================

QString themeStyleSheet(const Theme& theme)
{
    static const QRegularExpression colorName(u"@([A-Za-z]+)"_s);
    const QString sheet = QString::fromLatin1(SheetTemplate).trimmed();

    QString result;
    result.reserve(sheet.size());
    qsizetype copied = 0;
    for (auto it = colorName.globalMatch(sheet); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QStringView name = match.capturedView(1);
        const auto entry = std::ranges::find_if(SheetColors, [name](const auto& color) { return color.first == name; });
        if (entry == std::end(SheetColors)) {
            qWarning() << "Theme.cpp: the style sheet names a color Theme does not have:" << match.captured();
            continue;   // left as is: Qt ignores the rule
        }
        result += QStringView(sheet).mid(copied, match.capturedStart() - copied);
        result += (theme.*entry->second).name();
        copied = match.capturedEnd();
    }
    result += QStringView(sheet).mid(copied);
    return result;
}

void applyBaseStyle(QApplication& app)
{
    app.setStyle(QStyleFactory::create(u"Fusion"_s));
}

void applyPalette(QApplication& app, const Theme& theme)
{
    const bool dark = (theme.canvas.lightness() < 128);

    QPalette palette;
    palette.setColor(QPalette::Window, theme.window);
    palette.setColor(QPalette::WindowText, theme.text);
    palette.setColor(QPalette::Base, theme.field);
    palette.setColor(QPalette::AlternateBase, theme.window);
    palette.setColor(QPalette::Text, theme.text);
    palette.setColor(QPalette::PlaceholderText, theme.mutedText);
    palette.setColor(QPalette::Button, theme.button);
    palette.setColor(QPalette::ButtonText, theme.text);
    palette.setColor(QPalette::BrightText, theme.accent);
    palette.setColor(QPalette::ToolTipBase, theme.button);
    palette.setColor(QPalette::ToolTipText, theme.text);
    palette.setColor(QPalette::Highlight, theme.accent);
    palette.setColor(QPalette::HighlightedText, theme.onAccent);
    palette.setColor(QPalette::Link, theme.accent);
    palette.setColor(QPalette::Light, theme.button);
    palette.setColor(QPalette::Midlight, theme.border);
    palette.setColor(QPalette::Mid, theme.border);
    palette.setColor(QPalette::Dark, theme.border.darker(dark ? 150 : 120));
    palette.setColor(QPalette::Shadow, dark ? QColor(Qt::black) : theme.border.darker(160));

    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, theme.disabledText);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, theme.border);

    app.setPalette(palette);
}

QIcon colorSchemeIcon(ColorScheme scheme)
{
    // QIcon also loads the @2x image for high-DPI screens
    return QIcon(scheme == ColorScheme::Dark ? u":/theme/sun.png"_s : u":/theme/moon.png"_s);
}

QIcon searchIcon(const Theme& theme)
{
    // Drawn, not an image, so it takes each mode's muted text color; sharp at 1x, 2x and 3x
    constexpr int Size = 16;   // logical pixels
    QIcon icon;
    for (const qreal scale : {1.0, 2.0, 3.0}) {
        QPixmap pixmap(QSize(Size, Size) * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(theme.mutedText, 1.6, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(QPointF(6.5, 6.5), 4.5, 4.5);
        painter.drawLine(QPointF(9.9, 9.9), QPointF(14, 14));
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

ColorScheme systemColorScheme()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light ? ColorScheme::Light : ColorScheme::Dark;
}
