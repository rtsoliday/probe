// Shared Help menu and offline manual, following the companion Qt ports.
#pragma once
#include <QString>
#include <QUrl>
class QDialog;
class QMenu;
class QWidget;
namespace probe {
QDialog* showHelp(QWidget* parent, const QString& topic = {});
void showAbout(QWidget* parent);
QUrl helpManualUrl();
bool openHelpManual(QWidget* parent);
void populateHelpMenu(QMenu* menu, QWidget* owner);
void updateHelpAppearance(QWidget* owner);
} // namespace probe
