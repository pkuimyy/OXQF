#include <oxq/core/product.hpp>
#include <oxq/convert/cbl_reader.hpp>
#include <oxq/convert/product.hpp>

#include <string_view>

int main() {
  const oxq::convert::CblReaderLimits limits;
  if (limits.max_directory_entries == 0 || limits.max_nodes == 0 ||
      limits.max_tree_depth == 0 || limits.max_comment_bytes == 0) {
    return 1;
  }
  if (oxq::convert::product_name() != std::string_view{"oxq-convert"}) {
    return 1;
  }
  return oxq::convert::product_version() == oxq::core::product_version() ? 0 : 1;
}
