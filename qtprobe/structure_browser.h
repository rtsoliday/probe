#pragma once
#include "model.h"
#include <QHash>
#include <QWidget>
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
namespace probe {
// Every field of a pvAccess structure as a tree: name, type and live value.
// Updates are applied to the existing rows, so expansion, selection and
// scrolling survive monitor events. Values that changed in the latest update
// are marked; a collapsed field marks its name when something inside changed.
class StructureBrowser final : public QWidget {
public:
  explicit StructureBrowser(QWidget* parent = nullptr);
  // Find, expand, collapse and copy controls, placed in the section header.
  QWidget* controls() const { return tools; }
  void setTree(const FieldTree&);
  // Frees the rows of a hidden or closed section; expansion choices are kept.
  void release();
  bool empty() const;
  // Dotted path of a row (value.position[2]); empty for "… more" rows.
  static QString path(const QTreeWidgetItem*);
  // The field shown by a row, valid until the next setTree().
  const FieldNode* node(const QTreeWidgetItem*) const;
  QSize sizeHint() const override;
protected:
  void changeEvent(QEvent*) override;
private:
  QWidget* tools;
  QLineEdit* filter;
  QLabel* summary;
  QTreeWidget* view;
  FieldTree tree;
  // Expansion by path: the default the first time a field appears, then the user's.
  QHash<QString, bool> open;
  // Values at the previous update, to mark what changed.
  QHash<QString, QString> previous, latest;
  bool applying = false;
  bool sync(QTreeWidgetItem* parent, const FieldNode& node, const QString& path, int depth);
  void applyFilter();
  void setOpen(QTreeWidgetItem*, bool);
};
} // namespace probe
