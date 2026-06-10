#include "renameworker.h"

#include <chrono>
#include <memory>

#include "core/exec/renameexecutor.h"

namespace {

QStringList collectErrors(const fp::exec::Report& report) {
    QStringList errors;
    for (const fp::exec::OpResult& f : report.failures) {
        errors << QStringLiteral("%1 → %2: %3")
                      .arg(QString::fromStdString(f.from),
                           QString::fromStdString(f.to),
                           QString::fromStdString(f.error));
        if (errors.size() >= 20) {
            errors << QStringLiteral("…");
            break;
        }
    }
    return errors;
}

}  // namespace

void RenameWorker::run(quint64 runId, const QString& folder,
                       const QList<RenameOp>& ops, const QString& journalPath) {
    std::vector<fp::exec::Op> coreOps;
    coreOps.reserve(static_cast<size_t>(ops.size()));
    for (const RenameOp& op : ops)
        coreOps.push_back({op.from.toStdString(), op.to.toStdString()});

    fp::exec::Options opts;
    opts.cancelFlag = cancelFlag_;
    opts.journalPath = journalPath.toStdString();
    auto lastEmitMs = std::make_shared<std::atomic<long long>>(0);
    opts.onProgress = [this, runId, lastEmitMs](size_t processed, size_t total) {
        using namespace std::chrono;
        const long long now =
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
                .count();
        const long long prev = lastEmitMs->load();
        if (processed != total && now - prev < 100) return;
        lastEmitMs->store(now);
        emit progress(runId, static_cast<qsizetype>(processed),
                      static_cast<qsizetype>(total));
    };

    const fp::exec::Report report =
        fp::exec::execute(folder.toStdString(), coreOps, opts);
    emit finished(runId, static_cast<qsizetype>(report.done),
                  static_cast<qsizetype>(report.failed),
                  static_cast<qsizetype>(report.skipped), report.cancelled,
                  collectErrors(report));
}

void RenameWorker::undo(quint64 runId, const QString& folder,
                        const QString& journalPath) {
    fp::exec::Options opts;
    opts.cancelFlag = cancelFlag_;
    opts.onProgress = [this, runId](size_t processed, size_t total) {
        if (processed % 100 == 0 || processed == total)
            emit progress(runId, static_cast<qsizetype>(processed),
                          static_cast<qsizetype>(total));
    };
    const fp::exec::Report report =
        fp::exec::undoFromJournal(folder.toStdString(),
                                  journalPath.toStdString(), opts);
    emit finished(runId, static_cast<qsizetype>(report.done),
                  static_cast<qsizetype>(report.failed),
                  static_cast<qsizetype>(report.skipped), report.cancelled,
                  collectErrors(report));
}
