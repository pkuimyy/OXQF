#pragma once

#include <oxq/core/codec_error.hpp>
#include <oxq/core/game_model.hpp>

#include <cstddef>
#include <span>
#include <variant>

namespace oxq::core {

struct ReaderLimits {
  std::size_t max_file_size{1024U * 1024U * 1024U};
  std::size_t max_sections{1024};
  std::size_t max_strings{10'000'000};
  std::size_t max_string_bytes{16U * 1024U * 1024U};
  std::size_t max_total_string_bytes{512U * 1024U * 1024U};
  std::size_t max_metadata_fields{65'536};
  std::size_t max_extended_metadata_bytes{1024U * 1024U};
  std::size_t max_nodes{10'000'000};
  std::size_t max_annotations{10'000'000};
  std::size_t max_tree_depth{1'000'000};
};

struct ReaderDiagnostics {
  bool canonical_section_order{true};
  bool canonical_string_pool_order{true};
  bool canonical_metadata_order{true};
  bool canonical_extended_metadata{true};
  bool canonical_piece_order{true};
  bool canonical_node_order{true};
  std::size_t skipped_unknown_sections{};
  std::size_t skipped_unknown_metadata_fields{};
  std::size_t skipped_unknown_metadata_value_types{};

  [[nodiscard]] bool canonical_ordering() const noexcept;
};

struct ReaderResult {
  GameModel game;
  ReaderDiagnostics diagnostics;
};

using ReaderOutcome = std::variant<ReaderResult, CodecError>;

[[nodiscard]] ReaderOutcome read_oxq(
    std::span<const std::byte> input,
    const ReaderLimits& limits = {});

}  // namespace oxq::core
