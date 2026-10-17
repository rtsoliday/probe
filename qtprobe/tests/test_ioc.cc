#include "inspector.h"
#include "record_explorer.h"
#include <QtTest>
#include <QtWidgets>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTcpServer>
#include <QUdpSocket>
#include <clocale>
#include <cmath>
#include <string>
#include <vector>
using namespace probe;
class IocTest : public QObject {
  Q_OBJECT
  QProcess ioc, repeater;
  QString prefix, artifacts;
  QTemporaryDir temp;
  QStringList providers() const {
    QStringList p{"ca"};
#if QTPROBE_HAVE_PVA
    p << "pva";
#endif
    return p;
  }
  void launch() {
    const QString executable = QString(EPICS_TEST_BIN) +
#if QTPROBE_HAVE_PVA
        "/softIocPVA";
#else
        "/softIoc";
#endif
    ioc.setProcessChannelMode(QProcess::MergedChannels);
    ioc.start(executable, {"-m", "P=" + prefix, "-a", QFINDTESTDATA("fixture.acf"), "-d", QFINDTESTDATA("fixture.db")});
    QVERIFY2(ioc.waitForStarted(5000), qPrintable(ioc.errorString()));
    QTest::qWait(300);
    QVERIFY2(ioc.state() == QProcess::Running, ioc.readAll().constData());
  }
  void saveLog() {
    QFile file(artifacts + "/ioc.log"); if (file.open(QIODevice::Append)) file.write(ioc.readAll());
  }
private slots:
  void initTestCase() {
    prefix = "QtProbeTest:" + QString::number(QCoreApplication::applicationPid()) + ':';
    artifacts = QCoreApplication::applicationDirPath() + "/test-artifacts"; QDir().mkpath(artifacts);
    int base = 20000 + int(QCoreApplication::applicationPid() % 8000) * 4;
    bool available = false;
    for (int attempt = 0; attempt < 100; ++attempt, base += 4) {
      std::vector<std::unique_ptr<QTcpServer>> tcp;
      std::vector<std::unique_ptr<QUdpSocket>> udp;
      available = true;
      for (int n = 0; n < 4; ++n) {
        auto t = std::make_unique<QTcpServer>(); auto u = std::make_unique<QUdpSocket>();
        if (!t->listen(QHostAddress::LocalHost, quint16(base + n)) || !u->bind(QHostAddress::LocalHost, quint16(base + n))) { available = false; break; }
        tcp.push_back(std::move(t)); udp.push_back(std::move(u));
      }
      if (available) break;
    }
    QVERIFY2(available, "Cannot reserve isolated loopback ports");
    qputenv("EPICS_CA_AUTO_ADDR_LIST", "NO"); qputenv("EPICS_CA_ADDR_LIST", ("127.0.0.1:" + QString::number(base)).toLatin1());
    qputenv("EPICS_CA_SERVER_PORT", QByteArray::number(base)); qputenv("EPICS_CA_REPEATER_PORT", QByteArray::number(base + 1));
    qputenv("EPICS_CAS_INTF_ADDR_LIST", "127.0.0.1"); qputenv("EPICS_CAS_BEACON_ADDR_LIST", ("127.0.0.1:" + QString::number(base + 1)).toLatin1());
    qputenv("EPICS_PVA_AUTO_ADDR_LIST", "NO"); qputenv("EPICS_PVA_ADDR_LIST", "127.0.0.1");
    qputenv("EPICS_PVA_SERVER_PORT", QByteArray::number(base + 2)); qputenv("EPICS_PVA_BROADCAST_PORT", QByteArray::number(base + 3));
    qputenv("EPICS_PVAS_INTF_ADDR_LIST", "127.0.0.1");
    repeater.start(QString(EPICS_TEST_BIN) + "/caRepeater", {"-v"});
    QVERIFY(repeater.waitForStarted(5000));
    launch();
  }
  void scalarMetadataWriteAlarm() {
    for (const auto& provider : providers()) {
      Channel c({prefix + "number", provider}); QSignalSpy results(&c, &Channel::writeFinished); c.start();
      QTRY_VERIFY_WITH_TIMEOUT(c.snapshot.hasValue && c.snapshot.metadataReady && c.snapshot.writable, 10000);
      QCOMPARE(c.snapshot.number, 12.5); QCOMPARE(c.snapshot.units, QString("mA"));
      QCOMPARE(c.snapshot.precision, 3); QVERIFY(!c.snapshot.host.isEmpty());
      QCOMPARE(c.snapshot.displayHigh, 100.0); QCOMPARE(c.snapshot.controlLow, -100.0);
      c.write("85"); QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 5000); QVERIFY(results[0][0].toBool());
      QTRY_COMPARE(c.snapshot.number, 85.0); QTRY_COMPARE(c.snapshot.severity, 2);
      QVERIFY(!c.snapshot.timestamp().isEmpty());
      c.write("12.5"); QTRY_COMPARE(results.size(), 2); QTRY_COMPARE(c.snapshot.number, 12.5);
      c.write("nonsense"); QTRY_COMPARE(results.size(), 3); QVERIFY(!results[2][0].toBool());
      QCOMPARE(c.snapshot.number, 12.5);
      QVERIFY(c.history.samples.size() >= 3);
      if (provider == "ca") QTRY_COMPARE(c.snapshot.description, QString("QtProbe local test value"));
    }
  }
  void enumStringIntegerAndArrays() {
    for (const auto& provider : providers()) {
      Channel e({prefix + "enum", provider}); e.start();
      QTRY_VERIFY_WITH_TIMEOUT(e.snapshot.hasValue && e.snapshot.writable && e.snapshot.choices.size() == 3, 10000);
      e.write("Standby"); QTRY_COMPARE(e.snapshot.number, 2.0);
      e.write("On"); QTRY_COMPARE(e.snapshot.number, 1.0);
      Channel t({prefix + "text", provider}); t.start(); QTRY_VERIFY(t.snapshot.hasValue && t.snapshot.writable);
      t.write("value, \"quoted\""); QTRY_COMPARE(t.snapshot.values.value(0), QString("value, \"quoted\""));
      t.write("hello"); QTRY_COMPARE(t.snapshot.values.value(0), QString("hello"));
      Channel n({prefix + "integer", provider}); n.start(); QTRY_VERIFY(n.snapshot.hasValue && n.snapshot.writable);
      QCOMPARE(n.snapshot.values[0], QString("2147483647"));
      QSignalSpy invalid(&n, &Channel::writeFinished); n.write("2147483648"); QTRY_COMPARE(invalid.size(), 1); QVERIFY(!invalid[0][0].toBool());
      Channel a({prefix + "array", provider}); a.start(); QTRY_VERIFY(a.snapshot.hasValue && a.snapshot.writable);
      a.write("[1, 2.5, -3, 4]"); QTRY_COMPARE(a.snapshot.count, quint64(4));
      QCOMPARE(a.snapshot.values, (QStringList{"1", "2.5", "-3", "4"}));
      Channel capped({prefix + "array", provider}, false, 2); capped.start();
      QTRY_VERIFY(capped.snapshot.hasValue); QCOMPARE(capped.snapshot.values.size(), 2); QVERIFY(capped.snapshot.truncated);
    }
  }
  void precisionLimitsLongStringsAndProcess() {
    for (const auto& provider : providers()) {
      Channel c({prefix + "number", provider}); c.start();
      QTRY_VERIFY_WITH_TIMEOUT(c.snapshot.hasValue && c.snapshot.metadataReady, 10000);
      QCOMPARE(c.snapshot.valueText(), QString("12.500")); QCOMPARE(c.snapshot.exactText(), QString("12.5"));
      if (provider == "ca" || std::isfinite(c.snapshot.alarmHigh)) {
        QCOMPARE(c.snapshot.alarmLow, -80.0); QCOMPARE(c.snapshot.warningLow, -60.0);
        QCOMPARE(c.snapshot.warningHigh, 60.0); QCOMPARE(c.snapshot.alarmHigh, 80.0);
        QVERIFY(c.snapshot.lowLimitsSet() && c.snapshot.highLimitsSet());
      }
      Channel counter({prefix + "counter", provider}); counter.start(); QTRY_VERIFY_WITH_TIMEOUT(counter.snapshot.hasValue, 10000);
      if (provider == "ca") {
        QTRY_VERIFY(counter.snapshot.processable); const double before = counter.snapshot.number;
        QSignalSpy processed(&counter, &Channel::writeFinished); counter.process();
        QTRY_COMPARE(processed.size(), 1); QVERIFY(processed[0][0].toBool()); QCOMPARE(processed[0][1].toString(), QString("Record processed"));
        QTRY_COMPARE(counter.snapshot.number, before + 1);
      } else {
        QTest::qWait(100); QVERIFY(!counter.snapshot.processable);
        QSignalSpy refused(&counter, &Channel::writeFinished); counter.process(); QCOMPARE(refused.size(), 1); QVERIFY(!refused[0][0].toBool());
      }
    }
    // CA "$" fields are long strings; text writes include the terminator.
    Channel desc({prefix + "number.DESC$", "ca"}); desc.start();
    QTRY_VERIFY_WITH_TIMEOUT(desc.snapshot.hasValue && desc.snapshot.writable, 10000);
    QVERIFY(desc.snapshot.longString); QCOMPARE(desc.snapshot.valueText(), QString("QtProbe local test value"));
    QSignalSpy descWrites(&desc, &Channel::writeFinished);
    desc.write("Long string writes work"); QTRY_COMPARE(descWrites.size(), 1); QVERIFY(descWrites[0][0].toBool());
    QTRY_COMPARE(desc.snapshot.text, QString("Long string writes work"));
    desc.write(QString(60, 'x')); QTRY_COMPARE(descWrites.size(), 2); QVERIFY(!descWrites[1][0].toBool());
    desc.write("QtProbe local test value"); QTRY_COMPARE(descWrites.size(), 3);
    // A CHAR waveform holding text reads as text over both providers.
    Channel wave({prefix + "longText", "ca"}); wave.start(); QTRY_VERIFY_WITH_TIMEOUT(wave.snapshot.hasValue && wave.snapshot.writable, 10000);
    QVERIFY(wave.snapshot.bytes); QSignalSpy waveWrites(&wave, &Channel::writeFinished);
    wave.write("[104, 101, 108, 108, 111, 0]"); QTRY_COMPARE(waveWrites.size(), 1); QVERIFY(waveWrites[0][0].toBool());
    QTRY_VERIFY(wave.snapshot.longString); QCOMPARE(wave.snapshot.text, QString("hello"));
    wave.write("hello world"); QTRY_COMPARE(wave.snapshot.text, QString("hello world"));
#if QTPROBE_HAVE_PVA
    Channel pvaWave({prefix + "longText", "pva"}); pvaWave.start();
    QTRY_VERIFY_WITH_TIMEOUT(pvaWave.snapshot.hasValue && pvaWave.snapshot.writable && pvaWave.snapshot.longString, 10000);
    QCOMPARE(pvaWave.snapshot.text, QString("hello world"));
    pvaWave.write("from pva"); QTRY_COMPARE(pvaWave.snapshot.text, QString("from pva")); QTRY_COMPARE(wave.snapshot.text, QString("from pva"));
#endif
  }
  void inspectorWriteUndoAndProcess() {
    writeLog().clear();
    Options options; Inspector inspector(Address::parse(prefix + "number"), options); inspector.show();
    auto* channel = inspector.channel();
    QTRY_VERIFY_WITH_TIMEOUT(channel->snapshot.hasValue && channel->snapshot.writable && channel->snapshot.metadataReady, 10000);
    inspector.refresh();
    auto* undo = inspector.findChild<QPushButton*>("undoWriteButton"); QVERIFY(!undo->isEnabled());
    QVERIFY(inspector.findChild<QWidget*>("rangeGauge")->isVisible());
    inspector.requestWrite("20"); QTRY_COMPARE(channel->snapshot.number, 20.0); QTRY_VERIFY(undo->isEnabled());
    QVERIFY(undo->toolTip().contains("12.5")); QCOMPARE(writeLog().size(), size_t(1)); QVERIFY(writeLog().back().ok);
    QCOMPARE(writeLog().back().before, QString("12.5")); QCOMPARE(writeLog().back().after, QString("20"));
    undo->click(); QTRY_COMPARE(channel->snapshot.number, 12.5); QTRY_COMPARE(writeLog().size(), size_t(2));
    // Undo can be repeated to restore the value it replaced.
    QVERIFY(undo->toolTip().contains("20"));
    auto* process = inspector.findChild<QToolButton*>("processButton"); QTRY_VERIFY(process->isVisible());
    process->click(); QTRY_COMPARE(writeLog().size(), size_t(3));
    QVERIFY(writeLog().back().ok); QCOMPARE(writeLog().back().result, QString("Record processed")); QCOMPARE(channel->snapshot.number, 12.5);
    writeLog().clear();
  }
  void recordExplorerReadsFieldsLinksAndProblems() {
    for (const auto& provider : providers()) {
      Channel subject({prefix + "calcLinked", provider}); subject.start(); QTRY_VERIFY_WITH_TIMEOUT(subject.snapshot.hasValue, 10000);
      RecordExplorer explorer(&subject, 3); explorer.show(); explorer.setActive(true); QVERIFY(explorer.active());
      QTRY_COMPARE_WITH_TIMEOUT(explorer.fieldText("SCAN"), QString("1 second"), 10000);
      QTRY_COMPARE_WITH_TIMEOUT(explorer.fieldText("RTYP"), QString("calc"), 10000);
      QTRY_VERIFY_WITH_TIMEOUT(explorer.fieldText("INPA").startsWith(prefix + "number CP"), 10000);
      QTRY_VERIFY_WITH_TIMEOUT(explorer.fieldText("FLNK").startsWith(prefix + "text"), 10000);
      auto findings = [&] { QStringList out; for (const auto& f : explorer.findings()) out << f.text; return out.join('\n'); };
      QTRY_VERIFY(findings().contains("SCAN 1 second")); QVERIFY(findings().contains("No processing problems"));
      // Clicking a link cell opens its target.
      auto* table = explorer.findChild<QTableWidget*>("recordFields"); QString opened;
      explorer.openLink = [&](const QString& target) { opened = target; };
      auto findRow = [&] { for (int r = 0; r < table->rowCount(); ++r) if (table->item(r, 0) && table->item(r, 0)->text() == "INPA") return r; return -1; };
      QTRY_VERIFY(findRow() >= 0); const int row = findRow();
      QTRY_COMPARE(table->item(row, 1)->data(Qt::UserRole).toString(), prefix + "number");
      emit table->cellClicked(row, 1); QCOMPARE(opened, prefix + "number");
      explorer.setActive(false); QVERIFY(!explorer.active());
      Channel disabled({prefix + "disabled", provider}); disabled.start(); QTRY_VERIFY_WITH_TIMEOUT(disabled.snapshot.connected, 10000);
      RecordExplorer why(&disabled, 3); why.show(); why.setActive(true);
      QTRY_VERIFY_WITH_TIMEOUT([&] { for (const auto& f : why.findings()) if (f.problem && f.text.startsWith("Disabled: DISA equals DISV")) return true; return false; }(), 10000);
      QTRY_VERIFY_WITH_TIMEOUT([&] { for (const auto& f : why.findings()) if (f.problem && f.text.startsWith("UDF = 1")) return true; return false; }(), 10000);
      for (const auto& f : why.findings()) QVERIFY2(!f.text.startsWith("Alarm 18"), qPrintable(f.text));
    }
  }
  void readOnlyAndMultipleContexts() {
    for (const auto& provider : providers()) {
      Channel ro({prefix + "number", provider}, true); ro.start(); QTRY_VERIFY(ro.snapshot.hasValue);
      const double original = ro.snapshot.number; QSignalSpy results(&ro, &Channel::writeFinished);
      ro.write("99"); QCOMPARE(results.size(), 1); QVERIFY(!results[0][0].toBool());
      QTest::qWait(150); QCOMPARE(ro.snapshot.number, original);
      // PVA read-only channels never set up a put operation, so they are not writable.
      if (provider == "pva") QVERIFY(!ro.snapshot.writable);
      { Channel peer({prefix + "number", provider}); peer.start(); QTRY_VERIFY(peer.snapshot.hasValue); }
      ro.stop(); QVERIFY(!ro.snapshot.connected); ro.start(); QTRY_VERIFY(ro.snapshot.hasValue);
    }
  }
  void sharedDeliveryStaysQuietAndSurvivesPeerRemoval() {
    for (const auto& provider : providers()) {
      std::vector<std::unique_ptr<Channel>> peers;
      for (int i = 0; i < 24; ++i) {
        auto c = std::make_unique<Channel>(Address{prefix + "number", provider}); c->start(); peers.push_back(std::move(c));
      }
      for (const auto& c : peers) QTRY_VERIFY_WITH_TIMEOUT(c->snapshot.hasValue && c->snapshot.writable, 10000);
      QTest::qWait(250); QSignalSpy idle(peers.back().get(), &Channel::changed); QTest::qWait(250); QCOMPARE(idle.size(), 0);
      // Destroy peers while a delivery request is queued; surviving channels
      // must keep receiving data, including a fresh connection after restart.
      for (int i = 0; i < 12; ++i) peers.pop_back();
      auto* survivor = peers.back().get(); QSignalSpy result(survivor, &Channel::writeFinished);
      survivor->write("16"); QTRY_COMPARE(result.size(), 1); QVERIFY(result[0][0].toBool());
      for (const auto& c : peers) QTRY_COMPARE(c->snapshot.number, 16.0);
      survivor->stop(); survivor->start(); QTRY_VERIFY(survivor->snapshot.hasValue && survivor->snapshot.writable);
      survivor->write("12.5"); QTRY_COMPARE(result.size(), 2);
      for (const auto& c : peers) QTRY_COMPARE(c->snapshot.number, 12.5);
    }
  }
  void cliJsonAndFailure() {
    const auto exe = QCoreApplication::applicationDirPath() + "/qtprobe";
    QProcess process; auto env = QProcessEnvironment::systemEnvironment(); env.remove("DISPLAY"); env.remove("WAYLAND_DISPLAY"); process.setProcessEnvironment(env);
    QStringList args{"--info", "--json", "-w", "5", prefix + "number"};
#if QTPROBE_HAVE_PVA
    args << "pva://" + prefix + "number";
#endif
    process.start(exe, args); QVERIFY(process.waitForFinished(10000));
    QCOMPARE(process.exitCode(), 0);
    auto data = QJsonDocument::fromJson(process.readAllStandardOutput()); QVERIFY(data.isArray()); QCOMPARE(data.array().size(), providers().size());
#if QTPROBE_HAVE_PVA
    // Library diagnostics (here pvAccess debug lines) go to stderr, not into the report.
    auto debug = env; debug.insert("EPICS_PVA_DEBUG", "3"); process.setProcessEnvironment(debug);
    process.start(exe, {"--info", "--json", "-w", "5", "pva://" + prefix + "number"}); QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(), 0);
    QVERIFY(QJsonDocument::fromJson(process.readAllStandardOutput()).isArray()); QVERIFY(!process.readAllStandardError().isEmpty());
    process.setProcessEnvironment(env);
#endif
    process.start(exe, {"--info", "--json", "-w", "0.2", prefix + "does-not-exist"}); QVERIFY(process.waitForFinished(5000)); QCOMPARE(process.exitCode(), 1);
    QVERIFY(QJsonDocument::fromJson(process.readAllStandardOutput()).isArray());
    process.start(exe, {"--info", "--max-elements", "0", prefix + "number"}); QVERIFY(process.waitForFinished(5000)); QCOMPARE(process.exitCode(), 2);
    // QtProbe reads --session itself; Qt reserves -session for session management.
    QTemporaryDir sessions; QFile bad(sessions.filePath("bad.qtp")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write(R"({"version":1,"pvs":["bad name"]})"); bad.close();
    for (const char* option : {"--session", "-session"}) {
      process.start(exe, {option, bad.fileName()}); QVERIFY(process.waitForFinished(10000));
      QCOMPARE(process.exitCode(), 2); QVERIFY(process.readAllStandardError().contains("PV name"));
    }
    // Argument errors are reported without a display.
    auto bare = env; bare.remove("QT_QPA_PLATFORM"); process.setProcessEnvironment(bare);
    process.start(exe, {"--json", prefix + "number"}); QVERIFY(process.waitForFinished(5000));
    QCOMPARE(process.exitStatus(), QProcess::NormalExit); QCOMPARE(process.exitCode(), 2); process.setProcessEnvironment(env);
    // EPICS parses numbers (CA settings, PVA puts) with C rules in a decimal-comma locale.
    const std::string numeric = std::setlocale(LC_NUMERIC, nullptr);
    if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8")) {
      std::setlocale(LC_NUMERIC, numeric.c_str());
      auto german = env; german.insert("LC_ALL", "de_DE.UTF-8"); german.insert("EPICS_CA_CONN_TMO", "30.5"); process.setProcessEnvironment(german);
      process.start(exe, {"--info", "-w", "5", prefix + "number"}); QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(), 0);
      const auto errors = process.readAllStandardError(); QVERIFY2(!errors.contains("real number"), errors.constData());
    }
  }
  void pvaStructureTree() {
#if QTPROBE_HAVE_PVA
    // An NTScalar: nested metadata with summaries, and Info's dotted paths from the same tree.
    Channel n({prefix + "number", "pva"}); n.start();
    QTRY_VERIFY_WITH_TIMEOUT(n.snapshot.hasValue && n.snapshot.structure, 10000);
    const auto& root = *n.snapshot.structure;
    QCOMPARE(root.type, QString("epics:nt/NTScalar:1.0"));
    auto find = [](const FieldNode& node, const QString& name) -> const FieldNode* {
      for (const auto& child : node.children) if (child.name == name) return &child;
      return nullptr;
    };
    const auto* value = find(root, "value"); QVERIFY(value); QCOMPARE(value->type, QString("double"));
    QCOMPARE(value->value, n.snapshot.values[0]); QCOMPARE(root.value, value->value);
    const auto* alarm = find(root, "alarm"); QVERIFY(alarm); QVERIFY(!alarm->value.isEmpty()); QVERIFY(find(*alarm, "severity"));
    const auto* stamp = find(root, "timeStamp"); QVERIFY(stamp); QCOMPARE(stamp->value, n.snapshot.timestamp());
    const auto* display = find(root, "display"); QVERIFY(display); QCOMPARE(find(*display, "units")->value, QString("mA"));
    QCOMPARE(n.snapshot.information().value("display.units"), QString("mA"));
    // A QSRV group: its own ID, an NTEnum member summarized by its state and a plain array.
    Channel g({prefix + "group", "pva"}); g.start();
    QTRY_VERIFY_WITH_TIMEOUT(g.snapshot.hasValue && g.snapshot.structure, 10000);
    QCOMPARE(g.snapshot.structure->type, QString("qtprobe:test/Group:1.0"));
    const auto* mode = find(*g.snapshot.structure, "mode"); QVERIFY(mode); QVERIFY(find(*mode, "value"));
    QVERIFY2(QRegularExpression("^\\([0-2]\\) (Off|On|Standby)$").match(find(*mode, "value")->value).hasMatch(), qPrintable(find(*mode, "value")->value));
    Channel a({prefix + "array", "pva"}); a.start(); QTRY_VERIFY(a.snapshot.writable);
    a.write("[1, 2, 3]");
    QTRY_VERIFY_WITH_TIMEOUT(find(*g.snapshot.structure, "list") && find(*g.snapshot.structure, "list")->length == 3, 5000);
    const auto* list = find(*g.snapshot.structure, "list");
    QCOMPARE(list->kind, FieldNode::Array); QCOMPARE(list->type, QString("double[]")); QCOMPARE(list->value, QString("[1, 2, 3]"));
    QCOMPARE(list->children.size(), 3); QCOMPARE(list->children[2].name, QString("[2]")); QCOMPARE(list->children[2].value, QString("3"));
    QVERIFY(g.snapshot.valueText().contains("see Structure"));
    // CA has no structure.
    Channel ca({prefix + "number", "ca"}); ca.start(); QTRY_VERIFY_WITH_TIMEOUT(ca.snapshot.hasValue, 10000); QVERIFY(!ca.snapshot.structure);
#else
    QSKIP("Built without pvAccess");
#endif
  }
  void serverDeniesWritesAndInt64IsExact() {
    for (const auto& provider : providers()) {
      Channel protectedPv({prefix + "protected", provider}); protectedPv.start();
      QTRY_VERIFY_WITH_TIMEOUT(protectedPv.snapshot.hasValue, 10000);
      QSignalSpy denied(&protectedPv, &Channel::writeFinished);
      protectedPv.write("42"); QTRY_COMPARE(denied.size(), 1); QVERIFY(!denied[0][0].toBool());
      QTest::qWait(150); QCOMPARE(protectedPv.snapshot.number, 7.0);
    }
#if QTPROBE_HAVE_PVA
    Channel n({prefix + "int64", "pva"}); n.start();
    QTRY_VERIFY_WITH_TIMEOUT(n.snapshot.hasValue && n.snapshot.writable, 10000);
    QCOMPARE(n.snapshot.values[0], QString("9223372036854775806"));
    QSignalSpy results(&n, &Channel::writeFinished);
    n.write("9223372036854775807"); QTRY_COMPARE(results.size(), 1); QVERIFY(results[0][0].toBool());
    QTRY_COMPARE(n.snapshot.values[0], QString("9223372036854775807"));
    n.write("9223372036854775808"); QTRY_COMPARE(results.size(), 2); QVERIFY(!results[1][0].toBool());
    n.write("-9223372036854775808"); QTRY_COMPARE(results.size(), 3); QVERIFY(results[2][0].toBool());
    QTRY_COMPARE(n.snapshot.values[0], QString("-9223372036854775808"));
#endif
  }
  void closingPvLogsPendingWrite() {
    // A PV closed before its write completes keeps a write-log entry with an unknown outcome.
    Options options; writeLog().clear(); Window window(options);
    QStringList names; for (const auto& provider : providers()) names << provider + "://" + prefix + "number";
    window.addPvs(names);
    for (int i = 0; i < window.inspectors()->count(); ++i) {
      auto* inspector = static_cast<Inspector*>(window.inspectors()->widget(i));
      QTRY_VERIFY_WITH_TIMEOUT(inspector->channel()->snapshot.hasValue && inspector->channel()->snapshot.writable, 10000);
    }
    const int count = window.inspectors()->count();
    for (int i = 0; i < count; ++i) {
      auto* inspector = static_cast<Inspector*>(window.inspectors()->widget(0));
      inspector->requestWrite("12.5"); QVERIFY(inspector->channel()->writePending()); window.closePv(0);
      QCOMPARE(writeLog().size(), size_t(i + 1)); QVERIFY(!writeLog().back().ok); QVERIFY(writeLog().back().result.contains("outcome unknown"));
    }
    writeLog().clear();
  }
  void enumLabelsReadingAsIndices() {
    for (const auto& provider : providers()) {
      // State "4" is index 2 and label "2" is index 1: the state list, Readback and Undo select the intended state.
      Options options; options.provider = provider; writeLog().clear();
      Inspector gain(Address::parse(prefix + "gain", provider), options); gain.show(); auto* channel = gain.channel();
      QTRY_VERIFY_WITH_TIMEOUT(channel->snapshot.hasValue && channel->snapshot.writable && channel->snapshot.choices.size() == 4, 10000);
      gain.refresh(); QCOMPARE(channel->snapshot.number, 1.0);
      auto* undo = gain.findChild<QPushButton*>("undoWriteButton"); auto* write = gain.findChild<QPushButton*>("writeButton");
      emit gain.findChild<QComboBox*>("enumChoice")->activated(2); write->click();
      QTRY_COMPARE(channel->snapshot.number, 2.0); QTRY_VERIFY(undo->isEnabled());
      undo->click(); QTRY_COMPARE(channel->snapshot.number, 1.0); QTRY_COMPARE(writeLog().size(), size_t(2));
      gain.findChild<QPushButton*>("useReadbackButton")->click(); QTRY_VERIFY(write->isEnabled()); write->click();
      QTRY_COMPARE(writeLog().size(), size_t(3)); QVERIFY(writeLog().back().ok); QTest::qWait(150); QCOMPARE(channel->snapshot.number, 1.0);
      writeLog().clear();
    }
  }
  void undoRestoresExactElements() {
    for (const auto& provider : providers()) {
      // Bytes that read as binary, replaced by text: Undo restores the bytes, not their JSON text.
      Channel raw({prefix + "longText", provider}); raw.start(); QTRY_VERIFY_WITH_TIMEOUT(raw.snapshot.hasValue && raw.snapshot.writable, 10000);
      QSignalSpy rawWrites(&raw, &Channel::writeFinished); raw.writeValues({"0", "1", "2"}); QTRY_COMPARE(rawWrites.size(), 1);
      QTRY_COMPARE(raw.snapshot.values, (QStringList{"0", "1", "2"}));
      Options options; options.provider = provider;
      { Inspector text(Address::parse(prefix + "longText", provider), options); text.show(); auto* channel = text.channel();
        QTRY_VERIFY_WITH_TIMEOUT(channel->snapshot.hasValue && channel->snapshot.writable && channel->snapshot.count == 3, 10000); QVERIFY(!channel->snapshot.longString);
        text.requestWrite("[104, 105, 0]"); QTRY_COMPARE(raw.snapshot.text, QString("hi"));
        auto* undo = text.findChild<QPushButton*>("undoWriteButton"); QTRY_VERIFY(undo->isEnabled()); undo->click();
        QTRY_COMPARE(raw.snapshot.values, (QStringList{"0", "1", "2"})); }
      // A truncated readback cannot be restored, so Undo stays unavailable.
      Channel full({prefix + "array", provider}); full.start(); QTRY_VERIFY_WITH_TIMEOUT(full.snapshot.writable, 10000);
      full.write("[1, 2, 3, 4, 5, 6, 7, 8]"); QTRY_COMPARE(full.snapshot.count, quint64(8));
      options.maxElements = 4;
      Inspector capped(Address::parse(prefix + "array", provider), options); capped.show(); auto* channel = capped.channel();
      QTRY_VERIFY_WITH_TIMEOUT(channel->snapshot.writable && channel->snapshot.truncated, 10000);
      QSignalSpy done(channel, &Channel::writeFinished); capped.requestWrite("[9, 9, 9, 9, 9, 9, 9, 9]"); QTRY_COMPARE(done.size(), 1); QVERIFY(done[0][0].toBool());
      auto* undo = capped.findChild<QPushButton*>("undoWriteButton"); QVERIFY(!undo->isEnabled()); QVERIFY(undo->toolTip().contains("truncated"));
      capped.requestUndo(); QTest::qWait(150); QCOMPARE(done.size(), 1); QTRY_COMPARE(full.snapshot.count, quint64(8));
    }
  }
  void reconnectAfterIocRestart() {
    std::vector<std::unique_ptr<Channel>> channels;
    for (const auto& provider : providers()) { auto c = std::make_unique<Channel>(Address{prefix + "number", provider}); c->start(); channels.push_back(std::move(c)); }
    for (const auto& c : channels) QTRY_VERIFY_WITH_TIMEOUT(c->snapshot.hasValue, 10000);
    ioc.terminate(); QVERIFY(ioc.waitForFinished(5000)); saveLog();
    for (const auto& c : channels) QTRY_VERIFY_WITH_TIMEOUT(!c->snapshot.connected, 15000);
    launch();
    for (const auto& c : channels) {
      QTRY_VERIFY_WITH_TIMEOUT(c->snapshot.connected && c->snapshot.hasValue && c->snapshot.writable, 20000);
      QCOMPARE(c->snapshot.number, 12.5);
    }
    for (const auto& c : channels) {
      QSignalSpy result(c.get(), &Channel::writeFinished);
      c->write("14"); QTRY_COMPARE(result.size(), 1); QVERIFY(result[0][0].toBool());
      QTRY_COMPARE(c->snapshot.number, 14.0);
    }
  }
  void cleanupTestCase() {
    ioc.terminate(); if (!ioc.waitForFinished(3000)) { ioc.kill(); ioc.waitForFinished(); } saveLog();
    repeater.terminate(); if (!repeater.waitForFinished(3000)) { repeater.kill(); repeater.waitForFinished(); }
  }
};
QTEST_MAIN(IocTest)
#include "test_ioc.moc"
