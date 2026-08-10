#include <oxq/core/product.hpp>

#include <string_view>

int main() {
  if (oxq::core::product_name() != std::string_view{"oxq-core"}) {
    return 1;
  }
  return oxq::core::product_version().empty() ? 1 : 0;
}
