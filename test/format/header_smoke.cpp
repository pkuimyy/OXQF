#include <oxq/format/codec_error.hpp>
#include <oxq/format/document.hpp>
#include <oxq/format/product.hpp>
#include <oxq/format/reader.hpp>
#include <oxq/format/state_validation.hpp>
#include <oxq/format/validation.hpp>
#include <oxq/format/validator.hpp>
#include <oxq/format/writer.hpp>

#include <string_view>
#include <variant>
#include <vector>

int main() {
  oxq::format::GameDocument document;
  const oxq::format::ReaderLimits reader_limits;
  const oxq::format::WriterLimits writer_limits;
  const oxq::format::ValidatorOutcome validator{oxq::format::ReaderDiagnostics{}};
  const std::vector<oxq::format::ValidationIssue> state_issues;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000021");

  if (reader_limits.max_nodes == 0 || writer_limits.max_file_size == 0 ||
      !state_issues.empty() ||
      !std::holds_alternative<oxq::format::ReaderDiagnostics>(validator)) {
    return 1;
  }
  if (oxq::format::product_name() != std::string_view{"oxq-format"}) {
    return 1;
  }
  return oxq::format::has_errors(oxq::format::validate(document)) ? 1 : 0;
}
