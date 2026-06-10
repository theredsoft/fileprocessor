#include "infra/exifreader.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "minitest.h"

namespace fs = std::filesystem;
using fp::infra::parseJpegBuffer;
using fp::infra::readPhotoDate;

namespace {

// Builds a minimal valid JPEG with one APP1/EXIF segment.
struct JpegBuilder {
    std::vector<unsigned char> tiff;
    bool bigEndian;

    explicit JpegBuilder(bool be) : bigEndian(be) {}

    void u16(uint16_t v) {
        if (bigEndian) {
            tiff.push_back(static_cast<unsigned char>(v >> 8));
            tiff.push_back(static_cast<unsigned char>(v));
        } else {
            tiff.push_back(static_cast<unsigned char>(v));
            tiff.push_back(static_cast<unsigned char>(v >> 8));
        }
    }
    void u32(uint32_t v) {
        if (bigEndian) {
            u16(static_cast<uint16_t>(v >> 16));
            u16(static_cast<uint16_t>(v));
        } else {
            u16(static_cast<uint16_t>(v));
            u16(static_cast<uint16_t>(v >> 16));
        }
    }
    void ascii(const char* s, size_t n) {
        tiff.insert(tiff.end(), s, s + n);
    }

    std::vector<unsigned char> wrap() const {
        std::vector<unsigned char> jpeg = {0xFF, 0xD8, 0xFF, 0xE1};
        const size_t segLen = 2 + 6 + tiff.size();
        jpeg.push_back(static_cast<unsigned char>(segLen >> 8));
        jpeg.push_back(static_cast<unsigned char>(segLen));
        const char exif[] = {'E', 'x', 'i', 'f', 0, 0};
        jpeg.insert(jpeg.end(), exif, exif + 6);
        jpeg.insert(jpeg.end(), tiff.begin(), tiff.end());
        jpeg.push_back(0xFF);
        jpeg.push_back(0xD9);
        return jpeg;
    }
};

// IFD0 → ExifIFD → DateTimeOriginal "2024:01:15 10:20:30".
std::vector<unsigned char> jpegWithDateTimeOriginal(bool bigEndian) {
    JpegBuilder b(bigEndian);
    b.ascii(bigEndian ? "MM" : "II", 2);
    b.u16(42);
    b.u32(8);          // IFD0 offset
    b.u16(1);          // IFD0: one entry
    b.u16(0x8769);     //   ExifIFD pointer
    b.u16(4);          //   LONG
    b.u32(1);
    b.u32(26);         //   ExifIFD offset
    b.u32(0);          // next IFD
    b.u16(1);          // ExifIFD: one entry (offset 26)
    b.u16(0x9003);     //   DateTimeOriginal
    b.u16(2);          //   ASCII
    b.u32(20);
    b.u32(44);         //   value offset
    b.u32(0);          // next IFD
    b.ascii("2024:01:15 10:20:30", 20);  // offset 44, includes '\0'
    return b.wrap();
}

// Only IFD0 with the plain DateTime tag (no ExifIFD).
std::vector<unsigned char> jpegWithIfd0DateOnly() {
    JpegBuilder b(false);
    b.ascii("II", 2);
    b.u16(42);
    b.u32(8);
    b.u16(1);
    b.u16(0x0132);  // DateTime
    b.u16(2);
    b.u32(20);
    b.u32(26);
    b.u32(0);
    b.ascii("2023:07:01 08:09:10", 20);
    return b.wrap();
}

void writeFile(const fs::path& p, const std::vector<unsigned char>& bytes) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST(exif_little_endian_datetime_original) {
    const auto jpeg = jpegWithDateTimeOriginal(false);
    const auto date = parseJpegBuffer(jpeg.data(), jpeg.size());
    CHECK(date.has_value());
    CHECK_EQ(date->year, 2024);
    CHECK_EQ(date->month, 1);
    CHECK_EQ(date->day, 15);
    CHECK_EQ(date->hour, 10);
    CHECK_EQ(date->minute, 20);
    CHECK_EQ(date->second, 30);
}

TEST(exif_big_endian_datetime_original) {
    const auto jpeg = jpegWithDateTimeOriginal(true);
    const auto date = parseJpegBuffer(jpeg.data(), jpeg.size());
    CHECK(date.has_value());
    CHECK_EQ(date->year, 2024);
    CHECK_EQ(date->second, 30);
}

TEST(exif_falls_back_to_ifd0_datetime) {
    const auto jpeg = jpegWithIfd0DateOnly();
    const auto date = parseJpegBuffer(jpeg.data(), jpeg.size());
    CHECK(date.has_value());
    CHECK_EQ(date->year, 2023);
    CHECK_EQ(date->month, 7);
}

TEST(exif_absent_or_garbage_returns_nothing) {
    const std::vector<unsigned char> plain = {0xFF, 0xD8, 0xFF, 0xD9};
    CHECK(!parseJpegBuffer(plain.data(), plain.size()).has_value());
    const std::vector<unsigned char> notJpeg = {'h', 'i', '!', '!'};
    CHECK(!parseJpegBuffer(notJpeg.data(), notJpeg.size()).has_value());
    const auto good = jpegWithDateTimeOriginal(false);
    for (size_t cut : {size_t(3), size_t(10), good.size() / 2})
        CHECK(!parseJpegBuffer(good.data(), cut).has_value());
}

TEST(exif_read_from_file_with_extension_gate) {
    const fs::path dir = fs::temp_directory_path() / "fp_exif_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto jpeg = jpegWithDateTimeOriginal(false);
    writeFile(dir / "photo.jpg", jpeg);
    writeFile(dir / "photo.txt", jpeg);  // same bytes, wrong extension

    const auto fromJpg = readPhotoDate((dir / "photo.jpg").string());
    CHECK(fromJpg.has_value());
    CHECK_EQ(fromJpg->year, 2024);
    CHECK(!readPhotoDate((dir / "photo.txt").string()).has_value());
    CHECK(!readPhotoDate((dir / "missing.jpg").string()).has_value());
    fs::remove_all(dir);
}

TEST(template_uses_reports_photodate) {
    using fp::tmpl::Source;
    using fp::tmpl::Template;
    CHECK(Template::parse("{photodate OR filedate}").uses(Source::PhotoDate));
    CHECK(!Template::parse("{name}_{number}.{ext}").uses(Source::PhotoDate));
}
