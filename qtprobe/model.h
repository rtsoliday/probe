// QtProbe data model. Distributed under the license in ../LICENSE.
#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
#error QtProbe requires Qt 5.15 or newer
#endif
namespace probe {
struct Address {
  QString name, provider;
  static Address parse(QString text, QString defaultProvider = "ca");
  QString uri() const { return provider + "://" + name; }
};
// The name other EPICS tools accept: plain for CA, pva:// otherwise.
inline QString portableName(const QString& provider, const QString& name) { return provider == "ca" ? name : provider + "://" + name; }
// One field of a pvAccess structure as the Structure browser shows it. The
// PVA backend builds the tree, bounded in fields and in listed array
// elements; CA PVs have none.
struct FieldNode {
  enum Kind { Scalar, Array, Structure, Union, StructureArray, UnionArray };
  Kind kind = Scalar;
  // type is the pvData ID (double, string[], epics:nt/NTScalar:1.0, any…).
  // value is the scalar text, an array summary, or a summary of a
  // well-known structure (enum_t, alarm_t, time_t or a scalar "value").
  QString name, type, value;
  // Elements received for arrays; members or elements left out by the bounds.
  quint64 length = 0, omitted = 0;
  QVector<FieldNode> children;
  bool container() const { return kind != Scalar; }
};
using FieldTree = std::shared_ptr<const FieldNode>;
// Indented "type name value" lines, like pvget -v, for copying.
QString structureText(const FieldNode&, int depth = 0);
QJsonObject structureJson(const FieldNode&);
// Leaf fields by dotted path (value.index, alarm.severity…), at most limit.
void flattenStructure(const FieldNode&, QMap<QString, QString>&, int limit = 512);
struct Snapshot {
  QString name, provider, host, type, structureId, units, description;
  bool connected = false, readable = false, writable = false;
  bool hasValue = false, numeric = false, array = false, truncated = false;
  bool metadataReady = false;
  // A CA char or PVA byte array; longString when it holds text (decoded in text).
  bool bytes = false, longString = false;
  // CA only: the record's PROC field is connected and writable.
  bool processable = false;
  int severity = 0, status = 0, precision = -1;
  QString alarmMessage, error;
  quint64 count = 0, capacity = 0, sequence = 0;
  qint64 seconds = 0;
  int nanoseconds = 0;
  double number = std::numeric_limits<double>::quiet_NaN();
  double displayLow = 0, displayHigh = 0, controlLow = 0, controlHigh = 0;
  // LOLO, LOW, HIGH and HIHI (PVA valueAlarm limits); NaN when unavailable.
  double alarmLow = std::numeric_limits<double>::quiet_NaN(), warningLow = alarmLow, warningHigh = alarmLow, alarmHigh = alarmLow;
  QString text;
  QStringList values, choices;
  // Extra metadata. For PVA, information() adds the structure's leaf fields.
  QMap<QString, QString> fields;
  // The full pvAccess structure; shared, so copying a snapshot stays cheap.
  FieldTree structure;
  QString timestamp() const;
  QString severityText() const;
  // Display text. Native rounds floating-point values to the PV precision.
  QString valueText(const QString& format = "Native") const;
  // The value exactly as received, for copying, history and exports.
  QString exactText() const;
  // The exact readback in the form the write editor accepts.
  QString editText() const;
  bool floating() const;
  // CA reports unset alarm limits as zero; a side is set unless both are zero.
  bool lowLimitsSet() const { return std::isfinite(alarmLow) && std::isfinite(warningLow) && (alarmLow != 0 || warningLow != 0); }
  bool highLimitsSet() const { return std::isfinite(alarmHigh) && std::isfinite(warningHigh) && (alarmHigh != 0 || warningHigh != 0); }
  // Decodes byte arrays; backends call this after filling values.
  void decodeBytes();
  QMap<QString, QString> information() const;
  QJsonObject json() const;
};
// IOC time in ISO-8601 UTC with nanoseconds; empty for a zero time.
QString epicsTimestamp(qint64 seconds, int nanoseconds);
struct Sample {
  QString value;
  qint64 seconds = 0, receivedMs = 0;
  int nanoseconds = 0;
  double number = std::numeric_limits<double>::quiet_NaN();
  int severity = 0;
  bool connected = true;
  quint64 serial = 0;
  QString timestamp() const { return epicsTimestamp(seconds, nanoseconds); }
};
// Monitor events for one PV, bounded twice: by event count (--history) and
// by an estimated byte budget shared by every History in the process
// (--history-memory). Each append evicts this PV's oldest events while it
// exceeds its share, so total history memory stays near the budget no
// matter how long QtProbe runs. Read samples; change them only via append/clear.
class History {
public:
  explicit History(size_t capacity = 10000);
  ~History();
  History(const History&) = delete;
  History& operator=(const History&) = delete;
  void append(const Snapshot&);
  void clear();
  std::deque<Sample> samples;
  QString statistics() const;
  // For enums: share of time in each state while QtProbe watched (from event
  // receipt times up to nowMs, so old or skewed IOC clocks cannot distort it)
  // and the number of state changes.
  QString stateStatistics(const QStringList& choices, qint64 nowMs) const;
  QByteArray csv() const;
  // Estimated bytes held by this history and by all histories.
  size_t bytes() const { return bytes_; }
  static size_t totalBytes();
  static void setMemoryBudget(size_t bytes);
  // Longer values (long strings) are stored with a trailing ellipsis.
  static constexpr int maxValueLength = 1000;
private:
  size_t capacity_, bytes_ = 0;
  quint64 nextSerial_ = 0;
  void popFront();
};
// Writes use exact decimal strings. Never convert 64-bit integers through double.
QStringList parseWrite(const Snapshot&, const QString&, QString* error);
// Text that parseWrite reads as enum state index: its label when that selects
// it, else the index; empty when no text selects that state.
QString enumWriteText(const QStringList& choices, int index);
// Writes and record processing requested in this process, newest last.
// At most 1000 records; each text field is limited like history values.
struct WriteRecord {
  QDateTime time;
  QString pv, before, after, result;
  bool ok = false;
};
std::deque<WriteRecord>& writeLog();
void logWrite(WriteRecord);
QByteArray writeLogCsv();
// A QtStripTool configuration plotting up to ten CA snapshots.
QString stripToolConfig(const QList<Snapshot>&);
QString csvCell(QString);
// Rendering only: original sample indices, with -1 for a connection gap.
// Each screen column retains its endpoints and extrema in time order.
QVector<int> plotEnvelope(const QVector<double>&, int pixels);
} // namespace probe
