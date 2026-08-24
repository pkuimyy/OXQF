#include "unicode_nfc.hpp"

#include "generated/unicode_nfc_data.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oxq::core::detail {
namespace {

constexpr std::uint32_t kSBase = 0xac00;
constexpr std::uint32_t kLBase = 0x1100;
constexpr std::uint32_t kVBase = 0x1161;
constexpr std::uint32_t kTBase = 0x11a7;
constexpr std::uint32_t kLCount = 19;
constexpr std::uint32_t kVCount = 21;
constexpr std::uint32_t kTCount = 28;
constexpr std::uint32_t kNCount = kVCount * kTCount;
constexpr std::uint32_t kSCount = kLCount * kNCount;

[[nodiscard]] std::uint8_t combining_class(std::uint32_t code_point) noexcept {
  const auto found = std::ranges::lower_bound(
      unicode_data::combining_classes, code_point, {},
      &unicode_data::CombiningClassEntry::code_point);
  return found != unicode_data::combining_classes.end() && found->code_point == code_point
             ? found->value
             : 0;
}

void decompose(std::uint32_t code_point, std::vector<std::uint32_t>& output) {
  if (code_point >= kSBase && code_point < kSBase + kSCount) {
    const auto syllable = code_point - kSBase;
    output.push_back(kLBase + syllable / kNCount);
    output.push_back(kVBase + (syllable % kNCount) / kTCount);
    const auto trailing = syllable % kTCount;
    if (trailing != 0) {
      output.push_back(kTBase + trailing);
    }
    return;
  }
  const auto found = std::ranges::lower_bound(
      unicode_data::decomposition_entries, code_point, {},
      &unicode_data::DecompositionEntry::code_point);
  if (found == unicode_data::decomposition_entries.end() || found->code_point != code_point) {
    output.push_back(code_point);
    return;
  }
  for (std::size_t index = 0; index < found->length; ++index) {
    decompose(unicode_data::decomposition_data[found->offset + index], output);
  }
}

[[nodiscard]] std::optional<std::uint32_t> hangul_composition(
    std::uint32_t first, std::uint32_t second) noexcept {
  if (first >= kLBase && first < kLBase + kLCount &&
      second >= kVBase && second < kVBase + kVCount) {
    return kSBase + ((first - kLBase) * kVCount + (second - kVBase)) * kTCount;
  }
  if (first >= kSBase && first < kSBase + kSCount &&
      (first - kSBase) % kTCount == 0 && second > kTBase &&
      second < kTBase + kTCount) {
    return first + second - kTBase;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> compose(std::uint32_t first,
                                                   std::uint32_t second) noexcept {
  if (const auto hangul = hangul_composition(first, second); hangul.has_value()) {
    return hangul;
  }
  const auto found = std::ranges::lower_bound(
      unicode_data::composition_entries, std::pair{first, second}, {},
      [](const unicode_data::CompositionEntry& entry) {
        return std::pair{entry.first, entry.second};
      });
  if (found != unicode_data::composition_entries.end() && found->first == first &&
      found->second == second) {
    return found->composite;
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<std::uint32_t> decode(std::string_view text) {
  std::vector<std::uint32_t> result;
  result.reserve(text.size());
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const auto first = static_cast<std::uint8_t>(text[cursor++]);
    if (first <= 0x7fU) {
      result.push_back(first);
    } else if (first <= 0xdfU) {
      const auto second = static_cast<std::uint8_t>(text[cursor++]);
      result.push_back(((first & 0x1fU) << 6U) | (second & 0x3fU));
    } else if (first <= 0xefU) {
      const auto second = static_cast<std::uint8_t>(text[cursor++]);
      const auto third = static_cast<std::uint8_t>(text[cursor++]);
      result.push_back(((first & 0x0fU) << 12U) | ((second & 0x3fU) << 6U) |
                       (third & 0x3fU));
    } else {
      const auto second = static_cast<std::uint8_t>(text[cursor++]);
      const auto third = static_cast<std::uint8_t>(text[cursor++]);
      const auto fourth = static_cast<std::uint8_t>(text[cursor++]);
      result.push_back(((first & 0x07U) << 18U) | ((second & 0x3fU) << 12U) |
                       ((third & 0x3fU) << 6U) | (fourth & 0x3fU));
    }
  }
  return result;
}

void append_utf8(std::string& output, std::uint32_t code_point) {
  if (code_point <= 0x7fU) {
    output.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7ffU) {
    output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  } else if (code_point <= 0xffffU) {
    output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  } else {
    output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  }
}

}  // namespace

std::string normalize_nfc(std::string_view text) {
  std::vector<std::uint32_t> decomposed;
  for (const auto code_point : decode(text)) {
    decompose(code_point, decomposed);
  }
  for (std::size_t index = 1; index < decomposed.size(); ++index) {
    const auto current_class = combining_class(decomposed[index]);
    if (current_class == 0) {
      continue;
    }
    std::size_t position = index;
    while (position > 0 && combining_class(decomposed[position - 1]) > current_class) {
      std::swap(decomposed[position - 1], decomposed[position]);
      --position;
    }
  }

  std::vector<std::uint32_t> composed;
  composed.reserve(decomposed.size());
  std::size_t starter_position = 0;
  std::uint32_t starter = 0;
  std::uint8_t previous_class = 0;
  for (const auto code_point : decomposed) {
    const auto current_class = combining_class(code_point);
    const auto composite = composed.empty() ? std::nullopt : compose(starter, code_point);
    if (composite.has_value() && (previous_class == 0 || previous_class < current_class)) {
      composed[starter_position] = *composite;
      starter = *composite;
      continue;
    }
    if (current_class == 0) {
      starter_position = composed.size();
      starter = code_point;
    }
    composed.push_back(code_point);
    previous_class = current_class;
  }

  std::string result;
  result.reserve(text.size());
  for (const auto code_point : composed) {
    append_utf8(result, code_point);
  }
  return result;
}

bool is_nfc(std::string_view text) {
  return normalize_nfc(text) == text;
}

}  // namespace oxq::core::detail
