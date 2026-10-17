#pragma once
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QFont>
#include <QString>
class QWidget;
namespace probe {
struct Colors {
  QColor window, surface, raised, border, text, muted, accent, accentText,
      selected, positive, warning, negative;
  bool dark = false;
};
enum class Symbol { Search, Plus, Close, Copy, Reconnect, Stop, Pause, Export, Wave, Sun, Moon, Process };
Colors colors(const QPalette&);
void applyAppearance(QWidget*, const QString& appearance);
QString workspaceStyle(const Colors&);
QIcon symbolIcon(Symbol, const QColor&);
// A small filled status dot, used beside PV names.
QIcon dotIcon(const QColor&);
void setTone(QWidget*, const QString& tone);
// Fonts relative to the application font. They are recorded on the widget so
// rescaleFonts() can follow a zoom change.
void scaleFont(QWidget*, qreal scale, bool bold = false, bool fixed = false);
// Pixels that always show text unelided. Unhinted fonts (common on remote X
// displays) have fractional widths that QFontMetrics::horizontalAdvance rounds
// down, so text sized with it is elided by its last character.
int textWidth(const QFont&, const QString&);
void rescaleFonts(QWidget* root);
// Interface zoom as a percentage of the startup font; applied to the application font.
int zoom();
void setZoom(int percent);
} // namespace probe
