#pragma once

#include <iosfwd>

namespace oxq::cli {

enum class ExitCode {
  success = 0,
  usage = 2,
  input_error = 3,
  output_error = 4,
  validation_error = 5,
  conversion_rejected = 6,
  partial_failure = 7,
};

[[nodiscard]] int run(int argc, char* argv[], std::ostream& output,
                      std::ostream& error);

}  // namespace oxq::cli
