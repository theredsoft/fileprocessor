#include "core/exec/renameexecutor.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "core/template/textutil.h"

namespace fs = std::filesystem;

namespace fp::exec {
namespace {

constexpr size_t kNoBlocker = static_cast<size_t>(-1);

std::string pathToUtf8(const fs::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

fs::path toPath(const std::string& utf8) {
#ifdef _WIN32
    // fs::path(std::string) would interpret bytes in the ANSI codepage.
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                        static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), len);
    return fs::path(wide);
#else
    return fs::path(utf8);
#endif
}

fs::path joinPath(const fs::path& folder, const std::string& nameUtf8) {
    fs::path p = folder / toPath(nameUtf8);
#ifdef _WIN32
    // Lift the MAX_PATH limit for deep folders.
    if (p.native().size() > 250 && p.is_absolute() &&
        p.native().rfind(L"\\\\?\\", 0) != 0)
        p = fs::path(L"\\\\?\\" + p.native());
#endif
    return p;
}

unsigned threadCount(const Options& opts, size_t workItems) {
    unsigned n = opts.threads != 0 ? opts.threads
                                   : std::min(8u, std::thread::hardware_concurrency());
    if (n == 0) n = 4;
    return static_cast<unsigned>(std::min<size_t>(n, workItems));
}

// Thread-safe append-only journal; every line is flushed so a crash loses at
// most the operation in flight.
class Journal {
public:
    explicit Journal(const std::string& path) {
        if (!path.empty()) out_.open(toPath(path), std::ios::app);
    }
    void append(const Op& op) {
        if (!out_.is_open()) return;
        const std::lock_guard<std::mutex> lock(mu_);
        out_ << op.from << '\t' << op.to << '\n';
        out_.flush();
    }

private:
    std::mutex mu_;
    std::ofstream out_;
};

std::string renameOne(const fs::path& folder, const Op& op) {
    const fs::path from = joinPath(folder, op.from);
    const fs::path to = joinPath(folder, op.to);
    std::error_code ec;
    // POSIX rename() silently overwrites; refuse instead. A pure case change
    // points at the same file on case-insensitive filesystems — allow it.
    const bool caseOnly = text::toLower(op.from) == text::toLower(op.to);
    if (!caseOnly && fs::exists(to, ec)) return "target name already exists";
    fs::rename(from, to, ec);
    if (ec) return ec.message();
    return {};
}

}  // namespace

std::vector<std::vector<Op>> orderOperations(
    std::vector<Op> ops, const std::vector<std::string>& existingNamesLower) {
    const size_t n = ops.size();

    std::unordered_set<std::string> taken(existingNamesLower.begin(),
                                          existingNamesLower.end());
    std::unordered_map<std::string, size_t> byFrom;
    byFrom.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        taken.insert(text::toLower(ops[i].from));
        taken.insert(text::toLower(ops[i].to));
        byFrom[text::toLower(ops[i].from)] = i;
    }

    // blocker[i] = op that must vacate ops[i].to first (at most one: source
    // names are unique). A case-only rename never blocks itself.
    std::vector<size_t> blocker(n, kNoBlocker);
    auto findBlocker = [&](size_t i) {
        const auto it = byFrom.find(text::toLower(ops[i].to));
        return (it != byFrom.end() && it->second != i) ? it->second : kNoBlocker;
    };
    for (size_t i = 0; i < n; ++i) blocker[i] = findBlocker(i);

    // Break swap cycles: move one member to a unique temp name first, then
    // finish the hop after the cycle unwinds.
    size_t tempCounter = 0;
    auto makeTempName = [&] {
        std::string name;
        do {
            name = "~fp_tmp_" + std::to_string(tempCounter++) + ".tmp";
        } while (taken.count(name) != 0);
        taken.insert(name);
        return name;
    };

    enum class Mark : unsigned char { Unvisited, InProgress, Done };
    std::vector<Mark> mark(n, Mark::Unvisited);
    for (size_t start = 0; start < ops.size() && start < n; ++start) {
        if (mark[start] != Mark::Unvisited) continue;
        std::vector<size_t> path;
        size_t cur = start;
        while (cur != kNoBlocker && mark[cur] == Mark::Unvisited) {
            mark[cur] = Mark::InProgress;
            path.push_back(cur);
            cur = blocker[cur];
        }
        if (cur != kNoBlocker && mark[cur] == Mark::InProgress) {
            // `cur` re-entered the current path: cycle found. Split it:
            // from→to becomes from→temp (unblocked now) + temp→to (last).
            const std::string temp = makeTempName();
            const size_t tail = ops.size();
            ops.push_back({temp, ops[cur].to});
            blocker.push_back(blocker[cur]);
            mark.push_back(Mark::Done);
            ops[cur].to = temp;
            blocker[cur] = kNoBlocker;
            // The op that waited for `cur` keeps waiting for it — correct:
            // from→temp vacates `from` all the same.
            (void)tail;
        }
        for (const size_t i : path) mark[i] = Mark::Done;
    }

    // depth = position of the op in its chain; memoized walk, no recursion.
    std::vector<size_t> depth(ops.size(), kNoBlocker);
    for (size_t i = 0; i < ops.size(); ++i) {
        if (depth[i] != kNoBlocker) continue;
        std::vector<size_t> path;
        size_t cur = i;
        while (cur != kNoBlocker && depth[cur] == kNoBlocker) {
            path.push_back(cur);
            cur = blocker[cur];
        }
        size_t base = (cur == kNoBlocker) ? 0 : depth[cur] + 1;
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            depth[*it] = base++;
        }
    }

    size_t maxDepth = 0;
    for (const size_t d : depth) maxDepth = std::max(maxDepth, d);
    std::vector<std::vector<Op>> waves(ops.empty() ? 0 : maxDepth + 1);
    for (size_t i = 0; i < ops.size(); ++i)
        waves[depth[i]].push_back(std::move(ops[i]));
    return waves;
}

Report execute(const std::string& folderUtf8, const std::vector<Op>& ops,
               const Options& opts) {
    Report report;
    if (ops.empty()) return report;

    const fs::path folder = toPath(folderUtf8);

    std::vector<std::string> existing;
    {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(folder, ec))
            existing.push_back(text::toLower(pathToUtf8(entry.path().filename())));
    }

    auto waves = orderOperations(ops, existing);
    for (const auto& wave : waves) report.total += wave.size();

    Journal journal(opts.journalPath);
    std::mutex mu;  // guards stuckNames and report counters/failures
    std::unordered_set<std::string> stuckNames;
    std::atomic<size_t> processed{0};

    auto notify = [&] {
        if (opts.onProgress)
            opts.onProgress(processed.fetch_add(1) + 1, report.total);
        else
            processed.fetch_add(1);
    };
    auto isCancelled = [&] {
        return opts.cancelFlag != nullptr && opts.cancelFlag->load();
    };

    for (const auto& wave : waves) {
        std::atomic<size_t> next{0};
        auto workerLoop = [&] {
            while (true) {
                const size_t k = next.fetch_add(1);
                if (k >= wave.size()) return;
                const Op& op = wave[k];

                if (isCancelled()) {
                    const std::lock_guard<std::mutex> lock(mu);
                    ++report.skipped;
                    report.cancelled = true;
                    stuckNames.insert(text::toLower(op.from));
                    notify();
                    continue;
                }
                {
                    const std::lock_guard<std::mutex> lock(mu);
                    if (stuckNames.count(text::toLower(op.to)) != 0) {
                        ++report.skipped;
                        stuckNames.insert(text::toLower(op.from));
                        notify();
                        continue;
                    }
                }
                const std::string error = renameOne(folder, op);
                if (error.empty()) {
                    journal.append(op);
                    {
                        const std::lock_guard<std::mutex> lock(mu);
                        ++report.done;
                    }
                } else {
                    const std::lock_guard<std::mutex> lock(mu);
                    ++report.failed;
                    stuckNames.insert(text::toLower(op.from));
                    report.failures.push_back({op.from, op.to, error});
                }
                notify();
            }
        };

        const unsigned nThreads = threadCount(opts, wave.size());
        if (nThreads <= 1) {
            workerLoop();
        } else {
            std::vector<std::thread> pool;
            pool.reserve(nThreads);
            for (unsigned t = 0; t < nThreads; ++t) pool.emplace_back(workerLoop);
            for (auto& th : pool) th.join();
        }
    }
    return report;
}

Report undoFromJournal(const std::string& folderUtf8,
                       const std::string& journalPath, const Options& opts) {
    std::vector<Op> recorded;
    {
        std::ifstream in(toPath(journalPath));
        std::string line;
        while (std::getline(in, line)) {
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            recorded.push_back({line.substr(0, tab), line.substr(tab + 1)});
        }
    }

    Report report;
    report.total = recorded.size();
    const fs::path folder = toPath(folderUtf8);

    // Strictly reverse order, single-threaded: undo must unwind chains and
    // temp hops exactly backwards.
    size_t processed = 0;
    for (auto it = recorded.rbegin(); it != recorded.rend(); ++it) {
        if (opts.cancelFlag != nullptr && opts.cancelFlag->load()) {
            report.cancelled = true;
            report.skipped = report.total - processed;
            break;
        }
        const Op reversed{it->to, it->from};
        const std::string error = renameOne(folder, reversed);
        if (error.empty()) {
            ++report.done;
        } else {
            ++report.failed;
            report.failures.push_back({reversed.from, reversed.to, error});
        }
        ++processed;
        if (opts.onProgress) opts.onProgress(processed, report.total);
    }
    return report;
}

}  // namespace fp::exec
