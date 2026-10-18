// PVA worker callbacks copy bounded snapshots into an inbox; Qt owns all UI.
#include "channel.h"
#include <stdexcept>
#if QTPROBE_HAVE_PVA
#include <pv/clientFactory.h>
#include <pv/createRequest.h>
#include <pv/pvAccess.h>
#include <pv/pvData.h>
#include <cmath>
#include <type_traits>
#include <deque>
#include <mutex>
#include <sstream>
#include <functional>
namespace pva = epics::pvAccess;
namespace pvd = epics::pvData;
namespace probe {
namespace {
QString scalarText(const pvd::PVScalar& value) {
  switch (value.getScalar()->getScalarType()) {
  case pvd::pvFloat: return QString::number(value.getAs<float>(), 'g', 9);
  case pvd::pvDouble: return QString::number(value.getAs<double>(), 'g', 17);
  default: return QString::fromStdString(value.getAs<std::string>());
  }
}
template<class T> void arrayValues(const pvd::PVScalarArray& value, QStringList& out, int limit) {
  const auto& array = static_cast<const pvd::PVValueArray<T>&>(value);
  const auto data = array.view();
  for (size_t i = 0; i < std::min<size_t>(data.size(), size_t(limit)); ++i) {
    if constexpr (std::is_same_v<T, std::string>) out << QString::fromStdString(data[i]);
    else if constexpr (std::is_floating_point_v<T>) out << QString::number(data[i], 'g', sizeof(T) == 4 ? 9 : 17);
    else if constexpr (std::is_signed_v<T>) out << QString::number(qlonglong(data[i]));
    else out << QString::number(qulonglong(data[i]));
  }
}
void arrayText(const pvd::PVScalarArray& array, QStringList& out, int limit) {
  switch (array.getScalarArray()->getElementType()) {
#define ARRAY(KIND, TYPE) case pvd::KIND: arrayValues<TYPE>(array, out, limit); break
  ARRAY(pvBoolean, pvd::boolean); ARRAY(pvByte, pvd::int8); ARRAY(pvUByte, pvd::uint8);
  ARRAY(pvShort, pvd::int16); ARRAY(pvUShort, pvd::uint16);
  ARRAY(pvInt, pvd::int32); ARRAY(pvUInt, pvd::uint32);
  ARRAY(pvLong, pvd::int64); ARRAY(pvULong, pvd::uint64);
  ARRAY(pvFloat, float); ARRAY(pvDouble, double); ARRAY(pvString, std::string);
#undef ARRAY
  }
}
// Structure browser bounds: fields in one tree and elements listed per array.
constexpr int treeFields = 4000, treeElements = 100;
const FieldNode* member(const FieldNode& node, const char* name) {
  for (const auto& child : node.children) if (child.name == QLatin1String(name)) return &child;
  return nullptr;
}
// One-line summaries let collapsed metadata read at a glance.
QString summary(const FieldNode& node) {
  if (node.type == "enum_t") {
    const auto* index = member(node, "index"); const auto* choices = member(node, "choices");
    if (!index) return {};
    bool ok; const int i = index->value.toInt(&ok);
    return choices && ok && i >= 0 && i < choices->children.size() ? QString("(%1) %2").arg(i).arg(choices->children[i].value) : '(' + index->value + ')';
  }
  // Alarm and time structures are recognized by their members: servers do not
  // all give them the alarm_t and time_t IDs.
  if (const auto* severity = member(node, "severity"); severity && member(node, "status")) {
    const auto* message = member(node, "message");
    const QStringList names{"NO_ALARM", "MINOR", "MAJOR", "INVALID", "UNDEFINED"};
    const QString level = names.value(severity->value.toInt(), severity->value);
    return message && !message->value.isEmpty() && message->value != level ? level + " · " + message->value : level;
  }
  if (const auto* seconds = member(node, "secondsPastEpoch")) {
    const auto* nanoseconds = member(node, "nanoseconds");
    return epicsTimestamp(seconds->value.toLongLong(), nanoseconds ? nanoseconds->value.toInt() : 0);
  }
  if (const auto* value = member(node, "value")) return value->value;
  return {};
}
FieldNode tree(const pvd::PVField* field, const QString& name, int& budget) {
  FieldNode n; n.name = name; --budget;
  if (!field) { n.type = "(null)"; return n; }
  n.type = QString::fromStdString(field->getField()->getID());
  // Adds children until the field budget runs out; the rest are counted.
  auto add = [&](size_t total, const std::function<FieldNode(size_t)>& make) {
    for (size_t i = 0; i < total; ++i) {
      if (budget <= 0) { n.omitted = total - i; return; }
      n.children.append(make(i));
    }
  };
  switch (field->getField()->getType()) {
  case pvd::scalar: n.value = scalarText(static_cast<const pvd::PVScalar&>(*field)); break;
  case pvd::scalarArray: {
    const auto& array = static_cast<const pvd::PVScalarArray&>(*field);
    n.kind = FieldNode::Array; n.length = array.getLength();
    QStringList values; arrayText(array, values, int(std::min<size_t>(n.length, treeElements)));
    n.value = '[' + QStringList(values.mid(0, 16)).join(", ") + (n.length > 16 ? ", …]" : "]");
    const auto element = QString::fromLatin1(pvd::ScalarTypeFunc::name(array.getScalarArray()->getElementType()));
    add(size_t(values.size()), [&](size_t i) { --budget; FieldNode e; e.name = QString("[%1]").arg(i); e.type = element; e.value = values[int(i)]; return e; });
    n.omitted = n.length - quint64(n.children.size());
    break;
  }
  case pvd::structure: {
    const auto& structure = static_cast<const pvd::PVStructure&>(*field);
    n.kind = FieldNode::Structure;
    const auto& names = structure.getStructure()->getFieldNames(); const auto& fields = structure.getPVFields();
    add(names.size(), [&](size_t i) { return tree(fields[i].get(), QString::fromStdString(names[i]), budget); });
    n.value = summary(n);
    break;
  }
  case pvd::union_: {
    const auto& u = static_cast<const pvd::PVUnion&>(*field);
    n.kind = FieldNode::Union;
    if (auto selected = u.get()) {
      const auto member = u.getUnion()->isVariant() ? QString("(any)") : QString::fromStdString(u.getSelectedFieldName());
      add(1, [&](size_t) { return tree(selected.get(), member, budget); });
    } else n.value = "(empty)";
    break;
  }
  case pvd::structureArray: {
    const auto elements = static_cast<const pvd::PVStructureArray&>(*field).view();
    n.kind = FieldNode::StructureArray; n.length = elements.size();
    add(std::min<size_t>(elements.size(), treeElements), [&](size_t i) { return tree(elements[i].get(), QString("[%1]").arg(i), budget); });
    n.omitted = n.length - quint64(n.children.size());
    break;
  }
  case pvd::unionArray: {
    const auto elements = static_cast<const pvd::PVUnionArray&>(*field).view();
    n.kind = FieldNode::UnionArray; n.length = elements.size();
    add(std::min<size_t>(elements.size(), treeElements), [&](size_t i) { return tree(elements[i].get(), QString("[%1]").arg(i), budget); });
    n.omitted = n.length - quint64(n.children.size());
    break;
  }
  }
  if ((n.kind == FieldNode::StructureArray || n.kind == FieldNode::UnionArray) && n.length) n.value = QString("%1 elements").arg(n.length);
  return n;
}
double number(const pvd::PVStructure& data, const char* path, double fallback = 0) {
  auto v = data.getSubField<pvd::PVScalar>(path); return v ? v->getAs<double>() : fallback;
}
QString text(const pvd::PVStructure& data, const char* path) {
  auto v = data.getSubField<pvd::PVScalar>(path); return v ? scalarText(*v) : QString();
}
Snapshot extract(const pvd::PVStructure& data, int limit) {
  Snapshot s;
  s.connected = s.readable = s.metadataReady = true;
  s.structureId = QString::fromStdString(data.getStructure()->getID());
  s.units = text(data, "display.units"); s.description = text(data, "display.description");
  s.precision = int(number(data, "display.precision", -1));
  s.severity = int(number(data, "alarm.severity")); s.status = int(number(data, "alarm.status"));
  s.alarmMessage = text(data, "alarm.message");
  s.seconds = qint64(number(data, "timeStamp.secondsPastEpoch"));
  s.nanoseconds = int(number(data, "timeStamp.nanoseconds"));
  s.displayLow = number(data, "display.limitLow"); s.displayHigh = number(data, "display.limitHigh");
  s.controlLow = number(data, "control.limitLow"); s.controlHigh = number(data, "control.limitHigh");
  const double none = std::numeric_limits<double>::quiet_NaN();
  s.alarmLow = number(data, "valueAlarm.lowAlarmLimit", none); s.warningLow = number(data, "valueAlarm.lowWarningLimit", none);
  s.warningHigh = number(data, "valueAlarm.highWarningLimit", none); s.alarmHigh = number(data, "valueAlarm.highAlarmLimit", none);
  int budget = treeFields;
  auto root = std::make_shared<FieldNode>(tree(&data, {}, budget));
  s.structure = std::move(root);
  auto value = data.getSubField("value");
  if (auto scalar = std::dynamic_pointer_cast<const pvd::PVScalar>(value)) {
    s.type = QString::fromStdString(scalar->getScalar()->getID());
    s.values << scalarText(*scalar); s.count = 1;
    s.numeric = scalar->getScalar()->getScalarType() != pvd::pvString;
    // QSRV serves string fields through a 39-byte DBR_STRING; a full one may be a longer field cut short.
    if (!s.numeric && scalar->getAs<std::string>().size() >= 39) s.truncated = true;
    if (s.numeric) s.number = scalar->getAs<double>();
  } else if (auto array = std::dynamic_pointer_cast<const pvd::PVScalarArray>(value)) {
    s.type = QString::fromStdString(array->getScalarArray()->getID());
    s.array = true; s.count = array->getLength(); s.capacity = array->getCapacity();
    s.truncated = s.count > quint64(limit); arrayText(*array, s.values, limit);
    const auto element = array->getScalarArray()->getElementType();
    s.numeric = element != pvd::pvString; s.bytes = element == pvd::pvByte || element == pvd::pvUByte;
    if (s.numeric && !s.values.isEmpty()) s.number = s.values[0].toDouble();
  } else if (auto index = data.getSubField<pvd::PVInt>("value.index")) {
    s.type = "enum"; s.numeric = true; s.number = index->get(); s.count = 1;
    s.values << QString::number(index->get());
    if (auto choices = data.getSubField<pvd::PVStringArray>("value.choices"))
      arrayValues<std::string>(*choices, s.choices, 65536);
  } else s.type = value ? QString::fromStdString(value->getField()->getID()) : "structure (no value field)";
  s.hasValue = true;
  return s;
}
struct Event {
  enum Kind { Connected, Disconnected, Data, Error, PutReady, PutDone, GetDone } kind;
  bool ok = true;
  QString message;
  Snapshot snapshot;
  pvd::StructureConstPtr structure;
  quint64 epoch = 0;
  // GetDone: the stored array length, or the stored scalar text.
  size_t stored = 0;
  std::string storedText;
  pvd::shared_vector<const std::string> storedStrings;
  explicit Event(Kind k, bool success = true, QString msg = {}) : kind(k), ok(success), message(std::move(msg)) {}
};
struct Inbox {
  std::mutex mutex;
  std::function<void()> wake;
  bool active = true;
  std::deque<Event> queue;
  void post(Event e) {
    std::lock_guard<std::mutex> guard(mutex);
    if (!active) return;
    const bool notify = queue.empty();
    // Coalesce adjacent data events to keep a fast IOC from growing the queue.
    if (e.kind == Event::Data && !queue.empty() && queue.back().kind == Event::Data && queue.back().epoch == e.epoch) {
      const auto skipped = queue.back().snapshot.fields.value("Client coalesced updates").toULongLong() + 1;
      e.snapshot.fields["Client coalesced updates"] = QString::number(skipped);
      queue.back() = std::move(e);
    } else if (queue.size() < 256) queue.push_back(std::move(e));
    if (notify && wake) wake();
  }
};
struct ChannelRequester : pva::ChannelRequester {
  std::shared_ptr<Inbox> inbox;
  explicit ChannelRequester(std::shared_ptr<Inbox> i) : inbox(std::move(i)) {}
  std::string getRequesterName() override { return "qtprobe"; }
  void message(const std::string&, pvd::MessageType) override {}
  void channelCreated(const pvd::Status& status, const pva::Channel::shared_pointer&) override {
    if (!status.isSuccess()) inbox->post(Event(Event::Error, false, QString::fromStdString(status.getMessage())));
  }
  void channelStateChange(const pva::Channel::shared_pointer&, pva::Channel::ConnectionState state) override {
    if (state == pva::Channel::CONNECTED) inbox->post(Event(Event::Connected));
    else if (state == pva::Channel::DISCONNECTED || state == pva::Channel::DESTROYED) inbox->post(Event(Event::Disconnected));
  }
};
struct MonitorRequester : pva::MonitorRequester {
  std::shared_ptr<Inbox> inbox;
  int limit;
  quint64 epoch;
  MonitorRequester(std::shared_ptr<Inbox> i, int l, quint64 e) : inbox(std::move(i)), limit(l), epoch(e) {}
  std::string getRequesterName() override { return "qtprobe"; }
  void message(const std::string&, pvd::MessageType) override {}
  void report(const pvd::Status& status) {
    if (!status.isSuccess()) { Event e(Event::Error, false, QString::fromStdString(status.getMessage())); e.epoch = epoch; inbox->post(std::move(e)); }
  }
  void monitorConnect(const pvd::Status& status, const pva::MonitorPtr& monitor, const pvd::StructureConstPtr&) override {
    if (!status.isSuccess()) { report(status); return; }
    report(monitor->start());
  }
  void monitorEvent(const pva::MonitorPtr& monitor) override {
    while (auto element = monitor->poll()) {
      Event e(Event::Data); e.epoch = epoch;
      try { e.snapshot = extract(*element->pvStructurePtr, limit); }
      catch (const std::exception& ex) { e = Event(Event::Error, false, QString::fromLocal8Bit(ex.what())); e.epoch = epoch; }
      monitor->release(element); inbox->post(std::move(e));
    }
  }
  void unlisten(const pva::MonitorPtr&) override {
    Event e(Event::Error, false, "PVA monitor ended"); e.epoch = epoch; inbox->post(std::move(e));
  }
};
struct PutRequester : pva::ChannelPutRequester {
  std::shared_ptr<Inbox> inbox;
  quint64 epoch;
  PutRequester(std::shared_ptr<Inbox> i, quint64 e) : inbox(std::move(i)), epoch(e) {}
  std::string getRequesterName() override { return "qtprobe"; }
  void message(const std::string&, pvd::MessageType) override {}
  void channelPutConnect(const pvd::Status& status, const pva::ChannelPut::shared_pointer&, const pvd::StructureConstPtr& structure) override {
    Event e(Event::PutReady, status.isSuccess(), QString::fromStdString(status.getMessage()));
    e.structure = structure; e.epoch = epoch; inbox->post(std::move(e));
  }
  void putDone(const pvd::Status& status, const pva::ChannelPut::shared_pointer&) override {
    Event e(Event::PutDone, status.isSuccess(), QString::fromStdString(status.getMessage())); e.epoch = epoch; inbox->post(std::move(e));
  }
  void getDone(const pvd::Status& status, const pva::ChannelPut::shared_pointer&, const pvd::PVStructure::shared_pointer& data, const pvd::BitSet::shared_pointer&) override {
    Event e(Event::GetDone, status.isSuccess() && data, QString::fromStdString(status.getMessage())); e.epoch = epoch;
    if (e.ok) {
      auto value = data->getSubField("value");
      if (auto array = std::dynamic_pointer_cast<pvd::PVScalarArray>(value)) {
        e.stored = array->getLength();
        if (auto strings = std::dynamic_pointer_cast<pvd::PVStringArray>(value)) e.storedStrings = strings->view();
      }
      else if (auto scalar = std::dynamic_pointer_cast<pvd::PVScalar>(value)) e.storedText = scalar->getAs<std::string>();
      else e.ok = false;
    }
    inbox->post(std::move(e));
  }
};
QString validate(QString text, pvd::ScalarType type) {
  if (type == pvd::pvString) return text;
  if (type == pvd::pvBoolean) {
    if (text == "true" || text == "1") return "true";
    if (text == "false" || text == "0") return "false";
    throw std::runtime_error("Boolean value must be true, false, 1 or 0");
  }
  bool ok = false;
  if (type == pvd::pvFloat || type == pvd::pvDouble) {
    const double n = text.toDouble(&ok);
    if (!ok || !std::isfinite(n) || (type == pvd::pvFloat && std::abs(n) > std::numeric_limits<float>::max()))
      throw std::runtime_error("Invalid or out-of-range floating-point value");
    return text;
  }
  const bool unsignedType = type == pvd::pvUByte || type == pvd::pvUShort || type == pvd::pvUInt || type == pvd::pvULong;
  int bits = type == pvd::pvByte || type == pvd::pvUByte ? 8 : type == pvd::pvShort || type == pvd::pvUShort ? 16 : type == pvd::pvInt || type == pvd::pvUInt ? 32 : 64;
  const int base = text.startsWith("0x") || text.startsWith("0b") ? 0 : 10;
  if (unsignedType) {
    const auto n = text.toULongLong(&ok, base);
    if (ok && !text.trimmed().startsWith('-') && (bits == 64 || n < (quint64(1) << bits))) return QString::number(n);
  } else {
    const auto n = text.toLongLong(&ok, base);
    if (ok && (bits == 64 || (n >= -(qint64(1) << (bits - 1)) && n < (qint64(1) << (bits - 1))))) return QString::number(n);
  }
  throw std::runtime_error("Invalid or out-of-range integer value");
}
class Pva final : public Connection {
  Channel& owner;
  Address address;
  int limit;
  quint64 epoch = 0, sequence = 0;
  Snapshot s;
  std::shared_ptr<Inbox> inbox = std::make_shared<Inbox>();
  pva::ChannelProvider::shared_pointer provider;
  pva::Channel::shared_pointer channel;
  pva::ChannelRequester::shared_pointer channelRequester;
  pva::MonitorRequester::shared_pointer monitorRequester;
  pva::ChannelPutRequester::shared_pointer putRequester;
  pva::MonitorPtr monitor;
  pva::ChannelPut::shared_pointer put;
  pvd::StructureConstPtr putStructure;
  // Servers may shorten strings and arrays without an error (QSRV keeps 39
  // bytes of a DBF_STRING and NELM elements), so such writes are read back.
  struct Check {
    bool active = false, text = false;
    size_t sent = 0;
    std::string value;
    pvd::shared_vector<const std::string> strings;
  } check;
  void clearOperations() {
    ++epoch;
    putStructure.reset(); check = {};
    if (monitor) monitor->destroy();
    monitor.reset(); monitorRequester.reset();
    if (put) put->destroy();
    put.reset(); putRequester.reset();
  }
public:
  Pva(Channel& o, Address a, int l) : owner(o), address(a), limit(l) {
    s.name = a.name; s.provider = "pva";
    pva::ClientFactory::start(); provider = pva::ChannelProviderRegistry::clients()->getProvider("pva");
    if (!provider) throw std::runtime_error("EPICS pvAccess provider unavailable");
    channelRequester = std::make_shared<ChannelRequester>(inbox);
    channel = provider->createChannel(a.name.toStdString(), channelRequester, pva::ChannelProvider::PRIORITY_DEFAULT);
    // Install only after construction succeeds. post() and destruction use
    // the same lock; queued calls belong to the QObject owner and never
    // capture a backend that reconnect could delete. start() drains anything
    // posted before this notifier was installed.
    { std::lock_guard<std::mutex> guard(inbox->mutex);
      inbox->wake = [target = &owner] {
        QMetaObject::invokeMethod(target, [target] { target->requestPoll(); }, Qt::QueuedConnection);
      };
    }
  }
  ~Pva() override {
    { std::lock_guard<std::mutex> guard(inbox->mutex); inbox->active = false; inbox->queue.clear(); }
    clearOperations(); if (channel) channel->destroy();
  }
  void poll() override {
    std::deque<Event> events;
    { std::lock_guard<std::mutex> guard(inbox->mutex); events.swap(inbox->queue); }
    for (auto& e : events) {
      if (e.epoch && e.epoch != epoch) continue;
      switch (e.kind) {
      case Event::Connected:
        clearOperations(); s.connected = true; s.hasValue = s.writable = false; s.error.clear();
        s.host = QString::fromStdString(channel->getRemoteAddress());
        monitorRequester = std::make_shared<MonitorRequester>(inbox, limit, epoch);
        monitor = channel->createMonitor(monitorRequester, pvd::CreateRequest::create()->createRequest("field()"));
        // Read-only channels (--read-only, --info, Record fields) create no put operation on the server.
        if (!owner.readOnly()) {
          putRequester = std::make_shared<PutRequester>(inbox, epoch);
          put = channel->createChannelPut(putRequester, pvd::CreateRequest::create()->createRequest("field(value)"));
        }
        break;
      case Event::Disconnected:
        clearOperations(); s.connected = s.readable = s.writable = false;
        s.error = "Disconnected; reconnecting automatically";
        if (owner.writePending()) owner.completeWrite(false, "Disconnected during write; outcome unknown.", true);
        break;
      case Event::Data: {
        auto incoming = std::move(e.snapshot);
        incoming.host = s.host; incoming.writable = s.writable;
        incoming.name = address.name; incoming.provider = "pva"; incoming.sequence = ++sequence;
        s = std::move(incoming); break;
      }
      case Event::PutReady:
        if (e.ok && e.structure) {
          putStructure = e.structure;
          const auto data = pvd::getPVDataCreate()->createPVStructure(putStructure);
          auto value = data->getSubField("value");
          if (value && value->getField()->getType() == pvd::structure) value = data->getSubField("value.index");
          s.writable = value && (value->getField()->getType() == pvd::scalar || value->getField()->getType() == pvd::scalarArray) && channel->getAccessRights(value) == pva::readWrite;
        } else { s.writable = false; s.fields["Write setup"] = e.message; }
        break;
      case Event::PutDone:
        if (e.ok && check.active && put) { put->get(); break; }
        check = {}; owner.completeWrite(e.ok, e.message); break;
      case Event::GetDone: {
        if (!check.active) break;
        QString note = "Write completed";
        if (!e.ok) note += "; the readback check failed. Check the readback.";
        else if (check.text && e.storedText.size() < check.value.size() && check.value.compare(0, e.storedText.size(), e.storedText) == 0)
          note += QString(", but the server kept only %1 of %2 bytes (field size). Check the readback.").arg(e.storedText.size()).arg(check.sent);
        else if (!check.text && e.stored < check.sent)
          note += QString(", but the server kept only %1 of %2 elements (field size). Check the readback.").arg(e.stored).arg(check.sent);
        else for (size_t i = 0; i < std::min(check.strings.size(), e.storedStrings.size()); ++i) {
          const auto& sent = check.strings[i]; const auto& stored = e.storedStrings[i];
          if (stored.size() < sent.size() && sent.compare(0, stored.size(), stored) == 0) {
            note += QString(", but the server kept only %1 of %2 bytes in string element %3 (field size). Check the readback.").arg(stored.size()).arg(sent.size()).arg(i);
            break;
          }
        }
        check = {}; owner.completeWrite(true, note); break;
      }
      case Event::Error: s.error = e.message; break;
      }
      owner.update(s);
    }
  }
  void refetch() override {
    // A new monitor delivers the current value first. Only for channels
    // without a put, whose events share the epoch.
    if (!channel || !channel->isConnected() || !monitor || put) return;
    ++epoch; monitor->destroy(); monitor.reset();
    monitorRequester = std::make_shared<MonitorRequester>(inbox, limit, epoch);
    monitor = channel->createMonitor(monitorRequester, pvd::CreateRequest::create()->createRequest("field()"));
  }
  void write(const QStringList& values) override {
    if (!channel || !channel->isConnected() || !s.writable || !put || !putStructure)
      throw std::runtime_error("PVA write operation unavailable");
    auto data = pvd::getPVDataCreate()->createPVStructure(putStructure);
    auto value = data->getSubField("value");
    if (value && value->getField()->getType() == pvd::structure) value = data->getSubField("value.index");
    Check next;
    if (auto scalar = std::dynamic_pointer_cast<pvd::PVScalar>(value)) {
      const auto type = scalar->getScalar()->getScalarType();
      const auto text = validate(values[0], type).toStdString();
      scalar->putFrom<std::string>(text);
      if (type == pvd::pvString) next = {true, true, text.size(), text, {}};
    } else if (auto array = std::dynamic_pointer_cast<pvd::PVScalarArray>(value)) {
      pvd::shared_vector<std::string> input;
      for (const auto& v : values) input.push_back(validate(v, array->getScalarArray()->getElementType()).toStdString());
      next = {true, false, input.size(), {}, {}};
      const auto frozen = pvd::freeze(input);
      if (array->getScalarArray()->getElementType() == pvd::pvString) next.strings = frozen;
      array->putFrom<std::string>(frozen);
    } else throw std::runtime_error("This PVA structure is inspection only");
    check = std::move(next);
    pvd::BitSet::shared_pointer bits(new pvd::BitSet);
    bits->set(pvd::uint32(value->getFieldOffset()));
    put->put(data, bits);
  }
};
} // namespace
std::unique_ptr<Connection> createPva(Channel& o, const Address& a, int l) { return std::make_unique<Pva>(o, a, l); }
} // namespace probe
#else
namespace probe {
std::unique_ptr<Connection> createPva(Channel&, const Address&, int) {
  throw std::runtime_error("Built without pvAccess. Rebuild against EPICS 7 Base with pvAccess and pvData.");
}
}
#endif
