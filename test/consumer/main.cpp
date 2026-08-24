#include <oxq/core/game_model.hpp>
#include <oxq/core/product.hpp>
#include <oxq/core/reader.hpp>
#include <oxq/core/validation.hpp>
#include <oxq/core/validator.hpp>
#include <oxq/convert/product.hpp>

#include <variant>

int main() {
  oxq::core::GameModel game;
  const oxq::core::ReaderLimits reader_limits;
  const oxq::core::ValidatorOutcome validator{oxq::core::ReaderDiagnostics{}};
  game.uuid = *oxq::core::Uuid::parse("01980000-0000-7000-8000-000000000020");
  return reader_limits.max_file_size > 0 &&
                 std::holds_alternative<oxq::core::ReaderDiagnostics>(validator) &&
                 oxq::core::product_version() == oxq::convert::product_version() &&
                 !oxq::core::has_errors(oxq::core::validate(game))
             ? 0
             : 1;
}
