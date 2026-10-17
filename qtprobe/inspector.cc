#include "inspector.h"
#include "appearance.h"
#include "help.h"
#include "comparison.h"
#include "record_explorer.h"
#include "structure_browser.h"
#include "pv_drop.h"
#include "motif_pv_drop.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <functional>
namespace probe {
namespace {
void saveFile(const QString& path, const QByteArray& bytes) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    throw std::runtime_error(("Cannot save " + path + ": " + file.errorString()).toStdString());
}
QLabel* label(const QString& text = {}, const char* role = nullptr, qreal scale = 1, bool bold = false) {
  auto* result = new QLabel(text); result->setTextFormat(Qt::PlainText);
  if (role) result->setProperty("role", role);
  scaleFont(result, scale, bold); return result;
}
QLabel* wrappingLabel(const QString& text = {}, const char* role = nullptr, qreal scale = 1) {
  auto* result = label(text, role, scale); result->setWordWrap(true);
  result->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); return result;
}
ElidedLabel* elided(const char* name, const char* role = nullptr, qreal scale = 1, Qt::TextElideMode mode = Qt::ElideRight) {
  auto* result = new ElidedLabel(mode); result->setObjectName(name);
  if (role) result->setProperty("role", role);
  scaleFont(result, scale); return result;
}
QFrame* card(const char* name = nullptr) {
  auto* result = new QFrame; result->setProperty("card", true);
  if (name) result->setObjectName(name);
  return result;
}
QVBoxLayout* box(QWidget* owner, int margin = 10, int spacing = 6) {
  auto* layout = new QVBoxLayout(owner); layout->setContentsMargins(margin, margin, margin, margin);
  layout->setSpacing(spacing); return layout;
}
QPushButton* button(const QString& text, const char* name = nullptr, bool primary = false) {
  auto* result = new QPushButton(text); result->setCursor(Qt::PointingHandCursor);
  if (name) result->setObjectName(name);
  if (primary) result->setProperty("role", "primary");
  return result;
}
QToolButton* tool(const QString& text, Symbol symbol, const char* name = nullptr) {
  auto* result = new QToolButton; result->setText(text);
  result->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
  result->setProperty("symbol", int(symbol)); result->setIcon(symbolIcon(symbol, colors(result->palette()).muted));
  result->setCursor(Qt::PointingHandCursor); if (name) result->setObjectName(name); return result;
}
// Sizes to the current page only, so a large hidden page cannot hold the
// window open; the window then fits the page that is actually shown.
class FitStack final : public QStackedWidget {
public:
  FitStack() { connect(this, &QStackedWidget::currentChanged, this, [this] { updateGeometry(); }); }
  QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize(); }
  QSize minimumSizeHint() const override { return currentWidget() ? currentWidget()->minimumSizeHint() : QSize(); }
};
// PV tabs elide in the middle but keep enough of each name to tell them
// apart. Scroll buttons cover the rest.
class PvTabBar final : public QTabBar {
protected:
  QSize minimumTabSizeHint(int index) const override {
    auto size = QTabBar::minimumTabSizeHint(index);
    size.setWidth(std::min(tabSizeHint(index).width(), size.width() + fontMetrics().horizontalAdvance(QString(9, 'x'))));
    return size;
  }
};
class DataTable final : public QTableWidget {
public:
  DataTable(const QStringList& headers, int rows) : rows(rows) {
    setColumnCount(headers.size()); setHorizontalHeaderLabels(headers);
    setEditTriggers(QAbstractItemView::NoEditTriggers); setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectRows); setSelectionMode(QAbstractItemView::ExtendedSelection);
    verticalHeader()->hide(); verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
    horizontalHeader()->setStretchLastSection(true); setWordWrap(false); setShowGrid(false);
  }
  // Sizes a column to show this sample text in full, now and after zoom.
  void fitColumn(int column, const QString& sample) { samples[column] = sample; fitColumns(); }
  // A fixed number of visible rows keeps the window size stable as data arrives.
  QSize sizeHint() const override { return {360, height(rows)}; }
  QSize minimumSizeHint() const override { return {160, height(3)}; }
protected:
  void changeEvent(QEvent* event) override {
    QTableWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange) { verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8); fitColumns(); updateGeometry(); }
  }
  void fitColumns() {
    // Cell padding and border from the style sheet, plus the item margin.
    for (auto it = samples.cbegin(); it != samples.cend(); ++it) setColumnWidth(it.key(), textWidth(font(), it.value()) + 24);
  }
  void keyPressEvent(QKeyEvent* event) override {
    if (!event->matches(QKeySequence::Copy)) { QTableWidget::keyPressEvent(event); return; }
    auto indexes = selectionModel()->selectedIndexes();
    std::sort(indexes.begin(), indexes.end(), [](const QModelIndex& a, const QModelIndex& b) {
      return a.row() == b.row() ? a.column() < b.column() : a.row() < b.row();
    });
    QString text; int row = -1;
    for (const auto& index : indexes) {
      if (!text.isEmpty()) text += index.row() == row ? '\t' : '\n';
      text += index.data().toString(); row = index.row();
    }
    QApplication::clipboard()->setText(text); event->accept();
  }
private:
  int rows;
  QMap<int, QString> samples;
  int height(int count) const { return horizontalHeader()->sizeHint().height() + verticalHeader()->defaultSectionSize() * count + 2 * frameWidth(); }
};
void cell(QTableWidget* table, int row, int column, const QString& text) {
  if (!table->item(row, column)) table->setItem(row, column, new QTableWidgetItem);
  if (table->item(row, column)->text() != text) {
    table->item(row, column)->setText(text); table->item(row, column)->setToolTip(text);
  }
}
class CompactSpinBox final : public QDoubleSpinBox {
protected:
  QString textFromValue(double value) const override { return locale().toString(value, 'g', 10); }
};
QString infoText(const Snapshot& s) {
  QString result = s.name + '\n'; const auto fields = s.information();
  for (auto it = fields.cbegin(); it != fields.cend(); ++it) result += it.key() + ": " + it.value() + '\n';
  return result + "Value: " + s.valueText() + '\n';
}
QString alarmText(const Snapshot& s) {
  const QStringList names{"No alarm", "Minor alarm", "Major alarm", "Invalid alarm"};
  return s.severity >= 0 && s.severity < names.size() ? names[s.severity] : s.severityText();
}
QString alarmTone(const Snapshot& s) {
  return !s.connected || !s.readable || !s.hasValue ? "muted" : s.severity >= 2 ? "negative" : s.severity == 1 ? "warning" : "positive";
}
QString connectionText(const Snapshot& s) {
  return s.connected ? (s.readable ? "Connected" : "No read access") : s.error == "Stopped" ? "Stopped" : "Disconnected";
}
QString shortType(QString text) { return text.remove("DBF_"); }
QString preview(const QString& text) { return text.isEmpty() ? QString("(empty)") : text.size() > 120 ? text.left(120) + "…" : text; }
void startNameDrag(QWidget* source, const QString& name) {
  auto* mime = new QMimeData; mime->setText(name);
  QDrag drag(source); drag.setMimeData(mime); drag.exec(Qt::CopyAction);
}
} // namespace
// Display range with alarm bands and a marker for the value.
class RangeGauge final : public QWidget {
public:
  double low = 0, high = 0, value = std::numeric_limits<double>::quiet_NaN();
  double alarmLow = 0, warningLow = 0, warningHigh = 0, alarmHigh = 0;
  bool lowSet = false, highSet = false;
  QString tone = "positive";
  RangeGauge() { setObjectName("rangeGauge"); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }
  QSize sizeHint() const override { return {200, 12 + labelMetrics().height()}; }
  QSize minimumSizeHint() const override { return {60, sizeHint().height()}; }
protected:
  QFont labelFont() const { auto f = font(); if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() * 0.78); return f; }
  QFontMetrics labelMetrics() const { return QFontMetrics(labelFont()); }
  void paintEvent(QPaintEvent*) override {
    if (!(high > low)) return;
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing); const auto c = colors(window()->palette());
    const QRectF bar(4, 4, width() - 8, 6);
    auto x = [&](double v) { return bar.left() + bar.width() * std::clamp((v - low) / (high - low), 0.0, 1.0); };
    QPainterPath shape; shape.addRoundedRect(bar, 3, 3);
    p.fillPath(shape, c.raised); p.save(); p.setClipPath(shape); p.setPen(Qt::NoPen);
    auto band = [&](double a, double b, QColor color) {
      if (!(b > a)) return;
      color.setAlpha(c.dark ? 150 : 110); p.setBrush(color); p.drawRect(QRectF(x(a), bar.top(), x(b) - x(a), bar.height()));
    };
    if (lowSet) { band(low, std::min(alarmLow, warningLow), c.negative); band(std::min(alarmLow, warningLow), std::max(alarmLow, warningLow), c.warning); }
    if (highSet) { band(std::min(warningHigh, alarmHigh), std::max(warningHigh, alarmHigh), c.warning); band(std::max(warningHigh, alarmHigh), high, c.negative); }
    p.restore(); p.setPen(QPen(c.border, 1)); p.setBrush(Qt::NoBrush); p.drawPath(shape);
    if (std::isfinite(value)) {
      const QColor marker = tone == "negative" ? c.negative : tone == "warning" ? c.warning : tone == "muted" ? c.muted : c.accent;
      p.setPen(Qt::NoPen); p.setBrush(marker);
      if (value < low || value > high) {
        // Out of range: an arrow at the nearer end.
        const qreal edge = value < low ? bar.left() : bar.right(), d = value < low ? -1 : 1;
        p.drawPolygon(QPolygonF(QVector<QPointF>{{edge - 4 * d, bar.top() - 3}, {edge + 3 * d, bar.center().y()}, {edge - 4 * d, bar.bottom() + 3}}));
      } else p.drawRoundedRect(QRectF(x(value) - 1.75, bar.top() - 3, 3.5, bar.height() + 6), 1.5, 1.5);
    }
    p.setFont(labelFont()); p.setPen(c.muted);
    const QRectF labels(bar.left(), bar.bottom() + 2, bar.width(), labelMetrics().height());
    p.drawText(labels, Qt::AlignLeft | Qt::AlignTop, QString::number(low, 'g', 6));
    p.drawText(labels, Qt::AlignRight | Qt::AlignTop, QString::number(high, 'g', 6));
  }
};
ElidedLabel::ElidedLabel(Qt::TextElideMode mode, QWidget* parent) : QLabel(parent), mode(mode) { setTextFormat(Qt::PlainText); }
QSize ElidedLabel::sizeHint() const {
  auto size = QLabel::sizeHint(); const auto margins = contentsMargins();
  const int full = textWidth(font(), text().section('\n', 0, 0)) + margins.left() + margins.right() + 2 * margin();
  size.setWidth(std::min(std::max(size.width(), full), 480)); return size;
}
QSize ElidedLabel::minimumSizeHint() const {
  auto size = QLabel::minimumSizeHint();
  size.setWidth(std::min(size.width(), fontMetrics().horizontalAdvance(QStringLiteral("…")) * 4)); return size;
}
void ElidedLabel::paintEvent(QPaintEvent*) {
  QPainter p(this); const auto area = contentsRect(); auto font = this->font();
  auto shown = text(); const int newline = shown.indexOf('\n');
  if (newline >= 0) shown = shown.left(newline) + "…";
  const qreal minimum = minimumScale * QApplication::font().pointSizeF();
  if (minimum > 0 && font.pointSizeF() > 0)
    while (font.pointSizeF() > minimum && QFontMetricsF(font).horizontalAdvance(shown) > area.width())
      font.setPointSizeF(std::max(minimum, font.pointSizeF() - 0.5));
  p.setFont(font); p.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, foregroundRole()));
  p.drawText(area, int(alignment()), QFontMetrics(font).elidedText(shown, mode, area.width()));
}
void Plot::paintEvent(QPaintEvent*) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing); const auto c = colors(window()->palette());
  if (!sparkline) p.fillRect(rect(), c.surface);
  double lo = std::numeric_limits<double>::infinity(), hi = -lo;
  for (double v : values) if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
  if (!std::isfinite(lo)) {
    if (sparkline) return;
    p.setPen(c.muted); symbolIcon(Symbol::Wave, c.muted).paint(&p, QRect(width() / 2 - 12, height() / 2 - 26, 24, 24));
    p.drawText(QRect(10, height() / 2 + 2, width() - 20, 30), Qt::AlignHCenter | Qt::AlignTop, "Numeric updates will appear here"); return;
  }
  const bool discrete = !levels.isEmpty();
  // States sit on whole numbers with half a state of margin; every state
  // stays on the axis even when it has not occurred.
  const double scale = discrete ? 1 : std::max({std::abs(lo), std::abs(hi), 1e-300});
  double low = discrete ? std::min(lo, 0.0) - 0.5 : lo / scale, high = discrete ? std::max(hi, levels.size() - 1.0) + 0.5 : hi / scale;
  if (low == high) { low -= 0.08; high += 0.08; }
  auto tick = [&](double fraction) {
    const long double number = (static_cast<long double>(low) + (high - low) * fraction) * scale;
    const auto bounded = std::clamp(number, -static_cast<long double>(std::numeric_limits<double>::max()), static_cast<long double>(std::numeric_limits<double>::max()));
    return QString::number(double(bounded), 'g', 6);
  };
  QRectF area = QRectF(rect()).adjusted(2, 2, -4, -2);
  if (!sparkline) {
    auto axisFont = font(); if (axisFont.pointSizeF() > 0) axisFont.setPointSizeF(axisFont.pointSizeF() * 0.82); p.setFont(axisFont);
    // Grid rows: four evenly spaced numbers, or one per enum state.
    QVector<QPair<double, QString>> rows;
    if (discrete) for (int state = int(std::ceil(low)); state <= int(std::floor(high)); ++state) rows << qMakePair(double(state), levels.value(state, QString::number(state)));
    else for (int i = 0; i <= 3; ++i) rows << qMakePair(low + (high - low) * i / 3, tick(i / 3.0));
    const int widest = discrete ? std::max(48, width() / 4) : std::numeric_limits<int>::max();
    int labelWidth = 0; for (const auto& row : rows) labelWidth = std::max(labelWidth, std::min(widest, textWidth(p.font(), row.second)));
    area = QRectF(rect()).adjusted(labelWidth + 14, 8, -12, -p.fontMetrics().height() - 8);
    if (area.width() <= 0 || area.height() <= 0) return;
    // Thin state labels that would overlap; their grid lines stay.
    const int every = discrete ? std::max(1, int(std::ceil(p.fontMetrics().height() * 1.1 / (area.height() / (high - low))))) : 1;
    for (int i = 0; i < rows.size(); ++i) {
      const auto y = area.bottom() - area.height() * (rows[i].first - low) / (high - low);
      p.setPen(QPen(c.border, 0.8, Qt::DashLine)); p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
      if (i % every) continue;
      p.setPen(c.muted); p.drawText(QRectF(0, y - p.fontMetrics().height() / 2, labelWidth + 6, p.fontMetrics().height()), Qt::AlignRight | Qt::AlignVCenter,
          p.fontMetrics().elidedText(rows[i].second, Qt::ElideRight, labelWidth));
    }
    for (const auto& [lines, color] : {std::make_pair(&warningLines, c.warning), std::make_pair(&alarmLines, c.negative)})
      for (double limit : *lines) {
        const double scaled = limit / scale;
        if (!std::isfinite(scaled) || scaled < low || scaled > high) continue;
        const auto y = area.bottom() - area.height() * (scaled - low) / (high - low);
        p.setPen(QPen(color, 1.2, Qt::DashLine)); p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
      }
  }
  if (area.width() <= 0 || area.height() <= 0) return;
  // A single value sits in the middle of the axis rather than at its start.
  auto point = [&](int index) { return QPointF(values.size() == 1 ? area.center().x() : area.left() + area.width() * index / (values.size() - 1), area.bottom() - area.height() * (values[index] / scale - low) / (high - low)); };
  const bool dense = values.size() > area.width() * 2;
  QPainterPath path, fill; QPolygonF run; QVector<QPolygonF> traces;
  bool first = true; QPointF start, last;
  auto finishFill = [&] {
    if (first) return;
    if (dense) { traces.append(std::move(run)); run = {}; }
    else { fill.lineTo(last.x(), area.bottom()); fill.lineTo(start.x(), area.bottom()); fill.closeSubpath(); }
  };
  for (int i : plotEnvelope(values, std::max(1, int(area.width())))) {
    if (i < 0) { finishFill(); first = true; continue; }
    const auto pt = point(i);
    // A state holds until the next event: draw across, then up or down.
    const QPointF corner(pt.x(), last.y());
    if (dense) { if (discrete && !first) run << corner; run << pt; }
    else if (first) { path.moveTo(pt); fill.moveTo(pt); }
    else { if (discrete) path.lineTo(corner); path.lineTo(pt); fill.lineTo(pt); }
    if (first) { start = pt; first = false; }
    last = pt;
  }
  finishFill();
  auto tint = c.accent; tint.setAlpha(c.dark ? 40 : 25);
  QLinearGradient gradient(area.topLeft(), area.bottomLeft()); gradient.setColorAt(0, tint); tint.setAlpha(0); gradient.setColorAt(1, tint);
  p.save(); p.setClipRect(area.adjusted(-3, -3, 3, 3));
  if (dense) {
    // Dense traces already occupy pixel columns. A one-pixel polyline avoids
    // the expensive translucent polygon and wide antialiased path stroker.
    p.setRenderHint(QPainter::Antialiasing, false); p.setPen(QPen(c.accent, 1));
    for (const auto& trace : traces) p.drawPolyline(trace);
    p.setRenderHint(QPainter::Antialiasing, true);
  } else {
    if (!sparkline && !discrete) p.fillPath(fill, gradient);
    p.setPen(QPen(c.accent, sparkline ? 1.3 : 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.drawPath(path);
  }
  if (!first && !sparkline) { p.setBrush(c.surface); p.drawEllipse(last, 3.5, 3.5); }
  p.restore();
  if (sparkline) return;
  // One axis line: first index, title, last index.
  p.setPen(c.muted);
  const QRectF axis(area.left(), area.bottom() + 4, area.width(), p.fontMetrics().height());
  const QString lastIndex = QString::number(std::max(0, int(values.size()) - 1));
  // With one value, end labels would suggest a span; the title alone remains.
  if (values.size() > 1) { p.drawText(axis, Qt::AlignLeft | Qt::AlignTop, "0"); p.drawText(axis, Qt::AlignRight | Qt::AlignTop, lastIndex); }
  const int side = p.fontMetrics().horizontalAdvance(lastIndex) + 12;
  QString axisTitle = timeAxis ? "Monitor events · oldest to newest" : "Element index";
  if (timeAxis && p.fontMetrics().horizontalAdvance(axisTitle) > axis.width() - 2 * side) axisTitle = "Monitor events";
  p.drawText(axis.adjusted(side, 0, -side, 0), Qt::AlignHCenter | Qt::AlignTop,
      p.fontMetrics().elidedText(axisTitle, Qt::ElideRight, std::max(0, int(axis.width()) - 2 * side)));
}
Inspector::Inspector(Address address, Options options, QWidget* parent) : QWidget(parent) {
  render = new QTimer(this); render->setObjectName("inspectorRenderTimer"); render->setInterval(100); render->setSingleShot(true);
  connect(render, &QTimer::timeout, this, [this] { if (dirty && !frozen && isVisible() && !window()->isMinimized()) refresh(false); });
  setObjectName("inspector");
  channel_ = new Channel(address, options.readOnly, options.maxElements, options.historySize, options.timeout, this);
  requested = QSettings().value("sections", 0).toInt() & AllSections;
  auto* outer = box(this, 0, 0); scroll = new QScrollArea; scroll->setObjectName("inspectorScroll"); scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); body = card("inspectorBody");
  scroll->setWidget(body); outer->addWidget(scroll); auto* layout = box(body, 10, 6);
  // Header: connection dot, name, alarm state and provider.
  auto* header = new QHBoxLayout; header->setSpacing(6);
  stateDot = label("●"); stateDot->setObjectName("connectionStatus"); stateDot->setAccessibleName("Connecting"); header->addWidget(stateDot);
  heading = elided("pvHeading", nullptr, 1.12, Qt::ElideMiddle); heading->setText(address.name);
  heading->setToolTip(address.uri() + "\nDrag the name to copy it into another tool");
  scaleFont(heading, 1.12, true); heading->setCursor(Qt::OpenHandCursor); heading->installEventFilter(this); header->addWidget(heading);
  auto* copy = tool("", Symbol::Copy, "copyPvButton"); copy->setProperty("role", "quiet"); copy->setToolTip("Copy PV address"); copy->setAccessibleName("Copy PV address");
  connect(copy, &QToolButton::clicked, this, [address] { QApplication::clipboard()->setText(address.uri()); });
  header->addWidget(copy); header->addStretch(1);
  paused = label("Paused", "badge", 0.86); paused->setObjectName("pausedBadge"); setTone(paused, "warning"); paused->hide(); header->addWidget(paused);
  alarm = label("Connecting", "badge", 0.86); alarm->setObjectName("alarmBadge"); header->addWidget(alarm);
  auto* protocol = label(address.provider == "pva" ? "PVA" : "CA", "muted", 0.86); protocol->setToolTip(address.provider == "pva" ? "pvAccess" : "Channel Access"); header->addWidget(protocol);
  layout->addLayout(header);
  description = elided("pvDescription", "muted"); description->hide(); layout->addWidget(description);
  // Readback: value, units and display format.
  auto* readback = new QHBoxLayout; readback->setSpacing(8);
  value = elided("value"); value->setText("—");
  scaleFont(value, 2.1, true, true); value->minimumScale = 1.1; value->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(value, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    QMenu menu(this); auto* copyValue = menu.addAction("Copy value"); auto* copyShown = menu.addAction("Copy displayed text");
    auto* picked = menu.exec(value->mapToGlobal(pos));
    if (picked == copyValue) QApplication::clipboard()->setText(channel_->snapshot.exactText());
    else if (picked == copyShown) QApplication::clipboard()->setText(channel_->snapshot.valueText(format->currentText()));
  });
  readback->addWidget(value); units = label({}, "muted", 1.15); units->setObjectName("valueUnits"); readback->addWidget(units, 0, Qt::AlignVCenter); readback->addStretch(1);
  format = new QComboBox; format->setObjectName("format"); format->setAccessibleName("Value display format"); format->setToolTip("Value display format");
  format->addItems({"Native", "Fixed", "Scientific", "Hex", "Binary", "Text"});
  format->setItemData(5, "Decode a byte array as text", Qt::ToolTipRole); readback->addWidget(format, 0, Qt::AlignVCenter); layout->addLayout(readback);
  gauge = new RangeGauge; gauge->hide(); layout->addWidget(gauge);
  facts = wrappingLabel({}, "muted", 0.9); facts->setObjectName("pvFacts"); facts->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(facts);
  // Write row. Enter or Write sends; the other controls only stage the edit.
  writeArea = new QWidget; writeArea->setObjectName("writeArea"); auto* writeLayout = box(writeArea, 0, 3);
  auto* writeRow = new QHBoxLayout; writeRow->setSpacing(6);
  enumChoice = new QComboBox; enumChoice->setObjectName("enumChoice"); enumChoice->setAccessibleName("Enum state"); enumChoice->hide();
  input = new QLineEdit; input->setObjectName("writeValue"); input->setAccessibleName("Proposed PV value"); input->setPlaceholderText("New value");
  useReadback = button("Readback", "useReadbackButton"); useReadback->setProperty("role", "quiet");
  undo = button("Undo", "undoWriteButton"); undo->setProperty("role", "quiet"); undo->setEnabled(false); undo->setToolTip("No write to undo yet");
  put = button("Write", "writeButton", true); put->setEnabled(false); put->setToolTip("Send the value (Enter)");
  writeRow->addWidget(enumChoice); writeRow->addWidget(input, 1); writeRow->addWidget(useReadback); writeRow->addWidget(undo); writeRow->addWidget(put); writeLayout->addLayout(writeRow);
  writeStatus = elided("writeStatus", "muted", 0.86); writeLayout->addWidget(writeStatus); layout->addWidget(writeArea);
  connect(put, &QPushButton::clicked, this, [this] { requestWrite(input->text()); });
  connect(input, &QLineEdit::returnPressed, this, [this] { if (put->isEnabled()) requestWrite(input->text()); });
  connect(undo, &QPushButton::clicked, this, [this] { requestUndo(); });
  connect(input, &QLineEdit::textEdited, this, [this] { lastWriteMessage.clear(); refreshWriteControls(); });
  connect(enumChoice, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
    // Stage text that selects this state even when labels read as indices.
    const auto text = enumWriteText(lastChoices, i);
    if (text.isEmpty()) { lastWriteMessage = "This state cannot be entered: its label repeats and its index is another state's label."; lastWriteOK = false; }
    else { input->setText(text); lastWriteMessage.clear(); }
    refreshWriteControls();
  });
  connect(useReadback, &QPushButton::clicked, this, [this] {
    input->setText(channel_->snapshot.editText());
    lastWriteMessage.clear(); input->setFocus(); input->selectAll(); refreshWriteControls();
  });
  // Section toggles on the left, channel commands on the right.
  auto* bar = new QHBoxLayout; bar->setSpacing(4);
  const char* titles[] = {"Trend", "Adjust", "Info", "Array", "History", "Record", "Structure"};
  const char* names[] = {"trendToggle", "adjustToggle", "infoToggle", "arrayToggle", "historyToggle", "recordToggle", "structureToggle"};
  const char* tips[] = {"Plot recent numeric updates", "Limits, slider and step controls that stage a value",
      "Metadata and record fields", "Array values and index plot", "Monitor events with IOC timestamps",
      "Record processing fields and links, with reasons it may not update",
      "Every field of the pvAccess structure with its type and live value"};
  for (int i = 0; i < sectionCount; ++i) {
    auto* toggle = new QToolButton; toggle->setText(titles[i]); toggle->setObjectName(names[i]); toggle->setToolTip(tips[i]);
    toggle->setCheckable(true); toggle->setProperty("role", "toggle"); toggle->setCursor(Qt::PointingHandCursor); toggles[i] = toggle; bar->addWidget(toggle);
    connect(toggle, &QToolButton::clicked, this, [this] {
      int mask = 0; for (int n = 0; n < sectionCount; ++n) if (toggles[n]->isChecked()) mask |= 1 << n;
      QSettings().setValue("sections", mask);
      if (sectionsRequested) sectionsRequested(mask); else setSections(mask);
    });
  }
  bar->addStretch(1);
  processButton = tool("", Symbol::Process, "processButton"); processButton->setProperty("role", "quiet"); processButton->hide();
  processButton->setToolTip("Process the record (write 1 to its PROC field)"); processButton->setAccessibleName("Process record"); bar->addWidget(processButton);
  connect(processButton, &QToolButton::clicked, this, [this] { requestProcess(); });
  auto* reconnect = tool("", Symbol::Reconnect, "reconnectButton"); reconnect->setProperty("role", "quiet"); reconnect->setToolTip("Reconnect"); reconnect->setAccessibleName("Reconnect"); bar->addWidget(reconnect);
  connect(reconnect, &QToolButton::clicked, channel_, &Channel::start);
  auto* stop = tool("", Symbol::Stop, "stopButton"); stop->setProperty("role", "quiet"); stop->setToolTip("Stop and release the channel"); stop->setAccessibleName("Stop"); bar->addWidget(stop);
  connect(stop, &QToolButton::clicked, channel_, &Channel::stop);
  freeze = tool("", Symbol::Pause, "pauseDisplay"); freeze->setProperty("role", "quiet"); freeze->setCheckable(true); freeze->setToolTip("Pause display; acquisition continues"); freeze->setAccessibleName("Pause display"); bar->addWidget(freeze);
  connect(freeze, &QToolButton::toggled, this, [this](bool b) {
    // Populate the hidden sections once so opening a section while paused
    // shows the same held snapshot, then stop scheduling visual work.
    if (b) refresh();
    frozen = b; paused->setVisible(b); render->stop(); if (!b) { invalidate(); refresh(false); }
  });
  layout->addLayout(bar);
  struct Panel { QVBoxLayout* content; QHBoxLayout* header; };
  auto section = [&](int index, const char* name, const QString& title, int stretch) {
    auto* panel = new QFrame; panel->setObjectName(name); panel->setProperty("section", true); panel->hide();
    auto* content = new QVBoxLayout(panel); content->setContentsMargins(0, 6, 0, 0); content->setSpacing(4);
    auto* top = new QHBoxLayout; top->setSpacing(6); top->addWidget(label(title, "eyebrow", 0.76)); content->addLayout(top);
    panels[index] = panel; layout->addWidget(panel, stretch); return Panel{content, top};
  };
  auto trendPanel = section(0, "trendSection", "TREND", 2);
  activityCount = label("Waiting for updates", "muted", 0.8); trendPanel.header->addStretch(); trendPanel.header->addWidget(activityCount);
  trendPlot = new Plot; trendPlot->setObjectName("trendPlot"); trendPlot->timeAxis = true; trendPlot->preferredHeight = 120; trendPanel.content->addWidget(trendPlot, 1);
  trendStats = wrappingLabel("Waiting for numeric samples", "muted", 0.86); trendStats->setObjectName("trendStatistics"); trendPanel.content->addWidget(trendStats);
  auto adjustPanel = section(1, "numericControls", "ADJUST", 0); numericControls = panels[1];
  auto* range = new QHBoxLayout; range->setSpacing(6);
  low = new CompactSpinBox; high = new CompactSpinBox; step = new CompactSpinBox;
  const QStringList rangeTitles{"Min", "Max", "Step"}, rangeNames{"Minimum", "Maximum", "Step size"}; const QList<QDoubleSpinBox*> spins{low, high, step};
  for (int i = 0; i < spins.size(); ++i) {
    range->addWidget(label(rangeTitles[i], "muted", 0.86));
    spins[i]->setRange(-1e100, 1e100); spins[i]->setDecimals(8); spins[i]->setButtonSymbols(QAbstractSpinBox::NoButtons); spins[i]->setAccessibleName(rangeNames[i]); range->addWidget(spins[i], 1);
  }
  low->setValue(0); high->setValue(100); step->setRange(0.00000001, 1e100); step->setValue(1); adjustPanel.content->addLayout(range);
  auto* sliderRow = new QHBoxLayout; sliderRow->setSpacing(6);
  slider = new QSlider(Qt::Horizontal); slider->setRange(0, 10000); slider->setObjectName("adjustSlider"); slider->setAccessibleName("Stage a numeric value");
  slider->setToolTip("Stages a value in the editor; press Write to send it");
  for (int sign : {-1, 1}) {
    auto* b = button(sign < 0 ? "−" : "+", sign < 0 ? "stepDownButton" : "stepUpButton"); b->setToolTip(sign < 0 ? "Stage the value minus one step" : "Stage the value plus one step");
    connect(b, &QPushButton::clicked, this, [this, sign] {
      bool ok; double n = input->text().toDouble(&ok); if (!ok) n = channel_->snapshot.number;
      if (std::isfinite(n)) { input->setText(QString::number(n + sign * step->value(), 'g', 17)); lastWriteMessage.clear(); refreshWriteControls(); }
    });
    if (sign < 0) { sliderRow->addWidget(b); sliderRow->addWidget(slider, 1); } else sliderRow->addWidget(b);
  }
  auto* showDial = tool("Dial", Symbol::Wave, "showDialButton"); showDial->setCheckable(true); showDial->setProperty("role", "quiet"); sliderRow->addWidget(showDial); adjustPanel.content->addLayout(sliderRow);
  dial = new QDial; dial->setRange(0, 10000); dial->setFixedSize(80, 80); dial->setNotchesVisible(true); dial->setAccessibleName("Stage a value with the dial"); dial->hide(); adjustPanel.content->addWidget(dial, 0, Qt::AlignHCenter);
  connect(showDial, &QToolButton::toggled, this, [this](bool b) { dial->setVisible(b); checkLayout(); });
  connect(slider, &QSlider::valueChanged, this, [this](int v) { stagePosition(v); }); connect(dial, &QDial::valueChanged, this, [this](int v) { stagePosition(v); });
  connect(low, &QDoubleSpinBox::editingFinished, this, [this] { automaticLimits = false; }); connect(high, &QDoubleSpinBox::editingFinished, this, [this] { automaticLimits = false; });
  auto infoPanel = section(2, "infoSection", "INFORMATION", 3);
  filter = new QLineEdit; filter->setObjectName("metadataFilter"); filter->setPlaceholderText("Find a field or value…"); filter->setClearButtonEnabled(true); filter->setAccessibleName("Filter PV information"); infoPanel.header->addWidget(filter, 1);
  auto* copyInfo = tool("", Symbol::Copy, "copyInfoButton"); copyInfo->setProperty("role", "quiet"); copyInfo->setToolTip("Copy all information"); copyInfo->setAccessibleName("Copy all information"); infoPanel.header->addWidget(copyInfo);
  connect(copyInfo, &QToolButton::clicked, this, [this] { QApplication::clipboard()->setText(infoText(channel_->snapshot)); });
  info = new DataTable({"Field", "Value"}, 9); info->setObjectName("information"); info->setColumnWidth(0, 180); infoPanel.content->addWidget(info, 1);
  auto arrayPanel = section(3, "arraySection", "ARRAY", 3);
  arrayNotice = wrappingLabel({}, "muted", 0.86); arrayPanel.content->addWidget(arrayNotice);
  waveform = new Plot; waveform->preferredHeight = 110; arrayPanel.content->addWidget(waveform);
  array = new DataTable({"Index", "Native value"}, 6); array->setObjectName("arrayValues"); arrayPanel.content->addWidget(array, 1);
  auto historyPanel = section(4, "historySection", "HISTORY", 3);
  historyCount = label({}, "muted", 0.8); historyPanel.header->addStretch(); historyPanel.header->addWidget(historyCount);
  auto* clear = button("Clear", "clearHistoryButton"); clear->setProperty("role", "quiet"); clear->setToolTip("Clear this PV's local history"); historyPanel.header->addWidget(clear);
  events = new DataTable({"IOC timestamp (UTC)", "Value", "State"}, 8); events->setObjectName("historyValues"); static_cast<DataTable*>(events)->fitColumn(0, "0000-00-00T00:00:00.000000000Z"); events->setColumnWidth(1, 140); historyPanel.content->addWidget(events, 1);
  connect(clear, &QPushButton::clicked, this, [this] { channel_->history.clear(); invalidate(); refresh(false); });
  auto recordPanel = section(5, "recordSection", "RECORD", 3);
  explorer = new RecordExplorer(channel_, options.timeout); recordPanel.content->addWidget(explorer, 1);
  explorer->openLink = [this](const QString& target) {
    if (openLink) openLink(target.contains("://") ? target : channel_->snapshot.provider + "://" + target);
  };
  auto structurePanel = section(6, "structureSection", "STRUCTURE", 3);
  structureHeader = structurePanel.header; structureContent = structurePanel.content;
  // Takes spare height only when no stretching section is open.
  layout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
  connect(format, &QComboBox::currentTextChanged, this, [this] { invalidate(); refresh(false); }); connect(filter, &QLineEdit::textChanged, this, [this] { invalidate(); refresh(false); });
  connect(channel_, &Channel::changed, this, [this] { invalidate(); refreshWriteControls(); });
  connect(channel_, &Channel::writeFinished, this, [this](bool ok, const QString& message) {
    lastWriteMessage = message; lastWriteOK = ok;
    if (pendingLog) {
      pendingLog = false; const auto& s = channel_->snapshot;
      logWrite({QDateTime::currentDateTime(), s.provider + "://" + s.name, pendingBefore, pendingAfter, message, ok});
      if (ok && !pendingProcess) { undoValue = pendingBefore; undoValues = pendingBeforeValues; hasUndo = pendingUndoable; undoLost = !pendingUndoable; }
      if (logged) logged();
    }
    refreshWriteControls();
  });
  refresh(); if (options.connectOnOpen) QTimer::singleShot(0, channel_, &Channel::start);
}
StructureBrowser* Inspector::structureBrowser() {
  if (!structure) {
    structure = new StructureBrowser; structureHeader->addWidget(structure->controls(), 1); structureContent->addWidget(structure, 1);
    // Icons are drawn for a theme; this one may be newer than the controls.
    for (auto* b : structure->controls()->findChildren<QToolButton*>())
      if (b->property("symbol").isValid()) b->setIcon(symbolIcon(Symbol(b->property("symbol").toInt()), colors(palette()).muted));
  }
  return structure;
}
void Inspector::invalidate() { dirty = true; if (!frozen && isVisible() && !window()->isMinimized() && !render->isActive()) render->start(); }
void Inspector::showEvent(QShowEvent* event) { QWidget::showEvent(event); explorer->setActive(shown & RecordSection); if (dirty) refresh(false); }
void Inspector::hideEvent(QHideEvent* event) { render->stop(); explorer->setActive(false); releaseViews(AllSections); QWidget::hideEvent(event); }
void Inspector::releaseViews(int sections) {
  // A paused display must keep showing what it held.
  if (frozen) return;
  bool released = false;
  if ((sections & TrendSection) && !trendPlot->values.isEmpty()) { QVector<double>().swap(trendPlot->values); released = true; }
  if ((sections & HistorySection) && events->rowCount()) { events->setRowCount(0); displayedHistorySerial = 0; released = true; }
  if ((sections & ArraySection) && array->rowCount()) { array->setRowCount(0); QVector<double>().swap(waveform->values); released = true; }
  if ((sections & InfoSection) && info->rowCount()) { info->setRowCount(0); released = true; }
  if ((sections & StructureSection) && structure && !structure->empty()) { structure->release(); released = true; }
  if (released) dirty = true;
}
void Inspector::requestWrite(const QString& text) {
  if (beginWrite(text)) channel_->write(text);
}
void Inspector::requestUndo() {
  // The saved elements bypass text parsing, which depends on the PV's current
  // contents (enum labels, text in byte arrays).
  if (hasUndo && beginWrite(undoValue)) channel_->writeValues(undoValues);
}
bool Inspector::beginWrite(const QString& shown) {
  const auto& s = channel_->snapshot;
  if (confirm && !confirm(QString("Write %1 to %2?\nCurrent value: %3").arg(preview(shown), s.name, preview(s.hasValue ? s.editText() : QString())))) return false;
  pendingLog = true; pendingProcess = false; pendingBefore = s.hasValue ? s.editText() : QString(); pendingAfter = shown;
  // Undo restores these exact elements; a truncated readback cannot be restored.
  pendingBeforeValues = s.values; pendingUndoable = s.hasValue && !s.truncated && !s.values.isEmpty();
  return true;
}
void Inspector::requestProcess() {
  const auto& s = channel_->snapshot;
  if (confirm && !confirm(QString("Process the record for %1?").arg(s.name))) return;
  pendingLog = true; pendingProcess = true; pendingBefore.clear(); pendingAfter = "Process (PROC = 1)";
  channel_->process();
}
bool Inspector::eventFilter(QObject* object, QEvent* event) {
  // Drag the PV name into QtEDM, QtStripTool or another QtProbe.
  if (object == heading) {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (event->type() == QEvent::MouseButtonPress && (mouse->button() == Qt::LeftButton || mouse->button() == Qt::MiddleButton)) dragStart = mouse->pos();
    else if (event->type() == QEvent::MouseMove && (mouse->buttons() & (Qt::LeftButton | Qt::MiddleButton)) &&
             (mouse->pos() - dragStart).manhattanLength() >= QApplication::startDragDistance()) {
      const auto& s = channel_->snapshot; startNameDrag(heading, portableName(s.provider, s.name)); return true;
    }
  }
  return QWidget::eventFilter(object, event);
}
int Inspector::preferredHeight(int width) const {
  return (body->hasHeightForWidth() ? body->heightForWidth(width) : body->sizeHint().height()) + 2;
}
QSize Inspector::sizeHint() const { const int w = std::max(width(), body->sizeHint().width()); return {w, preferredHeight(w)}; }
QSize Inspector::minimumSizeHint() const { return {body->minimumSizeHint().width(), 120}; }
void Inspector::setSections(int mask) {
  requested = mask & AllSections; applySections();
  if (isVisible()) refresh(false); else dirty = true;
}
void Inspector::applySections() {
  shown = requested & available;
  for (int i = 0; i < sectionCount; ++i) {
    const int bit = 1 << i;
    toggles[i]->setVisible(available & bit);
    if (toggles[i]->isChecked() != bool(requested & bit)) { QSignalBlocker block(toggles[i]); toggles[i]->setChecked(requested & bit); }
    panels[i]->setVisible(shown & bit);
  }
  releaseViews(AllSections & ~shown);
  // Field channels run only while the section is visible.
  explorer->setActive((shown & RecordSection) && isVisible());
  checkLayout();
}
void Inspector::checkLayout() {
  const int now = shown | (writeArea->isVisibleTo(body) ? 1 << sectionCount : 0) | (description->isVisibleTo(body) ? 2 << sectionCount : 0) |
      (dial->isVisibleTo(body) ? 4 << sectionCount : 0) | (gauge->isVisibleTo(body) ? 8 << sectionCount : 0);
  if (now == signature) return;
  signature = now; updateGeometry(); if (layoutChanged) layoutChanged();
}
void Inspector::stagePosition(int pos) {
  if (high->value() <= low->value()) return;
  const auto n = low->value() + (high->value() - low->value()) * pos / 10000;
  input->setText(QString::number(channel_->snapshot.precision == 0 ? std::round(n) : n, 'g', 17));
  lastWriteMessage.clear(); refreshWriteControls(); QSignalBlocker bs(slider), bd(dial); slider->setValue(pos); dial->setValue(pos);
}
void Inspector::refreshWriteControls() {
  const auto& s = channel_->snapshot;
  // Keep the row while disconnected so a flapping link cannot resize the window.
  if (s.connected) writableSeen = s.writable;
  writeArea->setVisible(!channel_->readOnly() && writableSeen);
  const bool writable = !channel_->readOnly() && s.connected && s.writable && s.hasValue;
  const bool ready = writable && !channel_->writePending() && !channel_->writeUncertain();
  put->setEnabled(ready); input->setEnabled(writable); enumChoice->setEnabled(writable);
  undo->setEnabled(ready && hasUndo);
  undo->setToolTip(hasUndo ? "Restore " + preview(undoValue) + ", the value before your last write" :
      undoLost ? "Undo unavailable: the readback before your last write was truncated" : "No write to undo yet");
  processButton->setVisible(!channel_->readOnly() && s.processable);
  processButton->setEnabled(s.connected && !channel_->writePending() && !channel_->writeUncertain());
  useReadback->setEnabled(writable && s.readable && (!s.array || !s.truncated) && (s.choices.isEmpty() || !s.editText().isEmpty()));
  useReadback->setToolTip(s.truncated ? "Readback is truncated; use a complete array for replacement writes." : "Copy the exact current readback into the value editor");
  QString message, tone = "muted";
  if (channel_->readOnly()) message = "Read-only session · writes are disabled.";
  else if (!s.connected || !s.hasValue) message = "Connect to the PV to write a value.";
  else if (!s.writable) message = "The server does not allow writes to this PV.";
  else if (channel_->writePending()) message = "Writing… waiting for server completion.";
  else if (channel_->writeUncertain()) { message = "Write outcome is uncertain. Check readback and reconnect before retrying."; tone = "warning"; }
  else if (!lastWriteMessage.isEmpty()) { message = lastWriteMessage; tone = lastWriteOK ? "positive" : "negative"; }
  else message = s.longString ? "Enter text. Writing replaces the whole string." : s.array ? "Enter a JSON array. Writing replaces the value array." : "Enter or Write sends the value. Adjust and enum choices only stage it.";
  writeStatus->setText(message); writeStatus->setToolTip(message); setTone(writeStatus, tone);
  checkLayout();
}
void Inspector::refresh(bool allSections) {
  refreshWriteControls(); if (frozen) return; dirty = false; render->stop();
  const auto& s = channel_->snapshot;
  const auto text = s.valueText(format->currentText());
  const auto exact = s.exactText();
  value->setText(text.size() > 200 ? text.left(200) + "…" : text); value->setToolTip(exact.size() > 2000 ? exact.left(2000) + "…" : exact);
  units->setText(s.units); units->setVisible(!s.units.isEmpty());
  const auto connection = connectionText(s);
  stateDot->setToolTip(connection); stateDot->setAccessibleName(connection); setTone(stateDot, s.connected && s.readable ? "positive" : "muted");
  alarm->setText(!s.connected || !s.readable ? connection : s.hasValue ? alarmText(s) : "Awaiting data"); setTone(alarm, alarmTone(s));
  setTone(value, !s.connected || !s.readable ? "muted" : s.severity ? alarmTone(s) : "normal");
  description->setText(s.description); description->setToolTip(s.description); description->setVisible(!s.description.isEmpty());
  QStringList parts;
  if (!s.error.isEmpty()) parts << s.error;
  if (!s.type.isEmpty()) parts << shortType(s.type);
  if (s.hasValue) parts << QString("%1 element%2").arg(s.count).arg(s.count == 1 ? "" : "s");
  if (channel_->readOnly()) parts << "read-only session";
  else if (s.connected) parts << (s.readable ? (s.writable ? "read/write" : "read only") : s.writable ? "write only" : "no access");
  parts << (s.timestamp().isEmpty() ? "waiting for an IOC timestamp" : s.timestamp());
  facts->setText(parts.join(" · ")); facts->setToolTip(s.host.isEmpty() ? QString() : "Host: " + s.host);
  setTone(facts, s.error.isEmpty() || s.error == "Stopped" ? "muted" : "warning");
  if (s.choices != lastChoices) { QSignalBlocker b(enumChoice); enumChoice->clear(); enumChoice->addItems(s.choices); lastChoices = s.choices; }
  enumChoice->setVisible(!s.choices.isEmpty()); if (!s.choices.isEmpty()) { QSignalBlocker b(enumChoice); enumChoice->setCurrentIndex(int(s.number)); }
  input->setPlaceholderText(s.longString ? "Text" : s.array ? "JSON array, for example [1, 2, 3]" : "New value");
  const bool numeric = s.numeric && !s.array && s.choices.isEmpty();
  gauge->setVisible(numeric && s.hasValue && s.displayHigh > s.displayLow);
  if (gauge->isVisibleTo(body)) {
    gauge->low = s.displayLow; gauge->high = s.displayHigh; gauge->value = s.connected && s.readable ? s.number : std::numeric_limits<double>::quiet_NaN();
    gauge->lowSet = s.lowLimitsSet(); gauge->highSet = s.highLimitsSet(); gauge->tone = alarmTone(s);
    gauge->alarmLow = s.alarmLow; gauge->warningLow = s.warningLow; gauge->warningHigh = s.warningHigh; gauge->alarmHigh = s.alarmHigh;
    QString tip = QString("Display range %1 to %2 %3").arg(s.displayLow, 0, 'g', 8).arg(s.displayHigh, 0, 'g', 8).arg(s.units).trimmed();
    if (s.lowLimitsSet()) tip += QString("\nLOLO %1 · LOW %2").arg(s.alarmLow, 0, 'g', 8).arg(s.warningLow, 0, 'g', 8);
    if (s.highLimitsSet()) tip += QString("\nHIGH %1 · HIHI %2").arg(s.warningHigh, 0, 'g', 8).arg(s.alarmHigh, 0, 'g', 8);
    if (s.controlHigh > s.controlLow) tip += QString("\nDrive limits %1 to %2").arg(s.controlLow, 0, 'g', 8).arg(s.controlHigh, 0, 'g', 8);
    gauge->setToolTip(tip); gauge->update();
  }
  trendPlot->warningLines.clear(); trendPlot->alarmLines.clear();
  if (numeric && s.lowLimitsSet()) { trendPlot->warningLines << s.warningLow; trendPlot->alarmLines << s.alarmLow; }
  if (numeric && s.highLimitsSet()) { trendPlot->warningLines << s.warningHigh; trendPlot->alarmLines << s.alarmHigh; }
  const bool exactAdjust = !((s.type == "long" || s.type == "ulong") && std::abs(s.number) > 9007199254740991.0);
  numericControls->setEnabled(put->isEnabled() && numeric && exactAdjust);
  numericControls->setToolTip(exactAdjust ? QString() : "Use the exact value editor for 64-bit integers outside the precise range of numeric controls.");
  available = InfoSection | HistorySection | RecordSection | (s.array ? ArraySection : TrendSection) | (s.structure ? StructureSection : 0) |
      (!channel_->readOnly() && writableSeen && numeric ? AdjustSection : 0);
  applySections();
  if (automaticLimits && s.displayHigh > s.displayLow) { low->setValue(s.displayLow); high->setValue(s.displayHigh); }
  if (numeric && std::isfinite(s.number) && !slider->isSliderDown() && !dial->isSliderDown() && high->value() > low->value()) {
    const double pos = std::clamp((s.number - low->value()) / (high->value() - low->value()), 0.0, 1.0);
    QSignalBlocker bs(slider), bd(dial); slider->setValue(int(pos * 10000)); dial->setValue(int(pos * 10000));
  }
  if (allSections || shown & InfoSection) {
    const auto fields = s.information(); int row = 0; info->setRowCount(fields.size());
    for (auto it = fields.cbegin(); it != fields.cend(); ++it, ++row) {
      cell(info, row, 0, it.key()); cell(info, row, 1, it.value());
      info->setRowHidden(row, !filter->text().isEmpty() && !(it.key() + ' ' + it.value()).contains(filter->text(), Qt::CaseInsensitive));
    }
  }
  if ((allSections || shown & StructureSection) && (structure || s.structure)) structureBrowser()->setTree(s.structure);
  if (allSections || shown & ArraySection) {
    const int count = s.array ? std::min(int(s.values.size()), 2048) : 0; array->setRowCount(count); waveform->values.clear();
    for (int i = 0; i < count; ++i) {
      cell(array, i, 0, QString::number(i)); cell(array, i, 1, s.values[i]); bool ok; double n = s.values[i].toDouble(&ok); waveform->values << (ok ? n : std::numeric_limits<double>::quiet_NaN());
    }
    arrayNotice->setText(s.array ? QString("%1 elements received · %2 retained · %3 shown%4").arg(s.count).arg(s.values.size()).arg(count).arg(s.truncated ? " · truncated" : "") : "Arrays appear here when available."); waveform->update();
  }
  const auto& samples = channel_->history.samples;
  if (allSections || shown & HistorySection) {
    const int count = int(std::min<size_t>(samples.size(), 1000));
    auto fillRow = [&](int row) {
      const auto& sample = samples[samples.size() - 1 - size_t(row)];
      cell(events, row, 0, sample.timestamp()); cell(events, row, 1, sample.value);
      const QStringList severity{"No alarm", "Minor", "Major", "Invalid"};
      cell(events, row, 2, !sample.connected ? "Disconnected" : sample.severity >= 0 && sample.severity < severity.size() ? severity[sample.severity] : QString::number(sample.severity));
    };
    int added = 0;
    while (added < count && samples[samples.size() - 1 - size_t(added)].serial > displayedHistorySerial) ++added;
    if (displayedHistorySerial && count && samples.front().serial <= displayedHistorySerial && added < count) {
      // Keep existing cells (and their selection) as new rows arrive. Rewriting
      // all 3000 cells on every event was the dominant cost of a full history.
      while (events->rowCount() + added > count) events->removeRow(events->rowCount() - 1);
      for (int i = 0; i < added; ++i) events->insertRow(0);
      for (int i = 0; i < added; ++i) fillRow(i);
    } else {
      events->setRowCount(count); for (int i = 0; i < count; ++i) fillRow(i);
    }
    displayedHistorySerial = count ? samples.back().serial : 0;
    historyCount->setText(samples.size() > 1000 ? QString("newest 1000 of %1 events").arg(samples.size()) : QString("%1 events").arg(samples.size()));
  }
  if (allSections || shown & TrendSection) {
    QVector<double> numbers; numbers.reserve(int(samples.size()));
    for (const auto& sample : samples) numbers << sample.number;
    trendPlot->values = numbers; trendPlot->levels = s.choices;
    trendStats->setText(s.choices.isEmpty() ? channel_->history.statistics() : channel_->history.stateStatistics(s.choices, QDateTime::currentMSecsSinceEpoch()));
    trendPlot->update();
  }
  activityCount->setText(QString("%1 retained events").arg(samples.size()));
}
void Inspector::saveSnapshot(const QString& path) { saveFile(path, QJsonDocument(channel_->snapshot.json()).toJson()); }
void Inspector::saveHistory(const QString& path) { saveFile(path, channel_->history.csv()); }
Window::Window(Options o) : options(o) {
  // Zoom scales the application font, so it applies before widgets are built.
  setZoom(QSettings().value("zoom", 100).toInt());
  confirmWrites = QSettings().value("confirmWrites", false).toBool();
  navigationTimer = new QTimer(this); navigationTimer->setObjectName("navigationRenderTimer"); navigationTimer->setInterval(150); navigationTimer->setSingleShot(true);
  connect(navigationTimer, &QTimer::timeout, this, [this] { if (navigationDirty && isVisible() && !isMinimized()) updateNavigation(); });
  fitTimer = new QTimer(this); fitTimer->setObjectName("windowFitTimer"); fitTimer->setInterval(0); fitTimer->setSingleShot(true);
  connect(fitTimer, &QTimer::timeout, this, &Window::fitToContents);
  sectionMask = QSettings().value("sections", 0).toInt() & AllSections;
  setWindowTitle("QtProbe" + QString(o.readOnly ? " — Read only" : "")); setWindowIcon(QIcon(":/qtprobe.svg"));
  setAcceptDrops(true); resize(640, 200);
  auto* central = new QWidget; central->setObjectName("workspace"); auto* layout = box(central, 8, 6); setCentralWidget(central);
  auto* entry = new QHBoxLayout; entry->setSpacing(6);
  name = new QComboBox; name->setEditable(true); name->setObjectName("pvName"); name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  name->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); name->setMinimumContentsLength(12); name->setAccessibleName("Process variable name");
  name->lineEdit()->setPlaceholderText("PV name, ca://PV or pva://PV"); name->lineEdit()->setClearButtonEnabled(true);
  name->addItems(QSettings().value("recentPvs").toStringList()); name->setCurrentText("");
  auto* searchIcon = name->lineEdit()->addAction(symbolIcon(Symbol::Search, colors(palette()).muted), QLineEdit::LeadingPosition); searchIcon->setProperty("symbol", int(Symbol::Search));
  provider = new QComboBox; provider->setObjectName("providerSelector"); provider->addItems({"ca", "pva"}); provider->setCurrentText(o.provider); provider->setAccessibleName("Default PV provider"); provider->setToolTip("Default provider. A ca:// or pva:// prefix always takes precedence.");
  auto* open = button("Add", "addPvButton", true); open->setProperty("symbol", int(Symbol::Plus)); open->setDefault(true); open->setToolTip("Open the PV (Enter)");
  exportButton = tool("", Symbol::Export, "exportButton"); exportButton->setPopupMode(QToolButton::InstantPopup); exportButton->setToolTip("Export snapshot, history or session"); exportButton->setAccessibleName("Export");
  entry->addWidget(name, 1); entry->addWidget(provider); entry->addWidget(open); entry->addWidget(exportButton);
  if (o.readOnly) { auto* ro = label("Read only", "badge", 0.84); ro->setObjectName("readOnlyBadge"); ro->setToolTip("Writes are disabled in this session"); setTone(ro, "warning"); entry->addWidget(ro); }
  layout->addLayout(entry);
  entryError = wrappingLabel({}, "muted"); entryError->setObjectName("entryError"); entryError->hide(); setTone(entryError, "negative"); layout->addWidget(entryError);
  connect(open, &QPushButton::clicked, this, [this] { addPv(name->currentText()); }); connect(name->lineEdit(), &QLineEdit::returnPressed, this, [this] { addPv(name->currentText()); });
  // Tab 0 is the All PVs table; tab n inspects stack page n - 1.
  pvTabs = new PvTabBar; pvTabs->setObjectName("pvTabs"); pvTabs->setDrawBase(false); pvTabs->setExpanding(false); pvTabs->setUsesScrollButtons(true);
  pvTabs->setElideMode(Qt::ElideMiddle); pvTabs->setContextMenuPolicy(Qt::CustomContextMenu); pvTabs->setAccessibleName("Open process variables");
  pvTabs->installEventFilter(this);
  tabStrip = new QWidget; tabStrip->setObjectName("pvTabStrip"); auto* stripLayout = new QHBoxLayout(tabStrip); stripLayout->setContentsMargins(0, 0, 0, 0); stripLayout->setSpacing(4);
  allTab = new QToolButton; allTab->setObjectName("allPvsTab"); allTab->setCheckable(true); allTab->setCursor(Qt::PointingHandCursor);
  allTab->setToolTip("All open PVs (Ctrl+1)"); allTab->setText("All");
  // A divider separates the pinned button from tabs scrolled partly out of view.
  auto* divider = new QFrame; divider->setObjectName("pvTabDivider"); divider->setFixedWidth(1);
  stripLayout->addWidget(allTab); stripLayout->addWidget(divider); stripLayout->addWidget(pvTabs, 1); tabStrip->hide(); layout->addWidget(tabStrip);
  connect(allTab, &QToolButton::clicked, this, [this] { setComparison(true); });
  pages = new FitStack; pages->setObjectName("workspacePages");
  auto* welcome = new QFrame; welcome->setObjectName("welcomePage"); auto* wl = new QHBoxLayout(welcome); wl->setContentsMargins(12, 8, 8, 8); wl->setSpacing(10);
  auto* welcomeText = wrappingLabel("Type a PV name above and press Enter, or middle-drag PVs here from QtEDM.", "muted"); welcomeText->setObjectName("welcomeHint"); wl->addWidget(welcomeText, 1);
  auto* openSession = button("Open session…", "welcomeOpenSession"); openSession->setProperty("role", "quiet"); wl->addWidget(openSession, 0, Qt::AlignVCenter);
  connect(openSession, &QPushButton::clicked, this, &Window::openSessionDialog);
  stack = new FitStack; stack->setObjectName("pvStack"); comparison = new Comparison;
  pages->addWidget(welcome); pages->addWidget(stack); pages->addWidget(comparison); layout->addWidget(pages, 1);
  connect(pvTabs, &QTabBar::currentChanged, this, [this](int index) { if (index >= 0) showPv(index); });
  // While the table is shown, the PV tab bar still has a current tab; clicking it inspects that PV.
  connect(pvTabs, &QTabBar::tabBarClicked, this, [this](int index) { if (index >= 0 && comparisonVisible) showPv(index); });
  comparison->selected = [this](Channel* channel) {
    for (int i = 0; i < stack->count(); ++i) if (static_cast<Inspector*>(stack->widget(i))->channel() == channel) stack->setCurrentIndex(i);
    navigationDirty = true; updateNavigation();
  };
  comparison->plot = [this](const QList<Channel*>& channels) { openInStripTool(channels); };
  connect(pvTabs, &QTabBar::customContextMenuRequested, this, [this](const QPoint& pos) {
    const int row = pvTabs->tabAt(pos); if (row < 0 || row >= stack->count()) return;
    auto* inspector = static_cast<Inspector*>(stack->widget(row));
    QMenu menu(this); auto* copy = menu.addAction("Copy PV address"); auto* strip = menu.addAction("Open in QtStripTool");
    auto* reconnect = menu.addAction("Reconnect"); menu.addSeparator(); auto* close = menu.addAction("Close PV");
    auto* chosen = menu.exec(pvTabs->mapToGlobal(pos));
    if (chosen == close) closePv(stack->indexOf(inspector));
    else if (chosen == strip) openInStripTool({inspector->channel()});
    else if (chosen == copy) { const auto& s = inspector->channel()->snapshot; QApplication::clipboard()->setText(s.provider + "://" + s.name); }
    else if (chosen == reconnect) inspector->channel()->start();
  });
  auto* file = menuBar()->addMenu("&File"); file->addAction("&Open PV", this, &Window::focusPvName, QKeySequence::Open);
  auto* focus = new QAction(this); focus->setShortcut(QKeySequence("Ctrl+L")); addAction(focus); connect(focus, &QAction::triggered, this, &Window::focusPvName);
  closeAction = file->addAction("Close PV", this, [this] { closePv(stack->currentIndex()); }, QKeySequence::Close); closeAction->setObjectName("closePvAction");
  file->addSeparator(); file->addAction("Open &session…", this, &Window::openSessionDialog);
  saveSessionAction = file->addAction("Save session…", this, [this] {
    const auto path = QFileDialog::getSaveFileName(this, "Save session", {}, "QtProbe session (*.qtp)");
    if (!path.isEmpty()) try { saveSession(path); } catch (const std::exception& e) { QMessageBox::warning(this, "Session", e.what()); }
  }); file->addSeparator();
  snapshotAction = file->addAction("Export snapshot (JSON)…", this, [this] {
    if (!current()) return;
    const auto path = QFileDialog::getSaveFileName(this, "Export snapshot", {}, "JSON (*.json)");
    if (!path.isEmpty()) try { current()->saveSnapshot(path); } catch (const std::exception& e) { QMessageBox::warning(this, "Export", e.what()); }
  }); snapshotAction->setObjectName("snapshotAction");
  historyAction = file->addAction("Export history (CSV)…", this, [this] {
    if (!current()) return;
    const auto path = QFileDialog::getSaveFileName(this, "Export history", {}, "CSV (*.csv)");
    if (!path.isEmpty()) try { current()->saveHistory(path); } catch (const std::exception& e) { QMessageBox::warning(this, "Export", e.what()); }
  }); historyAction->setObjectName("historyAction");
  auto* exportMenu = new QMenu(exportButton); exportMenu->addAction(snapshotAction); exportMenu->addAction(historyAction); exportMenu->addSeparator(); exportMenu->addAction(saveSessionAction); exportButton->setMenu(exportMenu);
  file->addSeparator(); file->addAction("&Quit", this, &QWidget::close, QKeySequence::Quit);
  auto* view = menuBar()->addMenu("&View"); auto* viewModes = new QActionGroup(this);
  allPvsAction = view->addAction("All PVs", this, [this] { setComparison(true); }, QKeySequence("Ctrl+1")); allPvsAction->setObjectName("allPvsAction"); allPvsAction->setCheckable(true); viewModes->addAction(allPvsAction);
  inspectorAction = view->addAction("Inspector", this, [this] { setComparison(false); }, QKeySequence("Ctrl+2")); inspectorAction->setObjectName("inspectorAction"); inspectorAction->setCheckable(true); viewModes->addAction(inspectorAction);
  nextPvAction = view->addAction("Next PV", this, [this] { cyclePv(1); }, QKeySequence("Ctrl+PgDown")); nextPvAction->setObjectName("nextPvAction");
  previousPvAction = view->addAction("Previous PV", this, [this] { cyclePv(-1); }, QKeySequence("Ctrl+PgUp")); previousPvAction->setObjectName("previousPvAction"); view->addSeparator();
  auto* zoomIn = view->addAction("Zoom in", this, [this] {
    const int steps[] = {70, 80, 90, 100, 110, 125, 150, 175, 200};
    for (int step : steps) if (step > zoom()) { setZoomLevel(step); return; }
  });
  zoomIn->setObjectName("zoomInAction"); zoomIn->setShortcuts({QKeySequence::ZoomIn, QKeySequence("Ctrl+=")});
  auto* zoomOut = view->addAction("Zoom out", this, [this] {
    const int steps[] = {200, 175, 150, 125, 110, 100, 90, 80, 70};
    for (int step : steps) if (step < zoom()) { setZoomLevel(step); return; }
  }, QKeySequence::ZoomOut); zoomOut->setObjectName("zoomOutAction");
  auto* actualSize = view->addAction("Actual size", this, [this] { setZoomLevel(100); }, QKeySequence("Ctrl+0")); actualSize->setObjectName("actualSizeAction");
  view->addSeparator();
  auto* appearanceMenu = view->addMenu("Appearance"); auto* appearanceGroup = new QActionGroup(this); appearanceGroup->setExclusive(true);
  const auto savedAppearance = QSettings().value("appearance", "system").toString();
  for (const auto& mode : {QString("system"), QString("light"), QString("dark")}) {
    auto* action = appearanceMenu->addAction(mode == "system" ? "Follow system" : mode == "light" ? "Light" : "Dark"); action->setCheckable(true); action->setData(mode); action->setObjectName("appearance_" + mode); appearanceGroup->addAction(action); action->setChecked(mode == savedAppearance);
    connect(action, &QAction::triggered, this, [this, mode] { setAppearance(mode); });
  }
  auto* tools = menuBar()->addMenu("&Tools");
  auto* strip = tools->addAction("Open in QtStripTool", this, [this] {
    QList<Channel*> channels;
    if (comparisonVisible) channels = comparison->selection();
    else if (current()) channels << current()->channel();
    if (channels.isEmpty()) { statusBar()->showMessage("Select PVs in the All table first", 6000); return; }
    openInStripTool(channels);
  }, QKeySequence("Ctrl+T")); strip->setObjectName("stripToolAction");
  tools->addSeparator();
  auto* confirmAction = tools->addAction("Confirm before writing"); confirmAction->setObjectName("confirmWritesAction"); confirmAction->setCheckable(true); confirmAction->setChecked(confirmWrites);
  confirmAction->setToolTip("Ask before each write, Undo or Process");
  connect(confirmAction, &QAction::toggled, this, [this](bool on) { confirmWrites = on; QSettings().setValue("confirmWrites", on); });
  auto* logAction = tools->addAction("Write log…", this, &Window::showWriteLog); logAction->setObjectName("writeLogAction");
  auto* help = menuBar()->addMenu("&Help"); populateHelpMenu(help, this);
  // A normal status widget: temporary messages cover it and then restore it.
  statusText = new QLabel; statusText->setObjectName("statusText"); statusBar()->addWidget(statusText, 1);
  connectionSummary = new QLabel; connectionSummary->setObjectName("connectionSummary"); statusBar()->addPermanentWidget(connectionSummary);
  installPvDropTarget(this, [this](const QStringList& names) { return addPvs(names); }, [this] { return provider->currentText(); });
  installMotifPvDropTarget(this, [this](const QStringList& names) { return addPvs(names); }, [this] { return provider->currentText(); });
  restoreGeometry(QSettings().value("windowGeometry").toByteArray()); setAppearance(savedAppearance); updateNavigation(); name->setFocus();
}
Inspector* Window::current() const { return dynamic_cast<Inspector*>(stack->currentWidget()); }
void Window::focusPvName() { name->setFocus(); name->lineEdit()->selectAll(); }
void Window::openSessionDialog() {
  const auto path = QFileDialog::getOpenFileName(this, "Open session", {}, "QtProbe session (*.qtp)");
  if (!path.isEmpty()) try { loadSession(path); } catch (const std::exception& e) { QMessageBox::warning(this, "Session", e.what()); }
}
void Window::setComparison(bool visible) { comparisonVisible = visible && stack->count() > 1; navigationDirty = true; updateNavigation(); }
void Window::showPv(int index) { if (index < 0 || index >= stack->count()) return; stack->setCurrentIndex(index); setComparison(false); }
void Window::cyclePv(int step) {
  // Positions: 0 is All PVs, then each open PV in tab order.
  const int count = stack->count(); if (count < 2) return;
  const int position = comparisonVisible ? 0 : stack->currentIndex() + 1, next = (position + step + count + 1) % (count + 1);
  if (next == 0) setComparison(true); else showPv(next - 1);
}
void Window::setSections(int mask) {
  sectionMask = mask & AllSections; QSettings().setValue("sections", sectionMask);
  for (int i = 0; i < stack->count(); ++i) static_cast<Inspector*>(stack->widget(i))->setSections(sectionMask);
  fitTimer->start();
}
void Window::applyIcons(QWidget* root) {
  const auto c = colors(palette());
  auto apply = [&](QAbstractButton* item) {
    if (item->property("symbol").isValid())
      item->setIcon(symbolIcon(Symbol(item->property("symbol").toInt()), item->property("role").toString() == "primary" ? c.accentText : c.muted));
  };
  if (auto* item = qobject_cast<QAbstractButton*>(root)) apply(item);
  for (auto* item : root->findChildren<QAbstractButton*>()) apply(item);
}
void Window::setAppearance(const QString& mode) {
  const bool updates = updatesEnabled(); setUpdatesEnabled(false);
  centralWidget()->setStyleSheet({});
  appearance = mode == "light" || mode == "dark" ? mode : "system"; applyAppearance(this, appearance);
  for (auto* widget : findChildren<QWidget*>()) widget->setPalette(palette());
  const auto c = colors(palette()); centralWidget()->setStyleSheet(workspaceStyle(c)); statusBar()->setStyleSheet(workspaceStyle(c));
  applyIcons(this);
  for (auto* action : findChildren<QAction*>()) {
    if (action->property("symbol").isValid()) action->setIcon(symbolIcon(Symbol(action->property("symbol").toInt()), c.muted));
    if (action->objectName().startsWith("appearance_")) action->setChecked(action->data().toString() == appearance);
  }
  QSettings().setValue("appearance", appearance);
  for (int i = 0; i < pvTabs->count(); ++i) pvTabs->setTabData(i, QString()); // repaint status dots
  comparison->restyle();
  for (auto* widget : findChildren<QWidget*>()) if (auto* plot = dynamic_cast<Plot*>(widget)) plot->update();
  updateHelpAppearance(this);
  setUpdatesEnabled(updates); navigationDirty = true; updateNavigation();
}
void Window::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  if (event->type() == QEvent::ApplicationPaletteChange && centralWidget() && appearance == "system") setAppearance("system");
  if (event->type() == QEvent::WindowStateChange) {
    if (isMinimized()) navigationTimer->stop();
    else { updateNavigation(); if (current() && pages->currentIndex() == 1) current()->refresh(false); }
  }
}
void Window::showEvent(QShowEvent* event) { QMainWindow::showEvent(event); updateNavigation(); fitTimer->start(); }
void Window::hideEvent(QHideEvent* event) { navigationTimer->stop(); fitTimer->stop(); QMainWindow::hideEvent(event); }
void Window::invalidateNavigation() { navigationDirty = true; if (isVisible() && !isMinimized() && !navigationTimer->isActive()) navigationTimer->start(); }
void Window::updateNavigation() {
  navigationDirty = false; navigationTimer->stop(); const int count = stack->count(); int connected = 0;
  const auto c = colors(palette());
  for (int i = 0; i < count && i < pvTabs->count(); ++i) {
    const auto& s = static_cast<Inspector*>(stack->widget(i))->channel()->snapshot; const int tab = i;
    const auto state = !s.connected ? (s.error == "Stopped" ? "Stopped" : "Disconnected") : !s.readable ? "No read access" : s.hasValue ? s.valueText() + (s.units.isEmpty() ? "" : " " + s.units) : "Waiting for data";
    const auto tone = alarmTone(s), tip = s.provider + "://" + s.name + "\n" + state + (s.error.isEmpty() ? "" : "\n" + s.error);
    if (pvTabs->tabText(tab) != s.name) pvTabs->setTabText(tab, s.name);
    if (pvTabs->tabToolTip(tab) != tip) pvTabs->setTabToolTip(tab, tip);
    if (pvTabs->tabData(tab).toString() != tone) {
      pvTabs->setTabData(tab, tone);
      pvTabs->setTabIcon(tab, dotIcon(tone == "positive" ? c.positive : tone == "warning" ? c.warning : tone == "negative" ? c.negative : c.muted));
    }
    if (s.connected) ++connected;
  }
  const bool multiple = count > 1; if (!multiple) comparisonVisible = false;
  const int page = count == 0 ? 0 : comparisonVisible ? 2 : 1;
  allTab->setText(QString("All (%1)").arg(count)); allTab->setChecked(comparisonVisible);
  { QSignalBlocker block(pvTabs); pvTabs->setCurrentIndex(stack->currentIndex()); }
  if (pvTabs->property("inactive").toBool() != comparisonVisible) {
    pvTabs->setProperty("inactive", comparisonVisible); pvTabs->style()->unpolish(pvTabs); pvTabs->style()->polish(pvTabs); pvTabs->update();
  }
  tabStrip->setVisible(multiple); pages->setCurrentIndex(page);
  connectionSummary->setText(count ? QString("%1 of %2 connected").arg(connected).arg(count) : QString());
  allPvsAction->setEnabled(multiple); inspectorAction->setEnabled(count > 0); nextPvAction->setEnabled(multiple); previousPvAction->setEnabled(multiple);
  allPvsAction->setChecked(comparisonVisible); inspectorAction->setChecked(!comparisonVisible);
  comparison->setCurrent(current() ? current()->channel() : nullptr); comparison->refresh();
  const bool havePv = current(); exportButton->setEnabled(havePv); snapshotAction->setEnabled(havePv); historyAction->setEnabled(havePv); saveSessionAction->setEnabled(count > 0); closeAction->setEnabled(havePv);
  statusText->setText(count ? QString("%1 open PV%2 · F1 for help").arg(count).arg(count == 1 ? "" : "s") : "Ready · F1 for help");
  // Refit when the shown structure changes, not on every value update.
  const auto key = QString("%1:%2:%3").arg(page).arg(multiple).arg(page == 2 ? count : page == 1 ? stack->currentIndex() : 0);
  if (key != fitKey) { fitKey = key; if (isVisible()) fitTimer->start(); }
}
void Window::fitToContents() {
  if (!isVisible() || isMinimized() || isMaximized() || isFullScreen()) return;
  centralWidget()->layout()->activate(); layout()->activate();
  const int pageWidth = pages->width(); int content;
  if (pages->currentWidget() == stack && current()) content = current()->preferredHeight(pageWidth);
  else if (pages->currentWidget() == comparison) content = comparison->preferredHeight();
  else { auto* page = pages->currentWidget(); content = page->hasHeightForWidth() ? page->heightForWidth(pageWidth) : page->sizeHint().height(); }
  const int limit = screen() ? screen()->availableGeometry().height() * 85 / 100 : 900;
  const int wanted = std::min(limit, height() - pages->height() + content);
  // A height the user chose is a floor until the workspace is emptied;
  // content may still grow the window beyond it.
  if (fittedHeight && std::abs(height() - fittedHeight) > 2) userHeight = height();
  if (pages->currentIndex() == 0) userHeight = 0;
  const int target = std::max({minimumSizeHint().height(), wanted, userHeight});
  if (target != height()) resize(width(), target);
  fittedHeight = target;
}
void Window::addPv(const QString& text) {
  // Pasted lists add every PV; a single name keeps its own error message.
  const auto names = text.trimmed().contains(QRegularExpression("\\s")) ? droppedPvNames(text, provider->currentText()) : QStringList{};
  addPvs(names.isEmpty() ? QStringList{text} : names);
}
void Window::connectInspector(Inspector* inspector) {
  inspector->setSections(sectionMask);
  inspector->sectionsRequested = [this](int mask) { setSections(mask); };
  inspector->layoutChanged = [this, inspector] { if (inspector == current() && isVisible()) fitTimer->start(); };
  inspector->logged = [this] { refreshWriteLog(); };
  inspector->openLink = [this](const QString& uri) { followLink(uri); };
  inspector->confirm = [this](const QString& question) {
    if (!confirmWrites) return true;
    QMessageBox box(QMessageBox::Question, "Confirm write", question, QMessageBox::Yes | QMessageBox::No, this);
    box.setObjectName("confirmWriteDialog"); box.setPalette(palette()); box.setDefaultButton(QMessageBox::No);
    return box.exec() == QMessageBox::Yes;
  };
}
void Window::followLink(const QString& uri) {
  if (!addPvs({uri})) return;
  QString address; try { address = Address::parse(uri).uri(); } catch (const std::exception&) { return; }
  for (int i = 0; i < stack->count(); ++i) {
    const auto& s = static_cast<Inspector*>(stack->widget(i))->channel()->snapshot;
    if (s.provider + "://" + s.name == address) { showPv(i); return; }
  }
}
void Window::setZoomLevel(int percent) {
  setZoom(percent); QSettings().setValue("zoom", zoom());
  // Style-sheet polishing fixes widget fonts, so re-polish for the new size.
  setAppearance(appearance); rescaleFonts(this); comparison->rescale();
  for (auto* widget : findChildren<QWidget*>()) widget->updateGeometry();
  statusBar()->showMessage(QString("Zoom %1%").arg(zoom()), 2000);
  if (isVisible()) fitTimer->start();
}
bool Window::eventFilter(QObject* object, QEvent* event) {
  // Middle-drag a PV tab into QtEDM, QtStripTool or another QtProbe.
  if (object == pvTabs) {
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::MiddleButton) { dragTab = pvTabs->tabAt(mouse->pos()); dragStart = mouse->pos(); }
    else if (event->type() == QEvent::MouseMove && (mouse->buttons() & Qt::MiddleButton) && dragTab >= 0 &&
             (mouse->pos() - dragStart).manhattanLength() >= QApplication::startDragDistance()) {
      const auto& s = static_cast<Inspector*>(stack->widget(dragTab))->channel()->snapshot; dragTab = -1;
      startNameDrag(pvTabs, portableName(s.provider, s.name)); return true;
    }
  }
  return QMainWindow::eventFilter(object, event);
}
void Window::openInStripTool(const QList<Channel*>& channels) {
  QList<Snapshot> snapshots; int skipped = 0;
  for (auto* channel : channels) {
    if (channel->snapshot.provider == "ca" && snapshots.size() < 10 && channel->snapshot.name.toUtf8().size() <= 63) snapshots << channel->snapshot;
    else ++skipped;
  }
  if (snapshots.isEmpty()) { statusBar()->showMessage("QtStripTool plots up to ten Channel Access PVs", 8000); return; }
  // QTPROBE_STRIPTOOL, then beside qtprobe, then PATH, then a sibling build tree.
  const auto suffix = QSysInfo::productType() == "windows" ? QString(".exe") : QString();
  const QDir here(QCoreApplication::applicationDirPath());
  QStringList candidates{qEnvironmentVariable("QTPROBE_STRIPTOOL"), here.filePath("qtstriptool" + suffix),
      QStandardPaths::findExecutable("qtstriptool"), here.filePath("../../../StripTool/bin/" + here.dirName() + "/qtstriptool" + suffix)};
  QString program; for (const auto& candidate : candidates) if (!candidate.isEmpty() && QFileInfo(candidate).isExecutable()) { program = candidate; break; }
  if (program.isEmpty()) { statusBar()->showMessage("QtStripTool not found. Set QTPROBE_STRIPTOOL to its path.", 10000); return; }
  const QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/striptool"; QDir().mkpath(folder);
  const QString path = folder + "/qtprobe-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".stp";
  try { saveFile(path, stripToolConfig(snapshots).toUtf8()); }
  catch (const std::exception& e) { statusBar()->showMessage(QString::fromLocal8Bit(e.what()), 10000); return; }
  // Keep only the newest configurations; each launch writes a new one.
  const auto old = QDir(folder).entryInfoList({"qtprobe-*.stp"}, QDir::Files, QDir::Time);
  for (int i = 20; i < old.size(); ++i) QFile::remove(old[i].filePath());
  if (!QProcess::startDetached(program, {path}, folder)) { statusBar()->showMessage("Could not start " + program, 10000); return; }
  statusBar()->showMessage(QString("Opened %1 PV%2 in QtStripTool%3").arg(snapshots.size()).arg(snapshots.size() == 1 ? "" : "s")
      .arg(skipped ? QString(" · skipped %1 (PVA, over ten, or long names)").arg(skipped) : QString()), 8000);
}
void Window::showWriteLog() {
  auto* dialog = findChild<QDialog*>("writeLogDialog", Qt::FindDirectChildrenOnly);
  if (!dialog) {
    dialog = new QDialog(this); dialog->setObjectName("writeLogDialog"); dialog->setWindowTitle("Write log"); dialog->resize(760, 360);
    auto* layout = new QVBoxLayout(dialog);
    auto* note = label("Writes and Process requests from this QtProbe session, newest first.", "muted", 0.9); layout->addWidget(note);
    auto* table = new DataTable({"Time", "PV", "Before", "Written", "Result"}, 10); table->setObjectName("writeLogTable");
    table->setColumnWidth(0, table->fontMetrics().horizontalAdvance("0000-00-00 00:00:00") + 28); table->setColumnWidth(1, 170); table->setColumnWidth(2, 90); table->setColumnWidth(3, 90); layout->addWidget(table, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* exportLog = buttons->addButton("Export CSV…", QDialogButtonBox::ActionRole); exportLog->setObjectName("exportWriteLog");
    auto* clearLog = buttons->addButton("Clear", QDialogButtonBox::ResetRole); clearLog->setObjectName("clearWriteLog");
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(exportLog, &QPushButton::clicked, dialog, [this, dialog] {
      const auto path = QFileDialog::getSaveFileName(dialog, "Export write log", {}, "CSV (*.csv)");
      if (!path.isEmpty()) try { saveFile(path, writeLogCsv()); } catch (const std::exception& e) { QMessageBox::warning(dialog, "Export", e.what()); }
    });
    connect(clearLog, &QPushButton::clicked, dialog, [this] { writeLog().clear(); refreshWriteLog(); });
    layout->addWidget(buttons);
  }
  dialog->setPalette(palette()); dialog->setStyleSheet(workspaceStyle(colors(palette())));
  refreshWriteLog(); dialog->show(); dialog->raise(); dialog->activateWindow();
}
void Window::refreshWriteLog() {
  auto* dialog = findChild<QDialog*>("writeLogDialog", Qt::FindDirectChildrenOnly); if (!dialog) return;
  auto* table = dialog->findChild<QTableWidget*>("writeLogTable"); const auto& log = writeLog();
  table->setRowCount(int(log.size())); int row = 0;
  for (auto it = log.rbegin(); it != log.rend(); ++it, ++row) {
    cell(table, row, 0, it->time.toString("yyyy-MM-dd HH:mm:ss")); cell(table, row, 1, it->pv);
    cell(table, row, 2, it->before); cell(table, row, 3, it->after); cell(table, row, 4, (it->ok ? "✓ " : "✗ ") + it->result);
  }
}
bool Window::addPvs(const QStringList& texts) {
  try {
    QStringList addresses, existing;
    for (int i = 0; i < stack->count(); ++i) { const auto& s = static_cast<Inspector*>(stack->widget(i))->channel()->snapshot; existing << s.provider + "://" + s.name; }
    for (const auto& text : texts) { const auto uri = Address::parse(text, provider->currentText()).uri(); if (!addresses.contains(uri)) addresses << uri; }
    if (addresses.isEmpty()) throw std::runtime_error("Drop or enter at least one PV name");
    int additions = 0; for (const auto& uri : addresses) if (!existing.contains(uri)) ++additions;
    if (stack->count() + additions > 100) throw std::runtime_error("A workspace can contain up to 100 PVs; close some PVs before adding this group");
    if (!entryError->isHidden()) { entryError->hide(); if (isVisible()) fitTimer->start(); }
    name->lineEdit()->setAccessibleDescription({});
    auto recent = QSettings().value("recentPvs").toStringList();
    { QSignalBlocker stackBlock(stack), tabBlock(pvTabs);
      for (const auto& uri : addresses) {
        int index = existing.indexOf(uri);
        if (index < 0) {
          const auto address = Address::parse(uri); auto* inspector = new Inspector(address, options, stack);
          connectInspector(inspector);
          connect(inspector->channel(), &Channel::changed, this, [this] { invalidateNavigation(); });
          index = stack->addWidget(inspector); existing << uri;
          const int tab = pvTabs->addTab(address.name);
          auto* close = new QToolButton; close->setObjectName("closeTabButton"); close->setProperty("symbol", int(Symbol::Close));
          close->setToolTip("Close PV"); close->setAccessibleName("Close " + uri); close->setFixedSize(16, 16); close->setIconSize(QSize(12, 12)); close->setCursor(Qt::PointingHandCursor);
          connect(close, &QToolButton::clicked, this, [this, inspector] { closePv(stack->indexOf(inspector)); });
          pvTabs->setTabButton(tab, QTabBar::RightSide, close);
          comparison->add(inspector->channel(), [this, inspector] { stack->setCurrentWidget(inspector); setComparison(false); },
                          [this, inspector] { closePv(stack->indexOf(inspector)); });
          applyIcons(inspector); applyIcons(close);
        }
        stack->setCurrentIndex(index); recent.removeAll(uri); recent.prepend(uri);
      }
    }
    applyIcons(comparison);
    while (recent.size() > 30) recent.removeLast();
    QSettings().setValue("recentPvs", recent);
    name->clear(); name->addItems(recent); name->setCurrentText("");
    comparisonVisible = stack->count() > 1 && (additions > 0 || addresses.size() > 1);
    comparison->clearFilter(); navigationDirty = true; updateNavigation(); return true;
  } catch (const std::exception& e) {
    const auto message = QString::fromLocal8Bit(e.what()); entryError->setText(message); entryError->show(); name->lineEdit()->setAccessibleDescription(message); name->setFocus();
    if (isVisible()) fitTimer->start();
    return false;
  }
}
void Window::closePv(int index) {
  if (index < 0 || index >= stack->count()) return;
  auto* widget = stack->widget(index);
  comparison->remove(static_cast<Inspector*>(widget)->channel());
  { QSignalBlocker stackBlock(stack), tabBlock(pvTabs); stack->removeWidget(widget); pvTabs->removeTab(index); }
  delete widget; navigationDirty = true; updateNavigation();
}
void Window::saveSession(const QString& path) {
  QJsonArray pvs;
  for (int i = 0; i < stack->count(); ++i) { const auto& s = static_cast<Inspector*>(stack->widget(i))->channel()->snapshot; pvs.append(s.provider + "://" + s.name); }
  saveFile(path, QJsonDocument(QJsonObject{{"version", 1}, {"pvs", pvs}}).toJson());
}
void Window::loadSession(const QString& path) {
  QFile file(path); if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open session");
  QJsonParseError error; const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || doc.object().value("version").toInt() != 1 || !doc.object().value("pvs").isArray()) throw std::runtime_error("Invalid QtProbe session");
  QStringList pvs; for (const auto& pv : doc.object().value("pvs").toArray()) pvs << Address::parse(pv.toString(), options.provider).uri();
  if (pvs.size() > 100) throw std::runtime_error("Session exceeds 100 PVs");
  if (!pvs.isEmpty() && !addPvs(pvs)) throw std::runtime_error(entryError->text().toStdString());
}
void Window::closeEvent(QCloseEvent* e) { QSettings().setValue("windowGeometry", saveGeometry()); QMainWindow::closeEvent(e); }
} // namespace probe
