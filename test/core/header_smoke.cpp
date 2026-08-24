#include <oxq/core/product.hpp>
#include <oxq/core/reader.hpp>
#include <oxq/core/validator.hpp>
#include <oxq/core/writer.hpp>

#include <string_view>
#include <variant>

int main() {
  const oxq::core::ReaderLimits limits;
  const oxq::core::ValidatorOutcome validator{oxq::core::ReaderDiagnostics{}};
  const oxq::core::WriterLimits writer_limits;
  if (limits.max_nodes == 0 || writer_limits.max_file_size == 0 ||
      !std::holds_alternative<oxq::core::ReaderDiagnostics>(validator)) {
    return 1;
  }
  if (oxq::core::product_name() != std::string_view{"oxq-core"}) {
    return 1;
  }
  return oxq::core::product_version().empty() ? 1 : 0;
}
