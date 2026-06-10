#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS  // std::sscanf is used portably and safely here
#endif

#include "infra/exifreader.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // windows.h min/max macros break std::min/std::max
#include <windows.h>

#include <filesystem>
#endif

#include "core/template/textutil.h"

namespace fp::infra {
namespace {

// EXIF sits in an APP1 segment near the start of the file; the image data
// itself is never needed.
constexpr size_t kHeadBytes = 256 * 1024;

constexpr uint16_t kTagExifIfdPointer = 0x8769;
constexpr uint16_t kTagDateTimeOriginal = 0x9003;
constexpr uint16_t kTagDateTimeDigitized = 0x9004;
constexpr uint16_t kTagDateTime = 0x0132;

struct Tiff {
    const unsigned char* data;
    size_t size;
    bool bigEndian;

    uint16_t u16(size_t off) const {
        return bigEndian ? static_cast<uint16_t>(data[off] << 8 | data[off + 1])
                         : static_cast<uint16_t>(data[off + 1] << 8 | data[off]);
    }
    uint32_t u32(size_t off) const {
        return bigEndian
                   ? (static_cast<uint32_t>(data[off]) << 24) |
                         (static_cast<uint32_t>(data[off + 1]) << 16) |
                         (static_cast<uint32_t>(data[off + 2]) << 8) |
                         data[off + 3]
                   : (static_cast<uint32_t>(data[off + 3]) << 24) |
                         (static_cast<uint32_t>(data[off + 2]) << 16) |
                         (static_cast<uint32_t>(data[off + 1]) << 8) |
                         data[off];
    }
    bool inBounds(size_t off, size_t len) const {
        return off <= size && len <= size - off;
    }
};

std::optional<fp::tmpl::DateTime> parseDateString(const char* s) {
    fp::tmpl::DateTime dt;
    // EXIF format: "YYYY:MM:DD HH:MM:SS"
    if (std::sscanf(s, "%4d:%2d:%2d %2d:%2d:%2d", &dt.year, &dt.month, &dt.day,
                    &dt.hour, &dt.minute, &dt.second) != 6)
        return std::nullopt;
    if (dt.year < 1800 || dt.month < 1 || dt.month > 12 || dt.day < 1 ||
        dt.day > 31)
        return std::nullopt;
    return dt;
}

// Looks up an ASCII date tag inside one IFD; also reports the ExifIFD offset
// if the IFD carries the pointer tag.
struct IfdScan {
    std::optional<fp::tmpl::DateTime> date;       // first matching date tag
    std::optional<uint32_t> exifIfdOffset;
};

IfdScan scanIfd(const Tiff& t, uint32_t ifdOffset,
                std::initializer_list<uint16_t> dateTags) {
    IfdScan result;
    if (!t.inBounds(ifdOffset, 2)) return result;
    const uint16_t count = t.u16(ifdOffset);
    if (!t.inBounds(ifdOffset + 2, static_cast<size_t>(count) * 12))
        return result;

    for (uint16_t i = 0; i < count; ++i) {
        const size_t e = ifdOffset + 2 + static_cast<size_t>(i) * 12;
        const uint16_t tag = t.u16(e);
        const uint16_t type = t.u16(e + 2);
        const uint32_t num = t.u32(e + 4);

        if (tag == kTagExifIfdPointer && type == 4 /*LONG*/ && num == 1) {
            result.exifIfdOffset = t.u32(e + 8);
            continue;
        }
        if (result.date) continue;
        for (const uint16_t wanted : dateTags) {
            if (tag != wanted || type != 2 /*ASCII*/ || num < 19) continue;
            // 19+ chars never fit inline, so the value field is an offset.
            const uint32_t valueOff = t.u32(e + 8);
            if (!t.inBounds(valueOff, num)) break;
            char buf[20];
            std::memcpy(buf, t.data + valueOff, 19);
            buf[19] = '\0';
            result.date = parseDateString(buf);
            break;
        }
    }
    return result;
}

std::optional<fp::tmpl::DateTime> parseTiff(const unsigned char* data,
                                            size_t size) {
    if (size < 8) return std::nullopt;
    bool bigEndian;
    if (data[0] == 'I' && data[1] == 'I')
        bigEndian = false;
    else if (data[0] == 'M' && data[1] == 'M')
        bigEndian = true;
    else
        return std::nullopt;

    const Tiff t{data, size, bigEndian};
    if (t.u16(2) != 42) return std::nullopt;

    const IfdScan ifd0 = scanIfd(t, t.u32(4), {kTagDateTime});
    if (ifd0.exifIfdOffset) {
        const IfdScan exifIfd = scanIfd(
            t, *ifd0.exifIfdOffset, {kTagDateTimeOriginal, kTagDateTimeDigitized});
        if (exifIfd.date) return exifIfd.date;
    }
    return ifd0.date;  // fallback: plain DateTime from IFD0
}

std::ifstream openBinary(const std::string& utf8) {
#ifdef _WIN32
    // UTF-8 names must not pass through the ANSI codepage.
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                        static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), len);
    return std::ifstream(std::filesystem::path(wide), std::ios::binary);
#else
    return std::ifstream(utf8, std::ios::binary);
#endif
}

bool hasJpegExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = fp::text::toLower(path.substr(dot + 1));
    return ext == "jpg" || ext == "jpeg";
}

}  // namespace

std::optional<fp::tmpl::DateTime> parseJpegBuffer(const unsigned char* data,
                                                  size_t size) {
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) return std::nullopt;

    size_t pos = 2;
    while (pos + 4 <= size) {
        if (data[pos] != 0xFF) return std::nullopt;
        const unsigned char marker = data[pos + 1];
        if (marker == 0xDA || marker == 0xD9) break;  // image data / EOI
        const size_t segLen =
            (static_cast<size_t>(data[pos + 2]) << 8) | data[pos + 3];
        if (segLen < 2 || pos + 2 + segLen > size) break;
        if (marker == 0xE1 && segLen >= 2 + 6 &&
            std::memcmp(data + pos + 4, "Exif\0\0", 6) == 0) {
            return parseTiff(data + pos + 10, segLen - 2 - 6);
        }
        pos += 2 + segLen;
    }
    return std::nullopt;
}

std::optional<fp::tmpl::DateTime> readPhotoDate(const std::string& filePathUtf8) {
    if (!hasJpegExtension(filePathUtf8)) return std::nullopt;

    std::ifstream in = openBinary(filePathUtf8);
    if (!in.is_open()) return std::nullopt;

    std::vector<unsigned char> head(kHeadBytes);
    in.read(reinterpret_cast<char*>(head.data()),
            static_cast<std::streamsize>(head.size()));
    const size_t got = static_cast<size_t>(in.gcount());
    return parseJpegBuffer(head.data(), got);
}

}  // namespace fp::infra
