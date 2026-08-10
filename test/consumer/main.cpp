#include <oxq/core/product.hpp>
#include <oxq/convert/product.hpp>

int main() {
  return oxq::core::product_version() == oxq::convert::product_version() ? 0 : 1;
}
