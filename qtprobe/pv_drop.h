#pragma once
#include <QStringList>
#include <functional>
class QWidget;
namespace probe {
// QtEDM exports a whitespace-separated text/plain list in a Copy drag.
// Parse the whole gesture before adding channels; reject files and bad lists.
QStringList droppedPvNames(const QString& text, const QString& provider = "ca");
void installPvDropTarget(QWidget*, std::function<bool(const QStringList&)>, std::function<QString()> defaultProvider);
} // namespace probe
