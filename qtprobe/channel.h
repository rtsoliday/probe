#pragma once
#include "model.h"
#include <QObject>
#include <QTimer>
#include <memory>
#include <stdexcept>
namespace probe {
class Connection {
public:
  virtual ~Connection() = default;
  virtual void poll() = 0;
  virtual void write(const QStringList&) = 0;
  virtual void process() { throw std::runtime_error("Record processing is available for Channel Access PVs only"); }
  // Reads the current value again, for fields the server does not post.
  virtual void refetch() {}
  // Ignores further events; the connection only stays open until released.
  virtual void detach() {}
};
class Channel final : public QObject {
  Q_OBJECT
public:
  Channel(Address, bool readOnly = false, int maxElements = 16384,
          int historySize = 10000, double timeout = 5, QObject* parent = nullptr);
  ~Channel() override;
  Snapshot snapshot;
  History history;
  bool readOnly() const { return readOnly_; }
  // Field channels (Record section): no companion DESC, RTYP or PROC channels.
  bool lightweight = false;
  void start();
  void stop();
  void write(const QString&);
  // Writes element values as given, without parsing text (Undo uses this).
  void writeValues(const QStringList&);
  // Writes 1 to the record's PROC field (CA).
  void process();
  // Reads the current value again (IOCs post no monitors for PACT, UDF, or DISA set through SDIS).
  void refetch();
  // Client time of the last value update, in ms since the epoch; 0 if none.
  qint64 lastUpdateMs = 0;
  // Backends only call these on the Qt thread.
  void update(Snapshot);
  // An uncertain outcome (timeout, disconnect) keeps writes disabled until start().
  void completeWrite(bool, QString, bool uncertain = false);
  // Thread-safe delivery request. Bursts share one pending, bounded-rate drain.
  void requestPoll();
  bool writePending() const { return writePending_; }
  bool writeUncertain() const { return writeUncertain_; }
signals:
  void changed();
  void writeFinished(bool ok, QString message);
private:
  Address address;
  bool readOnly_, writePending_ = false, writeUncertain_ = false;
  int maxElements_;
  double timeout_;
  QTimer timer, deadline, writeDeadline;
  std::unique_ptr<Connection> connection;
  // A detached CA connection kept by start() until the new one connects, so a
  // circuit used only by this PV stays up instead of being reused while closing.
  std::unique_ptr<Connection> previous;
};
std::unique_ptr<Connection> createCa(Channel&, const Address&, int);
std::unique_ptr<Connection> createPva(Channel&, const Address&, int);
} // namespace probe
