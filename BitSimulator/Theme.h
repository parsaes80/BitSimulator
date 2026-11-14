#pragma once
#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>

class QApplication;

enum class ColorScheme { Dark, Light };

// The app's two style sheets.
//
// BitSimulator.ui holds one style sheet, written in dark-mode colors. The comment at its top has a
// table that pairs every dark color with its light-mode color, one pair per line:
//     #0a0e11   #f7f9fa   canvas
// Light mode is the same style sheet with every color swapped through that table.
class ThemeSheets {
public:
    ThemeSheets() = default;
    explicit ThemeSheets(const QString& darkSheet);

    const QString& sheet(ColorScheme scheme) const { return scheme == ColorScheme::Dark ? m_dark : m_light; }

    // A color of the dark style sheet, as it is in `scheme` (unchanged if the table has no row for it)
    QColor color(const QString& darkColor, ColorScheme scheme) const;

    // Colors the style sheet uses that the table has no row for; they look the same in both modes
    const QStringList& unpairedColors() const { return m_unpaired; }

private:
    QString m_dark;
    QString m_light;
    QHash<QString, QString> m_toLight;   // lower case, e.g. "#0a0e11" -> "#f7f9fa"
    QStringList m_unpaired;
};

// Qt's Fusion style, the same on every platform. Called once, before the main window exists.
void applyBaseStyle(QApplication& app);

// The application palette underneath the style sheet, for anything its rules do not reach.
void applyPalette(QApplication& app, const ThemeSheets& sheets, ColorScheme scheme);

// The operating system's light/dark setting; dark if it does not say.
ColorScheme systemColorScheme();
