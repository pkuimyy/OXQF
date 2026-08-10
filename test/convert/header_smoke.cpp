#include <oxq/core/product.hpp>
#include <oxq/convert/product.hpp>

#include <string_view>

int main() {
  if (oxq::convert::product_name() != std::string_view{"oxq-convert"}) {
    return 1;
  }
  return oxq::convert::product_version() == oxq::core::product_version() ? 0 : 1;
}
