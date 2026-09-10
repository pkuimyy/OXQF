#include <oxq/core/product.hpp>

namespace oxq::core {

std::string_view product_name() noexcept {
  return "oxq-format";
}

std::string_view product_version() noexcept {
  return OXQF_PROJECT_VERSION;
}

}  // namespace oxq::core
