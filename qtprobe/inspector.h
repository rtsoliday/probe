#pragma once
#include "channel.h"
#include <QLabel>
#include <QMainWindow>
#include <QWidget>
#include <functional>
class QComboBox;
class QTabBar;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QSlider;
class QDial;
class QDoubleSpinBox;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QAction;
class QTimer;
class QBoxLayout;
namespace probe {
class Comparison;
class RangeGauge;
class RecordExplorer;
class StructureBrowser;
struct Options {
  QString provider = "ca";
  bool readOnly = false;
  bool connectOnOpen = true;
  int maxElements = 16384, historySize = 10000;
  double timeout = 5;
};
class Plot final : public QWidget {
public:
  explicit Plot(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(70); }
  QVector<double> values;
  bool timeAxis = false;
  // A sparkline has no axes or labels and is drawn over its parent's background.
  bool sparkline = false;
  int preferredHeight = 130;
  // Alarm limit lines, drawn when they fall within the plotted range.
  QVector<double> warningLines, alarmLines;
  // Enum state names. When set, values are state indices: drawn as steps
  // (a state holds until the next event), with state names on the axis.
  QStringList levels;
  QSize sizeHint() const override { return {320, preferredHeight}; }
protected:
  void paintEvent(QPaintEvent*) override;
};
// Keeps the exact text in text() while painting what fits: the font first
// shrinks toward minimumScale × the application font (when set), then the
// text is elided.
class ElidedLabel final : public QLabel {
public:
  explicit ElidedLabel(Qt::TextElideMode mode = Qt::ElideRight, QWidget* parent = nullptr);
  qreal minimumScale = 0;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
protected:
  void paintEvent(QPaintEvent*) override;
private:
  Qt::TextElideMode mode;
};
// Optional inspector sections, shown below the readback when toggled.
enum Section { TrendSection = 1, AdjustSection = 2, InfoSection = 4, ArraySection = 8, HistorySection = 16, RecordSection = 32, StructureSection = 64, AllSections = 127 };
constexpr int sectionCount = 7;
class Inspector final : public QWidget {
public:
  Inspector(Address, Options, QWidget* parent = nullptr);
  Channel* channel() const { return channel_; }
  void refresh(bool allSections = true);
  void saveSnapshot(const QString&);
  void saveHistory(const QString&);
  // Requested sections; unavailable ones (Array for scalars, Adjust without
  // numeric write access, Structure for CA) stay hidden until the PV supports them.
  int sections() const { return requested; }
  void setSections(int);
  // Height that shows the readback and open sections without scrolling.
  int preferredHeight(int width) const;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  // Set by Window so a toggle applies to every inspector; otherwise local.
  std::function<void(int)> sectionsRequested;
  // Called when visible structure changes, so the window can fit it.
  std::function<void()> layoutChanged;
  // Asked before each write or Process; returning false cancels it.
  std::function<bool(const QString& question)> confirm;
  // Called after a write or Process outcome is added to writeLog().
  std::function<void()> logged;
  // Sends the text like Write; Undo and the editor use this path.
  void requestWrite(const QString&);
  // Writes back the exact elements your last write replaced.
  void requestUndo();
  // Opens a PV named by a record link (a uri such as ca://REC).
  std::function<void(const QString& uri)> openLink;
  void requestProcess();
protected:
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
private:
  Channel* channel_;
  RangeGauge* gauge;
  QToolButton* processButton;
  QPushButton* undo;
  QString undoValue, pendingBefore, pendingAfter;
  QStringList undoValues, pendingBeforeValues;
  QPoint dragStart;
  bool hasUndo = false, pendingLog = false, pendingProcess = false;
  // Whether a byte array was shown as text when the editor's value was entered.
  bool enteredAsText = false;
  QString undoUnavailable, pendingUndoProblem;
  bool beginWrite(const QString& shown);
  QScrollArea* scroll;
  QWidget *body, *writeArea;
  ElidedLabel *heading, *description, *value, *writeStatus;
  QLabel *stateDot, *alarm, *paused, *units, *facts, *arrayNotice, *trendStats, *activityCount, *historyCount;
  QComboBox *format, *enumChoice;
  QLineEdit *input, *filter;
  QPushButton *put, *useReadback;
  QTableWidget *info, *array, *events;
  Plot *waveform, *trendPlot;
  QWidget* numericControls;
  QSlider* slider;
  QDial* dial;
  QDoubleSpinBox *low, *high, *step;
  QToolButton* freeze;
  QToolButton* toggles[sectionCount];
  QWidget* panels[sectionCount];
  RecordExplorer* explorer;
  // Created the first time a PVA structure is shown, so CA inspectors never pay for it.
  StructureBrowser* structure = nullptr;
  QBoxLayout *structureHeader, *structureContent;
  StructureBrowser* structureBrowser();
  QTimer* render;
  quint64 displayedHistorySerial = 0;
  QStringList lastChoices;
  QString lastWriteMessage;
  bool lastWriteOK = true, writableSeen = true;
  bool frozen = false, dirty = true, automaticLimits = true;
  int requested = 0, available = TrendSection | InfoSection | HistorySection | RecordSection, shown = 0, signature = -1;
  void stagePosition(int);
  void refreshWriteControls();
  void applySections();
  // Frees plot and table copies a hidden inspector or closed section would keep.
  void releaseViews(int sections);
  void checkLayout();
  void invalidate();
};
class Window final : public QMainWindow {
public:
  explicit Window(Options = {});
  void addPv(const QString&);
  bool addPvs(const QStringList&);
  void saveSession(const QString&);
  void loadSession(const QString&);
  QStackedWidget* inspectors() const { return stack; }
  void closePv(int index);
  void setAppearance(const QString&);
  void setSections(int);
  void setZoomLevel(int percent);
  void openInStripTool(const QList<Channel*>&);
  void showWriteLog();
  // Opens (or selects) the PV and shows its inspector.
  void followLink(const QString& uri);
protected:
  void closeEvent(QCloseEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
  void changeEvent(QEvent*) override;
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;
private:
  Options options;
  QStackedWidget *stack, *pages;
  // "All (n)" stays pinned at the left; only the PV tabs (tab i is PV i) scroll.
  QWidget* tabStrip;
  QToolButton* allTab;
  QTabBar* pvTabs;
  QComboBox *name, *provider;
  Comparison* comparison;
  QLabel *connectionSummary, *entryError, *statusText;
  QToolButton* exportButton;
  QAction *snapshotAction, *historyAction, *saveSessionAction, *closeAction;
  QAction *allPvsAction, *inspectorAction, *nextPvAction, *previousPvAction;
  bool navigationDirty = true;
  bool comparisonVisible = false, confirmWrites = false;
  int dragTab = -1;
  QPoint dragStart;
  QTimer *navigationTimer, *fitTimer;
  QString appearance, fitKey;
  int sectionMask = 0, fittedHeight = 0, userHeight = 0;
  Inspector* current() const;
  void updateNavigation();
  void invalidateNavigation();
  void focusPvName();
  void openSessionDialog();
  void setComparison(bool);
  void showPv(int index);
  void cyclePv(int step);
  void applyIcons(QWidget*);
  void fitToContents();
  void refreshWriteLog();
  void connectInspector(Inspector*);
};
} // namespace probe
