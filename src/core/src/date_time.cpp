#include "date_time.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace oxq::core::detail {
namespace {

[[nodiscard]] bool ascii_digits(std::string_view text, std::size_t offset,
                                std::size_t length) noexcept {
  if (offset > text.size() || length > text.size() - offset) {
    return false;
  }
  for (std::size_t index = offset; index < offset + length; ++index) {
    if (text[index] < '0' || text[index] > '9') {
      return false;
    }
  }
  return true;
}

[[nodiscard]] unsigned decimal(std::string_view text, std::size_t offset,
                               std::size_t length) noexcept {
  unsigned value = 0;
  for (std::size_t index = offset; index < offset + length; ++index) {
    value = value * 10U + static_cast<unsigned>(text[index] - '0');
  }
  return value;
}

[[nodiscard]] bool leap_year(unsigned year) noexcept {
  return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

[[nodiscard]] bool valid_calendar_date(std::string_view text) noexcept {
  if (text.size() < 10 || !ascii_digits(text, 0, 4) || text[4] != '-' ||
      !ascii_digits(text, 5, 2) || text[7] != '-' || !ascii_digits(text, 8, 2)) {
    return false;
  }
  const unsigned year = decimal(text, 0, 4);
  const unsigned month = decimal(text, 5, 2);
  const unsigned day = decimal(text, 8, 2);
  if (month == 0 || month > 12 || day == 0) {
    return false;
  }
  constexpr std::array<unsigned, 12> days{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const unsigned limit = month == 2 && leap_year(year) ? 29U : days[month - 1];
  return day <= limit;
}

[[nodiscard]] bool valid_zone(std::string_view zone) noexcept {
  if (zone.empty() || zone == "Z") {
    return true;
  }
  if (zone.size() != 6 || (zone[0] != '+' && zone[0] != '-') || zone[3] != ':' ||
      !ascii_digits(zone, 1, 2) || !ascii_digits(zone, 4, 2)) {
    return false;
  }
  return decimal(zone, 1, 2) <= 23U && decimal(zone, 4, 2) <= 59U;
}

}  // namespace

std::optional<DatePrecision> date_time_precision(std::string_view text) noexcept {
  if (text.size() == 4 && ascii_digits(text, 0, 4)) {
    return DatePrecision::year;
  }
  if (text.size() == 7 && ascii_digits(text, 0, 4) && text[4] == '-' &&
      ascii_digits(text, 5, 2) && decimal(text, 5, 2) >= 1U &&
      decimal(text, 5, 2) <= 12U) {
    return DatePrecision::month;
  }
  if (!valid_calendar_date(text)) {
    return std::nullopt;
  }
  if (text.size() == 10) {
    return DatePrecision::day;
  }
  if (text.size() < 16 || text[10] != 'T' || !ascii_digits(text, 11, 2) || text[13] != ':' ||
      !ascii_digits(text, 14, 2) || decimal(text, 11, 2) > 23U ||
      decimal(text, 14, 2) > 59U) {
    return std::nullopt;
  }

  std::size_t cursor = 16;
  DatePrecision precision = DatePrecision::minute;
  if (cursor < text.size() && text[cursor] == ':') {
    if (!ascii_digits(text, cursor + 1, 2) || decimal(text, cursor + 1, 2) > 59U) {
      return std::nullopt;
    }
    cursor += 3;
    precision = DatePrecision::second;
    if (cursor < text.size() && text[cursor] == '.') {
      const std::size_t fraction_start = ++cursor;
      while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
        ++cursor;
      }
      if (cursor == fraction_start) {
        return std::nullopt;
      }
      precision = DatePrecision::subsecond;
    }
  }
  return valid_zone(text.substr(cursor)) ? std::optional<DatePrecision>{precision} : std::nullopt;
}

}  // namespace oxq::core::detail
