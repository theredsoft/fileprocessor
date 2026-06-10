#include "previewworker.h"

#include <QDir>

#include "core/planner/renameplanner.h"
#include "infra/exifreader.h"

std::optional<fp::tmpl::DateTime> PreviewWorker::photoDateFor(
    const QString& folderPath, const FileEntry& f) {
    if (folderPath != cachedFolder_) {
        exifCache_.clear();
        cachedFolder_ = folderPath;
    }
    const QString key =
        f.fileName + QLatin1Char('#') +
        QString::number(f.lastModified.toMSecsSinceEpoch());
    auto it = exifCache_.constFind(key);
    if (it != exifCache_.constEnd()) return it.value();

    const auto date = fp::infra::readPhotoDate(
        (folderPath + QLatin1Char('/') + f.fileName).toStdString());
    exifCache_.insert(key, date);
    return date;
}

void PreviewWorker::compute(quint64 generation, const QString& templateText,
                            const QString& folderPath,
                            const QList<FileEntry>& files) {
    if (currentGeneration_->load() != generation) return;

    std::optional<fp::tmpl::Template> tmpl;
    try {
        tmpl = fp::tmpl::Template::parse(templateText.toStdString());
    } catch (const fp::tmpl::ParseError& e) {
        emit parseFailed(generation, static_cast<qsizetype>(e.pos),
                         QString::fromUtf8(e.what()));
        return;
    }
    const bool needsExif = tmpl->uses(fp::tmpl::Source::PhotoDate);

    std::vector<fp::plan::SourceFile> sources;
    sources.reserve(files.size());
    const std::string folder = QDir(folderPath).dirName().toStdString();
    long index = 0;
    for (const FileEntry& f : files) {
        if ((index & 0x3F) == 0 && currentGeneration_->load() != generation)
            return;

        fp::plan::SourceFile src;
        src.name = f.fileName.toStdString();
        src.ctx.folderName = folder;
        src.ctx.stem = f.stem.toStdString();
        src.ctx.ext = f.suffix.toStdString();
        src.ctx.index = index++;
        if (f.lastModified.isValid()) {
            const QDate d = f.lastModified.date();
            const QTime t = f.lastModified.time();
            src.ctx.fileDate = fp::tmpl::DateTime{
                d.year(), d.month(), d.day(), t.hour(), t.minute(), t.second()};
        }
        if (needsExif) src.ctx.photoDate = photoDateFor(folderPath, f);
        sources.push_back(std::move(src));
    }
    if (currentGeneration_->load() != generation) return;

    const fp::plan::Plan plan = fp::plan::buildPlan(*tmpl, sources);

    QList<PreviewItem> items;
    items.reserve(static_cast<qsizetype>(plan.items.size()));
    QList<RenameOp> ops;
    ops.reserve(static_cast<qsizetype>(plan.renameCount));
    for (const fp::plan::PlanItem& p : plan.items) {
        switch (p.issue) {
        case fp::plan::Issue::None:
            items.append({QString::fromStdString(p.newName),
                          PreviewItem::Kind::Rename});
            ops.append({QString::fromStdString(p.oldName),
                        QString::fromStdString(p.newName)});
            break;
        case fp::plan::Issue::Unchanged:
            items.append({QString::fromStdString(p.newName),
                          PreviewItem::Kind::Unchanged});
            break;
        default:
            items.append({QStringLiteral("⚠ ") + QString::fromStdString(p.message),
                          PreviewItem::Kind::Conflict});
            break;
        }
    }
    emit ready(generation, items, static_cast<qsizetype>(plan.renameCount),
               static_cast<qsizetype>(plan.conflictCount), ops);
}
