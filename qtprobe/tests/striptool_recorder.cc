// Native stand-in for QtStripTool: record the generated configuration without
// requiring a shell script or launching the real plotting application.
#include <QCoreApplication>
#include <QFile>

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto output = qEnvironmentVariable("QTPROBE_TEST_STRIPTOOL_OUTPUT");
  const auto args = app.arguments();
  return args.size() == 2 && !output.isEmpty() && QFile::copy(args[1], output) ? 0 : 1;
}
