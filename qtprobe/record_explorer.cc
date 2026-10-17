#include "record_explorer.h"
#include "appearance.h"
#include "channel.h"
#include <QtWidgets>
namespace probe {
namespace {
QString shownValue(const Snapshot& s) {
  if (!s.hasValue) return {};
  if (!s.choices.isEmpty() && std::isfinite(s.number) && s.number >= 0 && s.number < s.choices.size()) return s.choices[int(s.number)];
  if (s.longString || s.bytes) return s.text;
  return s.values.value(0);
}
class FieldTable final : public QTableWidget {
public:
  FieldTable() : QTableWidget(0, 2) {
    setObjectName("recordFields"); setHorizontalHeaderLabels({"Field", "Value"});
    setEditTriggers(NoEditTriggers); setSelectionMode(NoSelection); setFocusPolicy(Qt::NoFocus); setShowGrid(false); setWordWrap(false);
    verticalHeader()->hide(); horizontalHeader()->setStretchLastSection(true); setMouseTracking(true);
    verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
  }
  QSize sizeHint() const override { return {360, horizontalHeader()->sizeHint().height() + verticalHeader()->defaultSectionSize() * 12 + 2 * frameWidth()}; }
  QSize minimumSizeHint() const override { return {160, horizontalHeader()->sizeHint().height() + verticalHeader()->defaultSectionSize() * 4}; }
protected:
  void changeEvent(QEvent* event) override {
    QTableWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange) { verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8); updateGeometry(); }
  }
};
} // namespace
RecordExplorer::RecordExplorer(Channel* subject, double timeout, QWidget* parent) : QWidget(parent), subject(subject), timeout(timeout) {
  setObjectName("recordExplorer");
  auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4);
  summary = new QLabel; summary->setObjectName("recordDiagnosis"); summary->setTextFormat(Qt::PlainText); summary->setWordWrap(true);
  summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); summary->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(summary);
  table = new FieldTable; layout->addWidget(table, 1);
  table->setColumnWidth(0, table->fontMetrics().horizontalAdvance("Processing  SCAN") + 24);
  connect(table, &QTableWidget::cellClicked, this, [this](int row, int column) {
    const auto target = table->item(row, 1) ? table->item(row, 1)->data(Qt::UserRole).toString() : QString();
    if (column == 1 && !target.isEmpty() && openLink) openLink(target);
  });
  // Field updates arrive in bursts; refresh at most a few times a second.
  update = new QTimer(this); update->setSingleShot(true); update->setInterval(150);
  connect(update, &QTimer::timeout, this, &RecordExplorer::refresh);
  // Staleness and connection timeouts advance without monitor events.
  tick = new QTimer(this); tick->setInterval(2000);
  connect(tick, &QTimer::timeout, this, [this] {
    // IOCs post no monitors for PACT and UDF, or for DISA read through SDIS.
    for (const auto& row : rows) if (row.spec.name == "PACT" || row.spec.name == "UDF" || row.spec.name == "DISA") row.channel->refetch();
    refresh();
  });
  summary->setText("Open to read the record's fields.");
}
RecordExplorer::~RecordExplorer() { setActive(false); }
QSize RecordExplorer::sizeHint() const { return {360, summary->sizeHint().height() + 4 + table->sizeHint().height()}; }
void RecordExplorer::setActive(bool on) {
  if (on == active()) return;
  update->stop(); tick->stop(); rows.clear(); loadedType.clear(); layoutKey.clear();
  if (!on) return;
  record = recordName(subject->snapshot.name); provider = subject->snapshot.provider;
  started.start(); summary->setText(QString("Reading the fields of record %1…").arg(record)); setTone(summary, "muted");
  addFields(commonRecordFields()); tick->start(); refresh();
}
void RecordExplorer::addFields(const QVector<RecordField>& fields) {
  for (const auto& spec : fields) {
    // Links use FIELD$ so long link text is not cut to 40 characters.
    auto channel = std::make_unique<Channel>(Address{record + '.' + spec.name + (spec.link ? "$" : ""), provider}, true, spec.link ? 4096 : 256, 1, timeout);
    channel->lightweight = true;
    connect(channel.get(), &Channel::changed, this, [this] { if (!update->isActive()) update->start(); });
    channel->start(); rows.push_back({spec, std::move(channel), {}}); rows.back().since.start();
  }
}
QString RecordExplorer::fieldText(const QString& field) const {
  for (const auto& row : rows) if (row.spec.name == field) return shownValue(row.channel->snapshot);
  return {};
}
void RecordExplorer::refresh() {
  if (rows.empty()) return;
  const qint64 limit = qint64(timeout * 1000);
  const bool expired = started.elapsed() > limit;
  // The record type decides which links and deadbands to read. Add them
  // before taking pointers into rows.
  QString type, scan;
  for (const auto& row : rows) {
    if (row.spec.name == "RTYP") type = shownValue(row.channel->snapshot);
    if (row.spec.name == "SCAN") scan = shownValue(row.channel->snapshot);
  }
  if (!type.isEmpty() && type != loadedType) { loadedType = type; addFields(typeRecordFields(type)); }
  else if (expired && loadedType.isEmpty() && !scan.isEmpty()) { loadedType = "?"; addFields(typeRecordFields({})); }
  // Disconnected fields keep their last values on screen but are not diagnosed.
  QMap<QString, QString> values, display; QVector<const Row*> shown; bool disconnected = !subject->snapshot.connected;
  for (const auto& row : rows) {
    const auto& s = row.channel->snapshot;
    if (s.hasValue) display[row.spec.name] = row.spec.name == "STAT" ? alarmStatusName(shownValue(s)) : shownValue(s);
    if (s.hasValue && s.connected) values[row.spec.name] = display[row.spec.name];
    disconnected = disconnected || (s.hasValue && !s.connected);
    // Fields this record type or IOC version lacks never connect; drop them.
    if (s.hasValue || row.since.elapsed() <= limit) shown << &row;
  }
  if (expired && !values.contains("SCAN") && !values.contains("RTYP")) {
    summary->setText(QString("Record fields are unavailable: the server does not publish %1.SCAN or %1.RTYP (for example a portable "
        "server, or a name that is not an IOC record).").arg(record));
    setTone(summary, "warning"); table->setRowCount(0); shownFindings.clear(); return;
  }
  const double age = subject->lastUpdateMs ? (QDateTime::currentMSecsSinceEpoch() - subject->lastUpdateMs) / 1000.0 : -1;
  shownFindings = disconnected ? QVector<Finding>{{"Record fields are disconnected; the diagnosis resumes when they reconnect.", true}} : recordDiagnosis(values, age);
  QStringList lines; bool problem = false;
  for (const auto& finding : shownFindings) { lines << (finding.problem ? "⚠ " : "• ") + finding.text; problem |= finding.problem; }
  const auto head = type.isEmpty() ? record : QString("%1 · %2 record").arg(record, type);
  summary->setText(head + (lines.isEmpty() ? QString() : "\n" + lines.join('\n'))); setTone(summary, problem ? "warning" : "muted");
  // Rebuild rows only when the set of fields changes; otherwise update values.
  QString key; for (const auto* row : shown) key += row->spec.name + ',';
  const auto c = colors(window()->palette());
  if (key != layoutKey) {
    layoutKey = key; table->clearSpans(); table->setRowCount(0);
    QString group;
    for (const auto* row : shown) {
      if (row->spec.group != group) {
        group = row->spec.group; const int r = table->rowCount(); table->insertRow(r);
        auto* title = new QTableWidgetItem(group.toUpper()); auto font = title->font(); font.setBold(true); font.setPointSizeF(font.pointSizeF() * 0.8);
        title->setFont(font); title->setForeground(c.muted); table->setItem(r, 0, title); table->setSpan(r, 0, 1, 2);
      }
      const int r = table->rowCount(); table->insertRow(r);
      auto* name = new QTableWidgetItem(row->spec.name); name->setToolTip(row->spec.help); name->setData(Qt::UserRole + 1, row->spec.name);
      table->setItem(r, 0, name); table->setItem(r, 1, new QTableWidgetItem);
    }
  }
  for (int r = 0; r < table->rowCount(); ++r) {
    const auto field = table->item(r, 0) ? table->item(r, 0)->data(Qt::UserRole + 1).toString() : QString();
    if (field.isEmpty()) continue;
    const Row* row = nullptr; for (const auto* candidate : shown) if (candidate->spec.name == field) row = candidate;
    if (!row) continue;
    const auto& s = row->channel->snapshot; auto* item = table->item(r, 1);
    const auto text = !s.hasValue ? QString("…") : display.value(field).isEmpty() && row->spec.link ? QString("(empty)") : display.value(field);
    const auto target = row->spec.link && s.connected ? linkTarget(display.value(field)) : QString();
    if (item->text() != text) item->setText(text);
    item->setData(Qt::UserRole, target);
    auto font = item->font(); font.setUnderline(!target.isEmpty()); item->setFont(font);
    item->setForeground(!target.isEmpty() ? c.accent : !s.hasValue || !s.connected || (row->spec.link && display.value(field).isEmpty()) ? c.muted : c.text);
    item->setToolTip(!target.isEmpty() ? QString("%1\nClick to open %2. Modifiers: PP/NPP process the target or not, CA/CP/CPP use Channel Access "
        "(CP processes this record on change), MS/NMS pass alarms.").arg(text, target) : text);
  }
}
} // namespace probe
