#include "comparison.h"
#include "inspector.h"
#include "appearance.h"
#include <QtWidgets>
#include <algorithm>
#include <limits>
namespace probe {
namespace {
// Header and rows share these columns so they align: status dot, name,
// value, units, provider, state, age, trend and close. Zero width stretches.
constexpr int columnWidth[] = {10, 0, 0, 52, 30, 96, 50, 76, 18};
constexpr int columnStretch[] = {0, 3, 2, 0, 0, 0, 0, 0, 0};
constexpr int Provider = 4, Trend = 7;
void applyColumns(const QList<QWidget*>& cells) {
  for (int i = 0; i < cells.size(); ++i)
    if (columnWidth[i]) cells[i]->setFixedWidth(qRound(columnWidth[i] * zoom() / 100.0));
}
void addColumns(QHBoxLayout* layout, const QList<QWidget*>& cells) {
  for (int i = 0; i < cells.size(); ++i) {
    if (!columnWidth[i]) cells[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(cells[i], columnStretch[i]);
  }
  applyColumns(cells);
}
QLabel* small(QLabel* label, const char* name, const char* role = nullptr, qreal scale = 1) {
  label->setObjectName(name); label->setTextFormat(Qt::PlainText);
  if (role) label->setProperty("role", role);
  scaleFont(label, scale); return label;
}
QString stateText(const Snapshot& s) {
  return !s.connected ? (s.error == "Stopped" ? "Stopped" : "Disconnected") : !s.readable ? "No read access" : !s.hasValue ? "Waiting for data" : s.severity ? s.severityText() + " alarm" : "No alarm";
}
QString ageText(qint64 updated, qint64 now) {
  if (!updated) return "—";
  const qint64 seconds = std::max<qint64>(0, (now - updated) / 1000);
  if (seconds < 1) return "<1 s";
  if (seconds < 60) return QString("%1 s").arg(seconds);
  if (seconds < 3600) return QString("%1 min").arg(seconds / 60);
  if (seconds < 86400) return QString("%1 h").arg(seconds / 3600);
  return QString("%1 d").arg(seconds / 86400);
}
// Case-insensitive, with digit runs compared as numbers: Row:2 before Row:10.
int naturalCompare(const QString& a, const QString& b) {
  int i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (a[i].isDigit() && b[j].isDigit()) {
      int x = i, y = j;
      while (x < a.size() && a[x] == '0') ++x;
      while (y < b.size() && b[y] == '0') ++y;
      int xe = x, ye = y;
      while (xe < a.size() && a[xe].isDigit()) ++xe;
      while (ye < b.size() && b[ye].isDigit()) ++ye;
      if (xe - x != ye - y) return (xe - x) < (ye - y) ? -1 : 1;
      if (const int c = QStringView(a).mid(x, xe - x).compare(QStringView(b).mid(y, ye - y))) return c;
      i = xe; j = ye;
    } else {
      const auto ca = a[i].toCaseFolded(), cb = b[j].toCaseFolded();
      if (ca != cb) return ca < cb ? -1 : 1;
      ++i; ++j;
    }
  }
  return (a.size() - i) - (b.size() - j);
}
// Most severe first: disconnected, no access, waiting, INVALID, MAJOR, MINOR, OK.
int stateRank(const Snapshot& s) {
  return !s.connected ? 0 : !s.readable ? 1 : !s.hasValue ? 2 : s.severity >= 3 ? 3 : s.severity == 2 ? 4 : s.severity == 1 ? 5 : 6;
}
} // namespace
class PvRow final : public QFrame {
public:
  Comparison* owner;
  Channel* channel;
  std::function<void()> inspect, close;
  QLabel *dot, *units, *provider, *state, *age;
  ElidedLabel *name, *value;
  Plot* trend;
  QList<QWidget*> cells;
  QString dotKey;
  QPoint pressed;
  Qt::MouseButton pressedButton = Qt::NoButton;
  bool matches = true, dirty = true, selected = false;
  PvRow(Comparison* owner, Channel* channel, std::function<void()> inspect, std::function<void()> close, QWidget* parent)
      : QFrame(parent), owner(owner), channel(channel), inspect(std::move(inspect)), close(std::move(close)) {
    setObjectName("pvRow"); setCursor(Qt::PointingHandCursor); setFixedHeight(fontMetrics().height() + 12);
    setToolTip("Double-click or press Enter to inspect. Drag to copy PV names into another tool.");
    auto* layout = new QHBoxLayout(this); layout->setContentsMargins(8, 0, 6, 0); layout->setSpacing(8);
    dot = small(new QLabel, "rowDot"); dot->setAlignment(Qt::AlignCenter);
    name = static_cast<ElidedLabel*>(small(new ElidedLabel(Qt::ElideMiddle), "rowName"));
    value = static_cast<ElidedLabel*>(small(new ElidedLabel, "rowValue")); scaleFont(value, 1, false, true);
    units = small(new ElidedLabel, "rowUnits", "muted");
    provider = small(new QLabel, "rowProvider", "muted", 0.84);
    state = small(new ElidedLabel, "rowState", nullptr, 0.9);
    age = small(new QLabel("—"), "rowAge", "muted", 0.84); age->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    age->setToolTip("Time since QtProbe received the last value");
    trend = new Plot; trend->setObjectName("rowTrend"); trend->sparkline = true; trend->setMinimumHeight(0); trend->setFixedHeight(fontMetrics().height() + 2);
    auto* remove = new QToolButton; remove->setObjectName("closeRowButton"); remove->setProperty("symbol", int(Symbol::Close));
    remove->setToolTip("Close this PV"); remove->setAccessibleName("Close this PV"); remove->setFixedSize(18, 18); remove->setIconSize(QSize(12, 12));
    // Removing a row destroys this button; finish its clicked signal first.
    connect(remove, &QToolButton::clicked, parent, [parent, close = this->close] { QTimer::singleShot(0, parent, close); });
    cells = {dot, name, value, units, provider, state, age, trend, remove};
    addColumns(layout, cells);
    connect(channel, &Channel::changed, this, [this] { dirty = true; });
  }
  const Snapshot& snapshot() const { return channel->snapshot; }
  void rescale() { setFixedHeight(fontMetrics().height() + 12); trend->setFixedHeight(fontMetrics().height() + 2); applyColumns(cells); }
  void setColumns(bool showProvider, bool showTrend) { provider->setVisible(showProvider); trend->setVisible(showTrend); }
  void updateAge(qint64 now) { const auto text = ageText(channel->lastUpdateMs, now); if (age->text() != text) age->setText(text); }
  void refresh() {
    dirty = false;
    const auto& s = channel->snapshot;
    const auto uri = s.provider + "://" + s.name;
    setProperty("pvAddress", uri);
    name->setText(s.name); name->setToolTip(s.description.isEmpty() ? uri : uri + "\n" + s.description);
    provider->setText(s.provider.toUpper());
    const bool live = s.connected && s.readable && s.hasValue;
    const QString tone = !live ? "muted" : s.severity >= 2 ? "negative" : s.severity == 1 ? "warning" : "positive";
    state->setText(stateText(s)); state->setToolTip(s.error.isEmpty() ? s.alarmMessage : s.error); setTone(state, tone);
    const auto c = colors(window()->palette()); const QString key = tone + (c.dark ? ":dark" : ":light");
    if (key != dotKey) {
      dotKey = key;
      dot->setPixmap(dotIcon(tone == "positive" ? c.positive : tone == "warning" ? c.warning : tone == "negative" ? c.negative : c.muted).pixmap(12, 12));
    }
    const auto text = s.valueText(), exact = s.exactText();
    value->setText(text.size() > 120 ? text.left(120) + "…" : text);
    auto tip = exact.size() > 2000 ? exact.left(2000) + "…" : exact;
    if (s.array && !s.longString) tip += QString("\n%1 elements%2").arg(s.count).arg(s.truncated ? " (truncated)" : "");
    if (!live && s.hasValue) tip += "\nLast observation";
    if (!s.timestamp().isEmpty()) tip += "\nIOC time " + s.timestamp();
    value->setToolTip(tip); setTone(value, !live ? "muted" : s.severity >= 2 ? "negative" : s.severity == 1 ? "warning" : "normal");
    units->setText(s.units); units->setToolTip(s.units);
    updateAge(QDateTime::currentMSecsSinceEpoch());
    trend->values.clear(); trend->levels = s.choices; const auto& history = channel->history.samples;
    const auto first = history.size() > 120 ? history.size() - 120 : 0;
    for (size_t i = first; i < history.size(); ++i) trend->values.append(history[i].connected ? history[i].number : std::numeric_limits<double>::quiet_NaN());
    trend->update();
  }
protected:
  void mousePressEvent(QMouseEvent* event) override {
    pressed = event->pos(); pressedButton = event->button();
    // A press on an unselected row selects it so a drag carries that row.
    if (event->button() == Qt::LeftButton && !selected && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) owner->rowClicked(this, event->modifiers());
    event->accept();
  }
  void mouseMoveEvent(QMouseEvent* event) override {
    if (pressedButton == Qt::NoButton || (event->pos() - pressed).manhattanLength() < QApplication::startDragDistance()) return;
    const bool middle = pressedButton == Qt::MiddleButton; pressedButton = Qt::NoButton;
    owner->startDrag(this, middle);
  }
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (pressedButton == Qt::LeftButton && event->button() == Qt::LeftButton && rect().contains(event->pos())) owner->rowClicked(this, event->modifiers());
    pressedButton = Qt::NoButton; QFrame::mouseReleaseEvent(event);
  }
  void mouseDoubleClickEvent(QMouseEvent* event) override { if (event->button() == Qt::LeftButton) inspect(); }
  void contextMenuEvent(QContextMenuEvent* event) override { owner->rowMenu(this, event->globalPos()); }
};
Comparison::Comparison(QWidget* parent) : QWidget(parent) {
  setObjectName("comparison"); setFocusPolicy(Qt::StrongFocus); auto* outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0);
  frame = new QFrame; frame->setObjectName("comparisonCard"); frame->setProperty("card", true); outer->addWidget(frame);
  auto* layout = new QVBoxLayout(frame); layout->setContentsMargins(1, 6, 1, 1); layout->setSpacing(4);
  tools = new QHBoxLayout; tools->setContentsMargins(8, 0, 8, 0); tools->setSpacing(6);
  filter = new QLineEdit; filter->setObjectName("pvFilter"); filter->setPlaceholderText("Filter PVs…"); filter->setClearButtonEnabled(true);
  filter->setAccessibleName("Filter open PVs"); filter->setMaximumWidth(240); filter->installEventFilter(this); tools->addWidget(filter, 1);
  auto toggle = [&](const char* name, const QString& tip) {
    auto* button = new QToolButton; button->setObjectName(name); button->setCheckable(true); button->setProperty("role", "toggle");
    button->setToolTip(tip); button->setCursor(Qt::PointingHandCursor); tools->addWidget(button);
    connect(button, &QToolButton::toggled, this, [this] { refresh(); }); return button;
  };
  alarmsOnly = toggle("alarmsFilter", "Show PVs in MINOR, MAJOR or INVALID alarm");
  disconnectedOnly = toggle("disconnectedFilter", "Show disconnected PVs");
  tools->addStretch(1); layout->addLayout(tools);
  header = new QFrame; header->setObjectName("pvHeader"); headerLayout = new QHBoxLayout(header); headerLayout->setContentsMargins(8, 2, 6, 3); headerLayout->setSpacing(8);
  const char* titles[] = {"", "PV", "Value", "Units", "", "State", "Age", "Trend", ""};
  for (int i = 0; i < 9; ++i) {
    auto* title = small(new QLabel(titles[i]), "", "muted", 0.8);
    if (i == Name || i == Value || i == State || i == Age) {
      title->setObjectName(QString("sort%1").arg(titles[i])); title->setProperty("sortable", true); title->setProperty("column", i);
      title->setCursor(Qt::PointingHandCursor); title->setToolTip("Sort by this column"); title->installEventFilter(this);
    }
    if (i == Age) title->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headerCells << title;
  }
  addColumns(headerLayout, headerCells); layout->addWidget(header);
  empty = small(new QLabel("No open PVs match the filter."), "comparisonEmpty", "muted"); empty->setAlignment(Qt::AlignCenter); empty->hide(); layout->addWidget(empty);
  scroll = new QScrollArea; scroll->setObjectName("comparisonScroll"); scroll->setWidgetResizable(true); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setFocusPolicy(Qt::NoFocus);
  body = new QWidget; body->setObjectName("comparisonBody"); list = new QVBoxLayout(body); list->setContentsMargins(0, 0, 0, 0); list->setSpacing(0); list->addStretch(1);
  scroll->setWidget(body); layout->addWidget(scroll, 1);
  connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { refresh(); });
  // Keep the header aligned with the rows when the scroll bar appears.
  connect(scroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int maximum) {
    headerLayout->setContentsMargins(8, 2, 6 + (maximum > 0 ? scroll->verticalScrollBar()->sizeHint().width() : 0), 3);
  });
  connect(filter, &QLineEdit::textChanged, this, [this] { refresh(); });
  // Ages advance without monitor updates, so they tick only while shown.
  ageTimer = new QTimer(this); ageTimer->setObjectName("comparisonAgeTimer"); ageTimer->setInterval(1000);
  connect(ageTimer, &QTimer::timeout, this, [this] { updateAges(); if (sortKey == Age) sortRows(false); });
  updateHeader(); refresh();
}
void Comparison::add(Channel* channel, std::function<void()> inspect, std::function<void()> close) {
  auto* row = new PvRow(this, channel, std::move(inspect), std::move(close), body); rows.append(row); order.append(row);
  list->insertWidget(list->count() - 1, row); row->setColumns(showProvider, showTrend); row->refresh();
  sortRows(true); refresh();
}
void Comparison::remove(Channel* channel) {
  for (int i = 0; i < rows.size(); ++i) {
    if (rows[i]->channel != channel) continue;
    auto* row = rows.takeAt(i); order.removeAll(row);
    if (cursor == row) cursor = nullptr;
    if (anchor == row) anchor = nullptr;
    delete row; refresh(); return;
  }
}
void Comparison::setCurrent(Channel* channel) {
  for (auto* row : rows) {
    const bool current = row->channel == channel;
    if (row->property("current").toBool() == current) continue;
    row->setProperty("current", current); row->style()->unpolish(row); row->style()->polish(row); row->update();
  }
}
void Comparison::clearFilter() { filter->clear(); alarmsOnly->setChecked(false); disconnectedOnly->setChecked(false); }
void Comparison::restyle() { for (auto* row : rows) row->dirty = true; refresh(); }
void Comparison::rescale() {
  applyColumns(headerCells);
  for (auto* row : rows) row->rescale();
  updateGeometry();
}
QList<PvRow*> Comparison::visibleRows() const {
  QList<PvRow*> result; for (auto* row : order) if (row->matches) result << row;
  return result;
}
void Comparison::select(PvRow* row, bool on) {
  if (row->selected == on) return;
  row->selected = on; row->setProperty("selected", on); row->style()->unpolish(row); row->style()->polish(row); row->update();
}
void Comparison::clearSelection() { for (auto* row : rows) select(row, false); }
QList<Channel*> Comparison::selection() const {
  QList<Channel*> result; for (auto* row : order) if (row->selected && row->matches) result << row->channel;
  return result;
}
void Comparison::rowClicked(PvRow* row, Qt::KeyboardModifiers modifiers) {
  setFocus(Qt::MouseFocusReason);
  if (modifiers & Qt::ControlModifier) { select(row, !row->selected); cursor = anchor = row; }
  else if ((modifiers & Qt::ShiftModifier) && anchor) { moveCursor(row, true); return; }
  else { clearSelection(); select(row, true); cursor = anchor = row; }
  if (selected) selected(row->channel);
}
void Comparison::moveCursor(PvRow* row, bool extend) {
  if (!row) return;
  if (extend && anchor) {
    const auto shown = visibleRows(); const int a = shown.indexOf(anchor), b = shown.indexOf(row);
    for (int i = 0; i < shown.size(); ++i) select(shown[i], i >= std::min(a, b) && i <= std::max(a, b));
  } else { clearSelection(); select(row, true); anchor = row; }
  cursor = row; scroll->ensureWidgetVisible(row, 0, 0);
  if (selected) selected(row->channel);
}
void Comparison::keyPressEvent(QKeyEvent* event) {
  const auto shown = visibleRows();
  const bool extend = event->modifiers() & Qt::ShiftModifier;
  const int at = shown.indexOf(cursor), page = std::max(1, scroll->viewport()->height() / std::max(1, shown.isEmpty() ? 1 : shown.front()->height()) - 1);
  if (event->matches(QKeySequence::SelectAll)) { for (auto* row : shown) select(row, true); return; }
  if (event->matches(QKeySequence::Copy)) { QApplication::clipboard()->setText(copyText(Text)); return; }
  if (shown.isEmpty()) { QWidget::keyPressEvent(event); return; }
  switch (event->key()) {
  case Qt::Key_Down: moveCursor(shown.value(std::min(int(shown.size()) - 1, at + 1)), extend); return;
  case Qt::Key_Up: moveCursor(shown.value(std::max(0, at - 1)), extend); return;
  case Qt::Key_PageDown: moveCursor(shown.value(std::min(int(shown.size()) - 1, at + page)), extend); return;
  case Qt::Key_PageUp: moveCursor(shown.value(std::max(0, at - page)), extend); return;
  case Qt::Key_Home: moveCursor(shown.front(), extend); return;
  case Qt::Key_End: moveCursor(shown.back(), extend); return;
  case Qt::Key_Return: case Qt::Key_Enter: if (cursor && cursor->matches) cursor->inspect(); return;
  case Qt::Key_Escape: clearSelection(); return;
  default: QWidget::keyPressEvent(event);
  }
}
bool Comparison::eventFilter(QObject* object, QEvent* event) {
  if (object == filter && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Down) {
    const auto shown = visibleRows();
    if (!shown.isEmpty()) { setFocus(Qt::TabFocusReason); moveCursor(cursor && cursor->matches ? cursor : shown.front(), false); }
    return true;
  }
  if (event->type() == QEvent::MouseButtonRelease && object->property("sortable").toBool()) {
    sortBy(object->property("column").toInt()); return true;
  }
  return QWidget::eventFilter(object, event);
}
QString Comparison::copyText(CopyFormat format) const {
  QStringList lines;
  if (format == Csv) lines << "pv,provider,value,units,state,severity,timestamp_utc,age_s";
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  for (auto* channel : selection()) {
    const auto& s = channel->snapshot;
    if (format == Names) lines << portableName(s.provider, s.name);
    else if (format == Text) lines << QStringList{portableName(s.provider, s.name), s.exactText(), s.units, stateText(s), s.timestamp()}.join('\t');
    else lines << QStringList{csvCell(s.provider + "://" + s.name), s.provider, csvCell(s.exactText()), csvCell(s.units), csvCell(stateText(s)),
        QString::number(s.severity), s.timestamp(), channel->lastUpdateMs ? QString::number((now - channel->lastUpdateMs) / 1000.0, 'f', 1) : QString()}.join(',');
  }
  return lines.join('\n') + (lines.size() > (format == Csv ? 1 : 0) ? "\n" : "");
}
void Comparison::rowMenu(PvRow* row, const QPoint& position) {
  if (!row->selected) { clearSelection(); select(row, true); cursor = anchor = row; if (selected) selected(row->channel); }
  QList<PvRow*> chosen; for (auto* r : order) if (r->selected && r->matches) chosen << r;
  const int n = chosen.size();
  QMenu menu(this);
  auto* inspect = menu.addAction("Inspect"); menu.addSeparator();
  auto* copyRows = menu.addAction(n > 1 ? QString("Copy %1 rows").arg(n) : "Copy row"); copyRows->setShortcut(QKeySequence::Copy);
  auto* copyCsv = menu.addAction("Copy as CSV"); auto* copyNames = menu.addAction(n > 1 ? "Copy PV names" : "Copy PV name");
  auto* strip = menu.addAction("Open in QtStripTool"); menu.addSeparator();
  auto* reconnect = menu.addAction("Reconnect"); auto* closeRows = menu.addAction(n > 1 ? QString("Close %1 PVs").arg(n) : "Close PV");
  auto* picked = menu.exec(position);
  if (picked == inspect) row->inspect();
  else if (picked == copyRows) QApplication::clipboard()->setText(copyText(Text));
  else if (picked == copyCsv) QApplication::clipboard()->setText(copyText(Csv));
  else if (picked == copyNames) QApplication::clipboard()->setText(copyText(Names));
  else if (picked == strip && plot) plot(selection());
  else if (picked == reconnect) for (auto* r : chosen) r->channel->start();
  else if (picked == closeRows) for (auto* r : chosen) QTimer::singleShot(0, body, r->close);
}
void Comparison::startDrag(PvRow* row, bool onlyThis) {
  QStringList names;
  if (onlyThis || !row->selected) names << portableName(row->snapshot().provider, row->snapshot().name);
  else for (auto* channel : selection()) names << portableName(channel->snapshot.provider, channel->snapshot.name);
  auto* mime = new QMimeData; mime->setText(names.join(' '));
  QDrag drag(row); drag.setMimeData(mime); drag.exec(Qt::CopyAction);
}
void Comparison::sortBy(int column) {
  if (column != Name && column != Value && column != State && column != Age) column = Added;
  if (column == Added || column != sortKey) { sortKey = column; descending = false; }
  else if (!descending) descending = true;
  else { sortKey = Added; descending = false; }
  updateHeader(); sortRows(true);
}
void Comparison::updateHeader() {
  const char* titles[] = {"", "PV", "Value", "Units", "", "State", "Age", "Trend", ""};
  for (int i = 0; i < headerCells.size(); ++i)
    static_cast<QLabel*>(headerCells[i])->setText(QString(titles[i]) + (i == sortKey ? (descending ? " ▼" : " ▲") : ""));
}
void Comparison::sortRows(bool force) {
  // Live columns re-sort at most once a second so rows do not jitter.
  if (!force && (sortKey == Added || sortKey == Name || (lastSort.isValid() && lastSort.elapsed() < 1000))) return;
  lastSort.start();
  auto sorted = rows;
  if (sortKey != Added) {
    auto less = [&](PvRow* a, PvRow* b) {
      const auto &x = a->snapshot(), &y = b->snapshot();
      switch (sortKey) {
      case Name: { const int c = naturalCompare(x.name, y.name); return c ? c < 0 : x.provider < y.provider; }
      case Value: {
        const bool xn = x.hasValue && x.numeric && !x.array && std::isfinite(x.number), yn = y.hasValue && y.numeric && !y.array && std::isfinite(y.number);
        if (xn != yn) return xn;
        if (xn) return x.number < y.number;
        return naturalCompare(x.exactText(), y.exactText()) < 0;
      }
      case State: return stateRank(x) < stateRank(y);
      default: {
        // Most recent first; PVs without a value sort last.
        const qint64 xa = a->channel->lastUpdateMs ? a->channel->lastUpdateMs : std::numeric_limits<qint64>::min();
        const qint64 ya = b->channel->lastUpdateMs ? b->channel->lastUpdateMs : std::numeric_limits<qint64>::min();
        return xa > ya;
      }
      }
    };
    std::stable_sort(sorted.begin(), sorted.end(), [&](PvRow* a, PvRow* b) { return descending ? less(b, a) : less(a, b); });
  }
  if (sorted == order) return;
  order = sorted;
  for (auto* row : order) list->removeWidget(row);
  for (int i = 0; i < order.size(); ++i) list->insertWidget(i, order[i]);
  QTimer::singleShot(0, this, [this] { refresh(); });
}
void Comparison::refresh() {
  const auto text = filter->text(); bool changed = false; int matching = 0, alarms = 0, disconnected = 0;
  const bool quick = alarmsOnly->isChecked() || disconnectedOnly->isChecked();
  for (auto* row : rows) {
    const auto& s = row->snapshot();
    const bool alarm = s.connected && s.readable && s.hasValue && s.severity > 0, lost = !s.connected;
    alarms += alarm; disconnected += lost;
    const bool matches = (text.isEmpty() || (s.name + ' ' + s.provider + ' ' + s.description).contains(text, Qt::CaseInsensitive)) &&
        (!quick || (alarmsOnly->isChecked() && alarm) || (disconnectedOnly->isChecked() && lost));
    if (row->matches != matches) { row->matches = matches; row->setVisible(matches); changed = true; }
    matching += matches;
  }
  const auto alarmText = alarms ? QString("Alarms (%1)").arg(alarms) : QString("Alarms"), lostText = disconnected ? QString("Disconnected (%1)").arg(disconnected) : QString("Disconnected");
  if (alarmsOnly->text() != alarmText) alarmsOnly->setText(alarmText);
  if (disconnectedOnly->text() != lostText) disconnectedOnly->setText(lostText);
  empty->setVisible(!rows.isEmpty() && !matching);
  sortRows(false);
  // Rows move when the filter changes; catch up once the layout settles.
  if (changed) { QTimer::singleShot(0, this, [this] { refresh(); }); return; }
  if (!isVisible() || window()->isMinimized()) return;
  // Rows outside the viewport catch up when scrolled into view.
  const auto view = scroll->viewport()->rect();
  for (auto* row : rows)
    if (row->matches && row->dirty && view.intersects(QRect(row->mapTo(scroll->viewport(), QPoint()), row->size()))) row->refresh();
}
void Comparison::updateAges() {
  const auto view = scroll->viewport()->rect(); const qint64 now = QDateTime::currentMSecsSinceEpoch();
  for (auto* row : rows)
    if (row->matches && view.intersects(QRect(row->mapTo(scroll->viewport(), QPoint()), row->size()))) row->updateAge(now);
}
int Comparison::preferredHeight() const {
  const auto margins = frame->layout()->contentsMargins(); const int spacing = frame->layout()->spacing();
  const int rowHeight = rows.isEmpty() ? 0 : rows.front()->height();
  return margins.top() + margins.bottom() + 2 * frame->frameWidth() + tools->sizeHint().height() + header->sizeHint().height() +
      2 * spacing + int(rows.size()) * rowHeight + 2;
}
void Comparison::showEvent(QShowEvent* event) { QWidget::showEvent(event); ageTimer->start(); QTimer::singleShot(0, this, [this] { refresh(); }); }
void Comparison::hideEvent(QHideEvent* event) { ageTimer->stop(); QWidget::hideEvent(event); }
void Comparison::changeEvent(QEvent* event) { QWidget::changeEvent(event); if (event->type() == QEvent::FontChange) rescale(); }
void Comparison::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  const bool provider = width() >= 760, trend = width() >= 600;
  if (provider != showProvider || trend != showTrend) {
    showProvider = provider; showTrend = trend; headerCells[Provider]->setVisible(provider); headerCells[Trend]->setVisible(trend);
    for (auto* row : rows) row->setColumns(provider, trend);
  }
  QTimer::singleShot(0, this, [this] { refresh(); });
}
} // namespace probe
