#include "core/template/template.h"

#include "minitest.h"

using namespace fp::tmpl;

namespace {

FileContext makeCtx() {
    FileContext c;
    c.folderName = "Vacation";
    c.stem = "IMG 0042";
    c.ext = "jpg";
    c.index = 0;
    c.fileDate = DateTime{2026, 5, 12, 14, 30, 7};
    c.photoDate = DateTime{2025, 12, 31, 23, 59, 58};
    return c;
}

}  // namespace

TEST(plain_text_passthrough) {
    const auto t = Template::parse("hello.txt");
    CHECK_EQ(t.apply(makeCtx()), std::string("hello.txt"));
}

TEST(folder_name_ext) {
    const auto t = Template::parse("{folder}-{name}.{ext}");
    CHECK_EQ(t.apply(makeCtx()), std::string("Vacation-IMG 0042.jpg"));
}

TEST(number_default) {
    const auto t = Template::parse("{number}");
    auto c = makeCtx();
    CHECK_EQ(t.apply(c), std::string("1"));
    c.index = 9;
    CHECK_EQ(t.apply(c), std::string("10"));
}

TEST(number_sample_sets_start_and_width) {
    const auto t = Template::parse("{number = 0001}");
    auto c = makeCtx();
    CHECK_EQ(t.apply(c), std::string("0001"));
    c.index = 41;
    CHECK_EQ(t.apply(c), std::string("0042"));
    c.index = 10000;
    CHECK_EQ(t.apply(c), std::string("10001"));  // width grows past the sample
}

TEST(number_start_and_step) {
    const auto t = Template::parse("{number = 0010 AND step = 5}");
    auto c = makeCtx();
    c.index = 2;
    CHECK_EQ(t.apply(c), std::string("0020"));
}

TEST(number_negative_step) {
    const auto t = Template::parse("{number = 10 AND step = -3}");
    auto c = makeCtx();
    c.index = 4;
    CHECK_EQ(t.apply(c), std::string("-2"));
}

TEST(date_masks) {
    auto c = makeCtx();
    CHECK_EQ(Template::parse("{filedate = YYYY-MM-DD_hh-mm-ss}").apply(c),
             std::string("2026-05-12_14-30-07"));
    CHECK_EQ(Template::parse("{filedate}").apply(c), std::string("2026-05-12"));
    CHECK_EQ(Template::parse("{photodate = DD.MM.YY}").apply(c), std::string("31.12.25"));
}

TEST(quoted_date_mask_with_spaces) {
    const auto t = Template::parse("{filedate = \"YYYY MM\"}");
    CHECK_EQ(t.apply(makeCtx()), std::string("2026 05"));
}

TEST(or_fallback_chain) {
    const auto t =
        Template::parse("{photodate = YYYYMMDD OR filedate = YYYYMMDD OR \"nodate\"}");
    auto c = makeCtx();
    CHECK_EQ(t.apply(c), std::string("20251231"));
    c.photoDate.reset();
    CHECK_EQ(t.apply(c), std::string("20260512"));
    c.fileDate.reset();
    CHECK_EQ(t.apply(c), std::string("nodate"));
}

TEST(modifier_uppercase_lowercase) {
    auto c = makeCtx();
    c.stem = "Img scan";
    CHECK_EQ(Template::parse("{name AND uppercase}").apply(c), std::string("IMG SCAN"));
    CHECK_EQ(Template::parse("{name AND lowercase}").apply(c), std::string("img scan"));
}

TEST(modifier_cyrillic_case) {
    auto c = makeCtx();
    c.stem = "Фото Отпуск ё";
    CHECK_EQ(Template::parse("{name AND uppercase}").apply(c),
             std::string("ФОТО ОТПУСК Ё"));
    CHECK_EQ(Template::parse("{name AND lowercase}").apply(c),
             std::string("фото отпуск ё"));
}

TEST(modifier_nospaces_and_chains) {
    const auto c = makeCtx();  // stem = "IMG 0042"
    CHECK_EQ(Template::parse("{name AND nospaces}").apply(c), std::string("IMG0042"));
    CHECK_EQ(Template::parse("{name AND lowercase AND nospaces}").apply(c),
             std::string("img0042"));
}

TEST(modifier_first_counts_codepoints) {
    auto c = makeCtx();
    CHECK_EQ(Template::parse("{name AND first 3}").apply(c), std::string("IMG"));
    c.stem = "Фотоальбом";
    CHECK_EQ(Template::parse("{name AND first 4}").apply(c), std::string("Фото"));
}

TEST(keywords_case_insensitive_and_glued_equals) {
    const auto t = Template::parse("{NUMBER=01 and STEP=2 oR \"x\"}");
    auto c = makeCtx();
    c.index = 1;
    CHECK_EQ(t.apply(c), std::string("03"));
}

TEST(brace_escapes) {
    const auto t = Template::parse("a{{b}}c");
    CHECK_EQ(t.apply(makeCtx()), std::string("a{b}c"));
}

TEST(static_text_keeps_unicode) {
    const auto t = Template::parse("Фото_{number = 01}.{ext}");
    CHECK_EQ(t.apply(makeCtx()), std::string("Фото_01.jpg"));
}

TEST(full_example) {
    const auto t = Template::parse("{folder}_{number = 0001}.{ext}");
    auto c = makeCtx();
    CHECK_EQ(t.apply(c), std::string("Vacation_0001.jpg"));
    c.index = 1;
    CHECK_EQ(t.apply(c), std::string("Vacation_0002.jpg"));
}

TEST(eval_error_when_no_date_and_no_fallback) {
    const auto t = Template::parse("{photodate}");
    auto c = makeCtx();
    c.photoDate.reset();
    CHECK_THROWS_AS(t.apply(c), EvalError);
}

TEST(parse_error_positions_are_reported) {
    try {
        Template::parse("abc{nme}");
        CHECK(false);
    } catch (const ParseError& e) {
        CHECK_EQ(static_cast<long>(e.pos), 4L);  // points at 'nme'
    }
}

TEST(parse_errors) {
    CHECK_THROWS_AS(Template::parse("{nme}"), ParseError);            // unknown source
    CHECK_THROWS_AS(Template::parse("{name"), ParseError);            // missing '}'
    CHECK_THROWS_AS(Template::parse("name}"), ParseError);            // stray '}'
    CHECK_THROWS_AS(Template::parse("{}"), ParseError);               // empty token
    CHECK_THROWS_AS(Template::parse("{number = 12a}"), ParseError);   // bad sample
    CHECK_THROWS_AS(Template::parse("{name AND step = 2}"), ParseError);
    CHECK_THROWS_AS(Template::parse("{name AND first}"), ParseError); // missing count
    CHECK_THROWS_AS(Template::parse("{name AND first 0}"), ParseError);
    CHECK_THROWS_AS(Template::parse("{name AND blink}"), ParseError); // unknown modifier
    CHECK_THROWS_AS(Template::parse("{name OR}"), ParseError);        // dangling OR
    CHECK_THROWS_AS(Template::parse("{OR name}"), ParseError);        // leading OR
    CHECK_THROWS_AS(Template::parse("{name = x}"), ParseError);       // format on name
    CHECK_THROWS_AS(Template::parse("{filedate = qq}"), ParseError);  // mask w/o placeholders
    CHECK_THROWS_AS(Template::parse("{number =}"), ParseError);       // missing value
    CHECK_THROWS_AS(Template::parse("{\"abc}"), ParseError);          // unterminated quote
    CHECK_THROWS_AS(Template::parse("{\"x\" AND uppercase}"), ParseError);
}
