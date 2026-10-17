// Opt-in CPU measurements, kept out of correctness tests: timings depend on
// the host, Qt backend and IOC. Includes all threads in process CPU time.
#include "inspector.h"
#include <QtWidgets>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <ctime>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
#include <vector>
using namespace probe;
// MSVC's clock() measures elapsed time, not process CPU time. Include all
// threads on both platforms so idle Windows clients do not appear 100% busy.
double processCpuMs() {
#ifdef Q_OS_WIN
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    qFatal("Cannot read process CPU time");
  auto ticks = [](FILETIME time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
  return double(ticks(kernel) + ticks(user)) / 10000.0;
#else
  return 1000.0 * std::clock() / CLOCKS_PER_SEC;
#endif
}
double maxRssMb() {
#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS memory{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof memory))
    qFatal("Cannot read peak process memory");
  return memory.PeakWorkingSetSize / 1048576.0;
#else
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage)) qFatal("Cannot read peak process memory");
#ifdef Q_OS_MACOS
  return usage.ru_maxrss / 1048576.0; // Darwin reports bytes; Linux reports KiB.
#else
  return usage.ru_maxrss / 1024.0;
#endif
#endif
}
class MeasuredApplication : public QApplication {
public:
  using QApplication::QApplication;
  bool profile = false;
  QMap<QString, double> eventCpu;
  bool notify(QObject* receiver, QEvent* event) override {
    if (!profile || (event->type() != QEvent::Paint && event->type() != QEvent::Timer && event->type() != QEvent::LayoutRequest && event->type() != QEvent::UpdateRequest)) return QApplication::notify(receiver, event);
    const auto key = QString::number(int(event->type())) + ':' + receiver->metaObject()->className() + ':' + receiver->objectName();
    const auto start = processCpuMs(); const bool result = QApplication::notify(receiver, event);
    eventCpu[key] += processCpuMs() - start; return result;
  }
};
int main(int argc, char** argv) {
  MeasuredApplication app(argc, argv); app.setStyle("Fusion");
  QCoreApplication::setOrganizationName("QtProbeBenchmark");
  QTemporaryDir settings;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  QCommandLineParser parser; parser.addHelpOption();
  parser.addOption({"provider", "ca, pva, or synthetic (no network)", "provider", "synthetic"});
  parser.addOption({"pvs", "Number of PVs (0..100)", "count", "100"});
  parser.addOption({"prefix", "Prefix of test IOC records", "prefix", "QtProbeBenchmark:"});
  parser.addOption({"period", "Synthetic update interval; zero means static", "ms", "100"});
  parser.addOption({"duration", "Measurement duration", "ms", "4000"});
  parser.addOption({"history", "Preload this many synthetic history events per PV", "count", "0"});
  parser.addOption({"view", "all, inspector, trend, history, or hidden", "view", "all"});
  parser.addOption({"profile-events", "Include inclusive paint/timer/layout CPU timings (diagnostic overhead)."});
  parser.addOption({"screenshot", "Save the final workspace after measuring", "path"});
  parser.addOption({"history-memory", "History memory limit in MB", "MB", "256"});
  parser.process(app);
  const auto provider = parser.value("provider"), view = parser.value("view");
  const int count = parser.value("pvs").toInt(), period = parser.value("period").toInt();
  const int duration = parser.value("duration").toInt(), preload = parser.value("history").toInt();
  if (count < 0 || count > 100 || period < 0 || duration < 100 || preload < 0 || preload > 1000000 ||
      (provider != "ca" && provider != "pva" && provider != "synthetic") ||
      (view != "all" && view != "inspector" && view != "trend" && view != "history" && view != "hidden")) return 2;
  History::setMemoryBudget(size_t(std::max(16, parser.value("history-memory").toInt())) << 20);
  Options options; options.connectOnOpen = provider != "synthetic"; options.readOnly = true;
  options.historySize = std::max(10000, preload);
  Window window(options); window.resize(1280, 940);
  QStringList names; for (int i = 0; i < count; ++i) names << (provider == "pva" ? "pva://" : "ca://") + parser.value("prefix") + QString::number(i);
  if (count && !window.addPvs(names)) return 2;
  std::vector<Channel*> channels;
  for (int i = 0; i < count; ++i) channels.push_back(static_cast<Inspector*>(window.inspectors()->widget(i))->channel());
  auto update = [&] {
    for (auto* c : channels) {
      auto s = c->snapshot; s.connected = s.readable = s.hasValue = s.numeric = true;
      s.type = "DBF_DOUBLE"; s.count = s.capacity = 1; s.units = "mA";
      s.number = 200 + std::sin(s.sequence * 0.1); s.values = QStringList{QString::number(s.number, 'g', 17)};
      s.seconds = 1792166400 + s.sequence; ++s.sequence; c->update(s);
    }
  };
  if (provider == "synthetic") for (int i = 0; i < std::max(1, preload); ++i) update();
  window.show();
  if (count) {
    window.findChild<QAction*>(view == "all" || view == "hidden" ? "allPvsAction" : "inspectorAction")->trigger();
    window.setSections(view == "trend" ? TrendSection : view == "history" ? HistorySection : 0);
  }
  QEventLoop warmup;
  QTimer::singleShot(2000, &warmup, &QEventLoop::quit); warmup.exec();
  if (view == "hidden") window.hide();
  for (auto* c : channels) if (!c->snapshot.connected || !c->snapshot.hasValue) {
    QTextStream(stderr) << "Benchmark PV not ready: " << c->snapshot.provider << "://" << c->snapshot.name << ' ' << c->snapshot.error << '\n'; return 3;
  }
  quint64 beforeSequence = 0; for (auto* c : channels) beforeSequence += c->snapshot.sequence;
  QTimer source; source.setTimerType(Qt::PreciseTimer);
  if (provider == "synthetic" && period) { QObject::connect(&source, &QTimer::timeout, &app, update); source.start(period); }
  app.profile = parser.isSet("profile-events");
  const auto beforeCpu = processCpuMs(); QElapsedTimer elapsed; elapsed.start();
  QEventLoop measurement; QTimer::singleShot(duration, &measurement, &QEventLoop::quit); measurement.exec();
  const auto cpuMs = processCpuMs() - beforeCpu; const auto wallMs = elapsed.elapsed();
  quint64 afterSequence = 0; for (auto* c : channels) afterSequence += c->snapshot.sequence;
  QJsonObject result{{"provider", provider}, {"pvs", count}, {"view", view}, {"history_preload", preload},
      {"wall_ms", wallMs}, {"cpu_ms", cpuMs}, {"cpu_percent_one_core", 100 * cpuMs / wallMs},
      {"delivered_updates", double(afterSequence - beforeSequence)}, {"qt_platform", QApplication::platformName()},
      {"history_mb", History::totalBytes() / 1048576.0}, {"max_rss_mb", maxRssMb()}};
  app.profile = false;
  if (parser.isSet("profile-events")) { QJsonObject events; for (auto it = app.eventCpu.cbegin(); it != app.eventCpu.cend(); ++it) events[it.key()] = it.value(); result["inclusive_event_cpu_ms"] = events; }
  QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
  if (parser.isSet("screenshot") && !window.grab().save(parser.value("screenshot"))) return 4;
  return 0;
}
