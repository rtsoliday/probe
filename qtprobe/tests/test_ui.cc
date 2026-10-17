#include "inspector.h"
#include "help.h"
#include "about_banner.h"
#include "appearance.h"
#include "pv_drop.h"
#include "comparison.h"
#include "record_explorer.h"
#include "structure_browser.h"
#include "model.h"
#include <QtWidgets>
#include <QtTest>
#include <QDesktopServices>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <cmath>
using namespace probe;
class UrlCapture : public QObject {
  Q_OBJECT
public:
  UrlCapture() {
    QDesktopServices::setUrlHandler("file", this, "open");
    QDesktopServices::setUrlHandler("https", this, "open");
  }
  ~UrlCapture() override {
    QDesktopServices::unsetUrlHandler("file"); QDesktopServices::unsetUrlHandler("https");
  }
  QList<QUrl> urls;
public slots:
  void open(QUrl url) { urls.append(url); }
};
struct HelpPathScope {
  QByteArray original = qgetenv("QTPROBE_HELP_PATH");
  bool wasSet = qEnvironmentVariableIsSet("QTPROBE_HELP_PATH");
  ~HelpPathScope() { if (wasSet) qputenv("QTPROBE_HELP_PATH", original); else qunsetenv("QTPROBE_HELP_PATH"); }
};
class UiTest : public QObject {
  Q_OBJECT
  QTemporaryDir settings;
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("QtProbeTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    qputenv("XDG_CACHE_HOME", (settings.path() + "/cache").toUtf8());
    qputenv("EPICS_CA_AUTO_ADDR_LIST", "NO"); qputenv("EPICS_CA_ADDR_LIST", "127.0.0.1:59991");
    qputenv("EPICS_CA_REPEATER_PORT", "59992");
    qputenv("EPICS_PVA_AUTO_ADDR_LIST", "NO"); qputenv("EPICS_PVA_ADDR_LIST", "127.0.0.1:59993");
  }
  static Inspector* inspectorAt(Window& window, int index) { return static_cast<Inspector*>(window.inspectors()->widget(index)); }
  static void publishDemo(Inspector* inspector, int pv, int samples = 180) {
    Snapshot s = inspector->channel()->snapshot;
    s.connected = s.readable = s.hasValue = s.numeric = s.metadataReady = true; s.writable = pv != 2;
    s.type = pv == 1 ? "double" : "DBF_DOUBLE"; s.precision = 3; s.count = s.capacity = 1;
    s.description = pv == 0 ? "Storage ring beam current · UI preview" : pv == 1 ? "RF phase readback · UI preview" : "Horizontal orbit position · UI preview";
    s.host = "demo-ioc.local:5064"; s.units = pv == 0 ? "mA" : pv == 1 ? "deg" : "mm";
    s.displayHigh = s.controlHigh = pv == 0 ? 250 : 20; s.displayLow = s.controlLow = pv == 0 ? 0 : -20;
    s.fields["HOPR"] = QString::number(s.displayHigh); s.fields["LOPR"] = QString::number(s.displayLow); s.fields["RTYP"] = "ai";
    for (int n = 0; n < samples; ++n) {
      s.number = pv == 0 ? 201.5 + 0.24 * std::sin(n * 0.08) + 0.07 * std::cos(n * 0.31) : pv == 1 ? 4.8 + std::sin(n * 0.06) * 0.3 : 0.024;
      s.values = QStringList{QString::number(s.number, 'f', 3)}; s.seconds = 1792166400 + n; s.nanoseconds = 123456000; s.sequence = n + 1;
      inspector->channel()->update(s);
    }
    inspector->refresh();
  }
  static FieldTree structureDemo(const QString& value, const QString& seconds) {
    auto f = [](const QString& name, const QString& type, const QString& text = {}, FieldNode::Kind kind = FieldNode::Scalar) {
      FieldNode n; n.name = name; n.type = type; n.value = text; n.kind = kind; return n;
    };
    FieldNode root = f({}, "epics:nt/NTScalar:1.0", value, FieldNode::Structure);
    root.children << f("value", "double", value);
    auto alarm = f("alarm", "alarm_t", "NO_ALARM", FieldNode::Structure); alarm.children << f("severity", "int", "0") << f("message", "string", "NO_ALARM");
    auto stamp = f("timeStamp", "time_t", seconds, FieldNode::Structure); stamp.children << f("secondsPastEpoch", "long", seconds);
    auto data = f("data", "structure", {}, FieldNode::Structure);
    auto wave = f("wave", "double[]", "[1, 2]", FieldNode::Array); wave.length = 150; wave.omitted = 148;
    wave.children << f("[0]", "double", "1") << f("[1]", "double", "2");
    data.children << wave << f("label", "string", "BPM3");
    root.children << alarm << stamp << data;
    return std::make_shared<FieldNode>(root);
  }
  void inspectorValuesControlsAndExport() {
    QSettings().setValue("sections", 0);
    Options options; options.connectOnOpen = false;
    Inspector inspector(Address::parse("QtProbe:UI:Example"), options); inspector.resize(850, 650); inspector.show();
    Snapshot s; s.name = "QtProbe:UI:Example"; s.provider = "ca"; s.type = "DBF_DOUBLE";
    s.connected = s.readable = s.writable = s.numeric = s.hasValue = s.metadataReady = true;
    s.values << "12.5"; s.number = 12.5; s.units = "mA"; s.displayHigh = 100; s.precision = 3; s.sequence = 1;
    inspector.channel()->update(s); inspector.refresh();
    // Native display follows the PV precision; the exact value stays available.
    QCOMPARE(inspector.findChild<QLabel*>("value")->text(), QString("12.500")); QCOMPARE(inspector.findChild<QLabel*>("value")->toolTip(), QString("12.5"));
    QCOMPARE(inspector.findChild<QLabel*>("valueUnits")->text(), QString("mA"));
    QVERIFY(inspector.findChild<QPushButton*>("writeButton")->isEnabled());
    // Sections start closed; Array is offered only for arrays.
    for (auto name : {"trendSection", "numericControls", "infoSection", "arraySection", "historySection"}) QVERIFY(inspector.findChild<QWidget*>(name)->isHidden());
    QVERIFY(!inspector.findChild<QToolButton*>("adjustToggle")->isHidden()); QVERIFY(inspector.findChild<QToolButton*>("arrayToggle")->isHidden());
    QSignalSpy writes(inspector.channel(), &Channel::writeFinished);
    inspector.findChild<QSlider*>("adjustSlider")->setValue(2500);
    QCOMPARE(inspector.findChild<QLineEdit*>("writeValue")->text(), QString("25")); QCOMPARE(writes.size(), 0);
    // A wheel over an unfocused slider scrolls the page and leaves the edit alone.
    inspector.findChild<QLineEdit*>("writeValue")->setText("5");
    QWheelEvent wheel(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(inspector.findChild<QSlider*>("adjustSlider"), &wheel);
    QCOMPARE(inspector.findChild<QLineEdit*>("writeValue")->text(), QString("5")); QCOMPARE(inspector.findChild<QSlider*>("adjustSlider")->value(), 2500);
    inspector.findChild<QToolButton*>("infoToggle")->click(); QVERIFY(!inspector.findChild<QWidget*>("infoSection")->isHidden());
    QCOMPARE(inspector.sections(), int(InfoSection)); QCOMPARE(QSettings().value("sections").toInt(), int(InfoSection));
    auto filter = inspector.findChild<QLineEdit*>("metadataFilter"); filter->setText("Provider");
    auto info = inspector.findChild<QTableWidget*>("information");
    int visibleRows = 0;
    for (int row = 0; row < info->rowCount(); ++row) if (!info->isRowHidden(row)) ++visibleRows;
    QCOMPARE(visibleRows, 1); filter->clear();
    auto freeze = inspector.findChild<QToolButton*>("pauseDisplay"); freeze->setChecked(true);
    QVERIFY(inspector.findChild<QLabel*>("pausedBadge")->isVisible());
    s.number = 17; s.values = QStringList{"17"}; ++s.sequence;
    inspector.channel()->update(s); inspector.refresh();
    QCOMPARE(inspector.findChild<QLabel*>("value")->text(), QString("12.500"));
    freeze->setChecked(false); QCOMPARE(inspector.findChild<QLabel*>("value")->text(), QString("17.000"));
    s.type = "long"; s.number = 9223372036854775807.0; s.values = QStringList{"9223372036854775807"};
    inspector.channel()->update(s); inspector.refresh();
    QVERIFY(!inspector.findChild<QWidget*>("numericControls")->isEnabled());
    QVERIFY(inspector.findChild<QLineEdit*>("writeValue")->isEnabled());
    s.type = "DBF_DOUBLE"; s.number = 12.5; s.values = QStringList{"12.5"}; inspector.channel()->update(s); inspector.refresh();
    QTemporaryDir out;
    inspector.saveSnapshot(out.filePath("value.json")); inspector.saveHistory(out.filePath("history.csv"));
    QFile file(out.filePath("value.json")); QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["pv"].toString(), s.name);
    // Losing write access hides the write row and Adjust; a disconnect keeps them as they were.
    s.writable = false; inspector.channel()->update(s); inspector.refresh();
    QVERIFY(!inspector.findChild<QPushButton*>("writeButton")->isEnabled());
    QVERIFY(inspector.findChild<QWidget*>("writeArea")->isHidden()); QVERIFY(inspector.findChild<QToolButton*>("adjustToggle")->isHidden());
    s.connected = false; inspector.channel()->update(s); inspector.refresh(); QVERIFY(inspector.findChild<QWidget*>("writeArea")->isHidden());
    s.connected = s.writable = true; s.array = true; s.count = 3000; s.values.clear();
    for (int i = 0; i < 3000; ++i) s.values << QString::number(i);
    inspector.channel()->update(s); inspector.refresh();
    QVERIFY(!inspector.findChild<QWidget*>("writeArea")->isHidden()); QVERIFY(!inspector.findChild<QToolButton*>("arrayToggle")->isHidden());
    QCOMPARE(inspector.findChild<QTableWidget*>("arrayValues")->rowCount(), 2048);
    QDir().mkpath(QCoreApplication::applicationDirPath() + "/test-artifacts");
    s.array = false; s.count = 1; s.values = QStringList{"12.5"}; inspector.channel()->update(s); inspector.refresh();
    inspector.findChild<QToolButton*>("infoToggle")->click(); QCOMPARE(inspector.sections(), 0);
    inspector.resize(560, inspector.preferredHeight(560)); QApplication::processEvents();
    QVERIFY(inspector.grab().save(QCoreApplication::applicationDirPath() + "/test-artifacts/qtprobe.png"));
  }
  void sessionAndReadOnly() {
    Options o; o.readOnly = true; o.connectOnOpen = false; Window window(o);
    window.addPv("ca://QtProbe:UI:One"); window.addPv("ca://QtProbe:UI:One");
    QCOMPARE(window.inspectors()->count(), 1);
    window.addPv("ca://QtProbe:UI:Two"); QCOMPARE(window.inspectors()->count(), 2);
    QVERIFY(window.findChild<QLabel*>("readOnlyBadge"));
    QTemporaryDir dir; const auto path = dir.filePath("session.qtp"); window.saveSession(path);
    Window restored(o); restored.loadSession(path); QCOMPARE(restored.inspectors()->count(), 2);
    QCOMPARE(restored.findChildren<QFrame*>("pvRow").size(), 2);
    QCOMPARE(restored.findChild<QStackedWidget*>("workspacePages")->currentIndex(), 2);
    for (int i = 0; i < restored.inspectors()->count(); ++i) {
      QVERIFY(inspectorAt(restored, i)->channel()->readOnly());
      QVERIFY(inspectorAt(restored, i)->findChild<QWidget*>("writeArea")->isHidden());
    }
    QFile bad(dir.filePath("bad.qtp")); QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("{\"version\":1,\"pvs\":[\"ca://Good\",\"bad name\"]}"); bad.close();
    QVERIFY_EXCEPTION_THROWN(restored.loadSession(bad.fileName()), std::runtime_error);
    QCOMPARE(restored.inspectors()->count(), 2);
  }
  void workspaceNavigationEmptyStateAndExactReadback() {
    Options options; options.connectOnOpen = false; Window window(options); window.show();
    window.activateWindow(); QTest::qWait(20);
    auto* pages = window.findChild<QStackedWidget*>("workspacePages");
    auto* tabs = window.findChild<QTabBar*>("pvTabs"); auto* all = window.findChild<QToolButton*>("allPvsTab");
    auto* exportButton = window.findChild<QToolButton*>("exportButton");
    QVERIFY(pages && tabs && exportButton); QCOMPARE(pages->currentIndex(), 0); QVERIFY(!exportButton->isEnabled()); QVERIFY(!tabs->isVisible() && !all->isVisible());
    auto* name = window.findChild<QComboBox*>("pvName"); QTRY_VERIFY(name->hasFocus() || name->lineEdit()->hasFocus());
    window.addPv("http://bad"); QVERIFY(window.findChild<QLabel*>("entryError")->isVisible()); QCOMPARE(window.inspectors()->count(), 0);
    window.addPv("pva://DEMO:Exact"); QCOMPARE(pages->currentIndex(), 1); QVERIFY(!tabs->isVisible());
    window.addPv("ca://DEMO:Second");
    QCOMPARE(tabs->count(), 2); QVERIFY(tabs->isVisible() && all->isVisible()); QVERIFY(all->isChecked()); QCOMPARE(pages->currentIndex(), 2); QVERIFY(exportButton->isEnabled());
    QCOMPARE(tabs->tabText(0), QString("DEMO:Exact")); QCOMPARE(all->text(), QString("All (2)"));
    tabs->setCurrentIndex(0); QCOMPARE(window.inspectors()->currentIndex(), 0); QCOMPARE(pages->currentIndex(), 1); QVERIFY(!all->isChecked());
    window.findChild<QAction*>("nextPvAction")->trigger(); QCOMPARE(window.inspectors()->currentIndex(), 1); QCOMPARE(tabs->currentIndex(), 1);
    window.findChild<QAction*>("nextPvAction")->trigger(); QCOMPARE(pages->currentIndex(), 2); QVERIFY(all->isChecked());
    window.findChild<QAction*>("previousPvAction")->trigger(); QCOMPARE(window.inspectors()->currentIndex(), 1); QCOMPARE(pages->currentIndex(), 1);
    tabs->setCurrentIndex(0);
    auto* inspector = inspectorAt(window, 0);
    Snapshot s; s.name = "DEMO:Exact"; s.provider = "pva"; s.type = "long";
    s.connected = s.readable = s.writable = s.hasValue = s.numeric = true;
    s.values = QStringList{"9223372036854775807"}; s.number = s.values[0].toDouble(); s.sequence = 1;
    inspector->channel()->update(s); inspector->refresh();
    inspector->findChild<QPushButton*>("useReadbackButton")->click();
    QCOMPARE(inspector->findChild<QLineEdit*>("writeValue")->text(), s.values[0]);
    s.array = true; s.truncated = true; inspector->channel()->update(s); inspector->refresh();
    QVERIFY(!inspector->findChild<QPushButton*>("useReadbackButton")->isEnabled());
    s.array = s.truncated = false; inspector->channel()->update(s); inspector->refresh();
    auto* pause = inspector->findChild<QToolButton*>("pauseDisplay"); pause->setChecked(true);
    s.connected = s.writable = false; inspector->channel()->update(s);
    QVERIFY(!inspector->findChild<QPushButton*>("writeButton")->isEnabled());
    QCOMPARE(inspector->findChild<QLabel*>("value")->text(), QString("9223372036854775807"));
    pause->setChecked(false);
    QTRY_COMPARE(tabs->tabToolTip(0).contains("Disconnected"), true);
    window.findChild<QAction*>("allPvsAction")->trigger(); QCOMPARE(pages->currentIndex(), 2);
    auto* filter = window.findChild<QLineEdit*>("pvFilter"); filter->setText("second");
    const auto rows = window.findChildren<QFrame*>("pvRow"); QCOMPARE(rows.size(), 2);
    QVERIFY(rows[0]->isHidden()); QVERIFY(!rows[1]->isHidden());
    window.addPv("pva://DEMO:Exact"); QCOMPARE(window.inspectors()->count(), 2); QVERIFY(filter->text().isEmpty());
    QCOMPARE(window.inspectors()->currentIndex(), 0); QCOMPARE(pages->currentIndex(), 1);
    window.closePv(0); QCOMPARE(window.inspectors()->count(), 1); QCOMPARE(tabs->count(), 1); QVERIFY(!tabs->isVisible() && !all->isVisible()); QCOMPARE(pages->currentIndex(), 1);
    QCOMPARE(inspectorAt(window, 0)->channel()->snapshot.name, QString("DEMO:Second"));
    window.closePv(0); QCOMPARE(pages->currentIndex(), 0); QVERIFY(!exportButton->isEnabled());
  }
  void windowFitsContentAndSectionsAreShared() {
    QSettings().setValue("sections", 0);
    Options options; options.connectOnOpen = false; Window window(options); window.show(); QTest::qWait(50);
    const int empty = window.height(); QVERIFY2(empty < 220, qPrintable(QString::number(empty)));
    window.addPv("ca://DEMO:Fit:One"); auto* inspector = inspectorAt(window, 0); publishDemo(inspector, 0, 20); QTest::qWait(50);
    auto* scroll = inspector->findChild<QScrollArea*>("inspectorScroll");
    const int one = window.height(); QVERIFY(one > empty); QVERIFY2(one < 400, qPrintable(QString::number(one)));
    QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
    inspector->findChild<QToolButton*>("trendToggle")->click(); QTest::qWait(50);
    // The window grows to fit unless that would exceed most of the screen.
    const int trend = window.height(), cap = window.screen()->availableGeometry().height() * 85 / 100;
    QVERIFY(trend > one); QVERIFY(scroll->verticalScrollBar()->maximum() == 0 || trend == cap);
    window.addPv("ca://DEMO:Fit:Two"); QCOMPARE(inspectorAt(window, 1)->sections(), int(TrendSection));
    QCOMPARE(window.findChild<QStackedWidget*>("workspacePages")->currentIndex(), 2);
    window.findChild<QTabBar*>("pvTabs")->setCurrentIndex(0); QTest::qWait(50); QCOMPARE(window.height(), trend);
    inspector->findChild<QToolButton*>("trendToggle")->click(); QTest::qWait(50);
    // Closing the section shrinks back; the PV tab strip now adds its own row.
    auto* tabs = window.findChild<QWidget*>("pvTabStrip");
    QCOMPARE(inspectorAt(window, 1)->sections(), 0); QCOMPARE(window.height(), one + tabs->height() + window.centralWidget()->layout()->spacing());
    QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
    // After a manual resize the window only grows to fit.
    const int sized = window.height() + 120; window.resize(window.width(), sized); QTest::qWait(20);
    inspector->findChild<QToolButton*>("infoToggle")->click(); QTest::qWait(50); const int withInfo = window.height(); QVERIFY(withInfo >= sized);
    inspector->findChild<QToolButton*>("infoToggle")->click(); QTest::qWait(50); QCOMPARE(window.height(), sized);
    // An empty workspace returns to its compact size.
    window.closePv(0); window.closePv(0); QTest::qWait(50); QCOMPARE(window.height(), empty);
    QSettings().setValue("sections", 0);
  }
  void tableSelectionSortingFiltersAndCopy() {
    Options options; options.connectOnOpen = false; Window window(options); window.resize(900, 500); window.show();
    QVERIFY(window.addPvs({"ca://T:b", "ca://T:a10", "pva://T:a2", "ca://T:lost"}));
    const double numbers[] = {5, -1, 30, 0}; const int severities[] = {0, 2, 1, 0};
    for (int i = 0; i < 4; ++i) {
      auto* channel = inspectorAt(window, i)->channel(); auto s = channel->snapshot;
      s.connected = s.readable = s.hasValue = s.numeric = i != 3; s.hasValue = true; s.type = "DBF_DOUBLE"; s.precision = 1; s.units = "mA";
      s.number = numbers[i]; s.values = QStringList{QString::number(numbers[i])}; s.severity = severities[i]; s.sequence = 1; channel->update(s);
    }
    auto* comparison = window.findChild<Comparison*>("comparison"); auto* pages = window.findChild<QStackedWidget*>("workspacePages");
    QCOMPARE(pages->currentIndex(), 2);
    auto rows = window.findChildren<QFrame*>("pvRow");
    QTRY_COMPARE(rows[1]->findChild<QLabel*>("rowValue")->text(), QString("-1.0"));
    QVERIFY(rows[0]->findChild<QLabel*>("rowAge")->text() != "—");
    auto order = [&] { QStringList names; for (auto* row : window.findChildren<QFrame*>("pvRow")) names << row->findChild<QLabel*>("rowName")->text();
      std::sort(names.begin(), names.end(), [&](const QString& a, const QString& b) {
        auto y = [&](const QString& n) { for (auto* row : window.findChildren<QFrame*>("pvRow")) if (row->findChild<QLabel*>("rowName")->text() == n) return row->y(); return 0; };
        return y(a) < y(b); }); return names; };
    // Sorting: name is natural (a2 before a10), then value, state, back to added order.
    auto* byName = window.findChild<QLabel*>("sortPV"); QTest::mouseClick(byName, Qt::LeftButton); QApplication::processEvents();
    QCOMPARE(order(), (QStringList{"T:a2", "T:a10", "T:b", "T:lost"})); QVERIFY(byName->text().endsWith("▲"));
    QTest::mouseClick(byName, Qt::LeftButton); QApplication::processEvents(); QCOMPARE(order().front(), QString("T:lost"));
    QTest::mouseClick(byName, Qt::LeftButton); QApplication::processEvents(); QCOMPARE(order(), (QStringList{"T:b", "T:a10", "T:a2", "T:lost"}));
    QTest::mouseClick(window.findChild<QLabel*>("sortValue"), Qt::LeftButton); QApplication::processEvents();
    QCOMPARE(order(), (QStringList{"T:a10", "T:b", "T:a2", "T:lost"}));
    QTest::mouseClick(window.findChild<QLabel*>("sortState"), Qt::LeftButton); QApplication::processEvents();
    QCOMPARE(order(), (QStringList{"T:lost", "T:a10", "T:a2", "T:b"}));
    // Quick filters show counts and combine as "either".
    auto* alarms = window.findChild<QToolButton*>("alarmsFilter"); auto* lost = window.findChild<QToolButton*>("disconnectedFilter");
    QTRY_COMPARE(alarms->text(), QString("Alarms (2)")); QCOMPARE(lost->text(), QString("Disconnected (1)"));
    alarms->setChecked(true); QVERIFY(rows[0]->isHidden() && !rows[1]->isHidden() && !rows[2]->isHidden() && rows[3]->isHidden());
    lost->setChecked(true); QVERIFY(!rows[3]->isHidden()); alarms->setChecked(false); lost->setChecked(false); QVERIFY(!rows[0]->isHidden());
    // Selection: click, Ctrl+click, Shift+click; the view stays on the table.
    rows = window.findChildren<QFrame*>("pvRow");
    QTest::mouseClick(rows[0], Qt::LeftButton, {}, QPoint(40, 8)); QCOMPARE(pages->currentIndex(), 2);
    QCOMPARE(window.inspectors()->currentIndex(), 0); QCOMPARE(comparison->selection().size(), 1);
    QTest::mouseClick(rows[2], Qt::LeftButton, Qt::ControlModifier, QPoint(40, 8)); QCOMPARE(comparison->selection().size(), 2);
    QTest::mouseClick(rows[1], Qt::LeftButton, Qt::ControlModifier, QPoint(40, 8)); QCOMPARE(comparison->selection().size(), 3);
    QTest::mouseClick(rows[1], Qt::LeftButton, Qt::ControlModifier, QPoint(40, 8)); QCOMPARE(comparison->selection().size(), 2);
    const auto text = comparison->copyText(Comparison::Text).split('\n', Qt::SkipEmptyParts);
    QCOMPARE(text.size(), 2); QVERIFY(text.join('\n').contains("T:b\t5\tmA\tNo alarm")); QVERIFY(text.join('\n').contains("pva://T:a2\t30\tmA\tMINOR alarm"));
    const auto csv = comparison->copyText(Comparison::Csv);
    QVERIFY(csv.startsWith("pv,provider,value,units,state,severity,timestamp_utc,age_s\n")); QVERIFY(csv.contains("\"ca://T:b\",ca,\"5\",\"mA\",\"No alarm\",0,,"));
    QTest::keyClick(comparison, Qt::Key_A, Qt::ControlModifier); QCOMPARE(comparison->selection().size(), 4);
    QTest::keyClick(comparison, Qt::Key_C, Qt::ControlModifier); QCOMPARE(QApplication::clipboard()->text().count('\n'), 4);
    QCOMPARE(comparison->copyText(Comparison::Names), QString("T:lost\nT:a10\npva://T:a2\nT:b\n"));
    // Keyboard: arrows move a single selection; Enter inspects.
    comparison->setFocus(); QTest::keyClick(comparison, Qt::Key_Home); QCOMPARE(comparison->selection(), QList<Channel*>{inspectorAt(window, 3)->channel()});
    QTest::keyClick(comparison, Qt::Key_Down); QCOMPARE(comparison->selection(), QList<Channel*>{inspectorAt(window, 1)->channel()});
    QTest::keyClick(comparison, Qt::Key_Down, Qt::ShiftModifier); QCOMPARE(comparison->selection().size(), 2);
    QTest::keyClick(comparison, Qt::Key_Return); QCOMPARE(pages->currentIndex(), 1); QCOMPARE(window.inspectors()->currentIndex(), 2);
    // Ages tick only while the table is shown.
    auto* ages = window.findChild<QTimer*>("comparisonAgeTimer"); QVERIFY(!ages->isActive());
    window.findChild<QAction*>("allPvsAction")->trigger(); QVERIFY(ages->isActive()); window.hide(); QVERIFY(!ages->isActive());
  }
  void hiddenInspectorsReleaseViewData() {
    QSettings().setValue("sections", int(TrendSection | HistorySection));
    Options options; options.connectOnOpen = false; Window window(options); window.show();
    window.addPv("ca://M:one"); window.addPv("ca://M:two");
    for (int pv = 0; pv < 2; ++pv) publishDemo(inspectorAt(window, pv), pv, 50);
    auto* tabs = window.findChild<QTabBar*>("pvTabs"); tabs->setCurrentIndex(0); QApplication::processEvents();
    auto* first = inspectorAt(window, 0); auto* trend = first->findChild<Plot*>("trendPlot"); auto* events = first->findChild<QTableWidget*>("historyValues");
    QTRY_COMPARE(events->rowCount(), 50); QCOMPARE(trend->values.size(), 50);
    tabs->setCurrentIndex(1); QApplication::processEvents(); QVERIFY(trend->values.isEmpty()); QCOMPARE(events->rowCount(), 0);
    tabs->setCurrentIndex(0); QTRY_COMPARE(events->rowCount(), 50); QCOMPARE(trend->values.size(), 50);
    // Closing a section frees its copy; a paused display keeps what it holds.
    first->findChild<QToolButton*>("trendToggle")->click(); QVERIFY(trend->values.isEmpty());
    first->findChild<QToolButton*>("pauseDisplay")->setChecked(true); tabs->setCurrentIndex(1); QApplication::processEvents();
    QCOMPARE(events->rowCount(), 50);
    first->findChild<QToolButton*>("pauseDisplay")->setChecked(false);
    QSettings().setValue("sections", 0);
  }
  void recordLinksOpenTheirTarget() {
    Options options; options.connectOnOpen = false; Window window(options); window.show();
    window.addPv("ca://R:source"); auto* inspector = inspectorAt(window, 0);
    QVERIFY(!inspector->findChild<QToolButton*>("recordToggle")->isHidden());
    auto* explorer = inspector->findChild<RecordExplorer*>("recordExplorer"); QVERIFY(explorer && !explorer->active());
    explorer->openLink("R:target.VAL");
    QCOMPARE(window.inspectors()->count(), 2); QCOMPARE(window.findChild<QStackedWidget*>("workspacePages")->currentIndex(), 1);
    QCOMPARE(inspectorAt(window, window.inspectors()->currentIndex())->channel()->snapshot.name, QString("R:target.VAL"));
    explorer->openLink("pva://R:other"); QCOMPARE(inspectorAt(window, window.inspectors()->currentIndex())->channel()->snapshot.provider, QString("pva"));
    // Following a link to an open PV selects it instead of adding a duplicate.
    explorer->openLink("R:target.VAL"); QCOMPARE(window.inspectors()->count(), 3);
  }
  void enumTrendsUseStatesAndSteps() {
    QSettings().setValue("sections", int(TrendSection));
    Options options; options.connectOnOpen = false; Window window(options); window.show();
    window.addPv("ca://E:mode"); window.addPv("ca://E:value"); auto* mode = inspectorAt(window, 0);
    auto s = mode->channel()->snapshot; s.connected = s.readable = s.hasValue = s.numeric = true; s.type = "DBF_ENUM"; s.choices = QStringList{"Off", "On"};
    for (int i = 0; i < 6; ++i) { s.number = i % 2; s.values = QStringList{QString::number(i % 2)}; s.sequence = i + 1; mode->channel()->update(s); }
    window.findChild<QTabBar*>("pvTabs")->setCurrentIndex(0); mode->refresh();
    auto* trend = mode->findChild<Plot*>("trendPlot"); QCOMPARE(trend->levels, s.choices);
    QVERIFY(mode->findChild<QLabel*>("trendStatistics")->text().contains("5 changes"));
    QVERIFY(!mode->findChild<QLabel*>("trendStatistics")->text().contains("Mean"));
    QVERIFY(mode->findChild<QWidget*>("rangeGauge")->isHidden()); QVERIFY(trend->warningLines.isEmpty());
    window.findChild<QToolButton*>("allPvsTab")->click();
    QTRY_COMPARE(window.findChildren<QFrame*>("pvRow").front()->findChild<Plot*>("rowTrend")->levels, s.choices);
    // Ordinary numbers keep a continuous plot and numeric statistics.
    auto* value = inspectorAt(window, 1); publishDemo(value, 0, 10); window.findChild<QTabBar*>("pvTabs")->setCurrentIndex(1); value->refresh();
    QVERIFY(value->findChild<Plot*>("trendPlot")->levels.isEmpty()); QVERIFY(value->findChild<QLabel*>("trendStatistics")->text().contains("Mean"));
    // A wheel over the unfocused state list does not stage another state.
    s.writable = true; ++s.sequence; mode->channel()->update(s); window.findChild<QTabBar*>("pvTabs")->setCurrentIndex(0); mode->refresh();
    auto* edit = mode->findChild<QLineEdit*>("writeValue"); edit->setText("On"); auto* states = mode->findChild<QComboBox*>("enumChoice");
    QVERIFY(states->isEnabled()); QCOMPARE(states->currentIndex(), 1);
    QWheelEvent wheel(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(states, &wheel); QCOMPARE(edit->text(), QString("On")); QCOMPARE(states->currentIndex(), 1);
    QSettings().setValue("sections", 0);
  }
  void nameEntryOpensExactlyWhatWasTyped() {
    // Recent names that extend the typed text, or differ only in case, are not substituted.
    Options options; options.connectOnOpen = false; Window window(options); window.show(); QApplication::setActiveWindow(&window);
    window.addPvs({"SR:Q10", "Ring:Current"}); while (window.inspectors()->count()) window.closePv(0);
    auto* name = window.findChild<QComboBox*>("pvName");
    for (const QString typed : {"ca://SR:Q1", "ca://ring:current"}) {
      name->lineEdit()->clear(); name->lineEdit()->setFocus(); QTest::keyClicks(name->lineEdit(), typed); QTest::keyClick(name->lineEdit(), Qt::Key_Return);
      QCOMPARE(window.inspectors()->count(), 1); QCOMPARE(inspectorAt(window, 0)->channel()->snapshot.name, Address::parse(typed).name);
      window.closePv(0);
    }
  }
  void structureMenuSurvivesUpdates() {
    // An update while a field's menu is open replaces the tree; the chosen action uses the live rows.
    QSettings().setValue("sections", int(StructureSection));
    Options options; options.connectOnOpen = false; Window window(options); window.show();
    window.addPv("pva://S:menu"); auto* pva = inspectorAt(window, 0);
    auto s = pva->channel()->snapshot; s.connected = s.readable = s.hasValue = s.numeric = true;
    s.type = "double"; s.values = QStringList{"1"}; s.number = 1; s.count = 1; s.structure = structureDemo("1", "100"); s.sequence = 1;
    pva->channel()->update(s); pva->refresh();
    auto* tree = pva->findChild<QTreeWidget*>("structureFields"); QApplication::clipboard()->clear();
    bool chosen = false;
    QTimer::singleShot(100, &window, [&] {
      s.structure = structureDemo("2", "101"); s.sequence = 2; pva->channel()->update(s); pva->refresh();
      if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) { QTest::keyClick(menu, Qt::Key_Down); QTest::keyClick(menu, Qt::Key_Return); chosen = true; }
    });
    emit tree->customContextMenuRequested(tree->visualItemRect(tree->topLevelItem(0)).center());
    QVERIFY(chosen); QCOMPARE(QApplication::clipboard()->text(), QString("2"));
  }
  void structureBrowserUpdatesInPlaceFiltersAndCopies() {
    QSettings().setValue("sections", int(StructureSection));
    Options options; options.connectOnOpen = false; Window window(options); window.show();
    window.addPv("pva://S:demo"); window.addPv("ca://S:plain");
    auto* pva = inspectorAt(window, 0); auto* ca = inspectorAt(window, 1); auto* tabs = window.findChild<QTabBar*>("pvTabs");
    auto s = pva->channel()->snapshot; s.connected = s.readable = s.hasValue = s.numeric = true;
    s.type = "double"; s.values = QStringList{"1"}; s.number = 1; s.count = 1; s.structure = structureDemo("1", "100"); s.sequence = 1;
    pva->channel()->update(s); tabs->setCurrentIndex(0); pva->refresh();
    QVERIFY(!pva->findChild<QToolButton*>("structureToggle")->isHidden()); QVERIFY(!pva->findChild<QWidget*>("structureSection")->isHidden());
    auto* tree = pva->findChild<QTreeWidget*>("structureFields"); QCOMPARE(tree->topLevelItemCount(), 4);
    auto *value = tree->topLevelItem(0), *alarm = tree->topLevelItem(1), *stamp = tree->topLevelItem(2), *data = tree->topLevelItem(3);
    QCOMPARE(value->text(2), QString("1")); QCOMPARE(alarm->text(2), QString("NO_ALARM"));
    QVERIFY(pva->findChild<QLabel*>("structureSummary")->text().startsWith("epics:nt/NTScalar:1.0 · "));
    // NT metadata starts collapsed; other structures start open.
    QVERIFY(!alarm->isExpanded()); QVERIFY(!stamp->isExpanded()); QVERIFY(data->isExpanded());
    auto* wave = data->child(0); QCOMPARE(StructureBrowser::path(wave), QString("data.wave")); QCOMPARE(wave->childCount(), 3);
    QCOMPARE(StructureBrowser::path(wave->child(1)), QString("data.wave[1]")); QVERIFY(wave->child(2)->text(0).contains("148 more elements"));
    // Updates reuse the rows: expansion survives and changed values are marked.
    alarm->setExpanded(true); data->setExpanded(false);
    const auto accent = colors(tree->palette()).accent;
    s.structure = structureDemo("2", "101"); s.sequence = 2; pva->channel()->update(s); pva->refresh();
    QCOMPARE(tree->topLevelItem(0), value); QCOMPARE(value->text(2), QString("2"));
    QCOMPARE(value->foreground(2).color(), accent); QVERIFY(alarm->foreground(2).color() != accent);
    QVERIFY(alarm->isExpanded()); QVERIFY(!data->isExpanded());
    QCOMPARE(stamp->foreground(0).color(), accent); // collapsed, with a changed field inside
    // Redrawing the same data keeps its marks; an update without changes clears them.
    pva->refresh(); QCOMPARE(value->foreground(2).color(), accent);
    s.structure = structureDemo("2", "101"); s.sequence = 3; pva->channel()->update(s); pva->refresh();
    QVERIFY(value->foreground(2).color() != accent); QVERIFY(stamp->foreground(0).color() != accent);
    // A search shows matches, their contents and ancestors; clearing it restores expansion.
    auto* filter = pva->findChild<QLineEdit*>("structureFilter"); filter->setText("bpm3");
    QVERIFY(value->isHidden()); QVERIFY(!data->isHidden()); QVERIFY(data->isExpanded()); QVERIFY(wave->isHidden()); QVERIFY(!data->child(1)->isHidden());
    filter->setText("alarm"); QVERIFY(!alarm->isHidden()); QVERIFY(!alarm->child(0)->isHidden()); QVERIFY(data->isHidden());
    filter->clear(); QVERIFY(!value->isHidden()); QVERIFY(!data->isHidden()); QVERIFY(!data->isExpanded()); QVERIFY(alarm->isExpanded());
    // Rows copy as path, type and exact value; the button copies the whole structure.
    tree->setFocus(); tree->clearSelection(); value->setSelected(true); QTest::keySequence(tree, QKeySequence::Copy);
    QCOMPARE(QApplication::clipboard()->text(), QString("value\tdouble\t2"));
    pva->findChild<QToolButton*>("copyStructure")->click(); QVERIFY(QApplication::clipboard()->text().startsWith("double value 2\n"));
    QVERIFY(QApplication::clipboard()->text().contains("        … 148 more elements\n"));
    // CA has no structure. A hidden inspector frees its rows but keeps expansion choices.
    tabs->setCurrentIndex(1); ca->refresh();
    QVERIFY(ca->findChild<QToolButton*>("structureToggle")->isHidden()); QVERIFY(ca->findChild<QWidget*>("structureSection")->isHidden());
    QTRY_COMPARE(tree->topLevelItemCount(), 0);
    tabs->setCurrentIndex(0); pva->refresh(); QCOMPARE(tree->topLevelItemCount(), 4);
    QVERIFY(tree->topLevelItem(1)->isExpanded()); QVERIFY(!tree->topLevelItem(3)->isExpanded());
    QSettings().setValue("sections", 0);
  }
  void arraysHaveNoTrend() {
    // Element 0 over time is not a trend of an array or a text waveform; Array shows them.
    QSettings().setValue("sections", int(TrendSection | ArraySection));
    Options options; options.connectOnOpen = false; Window window(options); window.show();
    window.addPv("ca://A:wave"); window.addPv("ca://A:text"); window.addPv("ca://A:scalar");
    auto s = inspectorAt(window, 0)->channel()->snapshot; s.connected = s.readable = s.hasValue = s.numeric = s.array = true;
    s.type = "DBF_DOUBLE"; s.count = s.capacity = 3;
    for (int i = 0; i < 4; ++i) { s.values = QStringList{QString::number(i), "2", "3"}; s.number = i; s.sequence = i + 1; inspectorAt(window, 0)->channel()->update(s); }
    s.type = "DBF_CHAR"; s.bytes = true; s.count = s.capacity = 3; s.values = QStringList{"72", "105", "0"}; s.number = 72; s.sequence = 1;
    inspectorAt(window, 1)->channel()->update(s);
    for (int pv = 0; pv < 2; ++pv) {
      auto* inspector = inspectorAt(window, pv); window.findChild<QTabBar*>("pvTabs")->setCurrentIndex(pv); inspector->refresh();
      QVERIFY(inspector->findChild<QToolButton*>("trendToggle")->isHidden()); QVERIFY(!inspector->findChild<QToolButton*>("arrayToggle")->isHidden());
      QVERIFY(inspector->findChild<QWidget*>("trendSection")->isHidden()); QCOMPARE(inspector->sections(), int(TrendSection | ArraySection));
      for (const auto& sample : inspector->channel()->history.samples) QVERIFY(!std::isfinite(sample.number));
    }
    // Scalars keep Trend, and the shared choice still applies to them.
    auto* scalar = inspectorAt(window, 2); publishDemo(scalar, 0, 5); window.findChild<QTabBar*>("pvTabs")->setCurrentIndex(2); scalar->refresh();
    QVERIFY(!scalar->findChild<QToolButton*>("trendToggle")->isHidden()); QVERIFY(!scalar->findChild<QWidget*>("trendSection")->isHidden());
    window.findChild<QToolButton*>("allPvsTab")->click();
    QTRY_VERIFY(!window.findChildren<QFrame*>("pvRow").isEmpty());
    for (const auto& value : window.findChildren<QFrame*>("pvRow").front()->findChild<Plot*>("rowTrend")->values) QVERIFY(!std::isfinite(value));
    QSettings().setValue("sections", 0);
  }
  void textWidthsNeverElideUnhintedText() {
    // Remote X displays often render without hinting, with fractional widths.
    int rounded = 0;
    for (const auto hinting : {QFont::PreferFullHinting, QFont::PreferNoHinting})
      for (const QString text : {"OFF", "ON", "Standby", "201.81", "-0.666667", "64.371"})
        for (int tenth = 60; tenth <= 160; ++tenth) {
          QFont font = QApplication::font(); font.setPointSizeF(tenth / 10.0); font.setHintingPreference(hinting);
          const QFontMetrics metrics(font);
          QCOMPARE(metrics.elidedText(text, Qt::ElideRight, textWidth(font, text)), text);
          rounded += metrics.elidedText(text, Qt::ElideRight, metrics.horizontalAdvance(text)) != text;
          ElidedLabel label; label.setFont(font); label.setText(text); label.resize(label.sizeHint());
          QCOMPARE(metrics.elidedText(text, Qt::ElideRight, label.contentsRect().width()), text);
        }
    qInfo() << "rounded widths would have elided" << rounded << "cases";
    // History timestamps fit their column at any zoom.
    Options options; options.connectOnOpen = false; Inspector inspector(Address::parse("W:stamp"), options);
    auto* events = inspector.findChild<QTableWidget*>("historyValues"); const QString stamp = "2026-10-16T16:00:02.123456789Z";
    QVERIFY(events->columnWidth(0) >= textWidth(events->font(), stamp) + 12);
    auto bigger = events->font(); bigger.setPointSizeF(bigger.pointSizeF() * 1.5); events->setFont(bigger);
    QVERIFY(events->columnWidth(0) >= textWidth(bigger, stamp) + 12);
  }
  void allTabStaysVisibleWithManyPvs() {
    Options options; options.connectOnOpen = false; Window window(options); window.resize(640, 300); window.show();
    QStringList names; for (int i = 0; i < 30; ++i) names << QString("ca://Many:PV:%1:LongerName").arg(i);
    QVERIFY(window.addPvs(names)); auto* tabs = window.findChild<QTabBar*>("pvTabs"); auto* all = window.findChild<QToolButton*>("allPvsTab");
    // Inspect the right-most PV: its tab scrolls into view, All stays put.
    tabs->setCurrentIndex(28); tabs->setCurrentIndex(29); QApplication::processEvents(); QTest::qWait(50);
    QCOMPARE(window.findChild<QStackedWidget*>("workspacePages")->currentIndex(), 1);
    QVERIFY(all->isVisible()); QVERIFY(all->geometry().right() < tabs->geometry().left());
    QVERIFY(QRect(QPoint(), window.size()).contains(all->mapTo(&window, all->rect().center())));
    QVERIFY(tabs->tabRect(29).intersects(tabs->rect())); QVERIFY(!tabs->tabRect(0).intersects(tabs->rect()));
    QVERIFY(all->text() == "All (30)" && !all->isChecked());
    all->click(); QCOMPARE(window.findChild<QStackedWidget*>("workspacePages")->currentIndex(), 2); QVERIFY(all->isChecked());
    QVERIFY(tabs->property("inactive").toBool());
    // Clicking the PV tab that is still current returns to its inspector.
    emit tabs->tabBarClicked(29); QCOMPARE(window.findChild<QStackedWidget*>("workspacePages")->currentIndex(), 1);
    QCOMPARE(window.inspectors()->currentIndex(), 29); QVERIFY(!tabs->property("inactive").toBool());
    window.closePv(29); QCOMPARE(tabs->count(), 29); QCOMPARE(all->text(), QString("All (29)"));
  }
  void gaugeLimitsLoggingAndConfirmation() {
    QSettings().setValue("sections", int(TrendSection)); writeLog().clear();
    Options options; options.connectOnOpen = false; Window window(options); window.show();
    window.addPv("ca://G:one"); auto* inspector = inspectorAt(window, 0); auto s = inspector->channel()->snapshot;
    s.connected = s.readable = s.writable = s.hasValue = s.numeric = true; s.type = "DBF_DOUBLE"; s.precision = 2; s.values = QStringList{"70"}; s.number = 70;
    s.displayLow = 0; s.displayHigh = 100; s.alarmLow = 0; s.warningLow = 0; s.warningHigh = 60; s.alarmHigh = 80; s.severity = 1; s.sequence = 1;
    inspector->channel()->update(s); inspector->refresh();
    auto* gauge = inspector->findChild<QWidget*>("rangeGauge"); QVERIFY(gauge->isVisible()); QVERIFY(gauge->toolTip().contains("HIGH 60 · HIHI 80")); QVERIFY(!gauge->toolTip().contains("LOLO"));
    QCOMPARE(inspector->findChild<Plot*>("trendPlot")->warningLines, QVector<double>{60}); QCOMPARE(inspector->findChild<Plot*>("trendPlot")->alarmLines, QVector<double>{80});
    QVERIFY(inspector->findChild<QToolButton*>("processButton")->isHidden());
    // Without a connection the write fails; the attempt is still logged and Undo stays off.
    inspector->findChild<QLineEdit*>("writeValue")->setText("75"); inspector->requestWrite("75");
    QCOMPARE(writeLog().size(), size_t(1)); QCOMPARE(writeLog().back().before, QString("70")); QCOMPARE(writeLog().back().after, QString("75"));
    QVERIFY(!writeLog().back().ok); QVERIFY(!inspector->findChild<QPushButton*>("undoWriteButton")->isEnabled());
    window.showWriteLog(); auto* log = window.findChild<QDialog*>("writeLogDialog"); QVERIFY(log && log->isVisible());
    QCOMPARE(log->findChild<QTableWidget*>("writeLogTable")->rowCount(), 1);
    QDir().mkpath(QCoreApplication::applicationDirPath() + "/test-artifacts"); QVERIFY(log->grab().save(QCoreApplication::applicationDirPath() + "/test-artifacts/write-log.png"));
    // Confirmation: answering No sends and logs nothing.
    window.findChild<QAction*>("confirmWritesAction")->setChecked(true); QVERIFY(QSettings().value("confirmWrites").toBool());
    QString question;
    QTimer::singleShot(0, [&] { auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); if (!box) return; question = box->text(); box->button(QMessageBox::No)->click(); });
    inspector->requestWrite("76"); QVERIFY(question.contains("Write 76 to G:one")); QVERIFY(question.contains("Current value: 70"));
    QCOMPARE(writeLog().size(), size_t(1));
    window.findChild<QAction*>("confirmWritesAction")->setChecked(false);
    log->findChild<QPushButton*>("clearWriteLog")->click(); QVERIFY(writeLog().empty()); QCOMPARE(log->findChild<QTableWidget*>("writeLogTable")->rowCount(), 0);
    // Not a scalar in range: no gauge.
    s.array = true; s.count = 2; s.values = QStringList{"1", "2"}; ++s.sequence; inspector->channel()->update(s); inspector->refresh(); QVERIFY(gauge->isHidden());
    QSettings().setValue("sections", 0);
  }
  void pastedListsZoomAndStripTool() {
    Options options; options.connectOnOpen = false; Window window(options); window.show();
    window.addPv(" ca://P:a pva://P:b\nP:c "); QCOMPARE(window.inspectors()->count(), 3);
    QCOMPARE(inspectorAt(window, 1)->channel()->snapshot.provider, QString("pva"));
    // Zoom scales fonts, rows and the remembered setting.
    const qreal base = QApplication::font().pointSizeF(); auto* row = window.findChildren<QFrame*>("pvRow").front(); const int rowHeight = row->height();
    auto* value = inspectorAt(window, 0)->findChild<QLabel*>("value"); const qreal valueSize = value->font().pointSizeF();
    window.findChild<QAction*>("zoomInAction")->trigger(); window.findChild<QAction*>("zoomInAction")->trigger(); QCOMPARE(zoom(), 125);
    QCOMPARE(QSettings().value("zoom").toInt(), 125); QVERIFY(QApplication::font().pointSizeF() > base * 1.2);
    QVERIFY(value->font().pointSizeF() > valueSize * 1.2); QTRY_VERIFY(row->height() > rowHeight);
    window.findChild<QAction*>("actualSizeAction")->trigger(); QCOMPARE(zoom(), 100); QCOMPARE(QApplication::font().pointSizeF(), base); QTRY_COMPARE(row->height(), rowHeight);
    // QtStripTool receives a configuration with the CA PVs only.
    QTemporaryDir dir; const auto script = dir.filePath("fake-striptool"), record = dir.filePath("args");
    { QFile file(script); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QString("#!/bin/sh\ncp \"$1\" '%1'\n").arg(record).toUtf8()); }
    QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    qputenv("QTPROBE_STRIPTOOL", script.toUtf8());
    QList<Channel*> channels; for (int i = 0; i < 3; ++i) channels << inspectorAt(window, i)->channel();
    window.openInStripTool(channels);
    QTRY_VERIFY(QFile::exists(record)); QFile config(record); QVERIFY(config.open(QIODevice::ReadOnly)); const auto text = QString::fromUtf8(config.readAll());
    QVERIFY(text.contains("Strip.Curve.0.Name            P:a")); QVERIFY(text.contains("Strip.Curve.1.Name            P:c")); QVERIFY(!text.contains("P:b"));
    // Generated configurations do not accumulate.
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/striptool";
    for (int i = 0; i < 24; ++i) { window.openInStripTool({channels[0]}); QTest::qWait(2); }
    QVERIFY(QDir(folder).entryList({"qtprobe-*.stp"}, QDir::Files).size() <= 20);
    qunsetenv("QTPROBE_STRIPTOOL");
  }
  void modernWorkspaceVisualsAndAppearance() {
    QSettings().setValue("sections", 0);
    Options options; options.connectOnOpen = false; Window window(options); window.setAppearance("light"); window.show();
    const auto output = QCoreApplication::applicationDirPath() + "/test-artifacts/"; QDir().mkpath(output);
    QTest::qWait(50); QVERIFY(window.grab().save(output + "workspace-empty.png"));
    window.addPv("ca://DEMO:Beam:Current"); publishDemo(inspectorAt(window, 0), 0); QTest::qWait(100);
    QVERIFY(window.grab().save(output + "workspace-one.png"));
    auto* inspector = inspectorAt(window, 0);
    inspector->findChild<QToolButton*>("trendToggle")->click(); inspector->findChild<QToolButton*>("adjustToggle")->click(); QTest::qWait(100);
    QVERIFY(window.grab().save(output + "workspace-sections.png"));
    inspector->findChild<QToolButton*>("adjustToggle")->click();
    window.addPv("pva://DEMO:RF:Phase"); window.addPv("ca://DEMO:Orbit:Position");
    for (int pv = 1; pv < 3; ++pv) publishDemo(inspectorAt(window, pv), pv);
    QTest::qWait(200); QApplication::processEvents();
    QVERIFY(window.grab().save(output + "workspace-all-pvs-light.png"));
    window.setAppearance("dark"); QApplication::processEvents(); QVERIFY(window.grab().save(output + "workspace-all-pvs-dark.png"));
    window.resize(520, window.height()); QTest::qWait(50); QVERIFY(window.grab().save(output + "workspace-all-pvs-compact.png"));
    window.resize(640, window.height()); window.setAppearance("light");
    window.findChild<QTabBar*>("pvTabs")->setCurrentIndex(0); QTest::qWait(200);
    QVERIFY(window.grab().save(output + "workspace-light.png"));
    inspector->findChild<QPushButton*>("useReadbackButton")->click(); QApplication::processEvents();
    QVERIFY(window.grab().save(output + "workspace-write.png"));
    window.findChild<QAction*>("appearance_dark")->trigger(); QApplication::processEvents();
    QVERIFY(window.palette().color(QPalette::Window).lightness() < 128); QVERIFY(window.grab().save(output + "workspace-dark.png"));
    // Popups are separate windows; they still follow the theme, and disabled
    // menu text has no etched highlight.
    { QMenu popup(&window); popup.ensurePolished(); QVERIFY(popup.palette().color(QPalette::Window).lightness() < 128); }
    QCOMPARE(window.palette().color(QPalette::Disabled, QPalette::Light).alpha(), 0);
    QVERIFY(window.palette().color(QPalette::Disabled, QPalette::Text).lightness() < window.palette().color(QPalette::Active, QPalette::PlaceholderText).lightness());
    auto* fileMenu = window.menuBar()->actions().front()->menu(); fileMenu->popup(window.mapToGlobal(QPoint(0, 20))); QApplication::processEvents();
    QVERIFY(fileMenu->grab().save(output + "menu-dark.png")); fileMenu->hide();
    auto* help = showHelp(&window); QApplication::processEvents();
    qInfo() << "Dark Help palettes" << window.palette().color(QPalette::Base) << help->palette().color(QPalette::Base)
            << help->findChild<QTextBrowser*>("helpBrowser")->palette().color(QPalette::Base);
    QVERIFY(help->grab().save(output + "help-dark.png"));
    QVERIFY(help->findChild<QTextBrowser*>("helpBrowser")->palette().color(QPalette::Base).lightness() < 128);
    auto* helpFind = help->findChild<QLineEdit*>("helpFind"); helpFind->setText("pvaccess");
    const auto matches = help->findChild<QTextBrowser*>("helpBrowser")->extraSelections().size();
    window.setAppearance("light"); QVERIFY(help->findChild<QTextBrowser*>("helpBrowser")->palette().color(QPalette::Base).lightness() > 128);
    { QMenu popup(&window); popup.ensurePolished(); QVERIFY(popup.palette().color(QPalette::Window).lightness() > 128); }
    QCOMPARE(help->findChild<QTextBrowser*>("helpBrowser")->extraSelections().size(), matches);
    window.setAppearance("dark"); help->close();
    QCOMPARE(QSettings().value("appearance").toString(), QString("dark"));
    Window restored(options); QVERIFY(restored.palette().color(QPalette::Window).lightness() < 128);
    // A window shorter than its content scrolls the inspector.
    for (auto toggle : {"infoToggle", "historyToggle"}) inspector->findChild<QToolButton*>(toggle)->click();
    QTest::qWait(50); window.resize(640, 300); QTest::qWait(50); QVERIFY(window.grab().save(output + "workspace-compact.png"));
    QVERIFY(inspector->findChild<QScrollArea*>("inspectorScroll")->verticalScrollBar()->maximum() > 0);
    QSettings().setValue("sections", 0);
    window.setAppearance("system");
  }
  void multiPvComparisonUpdatesFilteringAndClosing() {
    Options options; options.connectOnOpen = false; options.readOnly = true;
    Window window(options); window.resize(1380, 1000); window.show();
    QVERIFY(window.addPvs({"ca://DEMO:Current", "pva://DEMO:Exact", "ca://DEMO:Array", "ca://DEMO:Current"}));
    QCOMPARE(window.inspectors()->count(), 3);
    auto* pages = window.findChild<QStackedWidget*>("workspacePages"); QCOMPARE(pages->currentIndex(), 2);
    auto rows = window.findChildren<QFrame*>("pvRow"); QCOMPARE(rows.size(), 3);
    for (int i = 0; i < 3; ++i) {
      auto* inspector = inspectorAt(window, i); auto s = inspector->channel()->snapshot;
      s.connected = s.readable = s.writable = s.hasValue = s.numeric = true;
      s.type = i == 1 ? "long" : "double"; s.count = s.capacity = i == 2 ? 3 : 1; s.array = i == 2; s.severity = i == 1 ? 2 : 0;
      s.values = i == 1 ? QStringList{"9223372036854775807"} : i == 2 ? QStringList{"1", "2", "3"} : QStringList{"201.25"};
      s.number = s.values[0].toDouble(); s.seconds = 1792166400; s.sequence = 1; inspector->channel()->update(s);
      QVERIFY(inspector->channel()->readOnly());
    }
    QTRY_COMPARE(rows[1]->findChild<QLabel*>("rowValue")->text(), QString("9223372036854775807"));
    QCOMPARE(rows[1]->findChild<QLabel*>("rowState")->text(), QString("MAJOR alarm"));
    QCOMPARE(rows[0]->findChild<QLabel*>("rowState")->text(), QString("No alarm"));
    auto* exact = rows[1]->findChild<QLabel*>("rowValue"); QVERIFY(QFontMetrics(exact->font()).horizontalAdvance(exact->text()) <= exact->width());
    QCOMPARE(rows[2]->findChild<QLabel*>("rowValue")->text(), QString("[1, 2, 3]"));
    QVERIFY(rows[0]->isVisible() && rows[1]->isVisible());
    // Name, value and state columns line up between rows and the header.
    QCOMPARE(rows[0]->findChild<QLabel*>("rowValue")->x(), rows[1]->findChild<QLabel*>("rowValue")->x());
    QVERIFY(rows[0]->findChild<QWidget*>("rowTrend")->isVisible());
    window.resize(560, 680); QTest::qWait(20); QVERIFY(!rows[0]->findChild<QWidget*>("rowTrend")->isVisible()); QVERIFY(rows[0]->findChild<QLabel*>("rowState")->isVisible());
    window.resize(1380, 1000); QTest::qWait(20);
    auto* filter = window.findChild<QLineEdit*>("pvFilter"); filter->setText("Exact");
    QVERIFY(rows[0]->isHidden()); QVERIFY(!rows[1]->isHidden()); QVERIFY(rows[2]->isHidden());
    filter->setText("no-such-pv"); for (auto* row : rows) QVERIFY(row->isHidden());
    QVERIFY(window.findChild<QLabel*>("comparisonEmpty")->isVisible());
    filter->clear(); QTest::qWait(20);
    // A click selects without leaving the table; a double-click inspects.
    QTest::mouseClick(rows[0], Qt::LeftButton, {}, QPoint(30, rows[0]->height() / 2));
    QCOMPARE(pages->currentIndex(), 2); QCOMPARE(window.inspectors()->currentIndex(), 0); QVERIFY(rows[0]->property("selected").toBool());
    QTest::mouseDClick(rows[0], Qt::LeftButton, {}, QPoint(30, rows[0]->height() / 2));
    QCOMPARE(pages->currentIndex(), 1); QCOMPARE(window.inspectors()->currentIndex(), 0);
    auto* inspector = inspectorAt(window, 0); inspector->refresh();
    inspector->findChild<QToolButton*>("pauseDisplay")->setChecked(true);
    auto s = inspector->channel()->snapshot; s.values = QStringList{"202.5"}; s.sequence++; inspector->channel()->update(s);
    window.findChild<QAction*>("allPvsAction")->trigger(); QCOMPARE(pages->currentIndex(), 2);
    QTRY_COMPARE(rows[0]->findChild<QLabel*>("rowValue")->text(), QString("202.5"));
    QCOMPARE(inspector->findChild<QLabel*>("value")->text(), QString("201.25"));
    s.connected = false; s.error = "Disconnected"; inspector->channel()->update(s);
    QTRY_COMPARE(rows[0]->findChild<QLabel*>("rowState")->text(), QString("Disconnected"));
    QVERIFY(rows[0]->findChild<QLabel*>("rowValue")->toolTip().contains("Last observation"));
    rows[1]->findChild<QToolButton*>("closeRowButton")->click(); QTRY_COMPARE(window.inspectors()->count(), 2);
    QCOMPARE(window.findChildren<QFrame*>("pvRow").size(), 2); QCOMPARE(pages->currentIndex(), 2);
    QCOMPARE(window.findChild<QTabBar*>("pvTabs")->count(), 2);
    QVERIFY(!window.addPvs({"ca://valid", "http://invalid"})); QCOMPARE(window.inspectors()->count(), 2);
    QStringList tooMany; for (int i = 0; i < 99; ++i) tooMany << QString("ca://capacity:%1").arg(i);
    QVERIFY(!window.addPvs(tooMany)); QCOMPARE(window.inspectors()->count(), 2);
    QCOMPARE(window.findChildren<QFrame*>("pvRow").size(), 2);
    QVERIFY(window.findChild<QLabel*>("entryError")->text().contains("100"));
    window.closePv(0); window.closePv(0); QCOMPARE(pages->currentIndex(), 0); QVERIFY(window.findChildren<QFrame*>("pvRow").isEmpty());
  }
  void textDropsReachWorkspaceAndChildControls() {
    QCOMPARE(droppedPvNames(" ca://A\tpva://B\nA\r\nwave[0:3] with,comma"), (QStringList{"ca://A", "pva://B", "ca://wave[0:3]", "ca://with,comma"}));
    QCOMPARE(droppedPvNames("A pva://B", "pva"), (QStringList{"pva://A", "pva://B"}));
    for (const auto& bad : {QString(), QString("ca://"), QString("A http://B"), QString("A\x01"), QString(65537, 'x'), QString(4097, 'x'), QString(101, ' ').replace(" ", "A ")}) QVERIFY(droppedPvNames(bad).isEmpty());
    Options options; options.connectOnOpen = false; Window window(options); window.show();
    auto* provider = window.findChild<QComboBox*>("providerSelector"); provider->setCurrentText("pva");
    auto send = [](QWidget* target, QMimeData& mime, bool expected, Qt::DropActions actions = Qt::CopyAction, bool releaseSelection = false) {
      QDragEnterEvent enter(QPoint(8, 8), actions, &mime, Qt::MiddleButton, Qt::NoModifier); QApplication::sendEvent(target, &enter);
      QCOMPARE(enter.isAccepted(), expected);
      if (!expected) return;
      QDragMoveEvent move(QPoint(9, 8), actions, &mime, Qt::MiddleButton, Qt::NoModifier); QApplication::sendEvent(target, &move); QVERIFY(move.isAccepted());
      if (releaseSelection) mime.clear();
      QDropEvent drop(QPointF(9, 8), actions, &mime, Qt::MiddleButton, Qt::NoModifier); QApplication::sendEvent(target, &drop);
      QVERIFY(drop.isAccepted()); QCOMPARE(drop.dropAction(), Qt::CopyAction);
    };
    QMimeData names; names.setText("A pva://B ca://C A"); send(&window, names, true, Qt::CopyAction | Qt::MoveAction, true);
    QCOMPARE(window.inspectors()->count(), 3);
    QCOMPARE(inspectorAt(window, 0)->channel()->snapshot.provider, QString("pva"));
    QCOMPARE(window.findChild<QStackedWidget*>("workspacePages")->currentIndex(), 2);
    auto* input = window.findChild<QComboBox*>("pvName")->lineEdit(); input->setText("staged-name");
    QMimeData single; single.setText("ca://D"); send(input, single, true); QCOMPARE(input->text(), QString("")); QCOMPARE(window.inspectors()->count(), 4);
    auto* inspector = inspectorAt(window, 0); window.inspectors()->setCurrentIndex(0);
    auto* editor = inspector->findChild<QLineEdit*>("writeValue"); editor->setText("staged value");
    QSignalSpy writes(inspector->channel(), &Channel::writeFinished);
    QMimeData extra; extra.setText("ca://E pva://F"); send(editor, extra, true);
    QCOMPARE(editor->text(), QString("staged value")); QCOMPARE(writes.count(), 0); QCOMPARE(window.inspectors()->count(), 6);
    QMimeData duplicate; duplicate.setText("pva://A"); send(window.findChild<QTabBar*>("pvTabs"), duplicate, true); QCOMPARE(window.inspectors()->count(), 6);
    QMimeData malformed; malformed.setText("G http://bad"); send(input, malformed, false); QCOMPARE(window.inspectors()->count(), 6);
    QMimeData file; file.setUrls({QUrl::fromLocalFile("/tmp/not-a-pv.txt")}); file.setText("not-a-pv"); send(input, file, false);
    QMimeData moveOnly; moveOnly.setText("ca://G"); send(input, moveOnly, false, Qt::MoveAction);
    QCOMPARE(window.inspectors()->count(), 6);
    // A cancelled gesture cannot leave stale names available for a later drop.
    QMimeData cancelled; cancelled.setText("ca://Cancelled"); QDragEnterEvent enter(QPoint(8, 8), Qt::CopyAction, &cancelled, Qt::MiddleButton, Qt::NoModifier); QApplication::sendEvent(&window, &enter); QVERIFY(enter.isAccepted());
    QDragLeaveEvent leave; QApplication::sendEvent(&window, &leave);
    QDropEvent stale(QPointF(8, 8), Qt::CopyAction, &cancelled, Qt::MiddleButton, Qt::NoModifier); QApplication::sendEvent(&window, &stale); QVERIFY(!stale.isAccepted()); QCOMPARE(window.inspectors()->count(), 6);
  }
  void renderWorkIsCoalescedAndHiddenSectionsCatchUp() {
    QSettings().setValue("sections", 0);
    Options options; options.connectOnOpen = false; Inspector inspector(Address::parse("DEMO:Render"), options);
    inspector.resize(1000, 900); inspector.show();
    auto* render = inspector.findChild<QTimer*>("inspectorRenderTimer"); QVERIFY(render); QVERIFY(render->isSingleShot());
    auto* historyToggle = inspector.findChild<QToolButton*>("historyToggle");
    auto* arrayToggle = inspector.findChild<QToolButton*>("arrayToggle");
    auto* history = inspector.findChild<QTableWidget*>("historyValues");
    auto* arrays = inspector.findChild<QTableWidget*>("arrayValues");
    QSignalSpy renders(render, &QTimer::timeout);
    auto s = inspector.channel()->snapshot; s.connected = s.readable = s.writable = s.hasValue = s.numeric = true; s.type = "DBF_DOUBLE";
    auto publish = [&](int value) { s.number = value; s.values = QStringList{QString::number(value)}; ++s.sequence; inspector.channel()->update(s); };
    for (int i = 0; i < 1100; ++i) publish(i);
    QTRY_COMPARE(inspector.findChild<QLabel*>("value")->text(), QString("1099"));
    QCOMPARE(history->rowCount(), 0); QVERIFY(renders.count() <= 2);
    QTest::qWait(200); const int idleCount = renders.count(); QTest::qWait(220); QCOMPARE(renders.count(), idleCount); QVERIFY(!render->isActive());
    historyToggle->click(); QCOMPARE(history->rowCount(), 1000); QCOMPARE(history->item(0, 1)->text(), QString("1099"));
    auto* oldNewest = history->item(0, 1); history->selectRow(0);
    for (int i = 1100; i < 1105; ++i) publish(i);
    QTRY_COMPARE(history->item(0, 1)->text(), QString("1104")); QCOMPARE(history->rowCount(), 1000);
    QCOMPARE(history->item(5, 1), oldNewest); QVERIFY(oldNewest->isSelected());
    QCOMPARE(history->item(999, 1)->text(), QString("105"));
    historyToggle->click(); s.array = true; s.count = 3; s.values = QStringList{"10", "20", "30"}; ++s.sequence; inspector.channel()->update(s);
    QTest::qWait(140); QCOMPARE(arrays->rowCount(), 0); QVERIFY(!arrayToggle->isHidden());
    arrayToggle->click(); QCOMPARE(arrays->rowCount(), 3); QCOMPARE(arrays->item(2, 1)->text(), QString("30"));
    inspector.hide(); s.array = false; publish(2000); QVERIFY(!render->isActive());
    inspector.show(); QCOMPARE(inspector.findChild<QLabel*>("value")->text(), QString("2000"));
    QVERIFY(arrayToggle->isHidden()); QVERIFY(inspector.findChild<QWidget*>("arraySection")->isHidden());
    auto* pause = inspector.findChild<QToolButton*>("pauseDisplay"); pause->setChecked(true); publish(2001); QVERIFY(!render->isActive());
    historyToggle->click(); QCOMPARE(history->item(0, 1)->text(), QString("2000"));
    pause->setChecked(false); QCOMPARE(history->item(0, 1)->text(), QString("2001"));
    inspector.channel()->history.clear(); inspector.refresh(false); QCOMPARE(history->rowCount(), 0);
    publish(2002); QTRY_COMPARE(history->rowCount(), 1); QCOMPARE(history->item(0, 1)->text(), QString("2002"));
    QSettings().setValue("sections", 0);
  }
  void offscreenCardsCatchUpOnScrollAndIdleTimersStop() {
    Options options; options.connectOnOpen = false; Window window(options); window.resize(1000, 400); window.show();
    QStringList names; for (int i = 0; i < 40; ++i) names << QString("ca://DEMO:Row:%1").arg(i);
    QVERIFY(window.addPvs(names)); QApplication::processEvents();
    const auto rows = window.findChildren<QFrame*>("pvRow"); QCOMPARE(rows.size(), 40);
    for (int i = 0; i < 40; ++i) {
      auto* channel = inspectorAt(window, i)->channel();
      auto s = channel->snapshot; s.connected = s.readable = s.hasValue = s.numeric = true; s.values = QStringList{QString::number(i)}; s.number = i; s.sequence = 1; channel->update(s);
    }
    QTRY_COMPARE(rows.front()->findChild<QLabel*>("rowValue")->text(), QString("0"));
    QCOMPARE(rows.back()->findChild<QLabel*>("rowValue")->text(), QString("—"));
    auto* scroll = window.findChild<QScrollArea*>("comparisonScroll"); QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    QTRY_COMPARE(rows.back()->findChild<QLabel*>("rowValue")->text(), QString("39"));
    auto* timer = window.findChild<QTimer*>("navigationRenderTimer"); QVERIFY(timer && timer->isSingleShot());
    QTRY_VERIFY(!timer->isActive()); QSignalSpy idle(timer, &QTimer::timeout); QTest::qWait(320); QCOMPARE(idle.count(), 0);
    QVERIFY(!window.findChild<QTimer*>("windowFitTimer")->isActive());
    window.hide(); auto* channel = inspectorAt(window, 39)->channel();
    auto s = channel->snapshot; s.values = QStringList{"40"}; ++s.sequence; channel->update(s); QVERIFY(!timer->isActive());
    window.show(); QTRY_COMPARE(rows.back()->findChild<QLabel*>("rowValue")->text(), QString("40"));
  }
  void qtedmMiddleButtonDrop() {
    const auto qtedm = qEnvironmentVariable("QTPROBE_TEST_QTEDM"), driver = qEnvironmentVariable("QTPROBE_TEST_DRAG_DRIVER");
    if (qtedm.isEmpty() || driver.isEmpty() || QApplication::platformName() != "xcb") QSKIP("Run test-qtedm-drop under X11 with QtEDM and the mouse driver");
    Options options; options.connectOnOpen = false; Window window(options); window.move(400, 50); window.resize(900, 600); window.show();
    struct Source { QProcess process; ~Source() { process.terminate(); if (!process.waitForFinished(3000)) { process.kill(); process.waitForFinished(3000); } } } source;
    source.process.start(qtedm, {"-x", QFileInfo("tests/qtprobe_drag_source.adl").absoluteFilePath()}); QVERIFY(source.process.waitForStarted());
    auto dragTo = [&](QWidget* target) {
      QApplication::processEvents(); const auto point = target->mapTo(&window, target->rect().center());
      QProcess mouse; mouse.start(driver, {QString::number(window.winId()), QString::number(point.x()), QString::number(point.y())});
      QVERIFY(mouse.waitForStarted()); QTRY_VERIFY_WITH_TIMEOUT(mouse.state() == QProcess::NotRunning, 10000);
      QCOMPARE(mouse.exitCode(), 0);
      QTest::qWait(200);
      const auto artifacts = QCoreApplication::applicationDirPath() + "/test-artifacts/"; QDir().mkpath(artifacts);
      QApplication::primaryScreen()->grabWindow(0).save(artifacts + "qtedm-drop-screen.png");
      if (!window.inspectors()->count()) qInfo() << "QtEDM output:" << source.process.readAllStandardError() << source.process.readAllStandardOutput();
      QTRY_VERIFY_WITH_TIMEOUT(window.inspectors()->count() > 0, 3000);
      QCOMPARE(inspectorAt(window, window.inspectors()->count() - 1)->channel()->snapshot.name, QString("qtedm:drop"));
    };
    dragTo(window.findChild<QFrame*>("welcomePage")); QCOMPARE(window.inspectors()->count(), 1); window.closePv(0);
    dragTo(window.findChild<QComboBox*>("pvName")->lineEdit()); QCOMPARE(window.inspectors()->count(), 1);
    // Duplicate drop onto the inspector does not create a second subscription.
    dragTo(inspectorAt(window, 0)->findChild<QLabel*>("value")); QCOMPARE(window.inspectors()->count(), 1); window.closePv(0);
    window.addPv("ca://test:existing"); auto* inspector = inspectorAt(window, 0);
    auto s = inspector->channel()->snapshot; s.connected = s.readable = s.writable = s.hasValue = s.numeric = true; s.type = "DBF_DOUBLE"; s.values = QStringList{"1"}; inspector->channel()->update(s); inspector->refresh();
    auto* editor = inspector->findChild<QLineEdit*>("writeValue"); editor->setText("staged edit");
    QSignalSpy writes(inspector->channel(), &Channel::writeFinished); dragTo(editor);
    QCOMPARE(window.inspectors()->count(), 2); QCOMPARE(editor->text(), QString("staged edit")); QCOMPARE(writes.count(), 0);
    QCOMPARE(window.findChild<QStackedWidget*>("workspacePages")->currentIndex(), 2);
    dragTo(window.findChildren<QFrame*>("pvRow")[1]); QCOMPARE(window.inspectors()->count(), 2);
    dragTo(window.findChild<QTabBar*>("pvTabs")); QCOMPARE(window.inspectors()->count(), 2);
  }
  void motifMiddleButtonDrop() {
    const auto source = qEnvironmentVariable("QTPROBE_TEST_MOTIF_SOURCE");
    if (source.isEmpty() || QApplication::platformName() != "xcb") QSKIP("Run test-motif under X11 with the Motif drag source helper");
    Options options; options.connectOnOpen = false; Window window(options); window.move(300, 100); window.show();
    // The helper drops at (300, 200) in the window; keep it that large once the
    // compact empty window has fitted itself.
    QTest::qWait(200); window.resize(700, 420); QTest::qWait(200);
    // The helper exits 0 for an accepted drop, 1 for a refused one, 2 on timeout.
    auto drop = [&](const QString& pv) -> int {
      QProcess sender; sender.start(source, {QString::number(window.winId()), pv});
      if (!sender.waitForStarted() || !QTest::qWaitFor([&] { return sender.state() == QProcess::NotRunning; }, 10000)) return -1;
      return sender.exitCode();
    };
    // Legacy MEDM sends a plain name through XmDragStart and COMPOUND_TEXT.
    QCOMPARE(drop("motif:one"), 0); QTRY_COMPARE(window.inspectors()->count(), 1);
    QCOMPARE(inspectorAt(window, 0)->channel()->snapshot.name, QString("motif:one"));
    QCOMPARE(drop("motif:one"), 0); QCOMPARE(window.inspectors()->count(), 1);
    window.findChild<QComboBox*>("providerSelector")->setCurrentText("pva");
    QCOMPARE(drop("motif:two"), 0); QTRY_COMPARE(window.inspectors()->count(), 2);
    QCOMPARE(inspectorAt(window, 1)->channel()->snapshot.provider, QString("pva"));
    // An invalid name is refused, so MEDM reports a failed drop.
    QCOMPARE(drop("http://bad"), 1); QCOMPARE(window.inspectors()->count(), 2);
  }
  void helpTopicsSearchNavigationAndReuse() {
    Options options; options.connectOnOpen = false; Window window(options); window.show();
    auto* action = window.findChild<QAction*>("helpAction"); QVERIFY(action);
    QVERIFY(action->shortcuts().contains(QKeySequence(Qt::Key_F1)));
    QVERIFY(window.findChild<QAction*>("keyboardShortcutsAction"));
    QVERIFY(window.findChild<QAction*>("manualAction"));
    QVERIFY(window.findChild<QAction*>("aboutAction"));
    window.activateWindow(); QTest::qWait(20); QTest::keyClick(&window, Qt::Key_F1);
    auto* help = window.findChild<QDialog*>("helpWindow"); QVERIFY(help && help->isVisible());
    QVERIFY(!help->isModal());
    auto* topics = help->findChild<QListWidget*>("helpTopics");
    auto* browser = help->findChild<QTextBrowser*>("helpBrowser");
    auto* find = help->findChild<QLineEdit*>("helpFind");
    auto* matches = help->findChild<QLabel*>("helpMatches");
    auto* next = help->findChild<QToolButton*>("helpNext");
    auto* previous = help->findChild<QToolButton*>("helpPrevious");
    QVERIFY(topics && browser && find && matches && next && previous); QVERIFY(topics->count() >= 10);
    QCOMPARE(topics->currentItem()->data(Qt::UserRole).toString(), QString("getting-started"));
    QVERIFY(browser->toPlainText().contains("QTPROBE_HELP_PATH")); QVERIFY(!next->isEnabled());
    find->setText("PVACCESS"); const int count = browser->extraSelections().size(); QVERIFY(count > 3);
    const int start = browser->textCursor().position(); next->click();
    QVERIFY(browser->textCursor().position() != start); previous->click(); QCOMPARE(browser->textCursor().position(), start);
    find->setFocus(); QTest::keyClick(find, Qt::Key_Return);
    QVERIFY(help->isVisible()); QVERIFY(browser->textCursor().position() != start);
    QTest::keyClick(find, Qt::Key_Return, Qt::ShiftModifier); QCOMPARE(browser->textCursor().position(), start);
    for (int i = 0; i < count; ++i) next->click();
    QCOMPARE(browser->textCursor().position(), start);
    find->setText("pvaccess"); QCOMPARE(browser->extraSelections().size(), count);
    find->setText("no-such-qtprobe-help-phrase-123"); QCOMPARE(matches->text(), QString("No matches"));
    QVERIFY(browser->extraSelections().isEmpty()); QVERIFY(!browser->textCursor().hasSelection()); QVERIFY(!next->isEnabled());
    find->clear(); QVERIFY(matches->text().isEmpty());
    window.findChild<QAction*>("keyboardShortcutsAction")->trigger();
    QCOMPARE(window.findChild<QDialog*>("helpWindow"), help);
    QCOMPARE(topics->currentItem()->data(Qt::UserRole).toString(), QString("keyboard-shortcuts"));
    topics->setCurrentRow(2); QVERIFY(browser->verticalScrollBar()->value() > 0);
    browser->verticalScrollBar()->setValue(0); QCOMPARE(topics->currentRow(), 0);
    const auto phrase = browser->document()->find("check the PV spelling");
    QVERIFY(!phrase.isNull()); QVERIFY(phrase.charFormat().fontWeight() < QFont::DemiBold);
    // Markdown links to other topics remain in the help browser.
    browser->anchorClicked(QUrl("#array-inspection"));
    QCOMPARE(topics->currentItem()->data(Qt::UserRole).toString(), QString("array-inspection"));
    help->activateWindow(); QTest::qWait(20); QTest::keyClick(browser, Qt::Key_F, Qt::ControlModifier);
    QTRY_VERIFY(find->hasFocus()); find->setText("pvaccess");
    const int shortcutStart = browser->textCursor().position();
    QTest::keyClick(find, Qt::Key_F3); QVERIFY(browser->textCursor().position() != shortcutStart);
    QTest::keyClick(find, Qt::Key_F3, Qt::ShiftModifier); QCOMPARE(browser->textCursor().position(), shortcutStart);
    QDir().mkpath(QCoreApplication::applicationDirPath() + "/test-artifacts");
    QVERIFY(help->grab().save(QCoreApplication::applicationDirPath() + "/test-artifacts/help-search.png"));
    find->clear(); showHelp(&window, "getting-started"); QApplication::processEvents();
    QVERIFY(help->grab().save(QCoreApplication::applicationDirPath() + "/test-artifacts/help-topics.png"));
    QTest::keyClick(find, Qt::Key_Escape); QVERIFY(!help->isVisible());
    action->trigger(); QVERIFY(help->isVisible()); QCOMPARE(window.findChildren<QDialog*>("helpWindow").size(), 1);
    help->findChild<QPushButton*>("helpClose")->click(); QVERIFY(!help->isVisible());
  }
  void manualUsesEmbeddedGuideAndSiteOverride() {
    HelpPathScope restore; UrlCapture capture; qunsetenv("QTPROBE_HELP_PATH");
    Options options; options.connectOnOpen = false; Window window(options);
    auto* action = window.findChild<QAction*>("manualAction"); QVERIFY(action); action->trigger();
    QCOMPARE(capture.urls.size(), 1); QVERIFY(capture.urls.back().isLocalFile());
    QCOMPARE(capture.urls.back(), helpManualUrl());
    QFile manual(capture.urls.back().toLocalFile()); QVERIFY(manual.open(QIODevice::ReadOnly));
    const auto html = manual.readAll();
    QVERIFY(html.contains("QtProbe User Manual")); QVERIFY(html.contains("pvAccess"));
    QVERIFY(html.contains("name=\"keyboard-shortcuts\"")); QVERIFY(html.contains("QTPROBE_HELP_PATH"));
    qputenv("QTPROBE_HELP_PATH", "https://example.org/manuals/QtProbe/");
    QCOMPARE(helpManualUrl(), QUrl("https://example.org/manuals/QtProbe/"));
    action->trigger(); QCOMPARE(capture.urls.size(), 2); QCOMPARE(capture.urls.back(), helpManualUrl());
    auto* help = showHelp(&window); help->findChild<QPushButton*>("helpManual")->click();
    QCOMPARE(capture.urls.size(), 3); QCOMPARE(capture.urls.back(), helpManualUrl()); help->close();
    const auto local = settings.filePath("site manual.html"); qputenv("QTPROBE_HELP_PATH", local.toUtf8());
    QCOMPARE(helpManualUrl(), QUrl::fromLocalFile(local));
  }
  void aboutCreditsAnimationAndHelpHandoff() {
    Options options; options.connectOnOpen = false; Window window(options);
    QString title, credits, footer; bool animated = false, saved = false;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = window.findChild<QDialog*>("aboutDialog"); if (!dialog) return;
      title = dialog->findChild<QLabel*>("aboutTitle")->text();
      credits = dialog->findChild<QTextBrowser*>("aboutCredits")->toPlainText();
      footer = dialog->findChild<QLabel*>("aboutFooter")->text();
      animated = dialog->findChild<QTimer*>("aboutAnimationTimer")->isActive();
      saved = dialog->grab().save(QCoreApplication::applicationDirPath() + "/test-artifacts/about.png");
      dialog->findChild<QPushButton*>("aboutOpenHelp")->click();
    });
    window.findChild<QAction*>("aboutAction")->trigger();
    QCOMPARE(title, QString("QtProbe"));
    for (auto name : {"Robert Soliday", "Fred Vong", "Ken Evans"}) QVERIFY(credits.contains(name));
    QVERIFY(footer.contains(qVersion())); QVERIFY(footer.contains("EPICS")); QVERIFY(animated); QVERIFY(saved);
    auto* help = window.findChild<QDialog*>("helpWindow"); QVERIFY(help && help->isVisible());
    QVERIFY(!QApplication::activeModalWidget()); help->close();
    QVERIFY(!window.findChild<QDialog*>("aboutDialog"));
  }
  void bannerStopsWhenHiddenAndRespondsToKeyboard() {
    AboutBanner banner; banner.show(); auto* timer = banner.findChild<QTimer*>("aboutAnimationTimer");
    QVERIFY(timer && timer->isActive()); QVERIFY(!banner.scanning());
    QTest::mouseClick(&banner, Qt::LeftButton); QVERIFY(banner.scanning()); QVERIFY(!banner.startScan());
    banner.advance(3.3); QVERIFY(!banner.scanning());
    QTest::keyClick(&banner, Qt::Key_Space); QVERIFY(banner.scanning());
    banner.hide(); QVERIFY(!timer->isActive()); QVERIFY(!banner.scanning());
    banner.show(); QVERIFY(timer->isActive());
  }
};
QTEST_MAIN(UiTest)
#include "test_ui.moc"
