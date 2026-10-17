// Record fields shown by the Record section, and why-isn't-it-updating rules.
#pragma once
#include <QMap>
#include <QStringList>
#include <QVector>
namespace probe {
struct RecordField {
  QString name, group, help;
  bool link = false;
};
// dbCommon fields shown for every record.
QVector<RecordField> commonRecordFields();
// Fields for a record type (RTYP): its links (inputs, outputs, forward and
// select links), simulation and deadband fields. Unknown types get INP, OUT and DOL.
QVector<RecordField> typeRecordFields(const QString& recordType);
// STAT as a name. CA menus carry at most 16 labels, so states such as
// DISABLE (18) arrive as numbers.
QString alarmStatusName(const QString& stat);
// The record part of a PV name: everything before the first '.'.
QString recordName(const QString& pv);
// The PV a database link reads or writes, or empty for constants, hardware
// addresses (@, #) and empty links. JSON "ca" and "pva" links return their
// "pv" member, with pva:// for pva links.
QString linkTarget(const QString& link);
struct Finding {
  QString text;
  bool problem = false;
};
// Reasons a record may not update, from field values (menu fields as labels).
// secondsSinceUpdate is the time since the PV's last monitor event, or < 0.
QVector<Finding> recordDiagnosis(const QMap<QString, QString>& fields, double secondsSinceUpdate);
} // namespace probe
