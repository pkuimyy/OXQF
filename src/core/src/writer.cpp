#include <oxq/core/writer.hpp>

#include "codec/binary.hpp"
#include "codec/crc32c.hpp"
#include "codec/restricted_json.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace oxq::core {
namespace {

constexpr std::uint32_t kNoNode = 0xffffffffU;
constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kSectionEntrySize = 40;
constexpr std::array<std::uint8_t, 8> kMagic{0x89, 0x4f, 0x58, 0x51,
                                             0x0d, 0x0a, 0x1a, 0x0a};

struct UnsignedStringLess {
  [[nodiscard]] bool operator()(std::string_view left, std::string_view right) const noexcept {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(), [](char lhs, char rhs) {
          return static_cast<unsigned char>(lhs) < static_cast<unsigned char>(rhs);
        });
  }
};

class Bytes {
 public:
  void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value & 0xffU));
    u8(static_cast<std::uint8_t>(value >> 8U));
  }

  void u32(std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
  }

  void u64(std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
      u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
  }

  void append(std::span<const std::byte> input) {
    bytes_.insert(bytes_.end(), input.begin(), input.end());
  }

  void append(std::string_view input) {
    append(std::as_bytes(std::span{input.data(), input.size()}));
  }

  void pad(std::size_t alignment) {
    bytes_.resize(bytes_.size() + (alignment - bytes_.size() % alignment) % alignment,
                  std::byte{0});
  }

  [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
  [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept { return bytes_; }
  [[nodiscard]] std::vector<std::byte> take() && { return std::move(bytes_); }

 private:
  std::vector<std::byte> bytes_;
};

struct MetadataField {
  std::uint16_t tag{};
  std::uint8_t value_type{};
  std::uint8_t flags{};
  std::variant<std::uint32_t, std::string> value;
};

struct PlannedTree {
  std::vector<std::size_t> order;
  std::vector<std::size_t> new_index;
  std::vector<std::uint32_t> ply;
  std::vector<std::uint32_t> next_sibling;
  std::vector<std::uint32_t> first_annotation;
};

struct Section {
  std::uint32_t type{};
  std::vector<std::byte> payload;
  std::size_t offset{};
  std::uint32_t crc{};
};

[[nodiscard]] WriterError failure(WriterErrorCode code, std::string message,
                                  std::optional<std::uint64_t> expected = {},
                                  std::optional<std::uint64_t> actual = {}) {
  return {code, std::move(message), {}, expected, actual};
}

void add_optional_string(std::vector<MetadataField>& fields, std::uint16_t tag,
                         const std::optional<std::string>& value) {
  if (value.has_value()) {
    fields.push_back({tag, 5, 0, *value});
  }
}

[[nodiscard]] std::vector<MetadataField> plan_metadata(const GameMetadata& metadata) {
  std::vector<MetadataField> fields;
  add_optional_string(fields, 0x0001, metadata.red_player.name);
  add_optional_string(fields, 0x0002, metadata.black_player.name);
  add_optional_string(fields, 0x0003, metadata.red_player.id);
  add_optional_string(fields, 0x0004, metadata.black_player.id);
  add_optional_string(fields, 0x0005, metadata.red_player.country);
  add_optional_string(fields, 0x0006, metadata.black_player.country);
  if (metadata.red_player.rating.has_value()) {
    fields.push_back({0x0007, 2, 0,
                      std::bit_cast<std::uint32_t>(*metadata.red_player.rating)});
  }
  if (metadata.black_player.rating.has_value()) {
    fields.push_back({0x0008, 2, 0,
                      std::bit_cast<std::uint32_t>(*metadata.black_player.rating)});
  }
  add_optional_string(fields, 0x0009, metadata.red_player.title);
  add_optional_string(fields, 0x000a, metadata.black_player.title);
  add_optional_string(fields, 0x000b, metadata.red_player.team);
  add_optional_string(fields, 0x000c, metadata.black_player.team);
  add_optional_string(fields, 0x000d, metadata.red_player.time_used);
  add_optional_string(fields, 0x000e, metadata.black_player.time_used);
  add_optional_string(fields, 0x0010, metadata.event.name);
  add_optional_string(fields, 0x0011, metadata.event.id);
  add_optional_string(fields, 0x0012, metadata.event.location);
  add_optional_string(fields, 0x0013, metadata.event.organizer);
  add_optional_string(fields, 0x0014, metadata.event.round);
  add_optional_string(fields, 0x0015, metadata.event.type);
  add_optional_string(fields, 0x0016, metadata.event.group);
  add_optional_string(fields, 0x0017, metadata.event.board_number);
  add_optional_string(fields, 0x0018, metadata.event.time_control);
  add_optional_string(fields, 0x0020, metadata.event.start_time);
  add_optional_string(fields, 0x0021, metadata.event.end_time);
  if (metadata.event.date_precision.has_value()) {
    fields.push_back(
        {0x0022, 1, 0, static_cast<std::uint32_t>(*metadata.event.date_precision)});
  }
  if (metadata.result.has_value()) {
    fields.push_back({0x0030, 1, 0, static_cast<std::uint32_t>(*metadata.result)});
  }
  add_optional_string(fields, 0x0031, metadata.result_text);
  add_optional_string(fields, 0x0040, metadata.opening.name);
  add_optional_string(fields, 0x0041, metadata.opening.code);
  add_optional_string(fields, 0x0042, metadata.opening.id);
  add_optional_string(fields, 0x0050, metadata.title);

  std::vector<std::string> tags = metadata.tags;
  std::ranges::sort(tags, UnsignedStringLess{});
  tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
  for (auto& tag : tags) {
    fields.push_back({0x0051, 5, 2, std::move(tag)});
  }
  add_optional_string(fields, 0x0052, metadata.game_type);
  add_optional_string(fields, 0x0060, metadata.referee);
  add_optional_string(fields, 0x0061, metadata.recorder);
  add_optional_string(fields, 0x0062, metadata.commentator);
  add_optional_string(fields, 0x0063, metadata.commentator_uri);
  add_optional_string(fields, 0x0064, metadata.creator);
  add_optional_string(fields, 0x0065, metadata.creator_uri);
  add_optional_string(fields, 0x0066, metadata.record_created_at);
  add_optional_string(fields, 0x0067, metadata.record_modified_at);
  add_optional_string(fields, 0x0100, metadata.provenance.source_format);
  add_optional_string(fields, 0x0101, metadata.provenance.source_record_id);
  add_optional_string(fields, 0x0102, metadata.provenance.source_uri);
  add_optional_string(fields, 0x0103, metadata.provenance.import_note);
  add_optional_string(fields, 0x0104, metadata.provenance.source_format_version);
  add_optional_string(fields, 0x0105, metadata.provenance.source_library_id);
  add_optional_string(fields, 0x0106, metadata.provenance.source_library_name);
  add_optional_string(fields, 0x0107, metadata.provenance.source_category);
  if (!metadata.extensions.empty()) {
    fields.push_back(
        {0x7fff, 5, 0, detail::serialize_restricted_metadata_json(metadata.extensions)});
  }
  std::ranges::stable_sort(fields, {}, &MetadataField::tag);
  return fields;
}

void collect_strings(const GameModel& game, const std::vector<MetadataField>& metadata,
                     std::map<std::string, std::uint32_t, UnsignedStringLess>& strings) {
  for (const auto& field : metadata) {
    if (std::holds_alternative<std::string>(field.value)) {
      strings.emplace(std::get<std::string>(field.value), 0);
    }
  }
  for (const auto& node : game.move_tree.nodes) {
    for (const auto& annotation : node.annotations) {
      strings.emplace(annotation.text, 0);
      if (annotation.author.has_value()) {
        strings.emplace(*annotation.author, 0);
      }
      if (annotation.language.has_value()) {
        strings.emplace(*annotation.language, 0);
      }
    }
  }
}

[[nodiscard]] std::variant<PlannedTree, WriterError> plan_tree(
    const MoveTree& tree, const WriterLimits& limits) {
  PlannedTree result;
  result.new_index.resize(tree.nodes.size());
  result.ply.resize(tree.nodes.size());
  result.next_sibling.assign(tree.nodes.size(), kNoNode);
  result.first_annotation.resize(tree.nodes.size());

  struct Pending {
    std::size_t node{};
    std::uint32_t ply{};
  };
  std::vector<Pending> stack{{0, 0}};
  while (!stack.empty()) {
    const auto current = stack.back();
    stack.pop_back();
    result.new_index[current.node] = result.order.size();
    result.ply[current.node] = current.ply;
    result.order.push_back(current.node);
    const auto& children = tree.nodes[current.node].children;
    for (auto child = children.rbegin(); child != children.rend(); ++child) {
      if (current.ply == std::numeric_limits<std::uint32_t>::max()) {
        return failure(WriterErrorCode::integer_overflow, "Move Tree ply exceeds u32");
      }
      stack.push_back({*child, current.ply + 1U});
    }
  }

  for (std::size_t parent = 0; parent < tree.nodes.size(); ++parent) {
    const auto& children = tree.nodes[parent].children;
    for (std::size_t index = 0; index + 1 < children.size(); ++index) {
      result.next_sibling[children[index]] =
          static_cast<std::uint32_t>(result.new_index[children[index + 1]]);
    }
  }
  std::size_t annotation_id = 1;
  for (const auto old_index : result.order) {
    const auto count = tree.nodes[old_index].annotations.size();
    if (count != 0) {
      if (annotation_id > std::numeric_limits<std::uint32_t>::max()) {
        return failure(WriterErrorCode::integer_overflow, "Annotation ID exceeds u32");
      }
      result.first_annotation[old_index] = static_cast<std::uint32_t>(annotation_id);
      if (count > std::numeric_limits<std::size_t>::max() - annotation_id) {
        return failure(WriterErrorCode::integer_overflow, "Annotation count overflowed");
      }
      annotation_id += count;
    }
  }
  if (annotation_id - 1 > limits.model.max_annotations) {
    return failure(WriterErrorCode::resource_limit, "Annotation count limit exceeded",
                   limits.model.max_annotations, annotation_id - 1);
  }
  if (annotation_id - 1 > std::numeric_limits<std::uint32_t>::max()) {
    return failure(WriterErrorCode::integer_overflow, "Annotation count exceeds u32");
  }
  return result;
}

[[nodiscard]] std::variant<std::vector<std::byte>, WriterError> build_string_pool(
    std::map<std::string, std::uint32_t, UnsignedStringLess>& strings,
    const WriterLimits& limits) {
  if (strings.size() > limits.max_strings) {
    return failure(WriterErrorCode::resource_limit, "String count limit exceeded",
                   limits.max_strings, strings.size());
  }
  if (strings.size() > std::numeric_limits<std::uint32_t>::max()) {
    return failure(WriterErrorCode::integer_overflow, "String count exceeds u32");
  }
  Bytes output;
  output.u16(1);
  output.u16(8);
  output.u32(static_cast<std::uint32_t>(strings.size()));
  for (auto& [value, reference] : strings) {
    if (output.size() > std::numeric_limits<std::uint32_t>::max() ||
        value.size() > std::numeric_limits<std::uint32_t>::max()) {
      return failure(WriterErrorCode::integer_overflow,
                     "STRING_POOL reference or string length exceeds u32");
    }
    reference = static_cast<std::uint32_t>(output.size());
    output.u32(static_cast<std::uint32_t>(value.size()));
    output.append(value);
    output.pad(4);
    if (output.size() > limits.max_total_string_bytes) {
      return failure(WriterErrorCode::resource_limit, "STRING_POOL byte limit exceeded",
                     limits.max_total_string_bytes, output.size());
    }
  }
  return std::move(output).take();
}

[[nodiscard]] std::vector<std::byte> build_metadata(
    const std::vector<MetadataField>& fields,
    const std::map<std::string, std::uint32_t, UnsignedStringLess>& strings) {
  Bytes output;
  output.u16(1);
  output.u16(8);
  output.u32(static_cast<std::uint32_t>(fields.size()));
  for (const auto& field : fields) {
    output.u16(field.tag);
    output.u8(field.value_type);
    output.u8(field.flags);
    output.u32(4);
    if (std::holds_alternative<std::string>(field.value)) {
      output.u32(strings.at(std::get<std::string>(field.value)));
    } else {
      output.u32(std::get<std::uint32_t>(field.value));
    }
  }
  return std::move(output).take();
}

[[nodiscard]] std::vector<std::byte> build_position(const Position& position) {
  std::vector<Piece> pieces = position.pieces;
  std::ranges::sort(pieces, {}, &Piece::square);
  Bytes output;
  output.u16(1);
  output.u16(16);
  output.u16(1);
  output.u8(static_cast<std::uint8_t>(position.side_to_move));
  output.u8(0);
  output.u16(position.fullmove_number);
  output.u16(static_cast<std::uint16_t>(pieces.size()));
  output.u32(0);
  for (const auto& piece : pieces) {
    output.u8(static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(piece.type) |
        static_cast<std::uint8_t>(static_cast<std::uint8_t>(piece.side) << 7U)));
    output.u8(piece.square);
    output.u16(0);
  }
  return std::move(output).take();
}

[[nodiscard]] std::vector<std::byte> build_move_tree(const MoveTree& tree,
                                                     const PlannedTree& plan) {
  Bytes output;
  output.u16(1);
  output.u16(16);
  output.u16(32);
  output.u16(0);
  output.u32(static_cast<std::uint32_t>(tree.nodes.size()));
  output.u32(0);
  for (const auto old_index : plan.order) {
    const auto& node = tree.nodes[old_index];
    output.u32(node.parent.has_value()
                   ? static_cast<std::uint32_t>(plan.new_index[*node.parent])
                   : kNoNode);
    output.u32(node.children.empty()
                   ? kNoNode
                   : static_cast<std::uint32_t>(plan.new_index[node.children.front()]));
    output.u32(plan.next_sibling[old_index]);
    output.u32(plan.first_annotation[old_index]);
    if (node.move.has_value()) {
      output.u8(node.move->from_square);
      output.u8(node.move->to_square);
      output.u8(0);
      output.u8(0);
    } else {
      output.u32(kNoNode);
    }
    output.u32(plan.ply[old_index]);
    output.u32(0);
    output.u32(0);
  }
  return std::move(output).take();
}

[[nodiscard]] std::vector<std::byte> build_annotations(
    const MoveTree& tree, const PlannedTree& plan,
    const std::map<std::string, std::uint32_t, UnsignedStringLess>& strings) {
  std::size_t count = 0;
  for (const auto& node : tree.nodes) {
    count += node.annotations.size();
  }
  Bytes output;
  output.u16(1);
  output.u16(16);
  output.u16(24);
  output.u16(0);
  output.u32(static_cast<std::uint32_t>(count));
  output.u32(0);
  std::uint32_t id = 1;
  for (const auto old_index : plan.order) {
    const auto& annotations = tree.nodes[old_index].annotations;
    for (std::size_t index = 0; index < annotations.size(); ++index) {
      const auto& annotation = annotations[index];
      output.u32(id);
      output.u32(index + 1 < annotations.size() ? id + 1U : 0U);
      output.u16(static_cast<std::uint16_t>(annotation.kind) + 1U);
      output.u16(annotation.before_move ? 1U : 0U);
      output.u32(strings.at(annotation.text));
      output.u32(annotation.author.has_value() ? strings.at(*annotation.author) : 0U);
      output.u32(annotation.language.has_value() ? strings.at(*annotation.language) : 0U);
      ++id;
    }
  }
  return std::move(output).take();
}

void write_u16(std::vector<std::byte>& output, std::size_t offset, std::uint16_t value) {
  output[offset] = static_cast<std::byte>(value & 0xffU);
  output[offset + 1] = static_cast<std::byte>(value >> 8U);
}

void write_u32(std::vector<std::byte>& output, std::size_t offset, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    output[offset + shift / 8] = static_cast<std::byte>((value >> shift) & 0xffU);
  }
}

void write_u64(std::vector<std::byte>& output, std::size_t offset, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    output[offset + shift / 8] = static_cast<std::byte>((value >> shift) & 0xffU);
  }
}

[[nodiscard]] std::variant<std::vector<std::byte>, WriterError> build_container(
    const Uuid& uuid, std::array<Section, 5> sections, const WriterLimits& limits) {
  std::size_t table_size = 0;
  std::size_t next_offset = 0;
  if (!detail::checked_multiply(sections.size(), kSectionEntrySize, table_size) ||
      !detail::checked_add(kHeaderSize, table_size, next_offset)) {
    return failure(WriterErrorCode::integer_overflow, "Section Table size overflowed");
  }
  next_offset += (8 - next_offset % 8) % 8;
  for (auto& section : sections) {
    section.offset = next_offset;
    section.crc = detail::crc32c(section.payload);
    if (!detail::checked_add(next_offset, section.payload.size(), next_offset)) {
      return failure(WriterErrorCode::integer_overflow, "OXQ file size overflowed");
    }
    const std::size_t padding = (8 - next_offset % 8) % 8;
    if (!detail::checked_add(next_offset, padding, next_offset)) {
      return failure(WriterErrorCode::integer_overflow, "OXQ padding size overflowed");
    }
  }
  if (next_offset > limits.max_file_size) {
    return failure(WriterErrorCode::resource_limit, "OXQ file size limit exceeded",
                   limits.max_file_size, next_offset);
  }

  std::vector<std::byte> output(next_offset, std::byte{0});
  for (std::size_t index = 0; index < kMagic.size(); ++index) {
    output[index] = static_cast<std::byte>(kMagic[index]);
  }
  write_u16(output, 0x08, 1);
  write_u16(output, 0x0a, 0);
  write_u32(output, 0x0c, kHeaderSize);
  write_u64(output, 0x10, output.size());
  write_u64(output, 0x18, kHeaderSize);
  write_u32(output, 0x20, sections.size());
  for (std::size_t index = 0; index < uuid.bytes.size(); ++index) {
    output[0x28 + index] = static_cast<std::byte>(uuid.bytes[index]);
  }

  for (std::size_t index = 0; index < sections.size(); ++index) {
    const auto& section = sections[index];
    const std::size_t entry = kHeaderSize + index * kSectionEntrySize;
    write_u32(output, entry, section.type);
    write_u32(output, entry + 4, 1);
    write_u64(output, entry + 8, section.offset);
    write_u64(output, entry + 16, section.payload.size());
    write_u64(output, entry + 24, section.payload.size());
    write_u32(output, entry + 32, section.crc);
    std::ranges::copy(section.payload, output.begin() +
                                           static_cast<std::ptrdiff_t>(section.offset));
  }
  write_u32(output, 0x38,
            detail::crc32c(std::span<const std::byte>{output}.subspan(kHeaderSize, table_size)));
  write_u32(output, 0x3c,
            detail::crc32c(std::span<const std::byte>{output}.first(0x3c)));
  return output;
}

}  // namespace

WriterOutcome write_oxq(const GameModel& game, const WriterLimits& limits) {
  auto issues = validate(game, limits.model);
  if (has_errors(issues)) {
    WriterError error;
    error.code = WriterErrorCode::invalid_model;
    error.message = "GameModel validation failed";
    error.validation_issues = std::move(issues);
    return error;
  }

  auto metadata = plan_metadata(game.metadata);
  if (metadata.size() > limits.max_metadata_fields) {
    return failure(WriterErrorCode::resource_limit, "Metadata Field count limit exceeded",
                   limits.max_metadata_fields, metadata.size());
  }
  if (metadata.size() > std::numeric_limits<std::uint32_t>::max()) {
    return failure(WriterErrorCode::integer_overflow, "Metadata Field count exceeds u32");
  }
  if (game.move_tree.nodes.size() > std::numeric_limits<std::uint32_t>::max()) {
    return failure(WriterErrorCode::integer_overflow, "Move Tree node count exceeds u32");
  }
  if (!game.metadata.extensions.empty() &&
      std::get<std::string>(metadata.back().value).size() >
          limits.max_extended_metadata_bytes) {
    return failure(WriterErrorCode::resource_limit,
                   "EXTENDED_METADATA JSON byte limit exceeded",
                   limits.max_extended_metadata_bytes,
                   std::get<std::string>(metadata.back().value).size());
  }

  std::map<std::string, std::uint32_t, UnsignedStringLess> strings;
  collect_strings(game, metadata, strings);
  auto string_pool_result = build_string_pool(strings, limits);
  if (std::holds_alternative<WriterError>(string_pool_result)) {
    return std::get<WriterError>(std::move(string_pool_result));
  }
  auto tree_plan_result = plan_tree(game.move_tree, limits);
  if (std::holds_alternative<WriterError>(tree_plan_result)) {
    return std::get<WriterError>(std::move(tree_plan_result));
  }
  const auto& tree_plan = std::get<PlannedTree>(tree_plan_result);

  std::array<Section, 5> sections{
      Section{1, build_metadata(metadata, strings)},
      Section{2, build_position(game.initial_position)},
      Section{3, build_move_tree(game.move_tree, tree_plan)},
      Section{4, build_annotations(game.move_tree, tree_plan, strings)},
      Section{5, std::get<std::vector<std::byte>>(std::move(string_pool_result))},
  };
  return build_container(game.uuid, std::move(sections), limits);
}

}  // namespace oxq::core
