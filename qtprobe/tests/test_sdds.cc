#include "sdds_export.h"
#include "inspector.h"
#include "test_compat.h"
#include <QtWidgets>
#include <QTemporaryDir>
#include <cstdlib>
#include <stdexcept>
#ifdef QTPROBE_HAVE_SDDS
#include <SDDS.h>
#endif
using namespace probe;

#ifdef QTPROBE_HAVE_SDDS
class Reader {
public:
  SDDS_DATASET data{};
  explicit Reader(const QString& path) {
    auto name = QFile::encodeName(path);
    if (!SDDS_InitializeInput(&data, name.data())) throw std::runtime_error("Opening exported SDDS file failed");
    if (SDDS_ReadPage(&data) <= 0) { SDDS_Terminate(&data); throw std::runtime_error("Reading exported SDDS page failed"); }
  }
  ~Reader() { SDDS_Terminate(&data); }
  int rows() { return int(SDDS_CountRowsOfInterest(&data)); }
  int type(const char* name) {
    QByteArray n(name); const int index = SDDS_GetColumnIndex(&data, n.data());
    if (index < 0) throw std::runtime_error("Missing column");
    return data.layout.column_definition[index].type;
  }
  QString parameter(const char* name) {
    QByteArray n(name); char* value = SDDS_GetParameterAsString(&data, n.data(), nullptr);
    if (!value) throw std::runtime_error("Missing parameter");
    const auto text = QString::fromUtf8(value); std::free(value); return text;
  }
  template<class T> QVector<T> column(const char* name) {
    QByteArray n(name); auto* values = static_cast<T*>(SDDS_GetColumn(&data, n.data()));
    if (!values) throw std::runtime_error("Missing numeric column");
    QVector<T> result; for (int i = 0; i < rows(); ++i) result << values[i];
    std::free(values); return result;
  }
  QStringList strings(const char* name) {
    QByteArray n(name); auto** values = static_cast<char**>(SDDS_GetColumn(&data, n.data()));
    if (!values) throw std::runtime_error("Missing string column");
    QStringList result;
    for (int i = 0; i < rows(); ++i) { result << QString::fromUtf8(values[i]); std::free(values[i]); }
    std::free(values); return result;
  }
  QStringList array(const char* name) {
    QByteArray n(name); auto* value = SDDS_GetArray(&data, n.data(), nullptr);
    if (!value) throw std::runtime_error("Missing metadata array");
    QStringList result; auto** strings = static_cast<char**>(value->data);
    for (int i = 0; i < value->elements; ++i) result << QString::fromUtf8(strings[i]);
    SDDS_FreeArray(value); return result;
  }
};
#endif

class SddsTest : public QObject {
  Q_OBJECT
  QTemporaryDir directory;
  QString path(const QString& name = "data.sdds") const { return directory.filePath(name); }
  static Snapshot scalar() {
    Snapshot s; s.name = "TEST:PV"; s.provider = "pva"; s.type = "double";
    s.connected = s.readable = s.writable = s.hasValue = s.numeric = true;
    s.values = QStringList{"1.2345678901234567"}; s.number = s.values[0].toDouble();
    s.units = "mA"; s.description = "Beam current"; s.host = "test-ioc";
    s.seconds = 1700000000; s.nanoseconds = 123456789;
    s.count = s.capacity = 1; s.status = 5; s.severity = 2;
    s.displayLow = -10; s.displayHigh = 100; s.alarmHigh = 90;
    s.fields["RTYP"] = "ai";
    return s;
  }
private slots:
  void initTestCase() {
    QVERIFY(directory.isValid());
    QCoreApplication::setOrganizationName("QtProbeSddsTests");
    QCoreApplication::setApplicationName(QFileInfo(directory.path()).fileName());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
  }
  void availabilityAndMenus() {
    Options options; options.connectOnOpen = false;
    Window window(options); window.show();
    const char* names[] = {"sddsHistoryAction", "sddsArrayAction", "sddsSnapshotAction"};
    auto* menu = window.findChild<QToolButton*>("exportButton")->menu();
    for (const char* name : names) {
      auto* action = window.findChild<QAction*>(name);
      QCOMPARE(action != nullptr, sddsAvailable());
      if (action) { QVERIFY(!action->isEnabled()); QVERIFY(menu->actions().contains(action)); }
    }
    History history;
    if (!sddsAvailable()) {
      QVERIFY_THROWS_EXCEPTION(std::runtime_error, saveSdds(path(), SddsExportKind::Snapshot, scalar(), history));
      QVERIFY(!QFileInfo::exists(path()));
      return;
    }
    window.addPv("pva://TEST:PV");
    auto* inspector = static_cast<Inspector*>(window.inspectors()->widget(0));
    auto s = scalar(); inspector->channel()->update(s);
    auto* historyAction = window.findChild<QAction*>(names[0]);
    auto* arrayAction = window.findChild<QAction*>(names[1]);
    QTRY_VERIFY(historyAction->isEnabled()); QVERIFY(!arrayAction->isEnabled());
    QVERIFY(window.findChild<QAction*>(names[2])->isEnabled());
    s.array = true; s.type = "double[]"; inspector->channel()->update(s);
    QTRY_VERIFY(arrayAction->isEnabled()); QVERIFY(!historyAction->isEnabled());
    window.closePv(0);
    QVERIFY(!historyAction->isEnabled() && !arrayAction->isEnabled());
  }
  void scalarHistory() {
#ifndef QTPROBE_HAVE_SDDS
    QSKIP("SDDS support is not compiled in");
#else
    auto s = scalar(); History history; history.append(s);
    s.seconds = -1; s.nanoseconds = 999999999; s.number = 2.5; s.values = QStringList{"2.5"}; history.append(s);
    s.connected = false; history.append(s);
    saveSdds(path(), SddsExportKind::History, s, history);
    Reader reader(path()); QCOMPARE(reader.data.layout.data_mode.mode, int32_t{SDDS_BINARY});
    QCOMPARE(reader.rows(), 3); QCOMPARE(reader.parameter("PV"), s.name);
    QCOMPARE(reader.parameter("Units"), s.units);
    QCOMPARE(reader.column<int32_t>("TimeNanoseconds"), (QVector<int32_t>{123456789, 999999999, 999999999}));
    QCOMPARE(reader.strings("TimeSeconds"), (QStringList{"1700000000", "-1", "-1"}));
    const auto values = reader.column<double>("Value");
    QVERIFY(values[0] == scalar().number); QCOMPARE(values[1], 2.5); QVERIFY(std::isnan(values[2]));
    QCOMPARE(reader.strings("ValueText"), (QStringList{scalar().values[0], "2.5", "2.5"}));
    QCOMPARE(reader.column<int32_t>("Severity"), (QVector<int32_t>{2, 2, 2}));
    QCOMPARE(reader.column<int32_t>("Connected"), (QVector<int32_t>{1, 1, 0}));
    QCOMPARE(reader.strings("ReceivedMilliseconds")[0], QString::number(history.samples[0].receivedMs));
    QCOMPARE(SDDS_ReadPage(&reader.data), int32_t{-1});
#endif
  }
  void arrayIncludesEveryRetainedElement() {
#ifndef QTPROBE_HAVE_SDDS
    QSKIP("SDDS support is not compiled in");
#else
    auto s = scalar(); s.array = true; s.type = "double[]"; s.count = 4000; s.capacity = 5000; s.truncated = true;
    s.values.clear(); for (int i = 0; i < 2100; ++i) s.values << QString::number(i * 0.25, 'g', 17);
    History history; saveSdds(path(), SddsExportKind::Array, s, history);
    Reader reader(path()); QCOMPARE(reader.rows(), 2100);
    QCOMPARE(reader.parameter("Count"), QString("4000")); QCOMPARE(reader.parameter("RetainedCount"), QString("2100"));
    QCOMPARE(reader.parameter("Truncated"), QString("1"));
    QCOMPARE(reader.column<int32_t>("Index").last(), int32_t{2099});
    QCOMPARE(reader.column<double>("Value").last(), 524.75);
    QCOMPARE(reader.strings("ValueText"), s.values);
#endif
  }
  void snapshotMetadataAndEnums() {
#ifndef QTPROBE_HAVE_SDDS
    QSKIP("SDDS support is not compiled in");
#else
    auto s = scalar(); s.type = "enum"; s.choices = QStringList{"Off", "On"}; s.values = QStringList{"1"};
    s.description = "Beam \"state\", \u00e9";
    History history; saveSdds(path(), SddsExportKind::Snapshot, s, history);
    Reader reader(path()); QCOMPARE(reader.rows(), 1);
    QCOMPARE(reader.parameter("TimeSeconds"), QString::number(s.seconds));
    QCOMPARE(reader.parameter("TimeNanoseconds"), QString::number(s.nanoseconds));
    QCOMPARE(reader.parameter("Description"), s.description); QCOMPARE(reader.parameter("Host"), s.host);
    QCOMPARE(reader.parameter("Status"), QString("5")); QCOMPARE(reader.parameter("Severity"), QString("2"));
    QCOMPARE(reader.type("Value"), SDDS_LONG); QCOMPARE(reader.column<int32_t>("Value")[0], int32_t{1});
    QCOMPARE(reader.strings("State"), (QStringList{"On"})); QCOMPARE(reader.array("EnumChoices"), s.choices);
    const auto keys = reader.array("MetadataNames"), values = reader.array("MetadataValues");
    QCOMPARE(keys.size(), values.size()); QVERIFY(keys.contains("RTYP")); QCOMPARE(values[keys.indexOf("RTYP")], QString("ai"));
#endif
  }
  void exactIntegerAndStringValues_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<QStringList>("values"); QTest::addColumn<bool>("numeric");
    QTest::newRow("int64") << QString("long[]") << QStringList{"-9223372036854775808", "9007199254740993", "9223372036854775807"} << true;
    QTest::newRow("uint64") << QString("ulong[]") << QStringList{"0", "18446744073709551615"} << true;
    QTest::newRow("uint32") << QString("uint[]") << QStringList{"0", "4294967295"} << true;
    QTest::newRow("strings") << QString("string[]") << QStringList{"001", "hello, \"world\"\n\u00e9", ""} << false;
    QTest::newRow("boolean") << QString("boolean[]") << QStringList{"true", "false"} << true;
  }
  void exactIntegerAndStringValues() {
#ifndef QTPROBE_HAVE_SDDS
    QSKIP("SDDS support is not compiled in");
#else
    QFETCH(QString, type); QFETCH(QStringList, values); QFETCH(bool, numeric);
    auto s = scalar(); s.array = true; s.type = type; s.values = values; s.numeric = numeric;
    s.count = s.capacity = values.size(); History history;
    saveSdds(path(), SddsExportKind::Array, s, history); Reader reader(path());
    QCOMPARE(reader.strings("ValueText"), values);
    if (type == "long[]") {
#ifdef SDDS_LONG64
      QCOMPARE(reader.type("Value"), SDDS_LONG64);
      const auto actual = reader.column<int64_t>("Value");
      for (int i = 0; i < actual.size(); ++i) QCOMPARE(QString::number(qlonglong(actual[i])), values[i]);
#else
      QCOMPARE(reader.strings("Value"), values);
#endif
    } else if (type == "ulong[]") {
#ifdef SDDS_ULONG64
      QCOMPARE(reader.type("Value"), SDDS_ULONG64);
      const auto actual = reader.column<uint64_t>("Value");
      for (int i = 0; i < actual.size(); ++i) QCOMPARE(QString::number(qulonglong(actual[i])), values[i]);
#else
      QCOMPARE(reader.strings("Value"), values);
#endif
    } else if (type == "uint[]") QCOMPARE(reader.column<uint32_t>("Value").last(), uint32_t{4294967295U});
    else if (type == "boolean[]") QCOMPARE(reader.column<int32_t>("Value"), (QVector<int32_t>{1, 0}));
    else QCOMPARE(reader.strings("Value"), values);
#endif
  }
  void emptyDataAndSafeFailures() {
#ifndef QTPROBE_HAVE_SDDS
    QSKIP("SDDS support is not compiled in");
#else
    History history; auto s = scalar();
    saveSdds(path(), SddsExportKind::History, s, history); { Reader reader(path()); QCOMPARE(reader.rows(), 0); }
    s.values.clear(); s.hasValue = false;
    saveSdds(path(), SddsExportKind::Snapshot, s, history); { Reader reader(path()); QCOMPARE(reader.rows(), 0); QCOMPARE(reader.parameter("HasValue"), QString("0")); }
    s.array = s.hasValue = true;
    saveSdds(path(), SddsExportKind::Array, s, history); { Reader reader(path()); QCOMPARE(reader.rows(), 0); }
    QFile existing(path()); QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("preserved"); existing.close();
    s.type = "double[]"; s.values = QStringList{"bad number"};
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, saveSdds(path(), SddsExportKind::Array, s, history));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, saveSdds(path(), SddsExportKind::History, s, history));
    s = scalar(); QVERIFY_THROWS_EXCEPTION(std::runtime_error, saveSdds(path(), SddsExportKind::Array, s, history));
    QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(), QByteArray("preserved")); existing.close();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, saveSdds(path("missing/data.sdds"), SddsExportKind::Snapshot, s, history));
    const auto unicode = path("\u6e2c\u5b9a-\u00e9.sdds"); saveSdds(unicode, SddsExportKind::Snapshot, s, history);
    QVERIFY(QFile::copy(unicode, path("unicode-copy.sdds"))); Reader reader(path("unicode-copy.sdds")); QCOMPARE(reader.rows(), 1);
#endif
  }
};
QTEST_MAIN(SddsTest)
#include "test_sdds.moc"
