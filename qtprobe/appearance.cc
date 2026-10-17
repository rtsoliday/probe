#include "appearance.h"
#include <QApplication>
#include <QFontDatabase>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QWidget>
#include <utility>
namespace probe {
Colors colors(const QPalette& palette) {
  if (palette.color(QPalette::Window).lightness() < 128)
    return {"#0e1822", "#162431", "#1c2d3b", "#304452", "#e8f0f5", "#a3b6c5",
      "#70d5d0", "#102c35", "#203f49", "#83deb7", "#f2c875", "#ff9ba7", true};
  return {"#f3f6fa", "#ffffff", "#f7f9fc", "#dce5ed", "#183142", "#617789",
    "#087e8b", "#ffffff", "#e4f3f3", "#087455", "#936009", "#bd3548", false};
}
void applyAppearance(QWidget* window, const QString& appearance) {
  auto p = QApplication::palette();
  if (appearance != "system") p.setColor(QPalette::Window, appearance == "dark" ? QColor("#0e1822") : QColor("#f3f6fa"));
  const auto c = colors(p);
  p.setColor(QPalette::Window, c.window); p.setColor(QPalette::WindowText, c.text);
  p.setColor(QPalette::Base, c.surface); p.setColor(QPalette::AlternateBase, c.raised);
  p.setColor(QPalette::Text, c.text); p.setColor(QPalette::Button, c.surface);
  p.setColor(QPalette::ButtonText, c.text); p.setColor(QPalette::Mid, c.border);
  p.setColor(QPalette::Highlight, c.accent); p.setColor(QPalette::HighlightedText, c.accentText);
  p.setColor(QPalette::Link, c.accent); p.setColor(QPalette::PlaceholderText, c.muted);
  p.setColor(QPalette::ToolTipBase, c.surface); p.setColor(QPalette::ToolTipText, c.text);
  // Bevel roles otherwise keep the system's light-theme white.
  p.setColor(QPalette::Light, c.dark ? c.raised : c.surface); p.setColor(QPalette::Midlight, c.raised);
  p.setColor(QPalette::Dark, c.border.darker(c.dark ? 130 : 150)); p.setColor(QPalette::Shadow, c.dark ? QColor("#05090d") : c.border.darker(200));
  // Dark muted text is too close to normal text to read as disabled.
  QColor disabled = c.muted;
  if (c.dark) disabled.setRgbF(c.muted.redF() * 0.62 + c.surface.redF() * 0.38, c.muted.greenF() * 0.62 + c.surface.greenF() * 0.38,
                               c.muted.blueF() * 0.62 + c.surface.blueF() * 0.38);
  for (auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText}) p.setColor(QPalette::Disabled, role, disabled);
  // Fusion etches disabled text with Light; the offset copy looked bold on dark menus.
  p.setColor(QPalette::Disabled, QPalette::Light, Qt::transparent);
  window->setPalette(p);
  // Menus, combo lists and tooltips are separate windows, so they never
  // inherit the window palette; give their classes the theme instead.
  for (const char* popup : {"QMenu", "QComboBoxPrivateContainer", "QTipLabel"}) QApplication::setPalette(p, popup);
}
QString workspaceStyle(const Colors& c) {
  QString css = R"(
    QWidget#workspace { background: @window; }
    QFrame[card="true"] { background: @surface; border: 1px solid @border; border-radius: 6px; }
    QFrame#welcomePage { background: transparent; border: 1px dashed @border; border-radius: 6px; }
    QFrame[section="true"] { background: transparent; border: 0; border-top: 1px solid @border; border-radius: 0; }
    QFrame#pvHeader { background: transparent; border: 0; border-bottom: 1px solid @border; border-radius: 0; }
    QFrame#pvRow { background: transparent; border: 0; border-bottom: 1px solid @border; border-radius: 0; }
    QFrame#pvRow:hover { background: @raised; }
    QFrame#pvRow[selected="true"] { background: @selected; }
    QFrame#pvRow[current="true"] { border-left: 3px solid @accent; }
    QLabel[sortable="true"]:hover { color: @accent; }
    QLabel { color: @text; background: transparent; }
    QLabel[role="muted"], QLabel[role="eyebrow"] { color: @muted; }
    QLabel[role="eyebrow"] { font-weight: 600; }
    QLabel[role="badge"] { background: @raised; border: 1px solid @border; border-radius: 8px; padding: 1px 7px; }
    QLabel[role="badge"][tone="positive"] { color: @positive; border-color: @positive; }
    QLabel[role="badge"][tone="warning"] { color: @warning; border-color: @warning; }
    QLabel[role="badge"][tone="negative"] { color: @negative; border-color: @negative; }
    QLabel[tone="positive"] { color: @positive; }
    QLabel[tone="warning"] { color: @warning; }
    QLabel[tone="negative"] { color: @negative; }
    QLabel[tone="muted"] { color: @muted; }
    QLineEdit, QComboBox, QDoubleSpinBox { background: @surface; color: @text; border: 1px solid @border; border-radius: 5px; padding: 3px 6px; selection-background-color: @accent; selection-color: @accentText; }
    QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus { border-color: @accent; }
    QLineEdit:disabled, QComboBox:disabled, QDoubleSpinBox:disabled { background: @raised; color: @muted; }
    QComboBox { padding-right: 20px; }
    QComboBox::drop-down { border: 0; width: 18px; }
    QComboBox::down-arrow { image: url(:/chevron-@theme.svg); width: 11px; height: 11px; }
    QComboBox QAbstractItemView { background: @surface; color: @text; selection-background-color: @selected; selection-color: @text; border: 1px solid @border; }
    QPushButton, QToolButton { color: @text; background: @surface; border: 1px solid @border; border-radius: 5px; padding: 3px 9px; }
    QPushButton:hover, QToolButton:hover { background: @raised; border-color: @accent; }
    QPushButton:focus, QToolButton:focus { border-color: @accent; }
    QPushButton:pressed, QToolButton:pressed, QToolButton:checked { background: @selected; }
    QPushButton[role="primary"] { background: @accent; color: @accentText; border-color: @accent; font-weight: 600; }
    QPushButton[role="primary"]:hover { background: @accentHover; }
    QPushButton:disabled, QToolButton:disabled { color: @muted; background: @raised; border-color: @border; }
    QToolButton[role="quiet"], QPushButton[role="quiet"] { background: transparent; border-color: transparent; padding: 3px 5px; }
    QToolButton[role="quiet"]:hover, QPushButton[role="quiet"]:hover { background: @raised; border-color: @border; }
    QToolButton[role="quiet"]:checked { background: @selected; border-color: @accent; }
    QToolButton[role="toggle"] { padding: 2px 8px; }
    QToolButton[role="toggle"]:checked { background: @selected; color: @accent; border-color: @accent; }
    QToolButton#exportButton { padding: 3px 6px; }
    QToolButton#exportButton::menu-indicator { image: none; width: 0; }
    QToolButton#closeTabButton, QToolButton#closeRowButton { background: transparent; border: 0; padding: 1px; }
    QToolButton#closeTabButton:hover, QToolButton#closeRowButton:hover { background: @border; }
    QTabBar#pvTabs { background: transparent; }
    QTabBar#pvTabs::tab { background: transparent; color: @muted; border: 0; border-bottom: 2px solid transparent; padding: 3px 6px; margin-right: 2px; max-width: 240px; }
    QTabBar#pvTabs::tab:hover { color: @text; background: @raised; }
    QTabBar#pvTabs::tab:selected { color: @text; border-bottom-color: @accent; }
    QTabBar#pvTabs QToolButton { padding: 0; }
    QTabBar#pvTabs[inactive="true"]::tab:selected { color: @muted; border-bottom-color: transparent; }
    QTabBar#pvTabs[inactive="true"]::tab:selected:hover { color: @text; }
    QToolButton#allPvsTab { background: transparent; color: @muted; border: 0; border-bottom: 2px solid transparent; border-radius: 0; padding: 3px 8px; }
    QToolButton#allPvsTab:hover { color: @text; background: @raised; }
    QFrame#pvTabDivider { background: @border; border: 0; margin: 4px 0; }
    QToolButton#allPvsTab:checked { color: @text; background: transparent; border-bottom-color: @accent; }
    QTableWidget, QTreeWidget { color: @text; background: @surface; alternate-background-color: @raised; border: 1px solid @border; border-radius: 0; selection-background-color: @selected; selection-color: @text; }
    QHeaderView::section { color: @muted; background: @raised; border: 0; border-bottom: 1px solid @border; padding: 3px 6px; text-align: left; }
    QTableWidget::item { padding: 1px 6px; border-bottom: 1px solid @border; }
    QTreeWidget::item { padding: 1px 4px; }
    QSlider::groove:horizontal { height: 4px; background: @border; border-radius: 2px; }
    QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
    QSlider::handle:horizontal { background: @surface; border: 2px solid @accent; width: 12px; margin: -6px 0; border-radius: 7px; }
    QCheckBox { color: @text; spacing: 6px; }
    QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid @border; border-radius: 3px; background: @surface; }
    QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(:/check-@theme.svg); }
    QScrollArea { background: transparent; border: 0; }
    QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
    QScrollBar::handle:vertical { background: @border; border-radius: 4px; min-height: 25px; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    QStatusBar { color: @muted; background: @window; }
    QStatusBar QLabel { color: @muted; }
  )";
  css.replace("@theme", c.dark ? "dark" : "light");
  // Replace longer tokens first because @accent is a prefix of two others.
  css.replace("@accentHover", c.dark ? c.accent.lighter(110).name() : c.accent.darker(110).name());
  css.replace("@accentText", c.accentText.name());
  const std::pair<const char*, QColor> tokens[] = {{"@window", c.window}, {"@surface", c.surface},
    {"@raised", c.raised}, {"@border", c.border}, {"@text", c.text}, {"@muted", c.muted},
    {"@accent", c.accent}, {"@selected", c.selected}, {"@positive", c.positive},
    {"@warning", c.warning}, {"@negative", c.negative}};
  for (const auto& token : tokens) css.replace(token.first, token.second.name());
  return css;
}
namespace {
int zoomPercent = 100;
QFont& startupFont() { static QFont font = QApplication::font(); return font; }
} // namespace
void scaleFont(QWidget* widget, qreal scale, bool bold, bool fixed) {
  widget->setProperty("fontScale", scale); widget->setProperty("fontBold", bold); widget->setProperty("fontFixed", fixed);
  auto font = fixed ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QApplication::font();
  const auto base = QApplication::font();
  if (base.pointSizeF() > 0) font.setPointSizeF(base.pointSizeF() * scale);
  else if (base.pixelSize() > 0) font.setPixelSize(qRound(base.pixelSize() * scale));
  font.setBold(bold); widget->setFont(font);
}
void rescaleFonts(QWidget* root) {
  auto apply = [](QWidget* w) {
    if (w->property("fontScale").isValid())
      scaleFont(w, w->property("fontScale").toReal(), w->property("fontBold").toBool(), w->property("fontFixed").toBool());
  };
  apply(root); for (auto* child : root->findChildren<QWidget*>()) apply(child);
}
int textWidth(const QFont& font, const QString& text) { return int(std::ceil(QFontMetricsF(font).horizontalAdvance(text))) + 1; }
int zoom() { return zoomPercent; }
void setZoom(int percent) {
  zoomPercent = std::clamp(percent, 70, 200);
  auto font = startupFont();
  if (font.pointSizeF() > 0) font.setPointSizeF(font.pointSizeF() * zoomPercent / 100);
  else if (font.pixelSize() > 0) font.setPixelSize(qRound(font.pixelSize() * zoomPercent / 100.0));
  QApplication::setFont(font);
}
void setTone(QWidget* widget, const QString& tone) {
  if (widget->property("tone").toString() == tone) return;
  widget->setProperty("tone", tone); widget->style()->unpolish(widget); widget->style()->polish(widget); widget->update();
}
namespace {
// Icons are drawn once per symbol and color and shared by every button, so
// each new inspector adds no pixmaps (or pixmap-cache entries) of its own.
QIcon* cachedIcon(int symbol, const QColor& color) {
  static QHash<QPair<int, QRgb>, QIcon>* icons = nullptr;
  if (!icons) { icons = new QHash<QPair<int, QRgb>, QIcon>; qAddPostRoutine([] { delete icons; icons = nullptr; }); }
  return &(*icons)[qMakePair(symbol, color.rgba())];
}
} // namespace
QIcon dotIcon(const QColor& color) {
  auto* cached = cachedIcon(-1, color);
  if (!cached->isNull()) return *cached;
  auto& result = *cached;
  for (int scale : {1, 2, 3}) {
    QPixmap image(12 * scale, 12 * scale); image.setDevicePixelRatio(scale); image.fill(Qt::transparent);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen); p.setBrush(color);
    p.drawEllipse(QPointF(6, 6), 3.5, 3.5); p.end(); result.addPixmap(image);
  }
  return result;
}
QIcon symbolIcon(Symbol symbol, const QColor& color) {
  auto* cached = cachedIcon(int(symbol), color);
  if (!cached->isNull()) return *cached;
  auto& result = *cached;
  for (int scale : {1, 2, 3}) {
    QPixmap image(20 * scale, 20 * scale); image.setDevicePixelRatio(scale); image.fill(Qt::transparent);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
    switch (symbol) {
    case Symbol::Search: p.drawEllipse(QRectF(3, 3, 10, 10)); p.drawLine(QPointF(12, 12), QPointF(17, 17)); break;
    case Symbol::Plus: p.drawLine(10, 4, 10, 16); p.drawLine(4, 10, 16, 10); break;
    case Symbol::Close: p.drawLine(5, 5, 15, 15); p.drawLine(15, 5, 5, 15); break;
    case Symbol::Copy: p.drawRoundedRect(QRectF(7, 7, 10, 10), 2, 2); p.drawLine(4, 13, 3, 13); p.drawLine(3, 13, 3, 3); p.drawLine(3, 3, 13, 3); p.drawLine(13, 3, 13, 4); break;
    case Symbol::Reconnect: p.drawArc(QRectF(3, 3, 14, 14), 45 * 16, 290 * 16); p.drawLine(14, 3, 17, 5); p.drawLine(17, 5, 17, 1); break;
    case Symbol::Stop: p.drawRoundedRect(QRectF(5, 5, 10, 10), 1, 1); break;
    case Symbol::Pause: p.drawLine(7, 4, 7, 16); p.drawLine(13, 4, 13, 16); break;
    case Symbol::Export: p.drawLine(10, 3, 10, 13); p.drawLine(6, 9, 10, 13); p.drawLine(14, 9, 10, 13); p.drawPolyline(QPolygonF(QVector<QPointF>{{3, 12}, {3, 17}, {17, 17}, {17, 12}})); break;
    case Symbol::Wave: p.drawPolyline(QPolygonF(QVector<QPointF>{{2, 11}, {5, 11}, {8, 4}, {12, 16}, {15, 9}, {18, 9}})); break;
    case Symbol::Sun: p.drawEllipse(QRectF(6, 6, 8, 8)); for (int n = 0; n < 8; ++n) { p.save(); p.translate(10, 10); p.rotate(n * 45); p.drawLine(0, -7, 0, -9); p.restore(); } break;
    case Symbol::Process: p.drawEllipse(QRectF(3, 3, 14, 14)); p.drawPolygon(QPolygonF(QVector<QPointF>{{8.5, 6.5}, {13.5, 10}, {8.5, 13.5}})); break;
    case Symbol::Moon: { QPainterPath path; path.moveTo(13, 2); path.cubicTo(0, 1, 0, 19, 13, 18); path.cubicTo(6, 15, 6, 5, 13, 2); p.drawPath(path); break; }
    }
    p.end(); result.addPixmap(image);
  }
  return result;
}
} // namespace probe
