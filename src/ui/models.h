#pragma once

#include <QAbstractListModel>
#include <QStringList>

#include "fileentry.h"

// Both panes use plain list models over pre-built rows: with 10k files the
// view must stay virtualized and the model must not own heavy per-row state.

class FileListModel : public QAbstractListModel {
    Q_OBJECT
public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void clear();
    void appendNames(const QStringList& names);  // scanner batches

private:
    QStringList names_;
};

class PreviewListModel : public QAbstractListModel {
    Q_OBJECT
public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    void clear();
    void setItems(QList<PreviewItem> items);

private:
    QList<PreviewItem> items_;
};
