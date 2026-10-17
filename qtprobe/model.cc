#include "model.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <atomic>
namespace probe {
Address Address::parse(QString text, QString provider) {
  text = text.trimmed();
  if (text.startsWith("pva://")) { provider = "pva"; text.remove(0, 6); }
  else if (text.startsWith("ca://")) { provider = "ca"; text.remove(0, 5); }
  if (provider != "ca" && provider != "pva")
    throw std::runtime_error("Provider must be ca or pva");
  if (text.isEmpty() || text.contains(QRegularExpression("[\\s\\x00-\\x1f]")) || text.contains("://"))
    throw std::runtime_error("Enter a PV name, ca://PV or pva://PV");
  return {text, provider};
}
QString epicsTimestamp(qint64 seconds, int nanoseconds) {
  if (!seconds) return {};
  return QDateTime::fromSecsSinceEpoch(seconds, Qt::UTC).toString("yyyy-MM-dd'T'HH:mm:ss") +
      QString(".%1Z").arg(nanoseconds, 9, 10, QLatin1Char('0'));
}
QString Snapshot::timestamp() const { return epicsTimestamp(seconds, nanoseconds); }
QString Snapshot::severityText() const {
  const QStringList names{"NO_ALARM", "MINOR", "MAJOR", "INVALID"};
  return severity >= 0 && severity < names.size() ? names[severity] : QString::number(severity);
}
bool Snapshot::floating() const {
  const auto t = type.toLower();
  return numeric && choices.isEmpty() && (t.contains("double") || t.contains("float"));
}
void Snapshot::decodeBytes() {
  text.clear(); longString = false;
  if (!bytes || !hasValue) return;
  QByteArray raw; raw.reserve(values.size());
  bool terminated = false;
  for (const auto& value : values) {
    const int byte = value.toInt() & 0xff;
    if (!byte) { terminated = true; break; }
    raw.append(char(byte));
  }
  text = QString::fromUtf8(raw);
  // "$" fields are long strings. Otherwise treat printable, non-empty
  // contents as text so binary buffers keep their numeric view.
  bool printable = !raw.isEmpty() && !text.contains(QChar::ReplacementCharacter);
  for (const auto ch : text) if (!ch.isPrint() && ch != '\t' && ch != '\n' && ch != '\r') { printable = false; break; }
  longString = name.endsWith('$') || (printable && (terminated || raw.size() == values.size()));
}
QString Snapshot::exactText() const {
  if (!hasValue) return "—";
  if (longString) return text;
  if (array) return "[" + values.mid(0, 12).join(", ") + (count > 12 ? ", …" : "") + "]";
  if (!choices.isEmpty() && std::isfinite(number) && number >= 0 && number < choices.size())
    return choices[int(number)] + " (" + values.value(0) + ")";
  return values.value(0, "Structured data — see Info");
}
QString Snapshot::editText() const {
  if (longString) return text;
  if (array) return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(values)).toJson(QJsonDocument::Compact));
  return values.value(0);
}
QString Snapshot::valueText(const QString& format) const {
  if (!hasValue) return "—";
  if (bytes && format == "Text") return text;
  if (longString && format == "Native") return text;
  if (numeric && !array && format != "Native") {
    if (format == "Hex" || format == "Binary") {
      // Parse the exact integer rather than rounding a double representation.
      bool ok;
      const auto value = values.value(0).toLongLong(&ok);
      if (ok) return (format == "Hex" ? "0x" : "0b") + QString::number(value, format == "Hex" ? 16 : 2);
      const auto unsignedValue = values.value(0).toULongLong(&ok);
      if (ok) return (format == "Hex" ? "0x" : "0b") + QString::number(unsignedValue, format == "Hex" ? 16 : 2);
      return values.value(0);
    }
    if (format == "Fixed" || format == "Scientific")
      return QString::number(number, format == "Scientific" ? 'e' : 'f', qBound(0, precision < 0 ? 6 : precision, 15));
  }
  // Like the Motif Probe, show floating-point values with the PV precision.
  if (floating() && precision >= 0) {
    const int digits = qBound(0, precision, 17);
    auto rounded = [&](const QString& exact) {
      bool ok; const double v = exact.toDouble(&ok);
      // Fixed notation like %f; beyond 1e21 keep the exact form instead.
      if (!ok || !std::isfinite(v) || std::abs(v) >= 1e21) return exact;
      return QString::number(v, 'f', digits);
    };
    if (!array) return rounded(values.value(0));
    QStringList shown; for (const auto& v : values.mid(0, 12)) shown << rounded(v);
    return "[" + shown.join(", ") + (count > 12 ? ", …" : "") + "]";
  }
  return exactText();
}
QMap<QString, QString> Snapshot::information() const {
  auto info = fields;
  info["PV"] = name; info["Provider"] = provider; info["Host"] = host;
  info["Active"] = connected ? "yes" : "no";
  info["Read access"] = readable ? "yes" : "no";
  info["Write access"] = writable ? "yes" : "no";
  info["Native data type"] = type; info["Structure ID"] = structureId;
  info["Field type"] = !choices.isEmpty() ? "enum" : array ? "scalar array" : values.isEmpty() ? "structure" : "scalar";
  info["Element count"] = QString::number(count); info["Capacity"] = QString::number(capacity);
  info["Retained elements"] = QString::number(values.size());
  info["Truncated"] = truncated ? "yes" : "no";
  info["Alarm severity"] = severityText(); info["Alarm status"] = QString::number(status);
  info["Alarm message"] = alarmMessage; info["Timestamp (UTC)"] = timestamp();
  info["Units"] = units; info["Description"] = description;
  info["Precision"] = QString::number(precision);
  if (bytes) info["Long string"] = longString ? "yes" : "no";
  for (int i = 0; i < choices.size(); ++i) info[QString("Enum choice %1").arg(i)] = choices[i];
  if (!error.isEmpty()) info["Diagnostic"] = error;
  return info;
}
QJsonObject Snapshot::json() const {
  QJsonObject info;
  const auto map = information();
  for (auto it = map.cbegin(); it != map.cend(); ++it) info[it.key()] = it.value();
  return {{"pv", name}, {"provider", provider}, {"connected", connected},
      {"values", QJsonArray::fromStringList(values)}, {"timestamp", timestamp()},
      {"severity", severityText()}, {"metadata", info}};
}
namespace {
std::atomic<size_t> historyBytes{0}, historyBudget{size_t(256) << 20};
std::atomic<int> historyCount{0};
QString bounded(QString text) {
  if (text.size() > History::maxValueLength) { text.truncate(History::maxValueLength - 1); text += QChar(0x2026); }
  text.squeeze(); return text;
}
// An estimate: the sample, its string data and deque/allocator overhead.
size_t sampleBytes(const Sample& s) { return sizeof(Sample) + 48 + size_t(s.value.capacity()) * sizeof(QChar); }
} // namespace
History::History(size_t capacity) : capacity_(capacity) { ++historyCount; }
History::~History() { clear(); --historyCount; }
size_t History::totalBytes() { return historyBytes; }
void History::setMemoryBudget(size_t bytes) { historyBudget = std::max<size_t>(bytes, 1 << 20); }
void History::popFront() { bytes_ -= sampleBytes(samples.front()); historyBytes -= sampleBytes(samples.front()); samples.pop_front(); }
void History::clear() { historyBytes -= bytes_; bytes_ = 0; samples.clear(); }
void History::append(const Snapshot& s) {
  if (!capacity_) return;
  Sample sample;
  sample.value = bounded(s.exactText()); sample.seconds = s.seconds; sample.nanoseconds = s.nanoseconds;
  sample.receivedMs = QDateTime::currentMSecsSinceEpoch();
  sample.number = s.hasValue && s.numeric && s.connected && s.readable ? s.number : std::numeric_limits<double>::quiet_NaN();
  sample.severity = s.severity; sample.connected = s.connected; sample.serial = ++nextSerial_;
  const auto added = sampleBytes(sample);
  samples.push_back(std::move(sample)); bytes_ += added; historyBytes += added;
  // This PV's share of the shared budget; the newest event is always kept.
  const size_t share = historyBudget / size_t(std::max(1, int(historyCount)));
  while (samples.size() > 1 && (samples.size() > capacity_ || bytes_ > share)) popFront();
}
QString History::statistics() const {
  size_t n = 0;
  double mean = 0, m2 = 0, lo = std::numeric_limits<double>::infinity(), hi = -lo;
  for (const auto& s : samples) if (std::isfinite(s.number)) {
    ++n; const double delta = s.number - mean; mean += delta / n; m2 += delta * (s.number - mean);
    lo = std::min(lo, s.number); hi = std::max(hi, s.number);
  }
  if (!n) return QString("%1 events; no numeric samples").arg(samples.size());
  return QString("%1 numeric samples    Min %2    Max %3    Mean %4    σ %5")
      .arg(n).arg(lo, 0, 'g', 8).arg(hi, 0, 'g', 8).arg(mean, 0, 'g', 8).arg(std::sqrt(m2 / n), 0, 'g', 8);
}
QString History::stateStatistics(const QStringList& choices, qint64 nowMs) const {
  QMap<int, qint64> spent; qint64 total = 0; int changes = 0, events = 0; double previous = std::numeric_limits<double>::quiet_NaN();
  for (size_t i = 0; i < samples.size(); ++i) {
    const auto& s = samples[i];
    if (!std::isfinite(s.number)) { previous = s.number; continue; }
    ++events;
    if (std::isfinite(previous) && previous != s.number) ++changes;
    previous = s.number;
    // A state lasts until the next event; the newest lasts until now.
    const qint64 end = i + 1 < samples.size() ? samples[i + 1].receivedMs : nowMs;
    const qint64 duration = std::max<qint64>(0, end - s.receivedMs);
    spent[int(s.number)] += duration; total += duration;
  }
  if (!events) return QString("%1 events; no state samples").arg(samples.size());
  QStringList parts;
  for (auto it = spent.cbegin(); it != spent.cend(); ++it) {
    const auto name = choices.value(it.key(), QString::number(it.key()));
    const double share = total > 0 ? 100.0 * it.value() / total : 0;
    parts << name + ' ' + (share > 0 && share < 1 ? QString("<1%") : QString("%1%").arg(qRound(share)));
  }
  // Right after the first events there is no measurable time yet.
  if (total <= 0) return QString("%1 events    %2 change%3").arg(events).arg(changes).arg(changes == 1 ? "" : "s");
  const double seconds = total / 1000.0;
  const auto span = seconds < 120 ? QString("%1 s").arg(seconds, 0, 'f', seconds < 10 ? 1 : 0) : seconds < 7200 ? QString("%1 min").arg(seconds / 60, 0, 'f', 0) : QString("%1 h").arg(seconds / 3600, 0, 'f', 1);
  return QString("%1 events    %2 change%3    Time in state over %4: %5").arg(events).arg(changes).arg(changes == 1 ? "" : "s").arg(span, parts.join(", "));
}
QString csvCell(QString s) { s.replace('"', "\"\""); return '"' + s + '"'; }
QVector<int> plotEnvelope(const QVector<double>& values, int pixels) {
  QVector<int> result;
  if (pixels <= 0 || values.isEmpty()) return result;
  const int size = int(values.size());
  auto gap = [&] { if (result.isEmpty() || result.back() != -1) result.append(-1); };
  if (qint64(size) <= qint64(pixels) * 4) {
    result.reserve(size);
    for (int i = 0; i < size; ++i) { if (std::isfinite(values[i])) result.append(i); else gap(); }
    return result;
  }
  result.reserve(pixels * 6);
  for (int bin = 0; bin < pixels; ++bin) {
    const int begin = int(qint64(bin) * size / pixels), end = int(qint64(bin + 1) * size / pixels);
    int first = -1, last = -1, low = -1, high = -1; bool broken = false;
    for (int i = begin; i < end; ++i) {
      if (!std::isfinite(values[i])) { broken = true; continue; }
      if (first < 0) first = low = high = i;
      last = i; if (values[i] < values[low]) low = i; if (values[i] > values[high]) high = i;
    }
    // A gap occupying part of one pixel breaks both edges of that column.
    // This avoids drawing a false connection across a missing observation.
    if (broken) gap();
    if (first >= 0) {
      std::array<int, 4> indices{first, low, high, last}; std::sort(indices.begin(), indices.end());
      int previous = -1;
      for (int index : indices) if (index != previous) { result.append(index); previous = index; }
    }
    if (broken) gap();
  }
  return result;
}
QByteArray History::csv() const {
  QString text = "timestamp_utc,received_ms,value,severity,connected\n";
  for (const auto& s : samples)
    text += csvCell(s.timestamp()) + ',' + QString::number(s.receivedMs) + ',' + csvCell(s.value) + ',' +
        QString::number(s.severity) + ',' + (s.connected ? "true" : "false") + '\n';
  return text.toUtf8();
}
QStringList parseWrite(const Snapshot& s, const QString& text, QString* error) {
  error->clear();
  QStringList values;
  if (s.longString) {
    // Text replaces the whole string; CA fields keep a terminating NUL.
    const auto bytes = text.toUtf8();
    const bool ca = s.provider == "ca", signedBytes = s.provider == "pva" && !s.type.contains("ubyte");
    if (ca && s.capacity && quint64(bytes.size()) + 1 > s.capacity) {
      *error = QString("Text exceeds the %1-byte field.").arg(s.capacity - 1); return {};
    }
    for (const char byte : bytes) values << QString::number(signedBytes ? int(byte) : int(quint8(byte)));
    if (ca || values.isEmpty()) values << "0";
    return values;
  }
  if (s.array) {
    QJsonParseError e;
    const auto doc = QJsonDocument::fromJson(text.toUtf8(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isArray()) {
      *error = "Array writes require a JSON array; quote 64-bit integers to preserve precision."; return {};
    }
    for (const auto& v : doc.array()) {
      if (v.isString()) values << v.toString();
      else if (v.isDouble()) {
        const double number = v.toDouble();
        if (std::abs(number) > 9007199254740991.0 && std::trunc(number) == number) {
          *error = "Quote integers above 2^53−1 in JSON arrays."; return {};
        }
        values << QString::number(number, 'g', 17);
      } else if (v.isBool()) values << (v.toBool() ? "1" : "0");
      else { *error = "Only scalar array elements are supported."; return {}; }
    }
    if (values.isEmpty()) { *error = "Empty array writes are not supported."; return {}; }
    if (s.provider == "ca" && s.capacity && quint64(values.size()) > s.capacity) {
      *error = "Array exceeds the channel capacity."; return {};
    }
  } else values << text;
  if (!s.choices.isEmpty()) {
    int index = s.choices.indexOf(text);
    bool ok = true;
    if (index < 0) index = text.toInt(&ok);
    if (!ok || index < 0 || index >= s.choices.size()) {
      *error = "Choose an enum label or a valid enum index."; return {};
    }
    values = QStringList{QString::number(index)};
  }
  return values;
}
std::deque<WriteRecord>& writeLog() { static std::deque<WriteRecord> log; return log; }
void logWrite(WriteRecord record) {
  record.before = bounded(record.before); record.after = bounded(record.after); record.result = bounded(record.result);
  auto& log = writeLog(); log.push_back(std::move(record));
  while (log.size() > 1000) log.pop_front();
}
QByteArray writeLogCsv() {
  QString text = "time_local,pv,before,written,ok,result\n";
  for (const auto& r : writeLog())
    text += csvCell(r.time.toString(Qt::ISODateWithMs)) + ',' + csvCell(r.pv) + ',' + csvCell(r.before) + ',' +
        csvCell(r.after) + ',' + (r.ok ? "true" : "false") + ',' + csvCell(r.result) + '\n';
  return text.toUtf8();
}
QString stripToolConfig(const QList<Snapshot>& snapshots) {
  QString text = "StripConfig                   1.2\n";
  int curve = 0;
  auto field = [&](const QString& key, const QString& value) { text += QString("Strip.Curve.%1.%2").arg(curve).arg(key).leftJustified(30) + value + '\n'; };
  for (const auto& s : snapshots) {
    if (curve >= 10 || s.provider != "ca" || s.name.toUtf8().size() > 63) continue;
    field("Name", s.name);
    if (!s.units.isEmpty() && !s.units.contains(QRegularExpression("\\s")) && s.units.toUtf8().size() <= 31) field("Units", s.units);
    if (s.displayHigh > s.displayLow) { field("Min", QString::number(s.displayLow, 'g', 17)); field("Max", QString::number(s.displayHigh, 'g', 17)); }
    if (s.precision >= 0 && s.precision <= 20) field("Precision", QString::number(s.precision));
    ++curve;
  }
  return text;
}
} // namespace probe
