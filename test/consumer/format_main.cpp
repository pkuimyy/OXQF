#include <oxq/convert/product.hpp>
#include <oxq/format/document.hpp>
#include <oxq/format/product.hpp>
#include <oxq/format/reader.hpp>
#include <oxq/format/state_validation.hpp>
#include <oxq/format/validation.hpp>
#include <oxq/format/validator.hpp>
#include <oxq/format/writer.hpp>

#include <variant>

int main() {
  oxq::format::GameDocument document;
  document.uuid =
      *oxq::format::Uuid::parse("01980000-0000-7000-8000-000000000022");
  const oxq::format::ValidatorOutcome validator{oxq::format::ReaderDiagnostics{}};

  return std::holds_alternative<oxq::format::ReaderDiagnostics>(validator) &&
                 !oxq::format::has_errors(oxq::format::validate(document)) &&
                 oxq::format::product_version() == oxq::convert::product_version()
             ? 0
             : 1;
}
