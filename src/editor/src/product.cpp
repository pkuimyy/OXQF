#include <oxq/editor/product.hpp>

namespace oxq::editor {

std::string_view product_name() noexcept {
  return "oxq-editor";
}

std::string_view product_version() noexcept {
  return OXQF_PROJECT_VERSION;
}

}  // namespace oxq::editor
