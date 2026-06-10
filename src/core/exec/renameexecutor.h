#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace fp::exec {

struct Op {
    std::string from;  // current file name (UTF-8, name only — no path)
    std::string to;    // target file name
};

struct OpResult {
    std::string from;
    std::string to;
    std::string error;
};

struct Report {
    size_t total = 0;    // operations to perform (incl. temp hops for cycles)
    size_t done = 0;     // renamed successfully
    size_t failed = 0;   // rename failed (target exists, permissions, ...)
    size_t skipped = 0;  // blocked by a failed predecessor or cancelled
    bool cancelled = false;
    std::vector<OpResult> failures;
};

struct Options {
    unsigned threads = 0;  // 0 = auto (min(8, hardware_concurrency))
    // Called from worker threads after every processed operation.
    std::function<void(size_t processed, size_t total)> onProgress;
    const std::atomic<bool>* cancelFlag = nullptr;
    std::string journalPath;  // empty = no journal; appended as "from\tto\n"
};

// Orders renames into waves: every op's target is vacated by an op in an
// earlier wave; ops inside one wave are independent and can run in parallel.
// Swap cycles (a→b, b→a) are broken with a unique temporary name, which adds
// an extra hop. `existingNamesLower` are case-folded names present in the
// folder (used to pick collision-free temp names). Exposed for tests.
std::vector<std::vector<Op>> orderOperations(
    std::vector<Op> ops, const std::vector<std::string>& existingNamesLower);

// Executes the renames inside `folderUtf8` on a thread pool, wave by wave.
// A failed op marks its source name as stuck: dependents are skipped, the
// rest of the plan continues. Cancellation is cooperative and keeps the
// journal consistent — completed renames stay recorded.
Report execute(const std::string& folderUtf8, const std::vector<Op>& ops,
               const Options& opts = {});

// Replays the journal backwards (to→from), restoring original names.
Report undoFromJournal(const std::string& folderUtf8,
                       const std::string& journalPath,
                       const Options& opts = {});

}  // namespace fp::exec
