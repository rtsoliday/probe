#include "inspector.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
using namespace probe;
int main(int argc, char** argv) {
  bool headless = false, explicitStyle = false, hasSession = false;
  // Qt reserves -session for X11 session management and removes it with its
  // value, so take QtProbe's --session file before Qt sees the arguments.
  QString sessionFile;
  std::vector<char*> args{argv[0]};
  for (int i = 1; i < argc; ++i) {
    const auto arg = QByteArray(argv[i]);
    if ((arg == "--session" || arg == "-session") && i + 1 < argc) { hasSession = true; sessionFile = QString::fromLocal8Bit(argv[++i]); continue; }
    if (arg.startsWith("--session=") || arg.startsWith("-session=")) { hasSession = true; sessionFile = QString::fromLocal8Bit(arg.mid(arg.indexOf('=') + 1)); continue; }
    if (arg == "-style" || arg.startsWith("-style=")) explicitStyle = true;
    if (arg == "--info" || arg == "-info" || arg == "--json" || arg == "-json" || arg == "--help" || arg == "--help-all" || arg == "-h" || arg == "--version" || arg == "-v") headless = true;
    args.push_back(argv[i]);
  }
  int count = int(args.size()); args.push_back(nullptr);
  std::unique_ptr<QCoreApplication> app;
  if (headless) app = std::make_unique<QCoreApplication>(count, args.data());
  else app = std::make_unique<QApplication>(count, args.data());
  // Qt adopts the user's locale, but EPICS parses numbers with the C library
  // (pvData puts, CA environment settings), so keep C numeric conventions.
  std::setlocale(LC_NUMERIC, "C");
  QCoreApplication::setApplicationName("qtprobe");
  QCoreApplication::setApplicationVersion("0.1.0");
  QCoreApplication::setOrganizationName("EPICS");
  QCommandLineParser parser;
  parser.setApplicationDescription("QtProbe — EPICS Channel Access and pvAccess inspector");
  parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
  parser.addHelpOption(); parser.addVersionOption();
  parser.addOption({"provider", "Default provider: ca or pva (URI prefixes override it).", "provider", "ca"});
  parser.addOption({{"w", "timeout"}, "Connection/read timeout in seconds.", "seconds", "5"});
  parser.addOption({"read-only", "Disable all writes in this process."});
  parser.addOption({"max-elements", "Maximum retained array elements (1–1000000).", "count", "16384"});
  parser.addOption({"history", "Maximum retained events per PV (1–1000000).", "count", "10000"});
  parser.addOption({"history-memory", "Memory limit for all retained events, in MB (16–65536).", "MB", "256"});
  parser.addOption({"info", "Print one metadata snapshot per PV without a display, then exit."});
  parser.addOption({"json", "Use JSON output with --info; values are exact strings."});
  parser.addOption({"session", "Open a QtProbe .qtp session.", "file"});
  parser.addPositionalArgument("PV", "PV names, ca://PV or pva://PV.", "[PV…]");
  parser.process(*app);
  Options options; options.provider = parser.value("provider"); options.readOnly = parser.isSet("read-only");
  bool timeoutOK, maxOK, historyOK, memoryOK;
  const int historyMemory = parser.value("history-memory").toInt(&memoryOK);
  options.timeout = parser.value("timeout").toDouble(&timeoutOK);
  options.maxElements = parser.value("max-elements").toInt(&maxOK);
  options.historySize = parser.value("history").toInt(&historyOK);
  if (!timeoutOK || !std::isfinite(options.timeout) || options.timeout <= 0 || options.timeout > 3600 ||
      !maxOK || options.maxElements < 1 || options.maxElements > 1000000 ||
      !historyOK || options.historySize < 1 || options.historySize > 1000000 ||
      !memoryOK || historyMemory < 16 || historyMemory > 65536 ||
      (options.provider != "ca" && options.provider != "pva") || (parser.isSet("json") && !parser.isSet("info"))) {
    QTextStream(stderr) << "Invalid provider, timeout, element/history/memory limit, or --json without --info.\n"; return 2;
  }
  History::setMemoryBudget(size_t(historyMemory) << 20);
  const auto names = parser.positionalArguments();
  try { for (const auto& name : names) Address::parse(name, options.provider); }
  catch (const std::exception& e) { QTextStream(stderr) << e.what() << '\n'; return 2; }
  if (parser.isSet("info")) {
    if (names.isEmpty() || hasSession || parser.isSet("session")) { QTextStream(stderr) << "--info requires PV names and cannot open a session.\n"; return 2; }
    // EPICS libraries print diagnostics on stdout (pvAccess uses printf).
    // Send them to stderr so stdout carries only the report.
    QFile report;
#ifdef Q_OS_UNIX
    std::fflush(stdout);
    const int reportFd = dup(STDOUT_FILENO);
    if (reportFd >= 0 && dup2(STDERR_FILENO, STDOUT_FILENO) >= 0) report.open(reportFd, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle);
    else if (reportFd >= 0) close(reportFd);
#endif
    if (!report.isOpen()) report.open(stdout, QIODevice::WriteOnly);
    std::vector<std::unique_ptr<Channel>> channels;
    QTimer deadline, ready;
    bool expired = false;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, app.get(), [&] { expired = true; app->quit(); });
    QObject::connect(&ready, &QTimer::timeout, app.get(), [&] {
      for (const auto& c : channels)
        if (!c->snapshot.connected || !c->snapshot.hasValue || !c->snapshot.metadataReady) return;
      app->quit();
    });
    for (const auto& name : names) {
      auto c = std::make_unique<Channel>(Address::parse(name, options.provider), true, options.maxElements, 1, options.timeout);
      c->start(); channels.push_back(std::move(c));
    }
    deadline.start(int(options.timeout * 1000)); ready.start(100); app->exec();
    QJsonArray json;
    bool failed = false;
    for (const auto& c : channels) {
      auto s = c->snapshot;
      if (!s.connected || !s.hasValue || !s.metadataReady) {
        failed = true; if (s.error.isEmpty()) s.error = expired ? "Timed out waiting for PV data/metadata" : "PV unavailable";
      }
      if (parser.isSet("json")) json.append(s.json());
      else {
        QTextStream output(&report); output << s.name << '\n'; const auto fields = s.information();
        for (auto it = fields.cbegin(); it != fields.cend(); ++it) output << "  " << it.key() << ": " << it.value() << '\n';
        output << "  Value: " << s.valueText() << "\n\n";
      }
    }
    if (parser.isSet("json")) QTextStream(&report) << QJsonDocument(json).toJson();
    return failed ? 1 : 0;
  }
  if (!explicitStyle) QApplication::setStyle("Fusion");
  Window window(options);
  if (hasSession || parser.isSet("session")) try { window.loadSession(hasSession ? sessionFile : parser.value("session")); }
  catch (const std::exception& e) { QTextStream(stderr) << e.what() << '\n'; return 2; }
  if (!names.isEmpty()) window.addPvs(names);
  window.show(); return app->exec();
}
