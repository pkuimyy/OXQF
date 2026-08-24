#include <oxq/core/product.hpp>
#include <oxq/core/reader.hpp>
#include <oxq/core/state_validation.hpp>
#include <oxq/core/validator.hpp>
#include <oxq/core/writer.hpp>

#include <string_view>
#include <variant>
#include <vector>

int main() {
  const oxq::core::ReaderLimits limits;
  const oxq::core::ValidatorOutcome validator{oxq::core::ReaderDiagnostics{}};
  const oxq::core::WriterLimits writer_limits;
  const std::vector<oxq::core::ValidationIssue> state_issues;
  if (limits.max_nodes == 0 || writer_limits.max_file_size == 0 ||
      !state_issues.empty() ||
      !std::holds_alternative<oxq::core::ReaderDiagnostics>(validator)) {
    return 1;
  }
  if (oxq::core::product_name() != std::string_view{"oxq-core"}) {
    return 1;
  }
  return oxq::core::product_version().empty() ? 1 : 0;
}
