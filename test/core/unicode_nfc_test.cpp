#include "unicode_nfc.hpp"

#include <string>

int main() {
  using oxq::core::detail::is_nfc;
  using oxq::core::detail::normalize_nfc;

  if (normalize_nfc("e\u0301") != "é" || !is_nfc("é") || is_nfc("e\u0301")) {
    return 1;
  }
  if (normalize_nfc("a\u0315\u0300") != "à\u0315") {
    return 2;
  }
  if (normalize_nfc("\u1100\u1161") != "가" ||
      normalize_nfc("\u1100\u1161\u11a8") != "각") {
    return 3;
  }
  const std::string musical = "\U0001d15e";
  const std::string musical_nfc = "\U0001d157\U0001d165";
  if (normalize_nfc(musical) != musical_nfc ||
      normalize_nfc(normalize_nfc(musical)) != musical_nfc) {
    return 4;
  }
  if (normalize_nfc("中文/ASCII") != "中文/ASCII") {
    return 5;
  }
  return 0;
}
