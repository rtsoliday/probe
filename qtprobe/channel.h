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
  // Writes 1 to the record's PROC field (CA).
  void process();
  // Client time of the last value update, in ms since the epoch; 0 if none.
  qint64 lastUpdateMs = 0;
  // Backends only call these on the Qt thread.
  void update(Snapshot);
  void completeWrite(bool, QString);
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
};
std::unique_ptr<Connection> createCa(Channel&, const Address&, int);
std::unique_ptr<Connection> createPva(Channel&, const Address&, int);
} // namespace probe
