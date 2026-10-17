#pragma once
#include <QStringList>
#include <functional>
class QWidget;
namespace probe {
// Accepts PV drops from legacy Motif MEDM (the Motif DROP_ONLY protocol) on
// X11. QtEDM drops use Qt drag and drop; see pv_drop.h. No-op elsewhere.
void installMotifPvDropTarget(QWidget* window, std::function<bool(const QStringList&)> addPvs,
                              std::function<QString()> defaultProvider);
} // namespace probe
