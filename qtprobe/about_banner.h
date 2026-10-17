// Decorative About illustration; this widget never connects to a PV.
#pragma once
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
namespace probe {
class AboutBanner final : public QWidget {
public:
  explicit AboutBanner(QWidget* parent = nullptr);
  QSize sizeHint() const override { return QSize(460, 140); }
  bool scanning() const { return scanTime >= 0; }
  bool startScan();
  void advance(qreal seconds);
protected:
  void paintEvent(QPaintEvent*) override;
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
private:
  QTimer timer;
  QElapsedTimer clock;
  qreal time = 0, scanTime = -1;
};
} // namespace probe
