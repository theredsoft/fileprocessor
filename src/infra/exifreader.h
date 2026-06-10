#pragma once

#include <optional>
#include <string>

#include "core/template/template.h"

namespace fp::infra {

// Extracts the capture date from a JPEG's EXIF block (DateTimeOriginal,
// falling back to DateTimeDigitized, then DateTime). Returns nullopt for
// non-JPEG files, files without EXIF and on any parse problem — the caller
// falls back to the file date via the template's OR chain.
//
// Self-contained parser (SOI → APP1 "Exif" → TIFF → IFD0/ExifIFD), both
// byte orders, bounds-checked; only the file head is read, not the image.
std::optional<fp::tmpl::DateTime> readPhotoDate(const std::string& filePathUtf8);

// Parses an EXIF buffer directly (the TIFF block with its APP1 "Exif\0\0"
// preamble already located inside a JPEG). Exposed for tests.
std::optional<fp::tmpl::DateTime> parseJpegBuffer(const unsigned char* data,
                                                  size_t size);

}  // namespace fp::infra
