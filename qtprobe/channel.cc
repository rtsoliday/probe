#include "channel.h"
#include <stdexcept>
#include <QDateTime>
#include <QThread>
namespace probe {
Channel::Channel(Address a, bool ro, int maxElements, int historySize, double timeout, QObject* p)
    : QObject(p), history(size_t(historySize)), address(a), readOnly_(ro),
      maxElements_(maxElements), timeout_(timeout) {
  snapshot.name = a.name; snapshot.provider = a.provider;
  timer.setInterval(25); timer.setSingleShot(true);
  connect(&timer, &QTimer::timeout, this, [this] {
    try { if (connection) connection->poll(); }
    catch (const std::exception& e) { snapshot.error = QString::fromLocal8Bit(e.what()); emit changed(); }
  });
  deadline.setSingleShot(true);
  connect(&deadline, &QTimer::timeout, this, [this] {
    previous.reset();
    // A specific error (for example read access denied) says more than a timeout.
    if (!snapshot.hasValue && snapshot.error.isEmpty()) {
      snapshot.error = "Connection/read timeout; discovery continues automatically.";
      emit changed();
    }
  });
  writeDeadline.setSingleShot(true);
  connect(&writeDeadline, &QTimer::timeout, this, [this] {
    // While connected, a CA put-callback completes when record processing
    // does (a long motor move); keep waiting. A disconnect completes it.
    if (snapshot.connected && connection) { writeSlow_ = true; emit changed(); return; }
    completeWrite(false, "Write completion timed out; outcome unknown. Check readback and reconnect before retrying.", true);
  });
}
Channel::~Channel() { timer.stop(); connection.reset(); previous.reset(); }
void Channel::start() {
  // Make before break for CA: clearing the PV's last channel on a circuit
  // closes it, and CA could attach the new channel to the closing circuit and
  // leave it disconnected for up to 10 s.
  std::unique_ptr<Connection> keep;
  if (address.provider != "pva" && connection) { connection->detach(); keep = std::move(connection); }
  stop();
  previous = std::move(keep);
  writeUncertain_ = false;
  snapshot = {}; snapshot.name = address.name; snapshot.provider = address.provider;
  emit changed();
  try {
    connection = address.provider == "pva" ? createPva(*this, address, maxElements_) : createCa(*this, address, maxElements_);
    deadline.start(int(timeout_ * 1000)); requestPoll();
  } catch (const std::exception& e) { snapshot.error = QString::fromLocal8Bit(e.what()); emit changed(); }
}
void Channel::requestPoll() {
  if (QThread::currentThread() != thread()) {
    QMetaObject::invokeMethod(this, [this] { requestPoll(); }, Qt::QueuedConnection); return;
  }
  if (connection && !timer.isActive()) timer.start();
}
void Channel::stop() {
  timer.stop(); deadline.stop(); writeDeadline.stop();
  const bool searching = bool(connection);
  connection.reset(); previous.reset();
  if (writePending_) completeWrite(false, "Disconnected before write completion; outcome unknown.", true);
  if (snapshot.connected) {
    auto s = snapshot; s.connected = s.readable = s.writable = false; s.error = "Stopped"; update(s);
  } else if (searching) { snapshot.error = "Stopped"; emit changed(); }  // no longer "reconnecting automatically"
}
void Channel::update(Snapshot s) {
  const bool event = (s.hasValue && s.sequence != snapshot.sequence) || s.connected != snapshot.connected || s.readable != snapshot.readable;
  if (s.hasValue) deadline.stop();
  if (s.connected) previous.reset();
  if (s.bytes) s.decodeBytes();
  if (event && s.hasValue && s.sequence != snapshot.sequence) lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
  snapshot = std::move(s);
  if (event) history.append(snapshot);
  emit changed();
}
void Channel::write(const QString& text) {
  if (readOnly_ || !snapshot.connected || !snapshot.writable || !connection || writePending_ || writeUncertain_) {
    emit writeFinished(false, "Write unavailable: read-only, disconnected, denied, pending or uncertain completion."); return;
  }
  QString error;
  const auto values = parseWrite(snapshot, text, &error);
  if (!error.isEmpty()) { emit writeFinished(false, error); return; }
  writeValues(values);
}
void Channel::writeValues(const QStringList& values) {
  if (readOnly_ || !snapshot.connected || !snapshot.writable || !connection || writePending_ || writeUncertain_ || values.isEmpty()) {
    emit writeFinished(false, "Write unavailable: read-only, disconnected, denied, pending or uncertain completion."); return;
  }
  // pvAccess keeps the previous type and states until the first value after a
  // reconnect; text parsed against them could select another state.
  if (address.provider == "pva" && !snapshot.hasValue) {
    emit writeFinished(false, "Write unavailable: waiting for the PV's first value after connecting."); return;
  }
  for (const auto& value : values) if (value.contains(QChar(0))) {
    emit writeFinished(false, "Text elements cannot contain embedded NUL characters."); return;
  }
  writePending_ = true;
  writeDeadline.start(10000);
  try { connection->write(values); }
  catch (const std::exception& e) { completeWrite(false, QString::fromLocal8Bit(e.what())); }
  emit changed();
}
void Channel::refetch() {
  if (!connection || !snapshot.connected) return;
  try { connection->refetch(); } catch (const std::exception& e) { snapshot.error = QString::fromLocal8Bit(e.what()); emit changed(); }
}
void Channel::process() {
  if (readOnly_ || !snapshot.connected || !snapshot.processable || !connection || writePending_ || writeUncertain_) {
    emit writeFinished(false, "Processing unavailable: read-only, disconnected, denied, pending or uncertain completion."); return;
  }
  writePending_ = true;
  writeDeadline.start(10000);
  try { connection->process(); }
  catch (const std::exception& e) { completeWrite(false, QString::fromLocal8Bit(e.what())); }
  emit changed();
}
void Channel::completeWrite(bool ok, QString message, bool uncertain) {
  if (!writePending_) return;
  writePending_ = writeSlow_ = false; writeDeadline.stop(); writeUncertain_ = writeUncertain_ || uncertain;
  emit writeFinished(ok, message.isEmpty() ? (ok ? "Write completed" : "Write failed") : message);
  emit changed();
}
} // namespace probe
