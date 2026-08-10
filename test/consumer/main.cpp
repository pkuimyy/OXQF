#include <oxq/core/game_model.hpp>
#include <oxq/core/product.hpp>
#include <oxq/core/validation.hpp>
#include <oxq/convert/product.hpp>

int main() {
  oxq::core::GameModel game;
  game.uuid = *oxq::core::Uuid::parse("01980000-0000-7000-8000-000000000020");
  return oxq::core::product_version() == oxq::convert::product_version() &&
                 !oxq::core::has_errors(oxq::core::validate(game))
             ? 0
             : 1;
}
