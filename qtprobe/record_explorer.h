#pragma once
#include "record.h"
#include <QElapsedTimer>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>
class QLabel;
class QTableWidget;
class QTimer;
namespace probe {
class Channel;
// Live record fields (SCAN, PACT, DISA, links…) for the PV's record, read as
// record.FIELD over the PV's provider, with a plain-language diagnosis.
// Channels exist only while active, i.e. while the section is shown.
class RecordExplorer final : public QWidget {
public:
  RecordExplorer(Channel* subject, double timeout, QWidget* parent = nullptr);
  ~RecordExplorer() override;
  void setActive(bool);
  bool active() const { return !rows.empty(); }
  // Opens a link target: a PV name, or pva://name for PVA JSON links.
  std::function<void(const QString&)> openLink;
  // Current text of a field, or empty; the findings shown above the table.
  QString fieldText(const QString& field) const;
  QVector<Finding> findings() const { return shownFindings; }
  QSize sizeHint() const override;
private:
  struct Row { RecordField spec; std::unique_ptr<Channel> channel; QElapsedTimer since; };
  Channel* subject;
  double timeout;
  QString record, provider, loadedType, layoutKey;
  std::vector<Row> rows;
  QLabel* summary;
  QTableWidget* table;
  QTimer *update, *tick;
  QElapsedTimer started;
  QVector<Finding> shownFindings;
  void addFields(const QVector<RecordField>&);
  void refresh();
};
} // namespace probe
