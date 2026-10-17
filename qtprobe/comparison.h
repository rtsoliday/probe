#pragma once
#include <QElapsedTimer>
#include <QWidget>
#include <functional>
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QTimer;
class QToolButton;
class QVBoxLayout;
namespace probe {
class Channel;
class PvRow;
// One compact row per PV. Shares the inspectors' channels and histories;
// opening this view creates no extra subscriptions and never exposes a write control.
class Comparison final : public QWidget {
public:
  enum Column { Added = -1, Name = 1, Value = 2, State = 5, Age = 6 };
  explicit Comparison(QWidget* parent = nullptr);
  void add(Channel*, std::function<void()> inspect, std::function<void()> close);
  void remove(Channel*);
  void refresh();
  // Marks the PV shown by the inspector.
  void setCurrent(Channel*);
  void clearFilter();
  // Redraws status dots after an appearance change.
  void restyle();
  // Re-applies column widths and row heights after a zoom change.
  void rescale();
  // Height that shows every row without scrolling.
  int preferredHeight() const;
  // Sorting cycles ascending, descending, then the order PVs were added.
  void sortBy(int column);
  // Selected rows in display order.
  QList<Channel*> selection() const;
  enum CopyFormat { Text, Csv, Names };
  QString copyText(CopyFormat) const;
  // The cursor row changed by click or keyboard.
  std::function<void(Channel*)> selected;
  // Open the given CA PVs in QtStripTool.
  std::function<void(const QList<Channel*>&)> plot;
protected:
  void resizeEvent(QResizeEvent*) override;
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void changeEvent(QEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  friend class PvRow;
  QList<PvRow*> rows, order;
  PvRow *cursor = nullptr, *anchor = nullptr;
  QFrame *frame, *header;
  QHBoxLayout *tools, *headerLayout;
  QList<QWidget*> headerCells;
  QVBoxLayout* list;
  QWidget* body;
  QLineEdit* filter;
  QToolButton *alarmsOnly, *disconnectedOnly;
  QLabel* empty;
  QScrollArea* scroll;
  QTimer* ageTimer;
  QElapsedTimer lastSort;
  int sortKey = Added;
  bool descending = false, showProvider = true, showTrend = true;
  QList<PvRow*> visibleRows() const;
  void select(PvRow*, bool);
  void clearSelection();
  void rowClicked(PvRow*, Qt::KeyboardModifiers);
  void moveCursor(PvRow*, bool extend);
  void rowMenu(PvRow*, const QPoint&);
  void startDrag(PvRow*, bool onlyThis);
  void sortRows(bool force);
  void updateHeader();
  void updateAges();
};
} // namespace probe
