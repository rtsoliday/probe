#include "record.h"
#include <QRegularExpression>
#include <cmath>
namespace probe {
namespace {
RecordField link(const QString& name, const QString& help) { return {name, "Links", help, true}; }
void links(QVector<RecordField>& out, const QString& prefix, const QString& suffixes, const QString& help) {
  for (const auto ch : suffixes) out << link(prefix + ch, help.arg(ch));
}
} // namespace
QVector<RecordField> commonRecordFields() {
  return {
    {"RTYP", "Type", "Record type", false},
    {"SCAN", "Processing", "Scan mechanism: when the record processes", false},
    {"PINI", "Processing", "Process once when the IOC starts", false},
    {"PHAS", "Processing", "Phase: order among records with the same SCAN", false},
    {"EVNT", "Processing", "Event that processes the record when SCAN is Event", false},
    {"PRIO", "Processing", "Scan priority for callback threads", false},
    {"PACT", "Processing", "Processing active: 1 while asynchronous processing is in progress", false},
    {"TPRO", "Processing", "Trace processing: print each processing on the IOC console", false},
    {"DISA", "Disable", "Disable value; the record does not process while DISA equals DISV", false},
    {"DISV", "Disable", "Value that DISA is compared with", false},
    {"DISS", "Disable", "Alarm severity raised while disabled", false},
    {"SDIS", "Disable", "Link read into DISA before each processing", true},
    {"UDF", "Alarm", "Undefined: 1 until the value is first set successfully", false},
    {"UDFS", "Alarm", "Severity used while UDF is 1", false},
    {"STAT", "Alarm", "Alarm status", false},
    {"SEVR", "Alarm", "Alarm severity", false},
    {"AMSG", "Alarm", "Alarm message (EPICS 7.0.6 and later)", false},
    {"DTYP", "Device", "Device support type", false},
    {"DISP", "Access", "Disable puts: when 1, CA and PVA writes to other fields are rejected", false},
    {"ASG", "Access", "Access security group", false},
    {"TSE", "Device", "Time stamp event: 0 uses the current time, -2 lets device support set it", false},
    {"TSEL", "Links", "Link that provides TSE", true},
    {"FLNK", "Links", "Forward link: the record processed after this one", true},
  };
}
QVector<RecordField> typeRecordFields(const QString& type) {
  static const QStringList inputs{"ai", "bi", "mbbi", "mbbiDirect", "longin", "int64in", "stringin", "lsi", "waveform", "aai", "subArray", "event", "compress"};
  static const QStringList outputs{"ao", "bo", "mbbo", "mbboDirect", "longout", "int64out", "stringout", "lso", "aao"};
  static const QStringList deadbands{"ai", "ao", "calc", "calcout", "longin", "longout", "int64in", "int64out", "sel", "sub"};
  QVector<RecordField> out;
  if (inputs.contains(type)) out << link("INP", "Input link: where the value is read from");
  if (outputs.contains(type)) {
    out << link("OUT", "Output link: where the value is written") << link("DOL", "Desired output link, read when OMSL is closed_loop")
        << RecordField{"OMSL", "Links", "Output mode: supervisory (writes) or closed_loop (reads DOL)", false};
  }
  if (inputs.contains(type) || outputs.contains(type)) {
    out << RecordField{"SIMM", "Device", "Simulation mode: NO uses the device; YES or RAW use SIOL", false}
        << link("SIML", "Link read into SIMM") << link("SIOL", "Simulation value link");
  }
  if (type == "calc" || type == "calcout" || type == "sel" || type == "sub") links(out, "INP", "ABCDEFGHIJKL", "Input %1");
  if (type == "calcout") out << link("OUT", "Output link written according to OOPT");
  if (type == "sel") out << link("NVL", "Link read into SELN (the selected input)");
  if (type == "aSub") { links(out, "INP", "ABCDEFGHIJKLMNOPQRSTU", "Input %1"); links(out, "OUT", "ABCDEFGHIJKLMNOPQRSTU", "Output %1"); out << link("SUBL", "Link providing the subroutine name"); }
  if (type == "fanout" || type == "seq") { links(out, "LNK", "0123456789ABCDEF", "Forward link %1"); out << link("SELL", "Link read into SELN"); }
  if (type == "seq") links(out, "DOL", "0123456789ABCDEF", "Value link %1");
  if (type == "dfanout") { out << link("DOL", "Desired output link") << link("SELL", "Link read into SELN"); links(out, "OUT", "ABCDEFGH", "Output %1"); }
  if (type == "printf") { links(out, "INP", "0123456789", "Argument %1"); out << link("OUT", "Output link for the formatted string"); }
  if (type == "histogram") out << link("SVL", "Signal value link");
  if (type.isEmpty() || (!inputs.contains(type) && !outputs.contains(type) && out.isEmpty() && type != "permissive" && type != "state"))
    out << link("INP", "Input link") << link("OUT", "Output link") << link("DOL", "Desired output link");
  if (deadbands.contains(type)) {
    out << RecordField{"MDEL", "Monitors", "Monitor deadband: value monitors post only for larger changes (−1 posts every processing)", false}
        << RecordField{"ADEL", "Monitors", "Archive deadband", false};
  }
  return out;
}
QString alarmStatusName(const QString& stat) {
  static const QStringList names{"NO_ALARM", "READ", "WRITE", "HIHI", "HIGH", "LOLO", "LOW", "STATE", "COS", "COMM", "TIMEOUT",
      "HWLIMIT", "CALC", "SCAN", "LINK", "SOFT", "BAD_SUB", "UDF", "DISABLE", "SIMM", "READ_ACCESS", "WRITE_ACCESS"};
  bool ok = false; const int n = stat.toInt(&ok);
  return ok && n >= 0 && n < names.size() ? names[n] : stat;
}
QString recordName(const QString& pv) { const int dot = pv.indexOf('.'); return dot < 0 ? pv : pv.left(dot); }
QString linkTarget(const QString& text) {
  const auto link = text.trimmed();
  if (link.isEmpty() || link.startsWith('@') || link.startsWith('#')) return {};
  if (link.startsWith('{')) {
    static const QRegularExpression pv("\"?pv\"?\\s*:\\s*\"([^\"]+)\"");
    const auto match = pv.match(link); if (!match.hasMatch()) return {};
    return (link.contains(QRegularExpression("^\\{\\s*\"?pva\"?\\s*:")) ? "pva://" : "") + match.captured(1);
  }
  const auto name = link.section(QRegularExpression("\\s+"), 0, 0);
  bool number = false; name.toDouble(&number);
  if (number || name.startsWith('"') || name.startsWith('[')) return {};
  return name;
}
QVector<Finding> recordDiagnosis(const QMap<QString, QString>& f, double age) {
  QVector<Finding> out;
  bool explained = false;  // disabled, stuck or simulated: no staleness hint
  auto has = [&](const char* name) { return f.contains(name) && !f.value(name).isEmpty(); };
  bool ok = false;
  if (has("DISA") && has("DISV")) {
    const double disa = f.value("DISA").toDouble(&ok), disv = f.value("DISV").toDouble();
    if (ok && disa == disv && (explained = true))
      out << Finding{QString("Disabled: DISA equals DISV (%1), so the record does not process%2.").arg(f.value("DISA"))
          .arg(linkTarget(f.value("SDIS")).isEmpty() ? QString() : "; DISA is read from SDIS " + f.value("SDIS")), true};
  }
  if (f.value("PACT") == "1" && (explained = true))
    out << Finding{"Processing is active (PACT = 1): an asynchronous operation has not finished. If it stays at 1, the device or driver is stuck.", true};
  if (f.value("UDF") == "1")
    out << Finding{QString("UDF = 1: the value has never been set successfully%1.").arg(f.value("PINI") == "NO" ? " (PINI is NO, so it was not processed at startup)" : ""), true};
  // A status without severity raises no alarm (for example DISABLE with DISS NO_ALARM).
  const auto stat = alarmStatusName(f.value("STAT"));
  if (!stat.isEmpty() && stat != "NO_ALARM" && f.value("SEVR") != "NO_ALARM") {
    static const QMap<QString, QString> hints{
      {"READ", "device support failed to read"}, {"WRITE", "device support failed to write"},
      {"HIHI", "the value is beyond an alarm limit"}, {"HIGH", "the value is beyond a warning limit"},
      {"LOW", "the value is beyond a warning limit"}, {"LOLO", "the value is beyond an alarm limit"},
      {"STATE", "the value is in an alarm state"}, {"COS", "the value changed state"},
      {"COMM", "communication with the device failed"}, {"TIMEOUT", "the device timed out"},
      {"HWLIMIT", "a hardware limit was reached"}, {"HW_LIMIT", "a hardware limit was reached"},
      {"CALC", "the expression could not be evaluated"}, {"SCAN", "the scan could not be set up"},
      {"LINK", "a link could not be read or written; check the links below"},
      {"SOFT", "device or record support raised it"}, {"BAD_SUB", "the subroutine was not found"},
      {"UDF", "the value is undefined"}, {"DISABLE", "the record is disabled"}, {"SIMM", "the record is in simulation mode"},
      {"READ_ACCESS", "read access was denied"}, {"WRITE_ACCESS", "write access was denied"}};
    out << Finding{QString("Alarm %1 (%2): %3%4.").arg(stat, f.value("SEVR"), hints.value(stat, "see the record documentation"))
        .arg(f.value("AMSG").isEmpty() ? QString() : " — " + f.value("AMSG")), true};
  }
  if (f.value("DISP") == "1") out << Finding{"DISP = 1: CA and PVA writes to this record are rejected (except to DISP).", true};
  const auto simm = f.value("SIMM");
  if (!simm.isEmpty() && simm != "NO" && simm != "0" && (explained = true)) out << Finding{QString("Simulation mode (SIMM = %1): values come from SIOL, not the device.").arg(simm), true};
  const auto scan = f.value("SCAN");
  static const QRegularExpression periodic("^\\s*([0-9]*\\.?[0-9]+)\\s*second");
  const auto period = periodic.match(scan);
  if (scan == "Passive") out << Finding{"SCAN Passive: it processes only when written, triggered by a forward link or a CP/PP link, or with Process.", false};
  else if (scan == "I/O Intr") out << Finding{QString("SCAN I/O Intr: it processes when device support%1 reports new data.").arg(f.value("DTYP").isEmpty() ? QString() : " (" + f.value("DTYP") + ")"), false};
  else if (scan == "Event") out << Finding{QString("SCAN Event: it processes when event %1 is posted.").arg(f.value("EVNT").isEmpty() ? "EVNT" : f.value("EVNT")), false};
  else if (period.hasMatch()) {
    const double seconds = period.captured(1).toDouble();
    out << Finding{QString("SCAN %1: it processes every %2 s.").arg(scan).arg(seconds), false};
    if (!explained && age >= 0 && age > 3 * seconds + 2)
      out << Finding{QString("No new value for %1 s although SCAN is %2. Monitors post only when the value changes%3, so a steady value looks stalled.")
          .arg(std::round(age)).arg(scan).arg(f.value("MDEL").isEmpty() ? QString() : " by more than MDEL (" + f.value("MDEL") + ")"), true};
  }
  if (!f.value("TPRO").isEmpty() && f.value("TPRO") != "0") out << Finding{"TPRO is on: the IOC console prints each processing.", false};
  bool problem = false; for (const auto& finding : out) problem |= finding.problem;
  if (!problem && has("SCAN")) out.prepend(Finding{"No processing problems found in these fields.", false});
  return out;
}
} // namespace probe
