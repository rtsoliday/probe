#include "about_banner.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
namespace probe {
AboutBanner::AboutBanner(QWidget* parent) : QWidget(parent), timer(this) {
  setObjectName("aboutBanner");
  setAccessibleName(tr("QtProbe signal explorer"));
  setAccessibleDescription(tr("An illustrative signal under a magnifying glass. Click or press Space to scan it."));
  setToolTip(tr("Click to explore the signal"));
  setFocusPolicy(Qt::StrongFocus); setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFixedHeight(sizeHint().height());
  timer.setObjectName("aboutAnimationTimer"); timer.setInterval(40);
  connect(&timer, &QTimer::timeout, this, [this] { advance(clock.restart() / 1000.0); });
}
bool AboutBanner::startScan() {
  if (scanning()) return false;
  scanTime = 0; update(); return true;
}
void AboutBanner::advance(qreal seconds) {
  if (!std::isfinite(seconds) || seconds <= 0) return;
  time += seconds;
  if (scanning()) { scanTime += seconds; if (scanTime >= 3.2) scanTime = -1; }
  update();
}
void AboutBanner::showEvent(QShowEvent* event) {
  QWidget::showEvent(event); clock.start(); timer.start();
}
void AboutBanner::hideEvent(QHideEvent* event) {
  timer.stop(); clock.invalidate(); scanTime = -1; QWidget::hideEvent(event);
}
void AboutBanner::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) { startScan(); event->accept(); }
  else QWidget::mousePressEvent(event);
}
void AboutBanner::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if (!event->isAutoRepeat()) startScan();
    event->accept();
  } else QWidget::keyPressEvent(event);
}
void AboutBanner::paintEvent(QPaintEvent*) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
  const qreal scale = qMin(width() / 460.0, height() / 140.0);
  p.translate((width() - 460 * scale) / 2, (height() - 140 * scale) / 2); p.scale(scale, scale);
  const auto base = palette().color(QPalette::Base), ink = palette().color(QPalette::Text);
  const auto accent = palette().color(QPalette::Link);
  const auto green = QColor(base.lightness() < 128 ? "#6ed7b4" : "#18795d");
  auto tint = accent; tint.setAlpha(22);
  p.setPen(QPen(palette().color(QPalette::Mid), 1)); p.setBrush(base);
  p.drawRoundedRect(QRectF(0.5, 0.5, 459, 139), 10, 10);
  p.setPen(Qt::NoPen); p.setBrush(tint); p.drawRoundedRect(QRectF(12, 12, 436, 116), 7, 7);
  p.setPen(QPen(palette().color(QPalette::Mid), 0.7, Qt::DotLine));
  for (int i = 1; i < 9; ++i) p.drawLine(QPointF(18 + i * 45, 37), QPointF(18 + i * 45, 117));
  for (int i = 0; i < 4; ++i) p.drawLine(QPointF(18, 47 + i * 22), QPointF(442, 47 + i * 22));
  const qreal phase = scanning() ? scanTime / 3.2 : (std::sin(time * 0.7) + 1) / 2;
  const qreal cx = 88 + phase * 240, cy = 78;
  auto signal = [this](qreal x) { return 79 + 17 * std::sin(x * 0.047 - time * 0.8) + 6 * std::cos(x * 0.12 + time * 0.4); };
  QPainterPath wave;
  for (int x = 20; x <= 440; ++x) {
    if (x == 20) wave.moveTo(x, signal(x)); else wave.lineTo(x, signal(x));
  }
  p.setBrush(Qt::NoBrush); p.setPen(QPen(green, 2.2)); p.drawPath(wave);
  p.setPen(QPen(accent, 8, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(QPointF(cx + 23, cy + 24), QPointF(cx + 43, cy + 44));
  auto lens = base; lens.setAlpha(220); p.setBrush(lens); p.setPen(QPen(accent, 4));
  p.drawEllipse(QPointF(cx, cy), 33, 33);
  p.save(); QPainterPath clip; clip.addEllipse(QPointF(cx, cy), 29, 29); p.setClipPath(clip);
  QPainterPath detail;
  for (int x = -30; x <= 30; ++x) {
    const qreal y = cy + (signal(cx + x / 1.8) - signal(cx)) * 1.8;
    if (x == -30) detail.moveTo(cx + x, y); else detail.lineTo(cx + x, y);
  }
  p.setBrush(Qt::NoBrush); p.setPen(QPen(green, 2.8)); p.drawPath(detail); p.restore();
  auto font = this->font(); font.setPixelSize(12); font.setBold(true); p.setFont(font); p.setPen(ink);
  p.drawText(QRectF(24, 15, 240, 22), Qt::AlignVCenter, scanning() ? tr("PV FOUND · LOOK CLOSER") : tr("EXPLORE THE SIGNAL"));
  font.setPixelSize(11); p.setFont(font); p.setPen(accent);
  p.drawText(QRectF(300, 15, 135, 22), Qt::AlignRight | Qt::AlignVCenter, "CA  /  pvAccess");
  if (scanning()) {
    p.setPen(Qt::NoPen); p.setBrush(accent); p.drawRoundedRect(QRectF(24, 125, 412 * phase, 3), 1.5, 1.5);
  }
}
} // namespace probe
