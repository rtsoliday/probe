#include "pv_drop.h"
#include "model.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QWidget>
namespace probe {
QStringList droppedPvNames(const QString& text, const QString& provider) {
  if (text.size() > 65536) return {};
  for (auto ch : text) if ((ch.unicode() < 32 && ch != '\r' && ch != '\n' && ch != '\t') || ch.unicode() == 127 || ch == QChar::ReplacementCharacter) return {};
  const auto tokens = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
  if (tokens.size() > 100) return {};
  QStringList names;
  try {
    for (const auto& token : tokens) {
      if (token.toUtf8().size() > 4096) return {};
      const auto name = Address::parse(token, provider).uri();
      if (!names.contains(name)) names.append(name);
    }
  } catch (const std::exception&) { return {}; }
  return names;
}
namespace {
class PvDropTarget final : public QObject {
public:
  PvDropTarget(QWidget* window, std::function<bool(const QStringList&)> add, std::function<QString()> provider)
      : QObject(window), window(window), add(std::move(add)), provider(std::move(provider)) { window->setAcceptDrops(true); qApp->installEventFilter(this); }
protected:
  bool eventFilter(QObject* object, QEvent* event) override {
    auto* widget = qobject_cast<QWidget*>(object);
    if (!widget || widget->window() != window) return false;
    if (event->type() == QEvent::DragLeave) { pending.clear(); internal = false; return false; }
    if (event->type() != QEvent::DragEnter && event->type() != QEvent::DragMove && event->type() != QEvent::Drop) return false;
    auto* drop = static_cast<QDropEvent*>(event);
    if (event->type() == QEvent::DragEnter) {
      // Cache on enter: an external Qt source may release its selection before
      // Drop arrives. Never request the selection again on each pointer motion.
      pending = drop->mimeData()->hasText() && !drop->mimeData()->hasUrls() ? droppedPvNames(drop->mimeData()->text(), provider()) : QStringList{};
      // A drag from QtProbe's own value editors is an ordinary text edit.
      auto* source = qobject_cast<QWidget*>(drop->source()); internal = source && source->window() == window;
    }
    if (internal) { if (event->type() == QEvent::Drop) internal = false; return false; }
    if (pending.isEmpty() || !(drop->possibleActions() & Qt::CopyAction)) drop->ignore();
    else if (event->type() != QEvent::Drop || add(pending)) { drop->setDropAction(Qt::CopyAction); drop->accept(); }
    else drop->ignore();
    if (event->type() == QEvent::Drop) pending.clear();
    return true;
  }
private:
  QWidget* window;
  std::function<bool(const QStringList&)> add;
  std::function<QString()> provider;
  QStringList pending;
  bool internal = false;
};
} // namespace
void installPvDropTarget(QWidget* window, std::function<bool(const QStringList&)> add, std::function<QString()> provider) { new PvDropTarget(window, std::move(add), std::move(provider)); }
} // namespace probe
