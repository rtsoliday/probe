// Optional SDDS exports. Distributed under the license in ../LICENSE.
#include "sdds_export.h"
#include <stdexcept>
#ifdef QTPROBE_HAVE_SDDS
#include <SDDS.h>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <variant>
#include <vector>
#include <type_traits>
#endif

namespace probe {
bool sddsAvailable() {
#ifdef QTPROBE_HAVE_SDDS
  return true;
#else
  return false;
#endif
}

#ifdef QTPROBE_HAVE_SDDS
namespace {
#ifdef SDDS_LONG64
constexpr int signed64Type = SDDS_LONG64;
#else
constexpr int signed64Type = SDDS_STRING;
#endif
#ifdef SDDS_ULONG64
constexpr int unsigned64Type = SDDS_ULONG64;
#else
constexpr int unsigned64Type = SDDS_STRING;
#endif

QByteArray utf8(const QString& text) {
  if (text.contains(QChar(0))) throw std::runtime_error("SDDS strings cannot contain NUL characters");
  return text.toUtf8();
}
[[noreturn]] void failed(const QString& operation) {
  int32_t count = 0;
  char** errors = SDDS_GetErrorMessages(&count, SDDS_ALL_GetErrorMessages);
  QString message = operation;
  if (errors) {
    for (int32_t i = 0; i < count; ++i) {
      message += ": " + QString::fromUtf8(errors[i]); std::free(errors[i]);
    }
    std::free(errors);
  }
  SDDS_ClearErrors();
  throw std::runtime_error(message.toStdString());
}
using Strings = std::vector<QByteArray>;
using Data = std::variant<std::vector<double>, std::vector<int32_t>, std::vector<uint32_t>,
                          std::vector<int64_t>, std::vector<uint64_t>, Strings>;
struct Column {
  QByteArray name, units;
  int type;
  Data values;
};
struct Output {
  SDDS_DATASET data{};
  bool initialized = false;
  std::vector<Column> columns;
  std::vector<std::pair<int32_t, QByteArray>> textParameters;
  // SDDS_Terminate ignores fclose's result, so detach and close the ordinary
  // binary stream ourselves before asking SDDS to free its layout/page data.
  bool close() {
    bool ok = true;
    if (data.layout.fp) { ok = std::fclose(data.layout.fp) == 0; data.layout.fp = nullptr; }
    if (initialized) { initialized = false; if (!SDDS_Terminate(&data)) ok = false; }
    return ok;
  }
  ~Output() { close(); }
  void parameter(const char* name, int type, const QString& value, const char* units = "") {
    // Owned buffers also support older SDDS headers with writable arguments.
    QByteArray n(name), u(units), text = utf8(value);
    const auto index = SDDS_DefineParameter(&data, n.data(), nullptr, u.data(), nullptr, nullptr,
                                           type, type == SDDS_STRING ? nullptr : text.data());
    if (index < 0) failed("Defining SDDS parameter " + QString::fromLatin1(name));
    // Binary string parameters preserve quotes, Unicode and empty text without
    // depending on the older readers' namelist escape handling.
    if (type == SDDS_STRING) textParameters.emplace_back(index, std::move(text));
  }
  void text(const char* name, const QString& value) { parameter(name, SDDS_STRING, value); }
  void integer(const char* name, int value) { parameter(name, SDDS_LONG, QString::number(value)); }
  void real(const char* name, double value, const char* units = "") {
    parameter(name, SDDS_DOUBLE, QString::number(value, 'g', 17), units);
  }
  void stringArrayDefinition(const char* name) {
    QByteArray n(name);
    if (SDDS_DefineArray(&data, n.data(), nullptr, nullptr, nullptr, nullptr,
                         SDDS_STRING, 0, 1, nullptr) < 0) failed("Defining SDDS metadata array");
  }
  void stringArray(const char* name, const QStringList& values) {
    QByteArray n(name);
    Strings bytes; for (const auto& value : values) bytes.push_back(utf8(value));
    std::vector<char*> pointers; for (auto& value : bytes) pointers.push_back(value.data());
    int32_t count = static_cast<int32_t>(values.size());
    if (!SDDS_SetArray(&data, n.data(), SDDS_CONTIGUOUS_DATA, pointers.data(), &count))
      failed("Writing SDDS metadata array");
  }
  template<class T> void column(const char* name, int type, std::vector<T> values, const char* units = "") {
    columns.push_back({QByteArray(name), QByteArray(units), type, std::move(values)});
  }
  void stringColumn(const char* name, const QStringList& values) {
    Strings bytes; for (const auto& value : values) bytes.push_back(utf8(value));
    column(name, SDDS_STRING, std::move(bytes));
  }
  void begin(int32_t rows) {
    for (auto& column : columns)
      if (SDDS_DefineColumn(&data, column.name.data(), nullptr, column.units.data(),
                            nullptr, nullptr, column.type, 0) < 0) failed("Defining SDDS data column");
    if (!SDDS_WriteLayout(&data) || !SDDS_StartPage(&data, rows)) failed("Writing SDDS layout");
    for (auto& parameter : textParameters)
      if (!SDDS_SetParameters(&data, SDDS_SET_BY_INDEX | SDDS_PASS_BY_VALUE,
                               parameter.first, parameter.second.data(), int32_t{-1})) failed("Writing SDDS parameter");
    if (!rows) return; // Older libraries reject zero-length SetColumn calls.
    int32_t index = 0;
    for (auto& column : columns) {
      std::visit([&](auto& values) {
        using T = std::decay_t<decltype(values)>;
        if constexpr (std::is_same_v<T, Strings>) {
          std::vector<char*> pointers; for (auto& value : values) pointers.push_back(value.data());
          if (!SDDS_SetColumn(&data, SDDS_SET_BY_INDEX, pointers.data(), rows, index)) failed("Writing SDDS string column");
        } else if (!SDDS_SetColumn(&data, SDDS_SET_BY_INDEX, values.data(), rows, index)) failed("Writing SDDS numeric column");
      }, column.values);
      ++index;
    }
  }
};

void metadata(Output& out, const Snapshot& s, SddsExportKind kind) {
  out.text("ExportKind", kind == SddsExportKind::History ? "History" : kind == SddsExportKind::Array ? "Array" : "Snapshot");
  out.text("PV", s.name); out.text("Provider", s.provider); out.text("Host", s.host);
  out.text("NativeType", s.type); out.text("StructureID", s.structureId);
  out.text("Units", s.units); out.text("Description", s.description);
  out.text("AlarmMessage", s.alarmMessage); out.text("Diagnostic", s.error);
  out.real("Time", double(s.seconds) + s.nanoseconds * 1e-9, "seconds");
  out.parameter("TimeSeconds", signed64Type, QString::number(s.seconds), "seconds");
  out.integer("TimeNanoseconds", s.nanoseconds);
  out.integer("Connected", s.connected); out.integer("Readable", s.readable); out.integer("Writable", s.writable);
  out.integer("HasValue", s.hasValue); out.integer("IsArray", s.array); out.integer("Truncated", s.truncated);
  out.integer("Severity", s.severity); out.integer("Status", s.status); out.integer("Precision", s.precision);
  out.parameter("Count", unsigned64Type, QString::number(s.count));
  out.parameter("Capacity", unsigned64Type, QString::number(s.capacity));
  out.parameter("RetainedCount", unsigned64Type, QString::number(s.values.size()));
  out.real("DisplayLow", s.displayLow); out.real("DisplayHigh", s.displayHigh);
  out.real("ControlLow", s.controlLow); out.real("ControlHigh", s.controlHigh);
  out.real("AlarmLow", s.alarmLow); out.real("WarningLow", s.warningLow);
  out.real("WarningHigh", s.warningHigh); out.real("AlarmHigh", s.alarmHigh);
  if (s.longString) out.text("Text", s.text);
  out.stringArrayDefinition("MetadataNames"); out.stringArrayDefinition("MetadataValues");
  if (!s.choices.isEmpty()) out.stringArrayDefinition("EnumChoices");
}

void values(Output& out, const Snapshot& s, const QStringList& text) {
  QString type = s.type.toLower(); type.remove("[]");
  const bool boolean = type == "boolean" || type == "bool";
  const bool signed64 = type == "long" || type == "int64" || type == "int64_t" || type == "dbf_int64";
  const bool unsigned64 = type == "ulong" || type == "uint64" || type == "uint64_t" || type == "dbf_uint64";
  const bool unsigned32 = type == "uint" || type == "uint32" || type == "dbf_ulong";
  const bool integer = boolean || !s.choices.isEmpty() || type == "enum" || type == "dbf_enum" ||
      type == "byte" || type == "ubyte" || type == "short" || type == "ushort" || type == "int" ||
      type == "int32" || type == "dbf_char" || type == "dbf_short" || type == "dbf_long";
  auto conversionFailed = [] { throw std::runtime_error("A retained value cannot be represented in its native SDDS type"); };
  if (!s.numeric || (signed64 && signed64Type == SDDS_STRING) || (unsigned64 && unsigned64Type == SDDS_STRING)) {
    out.stringColumn("Value", text);
  } else if (signed64) {
    std::vector<int64_t> data;
    for (const auto& value : text) { bool ok; auto n = value.toLongLong(&ok); if (!ok) conversionFailed(); data.push_back(n); }
    out.column("Value", signed64Type, std::move(data));
  } else if (unsigned64) {
    std::vector<uint64_t> data;
    for (const auto& value : text) { bool ok; auto n = value.toULongLong(&ok); if (!ok || value.startsWith('-')) conversionFailed(); data.push_back(n); }
    out.column("Value", unsigned64Type, std::move(data));
  } else if (unsigned32) {
    std::vector<uint32_t> data;
    for (const auto& value : text) { bool ok; auto n = value.toUInt(&ok); if (!ok || value.startsWith('-')) conversionFailed(); data.push_back(n); }
    out.column("Value", SDDS_ULONG, std::move(data));
  } else if (integer) {
    std::vector<int32_t> data;
    for (const auto& value : text) {
      bool ok = true;
      auto n = boolean && (value == "true" || value == "false") ? int(value == "true") : value.toInt(&ok);
      if (!ok) conversionFailed();
      data.push_back(n);
    }
    out.column("Value", SDDS_LONG, std::move(data));
  } else {
    std::vector<double> data;
    for (const auto& value : text) { bool ok; double n = value.toDouble(&ok); if (!ok) conversionFailed(); data.push_back(n); }
    const auto units = utf8(s.units); out.column("Value", SDDS_DOUBLE, std::move(data), units.constData());
  }
  // The original decimal/text representation also remains available for every
  // type, including large integers and text that looks numeric.
  out.columns.back().units = utf8(s.units);
  out.stringColumn("ValueText", text);
  if (!s.choices.isEmpty()) {
    QStringList states;
    for (const auto& value : text) {
      bool ok; const int index = value.toInt(&ok);
      states << (ok ? s.choices.value(index) : QString());
    }
    out.stringColumn("State", states);
  }
}

int32_t historyColumns(Output& out, const History& history, const QString& units) {
  std::vector<double> times, received, numbers;
  std::vector<int32_t> nanos, severity, connected;
  QStringList seconds, milliseconds, text;
  for (const auto& sample : history.samples) {
    times.push_back(double(sample.seconds) + sample.nanoseconds * 1e-9);
    seconds << QString::number(sample.seconds); nanos.push_back(sample.nanoseconds);
    received.push_back(double(sample.receivedMs) / 1000); milliseconds << QString::number(sample.receivedMs);
    numbers.push_back(sample.connected ? sample.number : std::numeric_limits<double>::quiet_NaN());
    text << sample.value; severity.push_back(sample.severity); connected.push_back(sample.connected);
  }
  out.column("Time", SDDS_DOUBLE, std::move(times), "seconds");
  // Strings keep exact timestamps even with SDDS versions predating int64.
  out.stringColumn("TimeSeconds", seconds); out.column("TimeNanoseconds", SDDS_LONG, std::move(nanos));
  out.column("ReceivedTime", SDDS_DOUBLE, std::move(received), "seconds");
  out.stringColumn("ReceivedMilliseconds", milliseconds);
  const auto valueUnits = utf8(units);
  out.column("Value", SDDS_DOUBLE, std::move(numbers), valueUnits.constData()); out.stringColumn("ValueText", text);
  out.column("Severity", SDDS_LONG, std::move(severity)); out.column("Connected", SDDS_LONG, std::move(connected));
  return static_cast<int32_t>(history.samples.size());
}
} // namespace
#endif

void saveSdds(const QString& path, SddsExportKind kind, const Snapshot& snapshot, const History& history) {
#ifndef QTPROBE_HAVE_SDDS
  (void)path; (void)kind; (void)snapshot; (void)history;
  throw std::runtime_error("SDDS export is unavailable in this build");
#else
  SDDS_ClearErrors();
  try {
    if (kind == SddsExportKind::Array && (!snapshot.array || !snapshot.hasValue))
      throw std::runtime_error("No current array value is available to export");
    if (kind == SddsExportKind::History && snapshot.array)
      throw std::runtime_error("Scalar history export is unavailable for an array PV; export its current array instead");
    QTemporaryFile temporary;
    if (!temporary.open()) throw std::runtime_error(temporary.errorString().toStdString());
    temporary.close();
#ifdef _WIN32
    auto filename = temporary.fileName().toLocal8Bit();
#else
    auto filename = QFile::encodeName(temporary.fileName());
#endif
    Output out;
    out.initialized = true;
    char description[] = "QtProbe data", contents[] = "PV export";
    if (!SDDS_InitializeOutput(&out.data, SDDS_BINARY, 1, description, contents, filename.data())) failed("Initializing SDDS output");
    metadata(out, snapshot, kind);
    int32_t rows;
    if (kind == SddsExportKind::History) rows = historyColumns(out, history, snapshot.units);
    else {
      const auto retained = snapshot.hasValue ? snapshot.values : QStringList{};
      rows = static_cast<int32_t>(retained.size());
      std::vector<int32_t> indices; for (int32_t i = 0; i < rows; ++i) indices.push_back(i);
      out.column("Index", SDDS_LONG, std::move(indices)); values(out, snapshot, retained);
    }
    out.begin(rows);
    const auto info = snapshot.information();
    out.stringArray("MetadataNames", info.keys()); out.stringArray("MetadataValues", info.values());
    if (!snapshot.choices.isEmpty()) out.stringArray("EnumChoices", snapshot.choices);
    if (!SDDS_WritePage(&out.data)) failed("Writing SDDS page");
    if (!out.close()) failed("Closing SDDS output");
    QSaveFile destination(path);
    if (!temporary.open() || !destination.open(QIODevice::WriteOnly))
      throw std::runtime_error((!temporary.isOpen() ? temporary.errorString() : destination.errorString()).toStdString());
    while (!temporary.atEnd()) {
      const auto block = temporary.read(64 * 1024);
      if (block.isEmpty() || destination.write(block) != block.size())
        throw std::runtime_error((temporary.error() != QFile::NoError ? temporary.errorString() : destination.errorString()).toStdString());
    }
    if (!destination.commit()) throw std::runtime_error(destination.errorString().toStdString());
  } catch (const std::exception& e) {
    SDDS_ClearErrors();
    throw std::runtime_error(("Cannot save " + path + ": " + QString::fromUtf8(e.what())).toStdString());
  }
#endif
}
} // namespace probe
