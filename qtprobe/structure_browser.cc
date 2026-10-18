#include "structure_browser.h"
#include "appearance.h"
#include <QtWidgets>
namespace probe {
namespace {
enum Role { PathRole = Qt::UserRole, NodeRole, ChangedBelowRole };
enum Column { NameColumn, TypeColumn, ValueColumn };
QString join(const QString& path, const QString& name) {
  return path.isEmpty() ? name : name.startsWith('[') ? path + name : path + '.' + name;
}
// NT metadata starts collapsed; the value and other content start open.
bool openByDefault(const FieldNode& node, int depth) {
  static const QStringList metadata{"alarm", "timeStamp", "display", "control", "valueAlarm", "record"};
  return depth == 0 && (node.kind == FieldNode::Structure || node.kind == FieldNode::Union) && !metadata.contains(node.name);
}
QString rowText(const QTreeWidgetItem* item) {
  return StructureBrowser::path(item) + '\t' + item->text(TypeColumn) + '\t' + item->data(ValueColumn, Qt::UserRole).toString();
}
// The exact value: a summary for structures, every retained element for arrays.
QString exactValue(const FieldNode& node) {
  if (node.kind != FieldNode::Array) return node.value;
  QStringList values; for (const auto& element : node.children) values << element.value;
  return '[' + values.join(", ") + (node.omitted ? ", …]" : "]");
}
int countFields(const FieldNode& node) { int n = node.children.size(); for (const auto& child : node.children) n += countFields(child); return n; }
class FieldView final : public QTreeWidget {
public:
  FieldView() {
    setObjectName("structureFields"); setColumnCount(3); setHeaderLabels({"Field", "Type", "Value"});
    setUniformRowHeights(true); setAlternatingRowColors(true); setSelectionMode(ExtendedSelection);
    setEditTriggers(NoEditTriggers); setTextElideMode(Qt::ElideRight); setContextMenuPolicy(Qt::CustomContextMenu);
    header()->setStretchLastSection(true); header()->setSectionsMovable(false); fitColumns();
  }
  QSize sizeHint() const override { return {360, header()->sizeHint().height() + rowHeight() * 12 + 2 * frameWidth()}; }
  QSize minimumSizeHint() const override { return {160, header()->sizeHint().height() + rowHeight() * 4 + 2 * frameWidth()}; }
protected:
  void changeEvent(QEvent* event) override {
    QTreeWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange) { fitColumns(); updateGeometry(); }
  }
  void keyPressEvent(QKeyEvent* event) override {
    if (!event->matches(QKeySequence::Copy)) { QTreeWidget::keyPressEvent(event); return; }
    // Rows in tree order, as path, type and exact value.
    QStringList rows;
    for (QTreeWidgetItemIterator it(this, QTreeWidgetItemIterator::Selected); *it; ++it) if (!StructureBrowser::path(*it).isEmpty()) rows << rowText(*it);
    QApplication::clipboard()->setText(rows.join('\n')); event->accept();
  }
private:
  int rowHeight() const { return fontMetrics().height() + 6; }
  void fitColumns() {
    setColumnWidth(NameColumn, textWidth(font(), "highWarningLimit") + indentation() * 3 + 24);
    // Long structure IDs elide; their tooltip has the full ID.
    setColumnWidth(TypeColumn, textWidth(font(), "structure[]") + 24);
  }
};
} // namespace
StructureBrowser::StructureBrowser(QWidget* parent) : QWidget(parent) {
  setObjectName("structureBrowser");
  tools = new QWidget; auto* row = new QHBoxLayout(tools); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(4);
  filter = new QLineEdit; filter->setObjectName("structureFilter"); filter->setPlaceholderText("Find a field, type or value…");
  filter->setClearButtonEnabled(true); filter->setAccessibleName("Filter structure fields"); row->addWidget(filter, 1);
  auto quiet = [&](const QString& text, const char* name, const QString& tip) {
    auto* b = new QToolButton; b->setText(text); b->setObjectName(name); b->setToolTip(tip); b->setAccessibleName(tip);
    b->setProperty("role", "quiet"); b->setCursor(Qt::PointingHandCursor); row->addWidget(b); return b;
  };
  auto* expand = quiet("Expand", "expandStructure", "Expand all fields");
  auto* collapse = quiet("Collapse", "collapseStructure", "Collapse all fields");
  auto* copy = quiet({}, "copyStructure", "Copy the whole structure");
  copy->setProperty("symbol", int(Symbol::Copy)); copy->setIcon(symbolIcon(Symbol::Copy, colors(copy->palette()).muted));
  auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4);
  summary = new QLabel; summary->setObjectName("structureSummary"); summary->setProperty("role", "muted"); summary->setTextFormat(Qt::PlainText);
  scaleFont(summary, 0.86); layout->addWidget(summary);
  view = new FieldView; layout->addWidget(view, 1);
  auto remember = [this](QTreeWidgetItem* item) {
    if (!applying && filter->text().isEmpty() && !path(item).isEmpty()) open[path(item)] = item->isExpanded();
    // A collapsed field marks its name when something inside it changed.
    item->setForeground(NameColumn, !item->isExpanded() && item->data(0, ChangedBelowRole).toBool() ? QBrush(colors(palette()).accent) : QBrush());
  };
  connect(view, &QTreeWidget::itemExpanded, this, remember);
  connect(view, &QTreeWidget::itemCollapsed, this, remember);
  connect(filter, &QLineEdit::textChanged, this, [this] { applyFilter(); });
  connect(expand, &QToolButton::clicked, this, [this] { for (QTreeWidgetItemIterator it(view); *it; ++it) if ((*it)->childCount()) setOpen(*it, true); });
  connect(collapse, &QToolButton::clicked, this, [this] { for (QTreeWidgetItemIterator it(view); *it; ++it) if ((*it)->childCount()) setOpen(*it, false); });
  connect(copy, &QToolButton::clicked, this, [this] { if (tree) QApplication::clipboard()->setText(structureText(*tree, -1)); });
  connect(view, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    auto* item = view->itemAt(pos);
    if (!node(item)) return;
    const auto where = path(item);
    QMenu menu(this);
    auto* copyValue = menu.addAction("Copy value"); auto* copyPath = menu.addAction("Copy field path");
    auto* copyRow = menu.addAction("Copy row"); auto* copyFields = menu.addAction("Copy field with contents");
    QAction *expandBelow = nullptr, *collapseBelow = nullptr;
    if (item->childCount()) { menu.addSeparator(); expandBelow = menu.addAction("Expand all below"); collapseBelow = menu.addAction("Collapse all below"); }
    auto* picked = menu.exec(view->viewport()->mapToGlobal(pos));
    // Monitor updates while the menu was open may have replaced the tree and its rows.
    item = nullptr;
    for (QTreeWidgetItemIterator it(view); *it; ++it) if (path(*it) == where) { item = *it; break; }
    const auto* field = node(item);
    if (!picked || !field) return;
    auto below = [this](QTreeWidgetItem* top, bool on) {
      std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* i) { if (i->childCount()) setOpen(i, on); for (int c = 0; c < i->childCount(); ++c) walk(i->child(c)); };
      walk(top);
    };
    if (picked == copyValue) QApplication::clipboard()->setText(exactValue(*field));
    else if (picked == copyPath) QApplication::clipboard()->setText(path(item));
    else if (picked == copyRow) QApplication::clipboard()->setText(rowText(item));
    else if (picked == copyFields) QApplication::clipboard()->setText(structureText(*field));
    else if (picked == expandBelow) below(item, true);
    else if (picked == collapseBelow) below(item, false);
  });
  summary->setText("Waiting for data");
}
QSize StructureBrowser::sizeHint() const { return {360, summary->sizeHint().height() + 4 + view->sizeHint().height()}; }
bool StructureBrowser::empty() const { return !view->topLevelItemCount(); }
QString StructureBrowser::path(const QTreeWidgetItem* item) { return item ? item->data(NameColumn, PathRole).toString() : QString(); }
const FieldNode* StructureBrowser::node(const QTreeWidgetItem* item) const {
  return item && tree ? reinterpret_cast<const FieldNode*>(item->data(NameColumn, NodeRole).value<quintptr>()) : nullptr;
}
void StructureBrowser::release() { view->clear(); previous.clear(); latest.clear(); tree.reset(); }
void StructureBrowser::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  // Marks use theme colors; repaint them for a new appearance.
  if ((event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) && tree) { auto held = tree; tree.reset(); previous = latest; setTree(held); }
}
void StructureBrowser::setOpen(QTreeWidgetItem* item, bool on) {
  // During a search, expansion follows the matches and is not remembered.
  if (filter->text().isEmpty()) open[path(item)] = on;
  item->setExpanded(on);
}
void StructureBrowser::setTree(const FieldTree& next) {
  // Repeated refreshes of the same data keep the marks of its update.
  if (next == tree && !empty()) return;
  if (!next) { release(); summary->setText("No structure"); return; }
  tree = next; latest.clear();
  applying = true; view->setUpdatesEnabled(false);
  sync(view->invisibleRootItem(), *tree, {}, -1);
  view->setUpdatesEnabled(true); applying = false;
  previous.swap(latest);
  // Retain expansion only for fields in the current schema.
  for (auto it = open.begin(); it != open.end();) {
    if (!previous.contains(it.key())) it = open.erase(it);
    else ++it;
  }
  const int fields = countFields(*tree);
  QString text = QString("%1 field%2").arg(fields).arg(fields == 1 ? "" : "s");
  if (!tree->type.isEmpty() && tree->type != "structure") text = tree->type + " · " + text;
  summary->setText(text); summary->setToolTip(text);
  if (!filter->text().isEmpty()) applyFilter();
}
bool StructureBrowser::sync(QTreeWidgetItem* parent, const FieldNode& node, const QString& base, int depth) {
  const auto c = colors(palette());
  bool changedBelow = false;
  // Rows match fields by position and path; a different shape rebuilds from there on.
  auto take = [&](int index, const QString& key) {
    auto* item = index < parent->childCount() ? parent->child(index) : nullptr;
    if (item && path(item) == key) return std::make_pair(item, false);
    while (parent->childCount() > index) delete parent->takeChild(index);
    item = new QTreeWidgetItem; item->setData(NameColumn, PathRole, key); parent->addChild(item);
    return std::make_pair(item, true);
  };
  auto set = [](QTreeWidgetItem* item, int column, const QString& text) { if (item->text(column) != text) item->setText(column, text); };
  for (int i = 0; i < node.children.size(); ++i) {
    const auto& child = node.children[i];
    const auto key = join(base, child.name);
    auto [item, created] = take(i, key);
    item->setData(NameColumn, NodeRole, QVariant::fromValue(quintptr(&child)));
    set(item, NameColumn, child.name); set(item, TypeColumn, child.type);
    // Long strings and arrays stay on one row; the tooltip holds more.
    QString shown = child.value; shown.replace('\n', QChar(0x21B5));
    set(item, ValueColumn, shown);
    const bool summaryChanged = item->data(ValueColumn, Qt::UserRole).toString() != child.value;
    if (summaryChanged) item->setData(ValueColumn, Qt::UserRole, child.value);
    // An array's summary shows its first 16 elements; the tooltip lists every received one.
    if (summaryChanged || (child.kind == FieldNode::Array && child.children.size() > 16)) {
      const auto exact = exactValue(child), tip = exact.size() > 60 ? exact.left(2000) : QString();
      if (item->toolTip(ValueColumn) != tip) item->setToolTip(ValueColumn, tip);
    }
    const auto typeTip = child.type.size() > 11 ? child.type : QString();
    if (item->toolTip(TypeColumn) != typeTip) item->setToolTip(TypeColumn, typeTip);
    if (item->foreground(TypeColumn).color() != c.muted) item->setForeground(TypeColumn, QBrush(c.muted));
    const auto old = previous.constFind(key);
    const bool changed = old != previous.cend() && *old != child.value;
    latest.insert(key, child.value);
    const bool inside = sync(item, child, key, depth + 1);
    item->setForeground(ValueColumn, changed ? QBrush(c.accent) : QBrush());
    item->setData(NameColumn, ChangedBelowRole, inside);
    item->setForeground(NameColumn, inside && !item->isExpanded() ? QBrush(c.accent) : QBrush());
    if (created) {
      const auto remembered = open.constFind(key);
      const bool on = remembered != open.cend() ? *remembered : openByDefault(child, depth + 1);
      if (remembered == open.cend()) open.insert(key, on);
      item->setExpanded(on);
    }
    changedBelow = changedBelow || changed || inside;
  }
  int rows = node.children.size();
  if (node.omitted) {
    // A muted row says what the bounds left out; it has no path.
    const char* what = node.kind == FieldNode::Structure ? "fields" : "elements";
    auto* more = rows < parent->childCount() && path(parent->child(rows)).isEmpty() ? parent->child(rows) : nullptr;
    if (!more) { while (parent->childCount() > rows) delete parent->takeChild(rows); more = new QTreeWidgetItem; parent->addChild(more); more->setFlags(Qt::ItemIsEnabled); }
    more->setText(NameColumn, QString("… %1 more %2").arg(node.omitted).arg(what)); more->setForeground(NameColumn, QBrush(c.muted));
    more->setFirstColumnSpanned(true); ++rows;
  }
  while (parent->childCount() > rows) delete parent->takeChild(rows);
  return changedBelow;
}
void StructureBrowser::applyFilter() {
  const auto needle = filter->text().trimmed();
  applying = true;
  std::function<bool(QTreeWidgetItem*, bool)> visit = [&](QTreeWidgetItem* item, bool inherited) {
    const bool self = !needle.isEmpty() && (path(item).contains(needle, Qt::CaseInsensitive) || item->text(TypeColumn).contains(needle, Qt::CaseInsensitive) ||
        item->data(ValueColumn, Qt::UserRole).toString().contains(needle, Qt::CaseInsensitive));
    bool below = false;
    for (int i = 0; i < item->childCount(); ++i) below = visit(item->child(i), inherited || self) || below;
    if (needle.isEmpty()) { item->setHidden(false); if (item->childCount()) item->setExpanded(open.value(path(item))); return false; }
    // Matches show their contents; ancestors of matches open to reveal them.
    item->setHidden(!(inherited || self || below));
    if (below) item->setExpanded(true);
    return self || below;
  };
  for (int i = 0; i < view->topLevelItemCount(); ++i) visit(view->topLevelItem(i), false);
  applying = false;
}
} // namespace probe
