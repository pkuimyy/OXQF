#include <oxq/core/product.hpp>
#include <oxq/convert/product.hpp>

#include <iostream>
#include <string_view>

namespace {

void print_usage(std::ostream& output) {
  output << "Usage: oxq [--help | --version]\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc == 2) {
    const std::string_view argument{argv[1]};
    if (argument == "--version") {
      std::cout << "oxq " << oxq::core::product_version() << '\n';
      return 0;
    }
    if (argument == "--help") {
      print_usage(std::cout);
      return 0;
    }
  }

  if (argc == 1) {
    print_usage(std::cout);
    return 0;
  }

  std::cerr << "oxq: unsupported arguments\n";
  print_usage(std::cerr);
  return 2;
}
