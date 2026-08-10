#include <oxq/convert/product.hpp>

namespace oxq::convert {

std::string_view product_name() noexcept {
  return "oxq-convert";
}

std::string_view product_version() noexcept {
  return OXQF_PROJECT_VERSION;
}

}  // namespace oxq::convert
