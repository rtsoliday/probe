#include "channel.h"
#include "record.h"
#include <QtTest>
#include <QJsonArray>
#include <QDateTime>
#include <cmath>
using namespace probe;
class CoreTest : public QObject {
  Q_OBJECT
private slots:
  void addresses() {
    QCOMPARE(Address::parse(" Test:PV ").uri(), QString("ca://Test:PV"));
    QCOMPARE(Address::parse("pva://Test:PV", "ca").provider, QString("pva"));
    QCOMPARE(Address::parse("ca://Test:PV", "pva").provider, QString("ca"));
    QVERIFY_EXCEPTION_THROWN(Address::parse(""), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(Address::parse("bad name"), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(Address::parse("https://x"), std::runtime_error);
  }
  void exactValuesAndFormats() {
    Snapshot s; s.hasValue = s.numeric = true; s.values << "18446744073709551615";
    s.number = s.values[0].toDouble();
    QCOMPARE(s.valueText(), s.values[0]);
    QCOMPARE(s.valueText("Hex"), QString("0xffffffffffffffff"));
    QCOMPARE(s.json()["values"].toArray()[0].toString(), s.values[0]);
    s.values[0] = "0.125"; s.number = 0.125; s.precision = 3;
    QCOMPARE(s.valueText("Fixed"), QString("0.125"));
    QCOMPARE(s.valueText("Binary"), QString("0.125"));
    s.seconds = 1700000000; s.nanoseconds = 123456789;
    QCOMPARE(s.timestamp(), QString("2023-11-14T22:13:20.123456789Z"));
  }
  void nativeDisplayUsesPrecision() {
    Snapshot s; s.hasValue = s.numeric = true; s.type = "DBF_DOUBLE"; s.precision = 3;
    s.values << "24.989000000000001"; s.number = s.values[0].toDouble();
    QCOMPARE(s.valueText(), QString("24.989")); QCOMPARE(s.exactText(), s.values[0]); QCOMPARE(s.editText(), s.values[0]);
    QCOMPARE(s.valueText("Scientific"), QString("2.499e+01"));
    s.precision = -1; QCOMPARE(s.valueText(), s.values[0]);
    s.precision = 0; s.values[0] = "9.2233720368547758e+18"; s.number = s.values[0].toDouble(); QCOMPARE(s.valueText(), QString("9223372036854775808"));
    s.precision = 2; s.values[0] = "1e+300"; s.number = 1e300; QCOMPARE(s.valueText(), QString("1e+300"));
    s.type = "double[]"; s.array = true; s.count = 3; s.values = QStringList{"1", "2.345", "3.14159"};
    QCOMPARE(s.valueText(), QString("[1.00, 2.35, 3.14]")); QCOMPARE(s.exactText(), QString("[1, 2.345, 3.14159]"));
    s = {}; s.hasValue = s.numeric = true; s.type = "DBF_LONG"; s.precision = 3; s.values << "42"; s.number = 42;
    QCOMPARE(s.valueText(), QString("42"));
  }
  void longStringsDecodeAndWrite() {
    Snapshot s; s.hasValue = s.numeric = s.array = s.bytes = true; s.provider = "ca"; s.capacity = 4; s.count = 4;
    s.values = QStringList{"104", "105", "0", "0"}; s.decodeBytes();
    QVERIFY(s.longString); QCOMPARE(s.text, QString("hi")); QCOMPARE(s.valueText(), QString("hi")); QCOMPARE(s.editText(), QString("hi"));
    QString error;
    QCOMPARE(parseWrite(s, "ok", &error), (QStringList{"111", "107", "0"})); QVERIFY(error.isEmpty());
    parseWrite(s, "four", &error); QVERIFY(error.contains("3-byte"));
    s.values = QStringList{"1", "2", "200"}; s.decodeBytes(); QVERIFY(!s.longString);
    QCOMPARE(s.valueText(), QString("[1, 2, 200]")); QCOMPARE(s.valueText("Text"), s.text);
    s.name = "rec.DESC$"; s.values = QStringList{"0", "0"}; s.decodeBytes(); QVERIFY(s.longString); QCOMPARE(s.valueText(), QString());
    s.provider = "pva"; s.type = "byte[]"; s.capacity = 0;
    QCOMPARE(parseWrite(s, "\u00e9", &error), (QStringList{"-61", "-87"}));
    s.type = "ubyte[]"; QCOMPARE(parseWrite(s, "\u00e9", &error), (QStringList{"195", "169"}));
  }
  void alarmLimitsAndWriteLog() {
    Snapshot s; s.alarmLow = s.warningLow = s.warningHigh = s.alarmHigh = 0;
    QVERIFY(!s.lowLimitsSet()); QVERIFY(!s.highLimitsSet());
    s.warningHigh = 60; s.alarmHigh = 80; QVERIFY(s.highLimitsSet()); QVERIFY(!s.lowLimitsSet());
    s.alarmLow = std::numeric_limits<double>::quiet_NaN(); s.warningLow = -60; QVERIFY(!s.lowLimitsSet());
    writeLog().clear();
    for (int i = 0; i < 1005; ++i) logWrite({QDateTime::currentDateTime(), "ca://A", QString::number(i), "x,y", "Write completed", true});
    QCOMPARE(writeLog().size(), size_t(1000)); QCOMPARE(writeLog().front().before, QString("5"));
    const auto csv = QString::fromUtf8(writeLogCsv());
    QVERIFY(csv.startsWith("time_local,pv,before,written,ok,result\n")); QVERIFY(csv.contains("\"x,y\",true"));
    writeLog().clear();
  }
  void stripToolConfiguration() {
    QList<Snapshot> list;
    for (int i = 0; i < 12; ++i) { Snapshot s; s.name = QString("Demo:%1").arg(i); s.provider = i == 1 ? "pva" : "ca"; list << s; }
    list[0].units = "mA"; list[0].displayLow = 0; list[0].displayHigh = 250; list[0].precision = 3; list[2].units = "deg C";
    const auto text = stripToolConfig(list);
    QVERIFY(text.startsWith("StripConfig                   1.2\n"));
    QVERIFY(text.contains("Strip.Curve.0.Name            Demo:0\n")); QVERIFY(text.contains("Strip.Curve.0.Units           mA\n"));
    QVERIFY(text.contains("Strip.Curve.0.Max             250\n")); QVERIFY(text.contains("Strip.Curve.0.Precision       3\n"));
    QVERIFY(!text.contains("Demo:1\n")); QVERIFY(!text.contains("deg C"));
    QVERIFY(text.contains("Strip.Curve.9.Name            Demo:10\n")); QVERIFY(!text.contains("Demo:11"));
  }
  void lastUpdateTime() {
    Channel c(Address::parse("unused")); QCOMPARE(c.lastUpdateMs, qint64(0));
    auto s = c.snapshot; s.connected = s.readable = s.hasValue = true; s.values << "1"; s.sequence = 1; c.update(s);
    QVERIFY(c.lastUpdateMs > 0); const auto first = c.lastUpdateMs;
    s.severity = 1; c.update(s); QCOMPARE(c.lastUpdateMs, first);
  }
  void writeParsing() {
    Snapshot s; QString error;
    s.choices = QStringList{"Off", "On"};
    QCOMPARE(parseWrite(s, "Off", &error), QStringList{"0"}); QVERIFY(error.isEmpty());
    QCOMPARE(parseWrite(s, "1", &error), QStringList{"1"});
    parseWrite(s, "2", &error); QVERIFY(!error.isEmpty());
    s.choices.clear(); s.array = true; s.provider = "ca"; s.capacity = 3;
    QCOMPARE(parseWrite(s, "[1,2,3]", &error).size(), 3); QVERIFY(error.isEmpty());
    QCOMPARE(parseWrite(s, "[\"18446744073709551615\"]", &error)[0], QString("18446744073709551615"));
    parseWrite(s, "[18446744073709551615]", &error); QVERIFY(!error.isEmpty());
    parseWrite(s, "[1,2,3,4]", &error); QVERIFY(!error.isEmpty());
    parseWrite(s, "[]", &error); QVERIFY(!error.isEmpty());
    parseWrite(s, "[null]", &error); QVERIFY(!error.isEmpty());
    parseWrite(s, "1,2", &error); QVERIFY(!error.isEmpty());
  }
  void boundedHistoryAndStatistics() {
    History history(3); Snapshot s; s.connected = s.readable = s.hasValue = s.numeric = true;
    for (int i = 1; i <= 4; ++i) { s.number = i; s.values = QStringList{QString::number(i)}; history.append(s); }
    QCOMPARE(history.samples.size(), size_t(3)); QCOMPARE(history.samples.front().number, 2.0);
    QVERIFY(history.statistics().contains("Mean 3"));
    s.connected = false; history.append(s);
    QVERIFY(std::isnan(history.samples.back().number));
    QVERIFY(history.csv().endsWith("false\n"));
    s.connected = true; s.hasValue = false; history.append(s);
    QVERIFY(std::isnan(history.samples.back().number));
    QCOMPARE(csvCell("one,\"two\""), QString("\"one,\"\"two\"\"\""));
    history.clear(); QVERIFY(history.samples.empty());
  }
  void historyMemoryIsBoundedAcrossPvs() {
    History::setMemoryBudget(size_t(1) << 20);
    Snapshot s; s.connected = s.readable = s.hasValue = s.numeric = true; s.seconds = 1700000000;
    {
      History a(1000000);
      for (int i = 0; i < 100000; ++i) { s.number = i; s.values = QStringList{QString::number(i)}; a.append(s); }
      QVERIFY(a.samples.size() < 100000); QVERIFY(a.bytes() <= (size_t(1) << 20)); QCOMPARE(History::totalBytes(), a.bytes());
      QCOMPARE(a.samples.back().value, QString("99999")); QCOMPARE(a.samples.back().timestamp(), QString("2023-11-14T22:13:20.000000000Z"));
      // A second PV gets half; the first trims to its share on its next event.
      History b(1000000);
      for (int i = 0; i < 100000; ++i) b.append(s);
      QVERIFY(b.bytes() <= (size_t(1) << 19)); a.append(s); QVERIFY(a.bytes() <= (size_t(1) << 19));
      QVERIFY(History::totalBytes() <= (size_t(1) << 20));
      // Long strings are stored shortened; the count limit still applies.
      Snapshot text = s; text.longString = true; text.text = QString(16384, 'x');
      History c(5); for (int i = 0; i < 10; ++i) c.append(text);
      QCOMPARE(c.samples.size(), size_t(5)); QCOMPARE(c.samples.back().value.size(), History::maxValueLength);
      QVERIFY(c.samples.back().value.endsWith(QChar(0x2026)));
    }
    QCOMPARE(History::totalBytes(), size_t(0));
    History::setMemoryBudget(size_t(256) << 20);
    writeLog().clear(); logWrite({QDateTime::currentDateTime(), "ca://A", QString(5000, 'b'), QString(5000, 'a'), "ok", true});
    QCOMPARE(writeLog().back().before.size(), History::maxValueLength); QCOMPARE(writeLog().back().after.size(), History::maxValueLength);
    writeLog().clear();
  }
  void recordLinksFieldsAndDiagnosis() {
    QCOMPARE(recordName("REC:A.VAL"), QString("REC:A")); QCOMPARE(recordName("REC:A"), QString("REC:A"));
    QCOMPARE(linkTarget("OTHER:B.VAL NPP NMS"), QString("OTHER:B.VAL")); QCOMPARE(linkTarget("  X:Y CP MS "), QString("X:Y"));
    for (const auto& constant : {"", "1.5", "-3", "@asyn(PORT,0)DATA", "#C0 S1", "\"text\"", "[1, 2]"}) QVERIFY(linkTarget(constant).isEmpty());
    QCOMPARE(linkTarget("{\"pva\":{\"pv\":\"PV:X\",\"proc\":true}}"), QString("pva://PV:X"));
    QCOMPARE(linkTarget("{ca:{pv:\"PV:Y\"}}"), QString("PV:Y"));
    auto names = [](const QVector<RecordField>& fields) { QStringList out; for (const auto& f : fields) out << f.name; return out; };
    QVERIFY(names(typeRecordFields("ai")).contains("INP")); QVERIFY(names(typeRecordFields("ai")).contains("MDEL")); QVERIFY(!names(typeRecordFields("ai")).contains("OUT"));
    QVERIFY(names(typeRecordFields("ao")).contains("DOL")); QCOMPARE(names(typeRecordFields("calc")).filter("INP").size(), 12);
    QVERIFY(names(typeRecordFields("calcout")).contains("OUT")); QCOMPARE(names(typeRecordFields("aSub")).filter("OUT").size(), 21);
    QVERIFY(names(typeRecordFields("myCustomRecord")).contains("INP")); QVERIFY(names(commonRecordFields()).contains("FLNK"));
    for (const auto& f : typeRecordFields("calcout")) if (f.name.startsWith("INP") || f.name == "OUT") QVERIFY(f.link);
    auto texts = [](const QVector<Finding>& findings, bool problems) { QStringList out; for (const auto& f : findings) if (f.problem == problems) out << f.text; return out.join('\n'); };
    QMap<QString, QString> f{{"SCAN", "1 second"}, {"DISA", "0"}, {"DISV", "1"}, {"PACT", "0"}, {"UDF", "0"}, {"STAT", "NO_ALARM"}, {"SEVR", "NO_ALARM"}, {"MDEL", "0"}};
    auto findings = recordDiagnosis(f, 1);
    QVERIFY(texts(findings, true).isEmpty()); QVERIFY(texts(findings, false).contains("No processing problems")); QVERIFY(texts(findings, false).contains("every 1 s"));
    QVERIFY(texts(recordDiagnosis(f, 30), true).contains("No new value for 30 s although SCAN is 1 second"));
    QVERIFY(texts(recordDiagnosis(f, 30), true).contains("MDEL (0)"));
    f["DISA"] = "1"; f["PACT"] = "1"; f["UDF"] = "1"; f["PINI"] = "NO"; f["STAT"] = "LINK"; f["SEVR"] = "INVALID"; f["DISP"] = "1"; f["SIMM"] = "YES";
    QVERIFY(!texts(recordDiagnosis(f, 30), true).contains("No new value"));
    const auto problems = texts(recordDiagnosis(f, -1), true);
    for (const auto& phrase : {"Disabled: DISA equals DISV", "PACT = 1", "UDF = 1", "PINI is NO", "Alarm LINK (INVALID): a link could not be read", "DISP = 1", "SIMM = YES"})
      QVERIFY2(problems.contains(phrase), phrase);
    QVERIFY(texts(recordDiagnosis({{"SCAN", "Passive"}}, -1), false).contains("Passive"));
    QCOMPARE(alarmStatusName("18"), QString("DISABLE")); QCOMPARE(alarmStatusName("LINK"), QString("LINK"));
    QVERIFY(texts(recordDiagnosis({{"STAT", "18"}, {"SEVR", "MINOR"}}, -1), true).contains("Alarm DISABLE (MINOR): the record is disabled"));
    QVERIFY(texts(recordDiagnosis({{"STAT", "18"}, {"SEVR", "NO_ALARM"}}, -1), true).isEmpty());
    QVERIFY(texts(recordDiagnosis({{"SCAN", "I/O Intr"}, {"DTYP", "asynInt32"}}, -1), false).contains("(asynInt32)"));
  }
  void enumStateStatistics() {
    History history(100); Snapshot s; s.connected = s.readable = s.hasValue = s.numeric = true; s.choices = QStringList{"Off", "On", "Standby"};
    for (int state : {0, 0, 1, 2, 1}) { s.number = state; s.values = QStringList{QString::number(state)}; history.append(s); }
    // Receipt times: Off 0–3 s, On 3–4 s, Standby 4–8 s, On 8–10 s (now).
    const qint64 times[] = {0, 1000, 3000, 4000, 8000}; int i = 0;
    for (auto& sample : const_cast<std::deque<Sample>&>(history.samples)) sample.receivedMs = 1000000 + times[i++];
    const auto text = history.stateStatistics(s.choices, 1010000);
    QVERIFY2(text.startsWith("5 events    3 changes    Time in state over 10 s: Off 30%, On 30%, Standby 40%"), qPrintable(text));
    // Disconnects are not a state and break the change count.
    s.connected = false; history.append(s); const_cast<Sample&>(history.samples.back()).receivedMs = 1010000;
    QVERIFY(history.stateStatistics(s.choices, 1020000).contains("over 10 s"));
    History empty; QCOMPARE(empty.stateStatistics(s.choices, 0), QString("0 events; no state samples"));
    History instant(10); s.connected = true; for (int state : {0, 1}) { s.number = state; instant.append(s); }
    for (auto& sample : const_cast<std::deque<Sample>&>(instant.samples)) sample.receivedMs = 5;
    QCOMPARE(instant.stateStatistics(s.choices, 5), QString("2 events    1 change"));
  }
  void readOnlyCannotWrite() {
    Channel c(Address::parse("unused"), true);
    c.snapshot.connected = c.snapshot.writable = true;
    QSignalSpy result(&c, &Channel::writeFinished); c.write("4");
    QCOMPARE(result.size(), 1); QVERIFY(!result[0][0].toBool());
    QVERIFY(!c.writePending());
  }
  void densePlotsKeepPeaksAndConnectionGaps() {
    QVector<double> values(10000, 1);
    values[123] = 100; values[124] = -50;
    const auto original = values;
    auto indices = plotEnvelope(values, 100);
    QVERIFY(indices.size() <= 600); QVERIFY(indices.contains(123)); QVERIFY(indices.contains(124));
    QCOMPARE(indices.front(), 0); QCOMPARE(indices.back(), 9999);
    int previous = -1; for (int index : indices) { QVERIFY(index > previous); previous = index; }
    QCOMPARE(values, original);
    values[5000] = std::numeric_limits<double>::quiet_NaN();
    indices = plotEnvelope(values, 100); QVERIFY(indices.contains(-1));
    // No line segment crosses the column containing the disconnected sample.
    previous = -1;
    for (int index : indices) { if (index < 0) { previous = -1; continue; } QVERIFY(!(previous >= 0 && previous < 5000 && index > 5000)); previous = index; }
    QCOMPARE(plotEnvelope(QVector<double>{1, 2, 3}, 20), (QVector<int>{0, 1, 2}));
    QVERIFY(plotEnvelope(values, 0).isEmpty());
    QVERIFY(plotEnvelope({}, 10).isEmpty());
    values.fill(std::numeric_limits<double>::quiet_NaN()); QCOMPARE(plotEnvelope(values, 100), QVector<int>{-1});
  }
  void structureTextJsonAndFlatten() {
    auto field = [](const char* name, const char* type, const char* value, FieldNode::Kind kind = FieldNode::Scalar) {
      FieldNode n; n.name = name; n.type = type; n.value = value; n.kind = kind; return n;
    };
    FieldNode root = field("", "epics:nt/NTTable:1.0", "", FieldNode::Structure);
    auto value = field("value", "structure", "", FieldNode::Structure);
    auto position = field("position", "double[]", "[0, 1.5, …]", FieldNode::Array); position.length = 5; position.omitted = 3;
    position.children << field("[0]", "double", "0") << field("[1]", "double", "1.5");
    value.children << position;
    auto items = field("items", "structure[]", "1 elements", FieldNode::StructureArray); items.length = 1;
    auto element = field("[0]", "structure", "", FieldNode::Structure); element.children << field("x", "int", "7"); items.children << element;
    auto choice = field("choice", "any", "", FieldNode::Union); choice.children << field("(any)", "string", "on");
    auto alarm = field("alarm", "alarm_t", "MAJOR · HIHI", FieldNode::Structure); alarm.children << field("severity", "int", "2");
    root.children << value << items << choice << alarm;
    const auto text = structureText(root, -1);
    QVERIFY(text.startsWith("structure value\n    double[] position [0, 1.5, …]\n        double [0] 0\n"));
    QVERIFY(text.contains("        … 3 more elements\n")); QVERIFY(text.contains("alarm_t alarm MAJOR · HIHI\n"));
    QVERIFY(structureText(root).startsWith("epics:nt/NTTable:1.0\n"));
    // Paths join members with dots and elements with brackets; arrays stay one entry.
    QMap<QString, QString> flat; flattenStructure(root, flat);
    QCOMPARE(flat.value("value.position"), QString("[0, 1.5, …]")); QVERIFY(!flat.contains("value.position[0]"));
    QCOMPARE(flat.value("items[0].x"), QString("7")); QCOMPARE(flat.value("choice.(any)"), QString("on"));
    QCOMPARE(flat.value("alarm.severity"), QString("2"));
    QMap<QString, QString> bounded; flattenStructure(root, bounded, 2); QCOMPARE(bounded.size(), 2);
    const auto json = structureJson(root);
    QCOMPARE(json["type"].toString(), QString("epics:nt/NTTable:1.0"));
    const auto first = json["fields"].toArray()[0].toObject()["fields"].toArray()[0].toObject();
    QCOMPARE(first["name"].toString(), QString("position")); QCOMPARE(first["length"].toDouble(), 5.0);
    QCOMPARE(first["omitted"].toDouble(), 3.0); QCOMPARE(first["fields"].toArray().size(), 2);
    Snapshot s; QVERIFY(!s.json().contains("structure"));
    s.structure = std::make_shared<FieldNode>(root); QCOMPARE(s.json()["structure"].toObject()["type"].toString(), QString("epics:nt/NTTable:1.0"));
    s.hasValue = true; QCOMPARE(s.valueText(), QString("Structured data — see Structure"));
  }
};
QTEST_GUILESS_MAIN(CoreTest)
#include "test_core.moc"
