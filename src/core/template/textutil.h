#pragma once

#include <string>
#include <string_view>

namespace fp::text {

// Lenient UTF-8 decode: invalid bytes become U+FFFD.
std::u32string utf8Decode(std::string_view s);
std::string utf8Encode(const std::u32string& s);

// Case mapping covers ASCII, Latin-1 letters and Cyrillic (а-я, ё).
std::string toUpper(std::string_view s);
std::string toLower(std::string_view s);

std::string removeSpaces(std::string_view s);
std::string firstCodepoints(std::string_view s, long n);

}  // namespace fp::text
