#include "core/planner/renameplanner.h"

#include "minitest.h"

using namespace fp::plan;
using fp::tmpl::DateTime;
using fp::tmpl::FileContext;
using fp::tmpl::Template;

namespace {

SourceFile makeFile(const std::string& stem, const std::string& ext, long index) {
    SourceFile f;
    f.name = ext.empty() ? stem : stem + "." + ext;
    f.ctx.folderName = "Vacation";
    f.ctx.stem = stem;
    f.ctx.ext = ext;
    f.ctx.index = index;
    f.ctx.fileDate = DateTime{2026, 5, 12, 14, 30, 7};
    return f;
}

std::string check(const std::string& name) {
    std::string message;
    validateWindowsName(name, message);
    return message;  // empty when valid
}

Issue issueOf(const std::string& name) {
    std::string message;
    return validateWindowsName(name, message);
}

}  // namespace

TEST(plan_basic_rename) {
    const auto t = Template::parse("{folder}_{number = 01}.{ext}");
    const auto plan = buildPlan(t, {makeFile("a", "jpg", 0), makeFile("b", "jpg", 1)});
    CHECK_EQ(plan.items[0].newName, std::string("Vacation_01.jpg"));
    CHECK_EQ(plan.items[1].newName, std::string("Vacation_02.jpg"));
    CHECK_EQ(plan.renameCount, static_cast<size_t>(2));
    CHECK_EQ(plan.conflictCount, static_cast<size_t>(0));
    CHECK(plan.executable());
}

TEST(plan_unchanged_files_are_not_renamed) {
    const auto t = Template::parse("{name}.{ext}");
    const auto plan = buildPlan(t, {makeFile("a", "jpg", 0)});
    CHECK(plan.items[0].issue == Issue::Unchanged);
    CHECK_EQ(plan.renameCount, static_cast<size_t>(0));
    CHECK_EQ(plan.conflictCount, static_cast<size_t>(0));
    CHECK(!plan.executable());
}

TEST(plan_duplicate_targets_clash) {
    const auto t = Template::parse("{folder}.{ext}");  // no {number} → same name
    const auto plan = buildPlan(t, {makeFile("a", "jpg", 0), makeFile("b", "jpg", 1)});
    CHECK(plan.items[0].issue == Issue::NameClash);
    CHECK(plan.items[1].issue == Issue::NameClash);
    CHECK_EQ(plan.conflictCount, static_cast<size_t>(2));
    CHECK(!plan.executable());
}

TEST(plan_clash_with_file_that_keeps_its_name) {
    // "a.jpg" stays as is; "A 1.jpg" → lowercase+nospaces → "a1.jpg"... make a
    // true collision: "A.jpg" lowercases to "a.jpg" which is kept by file one.
    const auto t = Template::parse("{name AND lowercase}.{ext}");
    const auto plan = buildPlan(t, {makeFile("a", "jpg", 0), makeFile("A", "jpg", 1)});
    CHECK(plan.items[0].issue == Issue::Unchanged);
    CHECK(plan.items[1].issue == Issue::NameClash);
    CHECK_EQ(plan.conflictCount, static_cast<size_t>(1));
}

TEST(plan_taking_a_vacated_name_is_allowed) {
    // "1.jpg" → "2.jpg" while "2.jpg" → "3.jpg": the target is being vacated,
    // so this is not a clash — the executor handles the ordering.
    const auto t = Template::parse("{number = 2}.{ext}");
    const auto plan = buildPlan(t, {makeFile("1", "jpg", 0), makeFile("2", "jpg", 1)});
    CHECK_EQ(plan.items[0].newName, std::string("2.jpg"));
    CHECK_EQ(plan.items[1].newName, std::string("3.jpg"));
    CHECK(plan.items[0].issue == Issue::None);
    CHECK(plan.items[1].issue == Issue::None);
    CHECK_EQ(plan.renameCount, static_cast<size_t>(2));
    CHECK(plan.executable());
}

TEST(plan_case_only_rename_is_allowed) {
    const auto t = Template::parse("{name AND uppercase}.{ext}");
    const auto plan = buildPlan(t, {makeFile("img", "jpg", 0)});
    CHECK(plan.items[0].issue == Issue::None);
    CHECK_EQ(plan.items[0].newName, std::string("IMG.jpg"));
    CHECK(plan.executable());
}

TEST(plan_eval_failure_is_a_conflict) {
    const auto t = Template::parse("{photodate}.{ext}");
    auto f = makeFile("a", "jpg", 0);  // no photoDate in context
    const auto plan = buildPlan(t, {f});
    CHECK(plan.items[0].issue == Issue::EvalFailed);
    CHECK_EQ(plan.conflictCount, static_cast<size_t>(1));
}

TEST(validate_forbidden_characters) {
    CHECK(issueOf("a:b.txt") == Issue::InvalidChar);
    CHECK(issueOf("a/b.txt") == Issue::InvalidChar);
    CHECK(issueOf("a?b.txt") == Issue::InvalidChar);
    CHECK(issueOf(std::string("a\tb.txt")) == Issue::InvalidChar);
    CHECK(issueOf("обычное имя.txt") == Issue::None);
}

TEST(validate_reserved_device_names) {
    CHECK(issueOf("CON") == Issue::ReservedName);
    CHECK(issueOf("con.txt") == Issue::ReservedName);
    CHECK(issueOf("Com3.dat") == Issue::ReservedName);
    CHECK(issueOf("lpt9") == Issue::ReservedName);
    CHECK(issueOf("CONSOLE.txt") == Issue::None);
    CHECK(issueOf("COM10.txt") == Issue::None);
}

TEST(validate_trailing_dot_or_space) {
    CHECK(issueOf("name.") == Issue::TrailingDotOrSpace);
    CHECK(issueOf("name ") == Issue::TrailingDotOrSpace);
}

TEST(validate_empty_and_too_long) {
    CHECK(issueOf("") == Issue::EmptyName);
    CHECK(issueOf(std::string(256, 'x')) == Issue::TooLong);
    CHECK(issueOf(std::string(255, 'x')) == Issue::None);
}

TEST(validate_messages_are_filled) {
    CHECK(!check("a|b").empty());
    CHECK(check("fine.txt").empty());
}
