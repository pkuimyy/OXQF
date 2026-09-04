#include "cli.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
  return oxq::cli::run(argc, argv, std::cout, std::cerr);
}
