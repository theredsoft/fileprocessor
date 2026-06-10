#include "models.h"

#include <QBrush>
#include <QColor>

int FileListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(names_.size());
}

QVariant FileListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= names_.size()) return {};
    if (role == Qt::DisplayRole) return names_.at(index.row());
    return {};
}

void FileListModel::clear() {
    beginResetModel();
    names_.clear();
    endResetModel();
}

void FileListModel::appendNames(const QStringList& names) {
    if (names.isEmpty()) return;
    const int first = static_cast<int>(names_.size());
    beginInsertRows({}, first, first + static_cast<int>(names.size()) - 1);
    names_ += names;
    endInsertRows();
}

int PreviewListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

QVariant PreviewListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= items_.size()) return {};
    const PreviewItem& item = items_.at(index.row());
    if (role == Qt::DisplayRole) return item.text;
    if (role == Qt::ForegroundRole) {
        if (item.kind == PreviewItem::Kind::Conflict)
            return QBrush(QColor(0xc0, 0x39, 0x2b));
        if (item.kind == PreviewItem::Kind::Unchanged)
            return QBrush(QColor(0x88, 0x88, 0x88));
    }
    return {};
}

void PreviewListModel::clear() {
    beginResetModel();
    items_.clear();
    endResetModel();
}

void PreviewListModel::setItems(QList<PreviewItem> items) {
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
}
