#include "core/exec/renameexecutor.h"

#include <filesystem>
#include <fstream>
#include <set>

#include "minitest.h"

namespace fs = std::filesystem;
using namespace fp::exec;

namespace {

// Self-cleaning temp folder; each test gets a fresh one.
struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        path = fs::temp_directory_path() /
               ("fp_exec_test_" + std::to_string(++counter));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }

    void addFile(const std::string& name, const std::string& content) const {
        std::ofstream(path / name) << content;
    }
    std::string read(const std::string& name) const {
        std::ifstream in(path / name);
        std::string s;
        std::getline(in, s);
        return s;
    }
    std::set<std::string> names() const {
        std::set<std::string> out;
        for (const auto& e : fs::directory_iterator(path))
            out.insert(e.path().filename().string());
        return out;
    }
    std::string utf8() const { return path.string(); }
};

size_t totalOps(const std::vector<std::vector<Op>>& waves) {
    size_t n = 0;
    for (const auto& w : waves) n += w.size();
    return n;
}

}  // namespace

TEST(order_independent_ops_form_one_wave) {
    const auto waves = orderOperations({{"a", "x"}, {"b", "y"}, {"c", "z"}}, {});
    CHECK_EQ(waves.size(), static_cast<size_t>(1));
    CHECK_EQ(waves[0].size(), static_cast<size_t>(3));
}

TEST(order_chain_is_sequenced) {
    // 1→2 must wait until 2→3 vacates "2".
    const auto waves = orderOperations({{"1", "2"}, {"2", "3"}}, {});
    CHECK_EQ(waves.size(), static_cast<size_t>(2));
    CHECK_EQ(waves[0][0].from, std::string("2"));
    CHECK_EQ(waves[1][0].from, std::string("1"));
}

TEST(order_cycle_gets_temp_hop) {
    const auto waves = orderOperations({{"a", "b"}, {"b", "a"}}, {});
    CHECK_EQ(totalOps(waves), static_cast<size_t>(3));  // one extra hop
    CHECK_EQ(waves.size(), static_cast<size_t>(3));
}

TEST(order_case_only_rename_is_unblocked) {
    const auto waves = orderOperations({{"img.jpg", "IMG.jpg"}}, {});
    CHECK_EQ(waves.size(), static_cast<size_t>(1));
}

TEST(execute_simple_renames) {
    TempDir dir;
    dir.addFile("a.txt", "A");
    dir.addFile("b.txt", "B");
    const auto report =
        execute(dir.utf8(), {{"a.txt", "x.txt"}, {"b.txt", "y.txt"}});
    CHECK_EQ(report.done, static_cast<size_t>(2));
    CHECK_EQ(report.failed, static_cast<size_t>(0));
    CHECK(dir.names() == std::set<std::string>({"x.txt", "y.txt"}));
    CHECK_EQ(dir.read("x.txt"), std::string("A"));
}

TEST(execute_chain_preserves_contents) {
    TempDir dir;
    dir.addFile("1.txt", "one");
    dir.addFile("2.txt", "two");
    const auto report =
        execute(dir.utf8(), {{"1.txt", "2.txt"}, {"2.txt", "3.txt"}});
    CHECK_EQ(report.done, static_cast<size_t>(2));
    CHECK(dir.names() == std::set<std::string>({"2.txt", "3.txt"}));
    CHECK_EQ(dir.read("2.txt"), std::string("one"));
    CHECK_EQ(dir.read("3.txt"), std::string("two"));
}

TEST(execute_swap_cycle) {
    TempDir dir;
    dir.addFile("a.txt", "A");
    dir.addFile("b.txt", "B");
    const auto report =
        execute(dir.utf8(), {{"a.txt", "b.txt"}, {"b.txt", "a.txt"}});
    CHECK_EQ(report.failed, static_cast<size_t>(0));
    CHECK(dir.names() == std::set<std::string>({"a.txt", "b.txt"}));
    CHECK_EQ(dir.read("a.txt"), std::string("B"));
    CHECK_EQ(dir.read("b.txt"), std::string("A"));
}

TEST(execute_occupied_target_fails_safely) {
    TempDir dir;
    dir.addFile("a.txt", "A");
    dir.addFile("b.txt", "B");  // not part of the rename set
    const auto report = execute(dir.utf8(), {{"a.txt", "b.txt"}});
    CHECK_EQ(report.failed, static_cast<size_t>(1));
    CHECK_EQ(report.done, static_cast<size_t>(0));
    CHECK_EQ(dir.read("b.txt"), std::string("B"));  // nothing overwritten
    CHECK(!report.failures.empty());
}

TEST(execute_missing_source_fails_but_rest_continues) {
    TempDir dir;
    dir.addFile("ok.txt", "OK");
    const auto report =
        execute(dir.utf8(), {{"ghost.txt", "g.txt"}, {"ok.txt", "fine.txt"}});
    CHECK_EQ(report.done, static_cast<size_t>(1));
    CHECK_EQ(report.failed, static_cast<size_t>(1));
    CHECK(dir.names() == std::set<std::string>({"fine.txt"}));
}

TEST(execute_cancellation_skips_everything) {
    TempDir dir;
    dir.addFile("a.txt", "A");
    std::atomic<bool> cancel{true};
    Options opts;
    opts.cancelFlag = &cancel;
    const auto report = execute(dir.utf8(), {{"a.txt", "b.txt"}}, opts);
    CHECK(report.cancelled);
    CHECK_EQ(report.done, static_cast<size_t>(0));
    CHECK(dir.names() == std::set<std::string>({"a.txt"}));
}

TEST(execute_reports_progress) {
    TempDir dir;
    for (int i = 0; i < 5; ++i)
        dir.addFile("f" + std::to_string(i) + ".txt", "x");
    size_t calls = 0;
    Options opts;
    opts.threads = 1;  // deterministic callback count
    opts.onProgress = [&calls](size_t, size_t) { ++calls; };
    std::vector<Op> ops;
    for (int i = 0; i < 5; ++i)
        ops.push_back({"f" + std::to_string(i) + ".txt",
                       "r" + std::to_string(i) + ".txt"});
    const auto report = execute(dir.utf8(), ops, opts);
    CHECK_EQ(report.done, static_cast<size_t>(5));
    CHECK_EQ(calls, static_cast<size_t>(5));
}

TEST(journal_roundtrip_undo) {
    TempDir dir;
    dir.addFile("1.txt", "one");
    dir.addFile("2.txt", "two");
    const std::string journal = (dir.path / "journal.log").string();

    Options opts;
    opts.journalPath = journal;
    const auto report =
        execute(dir.utf8(), {{"1.txt", "2.txt"}, {"2.txt", "3.txt"}}, opts);
    CHECK_EQ(report.done, static_cast<size_t>(2));

    const auto undo = undoFromJournal(dir.utf8(), journal);
    CHECK_EQ(undo.failed, static_cast<size_t>(0));
    CHECK_EQ(dir.read("1.txt"), std::string("one"));
    CHECK_EQ(dir.read("2.txt"), std::string("two"));
}

TEST(journal_undo_restores_swap_cycle) {
    TempDir dir;
    dir.addFile("a.txt", "A");
    dir.addFile("b.txt", "B");
    const std::string journal = (dir.path / "journal.log").string();

    Options opts;
    opts.journalPath = journal;
    execute(dir.utf8(), {{"a.txt", "b.txt"}, {"b.txt", "a.txt"}}, opts);

    const auto undo = undoFromJournal(dir.utf8(), journal);
    CHECK_EQ(undo.failed, static_cast<size_t>(0));
    CHECK_EQ(dir.read("a.txt"), std::string("A"));
    CHECK_EQ(dir.read("b.txt"), std::string("B"));
}

TEST(execute_many_files_in_parallel) {
    TempDir dir;
    std::vector<Op> ops;
    for (int i = 0; i < 500; ++i) {
        const std::string name = "file_" + std::to_string(i) + ".txt";
        dir.addFile(name, std::to_string(i));
        ops.push_back({name, "img_" + std::to_string(i) + ".txt"});
    }
    const auto report = execute(dir.utf8(), ops);
    CHECK_EQ(report.done, static_cast<size_t>(500));
    CHECK_EQ(report.failed, static_cast<size_t>(0));
    CHECK_EQ(dir.read("img_123.txt"), std::string("123"));
}
