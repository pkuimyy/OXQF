#include "names.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace oxq::cli {
namespace {

template <typename Enum, std::size_t Size>
[[nodiscard]] std::string_view enum_name(
    Enum value, const std::array<std::string_view, Size>& names) noexcept {
  const auto index = static_cast<std::size_t>(value);
  return index < names.size() ? names[index] : "unknown";
}

}  // namespace

std::string_view name(core::CodecErrorCode code) noexcept {
  static constexpr std::array names{
      std::string_view{"invalid_magic"},
      std::string_view{"unsupported_version"},
      std::string_view{"invalid_header"},
      std::string_view{"size_mismatch"},
      std::string_view{"integer_overflow"},
      std::string_view{"crc_mismatch"},
      std::string_view{"invalid_section_table"},
      std::string_view{"section_out_of_range"},
      std::string_view{"section_overlap"},
      std::string_view{"unknown_critical_section"},
      std::string_view{"invalid_utf8"},
      std::string_view{"invalid_string_ref"},
      std::string_view{"invalid_metadata"},
      std::string_view{"invalid_position"},
      std::string_view{"invalid_move"},
      std::string_view{"invalid_tree"},
      std::string_view{"invalid_annotation"},
      std::string_view{"resource_limit"},
  };
  return enum_name(code, names);
}

std::string_view name(core::ValidationCode code) noexcept {
  static constexpr std::array names{
      std::string_view{"nil_uuid"},
      std::string_view{"invalid_uuid_variant"},
      std::string_view{"invalid_side"},
      std::string_view{"invalid_fullmove_number"},
      std::string_view{"too_many_pieces"},
      std::string_view{"invalid_piece_type"},
      std::string_view{"invalid_square"},
      std::string_view{"duplicate_square"},
      std::string_view{"empty_tree"},
      std::string_view{"too_many_nodes"},
      std::string_view{"invalid_root"},
      std::string_view{"invalid_parent"},
      std::string_view{"invalid_child"},
      std::string_view{"duplicate_child"},
      std::string_view{"unreachable_node"},
      std::string_view{"tree_cycle"},
      std::string_view{"tree_too_deep"},
      std::string_view{"invalid_move"},
      std::string_view{"same_square_move"},
      std::string_view{"missing_source_piece"},
      std::string_view{"wrong_side_to_move"},
      std::string_view{"destination_occupied_by_same_side"},
      std::string_view{"invalid_annotation"},
      std::string_view{"invalid_annotation_kind"},
      std::string_view{"too_many_annotations"},
      std::string_view{"string_too_long"},
      std::string_view{"invalid_utf8"},
      std::string_view{"invalid_game_result"},
      std::string_view{"invalid_date_precision"},
      std::string_view{"missing_date_precision"},
      std::string_view{"invalid_date_time"},
      std::string_view{"duplicate_tag"},
      std::string_view{"invalid_extension_namespace"},
      std::string_view{"invalid_extension_key"},
      std::string_view{"empty_extension_namespace"},
      std::string_view{"empty_extension_array"},
  };
  return enum_name(code, names);
}

std::string_view name(convert::CblErrorCode code) noexcept {
  static constexpr std::array names{
      std::string_view{"invalid_magic"},
      std::string_view{"unsupported_version"},
      std::string_view{"truncated_input"},
      std::string_view{"integer_overflow"},
      std::string_view{"resource_limit"},
      std::string_view{"invalid_utf16"},
      std::string_view{"invalid_directory"},
      std::string_view{"resource_out_of_range"},
      std::string_view{"invalid_record"},
      std::string_view{"invalid_comment"},
      std::string_view{"invalid_move_tree"},
      std::string_view{"invalid_game_model"},
  };
  return enum_name(code, names);
}

std::string_view name(convert::CblWriteErrorCode code) noexcept {
  static constexpr std::array names{
      std::string_view{"too_many_games"},
      std::string_view{"integer_overflow"},
      std::string_view{"resource_limit"},
      std::string_view{"encoding_invariant"},
  };
  return enum_name(code, names);
}

std::string_view name(convert::ConversionCode code) noexcept {
  static constexpr std::array names{
      std::string_view{"cbl_uuid_mismatch"},
      std::string_view{"cbl_directory_uuid_invalid"},
      std::string_view{"cbl_record_uuid_invalid"},
      std::string_view{"cbl_uuid_derived"},
      std::string_view{"cbl_title_mismatch"},
      std::string_view{"cbl_duplicate_display_index"},
      std::string_view{"cbl_invalid_rating"},
      std::string_view{"cbl_invalid_date"},
      std::string_view{"cbl_fullmove_normalized"},
      std::string_view{"cbl_multiple_result"},
      std::string_view{"cbl_unknown_result"},
      std::string_view{"cbl_unknown_record_type"},
      std::string_view{"cbl_write_library_uuid_derived"},
      std::string_view{"cbl_write_invalid_game_model"},
      std::string_view{"cbl_write_text_too_long"},
      std::string_view{"cbl_write_metadata_unsupported"},
      std::string_view{"cbl_write_result_normalized"},
      std::string_view{"cbl_write_annotation_normalized"},
      std::string_view{"cbl_write_extension_unsupported"},
  };
  return enum_name(code, names);
}

}  // namespace oxq::cli
