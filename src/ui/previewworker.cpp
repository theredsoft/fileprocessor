#include "previewworker.h"

#include "core/template/template.h"

void PreviewWorker::compute(quint64 generation, const QString& templateText,
                            const QString& folderName,
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

    QList<PreviewItem> items;
    items.reserve(files.size());
    const std::string folder = folderName.toStdString();
    long index = 0;
    for (const FileEntry& f : files) {
        if ((index & 0xFF) == 0 && currentGeneration_->load() != generation)
            return;

        fp::tmpl::FileContext ctx;
        ctx.folderName = folder;
        ctx.stem = f.stem.toStdString();
        ctx.ext = f.suffix.toStdString();
        ctx.index = index++;
        if (f.lastModified.isValid()) {
            const QDate d = f.lastModified.date();
            const QTime t = f.lastModified.time();
            ctx.fileDate = fp::tmpl::DateTime{d.year(),  d.month(),  d.day(),
                                              t.hour(),  t.minute(), t.second()};
        }
        // ctx.photoDate: EXIF reader arrives in milestone 7

        try {
            items.append({QString::fromStdString(tmpl->apply(ctx)), false});
        } catch (const fp::tmpl::EvalError& e) {
            items.append(
                {QStringLiteral("⚠ ") + QString::fromUtf8(e.what()), true});
        }
    }
    emit ready(generation, items);
}
