// The shared non-preemptive context pump serializes callbacks and snapshot
// reads. CA may execute callbacks on its receive thread while ca_poll permits
// them; QObject delivery is always queued back to Qt.
#include "channel.h"
#include <cadef.h>
#include <db_access.h>
#include <alarm.h>
#include <algorithm>
#include <array>
#include <type_traits>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <QRegularExpression>
#include <vector>
namespace probe {
namespace {
void check(int status) {
  if (status != ECA_NORMAL) throw std::runtime_error(ca_message(status));
}
struct Context {
  QTimer pump;
  Context() {
    check(ca_context_create(ca_disable_preemptive_callback));
    // One pump for the shared CA context, independent of the number of PVs.
    pump.setInterval(25);
    QObject::connect(&pump, &QTimer::timeout, &pump, [] { ca_poll(); }); pump.start();
  }
  ~Context() { pump.stop(); ca_context_destroy(); }
};
std::weak_ptr<Context> sharedContext;
QString caString(const char* p, size_t n) {
  // Match PVA and CHAR waveforms; Windows' local encoding is an ANSI code page.
  return QString::fromUtf8(p, int(std::find(p, p + n, '\0') - p));
}
class Ca final : public Connection {
  Channel& owner;
  std::shared_ptr<Context> context;
  chid id = nullptr, procId = nullptr;
  evid monitor = nullptr, properties = nullptr;
  // Request types of the subscriptions above; CA keeps them across reconnects.
  long monitorType = -1, propertiesType = -1;
  struct Extra { Ca* owner; QString field; chid id = nullptr; evid monitor = nullptr; };
  std::vector<std::unique_ptr<Extra>> extras;
  Snapshot s;
  int limit;
  bool dirty = true, closing = false, putDone = false, putOK = false, putUncertain = false, processing = false, arrayPut = false;
  QString putMessage;
  void markDirty() { if (!dirty) { dirty = true; owner.requestPoll(); } }
  void fail(int status) { if (status != ECA_NORMAL) { s.error = ca_message(status); markDirty(); } }
  static Ca* self(chid id) { return static_cast<Ca*>(ca_puser(id)); }
  static void access(access_rights_handler_args a) {
    auto c = self(a.chid); if (!c || c->closing) return;
    c->s.readable = a.ar.read_access; c->s.writable = a.ar.write_access;
    c->markDirty();
  }
  static void connection(connection_handler_args a) {
    auto c = self(a.chid); if (!c || c->closing) return;
    auto& s = c->s;
    s.connected = a.op == CA_OP_CONN_UP;
    s.readable = s.connected && ca_read_access(a.chid);
    s.writable = s.connected && ca_write_access(a.chid);
    c->markDirty();
    if (!s.connected) {
      s.error = "Disconnected; reconnecting automatically";
      return;
    }
    // ca_host_name() points into a cache that CA's resolver thread rewrites; this copies under its lock.
    char host[256]; ca_get_host_name(a.chid, host, sizeof host);
    s.error.clear(); s.hasValue = false; s.host = QString::fromLocal8Bit(host);
    const auto native = ca_field_type(a.chid);
    s.type = dbf_type_to_text(native); s.capacity = ca_element_count(a.chid);
    s.array = s.capacity > 1; s.numeric = native != DBF_STRING;
    s.bytes = native == DBF_CHAR && s.array;
    s.metadataReady = false; s.fields.clear(); s.choices.clear();
    // The PV may return with another native type (a changed record); the
    // server would convert values to the old request type, so resubscribe.
    const long valueType = dbf_type_to_DBR_TIME(native), metaType = native == DBF_STRING ? -1 : native == DBF_ENUM ? DBR_CTRL_ENUM : DBR_CTRL_DOUBLE;
    if (c->monitor && c->monitorType != valueType) { ca_clear_subscription(c->monitor); c->monitor = nullptr; }
    if (c->properties && c->propertiesType != metaType) { ca_clear_subscription(c->properties); c->properties = nullptr; }
    if (native == DBF_STRING) s.metadataReady = true;
    else {
      c->fail(ca_array_get_callback(metaType, 1, a.chid, metadata, c));
      if (!c->properties) {
        c->fail(ca_create_subscription(metaType, 1, a.chid, DBE_PROPERTY, metadata, c, &c->properties));
        c->propertiesType = metaType;
      }
    }
    if (!c->monitor) {
      c->fail(ca_create_subscription(valueType, 0, a.chid, DBE_VALUE | DBE_ALARM, value, c, &c->monitor));
      c->monitorType = valueType;
    }
    ca_flush_io();
  }
  static void metadata(event_handler_args a) {
    auto c = static_cast<Ca*>(a.usr); if (!c || c->closing) return;
    if (a.status != ECA_NORMAL || !a.dbr) { c->fail(a.status); return; }
    auto& s = c->s;
    if (a.type == DBR_CTRL_ENUM) {
      const auto& v = *static_cast<const dbr_ctrl_enum*>(a.dbr);
      s.choices.clear();
      for (int i = 0; i < std::min<int>(v.no_str, MAX_ENUM_STATES); ++i)
        s.choices << caString(v.strs[i], MAX_ENUM_STRING_SIZE);
    } else {
      const auto& v = *static_cast<const dbr_ctrl_double*>(a.dbr);
      s.units = caString(v.units, MAX_UNITS_SIZE);
      const auto native = ca_field_type(c->id);
      s.precision = native == DBF_FLOAT || native == DBF_DOUBLE ? v.precision : 0;
      s.displayLow = v.lower_disp_limit; s.displayHigh = v.upper_disp_limit;
      s.controlLow = v.lower_ctrl_limit; s.controlHigh = v.upper_ctrl_limit;
      const std::pair<const char*, double> fields[] = {
        {"LOPR", v.lower_disp_limit}, {"HOPR", v.upper_disp_limit},
        {"DRVL", v.lower_ctrl_limit}, {"DRVH", v.upper_ctrl_limit},
        {"LOLO", v.lower_alarm_limit}, {"LOW", v.lower_warning_limit},
        {"HIGH", v.upper_warning_limit}, {"HIHI", v.upper_alarm_limit}};
      for (const auto& f : fields) s.fields[f.first] = QString::number(f.second, 'g', 17);
      s.alarmLow = v.lower_alarm_limit; s.warningLow = v.lower_warning_limit;
      s.warningHigh = v.upper_warning_limit; s.alarmHigh = v.upper_alarm_limit;
    }
    s.metadataReady = true; c->markDirty();
  }
  template<class T> static void extract(Ca* c, const T& v, long count) {
    auto& s = c->s;
    s.status = v.status; s.severity = v.severity;
    s.seconds = qint64(v.stamp.secPastEpoch) + POSIX_TIME_AT_EPICS_EPOCH;
    s.nanoseconds = int(v.stamp.nsec);
    // STAT comes from the server; a negative short must not index the table.
    s.alarmMessage = v.status >= 0 && v.status < ALARM_NSTATUS ? epicsAlarmConditionStrings[v.status] : "Unknown";
    const auto values = &v.value;
    for (long i = 0; i < count; ++i) {
      if constexpr (std::is_array_v<std::remove_reference_t<decltype(v.value)>>) {
        s.values << caString(values[i], MAX_STRING_SIZE);
        // DBR_STRING holds 39 bytes; a value that fills it may be a longer field (DESC, a link) cut short.
        if (std::find(values[i], values[i] + MAX_STRING_SIZE, '\0') - values[i] >= MAX_STRING_SIZE - 1) s.truncated = true;
      }
      else if constexpr (std::is_floating_point_v<std::remove_reference_t<decltype(v.value)>>)
        s.values << QString::number(values[i], 'g', std::is_same_v<T, dbr_time_float> ? 9 : 17);
      else s.values << QString::number(values[i]);
    }
  }
  static void value(event_handler_args a) {
    auto c = static_cast<Ca*>(a.usr); if (!c || c->closing) return;
    if (a.status != ECA_NORMAL || !a.dbr) { c->fail(a.status); return; }
    auto& s = c->s;
    s.values.clear(); const long count = std::min<long>(a.count, c->limit);
    s.count = a.count; s.truncated = a.count > c->limit;
    switch (a.type) {
#define EXTRACT(TYPE, STRUCT) case TYPE: extract(c, *static_cast<const STRUCT*>(a.dbr), count); break
    EXTRACT(DBR_TIME_STRING, dbr_time_string);
    EXTRACT(DBR_TIME_SHORT, dbr_time_short);
    EXTRACT(DBR_TIME_FLOAT, dbr_time_float);
    EXTRACT(DBR_TIME_ENUM, dbr_time_enum);
    EXTRACT(DBR_TIME_CHAR, dbr_time_char);
    EXTRACT(DBR_TIME_LONG, dbr_time_long);
    EXTRACT(DBR_TIME_DOUBLE, dbr_time_double);
#undef EXTRACT
    default: s.error = "Unsupported CA time type"; c->markDirty(); return;
    }
    s.hasValue = true; s.error.clear();
    s.number = s.numeric && count ? s.values[0].toDouble() : std::numeric_limits<double>::quiet_NaN();
    ++s.sequence; c->markDirty();
  }
  static void extraConnection(connection_handler_args a) {
    auto e = static_cast<Extra*>(ca_puser(a.chid));
    if (a.op != CA_OP_CONN_UP || e->monitor) return;
    ca_create_subscription(DBR_STRING, 1, a.chid, DBE_VALUE, extraValue, e, &e->monitor);
    ca_flush_io();
  }
  static void extraValue(event_handler_args a) {
    auto e = static_cast<Extra*>(a.usr);
    if (a.status != ECA_NORMAL || !a.dbr || e->owner->closing) return;
    const auto text = caString(static_cast<const char*>(a.dbr), MAX_STRING_SIZE);
    e->owner->s.fields[e->field] = text;
    if (e->field == "DESC") e->owner->s.description = text;
    e->owner->markDirty();
  }
  static void written(event_handler_args a) {
    auto c = static_cast<Ca*>(a.usr); if (!c || c->closing) return;
    c->putDone = true; c->putOK = a.status == ECA_NORMAL;
    // IOC array conversion is not atomic: a later invalid element can fail
    // after earlier elements changed, without posting a value monitor.
    c->putUncertain = a.status == ECA_DISCONN || (!c->putOK && c->arrayPut);
    if (c->putOK) c->putMessage = c->processing ? "Record processed" : "Write completed";
    else if (a.status == ECA_DISCONN) c->putMessage = "Disconnected during write; outcome unknown.";
    else if (c->arrayPut) c->putMessage = QString("Array write failed and may have partially changed the PV; outcome unknown. Check readback and reconnect before retrying. (%1)").arg(ca_message(a.status));
    else c->putMessage = QString::fromLocal8Bit(ca_message(a.status));
    c->owner.requestPoll();
  }
  static void procAccess(access_rights_handler_args a) {
    auto c = self(a.chid); if (!c || c->closing) return;
    c->s.processable = ca_state(a.chid) == cs_conn && a.ar.write_access; c->markDirty();
  }
  static void procConnection(connection_handler_args a) {
    auto c = self(a.chid); if (!c || c->closing) return;
    c->s.processable = a.op == CA_OP_CONN_UP && ca_write_access(a.chid); c->markDirty();
  }
public:
  Ca(Channel& o, const Address& address, int max) : owner(o), limit(max) {
    context = sharedContext.lock();
    if (!context) { context = std::make_shared<Context>(); sharedContext = context; }
    s.name = address.name; s.provider = "ca";
    check(ca_create_channel(address.name.toLocal8Bit().constData(), connection, this, 0, &id));
    ca_replace_access_rights_event(id, access);
    QString record = address.name;
    const auto dot = record.lastIndexOf('.');
    if (dot >= 0) record.truncate(dot);
    if (!owner.lightweight) for (const auto& field : {"DESC", "RTYP"}) {
      auto extra = std::make_unique<Extra>(); extra->owner = this; extra->field = field;
      const auto name = record + '.' + field;
      ca_create_channel(name.toLocal8Bit().constData(), extraConnection, extra.get(), 0, &extra->id);
      extras.push_back(std::move(extra));
    }
    if (!owner.readOnly() && !owner.lightweight) {
      const auto proc = record + ".PROC";
      if (ca_create_channel(proc.toLocal8Bit().constData(), procConnection, this, 0, &procId) == ECA_NORMAL)
        ca_replace_access_rights_event(procId, procAccess);
      else procId = nullptr;
    }
    ca_flush_io();
  }
  ~Ca() override {
    closing = true;
    for (auto& extra : extras) if (extra->id) ca_clear_channel(extra->id);
    if (procId) ca_clear_channel(procId);
    if (id) ca_clear_channel(id);
    ca_flush_io();
  }
  void poll() override {
    if (dirty) { dirty = false; owner.update(s); }
    if (putDone) {
      putDone = false;
      if (!putOK && arrayPut && s.connected) refetch();
      owner.completeWrite(putOK, putMessage, putUncertain);
    }
  }
  void detach() override { closing = true; }
  void refetch() override {
    if (!id || ca_state(id) != cs_conn) return;
    fail(ca_array_get_callback(dbf_type_to_DBR_TIME(ca_field_type(id)), 0, id, value, this)); ca_flush_io();
  }
  void process() override {
    if (!procId || ca_state(procId) != cs_conn || !ca_write_access(procId))
      throw std::runtime_error("The record's PROC field is unavailable or not writable");
    const dbr_long_t one = 1; processing = true; arrayPut = false;
    check(ca_array_put_callback(DBR_LONG, 1, procId, &one, written, this));
    ca_flush_io();
  }
  void write(const QStringList& values) override {
    if (!id || ca_state(id) != cs_conn || !ca_write_access(id))
      throw std::runtime_error("CA write access unavailable");
    processing = false;
    // CA counts use unsigned long, which is 32-bit on 64-bit Windows.
    if (quint64(values.size()) > std::numeric_limits<unsigned long>::max())
      throw std::runtime_error("Too many elements for a CA write");
    const auto count = static_cast<unsigned long>(values.size());
    arrayPut = count > 1;
    const auto native = ca_field_type(id);
    if (native == DBF_STRING) {
      std::vector<std::array<char, MAX_STRING_SIZE>> data;
      for (const auto& value : values) {
        const auto bytes = value.toUtf8();
        if (bytes.size() >= MAX_STRING_SIZE) throw std::runtime_error("CA strings must fit in 39 bytes");
        std::array<char, MAX_STRING_SIZE> item{}; std::memcpy(item.data(), bytes.constData(), bytes.size()); data.push_back(item);
      }
      check(ca_array_put_callback(DBR_STRING, count, id, data.data(), written, this));
    } else if (native == DBF_DOUBLE || native == DBF_FLOAT) {
      std::vector<double> data; bool beyondExact = false;
      for (const auto& value : values) {
        bool ok; double number = value.toDouble(&ok);
        if (!ok || !std::isfinite(number) || (native == DBF_FLOAT && std::abs(number) > std::numeric_limits<float>::max()))
          throw std::runtime_error("Invalid or out-of-range floating-point value");
        beyondExact = beyondExact || (native == DBF_DOUBLE && std::abs(number) >= 9007199254740992.0);
        data.push_back(number);
      }
      if (beyondExact) {
        // CA serves 64-bit integer fields as DBF_DOUBLE, which cannot hold every
        // integer beyond 2^53 and overflows at 2^63. Send integer text instead:
        // the IOC parses it for the field's own type, exactly or with an overflow
        // error, and a double field gets the same value as before.
        static const QRegularExpression integer("^[+-]?[0-9]{1,20}$");
        std::vector<std::array<char, MAX_STRING_SIZE>> text;
        for (int i = 0; i < values.size(); ++i) {
          auto trimmed = values[i].trimmed(); const double number = data[size_t(i)];
          // The IOC parses UINT64 text with base 0, where a leading zero selects
          // octal ("010" would store 8): send integers without leading zeros.
          const int start = trimmed.startsWith('+') || trimmed.startsWith('-') ? 1 : 0;
          while (trimmed.size() > start + 1 && trimmed.at(start) == '0' && trimmed.at(start + 1).isDigit()) trimmed.remove(start, 1);
          QString encoded;
          if (integer.match(trimmed).hasMatch()) encoded = trimmed;
          else if (std::abs(number) >= 1e38 || (number != 0 && std::abs(number) < 1)) {
            // IOC integer parsers accept a numeric prefix and ignore an
            // exponent, so "1e100" would silently write 1 to an int64 field.
            // Keep 21 digits before the exponent: the prefix overflows both
            // int64 and uint64, while a double parser gets the same value.
            const auto scientific = QString::number(std::abs(number), 'e', 16);
            const auto e = scientific.indexOf('e');
            const auto digits = scientific.left(e).remove('.');
            const auto exponent = scientific.mid(e + 1).toInt();
            const QString sign = number < 0 ? "-" : "";
            if (std::abs(number) >= 1e38)
              encoded = sign + digits.leftJustified(21, '0') + 'e' + QString::number(exponent - 20);
            else
              // A tiny array peer must convert to integer zero, not the
              // mantissa prefix ("1e-100" otherwise becomes integer 1).
              encoded = sign + "0." + digits + 'e' + QString::number(exponent + 1);
          } else encoded = std::trunc(number) == number ? QString::number(number, 'f', 0) : QString::number(number, 'g', 17);
          const auto bytes = encoded.toLatin1();
          if (bytes.size() >= MAX_STRING_SIZE) throw std::runtime_error("Numeric text exceeds the CA string size");
          std::array<char, MAX_STRING_SIZE> item{}; std::memcpy(item.data(), bytes.constData(), size_t(bytes.size())); text.push_back(item);
        }
        check(ca_array_put_callback(DBR_STRING, count, id, text.data(), written, this));
      } else check(ca_array_put_callback(DBR_DOUBLE, count, id, data.data(), written, this));
    } else {
      std::vector<dbr_long_t> data;
      for (const auto& value : values) {
        bool ok; auto number = value.toLongLong(&ok, value.startsWith("0x") || value.startsWith("0b") ? 0 : 10);
        const qint64 lo = native == DBF_CHAR || native == DBF_ENUM ? 0 : native == DBF_SHORT ? -32768 : INT32_MIN;
        const qint64 hi = native == DBF_CHAR ? 255 : native == DBF_ENUM ? 65535 : native == DBF_SHORT ? 32767 : INT32_MAX;
        if (!ok || number < lo || number > hi) throw std::runtime_error("Invalid or out-of-range integer value");
        data.push_back(dbr_long_t(number));
      }
      check(ca_array_put_callback(DBR_LONG, count, id, data.data(), written, this));
    }
    ca_flush_io();
  }
};
} // namespace
std::unique_ptr<Connection> createCa(Channel& owner, const Address& address, int limit) {
  return std::make_unique<Ca>(owner, address, limit);
}
} // namespace probe
