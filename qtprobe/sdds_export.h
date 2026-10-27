// Optional SDDS exports. Distributed under the license in ../LICENSE.
#pragma once
#include "model.h"

namespace probe {
enum class SddsExportKind { History, Array, Snapshot };
bool sddsAvailable();
// Throws std::runtime_error on failure. Only retained, locally owned data is
// exported; no channel reads or writes are performed.
void saveSdds(const QString& path, SddsExportKind kind,
              const Snapshot& snapshot, const History& history);
} // namespace probe
